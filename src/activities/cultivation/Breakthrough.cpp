#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Breakthrough.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace breakthrough {
namespace {

constexpr uint32_t kKindBreakthroughRoll = 55;  // contentSeed kind (50-54: rumor, life log, Chronicle)

// Base chance INTO realm r (DOC: 90/70/45/35, then -5 per realm, floor 20).
static const uint8_t kBaseChance[kMortalRealmCount] = {0, 90, 70, 45, 35, 30, 25, 20, 20, 20};

// DOC grade effects on the Qi rate, permille.
static const uint16_t kFoundationQi[6] = {1000, 800, 950, 1000, 1250, 1500};  // index = grade (0 = none)

uint8_t clampPercent(const uint32_t v) { return static_cast<uint8_t>(v > 100 ? 100 : v); }

}  // namespace

bool inGreatPerfection(const GameState& state) {
  return (state.progress.progressFlags & ProgressBlock::kInGreatPerfection) != 0;
}

bool isHalfStep(const GameState& state) { return (state.progress.progressFlags & ProgressBlock::kHalfStep) != 0; }

Chance chanceFor(const GameState& state) {
  Chance c;
  const ProgressBlock& p = state.progress;
  const uint8_t from = p.realm < kMortalRealmCount ? p.realm : kMortalRealmCount - 1;
  c.target = static_cast<uint8_t>(from + 1);
  if (c.target >= kMortalRealmCount) return c;  // ascension is a later milestone

  c.base = kBaseChance[c.target];
  c.daoHeart = static_cast<uint8_t>(static_cast<uint32_t>(p.daoHeart) * kDaoHeartChanceMax / 100u);
  const uint8_t consolidation = p.consolidation < kConsolidationMax ? p.consolidation : kConsolidationMax;
  c.consolidation =
      static_cast<uint8_t>(static_cast<uint32_t>(consolidation) * kConsolidationChanceMax / kConsolidationMax);
  // v0.2.1: depth counts half (20 points = +4, rounded), within consolidation's +15.
  const uint32_t depthChance =
      (static_cast<uint32_t>(depth(state)) * kConsolidationChanceMax + kConsolidationMax) / (2u * kConsolidationMax);
  const uint32_t room = kConsolidationChanceMax > c.consolidation ? kConsolidationChanceMax - c.consolidation : 0u;
  c.depth = static_cast<uint8_t>(depthChance < room ? depthChance : room);
  c.element = scrolls::manualMatchesRoot(state) ? kElementChance : 0;
  // v0.2.0: the pill taken for an attempt under way, or the one it would take.
  c.pillItem = alchemy::pillInUse(state);
  if (c.pillItem == inventory::Item::None) c.pillItem = alchemy::chosenPill(state, c.target);
  if (!alchemy::pillFits(c.pillItem, c.target)) c.pillItem = inventory::Item::None;
  c.pill = alchemy::breakthroughBonus(c.pillItem);
  uint32_t total = c.base + c.daoHeart + c.consolidation + c.depth + c.element + c.pill;
  if (total > kChanceCap) total = kChanceCap;
  // balance_sim.py: pity raises the chance by 15 per failure, but never past 90.
  if (p.failures > 0) {
    uint32_t withPity = total + static_cast<uint32_t>(kPityPerFailure) * p.failures;
    if (withPity > kPityCap) withPity = kPityCap;
    if (withPity > total) {
      c.pity = static_cast<uint8_t>(withPity - total);
      total = withPity;
    }
  }
  c.total = static_cast<uint8_t>(total);
  c.needsKey = c.target >= 2;  // the small bottleneck into Qi Condensation needs none
  if (c.needsKey) {
    c.keyIndex = scrolls::keyFor(state, c.target);  // Daos join in a later milestone
    c.pillIsKey = c.keyIndex < 0 && c.pillItem != inventory::Item::None;
    c.hasKey = c.keyIndex >= 0 || c.pillIsKey;
  }
  return c;
}

bool canAttempt(const GameState& state) {
  if (!atBottleneck(state)) return false;
  const Chance c = chanceFor(state);
  return c.target < kMortalRealmCount && (!c.needsKey || c.hasKey);
}

bool canEnterGreatPerfection(const GameState& state) {
  return atBottleneck(state) && !inGreatPerfection(state) && realmInfo(state.progress.realm).hasGreatPerfection;
}

void enterGreatPerfection(GameState& state) {
  if (canEnterGreatPerfection(state)) state.progress.progressFlags |= ProgressBlock::kInGreatPerfection;
}

uint8_t consolidationGained(const GameState& state, const uint32_t before, const uint32_t after) {
  if (!inGreatPerfection(state) || after <= before) return 0;
  const uint32_t steps = after / kSecondsPerConsolidation - before / kSecondsPerConsolidation;
  const uint8_t have = state.progress.consolidation;
  const uint8_t room = have < kConsolidationMax ? static_cast<uint8_t>(kConsolidationMax - have) : 0;
  return static_cast<uint8_t>(steps < room ? steps : room);
}

uint8_t depth(const GameState& state) {
  const uint8_t d = state.flagsAndCodex[5];
  return d < kDepthMax ? d : kDepthMax;
}

uint8_t depthGained(const GameState& state, const bool barWasFull, const uint32_t before, const uint32_t after) {
  if (!barWasFull || inGreatPerfection(state) || after <= before) return 0;
  const uint32_t steps = after / kSecondsPerDepth - before / kSecondsPerDepth;
  const uint8_t have = depth(state);
  const uint8_t room = static_cast<uint8_t>(kDepthMax - have);
  return static_cast<uint8_t>(steps < room ? steps : room);
}

void addDepth(GameState& state, const uint8_t points) {
  const uint32_t d = static_cast<uint32_t>(depth(state)) + points;
  state.flagsAndCodex[5] = static_cast<uint8_t>(d < kDepthMax ? d : kDepthMax);
}

uint8_t gradeScore(const GameState& state, const uint8_t targetRealm) {
  const ProgressBlock& p = state.progress;
  // Consolidation and half of depth (v0.2.1) share the 40-point part.
  uint32_t score = static_cast<uint32_t>(p.consolidation) + depth(state) / 2u;
  if (score > kConsolidationMax) score = kConsolidationMax;
  score += static_cast<uint32_t>(p.daoHeart) * kDaoHeartGradeMax / 100u;
  if (targetRealm >= 2 && targetRealm <= 4) score += alchemy::gradePillScore(state);  // 0-20
  if (scrolls::manualMatchesRoot(state)) score += kElementGradeScore;
  if (targetRealm == 2) score += originBonus(state.character.origin).foundationGradeScore;
  return clampPercent(score);
}

uint8_t foundationGradeFor(const uint8_t score) {
  if (score >= 90) return 5;  // Heavenly Dao
  if (score >= 70) return 4;  // Perfect
  if (score >= 40) return 3;  // Standard
  if (score >= 20) return 2;  // Flawed
  return 1;                   // Cracked
}

uint8_t coreGradeFor(const uint8_t score) {
  if (score >= 95) return 10;  // Flawless
  const uint8_t g = score / 10 < 8 ? static_cast<uint8_t>(score / 10) : 8;
  return static_cast<uint8_t>(g + 1);  // 1 = 9th grade ... 9 = 1st grade
}

uint8_t soulClarityFor(const uint8_t score) {
  if (score >= 90) return 4;  // Dao-Imprinted
  if (score >= 70) return 3;  // Radiant
  if (score >= 40) return 2;  // Clear
  return 1;                   // Hazy
}

uint32_t gradeQiPermille(const GameState& state) {
  const ProgressBlock& p = state.progress;
  uint32_t permille = kFoundationQi[p.foundationGrade <= 5 ? p.foundationGrade : 0];
  if (p.coreGrade >= 1 && p.coreGrade <= 10) {
    // DOC: 9th grade +0% ... 1st grade +30%, Flawless +40%.
    const uint32_t core = p.coreGrade == 10 ? 1400u : 1000u + 300u * (p.coreGrade - 1u) / 8u;
    permille = permille * core / 1000u;
  }
  return permille;
}

void failAttempt(GameState& state) {
  // DOC: "costs 25% of the Qi bar, never a stage"; pity grows.
  ProgressBlock& p = state.progress;
  QiNumber left = stageCost(p.realm, p.stage);
  left.mulRatio(3, 4);
  p.qi = left;
  if (p.failures < 255) ++p.failures;
  p.progressFlags |= ProgressBlock::kHalfStep;
  alchemy::clearPillInUse(state);  // user: a breakthrough pill is used up on every attempt
}

Result attempt(GameState& state) {
  Result r;
  if (!canAttempt(state)) return r;
  ProgressBlock& p = state.progress;
  // The pill is taken now (a trial took it already, when it began).
  alchemy::takePill(state, static_cast<uint8_t>(p.realm + 1));
  const Chance c = chanceFor(state);
  r.pillUsed = c.pillItem;
  r.attempted = true;
  r.chance = c.total;
  r.fromRealm = p.realm;
  r.toRealm = c.target;
  Rng rng(contentSeed(worldSeedOf(state), kKindBreakthroughRoll,
                      (static_cast<uint32_t>(p.realm) << 8) | static_cast<uint32_t>(p.failures)));
  r.roll = static_cast<uint8_t>(rng.below(100));
  r.success = r.roll < c.total;

  if (!r.success) {
    failAttempt(state);
    return r;
  }
  alchemy::clearPillInUse(state);

  if (c.needsKey && c.keyIndex >= 0) {
    r.key = scrolls::removeAt(state, static_cast<uint8_t>(c.keyIndex));  // spent only on success
    r.keyUsed = true;
  }
  r.gradeScore = gradeScore(state, c.target);
  if (c.target == 2) {
    r.gradeKind = GradeKind::Foundation;
    r.grade = p.foundationGrade = foundationGradeFor(r.gradeScore);
  } else if (c.target == 3) {
    r.gradeKind = GradeKind::Core;
    r.grade = p.coreGrade = coreGradeFor(r.gradeScore);
  } else if (c.target == 4) {
    r.gradeKind = GradeKind::Soul;
    r.grade = p.soulClarity = soulClarityFor(r.gradeScore);
  }
  p.realm = c.target;
  p.stage = 0;
  p.qi = QiNumber{};
  p.failures = 0;
  p.consolidation = 0;         // spent on this grade and this attempt
  state.flagsAndCodex[5] = 0;  // and depth (v0.2.1)
  if (c.target >= 2 && c.target <= 4) alchemy::resetGradePills(state);
  p.progressFlags &= static_cast<uint8_t>(~(ProgressBlock::kInGreatPerfection | ProgressBlock::kHalfStep));
  return r;
}

}  // namespace breakthrough
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
