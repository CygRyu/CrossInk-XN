#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Daily.h"
#include "Days.h"
#include "Rivals.h"

namespace cultivation {
namespace days {
namespace {

constexpr uint32_t kNoDay = StatsSnapshot::kNoDay;

uint32_t anchorOf(const GameState& s) {
  const uint8_t* p = s.flagsAndCodex.data() + kAnchorByte;
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void putAnchor(GameState& s, const uint32_t v) {
  uint8_t* p = s.flagsAndCodex.data() + kAnchorByte;
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void setInUse(GameState& s, const bool on) {
  uint8_t& f = s.flagsAndCodex[kFlagsByte];
  f = static_cast<uint8_t>(on ? (f | kInUse) : (f & ~kInUse));
}

}  // namespace

bool inUse(const GameState& s) { return (s.flagsAndCodex[kFlagsByte] & kInUse) != 0; }

void shiftStoredDays(GameState& s, const int32_t delta) {
  if (delta == 0) return;
  daily::shiftDays(s, delta);
  rivals::shiftDays(s, delta);
}

Result resolve(GameState& s, StatsSample& now, const bool slept) {
  Result r;
  const uint32_t secs = s.progress.gameReadingSeconds;
  const uint32_t stored = daily::today(s);  // the day the daily systems last had

  if (now.today != kNoDay) {
    // The date is known: calendar days. Coming back from reading days, the
    // switch counts as one day: the stored day becomes yesterday.
    if (inUse(s)) {
      if (stored != kNoDay && now.today >= 1u) {
        r.shift = static_cast<int32_t>(now.today - 1u) - static_cast<int32_t>(stored);
        shiftStoredDays(s, r.shift);
        r.switched = r.shift != 0;
      }
      setInUse(s, false);
    }
    return r;
  }

  r.readingDays = true;
  now.readingDay = true;
  if (!inUse(s) || stored == kNoDay) {
    // The first visit without a date (or a new life): carry on the day the
    // daily systems had, or start at day 0. No rollover now.
    setInUse(s, true);
    putAnchor(s, secs);
    now.today = stored != kNoDay ? stored : 0u;
    return r;
  }
  uint32_t anchor = anchorOf(s);
  if (secs < anchor) {  // cannot happen in one life; be safe
    anchor = secs;
    putAnchor(s, secs);
  }
  if (slept && secs - anchor >= kReadingDaySeconds && stored + 1u < 0xFFFEu) {
    now.today = stored + 1u;
    putAnchor(s, secs);
    r.rolled = true;
  } else {
    now.today = stored;
  }
  return r;
}

}  // namespace days
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
