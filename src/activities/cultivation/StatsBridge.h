#pragma once

#include <array>
#include <cstdint>

#include "GameState.h"

namespace cultivation {

// What the game reads from CrossInk on each visit: this device's own totals
// (GlobalReadingStats::load(), never loadAggregated()) plus today's date.
// Filled by readLocalStats() on the device, or by hand in host tests.
struct StatsSample {
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  uint32_t completedBooks = 0;
  std::array<uint32_t, 4> timeOfDaySeconds{};
  std::array<uint32_t, 7> dayOfWeekSeconds{};
  uint32_t today = StatsSnapshot::kNoDay;  // readingStatsDayIndex(), or kNoDay when the clock is not set
  uint32_t resetRevision = 0;              // GlobalReadingStats::localResetRevision()
};

// GlobalReadingStats::localResetRevision() is a RAM counter that restarts at 0
// on every boot, so it cannot be saved. The caller keeps one of these in RAM
// for the current boot; across reboots, falling totals are the reset signal.
struct RevisionWatch {
  bool known = false;
  uint32_t value = 0;
};

// Reading seconds split by the daily soft cap (DOC: 100% for the first 120
// minutes of a day, 50% up to 240, 25% after).
struct SoftCapTiers {
  uint32_t fullSeconds = 0;
  uint32_t halfSeconds = 0;
  uint32_t quarterSeconds = 0;

  // full + half/2 + quarter/4, rounded down.
  uint32_t qualifyingSeconds() const {
    return static_cast<uint32_t>((4ULL * fullSeconds + 2ULL * halfSeconds + quarterSeconds) / 4ULL);
  }
};

// Everything one visit can convert into resources. The bridge only measures;
// turning seconds into Qi, pages into stones and so on happens in the game
// rules, so tuning never touches this file.
struct VisitGains {
  bool resetDetected = false;  // totals went down: snapshot rebased, nothing credited
  uint32_t seconds = 0;        // reading time since the last visit
  SoftCapTiers tiers;          // the same seconds, split by the daily soft cap
  uint32_t pages = 0;
  uint32_t sessions = 0;
  // CrossInk counts sessions but not their lengths, so "+1 stamina per session
  // of 5+ minutes" is approximated as min(new sessions, new seconds / 300).
  uint32_t sessionsOfFiveMinutes = 0;
  int32_t completedBooksDelta = 0;  // can be negative: a book was unmarked
  std::array<uint32_t, 4> timeOfDaySeconds{};
};

namespace bridge {

constexpr uint32_t kFullTierSeconds = 120 * 60;  // first 120 minutes of a day
constexpr uint32_t kHalfTierSeconds = 120 * 60;  // the next 120 minutes
constexpr uint32_t kFiveMinutes = 5 * 60;
constexpr uint32_t kSecondsPerDay = 24 * 60 * 60;

// Monday = 0, matching readingStatsDayOfWeekIndex() (2000-01-01 was a Saturday).
constexpr uint8_t weekdayOf(const uint32_t dayIndex) { return static_cast<uint8_t>((5u + dayIndex) % 7u); }

// Snapshot at character creation: everything read before this moment stays
// out of the game (DOC: "Starting fresh with existing stats").
StatsSnapshot takeBaseline(const StatsSample& now);
BaselineTotals baselineTotals(const StatsSample& now);

// Converts the reading since the last visit into gains and advances the
// snapshot. Never produces negative or doubled gains:
// - Any total that went down (seconds, pages, sessions, time-of-day or
//   weekday buckets), or a reset revision change seen within this boot, means
//   CrossInk's stats were reset or restored: the snapshot is rebased to the
//   current totals and nothing is credited.
// - completedBooks alone going down is not a reset; unmarking a book does it.
VisitGains collect(StatsSnapshot& snapshot, const StatsSample& now, RevisionWatch& watch);

}  // namespace bridge
}  // namespace cultivation
