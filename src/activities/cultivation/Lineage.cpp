#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Achievements.h"
#include "Alchemy.h"
#include "Codex.h"
#include "Daos.h"
#include "Exploration.h"
#include "Lineage.h"
#include "Rivals.h"
#include "Scrolls.h"
#include "Simulation.h"

namespace cultivation {
namespace lineage {
namespace {

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void set16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void set32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// Max rank and cost per rank (user, session 14).
static const PerkInfo kPerks[kPerkCount] = {
    {5, 2},  // Tested Again: +1 destiny reroll
    {5, 2},  // Fated: +3 luck on ruin searches
    {3, 4},  // Past Life Echo: one scroll carried into the next life
    {4, 1},  // Hidden Purse: +50 starting stones
    {1, 3},  // Old Roads: the first region starts charted
    {2, 2},  // Remembered Recipes: one more recipe known at birth
    {1, 2},  // Familiar Face: the last rival is reborn with you
};

static const uint8_t kRealmImprints[kMortalRealmCount] = {0, 0, 2, 4, 7, 10, 14, 18, 23, 28};

uint8_t rawRank(const GameState& s, const Perk perk) {
  const uint8_t* l = s.lineage.data();
  switch (perk) {
    case Perk::TestedAgain:
      return l[kPerksA] & 0x0F;
    case Perk::Fated:
      return l[kPerksA] >> 4;
    case Perk::Echo:
      return l[kPerksB] & 0x0F;
    case Perk::HiddenPurse:
      return l[kPerksB] >> 4;
    case Perk::OldRoads:
      return l[kPerksC] & 0x01;
    case Perk::FamiliarFace:
      return (l[kPerksC] >> 1) & 0x01;
    case Perk::Recipes:
      return (l[kPerksC] >> 2) & 0x03;
    default:
      return 0;
  }
}

void setRank(GameState& s, const Perk perk, const uint8_t r) {
  uint8_t* l = s.lineage.data();
  switch (perk) {
    case Perk::TestedAgain:
      l[kPerksA] = static_cast<uint8_t>((l[kPerksA] & 0xF0) | (r & 0x0F));
      break;
    case Perk::Fated:
      l[kPerksA] = static_cast<uint8_t>((l[kPerksA] & 0x0F) | ((r & 0x0F) << 4));
      break;
    case Perk::Echo:
      l[kPerksB] = static_cast<uint8_t>((l[kPerksB] & 0xF0) | (r & 0x0F));
      break;
    case Perk::HiddenPurse:
      l[kPerksB] = static_cast<uint8_t>((l[kPerksB] & 0x0F) | ((r & 0x0F) << 4));
      break;
    case Perk::OldRoads:
      l[kPerksC] = static_cast<uint8_t>((l[kPerksC] & ~0x01) | (r & 0x01));
      break;
    case Perk::FamiliarFace:
      l[kPerksC] = static_cast<uint8_t>((l[kPerksC] & ~0x02) | ((r & 0x01) << 1));
      break;
    case Perk::Recipes:
      l[kPerksC] = static_cast<uint8_t>((l[kPerksC] & ~0x0C) | ((r & 0x03) << 2));
      break;
    default:
      break;
  }
}

}  // namespace

// --- Qi bonus --------------------------------------------------------------------------------------

int bestRealm(const GameState& state) { return static_cast<int>(state.lineage[kBestRealm]) - 1; }

uint16_t qiBonusFor(const int realm) {
  if (realm <= kFirstBonusRealm) return 0;
  const int r = realm < kMortalRealmCount ? realm : kMortalRealmCount - 1;
  return static_cast<uint16_t>((r - kFirstBonusRealm) * kQiBonusPerRealmPermille);
}

uint16_t qiBonusPermille(const GameState& state) { return qiBonusFor(bestRealm(state)); }

uint16_t livesEnded(const GameState& state) { return get16(state.lineage.data() + kLivesEnded); }
uint16_t imprints(const GameState& state) { return get16(state.lineage.data() + kImprints); }
uint16_t imprintsEarned(const GameState& state) { return get16(state.lineage.data() + kImprintsEarned); }
bool inUse(const GameState& state) { return (state.lineage[kFlags] & kFlagUsed) != 0; }

// --- Lifespan ------------------------------------------------------------------------------------

uint16_t lifespanYears(const GameState& state) { return realmInfo(state.progress.realm).lifespanYears; }

uint16_t yearsLeft(const GameState& state) {
  const uint16_t span = lifespanYears(state);
  const uint16_t age = ageYears(state);
  return age < span ? static_cast<uint16_t>(span - age) : 0;
}

Age ageStateAt(const uint16_t years, const uint16_t lifespan) {
  if (lifespan == 0) return Age::Fine;
  const uint32_t y = static_cast<uint32_t>(years) * 100u;
  if (years >= lifespan) return Age::Over;
  if (y >= static_cast<uint32_t>(lifespan) * 95u) return Age::Failing;
  if (y >= static_cast<uint32_t>(lifespan) * 80u) return Age::Greying;
  return Age::Fine;
}

Age ageState(const GameState& state) {
  if (!state.isCreated()) return Age::Fine;
  return ageStateAt(ageYears(state), lifespanYears(state));
}

// --- Imprints ------------------------------------------------------------------------------------

uint8_t realmImprints(const uint8_t realm) {
  return kRealmImprints[realm < kMortalRealmCount ? realm : kMortalRealmCount - 1];
}

uint16_t imprintsFor(const GameState& state) {
  uint16_t n = realmImprints(state.progress.realm);
  n = static_cast<uint16_t>(n + daos::foundCount(state));
  const uint16_t codexNow = codex::count(state);
  const uint16_t atStart = state.lineage[kCodexAtStart];
  if (codexNow > atStart) n = static_cast<uint16_t>(n + (codexNow - atStart) / kCodexPerImprint);
  return n;
}

// --- Perks ---------------------------------------------------------------------------------------

const PerkInfo& perkInfo(const Perk perk) {
  const int p = static_cast<int>(perk);
  return kPerks[p < kPerkCount ? p : 0];
}

uint8_t rank(const GameState& state, const Perk perk) {
  const uint8_t r = rawRank(state, perk);
  const uint8_t m = perkInfo(perk).maxRank;
  return r < m ? r : m;
}

bool canBuy(const GameState& state, const Perk perk) {
  if (static_cast<int>(perk) >= kPerkCount) return false;
  return rank(state, perk) < perkInfo(perk).maxRank && imprints(state) >= perkInfo(perk).cost;
}

bool buy(GameState& state, const Perk perk) {
  if (!canBuy(state, perk)) return false;
  set16(state.lineage.data() + kImprints, static_cast<uint16_t>(imprints(state) - perkInfo(perk).cost));
  setRank(state, perk, static_cast<uint8_t>(rank(state, perk) + 1));
  return true;
}

uint8_t fatedLuck(const GameState& state) { return static_cast<uint8_t>(rank(state, Perk::Fated) * kFatedLuckPerRank); }

uint8_t extraRerolls(const GameState& state) { return rank(state, Perk::TestedAgain); }

// --- The end of a life ---------------------------------------------------------------------------

bool canReincarnate(const GameState& state) { return state.isCreated() && state.progress.realm >= 2; }
bool canAbandon(const GameState& state) { return state.isCreated(); }

uint8_t echoComprehension(const uint8_t c) {
  if (c >= scrolls::kSacredComprehension) return 100;  // Perfected
  if (c >= 98) return 97;                              // Refined
  if (c >= 90) return 89;                              // Complete
  if (c >= 75) return 74;                              // Incomplete
  if (c >= 50) return 49;                              // Fragmentary
  return c;
}

EndResult endLife(GameState& state, const EndKind kind, const uint8_t* echo, const uint8_t echoCount) {
  EndResult res;
  if (!state.isCreated()) return res;
  codex::observe(state);
  // v0.3.8: a last look at the achievements, then old age or abandoning (they outlive the life).
  achievements::lifeEnding(state, kind == EndKind::OldAge, kind == EndKind::Abandoned);
  uint8_t* l = state.lineage.data();
  res.lifeNumber = lifeNumberOf(state);
  res.bestBefore = bestRealm(state);
  res.imprints = kind == EndKind::Abandoned ? 0 : imprintsFor(state);

  const uint8_t realm = state.progress.realm < kMortalRealmCount ? state.progress.realm : kMortalRealmCount - 1;
  if (res.bestBefore < realm) l[kBestRealm] = static_cast<uint8_t>(realm + 1);
  res.bestAfter = bestRealm(state);
  const uint16_t lives = livesEnded(state);
  set16(l + kLivesEnded, static_cast<uint16_t>(lives < 0xFFFF ? lives + 1 : lives));
  const uint32_t unspent = static_cast<uint32_t>(imprints(state)) + res.imprints;
  set16(l + kImprints, static_cast<uint16_t>(unspent < 0xFFFF ? unspent : 0xFFFF));
  const uint32_t earned = static_cast<uint32_t>(imprintsEarned(state)) + res.imprints;
  set16(l + kImprintsEarned, static_cast<uint16_t>(earned < 0xFFFF ? earned : 0xFFFF));
  memset(l + kPrevName, 0, kNameMaxBytes);
  memcpy(l + kPrevName, state.character.name.data(), kNameMaxBytes);
  set16(l + kPrevAge, ageYears(state));
  l[kPrevRealm] = static_cast<uint8_t>(realm + 1);
  set16(l + kPrevLife, res.lifeNumber);
  l[kLastEnd] = static_cast<uint8_t>(static_cast<uint8_t>(kind) + 1);
  // The rival this life knew (itself perhaps carried from an earlier one): Familiar Face brings it back.
  set32(l + kRivalSeed, rivals::rivalSeed(state));
  // Past Life Echo: the chosen scrolls, one quality step lower, unequipped.
  memset(l + kEcho, 0, kEchoSlots * 8);
  if (kind != EndKind::Abandoned && echo != nullptr) {
    const uint8_t allowed = rank(state, Perk::Echo);
    for (uint8_t i = 0; i < echoCount && res.echoKept < allowed && res.echoKept < kEchoSlots; ++i) {
      if (echo[i] >= scrolls::count(state)) continue;
      bool repeat = false;
      for (uint8_t j = 0; j < i; ++j) repeat = repeat || echo[j] == echo[i];
      if (repeat) continue;
      scrolls::Scroll sc = scrolls::get(state, echo[i]);
      if (sc.titleHash == 0) continue;
      sc.comprehension = echoComprehension(sc.comprehension);
      sc.flags = 0;
      scrolls::encode(sc, l + kEcho + 8 * res.echoKept);
      ++res.echoKept;
    }
  }
  l[kFlags] = static_cast<uint8_t>(l[kFlags] | kFlagUsed | kFlagMoveChronicle);

  // Everything else belongs to the life that ended.
  std::array<uint8_t, 72> kept = state.lineage;
  uint8_t codexBytes[codex::kBytes];
  memcpy(codexBytes, state.flagsAndCodex.data() + codex::kFirstByte, codex::kBytes);
  const uint8_t guide = static_cast<uint8_t>(state.flagsAndCodex[kGuideSeenByte] & kGuideSeenBit);
  const std::array<uint8_t, achievements::kBlockSize> keptAchievements = state.achievements;  // v0.3.8
  state.reset();
  state.lineage = kept;
  achievements::restoreAfterLife(state, keptAchievements.data());
  memcpy(state.flagsAndCodex.data() + codex::kFirstByte, codexBytes, codex::kBytes);
  state.flagsAndCodex[kGuideSeenByte] = guide;
  return res;
}

void applyToNewLife(GameState& state) {
  uint8_t* l = state.lineage.data();
  l[kCodexAtStart] = static_cast<uint8_t>(codex::count(state) < 255 ? codex::count(state) : 255);
  if (!inUse(state)) return;
  state.progress.stones += static_cast<uint32_t>(rank(state, Perk::HiddenPurse)) * kPurseStonesPerRank;
  for (uint8_t i = 0; i < kEchoSlots; ++i) {
    const scrolls::Scroll sc = scrolls::decode(l + kEcho + 8 * i);
    const uint8_t n = scrolls::count(state);
    if (sc.titleHash != 0 && n < scrolls::kCapacity) {
      scrolls::put(state, n, sc);
      state.techniqueScrolls[0] = static_cast<uint8_t>(n + 1);
    }
  }
  memset(l + kEcho, 0, kEchoSlots * 8);  // carried once
  if (rank(state, Perk::OldRoads) > 0) explore::chartAll(state, 0);
  uint8_t toLearn = rank(state, Perk::Recipes);
  for (uint8_t r = 0; r < alchemy::kRecipeCount && toLearn > 0; ++r) {
    if (alchemy::known(state, r)) continue;
    alchemy::learn(state, r);
    --toLearn;
  }
}

// --- The last life ---------------------------------------------------------------------------------

void previousName(const GameState& state, char* out, const size_t len) {
  if (len == 0) return;
  const size_t n = len - 1 < kNameMaxBytes ? len - 1 : kNameMaxBytes;
  memcpy(out, state.lineage.data() + kPrevName, n);
  out[n] = '\0';
}
uint16_t previousAge(const GameState& state) { return get16(state.lineage.data() + kPrevAge); }
int previousRealm(const GameState& state) { return static_cast<int>(state.lineage[kPrevRealm]) - 1; }
EndKind lastEnd(const GameState& state) {
  const uint8_t k = state.lineage[kLastEnd];
  return k >= 1 && k <= static_cast<uint8_t>(EndKind::Count) ? static_cast<EndKind>(k - 1) : EndKind::Reincarnated;
}
uint16_t previousLife(const GameState& state) { return get16(state.lineage.data() + kPrevLife); }
uint32_t carriedRivalSeed(const GameState& state) {
  return rank(state, Perk::FamiliarFace) > 0 && keepsRival(state) ? get32(state.lineage.data() + kRivalSeed) : 0u;
}
bool keepsRival(const GameState& state) { return (state.lineage[kFlags] & kFlagNewRival) == 0; }
void setKeepsRival(GameState& state, const bool keep) {
  state.lineage[kFlags] = keep ? static_cast<uint8_t>(state.lineage[kFlags] & ~kFlagNewRival)
                               : static_cast<uint8_t>(state.lineage[kFlags] | kFlagNewRival);
}

bool cellSeen(const GameState& state) { return (state.lineage[kFlags] & kFlagCellSeen) != 0; }
void markCellSeen(GameState& state) {
  state.lineage[kFlags] = static_cast<uint8_t>(state.lineage[kFlags] | kFlagCellSeen);
}
bool chronicleMovePending(const GameState& state) { return (state.lineage[kFlags] & kFlagMoveChronicle) != 0; }
void setChronicleMovePending(GameState& state, const bool pending) {
  state.lineage[kFlags] = pending ? static_cast<uint8_t>(state.lineage[kFlags] | kFlagMoveChronicle)
                                  : static_cast<uint8_t>(state.lineage[kFlags] & ~kFlagMoveChronicle);
}

}  // namespace lineage
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
