#pragma once

#include <cstdint>

#include "GameState.h"
#include "Inventory.h"
#include "Scrolls.h"

// Major-realm breakthroughs, Great Perfection and grades (DOC: Realms,
// stages and breakthroughs). Pure functions over GameState, mirroring
// tools/cultivation/balance_sim.py (breakthrough_chance, _set_grade).
//
// Keys (milestone 7): from Foundation Establishment on, a bottleneck needs a
// Technique Scroll of at least the tier Scrolls.h keyTierFor() names; it is
// used up only when the attempt succeeds. v0.2.0: a breakthrough pill of a
// fitting grade (Alchemy.h) is a key too and adds chance; it is used up on
// every attempt. When both are held the scroll is the key and the pill adds
// chance. Daos, the doc's third key, come later. Body Tempering -> Qi
// Condensation is the doc's "small bottleneck" and needs no key.
namespace cultivation {
namespace breakthrough {

constexpr uint8_t kPityPerFailure = 15;  // DOC
constexpr uint8_t kPityCap = 90;         // DOC
constexpr uint8_t kChanceCap = 99;       // balance_sim.py: never a certainty before pity
constexpr uint8_t kDaoHeartChanceMax = 15;
constexpr uint8_t kConsolidationChanceMax = 15;
constexpr uint8_t kConsolidationMax = 40;
constexpr uint32_t kSecondsPerConsolidation = 1800;  // DOC: 2 points per reading hour in Great Perfection
constexpr uint8_t kDaoHeartGradeMax = 25;
constexpr uint8_t kElementChance = 5;       // DOC: main technique's element matches the root
constexpr uint8_t kElementGradeScore = 15;  // DOC: grade score's technique element part

// The chance of the next attempt and where it comes from, for the screen.
struct Chance {
  uint8_t target = 0;         // realm the attempt would enter
  uint8_t base = 0;           // DOC: 90 / 70 / 45 / 35, then -5 per realm, floor 20
  uint8_t daoHeart = 0;       // up to +15
  uint8_t consolidation = 0;  // up to +15 (Great Perfection)
  uint8_t element = 0;        // +5 when the equipped manual's element matches the root
  uint8_t pill = 0;           // v0.2.0: the breakthrough pill's +10 / +17 / +25
  uint8_t pity = 0;           // what pity added on top of the rest
  uint8_t total = 0;
  bool needsKey = false;                             // every bottleneck after the small one into Qi Condensation
  bool hasKey = false;                               // a qualifying unequipped scroll is in the library
  int keyIndex = -1;                                 // that scroll (Scrolls.h keyFor), -1 if none or none needed
  inventory::Item pillItem = inventory::Item::None;  // the pill the attempt takes (or took)
  bool pillIsKey = false;                            // no key scroll: the pill is the key
};

Chance chanceFor(const GameState& state);

bool inGreatPerfection(const GameState& state);
bool isHalfStep(const GameState& state);

// A full Peak bar (or Great Perfection), the next realm exists, and the key
// is in hand when one is needed.
bool canAttempt(const GameState& state);
// From Qi Condensation on, a full Peak bar can enter Great Perfection instead.
bool canEnterGreatPerfection(const GameState& state);
void enterGreatPerfection(GameState& state);

// Consolidation points earned by a visit's reading while in Great
// Perfection: whole half-hours of this character's reading time, so no carry
// field is needed (the same trick as stamina). Applied by absorb().
uint8_t consolidationGained(const GameState& state, uint32_t readingSecondsBefore, uint32_t readingSecondsAfter);

enum class GradeKind : uint8_t { None, Foundation, Core, Soul };

struct Result {
  bool attempted = false;
  bool success = false;
  uint8_t chance = 0;
  uint8_t roll = 0;  // 0..99; success when roll < chance
  uint8_t fromRealm = 0;
  uint8_t toRealm = 0;
  GradeKind gradeKind = GradeKind::None;
  uint8_t grade = 0;       // the value stored in the save for that grade
  uint8_t gradeScore = 0;  // 0..100
  bool keyUsed = false;    // success with a key: `key` left the library
  scrolls::Scroll key;
  inventory::Item pillUsed = inventory::Item::None;  // v0.2.0: used up whatever the outcome
};

// One attempt. The roll is seeded by the world, the realm and the number of
// failures so far, so leaving and reopening the game never changes an outcome.
// Success: next realm at its first stage, Qi zero, grade set, consolidation
// spent, the key scroll (if one was needed) removed from the library. Failure: Half-step title, 25% of the bar lost,
// never a stage; pity grows and Great Perfection (if entered) is kept.
Result attempt(GameState& state);
// A failed attempt's cost on its own (also for a lost Tribulation or Heart
// Demon, milestone v0.1.9): 25% of the bar, failures + 1, Half-step; the
// breakthrough pill taken for it is gone.
void failAttempt(GameState& state);

// Grade score entering `targetRealm` (DOC: consolidation 0-40 + Dao Heart
// 0-25 + pills 0-20 + technique element 0-15; the Fallen clan heir adds 5 to
// the Foundation grade). Pills are the grade pills taken (Alchemy.h); the
// technique part is the equipped manual's element matching the root.
uint8_t gradeScore(const GameState& state, uint8_t targetRealm);

// Bands (DOC): Foundation 1 Cracked, 2 Flawed, 3 Standard, 4 Perfect, 5
// Heavenly Dao. Core 1 = 9th grade ... 9 = 1st grade, 10 = Flawless. Soul
// clarity 1 Hazy, 2 Clear, 3 Radiant, 4 Dao-Imprinted. 0 = not formed yet.
uint8_t foundationGradeFor(uint8_t score);
uint8_t coreGradeFor(uint8_t score);
uint8_t soulClarityFor(uint8_t score);

// Qi rate from the grades formed so far, in permille (1000 = no change).
uint32_t gradeQiPermille(const GameState& state);

}  // namespace breakthrough
}  // namespace cultivation
