#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "StatsBridge.h"

namespace cultivation {
namespace bridge {
namespace {

void copyTotals(StatsSnapshot& s, const StatsSample& now) {
  s.seconds = now.seconds;
  s.pages = now.pages;
  s.sessions = now.sessions;
  s.completedBooks = now.completedBooks;
  s.timeOfDaySeconds = now.timeOfDaySeconds;
  s.dayOfWeekSeconds = now.dayOfWeekSeconds;
}

bool anyTotalWentDown(const StatsSnapshot& s, const StatsSample& now) {
  if (now.seconds < s.seconds || now.pages < s.pages || now.sessions < s.sessions) return true;
  for (size_t i = 0; i < s.timeOfDaySeconds.size(); ++i) {
    if (now.timeOfDaySeconds[i] < s.timeOfDaySeconds[i]) return true;
  }
  for (size_t i = 0; i < s.dayOfWeekSeconds.size(); ++i) {
    if (now.dayOfWeekSeconds[i] < s.dayOfWeekSeconds[i]) return true;
  }
  return false;
}

uint32_t minU32(const uint32_t a, const uint32_t b) { return a < b ? a : b; }

// Splits `seconds` read across `days` calendar days into soft-cap tiers.
// `alreadyCounted` is reading credited earlier on one of those days, which
// has used up part of that day's full and half tiers.
void splitIntoTiers(uint32_t seconds, const uint32_t alreadyCounted, const uint32_t days, SoftCapTiers& out) {
  if (seconds == 0 || days == 0) return;
  const uint64_t fullCapacity = static_cast<uint64_t>(days) * kFullTierSeconds;
  const uint64_t halfCapacity = static_cast<uint64_t>(days) * kHalfTierSeconds;
  const uint32_t usedFull = minU32(alreadyCounted, kFullTierSeconds);
  const uint32_t usedHalf = minU32(alreadyCounted - usedFull, kHalfTierSeconds);
  const uint64_t freeFull = fullCapacity - usedFull;
  const uint64_t freeHalf = halfCapacity - usedHalf;

  const uint32_t full = static_cast<uint32_t>(seconds < freeFull ? seconds : freeFull);
  seconds -= full;
  const uint32_t half = static_cast<uint32_t>(seconds < freeHalf ? seconds : freeHalf);
  seconds -= half;
  out.fullSeconds += full;
  out.halfSeconds += half;
  out.quarterSeconds += seconds;
}

// How many days in [first, last] fall on weekday w (Monday = 0).
uint32_t weekdayOccurrences(const uint32_t first, const uint32_t last, const uint8_t w) {
  const uint32_t span = last - first + 1;
  uint32_t n = span / 7;
  const uint32_t remainder = span % 7;
  // The `remainder` days at the end of the span are last, last-1, ...
  for (uint32_t k = 0; k < remainder; ++k) {
    if (weekdayOf(last - k) == w) ++n;
  }
  return n;
}

}  // namespace

StatsSnapshot takeBaseline(const StatsSample& now) {
  StatsSnapshot s;
  copyTotals(s, now);
  s.lastVisitDay = now.today;
  s.secondsCountedOnLastVisitDay = 0;
  return s;
}

BaselineTotals baselineTotals(const StatsSample& now) {
  BaselineTotals b;
  b.seconds = now.seconds;
  b.pages = now.pages;
  b.sessions = now.sessions;
  b.completedBooks = now.completedBooks;
  return b;
}

VisitGains collect(StatsSnapshot& snapshot, const StatsSample& now, RevisionWatch& watch) {
  VisitGains g;

  const bool revisionChanged = watch.known && watch.value != now.resetRevision;
  watch.known = true;
  watch.value = now.resetRevision;

  const bool sameDay = now.today != StatsSnapshot::kNoDay && now.today == snapshot.lastVisitDay;
  if (revisionChanged || anyTotalWentDown(snapshot, now)) {
    g.resetDetected = true;
    copyTotals(snapshot, now);
    if (!sameDay) snapshot.secondsCountedOnLastVisitDay = 0;
    snapshot.lastVisitDay = now.today;
    return g;
  }

  g.seconds = now.seconds - snapshot.seconds;
  g.pages = now.pages - snapshot.pages;
  g.sessions = now.sessions - snapshot.sessions;
  g.sessionsOfFiveMinutes = minU32(g.sessions, g.seconds / kFiveMinutes);
  g.completedBooksDelta = static_cast<int32_t>(now.completedBooks - snapshot.completedBooks);
  for (size_t i = 0; i < g.timeOfDaySeconds.size(); ++i) {
    g.timeOfDaySeconds[i] = now.timeOfDaySeconds[i] - snapshot.timeOfDaySeconds[i];
  }

  uint32_t countedToday = 0;
  const bool datesUsable = now.today != StatsSnapshot::kNoDay && snapshot.lastVisitDay != StatsSnapshot::kNoDay &&
                           now.today >= snapshot.lastVisitDay;
  if (!datesUsable) {
    // No trustworthy dates (clock not set, or it went backwards): treat the
    // whole gap as one day. Devices without a reliable clock get "reading
    // days" in a later milestone (DOC: Devices without a reliable clock).
    const uint32_t used = sameDay ? snapshot.secondsCountedOnLastVisitDay : 0;
    splitIntoTiers(g.seconds, used, 1, g.tiers);
    countedToday = used + g.seconds;
  } else {
    // CrossInk also keeps per-weekday totals. Their differences say on which
    // weekdays the new reading happened, so each day gets its own cap. When
    // the gap is a week or longer, the same weekday occurs several times and
    // shares that many days of allowance (reading assumed spread evenly).
    const uint32_t first = snapshot.lastVisitDay;
    const uint32_t last = now.today;
    const uint8_t firstWeekday = weekdayOf(first);
    const uint8_t todayWeekday = weekdayOf(last);
    uint32_t attributed = 0;
    for (uint8_t w = 0; w < 7; ++w) {
      const uint32_t delta = now.dayOfWeekSeconds[w] - snapshot.dayOfWeekSeconds[w];
      if (delta == 0) continue;
      attributed += delta;
      uint32_t days = weekdayOccurrences(first, last, w);
      if (days == 0) days = 1;  // reading stamped on a weekday outside the gap (clock adjusted)
      const uint32_t used = w == firstWeekday ? snapshot.secondsCountedOnLastVisitDay : 0;
      splitIntoTiers(delta, used, days, g.tiers);
      if (w == todayWeekday) countedToday += (sameDay ? delta : delta / days);
    }
    // Reading CrossInk could not stamp with a date (clock briefly unset).
    const uint32_t unattributed = g.seconds > attributed ? g.seconds - attributed : 0;
    splitIntoTiers(unattributed, 0, last - first + 1, g.tiers);
    if (sameDay) countedToday += snapshot.secondsCountedOnLastVisitDay + unattributed;
  }

  copyTotals(snapshot, now);
  snapshot.lastVisitDay = now.today;
  snapshot.secondsCountedOnLastVisitDay = minU32(countedToday, kSecondsPerDay);
  return g;
}

}  // namespace bridge
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
