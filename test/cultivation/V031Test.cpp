#include <Codex.h>
#include <Daily.h>
#include <Days.h>
#include <GameState.h>
#include <Rivals.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>

// v0.3.1 (agreed with the user in session 15): reading days for devices
// without a date (Days.h). A reading day ends on the first visit after a
// sleep once an hour of reading has built up; with the date nothing changes.

using namespace cultivation;

namespace {

constexpr uint32_t kNoDay = StatsSnapshot::kNoDay;
constexpr uint32_t kMonday = 9802;

struct Game {
  GameState state;
  StatsSample reader;  // CrossInk's totals; `today` set per visit
  RevisionWatch watch;
  days::Result last;
  rivals::News rival;

  explicit Game(const uint32_t creationDay = kNoDay) {
    CharacterDraft d;
    snprintf(d.name, sizeof(d.name), "Lin Yue");
    d.destiny.purity = 50;
    d.destiny.talent = 2;
    d.origin = Origin::StreetOrphan;
    d.worldSeed = 0x5EED1234;
    StatsSample at = reader;
    at.today = creationDay;
    createCharacter(state, d, at);
    state.progress.realm = 1;
    state.progress.hpMax = 100;
    state.progress.hp = 100;
  }

  void read(const uint32_t minutes) {
    reader.seconds += minutes * 60u;
    reader.pages += minutes;
    reader.sessions += 1;
  }

  // The game's visit, in the activity's order. `slept`: the first visit of a boot.
  void visit(const bool slept, const uint32_t today = kNoDay) {
    StatsSample now = reader;
    now.today = today;
    const uint32_t lastDay = state.snapshot.lastVisitDay;
    const VisitGains g = bridge::collect(state.snapshot, now, watch);
    absorb(state, g);
    last = days::resolve(state, now, slept);
    daily::update(state, g, now, lastDay);
    rival = rivals::update(state, now);
  }
};

}  // namespace

TEST(V031Days, WithoutADateTheDailySystemsStartOnReadingDayZero) {
  Game g;
  g.read(20);
  g.visit(true);
  EXPECT_TRUE(g.last.readingDays);
  EXPECT_FALSE(g.last.rolled);
  EXPECT_TRUE(days::inUse(g.state));
  EXPECT_TRUE(daily::live(g.state));
  EXPECT_EQ(daily::today(g.state), 0u);
  EXPECT_NE(daily::omen(g.state), daily::Omen::None);
  EXPECT_NE(daily::task(g.state, 0), daily::Task::None);
  EXPECT_EQ(daily::minutesToday(g.state), 20);
  EXPECT_EQ(daily::currentStreak(g.state), 1);
  // The weekly allowance stays off without a date (user): the snapshot keeps no day.
  EXPECT_EQ(g.state.snapshot.lastVisitDay, kNoDay);
}

TEST(V031Days, AReadingDayNeedsAnHourAndASleep) {
  Game g;
  g.visit(true);
  // Several sittings in one boot are one day, however long.
  for (int i = 0; i < 6; ++i) {
    g.read(45);
    g.visit(false);
    EXPECT_FALSE(g.last.rolled) << i;
  }
  EXPECT_EQ(daily::today(g.state), 0u);
  // After a sleep, the hour has long been reached: a new day.
  g.read(5);
  g.visit(true);
  EXPECT_TRUE(g.last.rolled);
  EXPECT_EQ(daily::today(g.state), 1u);
  // Now 59 minutes and a sleep: not yet. One more minute and a sleep: the next day.
  g.read(59);
  g.visit(true);
  EXPECT_FALSE(g.last.rolled);
  g.read(1);
  g.visit(false);
  EXPECT_FALSE(g.last.rolled);  // an hour, but no sleep
  g.visit(true);
  EXPECT_TRUE(g.last.rolled);
  EXPECT_EQ(daily::today(g.state), 2u);
  EXPECT_EQ(days::kReadingDaySeconds, 3600u);
}

TEST(V031Days, ANewReadingDayBringsNewTasksAndKeepsItsOwnReading) {
  Game g;
  g.visit(true);
  const daily::Omen first = daily::omen(g.state);
  int changed = 0;
  for (int d = 0; d < 20; ++d) {
    g.read(70);
    g.visit(true);
    ASSERT_TRUE(g.last.rolled);
    EXPECT_EQ(daily::minutesToday(g.state), 70);  // the sitting that opened the day counts on it
    EXPECT_FALSE(daily::collected(g.state, 0));
    if (daily::omen(g.state) != first) ++changed;
  }
  EXPECT_GT(changed, 5);
  EXPECT_EQ(daily::today(g.state), 20u);
}

TEST(V031Days, TheStreakCountsReadingDaysInARow) {
  Game g;
  g.visit(true);
  for (int d = 0; d < 9; ++d) {
    g.read(60);
    g.visit(true);
    g.visit(false);  // a second look the same day changes nothing
  }
  EXPECT_EQ(daily::currentStreak(g.state), 10);
  EXPECT_EQ(daily::longestStreak(g.state), 10);
}

TEST(V031Days, TheRivalTrainsOneDayPerReadingDayAndNeverRests) {
  Game g;
  g.visit(true);
  EXPECT_TRUE(g.rival.created);
  for (int d = 0; d < 30; ++d) {
    g.read(30);
    g.visit(true);  // half an hour: the same day
    EXPECT_EQ(g.rival.daysCredited, 0u) << d;
    g.read(30);
    g.visit(true);  // the hour is full: a new day
    EXPECT_EQ(g.rival.daysCredited, 1u) << d;
    EXPECT_FALSE(g.rival.hiatusBegan);
  }
  EXPECT_FALSE(rivals::rival(g.state).hiatus);
}

TEST(V031Days, TheRankingWorksOnReadingDays) {
  Game g;
  g.visit(true);
  ASSERT_TRUE(rivals::ladderOpen(g.state));
  EXPECT_NE(rivals::challengeBout(g.state).block, rivals::Block::NoDate);
  rivals::challenge(g.state);
  EXPECT_EQ(rivals::challengeBout(g.state).block == rivals::Block::DoneToday ||
                rivals::challengeBout(g.state).block == rivals::Block::NoHp,
            true);
  g.state.progress.hp = 100;
  g.read(60);
  g.visit(true);
  ASSERT_TRUE(g.last.rolled);
  EXPECT_NE(rivals::challengeBout(g.state).block, rivals::Block::DoneToday);
}

TEST(V031Days, WithADateNothingChanges) {
  Game g(kMonday);
  g.read(90);
  g.visit(true, kMonday);
  EXPECT_FALSE(g.last.readingDays);
  EXPECT_FALSE(days::inUse(g.state));
  g.read(90);
  g.visit(true, kMonday);  // a sleep and an hour, but the same date: the same day
  EXPECT_EQ(daily::today(g.state), kMonday);
  g.visit(true, kMonday + 1);
  EXPECT_EQ(daily::today(g.state), kMonday + 1);
  for (uint8_t i = days::kFlagsByte; i < days::kAnchorByte + 4; ++i) EXPECT_EQ(g.state.flagsAndCodex[i], 0) << +i;
}

TEST(V031Days, LosingTheDateCarriesOnTheSameDay) {
  Game g(kMonday);
  g.read(20);
  g.visit(true, kMonday);
  const daily::Task t0 = daily::task(g.state, 0);
  g.read(20);
  g.visit(true);  // the clock is gone
  EXPECT_TRUE(g.last.readingDays);
  EXPECT_FALSE(g.last.rolled);
  EXPECT_EQ(daily::today(g.state), kMonday);
  EXPECT_EQ(daily::task(g.state, 0), t0);
  EXPECT_EQ(daily::minutesToday(g.state), 40);
  g.read(60);
  g.visit(true);
  EXPECT_EQ(daily::today(g.state), kMonday + 1);
}

TEST(V031Days, TheDateComingBackCountsAsOneDay) {
  // Never had a date: reading days from 0. The date appears years later.
  Game g;
  g.visit(true);
  for (int d = 0; d < 5; ++d) {
    g.read(60);
    g.visit(true);
  }
  ASSERT_EQ(daily::today(g.state), 5u);
  const uint32_t streakBefore = daily::longestStreak(g.state);
  g.read(10);
  g.visit(true, kMonday + 3);
  EXPECT_FALSE(g.last.readingDays);
  EXPECT_TRUE(g.last.switched);
  EXPECT_EQ(g.last.shift, static_cast<int32_t>(kMonday + 2 - 5));
  EXPECT_FALSE(days::inUse(g.state));
  EXPECT_EQ(daily::today(g.state), kMonday + 3);
  EXPECT_EQ(g.rival.daysCredited, 1u);  // not the thousands of days since day 0
  EXPECT_EQ(daily::longestStreak(g.state), streakBefore);

  // Reading days that ran ahead of the calendar: shifted back, still one day.
  Game h(kMonday);
  h.visit(true, kMonday);
  for (int d = 0; d < 12; ++d) {
    h.read(60);
    h.visit(true);
  }
  ASSERT_EQ(daily::today(h.state), kMonday + 11);  // the first visit without the date carried on Monday
  h.read(10);
  h.visit(true, kMonday + 4);
  EXPECT_EQ(h.last.shift, -8);
  EXPECT_EQ(daily::today(h.state), kMonday + 4);
  EXPECT_EQ(h.rival.daysCredited, 1u);
}

TEST(V031Days, ShiftingMovesEveryStoredDayAndKeepsNone) {
  Game g(kMonday);
  g.visit(true, kMonday);
  rivals::challenge(g.state);
  GameState s = g.state;
  days::shiftStoredDays(s, 10);
  EXPECT_EQ(daily::today(s), kMonday + 10);
  s.progress.hp = 100;
  // The ladder's challenge day moved with it: still "done" on the shifted today.
  EXPECT_EQ(rivals::challengeBout(s).block, rivals::Block::DoneToday);
  // A day never stored stays none.
  GameState empty;
  days::shiftStoredDays(empty, 5);
  EXPECT_EQ(daily::today(empty), kNoDay);
}

TEST(V031Days, TheSaveBytesAreSpareAndBelongToTheLife) {
  // Bytes 40-44 come after the codex (8-39) and are not kept across lives.
  EXPECT_GE(days::kFlagsByte, codex::kFirstByte + codex::kBytes);
  EXPECT_LT(days::kAnchorByte + 4, 48);
  EXPECT_NE(days::kFlagsByte, kGuideSeenByte);
}
