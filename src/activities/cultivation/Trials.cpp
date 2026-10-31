#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Breakthrough.h"
#include "Inventory.h"
#include "Rng.h"
#include "Simulation.h"
#include "Trials.h"

namespace cultivation {
namespace trials {
namespace {

constexpr uint32_t kKindSceneRoll = 71;  // contentSeed kinds: 70-79 trials

// Strike damage in percent of max HP (agreed with the user, v0.1.9): enduring
// every strike of the first Tribulation just survives (90%); later ones
// cannot be survived by enduring alone.
static const uint8_t kStrikes3[] = {25, 30, 35};
static const uint8_t kStrikes5[] = {15, 18, 21, 24, 27};
static const uint8_t kStrikes7[] = {12, 15, 18, 21, 24, 27, 30};
static const uint8_t kStrikes9[] = {10, 13, 16, 19, 22, 25, 28, 31, 34};
// An equipped Body technique's cut when enduring, by tier (Mortal .. Saint).
static const uint8_t kBodyCut[static_cast<int>(scrolls::Tier::Count)] = {10, 20, 30, 40, 50};

uint8_t& stepByte(GameState& s) { return s.techniqueMeta[1]; }
uint8_t& usedByte(GameState& s) { return s.techniqueMeta[2]; }
uint8_t& scenesByte(GameState& s) { return s.techniqueMeta[3]; }

uint16_t ceilPercent(const uint32_t value, const uint32_t percent) {
  return static_cast<uint16_t>((value * percent + 99u) / 100u);
}

bool canTurn(const scrolls::Kind k) {
  return k == scrolls::Kind::Sword || k == scrolls::Kind::PalmFist || k == scrolls::Kind::Movement;
}

uint8_t targetOf(const GameState& s) { return static_cast<uint8_t>(s.progress.realm + 1); }

// The scroll a Burn uses: the weakest unequipped one, never the key the roll will spend.
int burnCandidate(const GameState& s, const bool mindOnly) {
  const int key = scrolls::keyFor(s, targetOf(s));
  int best = -1;
  uint32_t bestStrength = 0;
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    if (static_cast<int>(i) == key) continue;
    const scrolls::Scroll sc = scrolls::get(s, i);
    if (sc.equipped()) continue;
    if (mindOnly && sc.kind != scrolls::Kind::Mind) continue;
    if (best < 0 || scrolls::strength(sc) < bestStrength) {
      best = i;
      bestStrength = scrolls::strength(sc);
    }
  }
  return best;
}

}  // namespace

Kind kindFor(const uint8_t target) {
  switch (target) {
    case 3:
    case 5:
    case 7:
    case 9:
      return Kind::Tribulation;
    case 4:
    case 6:
    case 8:
      return Kind::HeartDemon;
    default:
      return Kind::None;
  }
}

uint8_t strikesFor(const uint8_t target) { return kindFor(target) == Kind::Tribulation ? target : 0; }

uint8_t strikePercent(const uint8_t strikes, const uint8_t index) {
  const uint8_t* table = strikes == 3 ? kStrikes3 : strikes == 5 ? kStrikes5 : strikes == 7 ? kStrikes7 : kStrikes9;
  const uint8_t size = strikes == 3 ? 3 : strikes == 5 ? 5 : strikes == 7 ? 7 : 9;
  return table[index < size ? index : size - 1];
}

uint8_t bodyCutPercent(const GameState& s) {
  uint8_t cut = 0;
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if ((sc.flags & scrolls::Scroll::kEquippedTechnique) != 0 && sc.kind == scrolls::Kind::Body) {
      const uint8_t c = kBodyCut[static_cast<int>(scrolls::tierOf(sc))];
      if (c > cut) cut = c;
    }
  }
  return cut;
}

bool inTrial(const GameState& s) { return (s.progress.progressFlags & ProgressBlock::kInTrial) != 0; }

uint8_t step(const GameState& s) { return s.techniqueMeta[1]; }

bool begin(GameState& s) {
  if (inTrial(s)) return true;
  const Kind k = kindFor(targetOf(s));
  if (k == Kind::None || !breakthrough::canAttempt(s)) return false;
  if (k == Kind::Tribulation && s.progress.hp == 0) return false;
  s.progress.progressFlags |= ProgressBlock::kInTrial;
  alchemy::takePill(s, targetOf(s));  // v0.2.0: taken before the trial; lost with it, or spent on the roll
  stepByte(s) = 0;
  usedByte(s) = 0;
  scenesByte(s) = 0;
  return true;
}

void end(GameState& s) {
  s.progress.progressFlags &= static_cast<uint8_t>(~ProgressBlock::kInTrial);
  stepByte(s) = 0;
  usedByte(s) = 0;
  scenesByte(s) = 0;
}

Strike currentStrike(const GameState& s) {
  Strike st;
  st.count = strikesFor(targetOf(s));
  if (st.count == 0) return st;
  st.index = step(s) < st.count ? step(s) : static_cast<uint8_t>(st.count - 1);
  st.percent = strikePercent(st.count, st.index);
  st.base = ceilPercent(s.progress.hpMax, st.percent);

  // Endure, cut by the best equipped Body technique.
  const uint8_t cut = bodyCutPercent(s);
  const uint8_t n = scrolls::count(s);
  st.options[st.optionCount++] = {Answer::Endure, -1, ceilPercent(st.base, 100u - cut)};

  // Turn, once per equipped Sword, Palm/Fist or Movement technique.
  uint8_t ordinal = 0;  // among equipped techniques, in library order
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if ((sc.flags & scrolls::Scroll::kEquippedTechnique) == 0) continue;
    const bool used = (s.techniqueMeta[2] & (1u << ordinal)) != 0;
    ++ordinal;
    if (used || !canTurn(sc.kind)) continue;
    st.options[st.optionCount++] = {Answer::Turn, static_cast<int16_t>(i), ceilPercent(st.base, 50)};
  }

  if (inventory::count(s, inventory::Item::ThunderTalisman) > 0) st.options[st.optionCount++] = {Answer::Shield, -1, 0};

  const int burn = burnCandidate(s, false);
  if (burn >= 0) st.options[st.optionCount++] = {Answer::Burn, static_cast<int16_t>(burn), 0};
  return st;
}

StrikeResult answerStrike(GameState& s, const uint8_t choice) {
  StrikeResult r;
  if (!inTrial(s) || kindFor(targetOf(s)) != Kind::Tribulation) return r;
  const Strike st = currentStrike(s);
  if (choice >= st.optionCount) return r;
  const StrikeOption& o = st.options[choice];
  r.damage = o.damage;

  if (o.answer == Answer::Turn) {
    uint8_t ordinal = 0;
    for (int16_t i = 0; i < o.slot; ++i) {
      if ((scrolls::get(s, static_cast<uint8_t>(i)).flags & scrolls::Scroll::kEquippedTechnique) != 0) ++ordinal;
    }
    usedByte(s) = static_cast<uint8_t>(usedByte(s) | (1u << ordinal));
  } else if (o.answer == Answer::Burn) {
    r.burned = scrolls::removeAt(s, static_cast<uint8_t>(o.slot));
    r.didBurn = true;
  } else if (o.answer == Answer::Shield) {
    if (!inventory::remove(s, inventory::Item::ThunderTalisman)) return StrikeResult{};
    r.shielded = true;
  }

  ProgressBlock& p = s.progress;
  if (o.damage >= p.hp) {
    p.hp = 0;
    r.progress = Progress::Lost;
    end(s);
    return r;
  }
  p.hp = static_cast<uint16_t>(p.hp - o.damage);
  stepByte(s) = static_cast<uint8_t>(stepByte(s) + 1);
  if (stepByte(s) >= st.count) {
    r.progress = Progress::Won;
    end(s);
  }
  return r;
}

uint8_t sceneChance(const GameState& s) {
  uint32_t chance = 40u + static_cast<uint32_t>(s.progress.daoHeart) * 30u / 100u;
  const uint8_t core = s.progress.coreGrade;
  if (core >= 10) {
    chance += 20;
  } else if (core >= 1) {
    chance += (core - 1u) * 20u / 8u;  // 9th grade +0 ... 1st grade +20
  }
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if ((sc.flags & scrolls::Scroll::kEquippedTechnique) != 0 && sc.kind == scrolls::Kind::Mind) {
      chance += 10;
      break;
    }
  }
  return static_cast<uint8_t>(chance > 95 ? 95 : chance);
}

int burnableMindScroll(const GameState& s) { return burnCandidate(s, true); }

uint8_t scenesLost(const GameState& s) { return s.techniqueMeta[3] & 0x0F; }
uint8_t scenesWon(const GameState& s) { return static_cast<uint8_t>(s.techniqueMeta[3] >> 4); }

SceneResult answerScene(GameState& s, const SceneAnswer answer) {
  SceneResult r;
  if (!inTrial(s) || kindFor(targetOf(s)) != Kind::HeartDemon) return r;
  r.chance = sceneChance(s);
  if (answer == SceneAnswer::Incense) {
    if (!inventory::remove(s, inventory::Item::CalmIncense)) return r;
    r.incense = true;
    const uint32_t c = r.chance + static_cast<uint32_t>(inventory::kIncenseBonus);
    r.chance = static_cast<uint8_t>(c > 99 ? 99 : c);
  }
  if (answer == SceneAnswer::BurnMind) {
    const int slot = burnableMindScroll(s);
    if (slot < 0) return r;
    r.burned = scrolls::removeAt(s, static_cast<uint8_t>(slot));
    r.didBurn = true;
    r.passed = true;
  } else {
    const ProgressBlock& p = s.progress;
    Rng rng(contentSeed(worldSeedOf(s), kKindSceneRoll,
                        (static_cast<uint32_t>(p.realm) << 16) | (static_cast<uint32_t>(p.failures) << 8) | step(s)));
    r.roll = static_cast<uint8_t>(rng.below(100));
    r.passed = r.roll < r.chance;
  }
  uint8_t lost = scenesLost(s);
  uint8_t won = scenesWon(s);
  if (r.passed) {
    ++won;
  } else {
    ++lost;
  }
  scenesByte(s) = static_cast<uint8_t>((won << 4) | lost);
  stepByte(s) = static_cast<uint8_t>(stepByte(s) + 1);
  if (lost >= kHeartDemonLosses) {
    r.progress = Progress::Lost;
    end(s);
  } else if (won >= kHeartDemonScenes - kHeartDemonLosses + 1) {
    r.progress = Progress::Won;
    end(s);
  }
  return r;
}

}  // namespace trials
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
