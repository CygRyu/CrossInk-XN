#include <BuiltinPack.h>
#include <Exploration.h>
#include <GameState.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <LifeLog.h>
#include <Rivals.h>
#include <Rng.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

// v0.2.9 (agreed with the user in session 12, Guide and content sweep in
// session 13): low HP (wounded, collapse), realm rumors, life-log lists for
// Nascent Soul and later, the Sacred Step.
using namespace cultivation;
using explore::Enemy;
using explore::Wound;

namespace {

GameState explorer(const uint8_t realm = 1, const uint16_t hpMax = 100) {
  GameState s;
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.destiny.element = Element::Fire;
  d.origin = Origin::StreetOrphan;
  d.worldSeed = 0x5EED1234;
  StatsSample now;
  now.today = 9802;
  createCharacter(s, d, now);
  s.progress.realm = realm;
  s.progress.hpMax = hpMax;
  s.progress.hp = hpMax;
  return s;
}

// A random encounter waiting on region 0's map, the player one step from the entrance.
explore::Layout waitingBeast(GameState& s) {
  const explore::Layout l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  s.exploration[4] = 1;
  s.exploration[5] = static_cast<uint8_t>(explore::FightKind::Beast);
  s.exploration[7] = 0;
  return l;
}

struct Pack {
  std::string text;
  std::vector<PackSection> storage;
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;
  Pack() : text(builtinPackText(), builtinPackSize()), storage(512) {
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    PackParser parser(index, 0);
    parsePackText(text.data(), text.size(), parser);
    reader.setSource(0, text.data(), text.size());
    index.applyReplacements();
    grammar = std::make_unique<Grammar>(index, reader);
  }
};

}  // namespace

// --- Low HP ------------------------------------------------------------------------------

TEST(Wounds, ThresholdsAreThirtyAndTenPercent) {
  GameState s = explorer(1, 120);
  s.progress.hp = 36;  // exactly 30%
  EXPECT_EQ(explore::woundOf(s), Wound::None);
  s.progress.hp = 35;
  EXPECT_EQ(explore::woundOf(s), Wound::Wounded);
  s.progress.hp = 12;  // exactly 10%
  EXPECT_EQ(explore::woundOf(s), Wound::Wounded);
  s.progress.hp = 11;
  EXPECT_EQ(explore::woundOf(s), Wound::Badly);
  s.progress.hp = 0;
  EXPECT_EQ(explore::woundOf(s), Wound::Badly);
  EXPECT_EQ(explore::woundLevels(Wound::None), 0);
  EXPECT_EQ(explore::woundLevels(Wound::Wounded), 3);
  EXPECT_EQ(explore::woundLevels(Wound::Badly), 6);
  EXPECT_EQ(explore::woundSlipCut(Wound::Wounded), 15);
  EXPECT_EQ(explore::woundSlipCut(Wound::Badly), 30);
}

TEST(Wounds, AFightIsThreeAndSixLevelsHarder) {
  GameState s = explorer();
  const explore::Layout l = waitingBeast(s);
  const explore::Fight whole = explore::pendingFight(s, l);
  ASSERT_NE(whole.kind, explore::FightKind::None);
  EXPECT_EQ(whole.wound, Wound::None);
  const int d = explore::playerLevel(s, whole.enemy == Enemy::Cultivator) - whole.level;
  s.progress.hp = 25;
  const explore::Fight hurt = explore::pendingFight(s, l);
  EXPECT_EQ(hurt.wound, Wound::Wounded);
  EXPECT_EQ(hurt.chance, explore::winChance(d - 3));
  EXPECT_EQ(hurt.danger, explore::dangerFor(d - 3));
  s.progress.hp = 5;
  const explore::Fight badly = explore::pendingFight(s, l);
  EXPECT_EQ(badly.chance, explore::winChance(d - 6));
  if (whole.canSlip && whole.slipChance < 100) {
    EXPECT_EQ(hurt.slipChance, whole.slipChance - 15);
    EXPECT_EQ(badly.slipChance, whole.slipChance - 30);
  }
}

TEST(Wounds, SlippingAwayIsHarderEvenWithMovement) {
  GameState s = explorer();
  // An equipped Movement technique makes slipping certain while unhurt.
  scrolls::Scroll sc;
  sc.titleHash = 77;
  sc.hours = 5;
  sc.kind = scrolls::Kind::Movement;
  sc.comprehension = 90;
  sc.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::put(s, 0, sc);
  s.techniqueScrolls[0] = 1;
  const explore::Layout l = waitingBeast(s);
  const explore::Fight whole = explore::pendingFight(s, l);
  if (!whole.canSlip) GTEST_SKIP() << "a spirit";
  EXPECT_EQ(whole.slipChance, 100);
  s.progress.hp = 20;
  EXPECT_EQ(explore::pendingFight(s, l).slipChance, 85);
  s.progress.hp = 9;
  EXPECT_EQ(explore::pendingFight(s, l).slipChance, 70);
}

TEST(Wounds, RankingBoutsAndTheRivalSparFollowTheWound) {
  GameState s = explorer(1, 100);
  s.snapshot.lastVisitDay = 9802;
  const rivals::Bout whole = rivals::challengeBout(s);
  ASSERT_NE(whole.block, rivals::Block::NoLadder);  // blocked without a date, but the numbers are there
  s.progress.hp = 20;
  const rivals::Bout hurt = rivals::challengeBout(s);
  EXPECT_EQ(hurt.wound, Wound::Wounded);
  const int d = explore::playerLevel(s, true) - whole.who.level;
  EXPECT_EQ(hurt.chance, explore::winChance(d - 3));
  EXPECT_EQ(hurt.hpCost, whole.hpCost);  // the cost stays 10% of max HP (user)
}

TEST(Wounds, AMeetingChallengeUsesTheWoundButWhoDaresDoesNot) {
  for (uint32_t seed = 1; seed < 200; ++seed) {
    GameState s = explorer(2, 100);
    const explore::Layout l = explore::layoutOf(s, 0);
    explore::enter(s, l, 0);
    s.exploration[0] = static_cast<uint8_t>(seed);  // the step counter picks the stranger
    s.exploration[4] = 1;
    s.exploration[5] = static_cast<uint8_t>(explore::FightKind::Meeting);
    s.exploration[7] = 0;
    const explore::Meeting whole = explore::pendingMeeting(s, l);
    s.progress.hp = 5;
    const explore::Meeting hurt = explore::pendingMeeting(s, l);
    ASSERT_TRUE(hurt.active);
    EXPECT_EQ(hurt.wound, Wound::Badly);
    EXPECT_EQ(hurt.dares, whole.dares);
    EXPECT_EQ(hurt.offer, whole.offer);
    const int d = explore::playerLevel(s, true) - whole.level;
    EXPECT_EQ(hurt.chance, explore::winChance(d - 6));
  }
}

TEST(Wounds, ALossThatTakesTheLastHpIsACollapse) {
  int collapses = 0;
  int ordinary = 0;
  for (uint32_t seed = 1; seed < 400 && (collapses == 0 || ordinary == 0); ++seed) {
    GameState s = explorer(1, 100);
    const explore::Layout l = waitingBeast(s);
    s.exploration[0] = static_cast<uint8_t>(seed);
    s.exploration[1] = static_cast<uint8_t>(seed >> 8);
    const bool low = (seed & 1) != 0;
    s.progress.hp = low ? 1 : 100;
    const uint32_t stones = s.progress.stones;
    const uint32_t herbs = 0;
    const explore::Fight f = explore::pendingFight(s, l);
    if (f.enemy == Enemy::Spirit || f.damage >= 100) continue;
    const explore::FightResult r = explore::fight(s, l, explore::FightChoice::Fight);
    if (r.won) continue;
    if (low) {
      EXPECT_TRUE(r.collapsed);
      EXPECT_EQ(s.progress.hp, 0);
      EXPECT_EQ(r.thrownTo, l.entrance);
      EXPECT_EQ(explore::position(s, l, 0), l.entrance);
      EXPECT_EQ(s.progress.stones, stones);  // nothing owned is lost
      EXPECT_EQ(herbs, 0u);
      ++collapses;
    } else {
      EXPECT_FALSE(r.collapsed);
      EXPECT_GT(s.progress.hp, 0);
      ++ordinary;
    }
  }
  EXPECT_GT(collapses, 0);
  EXPECT_GT(ordinary, 0);
}

TEST(Wounds, TheMapStaysOpenWhileWoundedAndClosesAtZero) {
  GameState s = explorer(1, 100);
  const explore::Layout l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  uint8_t n[4];
  const uint8_t here = explore::position(s, l, 0);
  uint8_t to = here;
  for (uint8_t t = 0; t < explore::kTiles; ++t) {
    const int dr = t / 6 - here / 6;
    const int dc = t % 6 - here % 6;
    if ((dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1))) {
      if (l.tiles[t] != explore::Tile::Blocked) {
        to = t;
        break;
      }
    }
  }
  (void)n;
  ASSERT_NE(to, here);
  s.progress.stamina = 5;
  s.progress.hp = 5;
  EXPECT_EQ(explore::canStep(s, l, 0, to), explore::StepError::None);
  s.progress.hp = 0;
  EXPECT_EQ(explore::canStep(s, l, 0, to), explore::StepError::NoHp);
}

// --- Content sweep -------------------------------------------------------------------------

TEST(RealmRumors, EveryRealmListExpandsCleanly) {
  Pack pack;
  GrammarContext ctx;
  for (const char* list : {"rumor.realm1", "rumor.realm2", "rumor.realm3", "rumor.realm4"}) {
    ASSERT_TRUE(pack.grammar->hasKey(list, ctx)) << list;
    EXPECT_GE(pack.grammar->entryCount(list, ctx), 8u) << list;
    const std::string pattern = std::string("{rumor.opening} {") + list + "}.";
    std::set<std::string> distinct;
    for (uint32_t day = 0; day < 4000; ++day) {
      pack.grammar->beginScene();
      const std::string r = pack.grammar->expand(pattern.c_str(), contentSeed(1234, 50, day), ctx);
      EXPECT_EQ(pack.grammar->missing(), 0) << r;
      EXPECT_EQ(r.find_first_of("{}[]|$\\"), std::string::npos) << r;
      EXPECT_TRUE(r.front() >= 'A' && r.front() <= 'Z') << r;
      EXPECT_EQ(r.back(), '.') << r;
      EXPECT_LT(r.size(), 200u) << list << ": " << r;
      distinct.insert(r);
    }
    EXPECT_GT(distinct.size(), 60u) << list;
  }
}

TEST(RealmRumors, AboutOneDayInThree) {
  int realmDays = 0;
  for (uint32_t day = 0; day < 3000; ++day) realmDays += Rng(contentSeed(0x5EED1234, 129, day)).below(3) == 0 ? 1 : 0;
  EXPECT_NEAR(realmDays, 1000, 90);
}

TEST(LateLifeLog, NascentSoulAndLaterHaveTheirOwnLists) {
  Pack pack;
  GrammarContext ctx;
  for (const char* list : {"log.nascent", "log.late"}) {
    ASSERT_TRUE(pack.grammar->hasKey(list, ctx)) << list;
    EXPECT_GE(pack.grammar->entryCount(list, ctx), 12u) << list;
  }
  for (const uint8_t realm : {4, 5, 9}) {
    GameState s = explorer(realm);
    std::set<std::string> lists;
    for (uint32_t season = 64; season < 400; ++season) {
      const lifelog::SeasonPick p = lifelog::seasonPick(s, season, *pack.grammar, ctx);
      if (p.key) lists.insert(p.key);
    }
    EXPECT_TRUE(lists.count(realm == 4 ? "log.nascent" : "log.late")) << static_cast<int>(realm);
    EXPECT_TRUE(lists.count("log.any")) << static_cast<int>(realm);
    EXPECT_FALSE(lists.count(realm == 4 ? "log.late" : "log.nascent"));
  }
}

TEST(LateLifeLog, LinesExpandCleanly) {
  Pack pack;
  GrammarContext ctx;
  GameState s = explorer(5);
  for (const char* list : {"log.nascent", "log.late"}) {
    const uint32_t n = pack.grammar->entryCount(list, ctx);
    for (uint32_t i = 0; i < n; ++i) {
      lifelog::Line line;
      pack.grammar->beginScene();
      ASSERT_TRUE(lifelog::eventLine(line, lifelog::LineKind::Event, list, i, s, *pack.grammar)) << list << " " << i;
      EXPECT_EQ(std::string(line.text).find_first_of("{}[]|$"), std::string::npos) << line.text;
      EXPECT_LT(strlen(line.text), lifelog::kLineSize - 1) << line.text;
    }
  }
}

TEST(SacredStep, OnceAfterReadingAPerfectedBookAgain) {
  GameState s = explorer();
  scrolls::BookRecord book;
  book.titleHash = 4242;
  book.seconds = 10u * 3600u;
  book.bucket = 2;
  book.shareRead = 99;
  scrolls::Event ev[2];
  ASSERT_EQ(scrolls::mint(s, book, false, ev), 1);
  ASSERT_EQ(ev[0].kind, scrolls::EventKind::Minted);
  EXPECT_EQ(scrolls::qualityFor(scrolls::get(s, 0).comprehension), scrolls::Quality::Perfected);
  // Marked finished again without reading more: nothing.
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Unchanged);
  // Four more hours is under half of ten: still nothing.
  book.seconds = 14u * 3600u;
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Unchanged);
  // Read again (five more hours) and finished at 98% or more: Sacred.
  book.seconds = 15u * 3600u;
  book.shareRead = 100;
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Sacred);
  const scrolls::Scroll sc = scrolls::get(s, 0);
  EXPECT_EQ(sc.comprehension, scrolls::kSacredComprehension);
  EXPECT_EQ(scrolls::qualityFor(sc.comprehension), scrolls::Quality::Sacred);
  EXPECT_EQ(sc.hours, 10);  // hours and tier never change
  // Only once.
  book.seconds = 40u * 3600u;
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Unchanged);
}

TEST(SacredStep, OnlyFromPerfected) {
  GameState s = explorer();
  scrolls::BookRecord book;
  book.titleHash = 99;
  book.seconds = 4u * 3600u;
  book.shareRead = 92;  // Refined
  scrolls::Event ev[2];
  scrolls::mint(s, book, false, ev);
  book.seconds = 20u * 3600u;
  book.shareRead = 100;
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Improved);  // first up to Perfected
  EXPECT_EQ(scrolls::get(s, 0).comprehension, 100);
  // Finished again: the book was read sixteen hours more than when the scroll
  // was made (more than half its 4 hours), so the Step comes now.
  scrolls::mint(s, book, true, ev);
  EXPECT_EQ(ev[0].kind, scrolls::EventKind::Sacred);
}

TEST(SacredStep, AManualGainsOneQualityStep) {
  scrolls::Scroll sc;
  sc.kind = scrolls::Kind::Manual;
  sc.hours = 1;  // Mortal, x1.15
  sc.comprehension = 100;
  EXPECT_EQ(scrolls::manualPermille(sc), 1150u * 1100u / 1000u);
  sc.comprehension = scrolls::kSacredComprehension;
  EXPECT_EQ(scrolls::manualPermille(sc), 1150u * 1150u / 1000u);
  // Strength (for keys and auto-selling) puts it above a Perfected one of its tier.
  scrolls::Scroll perfected = sc;
  perfected.comprehension = 100;
  EXPECT_GT(scrolls::strength(sc), scrolls::strength(perfected));
}

TEST(SacredStep, StoredInTheSaveAsIs) {
  GameState s = explorer();
  scrolls::Scroll sc;
  sc.titleHash = 5;
  sc.hours = 3;
  sc.comprehension = scrolls::kSacredComprehension;
  scrolls::put(s, 0, sc);
  s.techniqueScrolls[0] = 1;
  EXPECT_EQ(scrolls::get(s, 0).comprehension, scrolls::kSacredComprehension);
}

TEST(V029Pack, StillFitsTheDeviceIndex) {
  Pack pack;
  EXPECT_LT(pack.index.count, 320u);
}
