#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "StatsSource.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

namespace cultivation {

static_assert(READING_TIME_BUCKET_COUNT == 4, "StatsSample.timeOfDaySeconds must match CrossInk's buckets");
static_assert(READING_DAY_OF_WEEK_COUNT == 7, "StatsSample.dayOfWeekSeconds must match CrossInk's weekdays");

StatsSample readLocalStats() {
  // GlobalReadingStats is ~160 bytes; it lives on the stack only for this call.
  const GlobalReadingStats stats = GlobalReadingStats::load();

  StatsSample s;
  s.seconds = stats.totalReadingSeconds;
  s.pages = stats.totalPagesTurned;
  s.sessions = stats.totalSessions;
  s.completedBooks = stats.completedBooks;
  for (size_t i = 0; i < s.timeOfDaySeconds.size(); ++i) s.timeOfDaySeconds[i] = stats.timeOfDaySeconds[i];
  for (size_t i = 0; i < s.dayOfWeekSeconds.size(); ++i) s.dayOfWeekSeconds[i] = stats.dayOfWeekSeconds[i];
  s.resetRevision = GlobalReadingStats::localResetRevision();

  ReadingStatsDateTime now;
  if (getCurrentLocalReadingStatsDateTime(now)) s.today = readingStatsDayIndex(now.date);
  return s;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
