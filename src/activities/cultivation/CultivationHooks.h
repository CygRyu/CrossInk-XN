#pragma once

#include <cstdint>
#include <string>

#include "CultivationConfig.h"

struct BookReadingStats;
struct ReadingStatsDateTime;

// The game's hooks into CrossInk (DOC: "Book finished: a hook, not a scan").
// CrossInk calls this where it changes completedBooks, always inside
// `#if CROSSINK_CULTIVATION`. It reads the book's stats it is handed and never
// writes CrossInk's files.
namespace CultivationHooks {

#if CROSSINK_CULTIVATION
// A book was marked finished (completed) or unfinished. Appends one 12-byte
// record to /.crosspoint/cultivation/pending_scrolls.bin, only once a
// character exists. `stats` are the book's own stats; a reader that has not
// saved its open session yet passes that session's seconds and start time so
// the last stretch of the book counts too. `progressPercent` is where the
// reader was (0-100, negative if unknown).
void onBookCompletionChanged(const std::string& title, const BookReadingStats& stats, bool completed,
                             float progressPercent, uint32_t sessionSeconds = 0,
                             const ReadingStatsDateTime* sessionStart = nullptr);
#endif

}  // namespace CultivationHooks
