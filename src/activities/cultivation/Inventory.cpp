#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Inventory.h"

namespace cultivation {
namespace inventory {
namespace {

// Prices agreed with the user (v0.1.10), in Spirit Stones.
static const uint16_t kPrices[static_cast<int>(Item::Count)] = {0, 10, 30, 80, 10, 30, 80, 30, 80, 200, 150, 120, 400};
static const uint8_t kHeal[static_cast<int>(Item::Count)] = {0, 0, 0, 0, 0, 0, 0, 20, 50, 100, 0, 0, 0};

constexpr size_t kSlotSize = 3;

Item idAt(const GameState& s, const uint8_t slot) {
  const uint8_t id = s.inventory[slot * kSlotSize];
  return id < static_cast<uint8_t>(Item::Count) ? static_cast<Item>(id) : Item::None;
}
uint16_t countAt(const GameState& s, const uint8_t slot) {
  return static_cast<uint16_t>(s.inventory[slot * kSlotSize + 1] | (s.inventory[slot * kSlotSize + 2] << 8));
}
void setSlot(GameState& s, const uint8_t slot, const Item item, const uint16_t n) {
  s.inventory[slot * kSlotSize] = n == 0 ? 0 : static_cast<uint8_t>(item);
  s.inventory[slot * kSlotSize + 1] = n == 0 ? 0 : static_cast<uint8_t>(n);
  s.inventory[slot * kSlotSize + 2] = n == 0 ? 0 : static_cast<uint8_t>(n >> 8);
}
int slotOf(const GameState& s, const Item item) {
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (idAt(s, i) == item && countAt(s, i) > 0) return i;
  }
  return -1;
}

}  // namespace

uint16_t price(const Item item) {
  const int i = static_cast<int>(item);
  return i < static_cast<int>(Item::Count) ? kPrices[i] : 0;
}

uint8_t healPercent(const Item item) {
  const int i = static_cast<int>(item);
  return i < static_cast<int>(Item::Count) ? kHeal[i] : 0;
}

Item herbFor(const uint8_t region) {
  return region < 3 ? Item::HerbCommon : region < 6 ? Item::HerbSpirit : Item::HerbRare;
}
Item oreFor(const uint8_t region) {
  return region < 3 ? Item::OreCommon : region < 6 ? Item::OreSpirit : Item::OreRare;
}

uint16_t count(const GameState& s, const Item item) {
  const int slot = slotOf(s, item);
  return slot < 0 ? 0 : countAt(s, static_cast<uint8_t>(slot));
}

uint16_t add(GameState& s, const Item item, const uint16_t n) {
  if (item == Item::None || item >= Item::Count || n == 0) return 0;
  int slot = slotOf(s, item);
  if (slot < 0) {
    for (uint8_t i = 0; i < kSlots && slot < 0; ++i) {
      if (idAt(s, i) == Item::None || countAt(s, i) == 0) slot = i;
    }
    if (slot < 0) return 0;  // cannot happen: fewer kinds than slots
  }
  const uint16_t have = countAt(s, static_cast<uint8_t>(slot));
  const uint16_t room = have < kMaxCount ? static_cast<uint16_t>(kMaxCount - have) : 0;
  const uint16_t added = n < room ? n : room;
  setSlot(s, static_cast<uint8_t>(slot), item, static_cast<uint16_t>(have + added));
  return added;
}

bool remove(GameState& s, const Item item, const uint16_t n) {
  const int slot = slotOf(s, item);
  if (slot < 0) return false;
  const uint16_t have = countAt(s, static_cast<uint8_t>(slot));
  if (have < n) return false;
  setSlot(s, static_cast<uint8_t>(slot), item, static_cast<uint16_t>(have - n));
  return true;
}

uint8_t kinds(const GameState& s) {
  uint8_t k = 0;
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (idAt(s, i) != Item::None && countAt(s, i) > 0) ++k;
  }
  return k;
}

uint16_t healAmount(const GameState& s, const Item tonic) {
  const ProgressBlock& p = s.progress;
  if (!isTonic(tonic) || p.hp >= p.hpMax) return 0;
  const uint32_t heal = (static_cast<uint32_t>(p.hpMax) * healPercent(tonic) + 99u) / 100u;
  const uint32_t room = static_cast<uint32_t>(p.hpMax - p.hp);
  return static_cast<uint16_t>(heal < room ? heal : room);
}

Item tonicFor(const GameState& s, const uint16_t need) {
  Item largest = Item::None;
  for (const Item t : {Item::TonicMinor, Item::TonicGreater, Item::TonicFull}) {
    if (count(s, t) == 0) continue;
    largest = t;
    const uint32_t heal = (static_cast<uint32_t>(s.progress.hpMax) * healPercent(t) + 99u) / 100u;
    if (heal >= need) return t;
  }
  return largest;
}

uint16_t useTonic(GameState& s, const Item tonic) {
  if (!isTonic(tonic) || count(s, tonic) == 0) return 0;
  const uint16_t healed = healAmount(s, tonic);
  remove(s, tonic);
  s.progress.hp = static_cast<uint16_t>(s.progress.hp + healed);
  return healed;
}

}  // namespace inventory
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
