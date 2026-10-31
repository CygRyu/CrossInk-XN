#include <Alchemy.h>
#include <Breakthrough.h>
#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <Inventory.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

// v0.2.1 (agreed with the user): depth, the encounter cap, returning foes,
// the burned scroll remembered for Heart Demons.
using namespace cultivation;
using namespace cultivation::explore;
using inventory::Item;

namespace {

GameState makeCharacter(const uint8_t realm = 1, const uint32_t worldSeed = 0x5EED1234) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = worldSeed;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.realm = realm;
  s.progress.stage = 0;
  s.progress.hpMax = 100;
  s.progress.hp = 100;
  s.progress.stamina = 12;
  return s;
}

void atPeak(GameState& s, const uint8_t realm) {
  s.progress.realm = realm;
  s.progress.stage = static_cast<uint8_t>(realmInfo(realm).stages - 1);
  s.progress.qi = stageCost(realm, s.progress.stage);
}

VisitGains reading(const uint32_t seconds) {
  VisitGains g;
  g.seconds = seconds;
  g.tiers.fullSeconds = seconds;
  return g;
}

uint8_t walkableNeighbour(const Layout& l, const uint8_t from, const uint32_t salt) {
  uint8_t n[4];
  const uint8_t c = neighbours(from, n);
  for (uint8_t k = 0; k < c; ++k) {
    const uint8_t t = n[(k + salt) % c];
    if (walkable(l, t) && l.tiles[t] != Tile::Lair && l.tiles[t] != Tile::Guardian) return t;
  }
  return kNoTile;
}

}  // namespace

// --- Depth ----------------------------------------------------------------------------

TEST(Depth, GrowsAtAFullBarOutsideGreatPerfection) {
  GameState s = makeCharacter(2);
  atPeak(s, 2);
  const AbsorbResult a = absorb(s, reading(3600));
  EXPECT_EQ(a.depthGained, 2);
  EXPECT_EQ(breakthrough::depth(s), 2);
  absorb(s, reading(20 * 3600));
  EXPECT_EQ(breakthrough::depth(s), breakthrough::kDepthMax) << "capped at 20";
}

TEST(Depth, NothingBeforeTheBarIsFull) {
  GameState s = makeCharacter(2);
  absorb(s, reading(1800));
  EXPECT_EQ(breakthrough::depth(s), 0);
}

TEST(Depth, NothingInGreatPerfection) {
  GameState s = makeCharacter(2);
  atPeak(s, 2);
  breakthrough::enterGreatPerfection(s);
  const AbsorbResult a = absorb(s, reading(3600));
  EXPECT_EQ(a.depthGained, 0);
  EXPECT_EQ(a.consolidationGained, 2);
}

TEST(Depth, CountsHalfAndSharesConsolidationsCaps) {
  GameState s = makeCharacter(2);
  atPeak(s, 2);
  const uint8_t gradeBefore = breakthrough::gradeScore(s, 3);
  const uint8_t chanceBefore = breakthrough::chanceFor(s).total;
  breakthrough::addDepth(s, 20);
  EXPECT_EQ(breakthrough::gradeScore(s, 3), gradeBefore + 10);
  EXPECT_EQ(breakthrough::chanceFor(s).depth, 4);
  EXPECT_EQ(breakthrough::chanceFor(s).total, chanceBefore + 4);
  s.progress.consolidation = 40;
  const breakthrough::Chance c = breakthrough::chanceFor(s);
  EXPECT_EQ(c.consolidation, 15);
  EXPECT_EQ(c.depth, 0) << "consolidation's +15 is the cap for both";
  s.progress.consolidation = 36;
  EXPECT_EQ(breakthrough::gradeScore(s, 3) - gradeBefore, 40) << "and its 40 grade points";
}

TEST(Depth, KeptOnFailureSpentOnSuccess) {
  GameState s = makeCharacter(1);
  atPeak(s, 1);
  breakthrough::addDepth(s, 12);
  inventory::add(s, inventory::breakthroughPill(inventory::Grade::Common, inventory::Quality::Low), 30);
  bool sawFailure = false;
  for (int i = 0; i < 30 && s.progress.realm == 1; ++i) {
    breakthrough::attempt(s);
    if (s.progress.realm == 1) {
      sawFailure = true;
      EXPECT_EQ(breakthrough::depth(s), 12);
      s.progress.qi = stageCost(1, s.progress.stage);
    }
  }
  ASSERT_EQ(s.progress.realm, 2);
  EXPECT_EQ(breakthrough::depth(s), 0);
  (void)sawFailure;
}

// --- Encounter cap ----------------------------------------------------------------------

TEST(Encounters, NeverTwoRandomFightsInARow) {
  int fights = 0;
  for (uint32_t world = 1; world < 30; ++world) {
    GameState s = makeCharacter(9, world * 7919u);
    const Layout l = layoutOf(s, 0);
    enter(s, l, 0);
    bool last = false;
    for (uint32_t i = 0; i < 400; ++i) {
      s.progress.stamina = 12;
      s.progress.hp = 100;
      const uint8_t to = walkableNeighbour(l, position(s, l, 0), i);
      if (to == kNoTile) break;
      const StepResult r = step(s, l, 0, to);
      const bool random = r.fight && pendingFight(s, l).kind == FightKind::Beast;
      EXPECT_FALSE(last && random) << "world " << world << " step " << i;
      last = random;
      if (r.fight) {
        fight(s, l, FightChoice::Fight);
        ++fights;
      }
    }
  }
  EXPECT_GT(fights, 100);
}

// --- Returning foes ------------------------------------------------------------------------

namespace {
// Walks region 0 until a new random beast or bandit waits; false if none in many steps.
bool nextNewEncounter(GameState& s, const Layout& l, uint32_t& salt) {
  for (int i = 0; i < 2000; ++i, ++salt) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    const uint8_t to = walkableNeighbour(l, position(s, l, 0), salt);
    if (to == kNoTile) return false;
    step(s, l, 0, to);
    const Fight f = pendingFight(s, l);
    if (f.kind == FightKind::Beast && returningFoe(s) < 0 && (f.enemy == Enemy::Beast || f.enemy == Enemy::Bandit) &&
        f.canSlip)
      return true;
    if (f.kind != FightKind::None) fight(s, l, FightChoice::Fight);
  }
  return false;
}
}  // namespace

TEST(Foes, AnEnemyYouRanFromComesBackStronger) {
  int returned = 0;
  for (uint32_t world = 1; world < 20; ++world) {
    GameState s = makeCharacter(9, world * 104729u);
    // A Movement technique: slipping away always works.
    scrolls::Scroll steps;
    steps.titleHash = 9;
    steps.kind = scrolls::Kind::Movement;
    steps.hours = 1;
    steps.flags = scrolls::Scroll::kEquippedTechnique;
    scrolls::put(s, 0, steps);
    s.techniqueScrolls[0] = 1;
    const Layout l = layoutOf(s, 0);
    enter(s, l, 0);
    uint32_t salt = 0;
    ASSERT_TRUE(nextNewEncounter(s, l, salt));
    const uint32_t seed = beastSeed(s);
    const int level = pendingFight(s, l).level;
    const FightResult ran = fight(s, l, FightChoice::Slip);
    ASSERT_TRUE(ran.slipped);
    EXPECT_TRUE(ran.remembered);
    Foe f;
    ASSERT_TRUE(foe(s, 0, f));
    EXPECT_EQ(f.region, 0);
    EXPECT_EQ(f.seed, seed);
    EXPECT_EQ(f.returns, 1);
    EXPECT_FALSE(f.paid);
    // Walk until it comes back.
    for (int i = 0; i < 4000; ++i, ++salt) {
      s.progress.stamina = 12;
      s.progress.hp = 100;
      const uint8_t to = walkableNeighbour(l, position(s, l, 0), salt);
      if (to == kNoTile) break;
      step(s, l, 0, to);
      if (pendingFight(s, l).kind == FightKind::None) continue;
      if (returningFoe(s) < 0) {
        fight(s, l, FightChoice::Fight);
        continue;
      }
      EXPECT_EQ(beastSeed(s), seed) << "the same enemy (same name)";
      EXPECT_EQ(pendingFight(s, l).level, level + 3);
      EXPECT_EQ(pendingFight(s, l).returns, 1);
      ++returned;
      // Run again: it comes back stronger still; then beat it and it is gone.
      fight(s, l, FightChoice::Slip);
      ASSERT_TRUE(foe(s, 0, f));
      EXPECT_EQ(f.returns, 2);
      s.progress.realm = 9;
      for (int k = 0; k < 4000 && foe(s, 0, f); ++k, ++salt) {
        s.progress.stamina = 12;
        s.progress.hp = 100;
        const uint8_t next = walkableNeighbour(l, position(s, l, 0), salt);
        step(s, l, 0, next);
        if (pendingFight(s, l).kind == FightKind::None) continue;
        const bool back = returningFoe(s) >= 0;
        if (back) {
          EXPECT_EQ(pendingFight(s, l).level, level + 6);
        }
        const FightResult r = fight(s, l, FightChoice::Fight);
        if (back && r.won) {
          EXPECT_TRUE(r.foeBeaten);
        }
      }
      EXPECT_FALSE(foe(s, 0, f)) << "beaten: forgotten";
      break;
    }
  }
  EXPECT_GT(returned, 10);
}

TEST(Foes, PayingOffABanditIsRememberedAsPaid) {
  GameState s = makeCharacter(6);
  s.progress.stones = 100000;
  const Layout l = layoutOf(s, 3);  // the gorge: bandits
  enter(s, l, 3);
  for (uint32_t i = 0; i < 3000; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    step(s, l, 3, walkableNeighbour(l, position(s, l, 3), i));
    const Fight f = pendingFight(s, l);
    if (f.kind == FightKind::None) continue;
    if (f.payCost > 0 && f.kind == FightKind::Beast && returningFoe(s) < 0) {
      ASSERT_TRUE(fight(s, l, FightChoice::Pay).remembered);
      Foe foe0;
      ASSERT_TRUE(foe(s, 0, foe0));
      EXPECT_TRUE(foe0.paid);
      EXPECT_EQ(foe0.region, 3);
      return;
    }
    fight(s, l, FightChoice::Fight);
  }
  FAIL() << "no bandit met";
}

TEST(Foes, TheRingKeepsTheFourLatest) {
  GameState s = makeCharacter(9);
  scrolls::Scroll steps;
  steps.titleHash = 9;
  steps.kind = scrolls::Kind::Movement;
  steps.hours = 1;
  steps.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::put(s, 0, steps);
  s.techniqueScrolls[0] = 1;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  uint32_t salt = 0;
  std::vector<uint32_t> seeds;
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(nextNewEncounter(s, l, salt));
    seeds.push_back(beastSeed(s));
    fight(s, l, FightChoice::Slip);
  }
  for (uint8_t i = 0; i < kFoeRing; ++i) {
    Foe f;
    ASSERT_TRUE(foe(s, i, f));
    EXPECT_EQ(f.seed, seeds[2 + i]);
  }
}

TEST(Foes, SpiritsAndLairsAreNotRemembered) {
  GameState s = makeCharacter(8);
  const Layout l = layoutOf(s, 7);  // the battlefield: spirits
  enter(s, l, 7);
  inventory::add(s, Item::CalmIncense, 200);
  for (uint32_t i = 0; i < 3000; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    const uint8_t here = position(s, l, 7);
    uint8_t n[4];
    const uint8_t c = neighbours(here, n);
    uint8_t to = kNoTile;
    for (uint8_t k = 0; k < c && to == kNoTile; ++k) {
      if (walkable(l, n[(k + i) % c])) to = n[(k + i) % c];
    }
    step(s, l, 7, to);
    const Fight f = pendingFight(s, l);
    if (f.kind == FightKind::None) continue;
    if (f.enemy == Enemy::Spirit) {
      EXPECT_FALSE(fight(s, l, FightChoice::Incense).remembered);
    } else if (f.kind != FightKind::Beast && f.canSlip) {
      EXPECT_FALSE(fight(s, l, FightChoice::Slip).remembered);
    } else {
      fight(s, l, FightChoice::Fight);
    }
  }
  for (uint8_t i = 0; i < kFoeRing; ++i) {
    Foe f;
    if (foe(s, i, f)) {
      EXPECT_EQ(f.region, 7);
    }
  }
}

// --- The last burned scroll ------------------------------------------------------------------

TEST(Memory, TheLastBurnedScrollIsKept) {
  GameState s = makeCharacter(2);
  scrolls::Scroll none;
  EXPECT_FALSE(scrolls::lastBurned(s, none));
  scrolls::Scroll burned;
  burned.titleHash = 0xABCD1234;
  burned.kind = scrolls::Kind::PalmFist;
  burned.hours = 12;
  burned.comprehension = 80;
  burned.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::rememberBurned(s, burned);
  scrolls::Scroll back;
  ASSERT_TRUE(scrolls::lastBurned(s, back));
  EXPECT_EQ(back.titleHash, burned.titleHash);
  EXPECT_EQ(back.hours, 12);
  EXPECT_EQ(back.kind, scrolls::Kind::PalmFist);
  EXPECT_EQ(back.flags, 0) << "flags are not kept";
}

// --- Batches and brew surprises ---------------------------------------------------------------

TEST(Brewing, BatchesUseNTimesThePartsAndTakeLonger) {
  GameState s = makeCharacter(1);
  const uint8_t recipe = static_cast<uint8_t>(alchemy::Pill::TonicMinor);
  const alchemy::Recipe r = alchemy::recipesOf(s).recipes[recipe];
  alchemy::learn(s, recipe);
  for (uint8_t p = 0; p < r.parts; ++p) inventory::add(s, r.part[p], 2);
  EXPECT_EQ(alchemy::maxBatch(s, recipe), 2);
  EXPECT_FALSE(alchemy::brew(s, recipe, 3)) << "parts for 2 only";
  ASSERT_TRUE(alchemy::brew(s, recipe, 2));
  for (uint8_t p = 0; p < r.parts; ++p) EXPECT_EQ(inventory::count(s, r.part[p]), 0);
  EXPECT_EQ(alchemy::brewingBatch(s), 2);
  EXPECT_EQ(alchemy::brewMinutesLeft(s), 30u) << "20 minutes x 1.5";
  EXPECT_EQ(alchemy::batchMinutes(r, 3), 40u) << "x 2";
  s.progress.gameReadingSeconds += 30 * 60;
  const alchemy::Collected c = alchemy::collect(s);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.batch, 2);
  if (!c.failed) {
    const uint16_t each = static_cast<uint16_t>(static_cast<uint8_t>(c.quality) + 1);
    EXPECT_EQ(c.count, each * 2 + (c.extra ? 1 : 0));
  }
  EXPECT_EQ(alchemy::brewingBatch(s), 1) << "cleared";
}

TEST(Brewing, SurprisesComeAtTheAgreedRates) {
  int failed = 0, extra = 0, improved = 0, total = 0;
  for (uint32_t world = 1; world <= 3000; ++world) {
    GameState s = makeCharacter(1, world);
    const uint8_t recipe = static_cast<uint8_t>(alchemy::Pill::BreakthroughCommon);
    const alchemy::Recipe r = alchemy::recipesOf(s).recipes[recipe];
    alchemy::learn(s, recipe);
    for (uint8_t p = 0; p < r.parts; ++p) inventory::add(s, r.part[p]);
    ASSERT_TRUE(alchemy::brew(s, recipe));
    s.progress.gameReadingSeconds += 60 * 60;
    const alchemy::Collected c = alchemy::collect(s);
    ASSERT_TRUE(c.ok);
    ++total;
    if (c.failed) {
      ++failed;
      EXPECT_EQ(c.item, Item::Dross);
      EXPECT_GE(c.dross, 1);
      EXPECT_EQ(inventory::count(s, Item::Dross), c.dross);
      // Worth half the ingredients when sold.
      uint32_t value = 0;
      for (uint8_t p = 0; p < r.parts; ++p) value += inventory::price(r.part[p]);
      EXPECT_NEAR(static_cast<double>(c.dross * inventory::sellPrice(Item::Dross)), value / 2.0, 5.0);
      continue;
    }
    extra += c.extra ? 1 : 0;
    improved += c.improved ? 1 : 0;
    EXPECT_EQ(c.count, c.extra ? 2 : 1);
    EXPECT_TRUE(inventory::isBreakthroughPill(c.item));
  }
  EXPECT_NEAR(failed * 100.0 / total, 4.0, 1.2);
  EXPECT_NEAR(extra * 100.0 / total, 8.0, 1.8);
  EXPECT_NEAR(improved * 100.0 / total, 5.0, 1.8) << "6%, never from High";
}

TEST(Brewing, TheHerbalistFailsHalfAsOften) {
  int failed = 0;
  for (uint32_t world = 1; world <= 3000; ++world) {
    CharacterDraft d;
    snprintf(d.name, sizeof(d.name), "Wen");
    d.worldSeed = world;
    d.origin = Origin::HerbalistApprentice;
    GameState s;
    createCharacter(s, d, StatsSample{});
    s.progress.realm = 1;
    const alchemy::Recipe r = alchemy::recipesOf(s).recipes[0];
    for (uint8_t p = 0; p < r.parts; ++p) inventory::add(s, r.part[p]);
    ASSERT_TRUE(alchemy::brew(s, 0));
    s.progress.gameReadingSeconds += 3600;
    failed += alchemy::collect(s).failed ? 1 : 0;
  }
  EXPECT_NEAR(failed / 30.0, 2.0, 0.8);
}

// --- The 9,999 limit -----------------------------------------------------------------------

TEST(Bag, OrdinaryItemsPastTheLimitAreSoldOthersKept) {
  GameState s = makeCharacter(1);
  inventory::add(s, inventory::herbFor(0), 9998);
  const uint32_t stones = s.progress.stones;
  const inventory::AddResult a = inventory::addOrSell(s, inventory::herbFor(0), 5);
  EXPECT_EQ(a.added, 1);
  EXPECT_EQ(a.sold, 4);
  EXPECT_EQ(a.soldStones, 4u * 3u);
  EXPECT_EQ(s.progress.stones, stones + 12);
  inventory::add(s, Item::ThunderTalisman, 9999);
  const inventory::AddResult b = inventory::addOrSell(s, Item::ThunderTalisman, 2);
  EXPECT_EQ(b.added, 0);
  EXPECT_EQ(b.lost, 2);
  EXPECT_EQ(b.soldStones, 0u) << "prizes are never sold";
}

// --- Hard fights and bulk buying -----------------------------------------------------------

TEST(Loot, HardFightsSometimesPayWithAPrize) {
  EXPECT_EQ(prizePercent(Danger::Dangerous), 0);
  EXPECT_EQ(prizePercent(Danger::Hazardous), 15);
  EXPECT_EQ(prizePercent(Danger::Extreme), 25);
  EXPECT_EQ(prizePercent(Danger::Impossible), 35);
  int wins = 0, prizes = 0;
  for (uint32_t world = 1; world < 400; ++world) {
    GameState s = makeCharacter(1, world);  // region 0 at realm 1 is Hazardous for a fresh character? use region 2
    s.progress.realm = 3;
    s.progress.stage = 0;
    const Layout l = layoutOf(s, 2);
    enter(s, l, 2);
    for (uint32_t i = 0; i < 200; ++i) {
      s.progress.stamina = 12;
      s.progress.hp = 100;
      step(s, l, 2, walkableNeighbour(l, position(s, l, 2), i));
      const Fight f = pendingFight(s, l);
      if (f.kind == FightKind::None) continue;
      const bool hard = f.danger >= Danger::Hazardous;
      const uint32_t known = alchemy::knownCount(s);
      const FightResult r = fight(s, l, FightChoice::Fight);
      if (!r.won || !hard) continue;
      ++wins;
      const bool slip = alchemy::knownCount(s) > known;
      bool spirit = false;
      for (const Item it : {r.reward.item, r.reward.item2, r.reward.item3}) {
        if (inventory::isIngredient(it) && inventory::regionOf(it) == 3) spirit = true;
      }
      if (slip || spirit) ++prizes;
    }
  }
  ASSERT_GT(wins, 50);
  EXPECT_GT(prizes, wins / 12);
}

TEST(Merchants, HerbsAndOresComeInBulk) {
  GameState s = makeCharacter(1);
  const Layout l = layoutOf(s, 0);
  uint8_t merchant = kNoTile;
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (l.tiles[t] == Tile::Merchant) merchant = t;
  }
  ASSERT_NE(merchant, kNoTile);
  s.progress.stones = 105;
  EXPECT_EQ(affordable(s, inventory::herbFor(0)), 10);
  EXPECT_FALSE(buy(s, l, 0, merchant, 0, 11));
  EXPECT_TRUE(buy(s, l, 0, merchant, 0, 10));
  EXPECT_EQ(inventory::count(s, inventory::herbFor(0)), 10);
  EXPECT_EQ(s.progress.stones, 5u);
  EXPECT_FALSE(buy(s, l, 0, merchant, 2, 2)) << "limited lines one at a time";
  inventory::add(s, inventory::herbFor(0), 9989);
  s.progress.stones = 100000;
  EXPECT_EQ(affordable(s, inventory::herbFor(0)), 0) << "the bag holds 9,999";
}

// --- Text ---------------------------------------------------------------------------------------

TEST(Text, NewListsExpandCleanly) {
  std::string text(builtinPackText(), builtinPackSize());
  std::vector<PackSection> storage(384);
  PackIndex index;
  index.sections = storage.data();
  index.capacity = static_cast<uint16_t>(storage.size());
  PackParser parser(index, 0);
  parsePackText(text.data(), text.size(), parser);
  MemoryPackReader reader;
  reader.setSource(0, text.data(), text.size());
  index.applyReplacements();
  Grammar grammar(index, reader);
  GameState s = makeCharacter(4);
  const char* lists[] = {"demon.history.guardian", "demon.history.uncleared", "demon.history.ran",
                         "demon.history.paid",     "demon.history.burned",    "explore.fight.return",
                         "alchemy.failed",         "alchemy.extra",           "alchemy.improved"};
  for (const char* key : lists) {
    for (uint32_t seed = 0; seed < 8; ++seed) {
      char out[240];
      ASSERT_TRUE(exploretext::historyFlavor(s, key, seed, "the Reedwater Marshes", "a grey wolf",
                                             "Moon-Severing Sword Canon", grammar, out, sizeof(out)))
          << key;
      EXPECT_EQ(std::string(out).find_first_of("{}[]|$"), std::string::npos) << out;
    }
  }
  for (const char* key : {"explore.line.return.won", "explore.line.return.left"}) {
    lifelog::Line line;
    ASSERT_TRUE(exploretext::chronicleLine(line, s, key, 3, "the Reedwater Marshes", "a grey wolf", grammar)) << key;
    EXPECT_NE(std::string(line.text).find("grey wolf"), std::string::npos) << line.text;
  }
}

TEST(Depth, AQiPillWaitsWhileTheBarIsFull) {
  GameState s = makeCharacter(2);
  atPeak(s, 2);
  inventory::add(s, Item::QiPill);
  ASSERT_EQ(alchemy::use(s, Item::QiPill), alchemy::UseError::None);
  const AbsorbResult a = absorb(s, reading(1800));
  EXPECT_EQ(a.qiPillSeconds, 0u);
  EXPECT_EQ(alchemy::qiPillSecondsLeft(s), alchemy::kQiPillSeconds);
}
