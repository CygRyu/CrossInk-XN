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

// Splits `seconds` read across `weeks` calendar weeks into allowance tiers.
// `alreadyCounted` is reading credited earlier in one of those weeks, which
// has used up part of that week's full and half tiers.
void splitIntoTiers(uint32_t seconds, const uint32_t alreadyCounted, const uint32_t weeks, SoftCapTiers& out) {
  if (seconds == 0 || weeks == 0) return;
  const uint64_t fullCapacity = static_cast<uint64_t>(weeks) * kFullTierSeconds;
  const uint64_t halfCapacity = static_cast<uint64_t>(weeks) * kHalfTierSeconds;
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
  if (last < first) return 0;
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
  s.secondsCountedInLastVisitWeek = 0;
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

  const bool datesUsable = now.today != StatsSnapshot::kNoDay && snapshot.lastVisitDay != StatsSnapshot::kNoDay &&
                           now.today >= snapshot.lastVisitDay;
  const bool sameWeek = datesUsable && weekOf(now.today) == weekOf(snapshot.lastVisitDay);
  if (revisionChanged || anyTotalWentDown(snapshot, now)) {
    g.resetDetected = true;
    copyTotals(snapshot, now);
    if (!sameWeek) snapshot.secondsCountedInLastVisitWeek = 0;
    snapshot.lastVisitDay = now.today;
    return g;
  }

  g.seconds = now.seconds - snapshot.seconds;
  g.pages = now.pages - snapshot.pages;
  g.sessions = now.sessions - snapshot.sessions;
  g.completedBooksDelta = static_cast<int32_t>(now.completedBooks - snapshot.completedBooks);
  for (size_t i = 0; i < g.timeOfDaySeconds.size(); ++i) {
    g.timeOfDaySeconds[i] = now.timeOfDaySeconds[i] - snapshot.timeOfDaySeconds[i];
  }

  uint32_t countedThisWeek = 0;
  if (!datesUsable) {
    // No trustworthy dates (clock not set, or it went backwards): treat the
    // whole gap as one fresh week. Devices without a reliable clock get
    // "reading days" in a later milestone (DOC: Devices without a reliable clock).
    splitIntoTiers(g.seconds, 0, 1, g.tiers);
    countedThisWeek = g.seconds;
  } else if (sameWeek) {
    // Everything since the last visit happened in the week already partly credited.
    const uint32_t used = snapshot.secondsCountedInLastVisitWeek;
    splitIntoTiers(g.seconds, used, 1, g.tiers);
    countedThisWeek = used + g.seconds;
  } else {
    // The gap crosses into a later week. CrossInk's per-weekday totals say on
    // which weekdays the new reading happened; each weekday's reading is
    // spread evenly over that weekday's dates in the gap, and each date
    // belongs to the first week (partly used), a full week in between, or
    // the current week.
    const uint32_t first = snapshot.lastVisitDay;
    const uint32_t last = now.today;
    const uint32_t firstWeek = weekOf(first);
    const uint32_t lastWeek = weekOf(last);
    const uint32_t firstWeekEnd = lastDayOfWeek(firstWeek);
    const uint32_t lastWeekStart = firstDayOfWeek(lastWeek);
    uint64_t inFirst = 0;
    uint64_t inMiddle = 0;
    uint64_t inLast = 0;
    uint32_t attributed = 0;
    for (uint8_t w = 0; w < 7; ++w) {
      const uint32_t delta = now.dayOfWeekSeconds[w] - snapshot.dayOfWeekSeconds[w];
      if (delta == 0) continue;
      attributed += delta;
      const uint32_t dates = weekdayOccurrences(first, last, w);
      if (dates == 0) {  // stamped on a weekday outside the gap (clock adjusted)
        inLast += delta;
        continue;
      }
      const uint32_t datesInFirst = weekdayOccurrences(first, firstWeekEnd, w);
      const uint32_t datesInLast = weekdayOccurrences(lastWeekStart, last, w);
      const uint32_t share = delta / dates;
      inFirst += static_cast<uint64_t>(share) * datesInFirst;
      inMiddle += static_cast<uint64_t>(share) * (dates - datesInFirst - datesInLast);
      inLast += static_cast<uint64_t>(share) * datesInLast + (delta - share * dates);
    }
    // Reading CrossInk could not stamp with a date (clock briefly unset)
    // counts against the current week, the least generous choice.
    inLast += g.seconds > attributed ? g.seconds - attributed : 0;

    splitIntoTiers(static_cast<uint32_t>(inFirst), snapshot.secondsCountedInLastVisitWeek, 1, g.tiers);
    splitIntoTiers(static_cast<uint32_t>(inMiddle), 0, lastWeek - firstWeek - 1, g.tiers);
    splitIntoTiers(static_cast<uint32_t>(inLast), 0, 1, g.tiers);
    countedThisWeek = static_cast<uint32_t>(inLast < kSecondsPerWeek ? inLast : kSecondsPerWeek);
  }

  copyTotals(snapshot, now);
  snapshot.lastVisitDay = now.today;
  snapshot.secondsCountedInLastVisitWeek = minU32(countedThisWeek, kSecondsPerWeek);
  return g;
}

}  // namespace bridge
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
