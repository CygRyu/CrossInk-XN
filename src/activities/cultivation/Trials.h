#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "Scrolls.h"

// Tribulations and Heart Demons (DOC: Realms, stages and breakthroughs;
// v0.1.9, mechanics agreed with the user). A trial comes before the
// breakthrough roll of some realms; losing it is a failed attempt (25% of the
// bar, pity, Half-step), the key scroll is kept.
//
//   into Core Formation         Tribulation, 3 strikes
//   into Nascent Soul           Heart Demon, 3 scenes
//   into Soul Transformation    Tribulation, 5 strikes
//   into Void Refinement        Heart Demon
//   into Body Integration       Tribulation, 7 strikes
//   into Mahayana               Heart Demon
//   into Tribulation Transcendence   Great Tribulation, 9 strikes
//
// Tribulation: each strike's damage is a share of max HP, shown before the
// answer: Endure (an equipped Body technique cuts it by 10-50% by tier), Turn
// it with an equipped Sword, Palm/Fist or Movement technique (half damage,
// each technique once per tribulation), or Burn an unequipped scroll (no
// damage, the scroll is gone; never the scroll that will be the key). 0 HP
// loses. HP heals 1 per minute of reading (Simulation absorb()).
//
// Heart Demon: 3 scenes; each is a chance to see through it (40 + Dao Heart
// x 30 / 100 + Golden Core grade 0-20 + 10 with an equipped Mind technique,
// at most 95), or burn an unequipped Mind scroll to pass it for certain.
// Two scenes lost: the attempt fails; two won: on to the roll.
//
// A trial in progress lives in the save, so leaving the game (or a power cut)
// resumes it at the same strike or scene: ProgressBlock::kInTrial and the
// three spare bytes of the v2 techniqueMeta block (step, techniques used,
// scenes lost / won). No format change. Equipping is blocked during a trial,
// so "technique used" bits keep pointing at the same techniques.
namespace cultivation {
namespace trials {

enum class Kind : uint8_t { None, Tribulation, HeartDemon };

constexpr uint8_t kHeartDemonScenes = 3;
constexpr uint8_t kHeartDemonLosses = 2;  // this many lost scenes lose the trial
constexpr uint8_t kMaxTurnTechniques = scrolls::kMaxEquippedTechniques;

Kind kindFor(uint8_t targetRealm);
uint8_t strikesFor(uint8_t targetRealm);  // 0 when no Tribulation
// Percent of max HP that strike `index` (0-based) of a Tribulation deals.
uint8_t strikePercent(uint8_t strikes, uint8_t index);

bool inTrial(const GameState& state);
uint8_t step(const GameState& state);  // strike or scene, 0-based
// Begins the trial for the next realm (the breakthrough screen's Attempt).
// Returns false if there is none, a bar is not full, or HP is 0 for a Tribulation.
bool begin(GameState& state);
// Clears the trial state (after the roll, or after losing).
void end(GameState& state);

// --- Tribulation ----------------------------------------------------------------------

enum class Answer : uint8_t { Endure, Turn, Burn };

struct StrikeOption {
  Answer answer = Answer::Endure;
  int16_t slot = -1;    // the technique (Turn) or scroll (Burn); -1 for Endure
  uint16_t damage = 0;  // HP this answer costs
};

struct Strike {
  uint8_t index = 0;  // 0-based
  uint8_t count = 0;  // strikes in this Tribulation
  uint8_t percent = 0;
  uint16_t base = 0;  // damage before any answer
  StrikeOption options[2 + kMaxTurnTechniques] = {};
  uint8_t optionCount = 0;
};

// The current strike and its answers, in display order: Endure, the unused
// Turn techniques, Burn (the weakest unequipped scroll that is not the key).
Strike currentStrike(const GameState& state);

enum class Progress : uint8_t { Continue, Won, Lost };

struct StrikeResult {
  Progress progress = Progress::Continue;
  uint16_t damage = 0;
  scrolls::Scroll burned;  // Burn: the scroll used up
  bool didBurn = false;
};

// Applies option `choice` of currentStrike(). Won: the caller rolls the
// breakthrough; Lost: the caller applies breakthrough::failAttempt().
StrikeResult answerStrike(GameState& state, uint8_t choice);

// --- Heart Demon ---------------------------------------------------------------------

uint8_t sceneChance(const GameState& state);     // percent
int burnableMindScroll(const GameState& state);  // slot or -1
uint8_t scenesLost(const GameState& state);
uint8_t scenesWon(const GameState& state);

struct SceneResult {
  Progress progress = Progress::Continue;
  bool passed = false;
  uint8_t roll = 0;  // 0-99, passed when below the chance (not for a burned scroll)
  uint8_t chance = 0;
  scrolls::Scroll burned;
  bool didBurn = false;
};

// Faces the current scene (a seeded roll), or burns a Mind scroll to pass it.
SceneResult answerScene(GameState& state, bool burnMindScroll);

}  // namespace trials
}  // namespace cultivation
