#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace alchemy {
namespace {

// contentSeed kinds 100-119: alchemy.
constexpr uint32_t kKindRecipe = 100;
constexpr uint32_t kKindQuality = 102;

constexpr uint8_t kRecipeAttempts = 64;

struct RecipeSpec {
  Pill pill;
  uint8_t parts;
  uint8_t lowRegion;
  uint8_t highRegion;
  uint8_t brewMinutes;
};
// Agreed with the user (v0.2.0). Each recipe's ingredients come from the
// regions open when its pill is first needed (region i opens at realm i + 1).
// A breakthrough pill's grade is its weakest part's, so the spirit pill (first
// needed at Nascent Soul's peak, when region 3 is the only spirit region open)
// uses region 3 alone, and the rare pill (Body Integration's peak) region 6.
static const RecipeSpec kSpecs[kRecipeCount] = {
    {Pill::QiGathering, 2, 0, 0, 30},        {Pill::TonicMinor, 2, 0, 0, 20},
    {Pill::BreakthroughCommon, 3, 0, 0, 40}, {Pill::FoundationBuilding, 3, 0, 0, 40},
    {Pill::CoreForming, 3, 0, 1, 50},        {Pill::TonicGreater, 3, 0, 2, 40},
    {Pill::SoulClearing, 3, 0, 2, 50},       {Pill::BreakthroughSpirit, 3, 3, 3, 60},
    {Pill::TonicFull, 3, 3, 5, 60},          {Pill::BreakthroughRare, 3, 6, 6, 60}};

static const char* const kPillKeys[kRecipeCount] = {"qi",         "tonic.minor",   "breakthrough", "foundation",
                                                    "core",       "tonic.greater", "soul",         "breakthrough",
                                                    "tonic.full", "breakthrough"};

static const uint8_t kBreakthroughBonus[static_cast<int>(Quality::Count)] = {10, 17, 25};
static const uint8_t kGradePillPoints[static_cast<int>(Quality::Count)] = {6, 8, 10};
static const uint16_t kSlipPrice[static_cast<int>(Grade::Count)] = {60, 150, 300};
// Highest target realm a breakthrough pill opens, by grade (agreed: each grade
// covers the realm after its first region opens).
static const uint8_t kPillMaxTarget[static_cast<int>(Grade::Count)] = {4, 7, 255};

uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void set32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void sortParts(Item* parts, const uint8_t n) {
  for (uint8_t i = 1; i < n; ++i) {
    for (uint8_t j = i; j > 0 && parts[j] < parts[j - 1]; --j) {
      const Item t = parts[j];
      parts[j] = parts[j - 1];
      parts[j - 1] = t;
    }
  }
}

// Parts two sorted lists have in common, counting repeats.
uint8_t common(const Item* a, const uint8_t na, const Item* b, const uint8_t nb) {
  uint8_t i = 0;
  uint8_t j = 0;
  uint8_t n = 0;
  while (i < na && j < nb) {
    if (a[i] == b[j]) {
      ++n;
      ++i;
      ++j;
    } else if (a[i] < b[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  return n;
}

bool sameParts(const Recipe& r, const Item* parts, const uint8_t n) {
  return r.parts == n && common(r.part, r.parts, parts, n) == n;
}

uint8_t openRegions(const GameState& s) {
  return s.progress.realm < inventory::kRegions ? s.progress.realm : inventory::kRegions;
}

bool isHerbalist(const GameState& s) { return s.character.origin == Origin::HerbalistApprentice; }

}  // namespace

RecipeBook recipesFor(const uint32_t worldSeed) {
  RecipeBook book;
  for (uint8_t i = 0; i < kRecipeCount; ++i) {
    const RecipeSpec& spec = kSpecs[i];
    Recipe& r = book.recipes[i];
    r.pill = spec.pill;
    r.parts = spec.parts;
    r.brewMinutes = spec.brewMinutes;
    r.lowRegion = spec.lowRegion;
    r.highRegion = spec.highRegion;
    for (uint8_t attempt = 0; attempt < kRecipeAttempts; ++attempt) {
      Rng rng(contentSeed(worldSeed, kKindRecipe, (static_cast<uint32_t>(attempt) << 8) | i));
      const uint8_t span = static_cast<uint8_t>(spec.highRegion - spec.lowRegion + 1);
      bool herb = false;
      for (uint8_t p = 0; p < spec.parts; ++p) {
        // The first part comes from the newest region in range.
        const uint8_t region = p == 0 ? spec.highRegion : static_cast<uint8_t>(spec.lowRegion + rng.below(span));
        const bool isHerb = rng.below(2) == 0;
        herb = herb || isHerb;
        r.part[p] = isHerb ? inventory::herbFor(region) : inventory::oreFor(region);
      }
      if (!herb) r.part[0] = inventory::herbFor(spec.highRegion);  // at least one herb
      sortParts(r.part, r.parts);
      bool clash = false;
      for (uint8_t k = 0; k < i && !clash; ++k) clash = sameParts(book.recipes[k], r.part, r.parts);
      if (!clash) break;  // after kRecipeAttempts the last try stands (never seen in the host tests)
    }
    uint8_t weakest = 255;
    for (uint8_t p = 0; p < r.parts; ++p) {
      const uint8_t region = inventory::regionOf(r.part[p]);
      if (region < weakest) weakest = region;
    }
    r.grade = inventory::gradeOfRegion(weakest);
  }
  return book;
}

RecipeBook recipesOf(const GameState& s) { return recipesFor(worldSeedOf(s)); }

// --- Known recipes ------------------------------------------------------------------

bool known(const GameState& s, const uint8_t recipe) {
  if (recipe >= kRecipeCount) return false;
  const uint16_t bits = static_cast<uint16_t>(s.flagsAndCodex[0] | (s.flagsAndCodex[1] << 8));
  return (bits & (1u << recipe)) != 0;
}

void learn(GameState& s, const uint8_t recipe) {
  if (recipe >= kRecipeCount) return;
  uint16_t bits = static_cast<uint16_t>(s.flagsAndCodex[0] | (s.flagsAndCodex[1] << 8));
  bits = static_cast<uint16_t>(bits | (1u << recipe));
  s.flagsAndCodex[0] = static_cast<uint8_t>(bits);
  s.flagsAndCodex[1] = static_cast<uint8_t>(bits >> 8);
}

uint8_t knownCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kRecipeCount; ++i) n = static_cast<uint8_t>(n + (known(s, i) ? 1 : 0));
  return n;
}

int slipRecipe(const GameState& s) {
  const uint8_t open = openRegions(s);
  for (uint8_t i = 0; i < kRecipeCount; ++i) {
    if (!known(s, i) && kSpecs[i].highRegion < open) return i;
  }
  return -1;
}

uint16_t slipPrice(const RecipeBook& book, const uint8_t recipe) {
  if (recipe >= kRecipeCount) return 0;
  return kSlipPrice[static_cast<int>(book.recipes[recipe].grade)];
}

int learnSlip(GameState& s) {
  const int r = slipRecipe(s);
  if (r >= 0) learn(s, static_cast<uint8_t>(r));
  return r;
}

bool grantStartingRecipe(GameState& s) {
  if ((s.flagsAndCodex[4] & 0x01) != 0) return false;
  s.flagsAndCodex[4] = static_cast<uint8_t>(s.flagsAndCodex[4] | 0x01);
  if (!isHerbalist(s)) return false;
  const bool wasKnown = known(s, static_cast<uint8_t>(Pill::QiGathering));
  learn(s, static_cast<uint8_t>(Pill::QiGathering));
  return !wasKnown;
}

// --- The cauldron ----------------------------------------------------------------------

bool brewing(const GameState& s) { return s.timers[0] != 0 && s.timers[0] <= kRecipeCount; }

int brewingRecipe(const GameState& s) { return brewing(s) ? s.timers[0] - 1 : -1; }

uint32_t brewMinutesLeft(const GameState& s) {
  if (!brewing(s)) return 0;
  const uint32_t need = static_cast<uint32_t>(kSpecs[s.timers[0] - 1].brewMinutes) * 60u;
  const uint32_t start = get32(s.timers.data() + 2);
  const uint32_t now = s.progress.gameReadingSeconds;
  const uint32_t done = now > start ? now - start : 0;
  return done >= need ? 0 : (need - done + 59u) / 60u;
}

bool ready(const GameState& s) { return brewing(s) && brewMinutesLeft(s) == 0; }

uint16_t batchesHeld(const GameState& s, const Recipe& r) {
  uint16_t batches = inventory::kMaxCount;
  for (uint8_t p = 0; p < r.parts;) {
    uint8_t same = 1;
    while (p + same < r.parts && r.part[p + same] == r.part[p]) ++same;
    const uint16_t n = static_cast<uint16_t>(inventory::count(s, r.part[p]) / same);
    if (n < batches) batches = n;
    p = static_cast<uint8_t>(p + same);
  }
  return r.parts == 0 ? 0 : batches;
}

namespace {
void startBrew(GameState& s, const uint8_t recipe) {
  s.timers[0] = static_cast<uint8_t>(recipe + 1);
  set32(s.timers.data() + 2, s.progress.gameReadingSeconds);
  const uint16_t brews = static_cast<uint16_t>(s.timers[6] | (s.timers[7] << 8));
  const uint16_t next = static_cast<uint16_t>(brews + 1);
  s.timers[6] = static_cast<uint8_t>(next);
  s.timers[7] = static_cast<uint8_t>(next >> 8);
}

bool holds(const GameState& s, const Item* parts, const uint8_t n) {
  for (uint8_t p = 0; p < n;) {
    if (!inventory::isIngredient(parts[p])) return false;
    uint8_t same = 1;
    while (p + same < n && parts[p + same] == parts[p]) ++same;
    if (inventory::count(s, parts[p]) < same) return false;
    p = static_cast<uint8_t>(p + same);
  }
  return true;
}

void useUp(GameState& s, const Item* parts, const uint8_t n) {
  for (uint8_t p = 0; p < n; ++p) inventory::remove(s, parts[p]);
}
}  // namespace

bool canBrew(const GameState& s, const uint8_t recipe) {
  if (recipe >= kRecipeCount || !known(s, recipe) || brewing(s)) return false;
  const Recipe r = recipesOf(s).recipes[recipe];
  return holds(s, r.part, r.parts);
}

bool brew(GameState& s, const uint8_t recipe) {
  if (!canBrew(s, recipe)) return false;
  const Recipe r = recipesOf(s).recipes[recipe];
  useUp(s, r.part, r.parts);
  startBrew(s, recipe);
  return true;
}

Experiment experiment(GameState& s, const Item* in, const uint8_t count) {
  Experiment e;
  if (count < 2 || count > kMaxParts || brewing(s)) return e;
  Item parts[kMaxParts] = {};
  for (uint8_t i = 0; i < count; ++i) parts[i] = in[i];
  sortParts(parts, count);
  if (!holds(s, parts, count)) return e;
  e.valid = true;
  e.parts = count;
  useUp(s, parts, count);
  const RecipeBook book = recipesOf(s);
  for (uint8_t i = 0; i < kRecipeCount; ++i) {
    if (!sameParts(book.recipes[i], parts, count)) continue;
    e.recipe = i;
    e.learned = !known(s, i);
    learn(s, i);
    startBrew(s, i);
    return e;
  }
  bool anyUnknown = false;
  for (uint8_t i = 0; i < kRecipeCount; ++i) {
    const Recipe& r = book.recipes[i];
    if (r.parts != count || known(s, i)) continue;
    anyUnknown = true;
    const uint8_t m = common(r.part, r.parts, parts, count);
    if (m > e.matches) e.matches = m;
  }
  e.noneUnknown = !anyUnknown;
  return e;
}

uint8_t qualityBonus(const GameState& s, const Recipe& r) {
  uint8_t bonus = r.grade == Grade::Rare ? 20 : r.grade == Grade::Spirit ? 10 : 0;
  if (isHerbalist(s)) bonus = static_cast<uint8_t>(bonus + kHerbalistBonus);
  return bonus;
}

Collected collect(GameState& s) {
  Collected c;
  if (!ready(s)) return c;
  const uint8_t index = static_cast<uint8_t>(s.timers[0] - 1);
  const Recipe r = recipesOf(s).recipes[index];
  const uint16_t brews = static_cast<uint16_t>(s.timers[6] | (s.timers[7] << 8));
  Rng rng(contentSeed(worldSeedOf(s), kKindQuality, brews));
  c.roll = static_cast<uint8_t>(rng.below(100) + qualityBonus(s, r));
  c.quality = c.roll >= 85 ? Quality::High : c.roll >= 50 ? Quality::Medium : Quality::Low;
  c.ok = true;
  c.recipe = index;
  c.pill = r.pill;
  c.item = pillItem(r.pill, c.quality);
  const bool keepsQuality = inventory::isBreakthroughPill(c.item) || inventory::isGradePill(c.item);
  c.count = keepsQuality ? 1 : static_cast<uint16_t>(static_cast<uint8_t>(c.quality) + 1);
  inventory::add(s, c.item, c.count);
  s.timers[0] = 0;
  set32(s.timers.data() + 2, 0);
  return c;
}

// --- Breakthrough pills ------------------------------------------------------------

uint8_t breakthroughBonus(const Item pill) {
  if (!inventory::isBreakthroughPill(pill)) return 0;
  return kBreakthroughBonus[static_cast<int>(inventory::pillQuality(pill))];
}

bool pillFits(const Item pill, const uint8_t target) {
  if (!inventory::isBreakthroughPill(pill)) return false;
  return target >= 2 && target <= kPillMaxTarget[inventory::pillGroup(pill)];
}

namespace {
// Better = higher bonus, then a lower grade (keep the rarer pills).
bool better(const Item a, const Item b) {
  if (b == Item::None) return true;
  if (breakthroughBonus(a) != breakthroughBonus(b)) return breakthroughBonus(a) > breakthroughBonus(b);
  return inventory::pillGroup(a) < inventory::pillGroup(b);
}

Item bestHeld(const GameState& s, const uint8_t target) {
  Item best = Item::None;
  for (uint8_t g = 0; g < 3; ++g) {
    for (uint8_t q = 0; q < 3; ++q) {
      const Item p = inventory::breakthroughPill(static_cast<Grade>(g), static_cast<Quality>(q));
      if (inventory::count(s, p) > 0 && pillFits(p, target) && better(p, best)) best = p;
    }
  }
  return best;
}
}  // namespace

Item chosenPill(const GameState& s, const uint8_t target) {
  const uint8_t choice = s.timers[9];
  if (choice == kNoPillChoice) return Item::None;
  if (choice != 0) {
    const auto p = static_cast<Item>(choice);
    if (inventory::isBreakthroughPill(p) && inventory::count(s, p) > 0 && pillFits(p, target)) return p;
  }
  return bestHeld(s, target);
}

void cycleChoice(GameState& s, const uint8_t target, const bool pillIsOnlyKey) {
  // The options in order: every fitting pill held (by item id), then none.
  Item options[10];
  uint8_t n = 0;
  for (uint8_t i = static_cast<uint8_t>(Item::BreakthroughPill); i < static_cast<uint8_t>(Item::GradePill); ++i) {
    const auto p = static_cast<Item>(i);
    if (inventory::count(s, p) > 0 && pillFits(p, target)) options[n++] = p;
  }
  if (!pillIsOnlyKey) options[n++] = Item::None;
  if (n == 0) return;
  const Item now = chosenPill(s, target);
  uint8_t at = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (options[i] == now) at = i;
  }
  const Item next = options[(at + 1) % n];
  s.timers[9] = next == Item::None ? kNoPillChoice : static_cast<uint8_t>(next);
}

Item pillInUse(const GameState& s) {
  const auto p = static_cast<Item>(s.timers[8]);
  return inventory::isBreakthroughPill(p) ? p : Item::None;
}

Item takePill(GameState& s, const uint8_t target) {
  if (pillInUse(s) != Item::None) return pillInUse(s);
  const Item p = chosenPill(s, target);
  if (p == Item::None || !inventory::remove(s, p)) return Item::None;
  s.timers[8] = static_cast<uint8_t>(p);
  return p;
}

void clearPillInUse(GameState& s) { s.timers[8] = 0; }

// --- Grade pills and Qi Gathering pills ----------------------------------------------

int gradePillKindNow(const GameState& s) {
  const uint8_t realm = s.progress.realm;
  if (realm <= 1) return 0;
  if (realm == 2) return 1;
  if (realm == 3) return 2;
  return -1;
}

uint8_t gradePillPoints(const Item item) {
  return inventory::isGradePill(item) ? kGradePillPoints[static_cast<int>(inventory::pillQuality(item))] : 0;
}

uint8_t gradePillsTaken(const GameState& s) { return s.flagsAndCodex[2]; }
uint8_t gradePillScore(const GameState& s) { return s.flagsAndCodex[3] < 20 ? s.flagsAndCodex[3] : 20; }

void resetGradePills(GameState& s) {
  s.flagsAndCodex[2] = 0;
  s.flagsAndCodex[3] = 0;
}

uint32_t qiPillSecondsLeft(const GameState& s) { return get32(s.timers.data() + 10); }

UseError canUse(const GameState& s, const Item item) {
  if (inventory::count(s, item) == 0) return UseError::NotHeld;
  if (inventory::isGradePill(item)) {
    if (gradePillKindNow(s) != inventory::pillGroup(item)) return UseError::WrongRealm;
    if (gradePillsTaken(s) >= kMaxGradePills) return UseError::Enough;
    return UseError::None;
  }
  if (item == Item::QiPill) {
    return qiPillSecondsLeft(s) + kQiPillSeconds > kQiPillMaxSeconds ? UseError::QiFull : UseError::None;
  }
  return UseError::NotUsable;
}

UseError use(GameState& s, const Item item) {
  const UseError e = canUse(s, item);
  if (e != UseError::None) return e;
  inventory::remove(s, item);
  if (inventory::isGradePill(item)) {
    s.flagsAndCodex[2] = static_cast<uint8_t>(s.flagsAndCodex[2] + 1);
    s.flagsAndCodex[3] = static_cast<uint8_t>(s.flagsAndCodex[3] + gradePillPoints(item));
  } else {
    set32(s.timers.data() + 10, qiPillSecondsLeft(s) + kQiPillSeconds);
  }
  return UseError::None;
}

uint32_t spendQiPill(GameState& s, const uint32_t seconds) {
  const uint32_t left = qiPillSecondsLeft(s);
  const uint32_t boosted = seconds < left ? seconds : left;
  set32(s.timers.data() + 10, left - boosted);
  return boosted;
}

// --- Text helpers ------------------------------------------------------------------------

Item pillItem(const Pill pill, const Quality q) {
  switch (pill) {
    case Pill::QiGathering:
      return Item::QiPill;
    case Pill::TonicMinor:
      return Item::TonicMinor;
    case Pill::TonicGreater:
      return Item::TonicGreater;
    case Pill::TonicFull:
      return Item::TonicFull;
    case Pill::BreakthroughCommon:
      return inventory::breakthroughPill(Grade::Common, q);
    case Pill::BreakthroughSpirit:
      return inventory::breakthroughPill(Grade::Spirit, q);
    case Pill::BreakthroughRare:
      return inventory::breakthroughPill(Grade::Rare, q);
    case Pill::FoundationBuilding:
      return inventory::gradePill(0, q);
    case Pill::CoreForming:
      return inventory::gradePill(1, q);
    case Pill::SoulClearing:
      return inventory::gradePill(2, q);
    default:
      return Item::None;
  }
}

const char* pillKey(const Pill pill) {
  const int i = static_cast<int>(pill);
  return kPillKeys[i < kRecipeCount ? i : 0];
}

}  // namespace alchemy
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
