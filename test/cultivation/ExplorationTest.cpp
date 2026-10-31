#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <GameValues.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <Inventory.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

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

uint8_t blockedCount(const Layout& l) {
  uint8_t n = 0;
  for (const Tile t : l.tiles) n = static_cast<uint8_t>(n + (t == Tile::Blocked ? 1 : 0));
  return n;
}

uint8_t reachable(const Layout& l, uint8_t* dist) {
  for (uint8_t i = 0; i < kTiles; ++i) dist[i] = 0xFF;
  std::vector<uint8_t> queue{l.entrance};
  dist[l.entrance] = 0;
  for (size_t h = 0; h < queue.size(); ++h) {
    uint8_t n[4];
    const uint8_t c = neighbours(queue[h], n);
    for (uint8_t i = 0; i < c; ++i) {
      if (l.tiles[n[i]] == Tile::Blocked || dist[n[i]] != 0xFF) continue;
      dist[n[i]] = static_cast<uint8_t>(dist[queue[h]] + 1);
      queue.push_back(n[i]);
    }
  }
  return static_cast<uint8_t>(queue.size());
}

// A walkable neighbour of `from` (the first found), or kNoTile.
uint8_t walkableNeighbour(const Layout& l, const uint8_t from, const uint8_t avoid = kNoTile) {
  uint8_t n[4];
  const uint8_t c = neighbours(from, n);
  for (uint8_t i = 0; i < c; ++i) {
    if (walkable(l, n[i]) && n[i] != avoid) return n[i];
  }
  return kNoTile;
}

// Walks from the player's tile to `target` over walkable tiles (BFS path),
// fighting any fight on the way with `win` forced by a strong character.
// Returns false if the walk got stuck.
bool walkTo(GameState& s, const Layout& l, const uint8_t region, const uint8_t target) {
  for (int guard = 0; guard < 200; ++guard) {
    const uint8_t here = position(s, l, region);
    if (here == target) return true;
    // BFS from target to find the neighbour of `here` closest to target.
    uint8_t dist[kTiles];
    for (auto& d : dist) d = 0xFF;
    std::vector<uint8_t> q{target};
    dist[target] = 0;
    for (size_t h = 0; h < q.size(); ++h) {
      uint8_t n[4];
      const uint8_t c = neighbours(q[h], n);
      for (uint8_t i = 0; i < c; ++i) {
        if (!walkable(l, n[i]) || dist[n[i]] != 0xFF) continue;
        dist[n[i]] = static_cast<uint8_t>(dist[q[h]] + 1);
        q.push_back(n[i]);
      }
    }
    uint8_t n[4];
    const uint8_t c = neighbours(here, n);
    uint8_t best = kNoTile;
    for (uint8_t i = 0; i < c; ++i) {
      if (walkable(l, n[i]) && (best == kNoTile || dist[n[i]] < dist[best])) best = n[i];
    }
    if (best == kNoTile) return false;
    s.progress.stamina = 12;
    s.progress.hp = s.progress.hpMax;
    step(s, l, region, best);
    while (pendingFight(s, l).kind != FightKind::None) fight(s, l, FightChoice::Fight);
  }
  return false;
}

struct BuiltinPacks {
  std::string text = std::string(builtinPackText(), builtinPackSize());
  std::vector<PackSection> storage = std::vector<PackSection>(384);
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;

  BuiltinPacks() {
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

// --- Generation ---------------------------------------------------------------------

TEST(Exploration, EveryLayoutKeepsTheAgreedShape) {
  std::set<uint8_t> rates;
  for (uint32_t world = 1; world <= 300; ++world) {
    for (uint8_t region = 0; region < kRegionCount; ++region) {
      for (uint8_t reshape = 0; reshape < 3; ++reshape) {
        const Layout l = generate(world * 2654435761u, region, reshape);
        const uint8_t blocked = blockedCount(l);
        ASSERT_GE(blocked, 8) << world << "/" << int(region);
        ASSERT_LE(blocked, 12);
        uint8_t dist[kTiles];
        ASSERT_EQ(reachable(l, dist), kTiles - blocked) << "every walkable tile is reachable";
        EXPECT_EQ(l.walkable, kTiles - blocked);
        ASSERT_GE(l.content, 15);
        ASSERT_LE(l.content, 20);
        uint8_t content = 0;
        uint8_t guardians = 0;
        for (uint8_t i = 0; i < kTiles; ++i) {
          if (l.tiles[i] != Tile::Blocked && l.tiles[i] != Tile::Path) ++content;
          if (l.tiles[i] == Tile::Guardian) {
            ++guardians;
            for (uint8_t j = 0; j < kTiles; ++j) {
              if (l.tiles[j] != Tile::Blocked) {
                EXPECT_LE(dist[j], dist[i]) << "the guardian is farthest";
              }
            }
          }
        }
        EXPECT_EQ(content, l.content);
        EXPECT_EQ(guardians, 1);
        EXPECT_EQ(l.tiles[l.entrance], Tile::Path);
        const uint8_t r = rowOf(l.entrance);
        const uint8_t c = colOf(l.entrance);
        EXPECT_TRUE(r == 0 || c == 0 || r == kGrid - 1 || c == kGrid - 1) << "the entrance is on the edge";
        ASSERT_GE(l.encounterPercent, 5);
        ASSERT_LE(l.encounterPercent, 18);
        rates.insert(l.encounterPercent);
      }
    }
  }
  EXPECT_EQ(rates.size(), 14u) << "every rate from 5% to 18% occurs";
}

TEST(Exploration, LayoutsAreDeterministicAndReshapeChangesThem) {
  const Layout a = generate(42, 3, 0);
  const Layout b = generate(42, 3, 0);
  EXPECT_EQ(memcmp(a.tiles, b.tiles, sizeof(a.tiles)), 0);
  EXPECT_EQ(a.entrance, b.entrance);
  const Layout c = generate(42, 3, 1);
  EXPECT_NE(memcmp(a.tiles, c.tiles, sizeof(a.tiles)), 0);
  const Layout d = generate(43, 3, 0);
  EXPECT_NE(memcmp(a.tiles, d.tiles, sizeof(a.tiles)), 0);
}

TEST(Exploration, RegionsOpenOnePerRealm) {
  GameState s = makeCharacter(0);
  EXPECT_EQ(unlockedCount(s), 0);
  s.progress.realm = 1;
  EXPECT_EQ(unlockedCount(s), 1);
  EXPECT_TRUE(unlocked(s, 0));
  EXPECT_FALSE(unlocked(s, 1));
  s.progress.realm = 9;
  EXPECT_EQ(unlockedCount(s), 9);
}

// --- Combat numbers ---------------------------------------------------------------------

TEST(Exploration, DangerWordsAndChances) {
  EXPECT_EQ(dangerFor(10), Danger::Trivial);
  EXPECT_EQ(dangerFor(9), Danger::Weak);
  EXPECT_EQ(dangerFor(4), Danger::Weak);
  EXPECT_EQ(dangerFor(3), Danger::Dangerous);
  EXPECT_EQ(dangerFor(-3), Danger::Dangerous);
  EXPECT_EQ(dangerFor(-4), Danger::Hazardous);
  EXPECT_EQ(dangerFor(-9), Danger::Hazardous);
  EXPECT_EQ(dangerFor(-10), Danger::Extreme);
  EXPECT_EQ(dangerFor(-15), Danger::Extreme);
  EXPECT_EQ(dangerFor(-16), Danger::Impossible);
  EXPECT_EQ(winChance(0), 50);
  EXPECT_EQ(winChance(5), 80);
  EXPECT_EQ(winChance(20), 95);
  EXPECT_EQ(winChance(-20), 5);
  EXPECT_EQ(lossPercent(Danger::Trivial), 10);
  EXPECT_EQ(lossPercent(Danger::Impossible), 60);
}

TEST(Exploration, PlayerLevelCountsStageTechniquesAndGrades) {
  GameState s = makeCharacter(1);
  s.progress.stage = 4;
  EXPECT_EQ(playerLevel(s), 14);
  s.progress.realm = 2;
  s.progress.stage = 2;  // Late
  EXPECT_EQ(playerLevel(s), 26);
  s.progress.progressFlags |= ProgressBlock::kInGreatPerfection;
  EXPECT_EQ(playerLevel(s), 29);
  s.progress.foundationGrade = 4;
  EXPECT_EQ(playerLevel(s), 31);
  scrolls::Scroll sword;
  sword.titleHash = 7;
  sword.kind = scrolls::Kind::Sword;
  sword.hours = 10;  // Earth tier: +3
  sword.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::put(s, 0, sword);
  s.techniqueScrolls[0] = 1;
  EXPECT_EQ(playerLevel(s), 34);
  // A new region opens Dangerous, and is Weak by that realm's Peak.
  GameState t = makeCharacter(1);
  EXPECT_EQ(regionDanger(t, 0), Danger::Dangerous);
  t.progress.stage = 8;
  EXPECT_EQ(regionDanger(t, 0), Danger::Weak);
}

// --- Walking ------------------------------------------------------------------------------

TEST(Exploration, FirstVisitStartsOnTheChartedEntrance) {
  GameState s = makeCharacter();
  const Layout l = layoutOf(s, 0);
  EXPECT_EQ(chartedCount(s, 0), 0);
  EXPECT_EQ(enter(s, l, 0), l.entrance);
  EXPECT_TRUE(charted(s, 0, l.entrance));
  EXPECT_EQ(chartedCount(s, 0), 1);
}

TEST(Exploration, StepsCostStaminaAndRespectTerrain) {
  GameState s = makeCharacter();
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  const uint8_t to = walkableNeighbour(l, l.entrance);
  ASSERT_NE(to, kNoTile);
  // Not adjacent, blocked, no stamina, no HP.
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (!adjacent(l.entrance, t) && t != l.entrance) {
      EXPECT_EQ(canStep(s, l, 0, t), StepError::NotAdjacent);
      break;
    }
  }
  s.progress.stamina = 0;
  EXPECT_EQ(canStep(s, l, 0, to), StepError::NoStamina);
  s.progress.stamina = 3;
  s.progress.hp = 0;
  EXPECT_EQ(canStep(s, l, 0, to), StepError::NoHp);
  s.progress.hp = 100;
  ASSERT_EQ(canStep(s, l, 0, to), StepError::None);
  const StepResult r = step(s, l, 0, to);
  EXPECT_TRUE(r.moved);
  EXPECT_EQ(s.progress.stamina, 2);
  EXPECT_TRUE(charted(s, 0, to));
  EXPECT_EQ(position(s, l, 0), to);
  if (r.fight) {
    EXPECT_EQ(canStep(s, l, 0, l.entrance), StepError::FightWaiting);
  }
}

TEST(Exploration, EncountersAreReproducibleAndNearTheRegionRate) {
  // Walking back and forth many times: the share of random fights stays
  // near the region's rate, and a copy of the save makes the same rolls.
  GameState s = makeCharacter(9);  // strong, so fights are won
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  const uint8_t a = l.entrance;
  const uint8_t b = walkableNeighbour(l, a);
  ASSERT_NE(b, kNoTile);
  int fights = 0;
  int steps = 0;
  GameState copy = s;
  for (int i = 0; i < 2000; ++i) {
    s.progress.stamina = 12;
    copy.progress.stamina = 12;
    s.progress.hp = 100;  // a rare lost fight must not stop the walk
    copy.progress.hp = 100;
    const uint8_t to = position(s, l, 0) == a ? b : a;
    const StepResult r1 = step(s, l, 0, to);
    const StepResult r2 = step(copy, l, 0, to);
    ASSERT_EQ(r1.fight, r2.fight);
    ++steps;
    if (r1.fight && pendingFight(s, l).kind == FightKind::Beast) ++fights;
    while (pendingFight(s, l).kind != FightKind::None) fight(s, l, FightChoice::Fight);
    while (pendingFight(copy, l).kind != FightKind::None) fight(copy, l, FightChoice::Fight);
    ASSERT_EQ(position(s, l, 0), position(copy, l, 0));
  }
  const double share = 100.0 * fights / steps;
  EXPECT_NEAR(share, l.encounterPercent, 3.0);
}

TEST(Exploration, LosingCostsHpAndThrowsBackOntoAChartedTile) {
  GameState s = makeCharacter(1);
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  int losses = 0;
  for (int attempt = 0; attempt < 400 && losses < 20; ++attempt) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    const uint8_t here = position(s, l, 0);
    const uint8_t to = walkableNeighbour(l, here);
    const StepResult r = step(s, l, 0, to);
    if (!r.fight) continue;
    const Fight f = pendingFight(s, l);
    s.progress.realm = 0;  // weaker than the region: losses are likely
    const Fight weak = pendingFight(s, l);
    const FightResult fr = fight(s, l, FightChoice::Fight);
    s.progress.realm = 1;
    (void)f;
    if (fr.won) continue;
    ++losses;
    EXPECT_EQ(fr.hpLost, weak.damage);
    EXPECT_EQ(s.progress.hp, 100 - fr.hpLost);
    ASSERT_NE(fr.thrownTo, kNoTile);
    EXPECT_TRUE(adjacent(to, fr.thrownTo) || fr.thrownTo == to);
    EXPECT_TRUE(charted(s, 0, fr.thrownTo));
    EXPECT_TRUE(walkable(l, fr.thrownTo));
  }
  EXPECT_GT(losses, 0);
}

TEST(Exploration, ALossBelowZeroStopsAtZeroAndBlocksWalking) {
  GameState s = makeCharacter(1);
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  bool sawZero = false;
  for (int i = 0; i < 500 && !sawZero; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = 3;
    step(s, l, 0, walkableNeighbour(l, position(s, l, 0)));
    if (pendingFight(s, l).kind == FightKind::None) continue;
    s.progress.realm = 0;
    const FightResult r = fight(s, l, FightChoice::Fight);
    s.progress.realm = 1;
    if (r.won || r.slipped) continue;
    EXPECT_EQ(r.hpLost, 3);
    EXPECT_EQ(s.progress.hp, 0);
    sawZero = true;
    const uint8_t next = walkableNeighbour(l, position(s, l, 0));
    EXPECT_EQ(canStep(s, l, 0, next), StepError::NoHp);
  }
  EXPECT_TRUE(sawZero);
}

TEST(Exploration, SlippingAwayWithAMovementTechniqueAlwaysWorks) {
  GameState s = makeCharacter(1);
  scrolls::Scroll steps;
  steps.titleHash = 9;
  steps.kind = scrolls::Kind::Movement;
  steps.hours = 1;
  steps.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::put(s, 0, steps);
  s.techniqueScrolls[0] = 1;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  int slips = 0;
  for (int i = 0; i < 300; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    step(s, l, 0, walkableNeighbour(l, position(s, l, 0)));
    if (pendingFight(s, l).kind == FightKind::None) continue;
    EXPECT_EQ(pendingFight(s, l).slipChance, 100);
    const FightResult r = fight(s, l, FightChoice::Slip);
    EXPECT_TRUE(r.slipped);
    EXPECT_EQ(r.hpLost, 0);
    ++slips;
  }
  EXPECT_GT(slips, 0);
}

// --- Tiles, clearing, regrowth -------------------------------------------------------------

TEST(Exploration, ChartingEverythingAndBeatingTheGuardianClearsOnce) {
  GameState s = makeCharacter(9);  // strong enough to win every fight
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  const uint32_t stonesBefore = s.progress.stones;
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (walkable(l, t)) {
      ASSERT_TRUE(walkTo(s, l, 0, t)) << int(t);
    }
  }
  EXPECT_EQ(chartedCount(s, 0), l.walkable);
  EXPECT_TRUE(guardianBeaten(s, 0));
  EXPECT_TRUE(cleared(s, 0));
  EXPECT_GE(s.progress.stones, stonesBefore + 100);  // the clear reward
  EXPECT_GE(inventory::count(s, Item::TonicGreater), 1);
  EXPECT_GE(inventory::count(s, Item::ThunderTalisman) + inventory::count(s, Item::CalmIncense), 2);
}

TEST(Exploration, HerbsRegrowAfterThreeReadingHoursUniquesDoNot) {
  GameState s = makeCharacter(9);
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (walkable(l, t)) {
      ASSERT_TRUE(walkTo(s, l, 0, t));
    }
  }
  uint8_t herb = kNoTile;
  uint8_t ruin = kNoTile;
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (l.tiles[t] == Tile::Herb && herb == kNoTile) herb = t;
    if (l.tiles[t] == Tile::Ruin && ruin == kNoTile) ruin = t;
  }
  ASSERT_NE(herb, kNoTile);
  ASSERT_NE(ruin, kNoTile);
  EXPECT_TRUE(used(s, 0, herb));
  EXPECT_TRUE(used(s, 0, ruin));
  const uint8_t cycle = merchantCycle(s, 0);
  s.progress.gameReadingSeconds += 2 * 3600;
  tick(s);
  EXPECT_TRUE(used(s, 0, herb)) << "not yet";
  s.progress.gameReadingSeconds += 3600;
  tick(s);
  EXPECT_FALSE(used(s, 0, herb));
  EXPECT_TRUE(used(s, 0, ruin));
  EXPECT_EQ(merchantCycle(s, 0), static_cast<uint8_t>(cycle + 1));
  // Walking back onto the herb gathers it again.
  const uint16_t before = inventory::count(s, inventory::herbFor(0));
  ASSERT_TRUE(walkTo(s, l, 0, herb));
  if (position(s, l, 0) == herb) {
    EXPECT_GT(inventory::count(s, inventory::herbFor(0)), before);
  }
}

TEST(Exploration, UpheavalReshapesTwentyReadingHoursAfterClearing) {
  GameState s = makeCharacter(9);
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (walkable(l, t)) {
      ASSERT_TRUE(walkTo(s, l, 0, t));
    }
  }
  ASSERT_TRUE(cleared(s, 0));
  s.progress.gameReadingSeconds += 19 * 3600;
  EXPECT_EQ(tick(s), 0);
  s.progress.gameReadingSeconds += 3600;
  EXPECT_EQ(tick(s), 1);
  EXPECT_EQ(reshapeCount(s, 0), 1);
  EXPECT_FALSE(cleared(s, 0));
  EXPECT_EQ(chartedCount(s, 0), 0);
  const Layout fresh = layoutOf(s, 0);
  EXPECT_NE(memcmp(fresh.tiles, l.tiles, sizeof(l.tiles)), 0);
}

TEST(Exploration, RegionTalismanReshapesOnlyClearedRegions) {
  GameState s = makeCharacter(1);
  inventory::add(s, Item::RegionTalisman);
  EXPECT_FALSE(useRegionTalisman(s, 0));
  s.exploration[8 + 11] = 0x03;  // region 0: cleared, guardian beaten
  EXPECT_TRUE(useRegionTalisman(s, 0));
  EXPECT_EQ(reshapeCount(s, 0), 1);
  EXPECT_EQ(inventory::count(s, Item::RegionTalisman), 0);
}

// --- Merchants and items --------------------------------------------------------------------

TEST(Exploration, MerchantStockIsStableAndSellsBackAtAThird) {
  for (uint32_t world = 1; world < 40; ++world) {
    GameState s = makeCharacter(1, world * 7919u);
    const Layout l = layoutOf(s, 0);
    uint8_t merchant = kNoTile;
    for (uint8_t t = 0; t < kTiles; ++t) {
      if (l.tiles[t] == Tile::Merchant) merchant = t;
    }
    ASSERT_NE(merchant, kNoTile);
    const Stock a = stockFor(s, l, 0, merchant);
    const Stock b = stockFor(s, l, 0, merchant);
    ASSERT_EQ(a.count, b.count);
    EXPECT_GE(a.count, 3);
    EXPECT_LE(a.count, 5);
    EXPECT_EQ(a.lines[0].item, Item::HerbCommon);
    EXPECT_EQ(a.lines[1].item, Item::OreCommon);
    s.progress.stones = 10000;
    const uint8_t limited = 2;
    const Item item = a.lines[limited].item;
    EXPECT_TRUE(buy(s, l, 0, merchant, limited));
    EXPECT_EQ(s.progress.stones, 10000u - inventory::price(item));
    EXPECT_TRUE(stockFor(s, l, 0, merchant).lines[limited].soldOut);
    EXPECT_FALSE(buy(s, l, 0, merchant, limited)) << "one per restock";
    EXPECT_TRUE(buy(s, l, 0, merchant, 0));
    EXPECT_TRUE(buy(s, l, 0, merchant, 0)) << "herbs are not limited";
    const uint32_t before = s.progress.stones;
    EXPECT_TRUE(sell(s, item));
    EXPECT_EQ(s.progress.stones, before + inventory::price(item) / 3);
  }
}

TEST(Exploration, BuyingNeedsTheStones) {
  GameState s = makeCharacter(1);
  const Layout l = layoutOf(s, 0);
  uint8_t merchant = kNoTile;
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (l.tiles[t] == Tile::Merchant) merchant = t;
  }
  s.progress.stones = 9;
  EXPECT_FALSE(buy(s, l, 0, merchant, 0));
  EXPECT_EQ(inventory::count(s, Item::HerbCommon), 0);
}

TEST(Inventory, AddRemoveAndTonics) {
  GameState s = makeCharacter(1);
  EXPECT_EQ(inventory::kinds(s), 0);
  EXPECT_EQ(inventory::add(s, Item::TonicMinor, 2), 2);
  EXPECT_EQ(inventory::add(s, Item::TonicFull), 1);
  EXPECT_EQ(inventory::kinds(s), 2);
  EXPECT_EQ(inventory::count(s, Item::TonicMinor), 2);
  s.progress.hp = 10;
  EXPECT_EQ(inventory::tonicFor(s, 15), Item::TonicMinor);
  EXPECT_EQ(inventory::tonicFor(s, 60), Item::TonicFull);
  EXPECT_EQ(inventory::useTonic(s, Item::TonicMinor), 20);
  EXPECT_EQ(s.progress.hp, 30);
  EXPECT_EQ(inventory::useTonic(s, Item::TonicFull), 70);
  EXPECT_EQ(s.progress.hp, 100);
  EXPECT_EQ(inventory::count(s, Item::TonicFull), 0);
  EXPECT_EQ(inventory::kinds(s), 1);
  EXPECT_FALSE(inventory::remove(s, Item::TonicGreater));
  EXPECT_EQ(inventory::sellPrice(Item::TonicFull), 66);
  EXPECT_EQ(inventory::add(s, Item::HerbRare, 60000), inventory::kMaxCount);
}

// --- Trials use the treasures ----------------------------------------------------------------

TEST(Exploration, ThunderTalismanTakesAWholeStrike) {
  GameState s = makeCharacter(2);
  s.progress.stage = static_cast<uint8_t>(realmInfo(2).stages - 1);
  s.progress.qi = stageCost(2, s.progress.stage);
  scrolls::Scroll key;
  key.titleHash = 11;
  key.kind = scrolls::Kind::Mind;
  key.hours = 5;  // Yellow: the key into Core Formation
  scrolls::put(s, 0, key);
  s.techniqueScrolls[0] = 1;
  inventory::add(s, Item::ThunderTalisman);
  ASSERT_TRUE(trials::begin(s));
  const trials::Strike st = trials::currentStrike(s);
  int shield = -1;
  for (uint8_t i = 0; i < st.optionCount; ++i) {
    if (st.options[i].answer == trials::Answer::Shield) shield = i;
  }
  ASSERT_GE(shield, 0);
  const trials::StrikeResult r = trials::answerStrike(s, static_cast<uint8_t>(shield));
  EXPECT_TRUE(r.shielded);
  EXPECT_EQ(r.damage, 0);
  EXPECT_EQ(s.progress.hp, 100);
  EXPECT_EQ(inventory::count(s, Item::ThunderTalisman), 0);
  EXPECT_EQ(trials::step(s), 1);
  for (uint8_t i = 0; i < trials::currentStrike(s).optionCount; ++i) {
    EXPECT_NE(trials::currentStrike(s).options[i].answer, trials::Answer::Shield);
  }
}

TEST(Exploration, CalmHeartIncenseAddsTwentyFive) {
  GameState s = makeCharacter(3);
  s.progress.stage = static_cast<uint8_t>(realmInfo(3).stages - 1);
  s.progress.qi = stageCost(3, s.progress.stage);
  scrolls::Scroll key;
  key.titleHash = 12;
  key.kind = scrolls::Kind::Sword;
  key.hours = 10;  // Earth: the key into Nascent Soul
  scrolls::put(s, 0, key);
  s.techniqueScrolls[0] = 1;
  inventory::add(s, Item::CalmIncense);
  ASSERT_TRUE(trials::begin(s));
  const uint8_t base = trials::sceneChance(s);
  const trials::SceneResult r = trials::answerScene(s, trials::SceneAnswer::Incense);
  EXPECT_TRUE(r.incense);
  EXPECT_EQ(r.chance, base + 25);
  EXPECT_EQ(inventory::count(s, Item::CalmIncense), 0);
  EXPECT_FALSE(trials::answerScene(s, trials::SceneAnswer::Incense).incense) << "none left";
}

// --- Text ---------------------------------------------------------------------------------------

TEST(Exploration, TextExpandsCleanly) {
  BuiltinPacks packs;
  GameState s = makeCharacter(1);
  std::set<std::string> regionNames;
  for (uint8_t region = 0; region < kRegionCount; ++region) {
    char region_[exploretext::kNameSize];
    char beast[exploretext::kNameSize];
    char guardian[exploretext::kNameSize];
    ASSERT_TRUE(exploretext::regionName(s, region, *packs.grammar, region_, sizeof(region_)));
    regionNames.insert(region_);
    for (uint32_t seed = 0; seed < 20; ++seed) {
      ASSERT_TRUE(exploretext::beastName(s, region, seed * 977u, *packs.grammar, beast, sizeof(beast)));
      EXPECT_EQ(std::string(beast).find_first_of("{}[]|$"), std::string::npos) << beast;
    }
    ASSERT_TRUE(exploretext::guardianName(s, region, *packs.grammar, guardian, sizeof(guardian)));
    const char* lists[] = {
        "explore.path",       "explore.herb",          "explore.ore",          "explore.ruin.won",
        "explore.ruin.trap",  "explore.cave",          "explore.quiet",        "explore.quiet.full",
        "explore.merchant",   "explore.fight.beast",   "explore.fight.lair",   "explore.fight.guardian",
        "explore.won",        "explore.lost",          "explore.slip",         "explore.line.guardian",
        "explore.line.clear", "explore.line.upheaval", "explore.line.talisman"};
    for (const char* key : lists) {
      for (uint32_t seed = 0; seed < 12; ++seed) {
        char out[240];
        ASSERT_TRUE(
            exploretext::flavor(s, key, seed * 31u + region, region_, guardian, *packs.grammar, out, sizeof(out)))
            << key;
        const std::string line = out;
        EXPECT_EQ(line.find_first_of("{}[]|$\\"), std::string::npos) << key << ": " << line;
        EXPECT_EQ(line.back(), '.') << line;
        EXPECT_LT(line.size(), 200u) << line;
        EXPECT_TRUE(line[0] >= 'A' && line[0] <= 'Z') << line;
      }
    }
  }
  EXPECT_EQ(regionNames.size(), kRegionCount) << "every region has its own name";
  // A region's name does not change when it reshapes.
  char before[exploretext::kNameSize];
  char after[exploretext::kNameSize];
  exploretext::regionName(s, 0, *packs.grammar, before, sizeof(before));
  reshape(s, 0);
  exploretext::regionName(s, 0, *packs.grammar, after, sizeof(after));
  EXPECT_STREQ(before, after);
}

TEST(Exploration, PendingRegionNamesTheMapWithTheWaitingFight) {
  GameState s = makeCharacter(3);
  EXPECT_EQ(pendingRegion(s), -1);
  s.exploration[4] = 2;  // region 1 (+1)
  s.exploration[5] = static_cast<uint8_t>(FightKind::Beast);
  EXPECT_EQ(pendingRegion(s), 1);
  s.exploration[5] = 9;  // unknown fight kind: nothing waits
  EXPECT_EQ(pendingRegion(s), -1);
  s.exploration[5] = static_cast<uint8_t>(FightKind::Guardian);
  s.exploration[4] = kRegionCount + 1;  // out of range
  EXPECT_EQ(pendingRegion(s), -1);
}

TEST(Exploration, ADamagedPositionFallsBackToTheEntrance) {
  GameState s = makeCharacter(1);
  const Layout l = layoutOf(s, 0);
  uint8_t blocked = kNoTile;
  for (uint8_t t = 0; t < kTiles && blocked == kNoTile; ++t) {
    if (!walkable(l, t)) blocked = t;
  }
  ASSERT_NE(blocked, kNoTile);
  s.exploration[8 + 12] = static_cast<uint8_t>(blocked + 1);  // a crag: never a valid position
  EXPECT_EQ(position(s, l, 0), l.entrance);
  s.exploration[8 + 12] = 200;  // past the grid
  EXPECT_EQ(position(s, l, 0), l.entrance);
}

TEST(Exploration, LosingTheFightOnTheLastTileStillClears) {
  // Every walkable tile charted and the guardian beaten, but a beast was
  // waiting on the step that charted the last tile, and the fight was lost.
  bool sawLoss = false;
  for (uint32_t counter = 0; counter < 200 && !sawLoss; ++counter) {
    GameState s = makeCharacter(1);  // weak: fights are often lost
    const Layout l = layoutOf(s, 0);
    enter(s, l, 0);
    for (uint8_t t = 0; t < kTiles; ++t) {
      if (walkable(l, t)) s.exploration[8 + t / 8] = static_cast<uint8_t>(s.exploration[8 + t / 8] | (1u << (t % 8)));
    }
    s.exploration[8 + 11] = 0x02;  // guardian beaten, not cleared yet
    for (int i = 0; i < 4; ++i) s.exploration[i] = static_cast<uint8_t>(counter >> (8 * i));
    s.exploration[4] = 1;  // a beast waits in region 0
    s.exploration[5] = static_cast<uint8_t>(FightKind::Beast);
    const FightResult r = fight(s, l, FightChoice::Fight);
    if (r.won) continue;
    sawLoss = true;
    EXPECT_TRUE(r.tile.clearedNow);
    EXPECT_TRUE(cleared(s, 0));
  }
  EXPECT_TRUE(sawLoss);
}
