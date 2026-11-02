#pragma once

#include <cstdint>

#include "GameState.h"
#include "StatsBridge.h"

// Daos (v0.2.6, milestone 9a; agreed with the user in session 9, HANDOFF 14r).
//
// Five per life, hidden, each tied to a reading habit. Slots open as the
// character rises: 2 on entering Qi Condensation, then 1 each on entering
// Foundation, Core Formation and Nascent Soul. An open slot takes the first
// Dao whose condition is met (the user's rule A); when several are met at
// once, the one furthest along wins (ties by the world seed). After the fifth
// the path has settled: the other four stay closed for this life.
//
//   Dao            habit                condition                          level +1 per
//   Time           reading every day    a 14-day streak                    50 days of longest streak
//   Stillness      long sittings        8 days with 60+ minutes            20 more such days
//   Tide           binges, weekends     3 days with 3+ hours               8 more such days
//   Flowing Water  night reader         10 h at night, night >= 40%        25 h at night
//   Rising Sun     morning reader       10 h in the morning, >= 40%        25 h in the morning
//   Fire           afternoon reader     10 h in the afternoon, >= 40%      25 h in the afternoon
//   Metal          evening reader       10 h in the evening, >= 40%        25 h in the evening
//   Myriad Paths   reading at all hours 15 h, no time of day over 40%      25 h read
//   Fate           finishing books      2 books finished                   3 books
//
// Level = 1 + (measure - condition) / step, at most 9 (steps: the user's
// session-10 choice, slower than the first proposal). Time of day counts from
// the update for characters made before v0.2.6, so their slots open once 5
// hours have been tracked; streaks and books count from the character's
// creation (CrossInk keeps those).
//
// A found Dao at level >= target realm + 2 (at most 9) is a breakthrough key
// that is never spent (option 5B). No Dao touches the Qi rate (decision 1A)
// or stamina.
//
// Save (daosAndMeridians, reserved since milestone 1, so the save version
// stays 5): 0-1 found bits (u16, bit = Dao), 2-9 minutes read per time of day
// in 10-minute units (4 x u16, Morning..Night), 10 days with 60+ minutes,
// 11 days with 3+ hours (both u8, capped), 12-16 the level last announced per
// Dao (a nibble each, Dao i in byte 12 + i / 2, low nibble for even i),
// 17 bit 0x01: "the path has settled" was written. 18-22 are the meridians.
namespace cultivation {
namespace daos {

enum class Dao : uint8_t { Time = 0, Stillness, Tide, Water, Sun, Fire, Metal, Myriad, Fate, Count };
constexpr uint8_t kCount = static_cast<uint8_t>(Dao::Count);
constexpr uint8_t kPerLife = 5;
constexpr uint8_t kMaxLevel = 9;
constexpr uint16_t kTrackedUnitsForSlots = 30;  // 5 h in 10-minute units: slots wait for that much data
constexpr uint8_t kSharePercent = 40;

struct Rule {
  uint16_t condition;  // in the measure's units (days, hours, books)
  uint16_t step;
};
const Rule& ruleOf(Dao dao);

bool found(const GameState& state, Dao dao);
uint8_t foundCount(const GameState& state);
// Slots open now: by realm (2 / 3 / 4 / 5 from Qi Condensation to Nascent
// Soul), none until 5 hours have been tracked.
uint8_t slots(const GameState& state);
bool settled(const GameState& state);  // all five found

// Hours read per time of day (Morning, Afternoon, Evening, Night) since
// tracking began, in 10-minute units, and their total.
uint16_t bucketUnits(const GameState& state, uint8_t bucket);
uint32_t trackedUnits(const GameState& state);
uint8_t daysOver60(const GameState& state);
uint8_t daysOver3h(const GameState& state);
void addLongDay(GameState& state, uint32_t secondsThatDay);  // counts a finished day of 60+ min / 3+ h

// The measure in its own units (days, hours, books), whole.
uint32_t measure(const GameState& state, Dao dao);
// Progress toward the condition in permille (1000 = met, may go beyond),
// lowered for the time-of-day Daos when the share is under 40% and for Myriad
// Paths when one time of day holds more than 40%.
uint32_t progressPermille(const GameState& state, Dao dao);
bool conditionMet(const GameState& state, Dao dao);
// 0 when not found; 1..9 once found.
uint8_t level(const GameState& state, Dao dao);
// What the measure needs to reach for the next level (0 at level 9 or not found).
uint32_t nextLevelAt(const GameState& state, Dao dao);

// Adds a visit's time-of-day reading. `snapshotAfter` is the snapshot after
// bridge::collect(): whole 10-minute steps of CrossInk's own totals are
// counted, so no carry is stored.
void addReading(GameState& state, const VisitGains& gains, const StatsSnapshot& snapshotAfter);

// What changed for the Return summary and the Chronicle.
struct News {
  Dao foundDaos[kPerLife] = {};
  uint8_t foundCount = 0;
  Dao levelDaos[kCount] = {};
  uint8_t levels[kCount] = {};  // the new level of levelDaos[i]
  uint8_t levelCount = 0;
  bool settledNow = false;
};
// Fills open slots and records level changes. Call after each visit and
// after a breakthrough (a new realm opens a slot).
News settle(GameState& state);

// --- Keys and effects ----------------------------------------------------------------
// The found Dao that opens the bottleneck into `targetRealm` (the highest
// level, ties by table order), or Dao::Count when none does.
Dao keyFor(const GameState& state, uint8_t targetRealm);
uint8_t keyLevelFor(uint8_t targetRealm);  // target + 2, at most 9

// The element whose equipped techniques a Dao strengthens (Element::Count for none).
Element elementOf(Dao dao);
// Tiers added to equipped techniques of `element`: +1 at levels 3, 6 and 9 of
// its Dao (fights, Body cuts; never Cultivation manuals).
uint8_t tierBoost(const GameState& state, Element element);

uint8_t ruinBonus(const GameState& state);               // Time: +3 per level to win ruin searches
uint32_t consolidationPermille(const GameState& state);  // Stillness: +10% per level
uint8_t pityPerFailureBonus(const GameState& state);     // Tide: +2 per level
uint8_t heartDemonBonus(const GameState& state);         // Flowing Water: +2 per level
uint32_t healPermille(const GameState& state);           // Rising Sun: +10% per level
uint8_t brewQualityBonus(const GameState& state);        // Fire: +2 per level
uint16_t fightDamagePermille(const GameState& state);    // Metal: 2% less per level
uint32_t stonesPercentBonus(const GameState& state);     // Myriad Paths: +5% per level
uint8_t prizeChanceBonus(const GameState& state);        // Fate: +2 per level (prizes and recipe slips)

}  // namespace daos
}  // namespace cultivation
