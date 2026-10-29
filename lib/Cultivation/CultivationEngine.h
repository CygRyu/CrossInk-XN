#pragma once
// Reading-driven xianxia cultivation game: pure game logic, no hardware or
// Arduino dependencies so it can be unit-tested natively.
//
// Model (the "hybrid"):
//  * Core progression is DERIVED from reading stats (pages, time, books,
//    streak). Reading stats can be reset or merged from another device, so the
//    game keeps a tiny ledger (banked Qi + last derived value) instead of
//    trusting the raw stats directly: Qi only ever goes up.
//  * A few choices only the player can make (Dao path) plus a random seed are
//    persisted by CultivationStore.
//  * The journal ("zero-player" part) is a pure function of
//    (seed, path, qiFrom, qiTo), so it never needs to be stored.
//
// Optional heavier layers (techniques, sect, inventory) are intentionally NOT
// part of this core. See docs/cultivation.md for the planned extension points.

#include <cstddef>
#include <cstdint>

namespace Cultivation {

struct ReadingInputs {
  uint32_t pages = 0;           // total pages turned
  uint32_t seconds = 0;         // total reading seconds
  uint32_t books = 0;           // completed books
  uint16_t longestStreak = 0;   // longest daily reading streak
};

enum class Path : uint8_t { Undecided = 0, Orthodox = 1, Demonic = 2, Rogue = 3 };

// Persisted ledger. banked is the character's Qi; lastDerived is the derived
// value seen at the previous update.
struct Ledger {
  uint32_t banked = 0;
  uint32_t lastDerived = 0;
};

struct Progress {
  uint8_t realm = 0;       // index into the realm table
  uint8_t layer = 1;       // 1-based layer inside the realm
  uint32_t qiIntoLayer = 0;
  uint32_t qiForLayer = 1;
  bool maxed = false;      // top realm, top layer
};

struct Attributes {
  uint32_t body = 0;     // hours read
  uint32_t insight = 0;  // pages / 25
  uint32_t resolve = 0;  // longest streak (days)
  uint32_t fortune = 0;  // books finished
};

struct JournalEntry {
  char text[120];
};

constexpr size_t kRealmCount = 7;
constexpr uint8_t kLayersPerRealm = 9;
constexpr size_t kMaxJournalEntries = 5;
// The Dao path can be chosen once the character reaches this realm index.
constexpr uint8_t kPathChoiceRealm = 1;  // Foundation Establishment

// ---- Qi and progression --------------------------------------------------
uint32_t qiFromReading(const ReadingInputs& in);
// Banks any increase of derivedNow over ledger.lastDerived. A decrease (stats
// reset) just rebases lastDerived. Returns true if the ledger changed.
bool applyLedger(Ledger& ledger, uint32_t derivedNow);

Progress progressFor(uint32_t qi);
uint16_t stageIndex(const Progress& p);  // realm * 9 + (layer - 1)
const char* realmName(uint8_t realm);
bool pathChoiceAvailable(const Progress& p);
const char* pathName(Path path);

Attributes attributesFor(const ReadingInputs& in);
uint32_t combatPower(const Progress& p, const Attributes& a);

// ---- Flavour -------------------------------------------------------------
// Writes a deterministic character name such as "Lin Wuyan" into buf.
void characterName(uint32_t seed, char* buf, size_t bufSize);

// Fills out[] (capacity max, at most kMaxJournalEntries used) with the events
// for the Qi interval (qiFrom, qiTo]. Breakthroughs come first, then random
// encounters. Returns the number of entries written. Pure and deterministic.
size_t generateJournal(uint32_t seed, Path path, uint32_t qiFrom, uint32_t qiTo, JournalEntry* out, size_t max);

}  // namespace Cultivation
