#pragma once

#include <cstdint>

#include "GameState.h"

// v0.3.2 "The Living World" (agreed with the user in sessions 14 and 15):
// five sects per world follow their own stories while you read, and now and
// then a story turns one of your opened regions for a few reading hours.
// v0.3.3 (part 2, agreed in session 15): the people you meet and fight belong
// to the five sects, your wins can decide their stories, each sect remembers
// you (standing), and four more story types.
// Pure rules: no I/O, no UI, no text (the activity writes the lines).
//
// Time: one world step per in-game year, 60 reading minutes of this life
// (gameReadingSeconds / 3600). Not reading pauses the world.
//
// Each step, a sect between stories starts one with a 1-in-8 chance; a sect
// in a story moves one stage on with a 1-in-3 chance. A story has 3-5
// stages. Nine of the twelve story types turn a region at their second stage
// (stage 1): only an opened region (60% of such stories are set in one, the
// rest at the sect's home), only when no other condition is on anywhere, and
// only kConditionPercent of the time (tuned with V032Pacing / V033Pacing). A
// condition lasts kConditionYears (4 reading hours) unless you end it sooner.
//
// Save: GameState::living (save v8), 64 bytes; per life (a new world is
// zeroed with the rest of the life).
//   0-1    years already stepped (u16; reading hours of this life)
//   2      flags: 0x01 the sects were founded, 0x02 the v0.3.3 layout of +9
//   3      the condition (Condition, 0 = none)
//   4      its region
//   5-6    the year it ends (u16)
//   7      its tally: bandits beaten at a camp; beasts in a tide; sects arrived
//          at an inheritance; in a war, low nibble wins against the story's
//          sect, high nibble against the other (followers of a demonic cult: low)
//   8      the sect whose story made it
//   9      a war's other sect + 1
//   10-59  five sects, 10 bytes each:
//          +0 temper, +1 strength (1-10), +2 home region, +3 story (0 none),
//          +4 stage (0-based), +5 stages, +6 the other sect + 1 (a feud, an
//          alliance), +7 the story's region, +8 the story's seed byte,
//          +9 (v0.3.3) bits 0-1 its people you beat outside a war (0-3),
//             bit 2 dented in this story, bits 4-7 your standing + 5 (0-10)
//          (v0.3.2 kept "stories finished" in +9; reset once by advance())
//   60-61  alliances: one bit per pair of sects (v0.3.3)
//   62-63  spare
namespace cultivation {
namespace sects {

constexpr uint8_t kSects = 5;
constexpr uint8_t kStartOneIn = 8;         // user, session 15
constexpr uint8_t kAdvanceOneIn = 3;       // user, session 15
constexpr uint8_t kConditionPercent = 76;  // tuned (pacing test): every ~13 / 12 / 11 h at realm 1 / 3 / 6 (user: 12)
constexpr uint8_t kNearPercent = 60;       // a story in one of your regions, else at the sect's home
constexpr uint16_t kConditionYears = 4;    // user, session 15: 4 reading hours
constexpr uint8_t kCampWinsToBreak = 3;    // user, session 15
constexpr uint8_t kWarWinsToDecide = 3;    // v0.3.3 (user): three of one side's people decide a war
constexpr uint8_t kTideBeastsToEnd = 5;    // v0.3.3 (user)
constexpr uint8_t kPeopleToDent = 3;       // v0.3.3 (user): three of a sect's people, -1 strength once a story
constexpr uint8_t kArrivalsToClaim = 3;    // v0.3.3: three sects reach an inheritance and one takes it
constexpr uint8_t kArriveOneIn = 2;        // ... one arrives in a year with 2 in 3 (see Sects.cpp)
constexpr int8_t kStandingMin = -5;        // v0.3.3 (user: stored now, used by reputation later)
constexpr int8_t kStandingMax = 5;
constexpr uint16_t kMaxStepsPerVisit = 400;  // a very long binge still steps quickly

enum class Temper : uint8_t { Ambitious = 0, Cautious, Reclusive, Righteous, Ruthless, Count };

enum class Story : uint8_t {
  None = 0,
  Feud,         // two sects fight over a spirit vein -> sect war
  Succession,   // a sect master dies -> no condition
  Bandits,      // bandits gather -> bandit camp
  Tournament,   // a martial tournament -> tournament
  Fair,         // a temple fair -> temple fair
  BeastTide,    // a beast valley slips from a sect's hold -> beast tide
  Ruins,        // diggers cause a landslide -> ruins exposed
  Blight,       // a blight spreads, a sect hoards herbs -> blight
  Inheritance,  // v0.3.3: an old master's inheritance, the sects race for it -> inheritance
  Demonic,      // v0.3.3: a demonic cultivator haunts a region -> demonic cult
  Alliance,     // v0.3.3: two sects ally by marriage -> no condition
  GateMove,     // v0.3.3: a sect moves its gate -> no condition
  Count
};

enum class Condition : uint8_t {
  None = 0,
  SectWar,
  BanditCamp,
  Tournament,
  TempleFair,
  BeastTide,
  Ruins,
  Blight,
  Inheritance,  // v0.3.3: the ruins effect, and a race
  Demonic,      // v0.3.3: the sect war effects, against a cult's followers
  Count
};

Condition conditionOf(Story story);  // None for Succession, Alliance and GateMove

struct Sect {
  Temper temper = Temper::Ambitious;
  uint8_t strength = 5;
  uint8_t home = 0;
  Story story = Story::None;
  uint8_t stage = 0;
  uint8_t stages = 0;
  int8_t other = -1;  // a feud's or an alliance's other sect
  uint8_t region = 0;
  uint8_t seed = 0;
  int8_t standing = 0;  // v0.3.3: what the sect thinks of you, -5..+5
};

bool founded(const GameState& state);
Sect sect(const GameState& state, uint8_t index);
int8_t standing(const GameState& state, uint8_t index);
bool allied(const GameState& state, uint8_t a, uint8_t b);

// What happened, for the lines and the Chronicle.
enum class EventKind : uint8_t {
  Started,
  Ended,
  ConditionBegan,
  ConditionEnded,
  // v0.3.3: what you did (from the influence calls, never from a step)
  WarDecided,          // sect = the winner, other = the loser
  CultBroken,          // a demonic cult driven off; sect = the sect whose story it was
  CampBroken,          // sect = the sect whose story it was
  TideBroken,          // sect = the sect whose story it was
  YouTookInheritance,  // sect = the sect whose story it was
  Dented,              // you beat three of the sect's people
};
struct Event {
  EventKind kind = EventKind::Started;
  uint8_t sect = 0;
  int8_t other = -1;
  Story story = Story::None;
  Condition condition = Condition::None;
  uint8_t region = 0;
  bool good = true;  // an ended story: the good or the bad ending
  uint16_t year = 0;
  uint8_t seed = 0;  // the story's seed byte (text variety)
};
constexpr uint8_t kMaxEvents = 16;
struct Steps {
  uint16_t years = 0;  // steps taken
  uint8_t count = 0;   // events kept (the first kMaxEvents)
  uint16_t dropped = 0;
  Event events[kMaxEvents];
};

// Founds the sects on the first call of a life (no history before it), then
// takes one step per reading hour since the last call. Call once per visit,
// after the reading is absorbed.
Steps advance(GameState& state);

// The condition now (None when none). `region` is set when one is on.
Condition condition(const GameState& state, uint8_t* region = nullptr);
Condition conditionIn(const GameState& state, uint8_t region);
uint16_t conditionYearsLeft(const GameState& state);  // 0 when none
uint8_t conditionSect(const GameState& state);
int8_t conditionOther(const GameState& state);   // a sect war's other sect, -1 for none
uint8_t conditionTally(const GameState& state);  // byte 7 (see the layout)

// --- Effects (never Qi, stamina or anything owned) --------------------------
// A sect war and a demonic cult share the war's effects; an inheritance the
// ruins' effect.
int enemyLevelBonus(const GameState& state, uint8_t region);                     // +2
uint16_t fightStonesPermille(const GameState& state, uint8_t region);            // 1500
uint8_t encounterPercent(const GameState& state, uint8_t region, uint8_t base);  // beast tide x1.5
uint8_t meetPercent(const GameState& state, uint8_t region, uint8_t base);       // tournament, fair x2
// The enemy kind a condition calls for (one reroll toward it): 0xFF none,
// else explore::Enemy (Cultivator in a war or a cult, Bandit at a camp).
uint8_t favouredEnemy(const GameState& state, uint8_t region);
uint8_t beastLootPercent(const GameState& state, uint8_t region, uint8_t base);  // beast tide x2 (max 100)
uint8_t ruinBonus(const GameState& state, uint8_t region);                       // ruins, inheritance +15
bool herbsHalved(const GameState& state, uint8_t region);                        // blight
uint16_t buyPermille(const GameState& state, uint8_t region, bool herb);
uint16_t sellPermille(const GameState& state, uint8_t region, bool herb);
constexpr uint16_t kBuyFloorPermille = 700;  // user: discounts stop at 30% in total
bool tournamentOn(const GameState& state);   // the ranking's prizes are doubled

// --- People and influence (v0.3.3) -----------------------------------------
constexpr int8_t kCult = -2;  // a follower of a demonic cult, not of a sect
// The sect of a cultivator named by `seed` (a road meeting's name seed, a
// random fight's beast seed): in a region at war, one of the two sides by the
// seed; in a demonic cult's region, a follower (kCult); else one of the five.
// -1 before the sects are founded. `region` kNoRegion: no region (the ranking).
constexpr uint8_t kNoRegion = 0xFF;
int8_t personSect(const GameState& state, uint8_t region, uint32_t seed);

// What a win changed, for the result screens and the Chronicle (the events
// are the same kinds advance() returns, with the year of now).
struct Influence {
  uint8_t count = 0;
  Event events[2];
  uint32_t stones = 0;  // a camp broken: 50 x (region + 1)
  uint8_t herbs = 0;    // a tide ended: 2 herbs and 2 ores of the region
  uint8_t ores = 0;
  bool treasure = false;  // an inheritance taken: a treasure (and stones)
  bool tonic = false;     // a camp broken: a Greater tonic
};
// You beat a cultivator of `sect` (personSect) in `region`.
Influence cultivatorBeaten(GameState& state, uint8_t region, int8_t sect);
// You beat a bandit / a beast in `region` (camp, tide).
Influence banditBeaten(GameState& state, uint8_t region);
Influence beastBeaten(GameState& state, uint8_t region);
// You won a ruin or cave search in `region` (an inheritance).
Influence searchWon(GameState& state, uint8_t region);

// A live story for the rumor of the day (-1 when none), seeded by `when`.
int rumorSect(const GameState& state, uint32_t when);
// A sect that thinks well (> 0) or badly (< 0) of you, at least 3 either way,
// for a rumor about you (-1 when none), seeded by `when`.
int standingRumorSect(const GameState& state, uint32_t when);

}  // namespace sects
}  // namespace cultivation
