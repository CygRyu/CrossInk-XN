#include <StatsBridge.h>
#include <gtest/gtest.h>

using namespace cultivation;

namespace {
constexpr uint32_t kMonday = 7002;  // (5 + 7002) % 7 == 0
constexpr uint32_t kMin = 60;

// A device that reads and records stats the way CrossInk does.
struct FakeDevice {
  StatsSample s;
  void read(const uint32_t day, const uint32_t minutes, const uint32_t pages = 0, const uint8_t bucket = 2,
            const bool clockSet = true) {
    s.seconds += minutes * kMin;
    s.pages += pages;
    s.sessions += 1;
    if (clockSet) {
      s.timeOfDaySeconds[bucket] += minutes * kMin;
      s.dayOfWeekSeconds[bridge::weekdayOf(day)] += minutes * kMin;
    }
  }
  StatsSample on(const uint32_t day) const {
    StatsSample out = s;
    out.today = day;
    return out;
  }
};
}  // namespace

TEST(StatsBridge, WeekdayMatchesCrossInk) {
  EXPECT_EQ(bridge::weekdayOf(0), 5);  // 2000-01-01 was a Saturday
  EXPECT_EQ(bridge::weekdayOf(kMonday), 0);
}

TEST(StatsBridge, BaselineExcludesEarlierReading) {
  FakeDevice dev;
  dev.read(kMonday - 400, 600, 9000);  // a former life
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_FALSE(g.resetDetected);
  EXPECT_EQ(g.seconds, 0u);
  EXPECT_EQ(g.pages, 0u);

  const BaselineTotals b = bridge::baselineTotals(dev.on(kMonday));
  EXPECT_EQ(b.pages, 9000u);
}

TEST(StatsBridge, DifferencesSinceLastVisit) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 30, 25, 3);
  dev.read(kMonday, 3, 2, 3);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.seconds, 33 * kMin);
  EXPECT_EQ(g.pages, 27u);
  EXPECT_EQ(g.sessions, 2u);
  EXPECT_EQ(g.timeOfDaySeconds[3], 33 * kMin);
  EXPECT_EQ(g.tiers.fullSeconds, 33 * kMin);
  EXPECT_EQ(g.tiers.qualifyingSeconds(), 33 * kMin);

  // Nothing new: nothing gained, and no double counting.
  const VisitGains again = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(again.seconds, 0u);
  EXPECT_EQ(again.tiers.qualifyingSeconds(), 0u);
}

TEST(StatsBridge, WeeksRunMondayToSunday) {
  EXPECT_EQ(bridge::weekOf(kMonday), bridge::weekOf(kMonday + 6));      // Monday..Sunday
  EXPECT_EQ(bridge::weekOf(kMonday + 7), bridge::weekOf(kMonday) + 1);  // next Monday
  EXPECT_EQ(bridge::weekOf(kMonday - 1), bridge::weekOf(kMonday) - 1);  // the Sunday before
  EXPECT_EQ(bridge::firstDayOfWeek(bridge::weekOf(kMonday + 3)), kMonday);
  EXPECT_EQ(bridge::lastDayOfWeek(bridge::weekOf(kMonday + 3)), kMonday + 6);
  EXPECT_EQ(bridge::weekOf(0), bridge::weekOf(1));  // Sat 2000-01-01 and Sun 01-02
  EXPECT_EQ(bridge::firstDayOfWeek(0), 0u);
  EXPECT_EQ(bridge::lastDayOfWeek(0), 1u);
}

TEST(StatsBridge, WeeklyAllowanceWithinOneVisit) {
  // Decision 6A: 100% for the first 14 h of a week, 50% for the next 14 h, 25% after.
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 30 * 60);  // 30 h
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 14 * 60 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 14 * 60 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 2 * 60 * kMin);
  EXPECT_EQ(g.tiers.qualifyingSeconds(), (14 * 60 + 7 * 60 + 30) * kMin);
}

TEST(StatsBridge, WeekendBingeIsNotCappedLikeADay) {
  // 3.5 h on Saturday and Sunday: the old daily cap halved part of it, the weekly allowance does not.
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday + 5, 210);
  dev.read(kMonday + 6, 210);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 6), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 420 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 0u);
}

TEST(StatsBridge, AllowanceRemembersEarlierVisitsTheSameWeek) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 600);
  EXPECT_EQ(bridge::collect(snap, dev.on(kMonday), watch).tiers.fullSeconds, 600 * kMin);
  dev.read(kMonday + 3, 600);  // Thursday, same week: only 4 more full hours remain
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 3), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 240 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 360 * kMin);
  EXPECT_EQ(snap.secondsCountedInLastVisitWeek, 1200 * kMin);
}

TEST(StatsBridge, NewWeekStartsAFreshAllowance) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday + 6, 20 * 60);  // Sunday: 20 h
  bridge::collect(snap, dev.on(kMonday + 6), watch);
  dev.read(kMonday + 7, 100);  // next Monday
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 7), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 100 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 0u);
  EXPECT_EQ(snap.secondsCountedInLastVisitWeek, 100 * kMin);
}

TEST(StatsBridge, GapIntoTheNextWeekSplitsByWeekday) {
  // Wednesday visit with 10 h credited; then 8 h on Saturday (same week) and
  // 8 h on the next Monday; visit on that Tuesday.
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday + 2, 600);
  bridge::collect(snap, dev.on(kMonday + 2), watch);
  dev.read(kMonday + 5, 480);
  dev.read(kMonday + 7, 480);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 8), watch);
  EXPECT_EQ(g.tiers.fullSeconds, (240 + 480) * kMin);  // 4 h left last week + 8 h this week
  EXPECT_EQ(g.tiers.halfSeconds, 240 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 0u);
  EXPECT_EQ(snap.secondsCountedInLastVisitWeek, 480 * kMin);
}

TEST(StatsBridge, GapOfSeveralWeeksGivesEachWeekItsAllowance) {
  // 20 h on each of three Mondays; the weekday bucket merges them (60 h on "Monday").
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  for (uint32_t w = 0; w < 3; ++w) dev.read(kMonday + 7 * w, 20 * 60);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 14), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 3 * 14 * 60 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 3 * 6 * 60 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 0u);
  EXPECT_EQ(snap.secondsCountedInLastVisitWeek, 20 * 60 * kMin);
}

TEST(StatsBridge, GapGainsAlwaysAddUpToTheSecondsRead) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday + 4));
  RevisionWatch watch;
  for (uint32_t d = 0; d < 40; ++d) dev.read(kMonday + 4 + d, 37 + 13 * (d % 5));
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 43), watch);
  EXPECT_EQ(g.tiers.fullSeconds + g.tiers.halfSeconds + g.tiers.quarterSeconds, g.seconds);
}

TEST(StatsBridge, ReadingWithoutAClockIsCappedAsOneWeek) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(StatsSnapshot::kNoDay));
  RevisionWatch watch;
  dev.read(0, 30 * 60, 0, 2, /*clockSet=*/false);
  const VisitGains g = bridge::collect(snap, dev.on(StatsSnapshot::kNoDay), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 14 * 60 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 2 * 60 * kMin);
}

TEST(StatsBridge, ResetIsDetectedWhenTotalsFall) {
  FakeDevice dev;
  dev.read(kMonday, 600, 5000);
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.s = StatsSample{};  // CrossInk "Reset stats"
  dev.read(kMonday + 1, 20, 15);
  VisitGains g = bridge::collect(snap, dev.on(kMonday + 1), watch);
  EXPECT_TRUE(g.resetDetected);
  EXPECT_EQ(g.seconds, 0u);  // never negative, never the whole total again
  EXPECT_EQ(g.pages, 0u);

  // After rebasing, new reading counts normally.
  dev.read(kMonday + 1, 10, 8);
  g = bridge::collect(snap, dev.on(kMonday + 1), watch);
  EXPECT_FALSE(g.resetDetected);
  EXPECT_EQ(g.seconds, 10 * kMin);
  EXPECT_EQ(g.pages, 8u);
}

TEST(StatsBridge, ResetDetectedByRevisionWithinOneBoot) {
  // Reset, then read past the old totals before visiting: only the in-boot
  // revision counter can tell.
  FakeDevice dev;
  dev.read(kMonday, 10, 10);
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  bridge::collect(snap, dev.on(kMonday), watch);  // first visit this boot learns revision 0
  dev.s = StatsSample{};
  dev.s.resetRevision = 1;
  dev.read(kMonday, 60, 60);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_TRUE(g.resetDetected);
  EXPECT_EQ(g.seconds, 0u);
}

TEST(StatsBridge, RevisionStartsUnknownAfterReboot) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch freshBoot;
  dev.s.resetRevision = 3;  // value from an earlier boot means nothing now
  dev.read(kMonday, 10);
  EXPECT_FALSE(bridge::collect(snap, dev.on(kMonday), freshBoot).resetDetected);
}

TEST(StatsBridge, UnmarkingABookIsNotAReset) {
  FakeDevice dev;
  dev.s.completedBooks = 4;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.s.completedBooks = 3;
  dev.read(kMonday, 10);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_FALSE(g.resetDetected);
  EXPECT_EQ(g.completedBooksDelta, -1);
  EXPECT_EQ(g.seconds, 10 * kMin);
}

TEST(StatsBridge, ClockGoingBackwardsDoesNotCrash) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday + 10));
  RevisionWatch watch;
  dev.read(kMonday, 30);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.tiers.qualifyingSeconds(), 30 * kMin);
  EXPECT_EQ(snap.lastVisitDay, kMonday);
}
