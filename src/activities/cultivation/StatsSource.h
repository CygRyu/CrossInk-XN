#pragma once

#include "StatsBridge.h"

namespace cultivation {

// Reads this device's own reading stats and today's date. Read-only: it calls
// GlobalReadingStats::load() (never loadAggregated(), so stats synced from
// other devices are never counted) and never saves any CrossInk stats file.
// Device-only; host tests build StatsSample by hand instead.
StatsSample readLocalStats();
// v0.2.8: the local hour (0-23), or -1 when the clock is not set (a nemesis that fears the dawn).
int currentLocalHour();

}  // namespace cultivation
