#pragma once

#include <cstdint>

#include "GameState.h"

// Items (v0.1.10, agreed with the user): herbs and ores for alchemy (v0.2.0;
// for now they are kept and can be sold), healing tonics in three tiers,
// treasures for tribulations and the Region Talisman.
//
// Stored in the save's reserved 96-byte inventory block: 32 slots of 3 bytes,
// item id u8 + count u16 (little-endian); id 0 = empty slot, so an all-zero
// block is an empty bag. There are fewer item kinds than slots and one slot
// per kind, so the bag can never be full.
namespace cultivation {
namespace inventory {

enum class Item : uint8_t {
  None = 0,
  HerbCommon,
  HerbSpirit,
  HerbRare,
  OreCommon,
  OreSpirit,
  OreRare,
  TonicMinor,       // heals 20% of max HP
  TonicGreater,     // 50%
  TonicFull,        // to full
  ThunderTalisman,  // a Tribulation answer: absorbs one strike
  CalmIncense,      // +25 to one Heart Demon scene
  RegionTalisman,   // reshapes a cleared region at once
  Count
};

constexpr uint8_t kSlots = 32;
constexpr uint16_t kMaxCount = 9999;
constexpr uint8_t kIncenseBonus = 25;

uint16_t price(Item item);  // merchant price in Spirit Stones
// User: items sell back at a third of their price.
inline uint16_t sellPrice(const Item item) { return static_cast<uint16_t>(price(item) / 3u); }
uint8_t healPercent(Item item);  // tonics: 20 / 50 / 100; others 0
inline bool isTonic(const Item item) { return healPercent(item) > 0; }

// Grade by region: regions 1-3 common, 4-6 spirit, 7-10 rare.
Item herbFor(uint8_t region);
Item oreFor(uint8_t region);

uint16_t count(const GameState& state, Item item);
// Adds up to kMaxCount; returns how many were added.
uint16_t add(GameState& state, Item item, uint16_t n = 1);
bool remove(GameState& state, Item item, uint16_t n = 1);
uint8_t kinds(const GameState& state);  // item kinds held

// The tonic "Use a tonic" takes when `need` HP would do (the smallest held
// that covers it, else the largest held). None when no tonic is held.
Item tonicFor(const GameState& state, uint16_t need);
// HP a tonic would restore now (capped at max HP).
uint16_t healAmount(const GameState& state, Item tonic);
// Uses one tonic; returns HP restored (0 if none was held).
uint16_t useTonic(GameState& state, Item tonic);

}  // namespace inventory
}  // namespace cultivation
