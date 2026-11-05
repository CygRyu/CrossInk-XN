#pragma once

#include <cstdint>

#include "GameState.h"
#include "Inventory.h"
#include "StatsBridge.h"

// Daily systems (v0.2.6, milestone 9a; agreed with the user in session 9,
// HANDOFF 14r): the omen of the day (Heavenly Almanac), three daily tasks and
// the game's own reading streak. Every one is a bonus, never a requirement:
// nothing is lost on a missed day, and nothing here gives Qi or stamina.
//
// Days are calendar days (StatsSample::today). Without a date the Daily
// screen says so and nothing here changes; Daos keep growing.
//
// Today's reading: CrossInk keeps seconds per weekday, not per date, so a
// visit's reading on each weekday is spread over that weekday's dates since
// the last visit (the weekly allowance does the same). Pages and sessions are
// only known per visit: a day gets the visit's share by its minutes.
//
// Save (the `daily` block, reserved since milestone 1; save version stays 5):
//    0-1 day index + 1 of "today" (u16; 0 = never)
//    2   flags: 0x01 the date was known at the last visit (omen and tasks live),
//        0x02 the free reroll is used, 0x04/0x08/0x10 task 0/1/2 collected,
//        0x20 averages set up
//    3-5 task kinds (Task), 6-8 targets
//    9   the game task's count today
//   10-11 seconds read today in 10-second units (u16)
//   12-13 pages today (u16), 14 sessions today (u8)
//   15-16 average minutes a day x 10, 17-18 average pages a day x 10 (u16)
//   19-20 longest streak since creation, 21-22 current streak (u16)
//   23-24 spare
namespace cultivation {
namespace daily {

// --- Omen (Heavenly Almanac) -------------------------------------------------------------
// One a day from the date and the world seed. Never Qi, never a penalty.
enum class Omen : uint8_t {
  None = 0,
  HerbsThick,     // +1 herb per herb tile
  VeinsShallow,   // +1 ore per ore tile
  SteadyFurnace,  // +5 brew quality
  GenerousRoad,   // buying 10% cheaper
  SharpBlade,     // +2 fight level
  LightFeet,      // +15 to slip away
  ClearHeart,     // +5 on Heart Demon scenes
  CalmSky,        // lightning 10% softer
  OldRuins,       // +15 to ruin searches
  QuietMind,      // +20% consolidation in Great Perfection
  Count
};
constexpr uint8_t kOmenCount = static_cast<uint8_t>(Omen::Count) - 1;

// --- Tasks ----------------------------------------------------------------------------------
enum class Task : uint8_t {
  None = 0,
  ReadMinutes,  // read N minutes today -> 2 herbs of the newest region
  TurnPages,    // turn P pages today -> 2 ores of the newest region
  TwoSessions,  // 2 book openings and 10+ minutes today -> 1 minor tonic
  Chart,        // chart 2 new tiles -> 40 x (realm + 1) stones
  Gather,       // gather 2 herbs or ores -> 40 x (realm + 1) stones
  WinFight,     // win a fight of any kind -> 1 minor tonic
  Brew,         // start or take out a brew -> 1 herb of the newest region
  Merchant,     // buy or sell at a merchant -> 40 x (realm + 1) stones
  Meridian,     // open a meridian point -> 60 x (realm + 1) stones
  Count
};
constexpr uint8_t kTaskSlots = 3;          // two reading tasks, then one game task
constexpr uint8_t kRegionlessStones = 30;  // a herb or ore reward before any region opens
constexpr uint16_t kStartAverageMinutes = 20;
constexpr uint16_t kStartAveragePages = 15;
constexpr uint8_t kAverageDays = 14;

bool live(const GameState& state);       // the date was known at the last visit
uint32_t today(const GameState& state);  // StatsSnapshot::kNoDay when never known
Omen omen(const GameState& state);       // Omen::None when not live

// One visit: finishes the days since the last visit (averages, long days for
// the Daos), starts a new day (new omen and tasks) when the date changed,
// adds today's share of the reading and recomputes the streak.
// `lastVisitDay` is the snapshot's lastVisitDay before bridge::collect().
void update(GameState& state, const VisitGains& gains, const StatsSample& now, uint32_t lastVisitDay);

Task task(const GameState& state, uint8_t slot);
uint16_t target(const GameState& state, uint8_t slot);
uint16_t progress(const GameState& state, uint8_t slot);  // in the task's units, at most the target
bool done(const GameState& state, uint8_t slot);
bool collected(const GameState& state, uint8_t slot);
uint8_t waiting(const GameState& state);  // done and not collected

struct Reward {
  uint32_t stones = 0;
  inventory::Item item = inventory::Item::None;
  uint16_t count = 0;
  // v0.3.0: collect() only, when the third task of the day is collected: a memento (0xFF = none, Codex.h).
  uint8_t memento = 0xFF;
  bool mementoDuplicate = false;
  uint16_t mementoStones = 0;
};
Reward rewardFor(const GameState& state, uint8_t slot);
// Grants a done task's reward once. Returns false (and changes nothing) otherwise.
bool collect(GameState& state, uint8_t slot, Reward* out = nullptr);

// One free reroll a day: the task becomes another of its pool (one the
// player can do today, for the game task). Not for a collected task.
bool canReroll(const GameState& state, uint8_t slot);
bool reroll(GameState& state, uint8_t slot);

// The game's actions report here (exploration, alchemy, merchants, meridians);
// counted only when today's game task is of that kind.
void note(GameState& state, Task kind, uint16_t n = 1);

uint16_t minutesToday(const GameState& state);
uint16_t pagesToday(const GameState& state);
uint8_t sessionsToday(const GameState& state);
uint16_t averageMinutesX10(const GameState& state);
uint16_t averagePagesX10(const GameState& state);

// --- Streak --------------------------------------------------------------------------------
// The game's own, counted from character creation (or as far back as
// CrossInk's 730-day history goes): days with reading in a row, where one
// missed day per calendar week (Monday to Sunday) is covered (the doc's
// Stillness Charm). Today without reading yet does not break it.
uint16_t currentStreak(const GameState& state);
uint16_t longestStreak(const GameState& state);  // since creation; feeds the Dao of Time
struct Streaks {
  uint16_t current = 0;
  uint16_t longest = 0;
};
Streaks computeStreaks(const StatsSample& now, uint32_t fromDay);

// --- Omen effects (all zero / x1 on other days) --------------------------------------------
uint8_t extraHerbs(const GameState& state);
uint8_t extraOre(const GameState& state);
uint8_t brewQualityBonus(const GameState& state);
uint16_t buyPermille(const GameState& state);  // 900 on a Generous road day
int fightLevelBonus(const GameState& state);
uint8_t slipBonus(const GameState& state);
uint8_t heartDemonBonus(const GameState& state);
uint16_t lightningPermille(const GameState& state);  // 900 on a Calm sky day
uint8_t ruinBonus(const GameState& state);
uint32_t consolidationPermille(const GameState& state);  // 1200 on a Quiet mind day

}  // namespace daily
}  // namespace cultivation
