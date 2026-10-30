#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "Grammar.h"

// The life log (DOC: Time passage): "Each visit opens with a summary ... It
// lists one life event per in-game season passed, up to 6."
//
// A season is 3 in-game months (15 reading minutes). Seasons are counted from
// the character's age in months, so short visits add up without any extra
// save field: season k covers ageMonths 3k .. 3k+2, and a visit that moves
// the age from a to b has completed the seasons a/3 .. b/3 - 1.
//
// Every line is seeded by the world and its absolute season number, so the
// same season always produces the same line: reopening the game, or a crash
// before the save, never rerolls one.
//
// Life-log lines grant nothing. They are flavor until the systems they would
// describe (herbs, rivals, masters) exist.
namespace cultivation {
namespace lifelog {

constexpr uint8_t kMaxSeasonLines = 6;              // DOC: "up to 6"
constexpr uint8_t kMaxLines = kMaxSeasonLines + 2;  // + one stage line + one bottleneck line
constexpr size_t kLineSize = 200;                   // "Winter, age 123. " plus a pack line (under 180 bytes)

inline uint32_t seasonOf(const uint32_t ageMonths) { return ageMonths / 3; }
inline uint32_t ageOfSeason(const uint32_t season) { return season / 4; }                        // years
inline uint8_t seasonInYear(const uint32_t season) { return static_cast<uint8_t>(season % 4); }  // 0 = spring

// What one visit did to the character's age and stage.
struct VisitChange {
  uint32_t ageMonthsBefore = 0;
  uint32_t ageMonthsAfter = 0;
  uint8_t stagesGained = 0;
  bool reachedBottleneck = false;  // at a full Peak bar now, and was not before the visit
};

// Seasons completed by the visit (may be more than are shown).
inline uint32_t seasonsPassed(const VisitChange& v) {
  return v.ageMonthsAfter > v.ageMonthsBefore ? seasonOf(v.ageMonthsAfter) - seasonOf(v.ageMonthsBefore) : 0;
}

// The i-th of n shown seasons out of `count` completed ones, spread evenly so
// the first and last are always included (n = min(count, 6)).
uint32_t shownSeason(uint32_t firstSeason, uint32_t count, uint8_t i, uint8_t n);

enum class LineKind : uint8_t { Season, Stage, Bottleneck, Event };

struct Line {
  LineKind kind = LineKind::Season;
  uint32_t season = 0;  // labels the line: "Spring, age 17."
  char text[kLineSize] = {};
};

// One visit's lines, in order: seasons, then a stage line, then a bottleneck
// line. About 1.6 KB, so the activity allocates it for the summary only.
struct LifeLog {
  uint32_t monthsPassed = 0;
  uint32_t seasonsPassed = 0;
  uint8_t count = 0;
  Line lines[kMaxLines];
};

// Fills `log` for one visit. `state` is the character after the visit.
// Without a grammar engine (out of memory) the log has the counts but no lines.
void build(LifeLog& log, const VisitChange& change, uint32_t monthsPassed, const GameState& state, Grammar* grammar);

// Where a season's line comes from (exposed for tests). Three rolls seeded by
// the season alone pick the list: "log.any" 30% of the time, Great Perfection
// or Half-step lines 35% of the rest while in that state, otherwise the realm's own list
// (Body Tempering: the origin's list 3 in 7, else "log.body"; Qi
// Condensation "log.qi"; Foundation "log.foundation"; later realms "log.any").
// The entry is the list's next one in a fixed shuffled order per world, so a
// list's entries are all used before any repeats, whatever the state does.
struct SeasonPick {
  const char* key = nullptr;
  uint32_t entry = 0;
};
SeasonPick seasonPick(const GameState& state, uint32_t season, const Grammar& grammar, const GrammarContext& ctx);

// One line outside the seasons (a breakthrough, entering Great Perfection),
// labelled with the character's current season. False if the list is empty
// or the line could not be expanded.
bool eventLine(Line& line, LineKind kind, const char* key, uint32_t seed, const GameState& state, Grammar& grammar);

// "Spring, age 17. " into out.
void formatLabel(uint32_t season, char* out, size_t outLen);

}  // namespace lifelog
}  // namespace cultivation
