#include <Exploration.h>
#include <GameState.h>
#include <Rivals.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>

// v0.3.7 "Polish" (agreed with the user in session 17):
// - A: until you first meet, your rival can find you in any region you have opened.
// - B: a Coward nemesis runs at most twice, then it is cornered; a nemesis Trivial for you over
//   10 reading hours gives up the grudge and frees its place.
// - C: random encounters 10-25% of steps per region (5-18% before).

using namespace cultivation;

namespace {

constexpr uint32_t kMonday = 9802;

GameState hero(const uint8_t realm, const uint32_t seed = 0x5EED1234) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.origin = Origin::StreetOrphan;
  d.worldSeed = seed;
  GameState s;
  StatsSample now;
  now.today = kMonday;
  createCharacter(s, d, now);
  rivals::update(s, now);  // creates the rival (Body Tempering)
  s.progress.realm = realm;
  s.progress.stage = 3;
  s.progress.hpMax = 1000;
  s.progress.hp = 1000;
  s.progress.stamina = 20;
  return s;
}

uint8_t* nemBytes(GameState& s, const uint8_t i) { return s.rivals.data() + 24 + 24 * i; }

}  // namespace

// --- A. The rival comes looking for you -------------------------------------------------------

TEST(V037Rival, UntilYouMeetTheyMayFindYouInAnyRegionYouHaveOpened) {
  GameState s = hero(3);
  const rivals::Rival r = rivals::rival(s);
  ASSERT_TRUE(r.exists);
  ASSERT_FALSE(r.known);
  ASSERT_EQ(r.realm, 0);  // still in Body Tempering: their realm opens no region
  for (uint8_t region = 0; region < 3; ++region) {
    EXPECT_FALSE(rivals::reaches(s, region));
    EXPECT_TRUE(rivals::canMeetIn(s, region)) << int(region);
  }
  EXPECT_FALSE(rivals::canMeetIn(s, 3)) << "not a region you have opened";
  rivals::markKnown(s);
  for (uint8_t region = 0; region < 3; ++region) EXPECT_FALSE(rivals::canMeetIn(s, region));
}

TEST(V037Rival, AWalkInYourNewestRegionMeetsThemAtTheirOwnStrength) {
  int met = 0;
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    GameState s = hero(2, 0x7100u + seed);
    const explore::Layout l = explore::layoutOf(s, 1);
    explore::enter(s, l, 1);
    for (int i = 0; i < 3000 && !rivals::rival(s).known; ++i) {
      s.progress.stamina = 20;
      s.progress.hp = 1000;
      for (int k = 0; k < 4 && explore::pendingFight(s, l).kind != explore::FightKind::None &&
                      explore::pendingFight(s, l).kind != explore::FightKind::Meeting;
           ++k) {
        explore::fight(s, l, k == 0 ? explore::FightChoice::Slip : explore::FightChoice::Fight);
        s.progress.hp = 1000;
      }
      if (explore::meetingRegion(s) >= 0) explore::meet(s, l, explore::MeetChoice::WalkOn);
      uint8_t n[4];
      const uint8_t c = explore::neighbours(explore::position(s, l, 1), n);
      bool moved = false;
      for (uint8_t k = 0; k < c && !moved; ++k) {
        const uint8_t t = n[(i + k) % c];
        if (explore::canStep(s, l, 1, t) != explore::StepError::None) continue;
        const explore::StepResult r = explore::step(s, l, 1, t);
        moved = true;
        if (r.meeting) {
          const explore::Meeting m = explore::pendingMeeting(s, l);
          if (m.rival) {
            EXPECT_EQ(m.level, rivals::rivalLevel(s)) << "their own strength, not scaled to you";
            EXPECT_TRUE(rivals::rival(s).known);
            ++met;
          }
        }
      }
      if (!moved) break;
    }
  }
  EXPECT_GE(met, 3) << "a Body Tempering rival met in the second region";
}

// --- B. Cowards and outgrown nemeses ----------------------------------------------------------

TEST(V037Nemesis, ACowardRunsTwiceThenIsCornered) {
  int cornered = 0;
  for (uint32_t seed = 1; seed < 80 && cornered == 0; ++seed) {
    GameState s = hero(3, seed * 31u);
    const auto i = static_cast<uint8_t>(rivals::bear(s, explore::Enemy::Beast, 0, 12, 5u, 5u));
    uint8_t* b = nemBytes(s, i);
    b[4] = static_cast<uint8_t>(rivals::Trait::Coward);
    b[6] = 1 << 4;
    const explore::Layout l = explore::layoutOf(s, 0);
    explore::enter(s, l, 0);
    int fled = 0;
    for (int step = 0; step < 4000; ++step) {
      b[20] = b[21] = 0;  // never resting
      s.progress.stamina = 20;
      if (explore::meetingRegion(s) >= 0) explore::meet(s, l, explore::MeetChoice::WalkOn);
      const explore::Fight waiting = explore::pendingFight(s, l);
      if (waiting.kind != explore::FightKind::None && waiting.nemesis < 0)
        explore::fight(s, l, explore::FightChoice::Slip);
      if (explore::pendingFight(s, l).kind != explore::FightKind::None) break;
      uint8_t n[4];
      const uint8_t c = explore::neighbours(explore::position(s, l, 0), n);
      uint8_t to = explore::kNoTile;
      for (uint8_t k = 0; k < c && to == explore::kNoTile; ++k) {
        const uint8_t t = n[(step + k) % c];
        if (explore::canStep(s, l, 0, t) == explore::StepError::None) to = t;
      }
      if (to == explore::kNoTile) break;
      const explore::StepResult r = explore::step(s, l, 0, to);
      if (r.nemesisFled >= 0) {
        ++fled;
        EXPECT_LE(fled, rivals::kCowardFlights);
        EXPECT_FALSE(r.fight);
      }
      if (r.nemesisCornered >= 0) {
        EXPECT_EQ(fled, rivals::kCowardFlights);
        EXPECT_TRUE(r.fight);
        EXPECT_EQ(explore::pendingFight(s, l).nemesis, i);
        rivals::Nemesis nm;
        ASSERT_TRUE(rivals::nemesis(s, i, nm));
        EXPECT_TRUE(rivals::cornered(nm));
        ++cornered;
        break;
      }
    }
  }
  EXPECT_EQ(cornered, 1);
}

TEST(V037Nemesis, OneTrivialForYouOverTenReadingHoursLeavesAndFreesItsPlace) {
  GameState s = hero(5);
  for (int k = 0; k < 3; ++k) rivals::bear(s, explore::Enemy::Beast, 0, 12, 100u + k, 7u);
  ASSERT_EQ(rivals::nemesisCount(s), 3);
  EXPECT_EQ(rivals::bear(s, explore::Enemy::Bandit, 0, 12, 200u, 7u), -1) << "no place left";
  s.progress.gameReadingSeconds = 3600;
  EXPECT_EQ(rivals::roam(s), 0) << "the clock starts";
  s.progress.gameReadingSeconds = 3600 + 10 * 3600 - 600;
  EXPECT_EQ(rivals::roam(s), 0) << "not yet 10 reading hours";
  s.progress.gameReadingSeconds = 3600 + 10 * 3600;
  const uint8_t gone = rivals::roam(s);
  EXPECT_EQ(gone, 0x07);
  EXPECT_EQ(rivals::nemesisCount(s), 3) << "still there to be named";
  rivals::letGo(s, gone);
  EXPECT_EQ(rivals::nemesisCount(s), 0);
  EXPECT_GE(rivals::bear(s, explore::Enemy::Bandit, 1, 30, 300u, 7u), 0) << "a new enemy can take a place";
}

TEST(V037Nemesis, ADangerousOneStaysAndTheClockStartsOverWhenItCatchesUp) {
  GameState s = hero(1);
  s.progress.stage = 0;
  const auto i = static_cast<uint8_t>(rivals::bear(s, explore::Enemy::Beast, 0, 12, 5u, 5u));
  s.progress.gameReadingSeconds = 3600;
  for (int h = 0; h < 30; ++h) {
    s.progress.gameReadingSeconds += 3600;
    EXPECT_EQ(rivals::roam(s), 0) << "not Trivial for you";
  }
  // Trivial for 6 hours, then it grows back to Weak or more: the 10 hours start again.
  s.progress.realm = 5;
  rivals::roam(s);
  s.progress.gameReadingSeconds += 6 * 3600;
  EXPECT_EQ(rivals::roam(s), 0);
  nemBytes(s, i)[3] = 60;  // it won and grew
  EXPECT_EQ(rivals::roam(s), 0);
  nemBytes(s, i)[3] = 12;
  EXPECT_EQ(rivals::roam(s), 0) << "Trivial again: the clock starts over";
  s.progress.gameReadingSeconds += 6 * 3600;
  EXPECT_EQ(rivals::roam(s), 0) << "only 6 hours since it was Trivial again";
  s.progress.gameReadingSeconds += 4 * 3600;
  EXPECT_EQ(rivals::roam(s), 1u << i);
}

TEST(V037Nemesis, AnOlderSaveStartsTheClockOnItsNextVisit) {
  // Bytes 22-23 were spare (0) before v0.3.7: the first visit starts the clock, nothing leaves at once.
  GameState s = hero(5);
  const auto i = static_cast<uint8_t>(rivals::bear(s, explore::Enemy::Beast, 0, 12, 5u, 5u));
  nemBytes(s, i)[22] = nemBytes(s, i)[23] = 0;
  s.progress.gameReadingSeconds = 500u * 3600u;
  EXPECT_EQ(rivals::roam(s), 0);
  EXPECT_EQ(rivals::nemesisCount(s), 1);
}

// --- C. More encounters -----------------------------------------------------------------------

TEST(V037Encounters, TheAverageRateIsAboutSeventeenPercent) {
  uint32_t sum = 0, n = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    GameState s = hero(9, seed * 977u);
    for (uint8_t r = 0; r < explore::kRegionCount; ++r) {
      sum += explore::layoutOf(s, r).encounterPercent;
      ++n;
    }
  }
  EXPECT_NEAR(static_cast<double>(sum) / n, 17.5, 0.8);
}
