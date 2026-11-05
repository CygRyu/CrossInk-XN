#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Codex.h"
#include "Daos.h"
#include "Inventory.h"
#include "Rivals.h"
#include "Rng.h"
#include "Scrolls.h"
#include "Simulation.h"

namespace cultivation {
namespace codex {
namespace {

static const GroupInfo kGroups[static_cast<int>(Group::Count)] = {
    {kRealms, kMortalRealmCount},
    {kTalents, kTalentCount},
    {kOrigins, static_cast<uint8_t>(Origin::Count)},
    {kElements, static_cast<uint8_t>(Element::Count)},
    {kDaos, static_cast<uint8_t>(daos::Dao::Count)},
    {kScrolls, static_cast<uint8_t>(static_cast<int>(scrolls::Kind::Count) * static_cast<int>(scrolls::Tier::Count))},
    {kPills, alchemy::kRecipeCount},
    {kTraits, static_cast<uint8_t>(static_cast<int>(rivals::Trait::Count) - 1)},
    {kSacred, 1},
};
static_assert(kRealms + kMortalRealmCount <= kTalents, "codex groups overlap");
static_assert(kScrolls + 30 <= kPills, "codex groups overlap");
static_assert(kPills + alchemy::kRecipeCount <= kTraits, "codex groups overlap");
static_assert(kTraits + static_cast<int>(rivals::Trait::Count) - 1 <= kSacred, "codex groups overlap");
static_assert(kMementos + kMementoCount <= kBytes * 8, "mementos past the codex bytes");
static_assert(kFirstByte + kBytes <= 48, "codex past flagsAndCodex");

// The bag item that shows a pill kind was held (alchemy::Pill order).
bool pillHeld(const GameState& s, const alchemy::Pill pill) {
  using inventory::Item;
  auto anyQuality = [&](const Item base) {
    for (uint8_t q = 0; q < 3; ++q) {
      if (inventory::count(s, static_cast<Item>(static_cast<uint8_t>(base) + q)) > 0) return true;
    }
    return false;
  };
  auto group = [&](const Item first, const uint8_t g) {
    return anyQuality(static_cast<Item>(static_cast<uint8_t>(first) + g * 3));
  };
  switch (pill) {
    case alchemy::Pill::QiGathering:
      return inventory::count(s, Item::QiPill) > 0;
    case alchemy::Pill::TonicMinor:
      return inventory::count(s, Item::TonicMinor) > 0;
    case alchemy::Pill::TonicGreater:
      return inventory::count(s, Item::TonicGreater) > 0;
    case alchemy::Pill::TonicFull:
      return inventory::count(s, Item::TonicFull) > 0;
    case alchemy::Pill::BreakthroughCommon:
      return group(Item::BreakthroughPill, 0);
    case alchemy::Pill::BreakthroughSpirit:
      return group(Item::BreakthroughPill, 1);
    case alchemy::Pill::BreakthroughRare:
      return group(Item::BreakthroughPill, 2);
    case alchemy::Pill::FoundationBuilding:
      return group(Item::GradePill, 0);
    case alchemy::Pill::CoreForming:
      return group(Item::GradePill, 1);
    case alchemy::Pill::SoulClearing:
      return group(Item::GradePill, 2);
    default:
      return false;
  }
}

uint8_t popcount8(uint8_t v) {
  uint8_t n = 0;
  while (v != 0) {
    v = static_cast<uint8_t>(v & (v - 1));
    ++n;
  }
  return n;
}

}  // namespace

bool has(const GameState& state, const uint16_t bit) {
  if (bit >= kBytes * 8) return false;
  return (state.flagsAndCodex[kFirstByte + bit / 8] & (1u << (bit % 8))) != 0;
}

void mark(GameState& state, const uint16_t bit) {
  if (bit >= kBytes * 8) return;
  uint8_t& b = state.flagsAndCodex[kFirstByte + bit / 8];
  b = static_cast<uint8_t>(b | (1u << (bit % 8)));
}

uint16_t count(const GameState& state) {
  uint16_t n = 0;
  for (uint8_t i = 0; i < kBytes; ++i) n = static_cast<uint16_t>(n + popcount8(state.flagsAndCodex[kFirstByte + i]));
  return n;
}

const GroupInfo& groupInfo(const Group group) {
  const int g = static_cast<int>(group);
  return kGroups[g < static_cast<int>(Group::Count) ? g : 0];
}

uint8_t groupFound(const GameState& state, const Group group) {
  const GroupInfo& g = groupInfo(group);
  uint8_t n = 0;
  for (uint8_t i = 0; i < g.size; ++i) n = static_cast<uint8_t>(n + (has(state, g.first + i) ? 1 : 0));
  return n;
}

void observe(GameState& state) {
  if (!state.isCreated()) return;
  const uint8_t realm = state.progress.realm < kMortalRealmCount ? state.progress.realm : kMortalRealmCount - 1;
  for (uint8_t r = 0; r <= realm; ++r) mark(state, kRealms + r);
  if (state.character.talent < kTalentCount) mark(state, kTalents + state.character.talent);
  mark(state, kOrigins + static_cast<uint16_t>(state.character.origin));
  mark(state, kElements + static_cast<uint16_t>(state.character.rootElement));
  for (uint8_t d = 0; d < static_cast<uint8_t>(daos::Dao::Count); ++d) {
    if (daos::found(state, static_cast<daos::Dao>(d))) mark(state, kDaos + d);
  }
  const uint8_t n = scrolls::count(state);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(state, i);
    if (sc.titleHash == 0) continue;
    const int kind = static_cast<int>(sc.kind);
    if (kind < static_cast<int>(scrolls::Kind::Count)) {
      mark(state, static_cast<uint16_t>(kScrolls + kind * static_cast<int>(scrolls::Tier::Count) +
                                        static_cast<int>(scrolls::tierOf(sc))));
    }
    if (sc.comprehension == scrolls::kSacredComprehension) mark(state, kSacred);
  }
  for (uint8_t p = 0; p < alchemy::kRecipeCount; ++p) {
    if (pillHeld(state, static_cast<alchemy::Pill>(p))) mark(state, kPills + p);
  }
  for (uint8_t i = 0; i < rivals::kNemesisMax; ++i) {
    rivals::Nemesis nm;
    if (!rivals::nemesis(state, i, nm)) continue;
    for (uint8_t t = 0; t < nm.traitCount && t < rivals::kMaxTraits; ++t) {
      const int trait = static_cast<int>(nm.traits[t]);
      if ((nm.revealed & (1u << t)) != 0 && trait > 0 && trait < static_cast<int>(rivals::Trait::Count)) {
        mark(state, static_cast<uint16_t>(kTraits + trait - 1));
      }
    }
  }
}

Rarity rarityOf(const uint8_t memento) {
  const uint8_t i = static_cast<uint8_t>(memento % kSetSize);
  return i < 5 ? Rarity::Common : i < 7 ? Rarity::Rare : Rarity::Legendary;
}

bool hasMemento(const GameState& state, const uint8_t memento) {
  return memento < kMementoCount && has(state, kMementos + memento);
}

uint8_t mementosFound(const GameState& state) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMementoCount; ++i) n = static_cast<uint8_t>(n + (hasMemento(state, i) ? 1 : 0));
  return n;
}

uint8_t mementosInSet(const GameState& state, const uint8_t set) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kSetSize; ++i) {
    n = static_cast<uint8_t>(n + (hasMemento(state, static_cast<uint8_t>(set * kSetSize + i)) ? 1 : 0));
  }
  return n;
}

uint8_t mementosOfRarity(const GameState& state, const Rarity rarity) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMementoCount; ++i) {
    if (rarityOf(i) == rarity && hasMemento(state, i)) ++n;
  }
  return n;
}

uint8_t chancePercent(const Source source) {
  switch (source) {
    case Source::RegionCleared:
      return 20;
    case Source::Cave:
      return 10;
    case Source::Ruin:
      return 5;
    case Source::DailyTasks:
      return 1;
  }
  return 0;
}

uint16_t duplicateStones(const Rarity rarity) {
  return rarity == Rarity::Legendary ? 150 : rarity == Rarity::Rare ? 60 : 20;
}

uint8_t pick(const uint32_t seed) {
  Rng rng(mix32(seed, 0x4D454D4Fu));  // "MEMO"
  const uint32_t r = rng.below(100);
  const uint8_t set = static_cast<uint8_t>(rng.below(kSetCount));
  uint8_t index;
  if (r < 75) {
    index = static_cast<uint8_t>(rng.below(5));
  } else if (r < 95) {
    index = static_cast<uint8_t>(5 + rng.below(2));
  } else {
    index = 7;
  }
  return static_cast<uint8_t>(set * kSetSize + index);
}

Find roll(GameState& state, const Source source, const uint32_t seed) {
  Find f;
  if (Rng(seed).below(100) >= chancePercent(source)) return f;
  f.memento = pick(seed);
  if (hasMemento(state, f.memento)) {
    f.duplicate = true;
    f.stones = duplicateStones(rarityOf(f.memento));
    state.progress.stones += f.stones;
  } else {
    mark(state, kMementos + f.memento);
  }
  return f;
}

}  // namespace codex
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
