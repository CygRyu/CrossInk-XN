#pragma once

#include <cstdint>

#include "GameState.h"
#include "Inventory.h"

// Exploration (v0.1.10, agreed with the user; DOC: Exploration and world
// generation). Pure rules: no I/O, no UI, no clock.
//
// Regions: one per realm on the Mortal Plane. Region i opens at realm i + 1
// (Qi Condensation opens the first) and stays open. Its name, theme, layout,
// beasts and guardian come from the world seed, the region index and its
// reshape counter, and are never stored.
//
// The map is a 6x6 grid under fog. 8-12 tiles are impassable; every walkable
// tile is reachable from the entrance on the map's edge. A tile's terrain
// shows once you stand next to it, its content once you step on it. 15-20
// tiles hold content (herb, ore, ruin, hidden cave, beast lair, merchant,
// quiet spot, the guardian); the rest are paths. A step to a neighbouring
// walkable tile costs 1 stamina.
//
// Random encounters happen on manual steps only, at the region's own rate
// (5-18%), rolled from the world seed and a stored step counter, so leaving
// and re-entering never rerolls them. A lost fight costs HP and throws the
// player onto a charted neighbouring tile. At 0 HP the map cannot be walked.
//
// Charted = stepped on. Every walkable tile charted and the guardian beaten:
// Cleared, with a one-time reward. Herbs, ores, quiet spots and lairs come
// back 3 reading hours after the first one was used; ruins, caves and the
// guardian are once per layout. 20 reading hours after clearing (or at once
// with a Region Talisman) the region reshapes: new layout, guardian and
// unique finds; name, theme and beasts stay.
//
// Save (GameState::exploration, 200 bytes, all-zero = nothing explored):
//   0-3   encounter step counter u32
//   4     region of the waiting fight (+1; 0 = none)
//   5     waiting fight: 0 none, 1 beast, 2 lair, 3 guardian
//   6-7   spare
//   8 + 19 x region:
//     0-4   charted tiles (36 bits)     5-9   used-up tiles (36 bits)
//     10    reshape counter             11    flags (cleared, guardian beaten)
//     12    current tile + 1 (0 = not entered yet)
//     13-14 regrowth time               15-16 clear time
//           (reading time in 10-minute units, + 1; 0 = none)
//     17    regrowth cycle (merchant stock)   18 merchant purchases (bits)
namespace cultivation {
namespace explore {

constexpr uint8_t kRegionCount = 10;  // one per realm; nine open on the Mortal Plane
constexpr uint8_t kGrid = 6;
constexpr uint8_t kTiles = kGrid * kGrid;
constexpr uint8_t kNoTile = 0xFF;
constexpr uint32_t kRegrowUnits = 18;     // 3 reading hours, in 10-minute units
constexpr uint32_t kUpheavalUnits = 120;  // 20 reading hours
constexpr uint8_t kMerchantStockMax = 5;

enum class Tile : uint8_t { Blocked = 0, Path, Herb, Ore, Ruin, Cave, Lair, Merchant, Quiet, Guardian, Count };

struct Layout {
  Tile tiles[kTiles] = {};
  uint8_t entrance = 0;
  uint8_t walkable = 0;          // charting goal
  uint8_t content = 0;           // tiles that are not paths
  uint8_t encounterPercent = 5;  // 5-18
  uint8_t guardianLevel = 0;
  uint8_t lairBonus[kTiles] = {};  // fixed extra level per lair, 0-4
};

Layout generate(uint32_t worldSeed, uint8_t region, uint8_t reshape);
Layout layoutOf(const GameState& state, uint8_t region);

inline uint8_t rowOf(const uint8_t tile) { return tile / kGrid; }
inline uint8_t colOf(const uint8_t tile) { return tile % kGrid; }
// Up to 4 orthogonal neighbours in the order north, east, south, west; returns how many.
uint8_t neighbours(uint8_t tile, uint8_t out[4]);
bool adjacent(uint8_t a, uint8_t b);
inline bool walkable(const Layout& l, const uint8_t tile) { return tile < kTiles && l.tiles[tile] != Tile::Blocked; }
bool regrows(Tile t);  // herb, ore, quiet spot, lair
bool unique(Tile t);   // ruin, cave, guardian

// --- Regions -----------------------------------------------------------------

uint8_t unlockRealm(uint8_t region);  // realm that opens it
bool unlocked(const GameState& state, uint8_t region);
uint8_t unlockedCount(const GameState& state);
const char* themeKey(uint8_t region);  // grammar theme: "marsh", "wastes" ...

bool charted(const GameState& state, uint8_t region, uint8_t tile);
bool used(const GameState& state, uint8_t region, uint8_t tile);
uint8_t chartedCount(const GameState& state, uint8_t region);
uint8_t reshapeCount(const GameState& state, uint8_t region);
bool cleared(const GameState& state, uint8_t region);
bool guardianBeaten(const GameState& state, uint8_t region);
uint8_t merchantCycle(const GameState& state, uint8_t region);
// The player's tile in the region; the entrance before the first visit.
uint8_t position(const GameState& state, const Layout& layout, uint8_t region);
// True while the tile is waiting to regrow / the region to reshape, with the
// reading minutes left.
uint32_t regrowMinutesLeft(const GameState& state, uint8_t region);
uint32_t upheavalMinutesLeft(const GameState& state, uint8_t region);

// Enters the region's map: places the player on the entrance on the first
// visit (charting it). Returns the tile the player stands on.
uint8_t enter(GameState& state, const Layout& layout, uint8_t region);

// --- Combat --------------------------------------------------------------------

enum class Danger : uint8_t { Trivial = 0, Weak, Dangerous, Hazardous, Extreme, Impossible, Count };

// 10 x realm + stage (0-9) + equipped Sword / Palm-Fist techniques (1-5 by
// tier each) + Foundation Perfect 2 / Heavenly 3 + Golden Core 3rd grade or better 2.
int playerLevel(const GameState& state);
inline int regionBase(const uint8_t region) { return 10 * (region + 1); }
Danger dangerFor(int difference);  // player level minus enemy level
Danger regionDanger(const GameState& state, uint8_t region);
uint8_t winChance(int difference);  // 50 + 6 x d, 5..95
uint8_t lossPercent(Danger danger);
bool hasMovement(const GameState& state);
uint8_t bodyCut(const GameState& state);  // the best equipped Body technique's cut, 0-50%

enum class FightKind : uint8_t { None = 0, Beast, Lair, Guardian };

struct Fight {
  FightKind kind = FightKind::None;
  uint8_t region = 0;
  uint8_t tile = 0;
  int level = 0;
  Danger danger = Danger::Dangerous;
  uint8_t chance = 0;      // percent to win "Fight"
  uint8_t slipChance = 0;  // percent to slip away (100 with a Movement technique)
  uint16_t damage = 0;     // HP lost if the fight is lost
  uint16_t slipDamage = 0;
};

// The fight waiting on the map (after a step), if any.
Fight pendingFight(const GameState& state, const Layout& layout);
// The region whose map holds the waiting fight, or -1 when none is waiting.
int pendingRegion(const GameState& state);
uint32_t beastSeed(const GameState& state);    // names the waiting beast: stable until it is fought
uint32_t stepCounter(const GameState& state);  // steps and fights so far (seeds text too)

// --- Stepping and tiles ----------------------------------------------------------

struct Reward {
  uint32_t stones = 0;
  inventory::Item item = inventory::Item::None;
  uint16_t itemCount = 0;
  inventory::Item item2 = inventory::Item::None;  // the guardian and clear rewards give two
  uint16_t item2Count = 0;
};

enum class TileOutcome : uint8_t {
  Nothing,      // a path, or a tile already used up
  Gathered,     // herb or ore
  RuinWon,      // stones
  RuinTrapped,  // HP lost
  CaveFound,
  Rested,      // quiet spot: HP healed
  RestedFull,  // quiet spot with HP already full: not used up
  Merchant,    // open the shop
};

struct StepResult {
  bool moved = false;
  bool fight = false;  // pendingFight() now holds it
  TileOutcome outcome = TileOutcome::Nothing;
  Reward reward;
  uint16_t hpLost = 0;
  uint16_t hpHealed = 0;
  bool clearedNow = false;  // the region was just cleared: clearReward holds its reward
  Reward clearReward;
};

enum class StepError : uint8_t { None, NotAdjacent, Blocked, NoStamina, NoHp, FightWaiting };
StepError canStep(const GameState& state, const Layout& layout, uint8_t region, uint8_t to);

// Spends 1 stamina and moves; then a lair or guardian fight, a random
// encounter, or the tile's content.
StepResult step(GameState& state, const Layout& layout, uint8_t region, uint8_t to);
// Runs the content of the tile the player stands on (Confirm on the current
// tile: merchant; a herb that regrew since).
StepResult resolveHere(GameState& state, const Layout& layout, uint8_t region);

enum class FightChoice : uint8_t { Fight, Slip };

struct FightResult {
  bool won = false;
  bool slipped = false;  // got away without a fight
  uint8_t roll = 0;
  uint16_t hpLost = 0;
  uint8_t thrownTo = kNoTile;  // after a loss or slipping away from a lair or guardian
  Reward reward;
  bool guardianBeaten = false;
  StepResult tile;  // after winning or slipping past a beast: the tile's content
};

FightResult fight(GameState& state, const Layout& layout, FightChoice choice);

// --- Regrowth, upheaval and talismans ---------------------------------------------------

// Regrows used-up tiles whose time has come and reshapes regions 20 reading
// hours after they were cleared. Returns a bit per region that reshaped.
uint16_t tick(GameState& state);
bool canUseRegionTalisman(const GameState& state, uint8_t region);
bool useRegionTalisman(GameState& state, uint8_t region);
void reshape(GameState& state, uint8_t region);

// --- Merchants -------------------------------------------------------------------------

struct StockLine {
  inventory::Item item = inventory::Item::None;
  bool limited = false;  // one unit per restock (tonics, treasures, talismans)
  bool soldOut = false;
};
struct Stock {
  StockLine lines[kMerchantStockMax];
  uint8_t count = 0;
};
// Seeded by the tile and the region's regrowth cycle: reopening the shop shows
// the same stock; it restocks when the region regrows.
Stock stockFor(const GameState& state, const Layout& layout, uint8_t region, uint8_t tile);
bool buy(GameState& state, const Layout& layout, uint8_t region, uint8_t tile, uint8_t line);
bool sell(GameState& state, inventory::Item item);

}  // namespace explore
}  // namespace cultivation
