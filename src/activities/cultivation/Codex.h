#pragma once

#include <cstdint>

#include "GameState.h"

// v0.3.0: the codex and mementos (agreed with the user in session 14). What a
// lineage has seen, kept across lives; cosmetic only, never power.
//
// 256 bits in flagsAndCodex bytes 8-39 (the doc's codex bits, reserved since
// milestone 1). Bit numbers:
//     0-9    realms reached              10-14  talents         15-19  origins played
//    20-24   root elements               25-33  Daos found
//    34-63   scroll kind x tier (6 x 5)  64-73  pills held (alchemy::Pill order)
//    74-87   nemesis traits noticed      88     a Sacred scroll
//    89-95   spare
//    96-159  mementos (8 sets of 8: in each set 5 common, 2 rare, 1 legendary)
//   160-255  spare
namespace cultivation {
namespace codex {

constexpr uint8_t kFirstByte = 8;  // flagsAndCodex
constexpr uint8_t kBytes = 32;
constexpr uint16_t kRealms = 0;
constexpr uint16_t kTalents = 10;
constexpr uint16_t kOrigins = 15;
constexpr uint16_t kElements = 20;
constexpr uint16_t kDaos = 25;
constexpr uint16_t kScrolls = 34;
constexpr uint16_t kPills = 64;
constexpr uint16_t kTraits = 74;
constexpr uint16_t kSacred = 88;
constexpr uint16_t kMementos = 96;
constexpr uint8_t kMementoCount = 64;
constexpr uint8_t kSetCount = 8;
constexpr uint8_t kSetSize = 8;

bool has(const GameState& state, uint16_t bit);
void mark(GameState& state, uint16_t bit);
uint16_t count(const GameState& state);  // every entry found, mementos included

// The codex pages (Collection screen), in order.
enum class Group : uint8_t { Realms = 0, Talents, Origins, Elements, Daos, Scrolls, Pills, Traits, Sacred, Count };
struct GroupInfo {
  uint16_t first;
  uint8_t size;
};
const GroupInfo& groupInfo(Group group);
uint8_t groupFound(const GameState& state, Group group);
constexpr uint16_t kCodexEntries = 89;  // all groups

// Marks what the current life shows: realm, talent, origin, root, Daos,
// scrolls in the library, pills in the bag, nemesis traits noticed, Sacred.
// Cheap; called before every save.
void observe(GameState& state);

// --- Mementos -----------------------------------------------------------------------------------
enum class Rarity : uint8_t { Common = 0, Rare, Legendary };
Rarity rarityOf(uint8_t memento);
inline uint8_t setOf(uint8_t memento) { return static_cast<uint8_t>(memento / kSetSize); }
bool hasMemento(const GameState& state, uint8_t memento);
uint8_t mementosFound(const GameState& state);
uint8_t mementosInSet(const GameState& state, uint8_t set);
uint8_t mementosOfRarity(const GameState& state, Rarity rarity);

// Where one can turn up (user, session 14): a region cleared 20%, a cave 10%,
// a won ruin search 5%, all three daily tasks collected 1%.
enum class Source : uint8_t { RegionCleared = 0, Cave, Ruin, DailyTasks };
uint8_t chancePercent(Source source);
// Which one, when one turns up: common 75%, rare 20%, legendary 5%; then any of that rarity.
constexpr uint8_t kNone = 0xFF;
struct Find {
  uint8_t memento = kNone;
  bool duplicate = false;  // already in the collection: sold for `stones` (added to the player's)
  uint16_t stones = 0;
};
uint16_t duplicateStones(Rarity rarity);  // 20 / 60 / 150
// Rolls `source`'s chance with `seed`; on a find, records it (or sells a duplicate).
Find roll(GameState& state, Source source, uint32_t seed);
// The draw itself, without the chance (tests, and roll()).
uint8_t pick(uint32_t seed);

}  // namespace codex
}  // namespace cultivation
