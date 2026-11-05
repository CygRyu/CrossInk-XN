#pragma once

#include <cstdint>

#include "GameState.h"

// v0.3.0 "A New Life" (agreed with the user in session 14): lifespan, the end
// of a life (reincarnation, old age, abandoning it), imprints, legacy perks and
// the Qi bonus a lineage carries. Pure rules over GameState: no I/O, no UI.
//
// What survives a life is the save v7 `lineage` block (72 bytes) and the codex
// (Codex.h). Everything else starts again.
//
//   0     flags: 0x01 a life has ended (the block is in use), 0x02 the Lineage cell was opened,
//               0x04 the ended life's Chronicle still has to be moved to lives/,
//               0x08 Familiar Face is owned but a new rival was chosen
//   1     the best realm ever reached + 1 (0 = none yet)
//   2-3   lives ended (u16)
//   4-5   imprints not spent (u16)
//   6-7   imprints earned in all (u16)
//   8     perks: Tested Again (bits 0-3), Fated (bits 4-7)
//   9     perks: Past Life Echo (bits 0-3), Hidden Purse (bits 4-7)
//   10    perks: bit 0 Old Roads, bit 1 Familiar Face, bits 2-3 Remembered Recipes
//   11    how the last life ended (EndKind + 1)
//   12-15 the last life's rival (its seed); Familiar Face brings it back (0 = none)
//   16-39 Past Life Echo: three scrolls in the 8-byte slot format (title hash 0 = empty)
//   40-63 the last life's name (UTF-8, NUL-padded)
//   64-65 the last life's age in years (u16)
//   66    the last life's realm + 1
//   67    codex entries when this life began (imprints count the new ones)
//   68-69 the last life's number (u16), for its Chronicle in lives/
//   70-71 spare
namespace cultivation {
namespace lineage {

constexpr uint8_t kFlags = 0;
constexpr uint8_t kBestRealm = 1;
constexpr uint8_t kLivesEnded = 2;
constexpr uint8_t kImprints = 4;
constexpr uint8_t kImprintsEarned = 6;
constexpr uint8_t kPerksA = 8;
constexpr uint8_t kPerksB = 9;
constexpr uint8_t kPerksC = 10;
constexpr uint8_t kLastEnd = 11;
constexpr uint8_t kRivalSeed = 12;
constexpr uint8_t kEcho = 16;
constexpr uint8_t kPrevName = 40;
constexpr uint8_t kPrevAge = 64;
constexpr uint8_t kPrevRealm = 66;
constexpr uint8_t kCodexAtStart = 67;
constexpr uint8_t kPrevLife = 68;

constexpr uint8_t kFlagUsed = 0x01;
constexpr uint8_t kFlagCellSeen = 0x02;
constexpr uint8_t kFlagMoveChronicle = 0x04;
constexpr uint8_t kFlagNewRival = 0x08;  // Familiar Face owned, but the player chose a new rival (remembered)

// --- The Qi bonus (user, session 14: +3% a realm past Foundation, never added up) --------------
constexpr uint16_t kQiBonusPerRealmPermille = 30;
constexpr uint8_t kFirstBonusRealm = 2;            // Foundation Establishment gives nothing; Core Formation +3%
int bestRealm(const GameState& state);             // -1 before any life has ended
uint16_t qiBonusPermille(const GameState& state);  // 0 .. 210, from the best realm of past lives
uint16_t qiBonusFor(int realm);                    // what a best realm would give

uint16_t livesEnded(const GameState& state);
uint16_t imprints(const GameState& state);
uint16_t imprintsEarned(const GameState& state);
bool inUse(const GameState& state);

// --- Lifespan -----------------------------------------------------------------------------------
// The current realm's lifespan (DOC realm table). Warnings at 80% and 95%; the life ends at 100%.
enum class Age : uint8_t { Fine = 0, Greying, Failing, Over };
uint16_t lifespanYears(const GameState& state);
uint16_t yearsLeft(const GameState& state);  // 0 when over
Age ageState(const GameState& state);
Age ageStateAt(uint16_t years, uint16_t lifespan);

// --- Imprints (user, session 14: per life) ------------------------------------------------------
// The best realm of this life (none below Foundation), plus 1 per Dao found
// this life, plus 1 per 8 codex entries that are new this life. Abandoning a
// life earns none.
constexpr uint8_t kCodexPerImprint = 8;
uint8_t realmImprints(uint8_t realm);  // 0, 0, 2, 4, 7, 10, 14, 18, 23, 28
uint16_t imprintsFor(const GameState& state);

// --- Perks --------------------------------------------------------------------------------------
enum class Perk : uint8_t { TestedAgain = 0, Fated, Echo, HiddenPurse, OldRoads, Recipes, FamiliarFace, Count };
constexpr uint8_t kPerkCount = static_cast<uint8_t>(Perk::Count);
struct PerkInfo {
  uint8_t maxRank;
  uint8_t cost;  // imprints per rank
};
const PerkInfo& perkInfo(Perk perk);
uint8_t rank(const GameState& state, Perk perk);
bool canBuy(const GameState& state, Perk perk);
bool buy(GameState& state, Perk perk);
constexpr uint8_t kFatedLuckPerRank = 3;
constexpr uint16_t kPurseStonesPerRank = 50;
uint8_t fatedLuck(const GameState& state);     // added to ruin searches
uint8_t extraRerolls(const GameState& state);  // Tested Again

// --- The end of a life --------------------------------------------------------------------------
enum class EndKind : uint8_t { Reincarnated = 0, OldAge, Abandoned, Count };
constexpr uint8_t kEchoSlots = 3;
bool canReincarnate(const GameState& state);  // from Foundation Establishment
bool canAbandon(const GameState& state);      // any time, while a character lives

struct EndResult {
  uint16_t imprints = 0;    // earned by this life
  uint16_t lifeNumber = 0;  // the life that ended
  int bestBefore = -1;
  int bestAfter = -1;
  uint8_t echoKept = 0;
};
// Ends the current life: records it in the lineage (best realm, imprints, the
// last life's name and age, Echo scrolls by slot in the library, the rival's
// seed for Familiar Face), then clears everything that belongs to one life.
// Kept: the lineage block, the codex (Codex.h), and the Guide-seen bit. The
// character is then "not created": the next visit starts character creation.
// `echo` lists library slots (up to the Echo rank; ignored when abandoning).
EndResult endLife(GameState& state, EndKind kind, const uint8_t* echo, uint8_t echoCount);

// Called by createCharacter after the new life is set up (the lineage block
// and the codex already restored): Hidden Purse stones, Echo scrolls (one
// quality step lower, unequipped), Old Roads (the first region charted),
// Remembered Recipes, and the codex count this life starts from.
void applyToNewLife(GameState& state);

// One quality step lower for an Echo scroll: Sacred -> Perfected ... Incomplete -> Fragmentary.
uint8_t echoComprehension(uint8_t comprehension);

// The last life, for the new Chronicle's opening.
void previousName(const GameState& state, char* out, size_t len);
uint16_t previousAge(const GameState& state);
int previousRealm(const GameState& state);  // -1 when no life ended
EndKind lastEnd(const GameState& state);
uint16_t previousLife(const GameState& state);
// The rival's seed Familiar Face brings back, 0 when the perk is not owned or no life ended.
uint32_t carriedRivalSeed(const GameState& state);
// Familiar Face's choice (user, session 14): keep the last rival or meet a new one. Asked on the
// Legacy Perks screen before each new life; the last answer is remembered. Default: keep.
bool keepsRival(const GameState& state);
void setKeepsRival(GameState& state, bool keep);

bool cellSeen(const GameState& state);
void markCellSeen(GameState& state);
bool chronicleMovePending(const GameState& state);
void setChronicleMovePending(GameState& state, bool pending);

}  // namespace lineage
}  // namespace cultivation
