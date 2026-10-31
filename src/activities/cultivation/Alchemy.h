#pragma once

#include <cstdint>

#include "GameState.h"
#include "Inventory.h"

// Alchemy (v0.2.0, agreed with the user; DOC: Alchemy). Pure rules: no I/O,
// no UI, no clock.
//
// Every world has 10 recipes, generated from the world seed. A recipe names
// 2 or 3 specific ingredients (parts may repeat), drawn from the regions that
// are open when the player first needs its pill, with at least one part from
// the newest of those regions and at least one herb. No two recipes share a
// combination. Recipes are learned three ways: experimenting (pick 2-3 held
// ingredients; they are used up; a match teaches the recipe and starts
// brewing it, a miss says how many parts matched the closest unknown recipe
// with as many parts), recipe slips (merchants, ruins, caves, defeated
// cultivators), and the Herbalist's apprentice, who starts knowing the Qi
// Gathering recipe and brews 10 points better.
//
// One brew at a time, 20-60 reading minutes. Quality is rolled when the pill
// is collected: 0-99, +10 spirit / +20 rare ingredients (the recipe's weakest
// part), +10 Herbalist; under 50 Low, 50-84 Medium, 85+ High. Breakthrough
// and grade pills keep their quality; tonics and Qi pills come out 1/2/3 at a
// time by quality instead.
//
// Pills:
//   Breakthrough pill  a key for the bottleneck when its grade fits (common
//                      up to Nascent Soul, spirit up to Body Integration, rare
//                      any) and +10/+17/+25 chance; used up on every attempt.
//   Grade pill         +6/+8/+10 to the next grade score while that grade is
//                      the next one to be set; at most 2 count.
//   Qi Gathering pill  +25% Qi for the next 60 reading minutes (up to 180).
//   Tonics             the merchant tonics, brewed.
//
// Save (no new block):
//   timers[0]     brewing recipe + 1 (0 = cauldron empty)
//   timers[1]     v0.2.1: brews in this batch (0 or 1 = one; at most 3)
//   timers[2-5]   gameReadingSeconds when the brew began
//   timers[6-7]   brews begun so far (seeds the quality roll)
//   timers[8]     the breakthrough pill taken for the attempt under way (item id; 0 none)
//   timers[9]     breakthrough pill choice: 0 the best that fits, 0xFF none, else an item id
//   timers[10-13] Qi Gathering pill: reading seconds of boost left
//   timers[14-15] spare
//   flagsAndCodex[0-1]  known recipes (bit per recipe)
//   flagsAndCodex[2]    grade pills taken toward the next grade (0-2)
//   flagsAndCodex[3]    their grade score (0-20)
//   flagsAndCodex[4]    0x01 the Herbalist's starting recipe was given
namespace cultivation {
namespace alchemy {

using inventory::Grade;
using inventory::Item;
using inventory::Quality;

enum class Pill : uint8_t {
  QiGathering = 0,
  TonicMinor,
  BreakthroughCommon,
  FoundationBuilding,
  CoreForming,
  TonicGreater,
  SoulClearing,
  BreakthroughSpirit,
  TonicFull,
  BreakthroughRare,
  Count
};
constexpr uint8_t kRecipeCount = static_cast<uint8_t>(Pill::Count);
constexpr uint8_t kMaxParts = 3;
constexpr uint8_t kMaxGradePills = 2;
constexpr uint32_t kQiPillSeconds = 3600;      // one pill: 60 reading minutes
constexpr uint32_t kQiPillMaxSeconds = 10800;  // up to 180
constexpr uint32_t kQiPillBonusPermille = 250;
constexpr uint8_t kHerbalistBonus = 10;
constexpr uint8_t kNoPillChoice = 0xFF;

struct Recipe {
  Pill pill = Pill::QiGathering;
  uint8_t parts = 0;
  Item part[kMaxParts] = {};  // sorted by item id; repeats allowed
  uint8_t brewMinutes = 0;
  uint8_t lowRegion = 0;  // ingredients come from these regions
  uint8_t highRegion = 0;
  Grade grade = Grade::Common;  // the weakest part's grade
};

struct RecipeBook {
  Recipe recipes[kRecipeCount];
};
RecipeBook recipesFor(uint32_t worldSeed);
RecipeBook recipesOf(const GameState& state);

// --- Known recipes and recipe slips -------------------------------------------

bool known(const GameState& state, uint8_t recipe);
void learn(GameState& state, uint8_t recipe);
uint8_t knownCount(const GameState& state);
// The recipe a slip would teach now: the first unknown one (in table order)
// whose ingredients all come from open regions; -1 if none.
int slipRecipe(const GameState& state);
uint16_t slipPrice(const RecipeBook& book, uint8_t recipe);  // 60 / 150 / 300 by grade
// Teaches slipRecipe(); returns it, or -1.
int learnSlip(GameState& state);
// Gives the Herbalist's apprentice the Qi Gathering recipe once (at creation,
// or on the first visit of a character made before alchemy existed).
bool grantStartingRecipe(GameState& state);

// --- The cauldron ----------------------------------------------------------------

bool brewing(const GameState& state);
int brewingRecipe(const GameState& state);  // -1 when the cauldron is empty
uint32_t brewMinutesLeft(const GameState& state);
bool ready(const GameState& state);
// How many brews of a known recipe the bag holds the parts for.
uint16_t batchesHeld(const GameState& state, const Recipe& recipe);
bool canBrew(const GameState& state, uint8_t recipe);
// v0.2.1 (user): 1-3 brews of the same recipe in one go: the parts times N,
// the time x1, x1.5, x2; one quality roll for the batch.
constexpr uint8_t kMaxBatch = 3;
uint8_t maxBatch(const GameState& state, uint8_t recipe);  // what the bag holds parts for, at most 3
uint32_t batchMinutes(const Recipe& recipe, uint8_t batch);
uint8_t brewingBatch(const GameState& state);
bool brew(GameState& state, uint8_t recipe, uint8_t batch = 1);  // uses the parts, starts the timer

struct Experiment {
  bool valid = false;        // the parts were held and the cauldron was empty
  int recipe = -1;           // the recipe the parts made, if any
  bool learned = false;      // it was unknown until now
  uint8_t parts = 0;         // how many parts were used
  uint8_t matches = 0;       // a miss: parts in common with the closest unknown recipe of as many parts
  bool noneUnknown = false;  // a miss: every recipe with that many parts is already known
};
// Uses the parts up. A match starts brewing it.
Experiment experiment(GameState& state, const Item* parts, uint8_t count);

struct Collected {
  bool ok = false;
  uint8_t recipe = 0;
  Pill pill = Pill::QiGathering;
  Item item = Item::None;
  uint16_t count = 0;
  Quality quality = Quality::Low;
  uint8_t roll = 0;  // the quality roll with its bonuses
  // v0.2.1 (user): taking the pills out is a small event.
  uint8_t batch = 1;
  bool failed = false;    // 4% (2% for the Herbalist): only dross comes out
  bool extra = false;     // 8%: one more pill
  bool improved = false;  // 6%: the quality one band higher
  uint16_t dross = 0;
  inventory::AddResult added;  // what fit in the bag (sold or lost past 9,999)
};
constexpr uint8_t kFailPercent = 4;
constexpr uint8_t kExtraPercent = 8;
constexpr uint8_t kImprovePercent = 6;
Collected collect(GameState& state);
uint8_t qualityBonus(const GameState& state, const Recipe& recipe);  // ingredient grade + Herbalist

// --- Breakthrough pills ------------------------------------------------------------

uint8_t breakthroughBonus(Item pill);  // +10 / +17 / +25; 0 for anything else
bool pillFits(Item pill, uint8_t targetRealm);
// The pill the next attempt would take: the choice if it is held and fits,
// otherwise the best held that fits (highest bonus, then the lowest grade);
// None for "no pill" or when none fits.
Item chosenPill(const GameState& state, uint8_t targetRealm);
// The next choice on the Breakthrough screen: each fitting pill held, then
// "no pill" (left out when the pill is the only key).
void cycleChoice(GameState& state, uint8_t targetRealm, bool pillIsOnlyKey);
// The attempt under way: the pill taken for it (from the bag), or None.
Item pillInUse(const GameState& state);
// Moves chosenPill() from the bag into the attempt (trials and the roll).
Item takePill(GameState& state, uint8_t targetRealm);
void clearPillInUse(GameState& state);

// --- Grade pills and Qi Gathering pills ----------------------------------------------

enum class UseError : uint8_t { None, NotHeld, WrongRealm, Enough, QiFull, NotUsable };
// The grade pill kind (0-2) the current realm can take: 0 before Foundation
// Establishment, 1 in it, 2 in Core Formation; -1 later.
int gradePillKindNow(const GameState& state);
UseError canUse(const GameState& state, Item item);
UseError use(GameState& state, Item item);
uint8_t gradePillScore(const GameState& state);  // 0-20, added to the next grade score
uint8_t gradePillsTaken(const GameState& state);
void resetGradePills(GameState& state);  // after the grade is set
uint8_t gradePillPoints(Item item);      // +6 / +8 / +10
uint32_t qiPillSecondsLeft(const GameState& state);
// Absorb: of `seconds` read, how many were boosted (and spends them).
uint32_t spendQiPill(GameState& state, uint32_t seconds);

// --- Text helpers ---------------------------------------------------------------------

Item pillItem(Pill pill, Quality quality);  // the bag item a collected pill becomes
const char* pillKey(Pill pill);             // "qi", "tonic.minor" ... for grammar list keys

}  // namespace alchemy
}  // namespace cultivation
