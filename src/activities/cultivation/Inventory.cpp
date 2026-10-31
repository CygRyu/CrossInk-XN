#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Inventory.h"

namespace cultivation {
namespace inventory {
namespace {

// Prices agreed with the user (v0.1.10), in Spirit Stones; v0.2.0 pills are
// worth three times their agreed sell price (items sell back at a third).
uint16_t legacyPrice(const Item item) {
  switch (item) {
    case Item::LegacyHerbCommon:
    case Item::LegacyOreCommon:
      return 10;
    case Item::LegacyHerbSpirit:
    case Item::LegacyOreSpirit:
      return 30;
    case Item::LegacyHerbRare:
    case Item::LegacyOreRare:
      return 80;
    case Item::TonicMinor:
      return 30;
    case Item::TonicGreater:
      return 80;
    case Item::TonicFull:
      return 200;
    case Item::ThunderTalisman:
      return 150;
    case Item::CalmIncense:
      return 120;
    case Item::RegionTalisman:
      return 400;
    default:
      return 0;
  }
}

static const uint16_t kIngredientPrice[static_cast<int>(Grade::Count)] = {10, 30, 80};
// Breakthrough pills sell for 40/60/80 (common), 100/150/200 (spirit), 250/350/450 (rare).
static const uint16_t kBreakthroughPillPrice[9] = {120, 180, 240, 300, 450, 600, 750, 1050, 1350};
static const uint16_t kGradePillPrice[3] = {150, 210, 270};  // sell 50 / 70 / 90 by quality
constexpr uint16_t kQiPillPrice = 60;                        // sell 20

constexpr size_t kSlotSize = 3;

Item idAt(const GameState& s, const uint8_t slot) {
  const uint8_t id = s.bag[slot * kSlotSize];
  return id < static_cast<uint8_t>(Item::Count) ? static_cast<Item>(id) : Item::None;
}
uint16_t countAt(const GameState& s, const uint8_t slot) {
  return static_cast<uint16_t>(s.bag[slot * kSlotSize + 1] | (s.bag[slot * kSlotSize + 2] << 8));
}
void setSlot(GameState& s, const uint8_t slot, const Item item, const uint16_t n) {
  s.bag[slot * kSlotSize] = n == 0 ? 0 : static_cast<uint8_t>(item);
  s.bag[slot * kSlotSize + 1] = n == 0 ? 0 : static_cast<uint8_t>(n);
  s.bag[slot * kSlotSize + 2] = n == 0 ? 0 : static_cast<uint8_t>(n >> 8);
}
int slotOf(const GameState& s, const Item item) {
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (idAt(s, i) == item && countAt(s, i) > 0) return i;
  }
  return -1;
}

static_assert(static_cast<int>(Item::Count) <= kSlots, "one slot per kind: the bag can never be full");
static_assert(sizeof(GameState::bag) == kSlots * kSlotSize, "bag block");

}  // namespace

uint16_t price(const Item item) {
  if (isIngredient(item)) return kIngredientPrice[static_cast<int>(gradeOfRegion(regionOf(item)))];
  if (isBreakthroughPill(item))
    return kBreakthroughPillPrice[static_cast<uint8_t>(item) - static_cast<uint8_t>(Item::BreakthroughPill)];
  if (isGradePill(item)) return kGradePillPrice[static_cast<int>(pillQuality(item))];
  if (item == Item::QiPill) return kQiPillPrice;
  if (item == Item::Dross) return 15;  // sells for 5
  return legacyPrice(item);
}

uint8_t healPercent(const Item item) {
  switch (item) {
    case Item::TonicMinor:
      return 20;
    case Item::TonicGreater:
      return 50;
    case Item::TonicFull:
      return 100;
    default:
      return 0;
  }
}

uint16_t count(const GameState& s, const Item item) {
  const int slot = slotOf(s, item);
  return slot < 0 ? 0 : countAt(s, static_cast<uint8_t>(slot));
}

uint16_t add(GameState& s, const Item item, const uint16_t n) {
  if (item == Item::None || item == Item::RecipeSlip || item >= Item::Count || n == 0) return 0;
  int slot = slotOf(s, item);
  if (slot < 0) {
    for (uint8_t i = 0; i < kSlots && slot < 0; ++i) {
      if (idAt(s, i) == Item::None || countAt(s, i) == 0) slot = i;
    }
    if (slot < 0) return 0;  // cannot happen: fewer kinds than slots (static_assert below)
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

uint16_t room(const GameState& s, const Item item) { return static_cast<uint16_t>(kMaxCount - count(s, item)); }

AddResult addOrSell(GameState& s, const Item item, const uint16_t n) {
  AddResult r;
  r.added = add(s, item, n);
  const uint16_t extra = static_cast<uint16_t>(n - r.added);
  if (extra == 0) return r;
  if (isIngredient(item) || item == Item::Dross) {
    r.sold = extra;
    r.soldStones = static_cast<uint32_t>(sellPrice(item)) * extra;
    s.progress.stones += r.soldStones;
  } else {
    r.lost = extra;
  }
  return r;
}

bool migrateLegacyBag(GameState& s) {
  bool any = false;
  for (const uint8_t b : s.inventory) any = any || b != 0;
  if (!any) return false;
  // Region i opens at realm i + 1, so realm r has min(r, 10) regions open.
  const uint8_t open = s.progress.realm < kRegions ? s.progress.realm : kRegions;
  auto newest = [open](const uint8_t first, const uint8_t last) -> uint8_t {
    if (open == 0 || open - 1 < first) return first;
    return open - 1 < last ? static_cast<uint8_t>(open - 1) : last;
  };
  for (uint8_t slot = 0; slot < 32; ++slot) {
    const uint8_t id = s.inventory[slot * kSlotSize];
    const auto n = static_cast<uint16_t>(s.inventory[slot * kSlotSize + 1] | (s.inventory[slot * kSlotSize + 2] << 8));
    if (id == 0 || n == 0) continue;
    Item item = id < static_cast<uint8_t>(Item::RecipeSlip) ? static_cast<Item>(id) : Item::None;
    switch (item) {
      case Item::LegacyHerbCommon:
        item = herbFor(newest(0, 2));
        break;
      case Item::LegacyHerbSpirit:
        item = herbFor(newest(3, 5));
        break;
      case Item::LegacyHerbRare:
        item = herbFor(newest(6, 9));
        break;
      case Item::LegacyOreCommon:
        item = oreFor(newest(0, 2));
        break;
      case Item::LegacyOreSpirit:
        item = oreFor(newest(3, 5));
        break;
      case Item::LegacyOreRare:
        item = oreFor(newest(6, 9));
        break;
      default:
        break;
    }
    add(s, item, n);
  }
  s.inventory.fill(0);
  return true;
}

}  // namespace inventory
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
