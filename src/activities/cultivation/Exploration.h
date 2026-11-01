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
//   6     v0.2.1: bit 0x01 the last step brought a random fight (no two in a row)
//         v0.2.2: ... or a meeting; 0x02 talked, 0x04 slip bought, 0x08 treasure bought,
//         0x10 bought something, bits 5-7 the traveller's ingredient bought (0-5)
//         (all about the waiting meeting)
//   7     v0.2.1: the waiting random fight is returning foe N - 1 (0 = a new one)
//         v0.2.2: the waiting meeting is cultivator N - 1 of the ring in memory[32-47]
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

// v0.2.0 (agreed with the user): every enemy is one of four kinds, each with
// one difference. Beast: as before (an equipped Movement technique always gets
// you away). Bandit: can be paid off, 15 x (region + 1) stones (30 x at a
// hideout; never a guardian), and pays double a beast's stones when beaten,
// with no herbs. Cultivator: your Sword and Palm/Fist techniques count double,
// and so does the enemy's own random extra level; a win pays 1.5x and has a
// 20% chance of a recipe slip. Spirit: cannot be outrun or paid; burning a
// Calm-Heart Incense drives it off; a win pays a beast's stones, no herbs.
// The kind comes from the seed that names the enemy, so nothing is stored.
enum class Enemy : uint8_t { Beast = 0, Bandit, Cultivator, Spirit, Count };

Enemy encounterEnemy(const GameState& state, uint8_t region);
Enemy enemyFromSeed(uint8_t region, uint32_t seed);  // a random encounter's kind from the seed that names it // the
                                                     // waiting random encounter's kind
Enemy lairEnemy(const GameState& state, uint8_t region, uint8_t tile);  // beast den or bandits' hideout
Enemy guardianEnemy(const GameState& state, uint8_t region);
uint32_t enemySeed(const GameState& state, uint8_t region, uint8_t tile);  // lairs (tile) and the guardian (kNoTile)

// v0.2.1 (user): enemies that come back. A beast or bandit from a random
// encounter that you slipped away from or paid off is remembered (a ring of 4
// in the save's memory block: region + 1, how many times it came back
// stronger (bits 0-1) and whether you paid (0x10), and the seed that names it).
// A later random encounter in that region is the same enemy one time in
// three: 3 levels stronger for each escape (at most 6), paying 1.5x. Beating
// it forgets it; running or paying again makes it stronger still.
constexpr uint8_t kFoeRing = 4;
constexpr uint8_t kFoeLevelStep = 3;
constexpr uint8_t kFoeMaxReturns = 2;
struct Foe {
  uint8_t region = 0;
  uint8_t returns = 0;  // 1-2: levels added = 3 x returns
  bool paid = false;    // the last time, you paid them off (else you ran)
  uint32_t seed = 0;    // names it (like explore::beastSeed)
};
// Foe i of the ring; false when the entry is empty.
bool foe(const GameState& state, uint8_t index, Foe& out);
// The ring entry of the waiting random fight, or -1 when it is a new enemy.
int returningFoe(const GameState& state);

enum class Danger : uint8_t { Trivial = 0, Weak, Dangerous, Hazardous, Extreme, Impossible, Count };

// 10 x realm + stage (0-9) + equipped Sword / Palm-Fist techniques (1-5 by
// tier each; twice that against a cultivator) + Foundation Perfect 2 /
// Heavenly 3 + Golden Core 3rd grade or better 2.
int playerLevel(const GameState& state, bool techniquesDouble = false);
inline int regionBase(const uint8_t region) { return 10 * (region + 1); }
Danger dangerFor(int difference);  // player level minus enemy level
Danger regionDanger(const GameState& state, uint8_t region);
uint8_t winChance(int difference);  // 50 + 6 x d, 5..95
uint8_t lossPercent(Danger danger);
bool hasMovement(const GameState& state);
uint8_t bodyCut(const GameState& state);  // the best equipped Body technique's cut, 0-50%

enum class FightKind : uint8_t { None = 0, Beast, Lair, Guardian, Meeting };  // Meeting: v0.2.2, not a fight

struct Fight {
  FightKind kind = FightKind::None;
  Enemy enemy = Enemy::Beast;
  uint8_t region = 0;
  uint8_t tile = 0;
  int level = 0;
  Danger danger = Danger::Dangerous;
  uint8_t chance = 0;      // percent to win "Fight"
  uint8_t slipChance = 0;  // percent to slip away (100 with a Movement technique)
  uint16_t damage = 0;     // HP lost if the fight is lost
  uint16_t slipDamage = 0;
  bool canSlip = true;      // not from a spirit
  uint32_t payCost = 0;     // bandits (not a guardian): stones to be let go; 0 = cannot pay
  bool canIncense = false;  // a spirit, and a Calm-Heart Incense is held
  uint8_t returns = 0;      // v0.2.1: a returning foe, back for the Nth time (0 = new)
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
  int8_t recipe = -1;  // v0.2.0: a recipe slip taught this alchemy recipe
  inventory::Item item = inventory::Item::None;
  uint16_t itemCount = 0;
  inventory::Item item2 = inventory::Item::None;  // the guardian and clear rewards give two
  uint16_t item2Count = 0;
  inventory::Item item3 = inventory::Item::None;  // v0.2.1: a hard fight's prize on top
  uint16_t item3Count = 0;
  uint32_t soldStones = 0;  // v0.2.1: herbs or ores past 9,999, sold at a third
  uint16_t lost = 0;        // other items past 9,999
};

// v0.2.1 (user): a won Hazardous, Extreme or Impossible fight has a 15 / 25 /
// 35% chance of a prize: a recipe slip if one can be taught, otherwise two of
// the next grade's first ingredient (a region-4 herb or ore in a common
// region, region 7's in a spirit one, any rare region's in a rare one).
uint8_t prizePercent(Danger danger);

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
  bool fight = false;    // pendingFight() now holds it
  bool meeting = false;  // v0.2.2: pendingMeeting() now holds a cultivator
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

// Pay: a bandit's price (refused when the stones are short). Incense: one
// Calm-Heart Incense drives a spirit off. Both end it like slipping away.
enum class FightChoice : uint8_t { Fight, Slip, Pay, Incense };

struct FightResult {
  bool won = false;
  bool slipped = false;  // got away without a fight
  bool paid = false;     // v0.2.0: a bandit took the stones (FightResult::reward.stones is 0)
  uint32_t paidStones = 0;
  bool drivenOff = false;   // v0.2.0: a spirit fled from the incense
  bool refused = false;     // nothing happened: the answer was not available (short of stones, no incense)
  bool returning = false;   // v0.2.1: it was a returning foe
  bool remembered = false;  // v0.2.1: it got away and will come back (or will come back stronger)
  bool foeBeaten = false;   // v0.2.1: a returning foe was beaten and is gone
  uint8_t roll = 0;
  uint16_t hpLost = 0;
  uint8_t thrownTo = kNoTile;  // after a loss or slipping away from a lair or guardian
  Reward reward;
  bool guardianBeaten = false;
  StepResult tile;  // after winning or slipping past a beast: the tile's content
};

FightResult fight(GameState& state, const Layout& layout, FightChoice choice);

// --- Meetings (v0.2.2) ---------------------------------------------------------------------
//
// Agreed with the user: on manual steps only, 4% of steps (never on a merchant
// tile, never right after a random fight or another meeting), a named
// cultivator walks the same road. Name, sect, temperament and level come from
// the world seed and a 10-bit id; nothing is stored for a stranger. They are
// proud (challenge you if they dare, otherwise spar), cautious (trade, or
// spar; never challenge) or greedy (challenge if they dare, otherwise trade).
// "Dare" = you would rate them Dangerous or worse. Spar: the cultivator fight
// rules, no HP lost on a loss, a beast's stones on a win. Challenge: the same
// maths and a real fight: 3x a beast's stones, 30% for a recipe slip. Trade: a
// traveller's stock (up to 5 of the next region's ingredient, a recipe slip, a
// treasure).
// Talk: free, once; proud ones let slip the guardian's kind and danger,
// cautious ones only once they remember you, greedy ones charge for it.
//
// They remember you: a ring of 4 in memory[32-47], 4 bytes each:
//   b0 id bits 0-7 | b1 bits 0-3 region + 1, 4-5 meetings (1-3), 6-7 id bits 8-9
//   b2-b3 u16: bits 0-12 your age in years at the meeting, 13-15 how it ended.
// One meeting in three in that region is a remembered cultivator, 3 levels
// stronger per earlier meeting (at most 6).
constexpr uint8_t kMeetRing = 4;
constexpr uint8_t kMeetPercent = 4;
constexpr uint8_t kMeetLevelStep = 3;
constexpr uint8_t kTraderTile = 0xFE;        // Stock/buy "tile" of the waiting meeting's traveller
constexpr uint8_t kTraderIngredientMax = 5;  // per meeting (agreed with the user)

enum class Temperament : uint8_t { Proud = 0, Cautious, Greedy };
enum class Offer : uint8_t { Spar = 0, Trade, Challenge };
enum class MeetOutcome : uint8_t { None = 0, Won, Lost, Traded, Talked, Passed };

struct Meeting {
  bool active = false;
  uint8_t region = 0;
  uint16_t id = 0;                       // 10 bits
  uint32_t nameSeed = 0;                 // names the cultivator (exploretext::cultivatorName)
  int ring = -1;                         // ring index of a remembered cultivator, else -1
  uint8_t meets = 0;                     // times met before (0 = a stranger)
  MeetOutcome last = MeetOutcome::None;  // how the last meeting ended
  uint16_t yearsAgo = 0;                 // in-game years since
  Temperament temperament = Temperament::Proud;
  Offer offer = Offer::Spar;
  int level = 0;
  bool dares = false;
  Danger danger = Danger::Dangerous;
  uint8_t chance = 0;   // percent to win a spar or a challenge
  uint16_t damage = 0;  // HP lost when a challenge is lost
  bool talked = false;
  bool traded = false;      // bought something
  uint8_t levelsAdded = 0;  // 3 x earlier meetings (at most 6)
};

int meetingRegion(const GameState& state);  // the region whose map holds the waiting meeting, or -1
int waitingRegion(const GameState& state);  // a waiting fight's or meeting's region, or -1
Meeting pendingMeeting(const GameState& state, const Layout& layout);

enum class MeetChoice : uint8_t { Offer, Talk, WalkOn };
enum class TalkResult : uint8_t { None, Hint, Nothing, TooPoor };

struct MeetResult {
  bool refused = false;  // the answer was not available
  bool done = false;     // the meeting is over (the map is free again)
  bool fought = false;   // a spar or a challenge was played
  bool won = false;
  uint8_t roll = 0;
  uint16_t hpLost = 0;
  uint8_t thrownTo = kNoTile;
  Reward reward;
  StepResult tile;
  MeetOutcome outcome = MeetOutcome::None;
  TalkResult talk = TalkResult::None;
  Enemy hintEnemy = Enemy::Beast;
  Danger hintDanger = Danger::Dangerous;
  uint32_t talkPaid = 0;
  uint32_t talkCost = 0;  // what a greedy one asked when you were short
};

// Offer: play the spar or the challenge (a trade is the shop: stockFor / buy
// with kTraderTile). Talk: once, the meeting stays open. WalkOn: leave.
MeetResult meet(GameState& state, const Layout& layout, MeetChoice choice);
// Ring entry i of the cultivators you have met (false when empty), for the tests.
bool metCultivator(const GameState& state, uint8_t index, uint8_t& region, uint16_t& id, uint8_t& meets,
                   MeetOutcome& outcome);

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
  uint16_t price = 0;  // v0.2.2: a traveller's price per unit (0 = the list price)
  uint8_t left = 0;    // v0.2.2: units a traveller still sells (0 = no cap)
};
struct Stock {
  StockLine lines[kMerchantStockMax];
  uint8_t count = 0;
};
// Seeded by the tile and the region's regrowth cycle: reopening the shop shows
// the same stock; it restocks when the region regrows.
Stock stockFor(const GameState& state, const Layout& layout, uint8_t region, uint8_t tile);
// What a stock line costs (a recipe slip by the grade of the recipe it teaches).
uint16_t stockPrice(const GameState& state, inventory::Item item);
// v0.2.2: what a line costs per unit, a traveller's own price when it has one.
inline uint16_t linePrice(const GameState& state, const StockLine& line) {
  return line.price != 0 ? line.price : stockPrice(state, line.item);
}
// v0.2.1: herbs and ores can be bought `count` at a time; limited lines one.
bool buy(GameState& state, const Layout& layout, uint8_t region, uint8_t tile, uint8_t line, uint16_t count = 1);
// How many of an unlimited line the stones and the bag allow (0 for limited lines).
uint16_t affordable(const GameState& state, inventory::Item item, uint16_t unitPrice = 0);
// v0.2.2: affordable() for a stock line, within a traveller's cap.
inline uint16_t affordable(const GameState& state, const StockLine& line) {
  const uint16_t most = affordable(state, line.item, line.price);
  return line.left != 0 && line.left < most ? line.left : most;
}
bool sell(GameState& state, inventory::Item item);

}  // namespace explore
}  // namespace cultivation
