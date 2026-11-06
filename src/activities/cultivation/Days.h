#pragma once

#include <cstdint>

#include "GameState.h"
#include "StatsBridge.h"

// v0.3.1 (agreed with the user in session 15): reading days for devices
// without a date. Pure rules: no I/O, no UI.
//
// The design doc planned "reading days" measured on a drifting clock. This
// firmware has none on the original X4: its board profile has no clock chip,
// and its sleep turns the power off, so every wake starts from nothing. What
// the game can see is reading time and whether the device slept since the
// last visit (a wake is a new boot). So, without a valid date:
//
//   A reading day ends on the first visit after the device has slept, once at
//   least kReadingDaySeconds (60 minutes, user) of reading have built up since
//   the last rollover. Several sittings in one boot are always one day.
//
// The omen, the daily tasks and their reroll, the streak (it counts reading
// days in a row, user), the rival's days and the ranking's once-a-day bouts
// all run on that day. The weekly allowance does not (user: it stays off
// without a date, as before). With a date nothing here changes anything.
//
// Switching: the first visit without a date continues the day the daily
// systems already had (or starts at day 0); the first visit with a date again
// shifts every stored day so that the switch counts as exactly one day. A
// device that has its date never runs this.
//
// Save: GameState::flagsAndCodex bytes 40-44 (spare since v0.3.0; zeroed for
// a new life with the rest of the life):
//   40     flags: 0x01 reading days were in use at the last visit
//   41-44  gameReadingSeconds at the last rollover (u32)
namespace cultivation {
namespace days {

constexpr uint32_t kReadingDaySeconds = 60u * 60u;  // user, session 15
constexpr uint8_t kFlagsByte = 40;
constexpr uint8_t kAnchorByte = 41;
constexpr uint8_t kInUse = 0x01;

struct Result {
  bool readingDays = false;  // this visit runs on a reading day
  bool rolled = false;       // a new reading day began on this visit
  bool switched = false;     // the date came back: stored days were shifted
  int32_t shift = 0;         // by how much
};

// Call once per visit, after bridge::collect() and absorb() and before
// daily::update() and rivals::update(). With no date in `now`, gives it the
// reading day (now.today) and sets now.readingDay. `slept` is true on the
// first visit of this boot.
Result resolve(GameState& state, StatsSample& now, bool slept);

bool inUse(const GameState& state);  // reading days were in use at the last visit

// Shifts every stored day of the daily systems and the rival by `delta`
// (days stored as "+ 1, 0 = none" stay none). Used by resolve() on a switch.
void shiftStoredDays(GameState& state, int32_t delta);

}  // namespace days
}  // namespace cultivation
