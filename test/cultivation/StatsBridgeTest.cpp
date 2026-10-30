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

TEST(StatsBridge, SessionsOfFiveMinutesAreApproximated) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  for (int i = 0; i < 6; ++i) dev.read(kMonday, 1);  // six one-minute peeks
  dev.read(kMonday, 10);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.sessions, 7u);
  EXPECT_EQ(g.sessionsOfFiveMinutes, 3u);  // 16 minutes / 5
}

TEST(StatsBridge, SoftCapWithinOneDay) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 300);  // 5 hours
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 120 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 120 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 60 * kMin);
  EXPECT_EQ(g.tiers.qualifyingSeconds(), (120 + 60 + 15) * kMin);
}

TEST(StatsBridge, SoftCapRemembersEarlierVisitsTheSameDay) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 100);
  EXPECT_EQ(bridge::collect(snap, dev.on(kMonday), watch).tiers.fullSeconds, 100 * kMin);
  dev.read(kMonday, 100);  // same day: only 20 more full minutes remain
  const VisitGains g = bridge::collect(snap, dev.on(kMonday), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 20 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 80 * kMin);
  EXPECT_EQ(snap.secondsCountedOnLastVisitDay, 200 * kMin);
}

TEST(StatsBridge, NewDayStartsAFreshCap) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  dev.read(kMonday, 200);
  bridge::collect(snap, dev.on(kMonday), watch);
  dev.read(kMonday + 1, 100);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 1), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 100 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 0u);
}

TEST(StatsBridge, GapOfSeveralDaysCapsEachDaySeparately) {
  // Read 3 h on Monday, Tuesday and Wednesday; visit only on Wednesday.
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday));
  RevisionWatch watch;
  for (uint32_t d = 0; d < 3; ++d) dev.read(kMonday + d, 180);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 2), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 3 * 120 * kMin);
  EXPECT_EQ(g.tiers.halfSeconds, 3 * 60 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 0u);
}

TEST(StatsBridge, GapLongerThanAWeekSharesWeekdayAllowance) {
  // Two Mondays with 3 h each; the weekday bucket merges them (6 h on "Monday").
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(kMonday - 1));
  RevisionWatch watch;
  dev.read(kMonday, 180);
  dev.read(kMonday + 7, 180);
  const VisitGains g = bridge::collect(snap, dev.on(kMonday + 8), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 240 * kMin);  // 2 Mondays x 120
  EXPECT_EQ(g.tiers.halfSeconds, 120 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 0u);
}

TEST(StatsBridge, ReadingWithoutAClockIsCappedAsOneDay) {
  FakeDevice dev;
  StatsSnapshot snap = bridge::takeBaseline(dev.on(StatsSnapshot::kNoDay));
  RevisionWatch watch;
  dev.read(0, 300, 0, 2, /*clockSet=*/false);
  const VisitGains g = bridge::collect(snap, dev.on(StatsSnapshot::kNoDay), watch);
  EXPECT_EQ(g.tiers.fullSeconds, 120 * kMin);
  EXPECT_EQ(g.tiers.quarterSeconds, 60 * kMin);
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
