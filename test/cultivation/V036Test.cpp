#include <Daily.h>
#include <Exploration.h>
#include <GameState.h>
#include <Rivals.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <vector>

// v0.3.6 (agreed with the user in session 17): stamina.
// - A Trivial or Cleared region is walked for free; a fight there costs 1 stamina, won or lost.
//   At 0 stamina there you still walk and gather, but nothing new starts a fight.
// - The ranking ladder's first challenge of the day is free; up to 3 more cost 1 stamina each.

using namespace cultivation;
using namespace cultivation::explore;

namespace {

GameState hero(const uint8_t realm, const uint32_t seed = 0x5EED1234) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = seed;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.realm = realm;
  s.progress.stage = 3;
  s.progress.hpMax = 1000;
  s.progress.hp = 1000;
  s.progress.stamina = 12;
  return s;
}

void setCleared(GameState& s, const uint8_t region) { s.exploration[8 + 19 * region + 11] |= 0x01; }

uint8_t walkableNext(const Layout& l, const uint8_t from) {
  uint8_t n[4];
  const uint8_t c = neighbours(from, n);
  for (uint8_t i = 0; i < c; ++i) {
    if (walkable(l, n[i]) && l.tiles[n[i]] != Tile::Lair && l.tiles[n[i]] != Tile::Guardian &&
        l.tiles[n[i]] != Tile::Merchant)
      return n[i];
  }
  return kNoTile;
}

// Two plain tiles next to each other to pace between (no lair, guardian or merchant).
bool pacing(const Layout& l, uint8_t& a, uint8_t& b) {
  for (uint8_t t = 0; t < kTiles; ++t) {
    if (!walkable(l, t) || l.tiles[t] == Tile::Lair || l.tiles[t] == Tile::Guardian || l.tiles[t] == Tile::Merchant)
      continue;
    const uint8_t n = walkableNext(l, t);
    if (n != kNoTile) {
      a = t;
      b = n;
      return true;
    }
  }
  return false;
}

// Paces in `region` until a fight waits (true), or gives up. Meetings are walked past.
bool paceToAFight(GameState& s, const Layout& l, const uint8_t region, const uint8_t a, const uint8_t b) {
  for (int i = 0; i < 4000; ++i) {
    while (meetingRegion(s) >= 0) meet(s, l, MeetChoice::WalkOn);
    if (pendingFight(s, l).kind != FightKind::None) return true;
    const uint8_t here = position(s, l, region);
    const uint8_t to = here == a ? b : a;
    if (canStep(s, l, region, to) != StepError::None) return false;
    step(s, l, region, to);
  }
  return pendingFight(s, l).kind != FightKind::None;
}

// Puts the player on a plain tile `a` (with neighbour `b`) of region 0.
struct Map {
  Layout l;
  uint8_t a = kNoTile, b = kNoTile;
};
bool placed(GameState& s, Map& m) {
  m.l = layoutOf(s, 0);
  enter(s, m.l, 0);
  if (!pacing(m.l, m.a, m.b)) return false;
  s.exploration[8 + 12] = static_cast<uint8_t>(m.a + 1);  // stand on a
  return position(s, m.l, 0) == m.a;
}

}  // namespace

// --- Free walking -----------------------------------------------------------------------------

TEST(V036FreeWalk, TrivialAndClearedRegionsAreFreeOthersAreNot) {
  GameState weak = hero(1);
  EXPECT_NE(regionDanger(weak, 0), Danger::Trivial);
  EXPECT_FALSE(freeWalk(weak, 0));
  setCleared(weak, 0);
  EXPECT_TRUE(freeWalk(weak, 0));

  GameState strong = hero(9);
  EXPECT_EQ(regionDanger(strong, 0), Danger::Trivial);
  EXPECT_TRUE(freeWalk(strong, 0));
}

TEST(V036FreeWalk, StepsInAFreeRegionCostNothingAndWorkAtZero) {
  GameState s = hero(9);
  Map m;
  ASSERT_TRUE(placed(s, m));
  s.progress.stamina = 5;
  for (int i = 0; i < 30; ++i) {
    while (meetingRegion(s) >= 0) meet(s, m.l, MeetChoice::WalkOn);
    while (pendingFight(s, m.l).kind != FightKind::None) fight(s, m.l, FightChoice::Slip);
    s.progress.stamina = 5;
    const uint8_t to = position(s, m.l, 0) == m.a ? m.b : m.a;
    ASSERT_EQ(canStep(s, m.l, 0, to), StepError::None);
    const StepResult r = step(s, m.l, 0, to);
    EXPECT_TRUE(r.moved);
    EXPECT_EQ(s.progress.stamina, 5) << "a step in a free region is free";
  }
  s.progress.stamina = 0;
  while (pendingFight(s, m.l).kind != FightKind::None) fight(s, m.l, FightChoice::Slip);
  while (meetingRegion(s) >= 0) meet(s, m.l, MeetChoice::WalkOn);
  const uint8_t to = position(s, m.l, 0) == m.a ? m.b : m.a;
  EXPECT_EQ(canStep(s, m.l, 0, to), StepError::None);
}

TEST(V036FreeWalk, AStepInAnOtherRegionStillCostsOne) {
  GameState s = hero(1);
  s.progress.stage = 0;
  Map m;
  ASSERT_TRUE(placed(s, m));
  ASSERT_FALSE(freeWalk(s, 0));
  s.progress.stamina = 3;
  step(s, m.l, 0, m.b);
  EXPECT_EQ(s.progress.stamina, 2);
  s.progress.stamina = 0;
  while (pendingFight(s, m.l).kind != FightKind::None) fight(s, m.l, FightChoice::Slip);
  while (meetingRegion(s) >= 0) meet(s, m.l, MeetChoice::WalkOn);
  EXPECT_EQ(canStep(s, m.l, 0, m.a), StepError::NoStamina);
}

TEST(V036FreeWalk, AFightCostsOneWonOrLostEscapesAreFree) {
  int fought = 0, slipped = 0;
  for (uint32_t seed = 1; seed < 40 && (fought < 3 || slipped < 3); ++seed) {
    GameState s = hero(9, 0x1000u + seed);
    Map m;
    if (!placed(s, m)) continue;
    s.progress.stamina = 7;
    if (!paceToAFight(s, m.l, 0, m.a, m.b)) continue;
    ASSERT_EQ(s.progress.stamina, 7) << "walking to the fight was free";
    const Fight f = pendingFight(s, m.l);
    if (seed % 2 == 0 && f.canSlip) {
      fight(s, m.l, FightChoice::Slip);
      EXPECT_EQ(s.progress.stamina, 7);
      ++slipped;
    } else {
      fight(s, m.l, FightChoice::Fight);
      EXPECT_EQ(s.progress.stamina, 6);
      ++fought;
    }
  }
  EXPECT_GE(fought, 3);
  EXPECT_GE(slipped, 1);
}

TEST(V036FreeWalk, AFightInAPaidRegionCostsNothingMore) {
  // The step already paid: a fight in a region that is not free costs no extra stamina.
  for (uint32_t seed = 1; seed < 60; ++seed) {
    GameState s = hero(2, 0x2000u + seed);
    s.progress.stage = 0;
    Map m;
    if (!placed(s, m) || freeWalk(s, 0)) continue;
    s.progress.stamina = 20;
    if (!paceToAFight(s, m.l, 0, m.a, m.b)) continue;
    const uint8_t before = s.progress.stamina;
    fight(s, m.l, FightChoice::Fight);
    EXPECT_EQ(s.progress.stamina, before);
    return;
  }
  GTEST_SKIP() << "no fight found";
}

TEST(V036FreeWalk, AtZeroStaminaNothingNewStartsAFight) {
  int steps = 0, meetings = 0, tiredMeetings = 0;
  for (uint32_t seed = 1; seed < 6; ++seed) {
    GameState s = hero(9, 0x3000u + seed);
    Map m;
    if (!placed(s, m)) continue;
    s.progress.stamina = 0;
    for (int i = 0; i < 600; ++i) {
      ASSERT_EQ(pendingFight(s, m.l).kind, FightKind::None) << "no fight starts at 0 stamina";
      if (meetingRegion(s) >= 0) {
        const Meeting mt = pendingMeeting(s, m.l);
        ++meetings;
        if (mt.offer != Offer::Trade) {
          EXPECT_TRUE(mt.tired);
          ++tiredMeetings;
          const MeetResult r = meet(s, m.l, MeetChoice::Offer);
          EXPECT_TRUE(r.refused);
          EXPECT_FALSE(r.fought);
        }
        meet(s, m.l, MeetChoice::WalkOn);
      }
      const uint8_t to = position(s, m.l, 0) == m.a ? m.b : m.a;
      ASSERT_EQ(canStep(s, m.l, 0, to), StepError::None);
      step(s, m.l, 0, to);
      ++steps;
    }
  }
  EXPECT_GT(steps, 1000);
  EXPECT_GT(meetings, 0);
  EXPECT_GT(tiredMeetings, 0);
}

TEST(V036FreeWalk, AMeetingsFightCostsOneInAFreeRegion) {
  for (uint32_t seed = 1; seed < 40; ++seed) {
    GameState s = hero(9, 0x4000u + seed);
    Map m;
    if (!placed(s, m)) continue;
    for (int i = 0; i < 3000; ++i) {
      s.progress.stamina = 4;
      while (pendingFight(s, m.l).kind != FightKind::None) fight(s, m.l, FightChoice::Slip);
      if (meetingRegion(s) >= 0) {
        const Meeting mt = pendingMeeting(s, m.l);
        if (mt.offer == Offer::Trade) {
          meet(s, m.l, MeetChoice::WalkOn);
        } else {
          EXPECT_FALSE(mt.tired);
          const MeetResult r = meet(s, m.l, MeetChoice::Offer);
          EXPECT_TRUE(r.fought);
          EXPECT_EQ(s.progress.stamina, 3);
          return;
        }
      }
      const uint8_t to = position(s, m.l, 0) == m.a ? m.b : m.a;
      step(s, m.l, 0, to);
    }
  }
  FAIL() << "no spar or challenge met";
}

TEST(V036FreeWalk, ALairOrGuardianAtZeroIsTooTired) {
  for (uint32_t seed = 1; seed < 30; ++seed) {
    GameState s = hero(9, 0x5000u + seed);
    const Layout l = layoutOf(s, 0);
    enter(s, l, 0);
    for (uint8_t t = 0; t < kTiles; ++t) {
      if (l.tiles[t] != Tile::Lair && l.tiles[t] != Tile::Guardian) continue;
      uint8_t n[4];
      const uint8_t c = neighbours(t, n);
      for (uint8_t i = 0; i < c; ++i) {
        if (!walkable(l, n[i]) || l.tiles[n[i]] == Tile::Lair || l.tiles[n[i]] == Tile::Guardian) continue;
        s.exploration[8 + 12] = static_cast<uint8_t>(n[i] + 1);
        ASSERT_EQ(position(s, l, 0), n[i]);
        s.progress.stamina = 0;
        EXPECT_EQ(canStep(s, l, 0, t), StepError::TooTired);
        s.progress.stamina = 1;
        EXPECT_EQ(canStep(s, l, 0, t), StepError::None);
        // The lair's fight is paid when fought, not when stepped on.
        const StepResult r = step(s, l, 0, t);
        EXPECT_TRUE(r.fight);
        EXPECT_EQ(s.progress.stamina, 1);
        fight(s, l, FightChoice::Fight);
        EXPECT_EQ(s.progress.stamina, 0);
        return;
      }
    }
  }
  FAIL() << "no lair or guardian next to a plain tile";
}

TEST(V036FreeWalk, AtZeroTheRoadIsReproducible) {
  // At 0 stamina the encounter roll is still taken, so the road's later rolls do not shift:
  // the same steps reach the same tiles and the same meetings.
  GameState a = hero(9, 0x6001);
  GameState b = hero(9, 0x6001);
  Map ma, mb;
  ASSERT_TRUE(placed(a, ma));
  ASSERT_TRUE(placed(b, mb));
  a.progress.stamina = 0;
  b.progress.stamina = 0;
  for (int i = 0; i < 200; ++i) {
    while (meetingRegion(a) >= 0) meet(a, ma.l, MeetChoice::WalkOn);
    while (meetingRegion(b) >= 0) meet(b, mb.l, MeetChoice::WalkOn);
    const uint8_t to = position(a, ma.l, 0) == ma.a ? ma.b : ma.a;
    step(a, ma.l, 0, to);
    step(b, mb.l, 0, to);
    EXPECT_EQ(stepCounter(a), stepCounter(b));
  }
  EXPECT_EQ(a.exploration, b.exploration);
}

// --- The daily walk task ----------------------------------------------------------------------

TEST(V036Daily, AFightTaskNeedsStaminaAWalkTaskDoesNotInAFreeRegion) {
  // Before v0.3.6 the game task needed stamina; now a free region is enough for walking tasks.
  GameState s = hero(9);
  s.progress.stamina = 0;
  ASSERT_TRUE(freeWalk(s, 0));
  std::set<daily::Task> seen;
  for (uint32_t day = 0; day < 200; ++day) {
    // Reroll the game slot to see every task the day can offer.
    GameState t = s;
    StatsSample now;
    now.today = 9800 + day;
    VisitGains g;
    daily::update(t, g, now, 9799 + day);
    seen.insert(daily::task(t, 2));
  }
  EXPECT_EQ(seen.count(daily::Task::WinFight), 0u);
  EXPECT_TRUE(seen.count(daily::Task::Gather) + seen.count(daily::Task::Chart) + seen.count(daily::Task::Merchant) > 0);
}

// --- The ranking ladder -----------------------------------------------------------------------

namespace {

constexpr uint32_t kMonday = 9802;

struct Game {
  GameState state;
  StatsSample sample;
  std::set<uint32_t> days;
  RevisionWatch watch;

  Game() {
    CharacterDraft d;
    snprintf(d.name, sizeof(d.name), "Lin Yue");
    d.destiny.purity = 50;
    d.destiny.talent = 2;
    d.origin = Origin::StreetOrphan;
    d.worldSeed = 0x5EED1234;
    createCharacter(state, d, at(kMonday));
    state.progress.realm = 1;
    state.progress.hpMax = 100;
    state.progress.hp = 100;
  }
  void read(const uint32_t day, const uint32_t minutes) {
    sample.seconds += minutes * 60u;
    sample.pages += minutes;
    sample.sessions += 1;
    sample.timeOfDaySeconds[2] += minutes * 60u;
    sample.dayOfWeekSeconds[bridge::weekdayOf(day)] += minutes * 60u;
    days.insert(day);
  }
  StatsSample at(const uint32_t today) const {
    StatsSample out = sample;
    out.today = today;
    out.historyBits.fill(0);
    out.historyAnchorDay = days.empty() ? 0 : *days.rbegin();
    for (const uint32_t d : days) {
      const uint32_t delta = out.historyAnchorDay - d;
      if (delta < StatsSample::kHistoryDays) out.historyBits[delta / 8] |= static_cast<uint8_t>(1u << (delta % 8));
    }
    return out;
  }
  void visit(const uint32_t today) {
    const StatsSample now = at(today);
    const uint32_t lastDay = state.snapshot.lastVisitDay;
    const VisitGains g = bridge::collect(state.snapshot, now, watch);
    absorb(state, g);
    daily::update(state, g, now, lastDay);
    rivals::update(state, now);
    rivals::roam(state);
  }
};

Game ladder() {
  Game g;
  g.visit(kMonday);
  g.read(kMonday + 1, 10);
  g.visit(kMonday + 1);
  g.state.progress.hp = 100;
  return g;
}

}  // namespace

TEST(V036Ladder, TheFirstIsFreeThenThreeMoreForOneStaminaEach) {
  Game g = ladder();
  g.state.progress.stamina = 10;
  rivals::Bout b = rivals::challengeBout(g.state);
  EXPECT_EQ(b.block, rivals::Block::None);
  EXPECT_FALSE(b.extra);
  ASSERT_TRUE(rivals::challenge(g.state).done);
  EXPECT_EQ(g.state.progress.stamina, 10) << "the day's first challenge is free";
  for (uint8_t k = 0; k < rivals::kExtraChallenges; ++k) {
    g.state.progress.hp = 100;
    b = rivals::challengeBout(g.state);
    EXPECT_EQ(b.block, rivals::Block::None);
    EXPECT_TRUE(b.extra);
    EXPECT_EQ(b.extraLeft, rivals::kExtraChallenges - k);
    if (rivals::playerRank(g.state) == 1) return;  // nobody left to beat
    ASSERT_TRUE(rivals::challenge(g.state).done);
    EXPECT_EQ(g.state.progress.stamina, 10 - 1 - k) << "won or lost, an extra costs 1";
  }
  g.state.progress.hp = 100;
  b = rivals::challengeBout(g.state);
  EXPECT_EQ(b.block, rivals::Block::DoneToday);
  EXPECT_EQ(b.extraLeft, 0);
  EXPECT_FALSE(rivals::challenge(g.state).done);
  EXPECT_EQ(g.state.progress.stamina, 7);
}

TEST(V036Ladder, AnExtraNeedsStamina) {
  Game g = ladder();
  g.state.progress.stamina = 0;
  ASSERT_TRUE(rivals::challenge(g.state).done) << "the free one needs no stamina";
  g.state.progress.hp = 100;
  EXPECT_EQ(rivals::challengeBout(g.state).block, rivals::Block::NoStamina);
  EXPECT_FALSE(rivals::challenge(g.state).done);
  g.state.progress.stamina = 1;
  EXPECT_EQ(rivals::challengeBout(g.state).block, rivals::Block::None);
  EXPECT_TRUE(rivals::challenge(g.state).done);
  EXPECT_EQ(g.state.progress.stamina, 0);
}

TEST(V036Ladder, ExtrasRollAfreshAndResetTheNextDay) {
  // Each extra rolls afresh (it once reused the free one's roll).
  {
    Game g = ladder();
    g.state.progress.stamina = 10;
    std::set<uint8_t> rolls;
    for (int k = 0; k < 4; ++k) {
      g.state.progress.hp = 100;
      const rivals::BoutResult r = rivals::challenge(g.state);
      if (r.done) rolls.insert(r.roll);
    }
    EXPECT_GT(rolls.size(), 1u);
  }

  Game g = ladder();
  g.state.progress.stamina = 10;
  for (int k = 0; k < 4; ++k) {
    g.state.progress.hp = 100;
    rivals::challenge(g.state);
  }
  EXPECT_EQ(rivals::challengeBout(g.state).block == rivals::Block::DoneToday || rivals::playerRank(g.state) == 1, true);
  g.read(kMonday + 2, 10);
  g.visit(kMonday + 2);
  g.state.progress.hp = 100;
  const uint8_t stamina = g.state.progress.stamina;  // the day's reading added some
  const rivals::Bout b = rivals::challengeBout(g.state);
  EXPECT_EQ(b.block, rivals::Block::None);
  EXPECT_FALSE(b.extra);
  rivals::challenge(g.state);
  EXPECT_EQ(g.state.progress.stamina, stamina) << "the next day's first is free again";
  g.state.progress.hp = 100;
  EXPECT_EQ(rivals::challengeBout(g.state).extraLeft, rivals::kExtraChallenges);
}
