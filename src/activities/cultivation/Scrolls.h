#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"

// Technique Scrolls (DOC: Techniques, Daos and meridians; HANDOFF section 14).
// Every book marked finished becomes a scroll. Pure functions over GameState,
// so every rule has a host test; the files behind them are in ScrollStore.
//
// The library is the save's reserved techniqueScrolls block (513 bytes, no
// format change): byte 0 is the count, then up to 64 slots of 8 bytes,
// packed at the front in the order they were minted:
//
//   [0..3] title hash (u32 LE, never 0)   [4] hours read in that book (cap 255)
//   [5] element (bits 0-2) | kind << 3    [6] comprehension 0..100 (quality)
//   [7] flags: kEquippedManual, kEquippedTechnique
//
// Tier is derived from the hours, so it is never stored. The name is
// regenerated from the title hash and the world seed whenever it is shown
// (ScrollNames), so it costs no save space either.
namespace cultivation {
namespace scrolls {

constexpr uint8_t kCapacity = 64;
constexpr size_t kSlotSize = 8;
constexpr uint8_t kMaxEquippedTechniques = 3;
constexpr uint8_t kManualPity = 4;  // user: if none of the last 4 minted was a manual, the next one is
// A book read for less than this earns no scroll (anti-farming: marking
// unread books finished must not mint keys or sellable scrolls).
constexpr uint32_t kMinBookSeconds = 30 * 60;

enum class Tier : uint8_t { Mortal = 0, Yellow, Earth, Heaven, Saint, Count };
// DOC kinds. Manual is the "Cultivation manual": equip one for its Qi rate.
enum class Kind : uint8_t { Sword = 0, PalmFist, Body, Movement, Mind, Manual, Count };
// Quality bands from comprehension (share of the book read): DOC example 92% -> Refined.
enum class Quality : uint8_t { Fragmentary = 0, Incomplete, Complete, Refined, Perfected, Count };

struct Scroll {
  uint32_t titleHash = 0;
  uint8_t hours = 0;
  Element element = Element::Earth;
  Kind kind = Kind::Sword;
  uint8_t comprehension = 0;  // 0..100
  uint8_t flags = 0;

  static constexpr uint8_t kEquippedManual = 0x01;
  static constexpr uint8_t kEquippedTechnique = 0x02;

  bool equipped() const { return (flags & (kEquippedManual | kEquippedTechnique)) != 0; }
};

// --- Properties ---------------------------------------------------------------

Tier tierForHours(uint32_t hours);  // DOC: <3 Mortal, 3-8 Yellow, 8-15 Earth, 15-25 Heaven, 25+ Saint
inline Tier tierOf(const Scroll& s) { return tierForHours(s.hours); }
Quality qualityFor(uint8_t comprehension);  // <50, 50-74, 75-89, 90-97, 98+
// DOC: Morning Wood, Afternoon Fire, Evening Metal, Night Water; Earth when no
// time-of-day bucket holds more than 40% of the book's reading.
Element elementForBuckets(const uint32_t seconds[4]);
// Manual luck (user): random from the title and the world, plus pity.
Kind kindFor(uint32_t titleHash, uint32_t worldSeed, uint8_t sinceManual);

// Qi multiplier of a manual, permille: tier (1.15 .. 2.00) x quality (0.90 .. 1.10).
uint32_t manualPermille(const Scroll& s);
uint32_t sellPrice(const Scroll& s);  // Spirit Stones by tier (proposed: 25 / 60 / 150 / 300 / 500)
// For "weakest" (auto-sell, key choice): tier first, then comprehension.
uint32_t strength(const Scroll& s);

// --- The library in the save ------------------------------------------------------

uint8_t count(const GameState& state);
Scroll get(const GameState& state, uint8_t index);
void put(GameState& state, uint8_t index, const Scroll& s);
// The 8-byte slot format, also used by the v5 memory block (v0.2.1).
Scroll decode(const uint8_t* bytes);
void encode(const Scroll& s, uint8_t* bytes);
// v0.2.1: the last scroll burned in a tribulation, for Heart Demon scenes
// (memory bytes 24-31). False when none was burned this life.
void rememberBurned(GameState& state, const Scroll& s);
bool lastBurned(const GameState& state, Scroll& out);
// Removes slot `index`, moving the later ones down. Returns the removed scroll.
Scroll removeAt(GameState& state, uint8_t index);
int find(const GameState& state, uint32_t titleHash);  // index or -1

int equippedManual(const GameState& state);  // index or -1
uint8_t equippedTechniques(const GameState& state);
// The manual with the highest Qi multiplier (equipped or not), or -1.
int bestManual(const GameState& state);

// Equip: a Manual becomes the one equipped manual (the old one is unequipped);
// any other kind takes one of 3 technique places. Returns false if all 3 are taken.
bool equip(GameState& state, uint8_t index);
void unequip(GameState& state, uint8_t index);
// Sells an unequipped scroll for its price. Returns the stones gained (0 if it cannot be sold).
uint32_t sell(GameState& state, uint8_t index);

// Qi rate of the equipped manual, permille (1000 when none).
uint32_t equippedManualPermille(const GameState& state);
// The equipped manual's element matches the root (DOC: +15 grade score, +5 breakthrough chance).
bool manualMatchesRoot(const GameState& state);

// --- Keys ------------------------------------------------------------------------

// Lowest scroll tier that opens the breakthrough INTO `targetRealm` (user,
// session 3): Foundation Mortal, Core Yellow, Nascent Soul Earth, later Heaven.
// Qi Condensation needs no key (returns Tier::Count).
Tier keyTierFor(uint8_t targetRealm);
// The scroll the game would spend: the lowest-tier unequipped scroll that
// qualifies (then the lowest comprehension, then the oldest). -1 if none.
int keyFor(const GameState& state, uint8_t targetRealm);

// --- Minting ----------------------------------------------------------------------

// One finished book as the hook recorded it.
struct BookRecord {
  uint32_t titleHash = 0;
  uint32_t seconds = 0;     // reading recorded in that book
  uint8_t bucket = 0xFF;    // dominant time-of-day bucket 0..3, 0xFF when none is over 40%
  uint8_t shareRead = 100;  // 0..100
};

enum class EventKind : uint8_t {
  Minted,       // a new scroll
  AutoSold,     // the library was full: the weakest unequipped scroll was sold
  Improved,     // a reread raised the scroll's quality
  Unchanged,    // finished again, nothing new
  AlreadyUsed,  // this life already learned from the book (the scroll was spent or sold)
  TooShort,     // read for under 30 minutes: no scroll
};

struct Event {
  EventKind kind = EventKind::Minted;
  Scroll scroll;                 // the scroll concerned (for AutoSold: the one sold)
  uint8_t oldComprehension = 0;  // Improved
  uint32_t stones = 0;           // AutoSold
  uint16_t minutes = 0;          // TooShort: minutes read in the book
};

// Applies one finished book: a new scroll, or a reread raising an existing
// one. `alreadyMinted` says whether this life has minted this book before
// (the minted list in ScrollStore). Writes up to 2 events (a new scroll may
// push out the weakest one); returns how many.
uint8_t mint(GameState& state, const BookRecord& book, bool alreadyMinted, Event out[2]);

}  // namespace scrolls
}  // namespace cultivation
