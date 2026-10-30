#pragma once

#include <cstdint>

#include "GameState.h"

// Major-realm breakthroughs, Great Perfection and grades (DOC: Realms,
// stages and breakthroughs). Pure functions over GameState, mirroring
// tools/cultivation/balance_sim.py (breakthrough_chance, _set_grade).
//
// Not yet: the keys a bottleneck needs from Foundation Establishment on
// (Technique Scrolls, Daos, breakthrough pills), Tribulations and Heart
// Demons. Agreed with the user in session 3: Body Tempering -> Qi
// Condensation is the doc's "small bottleneck" and needs no key; later
// breakthroughs report a missing key until scrolls exist.
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

// The chance of the next attempt and where it comes from, for the screen.
struct Chance {
  uint8_t target = 0;         // realm the attempt would enter
  uint8_t base = 0;           // DOC: 90 / 70 / 45 / 35, then -5 per realm, floor 20
  uint8_t daoHeart = 0;       // up to +15
  uint8_t consolidation = 0;  // up to +15 (Great Perfection)
  uint8_t pity = 0;           // what pity added on top of the rest
  uint8_t total = 0;
  bool needsKey = false;  // every bottleneck after the small one into Qi Condensation
  bool hasKey = false;    // always false until Technique Scrolls (next milestone)
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
};

// One attempt. The roll is seeded by the world, the realm and the number of
// failures so far, so leaving and reopening the game never changes an outcome.
// Success: next realm at its first stage, Qi zero, grade set, consolidation
// spent. Failure: Half-step title, 25% of the bar lost, never a stage; pity
// grows and Great Perfection (if entered) is kept.
Result attempt(GameState& state);

// Grade score entering `targetRealm` (DOC: consolidation 0-40 + Dao Heart
// 0-25 + pills 0-20 + technique element 0-15; the Fallen clan heir adds 5 to
// the Foundation grade). Pills and techniques add nothing yet.
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
