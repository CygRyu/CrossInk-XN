#pragma once

#include <cstdint>

#include "GameState.h"

// Items (v0.1.10, agreed with the user; v0.2.0 alchemy): every region's own
// herb and ore, healing tonics in three tiers, treasures for tribulations,
// the Region Talisman, and the pills alchemy brews.
//
// Save v4 (v0.2.0): the bag is the 192-byte `bag` block, 64 slots of 3 bytes,
// item id u8 + count u16 (little-endian); id 0 = empty slot, so an all-zero
// block is an empty bag. There are fewer item kinds than slots and one slot
// per kind, so the bag can never be full. The v0.1.10 bag (the 96-byte
// `inventory` block) is moved over once by migrateLegacyBag() and then zeroed.
namespace cultivation {
namespace inventory {

enum class Item : uint8_t {
  None = 0,
  // v0.1.10 ingredients by grade. Only ever found in an old bag:
  // migrateLegacyBag() turns them into regional ingredients.
  LegacyHerbCommon,
  LegacyHerbSpirit,
  LegacyHerbRare,
  LegacyOreCommon,
  LegacyOreSpirit,
  LegacyOreRare,
  TonicMinor,         // 7: heals 20% of max HP
  TonicGreater,       // 50%
  TonicFull,          // to full
  ThunderTalisman,    // a Tribulation answer: absorbs one strike
  CalmIncense,        // +25 to one Heart Demon scene; drives off a spirit
  RegionTalisman,     // 12: reshapes a cleared region at once
  Herb0,              // 13-22: each region's herb (name from the world seed and the region's theme)
  Ore0 = Herb0 + 10,  // 23-32: each region's ore
  // 33-41: breakthrough pills by grade (common, spirit, rare) x quality (low, medium, high)
  BreakthroughPill = Ore0 + 10,
  // 42-50: grade pills (Foundation-Building, Core-Forming, Soul-Clearing) x quality
  GradePill = BreakthroughPill + 9,
  QiPill = GradePill + 9,  // 51
  RecipeSlip,              // 52: merchant stock only; buying one teaches a recipe, it never sits in the bag
  Dross,                   // 53 (v0.2.1): what a failed brew leaves; worth selling, nothing else
  Count
};

constexpr uint8_t kSlots = 64;
constexpr uint16_t kMaxCount = 9999;
constexpr uint8_t kIncenseBonus = 25;
constexpr uint8_t kRegions = 10;  // explore::kRegionCount

// Ingredient grade by region: regions 1-3 common, 4-6 spirit, 7-10 rare.
enum class Grade : uint8_t { Common = 0, Spirit, Rare, Count };
enum class Quality : uint8_t { Low = 0, Medium, High, Count };

inline Grade gradeOfRegion(const uint8_t region) {
  return region < 3 ? Grade::Common : region < 6 ? Grade::Spirit : Grade::Rare;
}
inline Item herbFor(const uint8_t region) {
  return static_cast<Item>(static_cast<uint8_t>(Item::Herb0) + (region < kRegions ? region : kRegions - 1));
}
inline Item oreFor(const uint8_t region) {
  return static_cast<Item>(static_cast<uint8_t>(Item::Ore0) + (region < kRegions ? region : kRegions - 1));
}
inline bool isHerb(const Item i) { return i >= Item::Herb0 && i < Item::Ore0; }
inline bool isOre(const Item i) { return i >= Item::Ore0 && i < Item::BreakthroughPill; }
inline bool isIngredient(const Item i) { return isHerb(i) || isOre(i); }
// The region an ingredient comes from (0-9); 0 for anything else.
inline uint8_t regionOf(const Item i) {
  if (isHerb(i)) return static_cast<uint8_t>(static_cast<uint8_t>(i) - static_cast<uint8_t>(Item::Herb0));
  if (isOre(i)) return static_cast<uint8_t>(static_cast<uint8_t>(i) - static_cast<uint8_t>(Item::Ore0));
  return 0;
}

inline Item breakthroughPill(const Grade g, const Quality q) {
  return static_cast<Item>(static_cast<uint8_t>(Item::BreakthroughPill) + static_cast<uint8_t>(g) * 3 +
                           static_cast<uint8_t>(q));
}
inline bool isBreakthroughPill(const Item i) { return i >= Item::BreakthroughPill && i < Item::GradePill; }
// Grade pills: 0 Foundation-Building, 1 Core-Forming, 2 Soul-Clearing.
inline Item gradePill(const uint8_t kind, const Quality q) {
  return static_cast<Item>(static_cast<uint8_t>(Item::GradePill) + (kind < 3 ? kind : 2) * 3 + static_cast<uint8_t>(q));
}
inline bool isGradePill(const Item i) { return i >= Item::GradePill && i < Item::QiPill; }
// For breakthrough and grade pills: their grade (or kind) and quality.
inline uint8_t pillGroup(const Item i) {
  const uint8_t base = static_cast<uint8_t>(isGradePill(i) ? Item::GradePill : Item::BreakthroughPill);
  return static_cast<uint8_t>((static_cast<uint8_t>(i) - base) / 3);
}
inline Quality pillQuality(const Item i) {
  const uint8_t base = static_cast<uint8_t>(isGradePill(i) ? Item::GradePill : Item::BreakthroughPill);
  return static_cast<Quality>((static_cast<uint8_t>(i) - base) % 3);
}

uint16_t price(Item item);  // what the item is worth in Spirit Stones (merchant price where sold)
// User: items sell back at a third of their price.
inline uint16_t sellPrice(const Item item) { return static_cast<uint16_t>(price(item) / 3u); }
uint8_t healPercent(Item item);  // tonics: 20 / 50 / 100; others 0
inline bool isTonic(const Item item) { return healPercent(item) > 0; }

uint16_t count(const GameState& state, Item item);
// Adds up to kMaxCount; returns how many were added.
uint16_t add(GameState& state, Item item, uint16_t n = 1);
// v0.2.1 (user): never lose a prize to the 9,999 limit silently. Herbs, ores
// and dross past it are sold at a third (`soldStones` says for how much);
// anything else stays at 9,999 and `kept` reports what did not fit.
struct AddResult {
  uint16_t added = 0;
  uint16_t sold = 0;  // ordinary items sold because the bag held 9,999 already
  uint32_t soldStones = 0;
  uint16_t lost = 0;  // other items past the limit (the screens say so)
};
AddResult addOrSell(GameState& state, Item item, uint16_t n = 1);
// How many more of an item fit under the limit.
uint16_t room(const GameState& state, Item item);
bool remove(GameState& state, Item item, uint16_t n = 1);
uint8_t kinds(const GameState& state);  // item kinds held

// The tonic "Use a tonic" takes when `need` HP would do (the smallest held
// that covers it, else the largest held). None when no tonic is held.
Item tonicFor(const GameState& state, uint16_t need);
// HP a tonic would restore now (capped at max HP).
uint16_t healAmount(const GameState& state, Item tonic);
// Uses one tonic; returns HP restored (0 if none was held).
uint16_t useTonic(GameState& state, Item tonic);

// v0.2.0: moves a v0.1.10 bag (the old 96-byte block) into the new one, once.
// Graded herbs and ores become the herb or ore of the newest open region of
// their grade (or the first region of that grade if none of them is open
// yet); every other item keeps its id. Returns true if anything was moved.
bool migrateLegacyBag(GameState& state);

}  // namespace inventory
}  // namespace cultivation
