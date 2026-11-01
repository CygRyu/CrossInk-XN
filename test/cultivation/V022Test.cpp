#include <Alchemy.h>
#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <Inventory.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// v0.2.2 (agreed with the user): cultivators met on the road.
using namespace cultivation;
using namespace cultivation::explore;
using inventory::Item;

namespace {

GameState makeCharacter(const uint8_t realm = 2, const uint32_t worldSeed = 0x5EED1234) {
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
  s.progress.stones = 100000;
  return s;
}

void setCounterBytes(GameState& s, const uint32_t v) {
  for (int i = 0; i < 4; ++i) s.exploration[i] = static_cast<uint8_t>(v >> (8 * i));
}

void forceMeeting(GameState& s, const uint8_t region, const uint8_t ring = 0) {
  s.exploration[4] = static_cast<uint8_t>(region + 1);
  s.exploration[5] = static_cast<uint8_t>(FightKind::Meeting);
  s.exploration[6] = 0x01;
  s.exploration[7] = ring;
}

// Searches counters until a stranger meeting in `region` satisfies `want`; leaves it waiting.
bool findMeeting(GameState& s, const Layout& l, const uint8_t region, const std::function<bool(const Meeting&)>& want,
                 const uint32_t from = 1) {
  for (uint32_t c = from; c < from + 30000; ++c) {
    setCounterBytes(s, c);
    forceMeeting(s, region);
    if (want(pendingMeeting(s, l))) return true;
  }
  return false;
}

uint8_t plainNeighbour(const Layout& l, const uint8_t from) {
  uint8_t n[4];
  const uint8_t c = neighbours(from, n);
  for (uint8_t k = 0; k < c; ++k) {
    if (walkable(l, n[k]) && l.tiles[n[k]] != Tile::Lair && l.tiles[n[k]] != Tile::Guardian &&
        l.tiles[n[k]] != Tile::Merchant)
      return n[k];
  }
  return kNoTile;
}

// A character whose region-0 entrance has a plain walkable neighbour (and, with `herbNext`, an herb tile).
GameState worldFor(const uint8_t realm, const bool herbNext = false) {
  for (uint32_t world = 1; world < 400; ++world) {
    GameState s = makeCharacter(realm, world * 7919u);
    const Layout l = layoutOf(s, 0);
    if (herbNext) {
      uint8_t n[4];
      const uint8_t c = neighbours(l.entrance, n);
      for (uint8_t i = 0; i < c; ++i) {
        if (l.tiles[n[i]] == Tile::Herb) return s;
      }
    } else if (plainNeighbour(l, l.entrance) != kNoTile) {
      return s;
    }
  }
  return makeCharacter(realm);
}

}  // namespace

TEST(Meetings, AreRareReproducibleAndShareTheCap) {
  GameState s = worldFor(9);
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  const uint8_t a = l.entrance;
  const uint8_t b = plainNeighbour(l, a);
  ASSERT_NE(b, kNoTile);
  GameState copy = s;
  int meetings = 0;
  int steps = 0;
  bool last = false;
  for (int i = 0; i < 4000; ++i) {
    s.progress.stamina = copy.progress.stamina = 12;
    s.progress.hp = copy.progress.hp = 100;
    const uint8_t to = position(s, l, 0) == a ? b : a;
    const StepResult r1 = step(s, l, 0, to);
    const StepResult r2 = step(copy, l, 0, to);
    ASSERT_EQ(r1.meeting, r2.meeting);
    ASSERT_EQ(r1.fight, r2.fight);
    EXPECT_FALSE(r1.fight && r1.meeting);
    const bool interrupted = r1.fight || r1.meeting;
    EXPECT_FALSE(last && interrupted) << "step " << i;
    last = interrupted;
    ++steps;
    if (r1.meeting) {
      ++meetings;
      EXPECT_EQ(canStep(s, l, 0, to == a ? b : a), StepError::FightWaiting);
      EXPECT_EQ(meetingRegion(s), 0);
      EXPECT_EQ(waitingRegion(s), 0);
      EXPECT_EQ(pendingRegion(s), -1);
      meet(s, l, MeetChoice::WalkOn);
      meet(copy, l, MeetChoice::WalkOn);
    }
    while (pendingFight(s, l).kind != FightKind::None) fight(s, l, FightChoice::Fight);
    while (pendingFight(copy, l).kind != FightKind::None) fight(copy, l, FightChoice::Fight);
  }
  const double share = 100.0 * meetings / steps;
  EXPECT_GT(share, 2.0);
  EXPECT_LT(share, 6.0);
}

TEST(Meetings, TemperamentsAndOffersFollowTheTable) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  int count[3] = {};
  int total = 0;
  const int with = playerLevel(s, true);
  const int plain = 2 * playerLevel(s, false) - with;
  for (uint32_t c = 1; c <= 6000; ++c) {
    setCounterBytes(s, c);
    forceMeeting(s, 1);
    const Meeting m = pendingMeeting(s, l);
    ASSERT_TRUE(m.active);
    ++count[static_cast<int>(m.temperament)];
    ++total;
    EXPECT_GE(m.level, plain - 9);
    EXPECT_LE(m.level, plain + 6);
    EXPECT_EQ(m.dares, with - m.level <= 3);
    if (!m.dares) {
      EXPECT_NE(m.offer, Offer::Challenge);
    }
    switch (m.temperament) {
      case Temperament::Proud:
        EXPECT_EQ(m.offer, m.dares ? Offer::Challenge : Offer::Spar);
        break;
      case Temperament::Cautious:
        EXPECT_NE(m.offer, Offer::Challenge);
        break;
      case Temperament::Greedy:
        EXPECT_EQ(m.offer, m.dares ? Offer::Challenge : Offer::Trade);
        break;
    }
    // Stable: asking twice gives the same person.
    const Meeting again = pendingMeeting(s, l);
    EXPECT_EQ(again.nameSeed, m.nameSeed);
    EXPECT_EQ(again.offer, m.offer);
  }
  EXPECT_NEAR(100.0 * count[0] / total, 35.0, 4.0);
  EXPECT_NEAR(100.0 * count[1] / total, 35.0, 4.0);
  EXPECT_NEAR(100.0 * count[2] / total, 30.0, 4.0);
}

TEST(Meetings, ASparCostsNothingWhenLostAndPaysLikeABeastWhenWon) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  enter(s, l, 1);
  bool won = false;
  bool lost = false;
  for (uint32_t from = 1; from < 400 && !(won && lost); ++from) {
    ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting& m) { return m.offer == Offer::Spar; }, from * 50));
    const uint32_t before = s.progress.stones;
    const uint16_t hp = s.progress.hp;
    const Meeting m = pendingMeeting(s, l);
    const MeetResult r = meet(s, l, MeetChoice::Offer);
    ASSERT_FALSE(r.refused);
    EXPECT_TRUE(r.fought);
    EXPECT_TRUE(r.done);
    EXPECT_EQ(s.progress.hp, hp);  // no HP either way
    EXPECT_EQ(r.thrownTo, kNoTile);
    EXPECT_EQ(meetingRegion(s), -1);
    EXPECT_EQ(s.exploration[7], 0);
    uint8_t region;
    uint16_t id;
    uint8_t meets;
    MeetOutcome outcome;
    ASSERT_TRUE(metCultivator(s, 0, region, id, meets, outcome));
    EXPECT_EQ(outcome, r.won ? MeetOutcome::Won : MeetOutcome::Lost);
    EXPECT_EQ(id, m.id);
    EXPECT_EQ(region, 1);
    EXPECT_EQ(meets, 1);
    if (r.won) {
      won = true;
      const bool half = regionDanger(s, 1) == Danger::Trivial;
      const uint32_t lo = half ? 5u : 10u;
      EXPECT_GE(r.reward.stones, lo);
      EXPECT_LE(r.reward.stones, half ? 10u : 20u);
      EXPECT_EQ(s.progress.stones, before + r.reward.stones);
    } else {
      lost = true;
      EXPECT_EQ(s.progress.stones, before);
    }
    for (uint8_t i = 0; i < 4; ++i)
      for (uint8_t j = 0; j < 4; ++j) s.memory[32 + 4 * i + j] = 0;
  }
  EXPECT_TRUE(won);
  EXPECT_TRUE(lost);
}

TEST(Meetings, AChallengeIsARealFight) {
  GameState s = makeCharacter(3);
  s.progress.realm = 1;  // weaker than the region so a loss happens
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  bool won = false;
  bool lost = false;
  for (uint32_t from = 1; from < 600 && !(won && lost); ++from) {
    s.progress.hp = 100;
    if (!findMeeting(s, l, 0, [](const Meeting& m) { return m.offer == Offer::Challenge; }, from * 40)) break;
    const Meeting m = pendingMeeting(s, l);
    const uint32_t before = s.progress.stones;
    const MeetResult r = meet(s, l, MeetChoice::Offer);
    EXPECT_TRUE(r.done);
    EXPECT_TRUE(r.fought);
    if (r.won) {
      won = true;
      const bool half = regionDanger(s, 0) == Danger::Trivial;
      EXPECT_GE(r.reward.stones, half ? 7u : 15u);
      EXPECT_LE(r.reward.stones, half ? 15u : 30u);
      EXPECT_EQ(s.progress.stones, before + r.reward.stones);
      EXPECT_EQ(s.progress.hp, 100);
    } else {
      lost = true;
      EXPECT_EQ(r.hpLost, m.damage);
      EXPECT_EQ(s.progress.hp, 100 - m.damage);
      EXPECT_EQ(s.progress.stones, before);
    }
    for (uint8_t i = 0; i < 16; ++i) s.memory[32 + i] = 0;
  }
  EXPECT_TRUE(won);
  EXPECT_TRUE(lost);
}

TEST(Meetings, ATravellersStockIsPricedByTemperamentAndSoldOnce) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  enter(s, l, 1);
  ASSERT_TRUE(findMeeting(
      s, l, 1, [](const Meeting& m) { return m.offer == Offer::Trade && m.temperament == Temperament::Cautious; }));
  Meeting m = pendingMeeting(s, l);
  Stock st = stockFor(s, l, 1, kTraderTile);
  ASSERT_GE(st.count, 2);
  ASSERT_LE(st.count, 3);
  EXPECT_FALSE(st.lines[0].limited);
  const uint8_t next = unlockedCount(s);  // the next closed region's ingredient
  EXPECT_EQ(inventory::regionOf(st.lines[0].item), next);
  EXPECT_EQ(st.lines[0].price, (inventory::price(st.lines[0].item) * 150u + 50u) / 100u);
  const uint8_t treasure = st.count - 1;
  ASSERT_TRUE(st.lines[treasure].limited);
  EXPECT_EQ(st.lines[treasure].price, inventory::price(st.lines[treasure].item));
  // Bulk ingredient purchase is priced per unit and limited by the stones.
  const uint32_t stones = s.progress.stones;
  EXPECT_TRUE(buy(s, l, 1, kTraderTile, 0, 3));
  EXPECT_EQ(s.progress.stones, stones - 3u * st.lines[0].price);
  // At most 5 of the ingredient per meeting (3 bought, 2 left).
  EXPECT_EQ(stockFor(s, l, 1, kTraderTile).lines[0].left, 2);
  EXPECT_EQ(affordable(s, stockFor(s, l, 1, kTraderTile).lines[0]), 2);
  EXPECT_FALSE(buy(s, l, 1, kTraderTile, 0, 3));
  EXPECT_TRUE(buy(s, l, 1, kTraderTile, 0, 2));
  EXPECT_TRUE(stockFor(s, l, 1, kTraderTile).lines[0].soldOut);
  EXPECT_FALSE(buy(s, l, 1, kTraderTile, 0, 1));
  EXPECT_FALSE(pendingMeeting(s, l).talked);             // the count does not spill into the other flags
  EXPECT_FALSE(buy(s, l, 1, kTraderTile, treasure, 2));  // a limited line is one unit
  EXPECT_TRUE(buy(s, l, 1, kTraderTile, treasure, 1));
  EXPECT_FALSE(buy(s, l, 1, kTraderTile, treasure, 1));  // sold
  EXPECT_TRUE(stockFor(s, l, 1, kTraderTile).lines[treasure].soldOut);
  EXPECT_TRUE(pendingMeeting(s, l).traded);
  EXPECT_EQ(inventory::count(s, st.lines[treasure].item), 1u);
  // Not enough stones: refused.
  s.progress.stones = 0;
  EXPECT_FALSE(buy(s, l, 1, kTraderTile, 0, 1));
  // Walking on records the trade and the next meeting gives 10% off.
  const MeetResult r = meet(s, l, MeetChoice::WalkOn);
  EXPECT_EQ(r.outcome, MeetOutcome::Traded);
  forceMeeting(s, 1, 1);
  const Meeting again = pendingMeeting(s, l);
  ASSERT_TRUE(again.active);
  EXPECT_EQ(again.ring, 0);
  EXPECT_EQ(again.id, m.id);
  EXPECT_EQ(again.last, MeetOutcome::Traded);
  EXPECT_EQ(again.levelsAdded, 3);
  EXPECT_FALSE(again.traded);  // a new meeting: nothing bought yet
  const Stock st2 = stockFor(s, l, 1, kTraderTile);
  EXPECT_LT(st2.lines[0].price, st.lines[0].price + 1);
}

TEST(Meetings, GreedyTradersAskMore) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  ASSERT_TRUE(findMeeting(
      s, l, 1, [](const Meeting& m) { return m.offer == Offer::Trade && m.temperament == Temperament::Greedy; }));
  const Stock st = stockFor(s, l, 1, kTraderTile);
  EXPECT_EQ(st.lines[0].price, inventory::price(st.lines[0].item) * 2u);
  const uint8_t t = st.count - 1;
  EXPECT_EQ(st.lines[t].price, (inventory::price(st.lines[t].item) * 130u + 50u) / 100u);
}

TEST(Meetings, TalkingDependsOnTemperamentAndWorksOnce) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  enter(s, l, 1);
  Enemy kind;
  // Proud: free, a real hint about the guardian.
  ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting& m) { return m.temperament == Temperament::Proud; }));
  uint32_t stones = s.progress.stones;
  MeetResult r = meet(s, l, MeetChoice::Talk);
  EXPECT_EQ(r.talk, TalkResult::Hint);
  EXPECT_FALSE(r.done);
  EXPECT_EQ(r.talkPaid, 0u);
  EXPECT_EQ(s.progress.stones, stones);
  kind = guardianEnemy(s, 1);
  EXPECT_EQ(r.hintEnemy, kind);
  EXPECT_TRUE(meet(s, l, MeetChoice::Talk).refused);  // once
  EXPECT_TRUE(pendingMeeting(s, l).talked);
  EXPECT_EQ(meet(s, l, MeetChoice::WalkOn).outcome, MeetOutcome::Talked);
  for (uint8_t i = 0; i < 16; ++i) s.memory[32 + i] = 0;

  // Cautious: nothing for a stranger, the hint once they know you.
  ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting& m) { return m.temperament == Temperament::Cautious; }));
  EXPECT_EQ(meet(s, l, MeetChoice::Talk).talk, TalkResult::Nothing);
  meet(s, l, MeetChoice::WalkOn);
  forceMeeting(s, 1, 1);
  ASSERT_EQ(pendingMeeting(s, l).temperament, Temperament::Cautious);
  EXPECT_EQ(meet(s, l, MeetChoice::Talk).talk, TalkResult::Hint);
  meet(s, l, MeetChoice::WalkOn);
  for (uint8_t i = 0; i < 16; ++i) s.memory[32 + i] = 0;

  // Greedy: sells it for 10 x (region + 1) stones; short of stones nothing happens.
  ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting& m) { return m.temperament == Temperament::Greedy; }));
  s.progress.stones = 5;
  r = meet(s, l, MeetChoice::Talk);
  EXPECT_EQ(r.talk, TalkResult::TooPoor);
  EXPECT_EQ(r.talkCost, 20u);
  EXPECT_EQ(s.progress.stones, 5u);
  EXPECT_FALSE(pendingMeeting(s, l).talked);
  s.progress.stones = 50;
  r = meet(s, l, MeetChoice::Talk);
  EXPECT_EQ(r.talk, TalkResult::Hint);
  EXPECT_EQ(r.talkPaid, 20u);
  EXPECT_EQ(s.progress.stones, 30u);
  meet(s, l, MeetChoice::WalkOn);

  // A beaten guardian leaves nothing to tell, and nothing is charged.
  s.exploration[8 + 19 * 1 + 11] |= 0x02;
  for (uint8_t i = 0; i < 16; ++i) s.memory[32 + i] = 0;
  ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting& m) { return m.temperament == Temperament::Greedy; }));
  s.progress.stones = 50;
  EXPECT_EQ(meet(s, l, MeetChoice::Talk).talk, TalkResult::Nothing);
  EXPECT_EQ(s.progress.stones, 50u);
}

TEST(Meetings, TheRingKeepsTheFourLatestAndTheirAges) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  s.progress.ageMonths = 12u * 4000u;  // far past one byte of years
  uint16_t ids[6] = {};
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(findMeeting(
        s, l, 1,
        [&](const Meeting& m) {
          for (int k = 0; k < i; ++k) {
            if (m.id == ids[k]) return false;
          }
          return true;
        },
        1 + i * 1000));
    ids[i] = pendingMeeting(s, l).id;
    meet(s, l, MeetChoice::WalkOn);
  }
  // The two oldest are forgotten: the ring holds ids 2..5 in order.
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t region, meets;
    uint16_t id;
    MeetOutcome o;
    ASSERT_TRUE(metCultivator(s, i, region, id, meets, o));
    EXPECT_EQ(id, ids[i + 2]);
    EXPECT_EQ(meets, 1);
    EXPECT_EQ(o, MeetOutcome::Passed);
  }
  // The age is kept in whole years: 24 months later it was "2 years ago".
  s.progress.ageMonths += 24;
  forceMeeting(s, 1, 4);
  const Meeting m = pendingMeeting(s, l);
  EXPECT_EQ(m.yearsAgo, 2);
  EXPECT_EQ(m.id, ids[5]);
}

TEST(Meetings, ReturnsGrowStrongerByThreeLevelsAtMostTwice) {
  GameState s = makeCharacter(3);
  const Layout l = layoutOf(s, 1);
  ASSERT_TRUE(findMeeting(s, l, 1, [](const Meeting&) { return true; }));
  const Meeting first = pendingMeeting(s, l);
  EXPECT_EQ(first.levelsAdded, 0);
  EXPECT_EQ(first.meets, 0);
  meet(s, l, MeetChoice::WalkOn);
  const int expected[] = {3, 6, 6, 6};
  for (int k = 0; k < 4; ++k) {
    forceMeeting(s, 1, 1);
    const Meeting m = pendingMeeting(s, l);
    ASSERT_EQ(m.ring, 0);
    EXPECT_EQ(m.levelsAdded, expected[k]) << k;
    EXPECT_EQ(m.nameSeed, first.nameSeed);  // the same person
    EXPECT_EQ(m.temperament, first.temperament);
    meet(s, l, MeetChoice::WalkOn);
  }
}

TEST(Meetings, OneInThreeInTheRegionIsARememberedFace) {
  GameState s = worldFor(9);
  s.progress.stage = 3;
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  forceMeeting(s, 0);  // meet someone once so the ring has a face for region 0
  setCounterBytes(s, 77);
  meet(s, l, MeetChoice::WalkOn);
  const uint8_t a = l.entrance;
  const uint8_t b = plainNeighbour(l, a);
  ASSERT_NE(b, kNoTile);
  int meetings = 0;
  int remembered = 0;
  for (int i = 0; i < 12000 && meetings < 400; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = 100;
    const uint8_t to = position(s, l, 0) == a ? b : a;
    const StepResult r = step(s, l, 0, to);
    if (r.meeting) {
      ++meetings;
      if (pendingMeeting(s, l).ring >= 0) ++remembered;
      meet(s, l, MeetChoice::WalkOn);
    }
    while (pendingFight(s, l).kind != FightKind::None) fight(s, l, FightChoice::Fight);
  }
  ASSERT_GE(meetings, 200);
  EXPECT_NEAR(100.0 * remembered / meetings, 33.3, 8.0);
}

TEST(Meetings, WalkingOnTakesWhatTheTileHolds) {
  GameState s = worldFor(3, true);
  const Layout l = layoutOf(s, 0);
  enter(s, l, 0);
  uint8_t herb = kNoTile;
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (l.tiles[t] == Tile::Herb && adjacent(t, l.entrance)) herb = t;
  }
  if (herb == kNoTile) GTEST_SKIP() << "no herb next to the entrance in this layout";
  // Step on it with a meeting forced in the way.
  s.exploration[8 + 19 * 0 + 12] = static_cast<uint8_t>(herb + 1);
  s.exploration[8 + herb / 8] |= static_cast<uint8_t>(1u << (herb % 8));  // charted
  forceMeeting(s, 0);
  const MeetResult r = meet(s, l, MeetChoice::WalkOn);
  EXPECT_TRUE(r.done);
  EXPECT_EQ(r.tile.outcome, TileOutcome::Gathered);
  EXPECT_TRUE(used(s, 0, herb));
}

TEST(Meetings, TheTextListsExpandCleanly) {
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
  GameState s = makeCharacter(3);
  // Names: a person and their sect, in every region's theme.
  for (uint8_t region = 0; region < 9; ++region) {
    for (uint32_t seed = 0; seed < 20; ++seed) {
      char who[exploretext::kNameSize];
      ASSERT_TRUE(exploretext::cultivatorName(s, region, seed * 977u, grammar, who, sizeof(who)));
      EXPECT_NE(std::string(who).find(" of the "), std::string::npos) << who;
      EXPECT_EQ(std::string(who).find_first_of("{}[]|$"), std::string::npos) << who;
    }
  }
  const char* screen[] = {"explore.meet.spar",        "explore.meet.trade",       "explore.meet.challenge",
                          "explore.meet.return",      "explore.meet.sparwon",     "explore.meet.sparlost",
                          "explore.meet.talk.proud",  "explore.meet.talk.shy",    "explore.meet.talk.open",
                          "explore.meet.talk.greedy", "explore.meet.talk.nothing"};
  for (const char* key : screen) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      char out[300];
      ASSERT_TRUE(exploretext::flavor(s, key, seed, "the Reedwater Marshes", "Old Bai of the Azure Cloud Sect", grammar,
                                      out, sizeof(out)))
          << key;
      EXPECT_EQ(std::string(out).find_first_of("{}[]|$"), std::string::npos) << out;
      // The bout lines show on the map, which has no header: they name the person.
      if (std::string(key) == "explore.meet.sparwon" || std::string(key) == "explore.meet.sparlost") {
        EXPECT_NE(std::string(out).find("Bai"), std::string::npos) << key << ": " << out;
      }
      EXPECT_LT(strlen(out), 191u) << out;
    }
  }
  const char* chronicle[] = {"explore.line.meet.sparwon",      "explore.line.meet.sparlost",
                             "explore.line.meet.challengewon", "explore.line.meet.challengelost",
                             "explore.line.meet.trade",        "explore.line.meet.talk"};
  for (const char* key : chronicle) {
    for (uint32_t seed = 0; seed < 6; ++seed) {
      lifelog::Line line;
      ASSERT_TRUE(exploretext::chronicleLine(line, s, key, seed, "the Reedwater Marshes",
                                             "Old Bai of the Azure Cloud Sect", grammar))
          << key;
      EXPECT_NE(std::string(line.text).find("Bai"), std::string::npos) << line.text;
      EXPECT_NE(std::string(line.text).find("Reedwater"), std::string::npos) << line.text;
    }
  }
  const char* again[] = {"explore.line.meet.again.won", "explore.line.meet.again.lost",
                         "explore.line.meet.again.traded", "explore.line.meet.again.talked",
                         "explore.line.meet.again.passed"};
  for (const char* key : again) {
    for (uint32_t seed = 0; seed < 6; ++seed) {
      lifelog::Line line;
      ASSERT_TRUE(exploretext::chronicleLine(line, s, key, seed, "the Reedwater Marshes",
                                             "Old Bai of the Azure Cloud Sect", grammar, "two years ago"))
          << key;
      EXPECT_NE(std::string(line.text).find("two years ago"), std::string::npos) << line.text;
      EXPECT_EQ(std::string(line.text).find_first_of("{}[]|$"), std::string::npos) << line.text;
    }
  }
}
