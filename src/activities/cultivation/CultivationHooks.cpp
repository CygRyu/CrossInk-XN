#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <HalStorage.h>
#include <Logging.h>

#include "CultivationHooks.h"
#include "GameState.h"
#include "ScrollStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

namespace CultivationHooks {

void onBookCompletionChanged(const std::string& title, const BookReadingStats& stats, const bool completed,
                             const float progressPercent, const uint32_t sessionSeconds,
                             const ReadingStatsDateTime* sessionStart) {
  using namespace cultivation;
  // Baseline rule: without a character, a finished book earns nothing (and
  // nothing is written for people who never open the game).
  if (!Storage.exists(save::kSavePath)) return;

  uint32_t buckets[4];
  for (int i = 0; i < 4; ++i) buckets[i] = stats.timeOfDaySeconds[i];
  uint32_t seconds = stats.totalReadingSeconds;
  if (sessionSeconds > 0) {
    seconds = seconds > UINT32_MAX - sessionSeconds ? UINT32_MAX : seconds + sessionSeconds;
    if (sessionStart != nullptr && sessionStart->isValid()) {
      const int b = static_cast<int>(readingTimeBucketForHour(sessionStart->hour));
      if (b >= 0 && b < 4) buckets[b] += sessionSeconds;
    }
  }
  const scrollstore::PendingRecord r =
      scrollstore::recordFor(title.c_str(), seconds, buckets, progressPercent, completed);
  if (scrollstore::append(r)) {
    LOG_INF("CULT", "Book %s: %lu s read, %u%% reached", completed ? "finished" : "unfinished",
            static_cast<unsigned long>(seconds), static_cast<unsigned>(r.book.shareRead));
  }
}

}  // namespace CultivationHooks

#endif  // CROSSINK_CULTIVATION
