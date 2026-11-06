#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Codex.h"
#include "Daily.h"
#include "Daos.h"
#include "Exploration.h"
#include "Lineage.h"
#include "Meridians.h"
#include "Rivals.h"
#include "Rng.h"
#include "Scrolls.h"
#include "Sects.h"
#include "Simulation.h"
#include "Trials.h"

namespace cultivation {
namespace explore {
namespace {

// contentSeed kinds 80-99: exploration.
constexpr uint32_t kKindLayout = 80;
constexpr uint32_t kKindTileReward = 82;
constexpr uint32_t kKindStock = 83;
constexpr uint32_t kKindEncounter = 84;
constexpr uint32_t kKindBeastLevel = 85;
constexpr uint32_t kKindFightRoll = 86;
constexpr uint32_t kKindClearReward = 87;
constexpr uint32_t kKindEnemy = 88;         // v0.2.0: lair and guardian kinds
constexpr uint32_t kKindMeetRoll = 94;      // v0.2.2: does a cultivator walk the road
constexpr uint32_t kKindMeetWho = 95;       // ... who: name, temperament, level (and the id of a new one)
constexpr uint32_t kKindMeetStock = 96;     // ... what a traveller sells
constexpr uint32_t kKindNemesisFind = 126;  // v0.2.8: a nemesis finds you (Rivals.cpp has 122-127)
constexpr uint32_t kKindMemento = 130;      // v0.3.0: a memento turns up (cleared region, cave, ruin)

constexpr size_t kHeaderBytes = 8;
constexpr size_t kRegionBytes = 19;
static_assert(kHeaderBytes + kRegionBytes * kRegionCount <= sizeof(GameState::exploration), "exploration block");

constexpr uint8_t kFlagCleared = 0x01;
constexpr uint8_t kFlagGuardian = 0x02;

constexpr uint8_t kGenerateAttempts = 64;

static const char* const kThemes[kRegionCount] = {"marsh", "wastes",  "forest",      "gorge", "snow",
                                                  "delta", "volcano", "battlefield", "salt",  "ridge"};
// Enemy kinds per theme, in percent: beast, bandit, cultivator, spirit
// (agreed with the user, v0.2.0). Lairs are dens or hideouts in the
// beast-to-bandit ratio.
static const uint8_t kEnemyWeights[kRegionCount][static_cast<int>(Enemy::Count)] = {
    {70, 15, 5, 10},   // marsh
    {50, 35, 10, 5},   // wastes
    {70, 15, 10, 5},   // forest
    {40, 45, 10, 5},   // gorge
    {60, 10, 25, 5},   // snow
    {45, 40, 10, 5},   // delta
    {55, 15, 25, 5},   // volcano
    {25, 20, 10, 45},  // battlefield
    {35, 20, 5, 40},   // salt
    {35, 10, 45, 10}   // ridge
};

Enemy pickEnemy(const uint8_t region, const uint32_t roll100) {
  const uint8_t* w = kEnemyWeights[region < kRegionCount ? region : 0];
  uint32_t acc = 0;
  for (uint8_t k = 0; k < static_cast<uint8_t>(Enemy::Count); ++k) {
    acc += w[k];
    if (roll100 < acc) return static_cast<Enemy>(k);
  }
  return Enemy::Beast;
}

// Lost-fight damage in percent of max HP by danger word (agreed, v0.1.10).
static const uint8_t kLossPercent[static_cast<int>(Danger::Count)] = {10, 15, 25, 35, 45, 60};

uint8_t* regionBytes(GameState& s, const uint8_t region) {
  return s.exploration.data() + kHeaderBytes + kRegionBytes * (region < kRegionCount ? region : 0);
}
const uint8_t* regionBytes(const GameState& s, const uint8_t region) {
  return s.exploration.data() + kHeaderBytes + kRegionBytes * (region < kRegionCount ? region : 0);
}

bool bit(const uint8_t* bits, const uint8_t i) { return (bits[i / 8] & (1u << (i % 8))) != 0; }
void setBit(uint8_t* bits, const uint8_t i) { bits[i / 8] = static_cast<uint8_t>(bits[i / 8] | (1u << (i % 8))); }
void clearBit(uint8_t* bits, const uint8_t i) { bits[i / 8] = static_cast<uint8_t>(bits[i / 8] & ~(1u << (i % 8))); }

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void set16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint32_t counter(const GameState& s) {
  const uint8_t* p = s.exploration.data();
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void setCounter(GameState& s, const uint32_t v) {
  uint8_t* p = s.exploration.data();
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void setPending(GameState& s, const uint8_t region, const FightKind kind) {
  s.exploration[4] = kind == FightKind::None ? 0 : static_cast<uint8_t>(region + 1);
  s.exploration[5] = static_cast<uint8_t>(kind);
  if (kind != FightKind::Beast && kind != FightKind::Meeting) s.exploration[7] = 0;
}

// --- Returning foes (v0.2.1): memory bytes 0-23, 6 bytes each ---------------------
uint8_t* foeBytes(GameState& s, const uint8_t i) { return s.memory.data() + 6 * i; }
const uint8_t* foeBytes(const GameState& s, const uint8_t i) { return s.memory.data() + 6 * i; }

void writeFoe(GameState& s, const uint8_t i, const Foe& f) {
  uint8_t* b = foeBytes(s, i);
  b[0] = static_cast<uint8_t>(f.region + 1);
  b[1] = static_cast<uint8_t>((f.returns & 0x03) | (f.paid ? 0x10 : 0));
  b[2] = static_cast<uint8_t>(f.seed);
  b[3] = static_cast<uint8_t>(f.seed >> 8);
  b[4] = static_cast<uint8_t>(f.seed >> 16);
  b[5] = static_cast<uint8_t>(f.seed >> 24);
}

void forgetFoe(GameState& s, const uint8_t i) {
  for (uint8_t k = i; k + 1 < kFoeRing; ++k) {
    for (uint8_t j = 0; j < 6; ++j) foeBytes(s, k)[j] = foeBytes(s, static_cast<uint8_t>(k + 1))[j];
  }
  for (uint8_t j = 0; j < 6; ++j) foeBytes(s, kFoeRing - 1)[j] = 0;
}

// A new foe goes at the end; with the ring full the oldest is forgotten.
void addFoe(GameState& s, const Foe& f) {
  uint8_t i = 0;
  while (i < kFoeRing && foeBytes(s, i)[0] != 0) ++i;
  if (i == kFoeRing) {
    forgetFoe(s, 0);
    i = kFoeRing - 1;
  }
  writeFoe(s, i, f);
}

int waitingFoeIn(const GameState& s, const uint8_t region) {
  for (uint8_t i = 0; i < kFoeRing; ++i) {
    if (foeBytes(s, i)[0] == region + 1) return i;
  }
  return -1;
}

// --- Cultivators met (v0.2.2): memory bytes 32-47, 4 bytes each ---------------------------
constexpr uint8_t kMeetBase = 32;
constexpr uint8_t kMeetFlagTalked = 0x02;
constexpr uint8_t kMeetFlagSlip = 0x04;
constexpr uint8_t kMeetFlagTreasure = 0x08;
constexpr uint8_t kMeetFlagBought = 0x10;
constexpr uint8_t kMeetBoughtShift = 5;  // bits 5-7: the traveller's ingredient bought (0-5)
constexpr uint8_t kMeetFlags = 0xFE;     // everything about the waiting meeting (bit 0x01 was the step cap)

struct Cult {
  uint8_t region = 0;
  uint8_t meets = 0;  // 1-3
  uint16_t id = 0;    // 10 bits
  MeetOutcome outcome = MeetOutcome::None;
  uint16_t years = 0;  // age in years at the last meeting
};

uint8_t* cultBytes(GameState& s, const uint8_t i) { return s.memory.data() + kMeetBase + 4 * i; }
const uint8_t* cultBytes(const GameState& s, const uint8_t i) { return s.memory.data() + kMeetBase + 4 * i; }

bool readCult(const GameState& s, const uint8_t i, Cult& out) {
  if (i >= kMeetRing) return false;
  const uint8_t* b = cultBytes(s, i);
  const uint8_t region = b[1] & 0x0F;
  if (region == 0 || region > kRegionCount) return false;
  out.region = static_cast<uint8_t>(region - 1);
  out.meets = static_cast<uint8_t>((b[1] >> 4) & 0x03);
  out.id = static_cast<uint16_t>(b[0] | (((b[1] >> 6) & 0x03) << 8));
  out.years = static_cast<uint16_t>(b[2] | ((b[3] & 0x1F) << 8));
  out.outcome = static_cast<MeetOutcome>(b[3] >> 5);
  return true;
}

void writeCult(GameState& s, const uint8_t i, const Cult& c) {
  uint8_t* b = cultBytes(s, i);
  b[0] = static_cast<uint8_t>(c.id);
  b[1] = static_cast<uint8_t>(((c.region + 1) & 0x0F) | ((c.meets & 0x03) << 4) | (((c.id >> 8) & 0x03) << 6));
  const uint16_t years = c.years > 0x1FFF ? 0x1FFF : c.years;
  b[2] = static_cast<uint8_t>(years);
  b[3] = static_cast<uint8_t>(((years >> 8) & 0x1F) | (static_cast<uint8_t>(c.outcome) << 5));
}

void forgetCult(GameState& s, const uint8_t i) {
  for (uint8_t k = i; k + 1 < kMeetRing; ++k) {
    for (uint8_t j = 0; j < 4; ++j) cultBytes(s, k)[j] = cultBytes(s, static_cast<uint8_t>(k + 1))[j];
  }
  for (uint8_t j = 0; j < 4; ++j) cultBytes(s, kMeetRing - 1)[j] = 0;
}

// A new cultivator goes at the end; with the ring full the oldest is forgotten.
void addCult(GameState& s, const Cult& c) {
  uint8_t i = 0;
  Cult tmp;
  while (i < kMeetRing && readCult(s, i, tmp)) ++i;
  if (i == kMeetRing) {
    forgetCult(s, 0);
    i = kMeetRing - 1;
  }
  writeCult(s, i, c);
}

// One of the cultivators met in `region` (picked by `pick`), or -1.
int waitingCultIn(const GameState& s, const uint8_t region, const uint32_t pick) {
  Cult c;
  int found[kMeetRing];
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMeetRing; ++i) {
    if (readCult(s, i, c) && c.region == region) found[n++] = i;
  }
  return n == 0 ? -1 : found[pick % n];
}

// Reading time in 10-minute units, + 1 so that 0 means "not set".
uint16_t stampNow(const GameState& s) {
  const uint32_t units = s.progress.gameReadingSeconds / 600u + 1u;
  return static_cast<uint16_t>(units < 0xFFFFu ? units : 0xFFFFu);
}
uint32_t unitsSince(const GameState& s, const uint16_t stamp) {
  const uint16_t now = stampNow(s);
  return now > stamp ? static_cast<uint32_t>(now - stamp) : 0u;
}

void setPosition(GameState& s, const uint8_t region, const uint8_t tile) { regionBytes(s, region)[12] = tile + 1; }
void chart(GameState& s, const uint8_t region, const uint8_t tile) { setBit(regionBytes(s, region), tile); }
void markUsed(GameState& s, const uint8_t region, const uint8_t tile) { setBit(regionBytes(s, region) + 5, tile); }
void startRegrow(GameState& s, const uint8_t region) {
  uint8_t* r = regionBytes(s, region);
  if (get16(r + 13) == 0) set16(r + 13, stampNow(s));
}

uint8_t floodCount(const Layout& l, uint8_t* dist) {
  uint8_t queue[kTiles];
  for (uint8_t i = 0; i < kTiles; ++i) dist[i] = 0xFF;
  uint8_t head = 0;
  uint8_t tail = 0;
  queue[tail++] = l.entrance;
  dist[l.entrance] = 0;
  while (head < tail) {
    const uint8_t t = queue[head++];
    uint8_t n[4];
    const uint8_t c = neighbours(t, n);
    for (uint8_t i = 0; i < c; ++i) {
      if (l.tiles[n[i]] == Tile::Blocked || dist[n[i]] != 0xFF) continue;
      dist[n[i]] = static_cast<uint8_t>(dist[t] + 1);
      queue[tail++] = n[i];
    }
  }
  return tail;
}

uint32_t ceilPercent(const uint32_t value, const uint32_t percent) { return (value * percent + 99u) / 100u; }

bool trivialYield(const GameState& s, const uint8_t region) {
  return regionDanger(s, region) == Danger::Trivial;  // DOC: Trivial regions give half yield
}
uint32_t halve(const uint32_t v, const bool half) { return half ? (v / 2 > 0 ? v / 2 : (v > 0 ? 1 : 0)) : v; }

// v0.3.2: fight stones in a region at war, +50%.
uint32_t warStones(const GameState& s, const uint8_t region, const uint32_t stones) {
  return stones * sects::fightStonesPermille(s, region) / 1000u;
}

void give(GameState& s, Reward& r, const inventory::Item item, const uint16_t n) {
  if (item == inventory::Item::None || n == 0) return;
  const inventory::AddResult a = inventory::addOrSell(s, item, n);
  r.soldStones += a.soldStones;
  r.lost = static_cast<uint16_t>(r.lost + a.lost);
  if (r.item == inventory::Item::None) {
    r.item = item;
    r.itemCount = n;
  } else if (r.item2 == inventory::Item::None) {
    r.item2 = item;
    r.item2Count = n;
  } else {
    r.item3 = item;
    r.item3Count = n;
  }
}

// v0.3.0: a memento may turn up with a find (Codex.h has the chances).
void mementoChance(GameState& s, Reward& r, const codex::Source source, const uint32_t index) {
  const codex::Find f = codex::roll(s, source, contentSeed(worldSeedOf(s), kKindMemento, index));
  if (f.memento == codex::kNone) return;
  r.memento = f.memento;
  r.mementoDuplicate = f.duplicate;
  r.mementoStones = f.stones;
}

inventory::Item treasure(Rng& rng) {
  return rng.below(2) == 0 ? inventory::Item::ThunderTalisman : inventory::Item::CalmIncense;
}

// v0.3.3: what ending a condition yourself pays (a camp, a tide, an inheritance).
void worldReward(GameState& s, const sects::Influence& w, const uint8_t region, Reward& r, Rng& rng) {
  if (w.stones > 0) {
    r.stones += w.stones;
    s.progress.stones += w.stones;
  }
  if (w.tonic) give(s, r, inventory::Item::TonicGreater, 1);
  if (w.herbs > 0) give(s, r, inventory::herbFor(region), w.herbs);
  if (w.ores > 0) give(s, r, inventory::oreFor(region), w.ores);
  if (w.treasure) give(s, r, treasure(rng), 1);
}

void checkClear(GameState& s, const Layout& l, const uint8_t region, StepResult& r) {
  uint8_t* b = regionBytes(s, region);
  if ((b[11] & kFlagCleared) != 0 || (b[11] & kFlagGuardian) == 0) return;
  if (chartedCount(s, region) < l.walkable) return;
  b[11] = static_cast<uint8_t>(b[11] | kFlagCleared);
  set16(b + 15, stampNow(s));
  Rng rng(contentSeed(worldSeedOf(s), kKindClearReward, (static_cast<uint32_t>(region) << 8) | b[10]));
  r.clearedNow = true;
  r.clearReward.stones = 100u * (region + 1u);
  s.progress.stones += r.clearReward.stones;
  give(s, r.clearReward, inventory::Item::TonicGreater, 1);
  give(s, r.clearReward, treasure(rng), 1);
  mementoChance(s, r.clearReward, codex::Source::RegionCleared, (static_cast<uint32_t>(region) << 8) | b[10]);
}

// The content of the tile the player stands on. Caller checks the clear.
void resolveTile(GameState& s, const Layout& l, const uint8_t region, StepResult& r) {
  const uint8_t tile = position(s, l, region);
  const Tile t = l.tiles[tile];
  r.outcome = TileOutcome::Nothing;
  if (t == Tile::Merchant) {
    r.outcome = TileOutcome::Merchant;
    return;
  }
  if (t == Tile::Path || t == Tile::Lair || t == Tile::Guardian || used(s, region, tile)) return;
  Rng rng(contentSeed(worldSeedOf(s), kKindTileReward, mix32(counter(s), tile)));
  const bool half = trivialYield(s, region);
  ProgressBlock& p = s.progress;
  switch (t) {
    case Tile::Herb:  // v0.2.4: Liver points add one herb each; v0.2.6: and the omen "Herbs grow thick"
      // v0.3.2: a blight halves what the tile gives.
      give(s, r.reward, inventory::herbFor(region),
           static_cast<uint16_t>(halve(halve(rng.range(1, 3), half) + meridians::extraHerbs(s) + daily::extraHerbs(s),
                                       sects::herbsHalved(s, region))));
      daily::note(s, daily::Task::Gather, r.reward.itemCount);
      r.outcome = TileOutcome::Gathered;
      markUsed(s, region, tile);
      startRegrow(s, region);
      break;
    case Tile::Ore:
      give(s, r.reward, inventory::oreFor(region),
           static_cast<uint16_t>(halve(rng.range(1, 2), half) + daily::extraOre(s)));  // v0.2.6: "Veins run shallow"
      daily::note(s, daily::Task::Gather, r.reward.itemCount);
      r.outcome = TileOutcome::Gathered;
      markUsed(s, region, tile);
      startRegrow(s, region);
      break;
    case Tile::Ruin: {
      // v0.2.6: the Dao of Time and the omen "Old ruins" add to the search.
      // v0.3.0: and the Fated perk, +3 a rank.
      // v0.3.2: and ruins a landslide exposed, +15.
      const uint32_t luck = 50u + originBonus(s.character.origin).eventLuck + daos::ruinBonus(s) + daily::ruinBonus(s) +
                            lineage::fatedLuck(s) + sects::ruinBonus(s, region);
      if (rng.below(100) < luck) {
        // v0.2.0: one won ruin in five holds a recipe slip instead of stones (v0.2.6: Fate adds).
        if (alchemy::slipRecipe(s) >= 0 && rng.below(100) < 20u + daos::prizeChanceBonus(s)) {
          r.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
        } else {
          r.reward.stones = halve(rng.range(20, 60) * (region + 1u), half);
          p.stones += r.reward.stones;
        }
        r.outcome = TileOutcome::RuinWon;
        r.world = sects::searchWon(s, region);  // v0.3.3: an inheritance, yours before the sects get it
        worldReward(s, r.world, region, r.reward, rng);
        mementoChance(s, r.reward, codex::Source::Ruin,
                      0x10000u | (static_cast<uint32_t>(region) << 8) |
                          (static_cast<uint32_t>(reshapeCount(s, region)) << 6) | tile);
      } else {
        const uint32_t loss = ceilPercent(meridians::damageHpBase(s), 10);
        r.hpLost = static_cast<uint16_t>(loss < p.hp ? loss : p.hp);
        p.hp = static_cast<uint16_t>(p.hp - r.hpLost);
        r.outcome = TileOutcome::RuinTrapped;
      }
      markUsed(s, region, tile);
      break;
    }
    case Tile::Cave:
      if (alchemy::slipRecipe(s) >= 0 && rng.below(100) < 15u + daos::prizeChanceBonus(s)) {  // v0.2.0: a recipe slip
        r.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
      } else if (rng.below(2) == 0) {
        give(s, r.reward, treasure(rng), 1);
      } else {
        r.reward.stones = halve(rng.range(10, 30) * (region + 1u), half);
        p.stones += r.reward.stones;
        give(s, r.reward, inventory::herbFor(region), 1);
      }
      r.outcome = TileOutcome::CaveFound;
      r.world = sects::searchWon(s, region);  // v0.3.3: an inheritance in a cave counts too
      worldReward(s, r.world, region, r.reward, rng);
      mementoChance(s, r.reward, codex::Source::Cave,
                    0x20000u | (static_cast<uint32_t>(region) << 8) |
                        (static_cast<uint32_t>(reshapeCount(s, region)) << 6) | tile);
      markUsed(s, region, tile);
      break;
    case Tile::Quiet: {
      if (p.hp >= p.hpMax) {
        r.outcome = TileOutcome::RestedFull;
        break;
      }
      const uint32_t heal = ceilPercent(p.hpMax, 50);
      const uint32_t room = static_cast<uint32_t>(p.hpMax - p.hp);
      r.hpHealed = static_cast<uint16_t>(heal < room ? heal : room);
      p.hp = static_cast<uint16_t>(p.hp + r.hpHealed);
      r.outcome = TileOutcome::Rested;
      markUsed(s, region, tile);
      startRegrow(s, region);
      break;
    }
    default:
      break;
  }
}

// A won fight's stones by enemy kind (agreed, v0.2.0): bandits double, cultivators 1.5x.
uint32_t kindStones(const Enemy e, const uint32_t stones) {
  if (e == Enemy::Bandit) return stones * 2u;
  if (e == Enemy::Cultivator) return stones * 3u / 2u;
  return stones;
}

// After a lost fight (or slipping away from a lair or guardian): onto a
// charted walkable neighbour, picked by `rng`; stays put if there is none.
uint8_t throwBack(GameState& s, const Layout& l, const uint8_t region, Rng& rng) {
  const uint8_t here = position(s, l, region);
  uint8_t n[4];
  uint8_t options[4];
  uint8_t count = 0;
  const uint8_t c = neighbours(here, n);
  for (uint8_t i = 0; i < c; ++i) {
    if (walkable(l, n[i]) && charted(s, region, n[i])) options[count++] = n[i];
  }
  if (count == 0) return here;
  const uint8_t to = options[rng.below(count)];
  setPosition(s, region, to);
  return to;
}

// v0.2.9: a collapse ends at the region's entrance (the way in is always charted).
uint8_t carryToEntrance(GameState& s, const Layout& l, const uint8_t region) {
  setPosition(s, region, l.entrance);
  chart(s, region, l.entrance);
  return l.entrance;
}

uint8_t merchantOrdinal(const Layout& l, const uint8_t tile) {
  uint8_t ordinal = 0;
  for (uint8_t i = 0; i < tile && i < kTiles; ++i) {
    if (l.tiles[i] == Tile::Merchant) ++ordinal;
  }
  return ordinal;
}

}  // namespace

// --- Layout ---------------------------------------------------------------------------

uint8_t neighbours(const uint8_t tile, uint8_t out[4]) {
  uint8_t c = 0;
  const uint8_t r = rowOf(tile);
  const uint8_t col = colOf(tile);
  if (r > 0) out[c++] = static_cast<uint8_t>(tile - kGrid);
  if (col + 1 < kGrid) out[c++] = static_cast<uint8_t>(tile + 1);
  if (r + 1 < kGrid) out[c++] = static_cast<uint8_t>(tile + kGrid);
  if (col > 0) out[c++] = static_cast<uint8_t>(tile - 1);
  return c;
}

bool adjacent(const uint8_t a, const uint8_t b) {
  if (a >= kTiles || b >= kTiles) return false;
  const int dr = static_cast<int>(rowOf(a)) - rowOf(b);
  const int dc = static_cast<int>(colOf(a)) - colOf(b);
  return (dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1));
}

bool regrows(const Tile t) { return t == Tile::Herb || t == Tile::Ore || t == Tile::Quiet || t == Tile::Lair; }
bool unique(const Tile t) { return t == Tile::Ruin || t == Tile::Cave || t == Tile::Guardian; }

Layout generate(const uint32_t worldSeed, const uint8_t region, const uint8_t reshapeCount) {
  Layout l;
  Rng rng(contentSeed(worldSeed, kKindLayout, (static_cast<uint32_t>(region) << 8) | reshapeCount));
  l.encounterPercent = static_cast<uint8_t>(rng.range(5, 18));

  // Terrain: an entrance on the edge and 8-12 crags, every walkable tile
  // reachable from the entrance (otherwise the next roll is tried).
  uint8_t dist[kTiles];
  bool ok = false;
  for (uint8_t attempt = 0; attempt < kGenerateAttempts && !ok; ++attempt) {
    for (auto& t : l.tiles) t = Tile::Path;
    const uint32_t side = rng.below(4);
    const uint8_t pos = static_cast<uint8_t>(rng.below(kGrid));
    l.entrance = side == 0   ? pos
                 : side == 1 ? static_cast<uint8_t>(pos * kGrid + kGrid - 1)
                 : side == 2 ? static_cast<uint8_t>((kGrid - 1) * kGrid + pos)
                             : static_cast<uint8_t>(pos * kGrid);
    const uint8_t blocked = static_cast<uint8_t>(rng.range(8, 12));
    uint8_t placed = 0;
    while (placed < blocked) {
      const uint8_t t = static_cast<uint8_t>(rng.below(kTiles));
      if (t == l.entrance || l.tiles[t] == Tile::Blocked) continue;
      l.tiles[t] = Tile::Blocked;
      ++placed;
    }
    ok = floodCount(l, dist) == kTiles - blocked;
    l.walkable = static_cast<uint8_t>(kTiles - blocked);
  }
  if (!ok) {  // never seen in the host tests; an open field is still a valid map
    for (auto& t : l.tiles) t = Tile::Path;
    l.walkable = kTiles;
    floodCount(l, dist);
  }

  // The guardian waits on the walkable tile farthest from the entrance.
  uint8_t guardian = l.entrance;
  for (uint8_t i = 0; i < kTiles; ++i) {
    if (l.tiles[i] != Tile::Blocked && dist[i] != 0xFF && dist[i] > dist[guardian]) guardian = i;
  }
  l.tiles[guardian] = Tile::Guardian;
  l.guardianLevel = static_cast<uint8_t>(regionBase(region) + 6 + rng.below(5));

  // Content on the other walkable tiles, in a shuffled order (agreed mix).
  uint8_t spots[kTiles];
  uint8_t spotCount = 0;
  for (uint8_t i = 0; i < kTiles; ++i) {
    if (l.tiles[i] == Tile::Path && i != l.entrance) spots[spotCount++] = i;
  }
  for (uint8_t i = spotCount; i > 1; --i) {
    const uint8_t j = static_cast<uint8_t>(rng.below(i));
    const uint8_t tmp = spots[i - 1];
    spots[i - 1] = spots[j];
    spots[j] = tmp;
  }
  struct Mix {
    Tile tile;
    uint8_t lo;
    uint8_t hi;
  };
  static const Mix kMix[] = {{Tile::Herb, 3, 4}, {Tile::Ore, 3, 4},      {Tile::Ruin, 2, 2}, {Tile::Cave, 1, 2},
                             {Tile::Lair, 2, 3}, {Tile::Merchant, 1, 2}, {Tile::Quiet, 1, 2}};
  uint8_t next = 0;
  l.content = 1;  // the guardian
  for (const Mix& m : kMix) {
    const uint8_t n = static_cast<uint8_t>(rng.range(m.lo, m.hi));
    for (uint8_t k = 0; k < n && next < spotCount; ++k) {
      l.tiles[spots[next]] = m.tile;
      if (m.tile == Tile::Lair) l.lairBonus[spots[next]] = static_cast<uint8_t>(rng.below(5));
      ++next;
      ++l.content;
    }
  }
  if (l.content < 15 && next < spotCount) {  // the smallest mix is 14: one more herb
    l.tiles[spots[next++]] = Tile::Herb;
    ++l.content;
  }
  return l;
}

Layout layoutOf(const GameState& s, const uint8_t region) {
  return generate(worldSeedOf(s), region, reshapeCount(s, region));
}

// --- Regions ---------------------------------------------------------------------------

uint8_t unlockRealm(const uint8_t region) { return static_cast<uint8_t>(region + 1); }

bool unlocked(const GameState& s, const uint8_t region) {
  return region < kRegionCount && s.progress.realm >= unlockRealm(region);
}

uint8_t unlockedCount(const GameState& s) {
  uint8_t n = 0;
  while (n < kRegionCount && unlocked(s, n)) ++n;
  return n;
}

const char* themeKey(const uint8_t region) { return kThemes[region < kRegionCount ? region : 0]; }

bool charted(const GameState& s, const uint8_t region, const uint8_t tile) {
  return tile < kTiles && bit(regionBytes(s, region), tile);
}
bool used(const GameState& s, const uint8_t region, const uint8_t tile) {
  return tile < kTiles && bit(regionBytes(s, region) + 5, tile);
}
uint8_t chartedCount(const GameState& s, const uint8_t region) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kTiles; ++i) n = static_cast<uint8_t>(n + (charted(s, region, i) ? 1 : 0));
  return n;
}
uint8_t reshapeCount(const GameState& s, const uint8_t region) { return regionBytes(s, region)[10]; }

void chartAll(GameState& s, const uint8_t region) {
  if (region >= kRegionCount) return;
  const Layout l = layoutOf(s, region);
  for (uint8_t i = 0; i < kTiles; ++i) {
    if (walkable(l, i)) chart(s, region, i);
  }
}
bool cleared(const GameState& s, const uint8_t region) { return (regionBytes(s, region)[11] & kFlagCleared) != 0; }
bool guardianBeaten(const GameState& s, const uint8_t region) {
  return (regionBytes(s, region)[11] & kFlagGuardian) != 0;
}
uint8_t merchantCycle(const GameState& s, const uint8_t region) { return regionBytes(s, region)[17]; }

uint8_t position(const GameState& s, const Layout& l, const uint8_t region) {
  const uint8_t stored = regionBytes(s, region)[12];
  if (stored == 0 || stored > kTiles || !walkable(l, static_cast<uint8_t>(stored - 1))) return l.entrance;
  return static_cast<uint8_t>(stored - 1);
}

uint32_t regrowMinutesLeft(const GameState& s, const uint8_t region) {
  const uint16_t stamp = get16(regionBytes(s, region) + 13);
  if (stamp == 0) return 0;
  const uint32_t since = unitsSince(s, stamp);
  const uint32_t units = meridians::regrowUnits(s, kRegrowUnits);  // v0.2.4: Triple Burner
  return since >= units ? 0 : (units - since) * 10u;
}

uint32_t upheavalMinutesLeft(const GameState& s, const uint8_t region) {
  if (!cleared(s, region)) return 0;
  const uint16_t stamp = get16(regionBytes(s, region) + 15);
  const uint32_t since = unitsSince(s, stamp);
  return since >= kUpheavalUnits ? 0 : (kUpheavalUnits - since) * 10u;
}

uint8_t enter(GameState& s, const Layout& l, const uint8_t region) {
  rivals::setPlayerRegion(s, region);  // v0.2.8: nemeses roam toward it
  uint8_t* b = regionBytes(s, region);
  if (b[12] == 0 || b[12] > kTiles || !walkable(l, static_cast<uint8_t>(b[12] - 1))) {
    setPosition(s, region, l.entrance);
    chart(s, region, l.entrance);
  }
  return position(s, l, region);
}

// --- Combat ------------------------------------------------------------------------------

int playerLevel(const GameState& s, const bool techniquesDouble) {
  const ProgressBlock& p = s.progress;
  const RealmInfo& info = realmInfo(p.realm);
  int stage = info.layered ? p.stage : p.stage * 3;
  if ((p.progressFlags & ProgressBlock::kInGreatPerfection) != 0) stage = 9;
  if (stage > 9) stage = 9;
  int level = 10 * p.realm + stage;
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if ((sc.flags & scrolls::Scroll::kEquippedTechnique) == 0) continue;
    if (sc.kind == scrolls::Kind::Sword || sc.kind == scrolls::Kind::PalmFist)
      level += (1 + static_cast<int>(scrolls::effectiveTier(s, sc))) * (techniquesDouble ? 2 : 1);  // v0.2.6: Daos
  }
  if (p.foundationGrade >= 5) {
    level += 3;  // Heavenly Dao Foundation
  } else if (p.foundationGrade == 4) {
    level += 2;  // Perfect Foundation
  }
  if (p.coreGrade >= 7) level += 2;  // 3rd-grade Golden Core or better
  return level + ownLevelBonus(s);   // v0.2.4: Large Intestine; v0.2.6: the omen "Sharp blade"
}

Danger dangerFor(const int d) {
  if (d >= 10) return Danger::Trivial;
  if (d >= 4) return Danger::Weak;
  if (d >= -3) return Danger::Dangerous;
  if (d >= -9) return Danger::Hazardous;
  if (d >= -15) return Danger::Extreme;
  return Danger::Impossible;
}

Danger regionDanger(const GameState& s, const uint8_t region) {
  return dangerFor(playerLevel(s) - (regionBase(region) + 2));
}

Wound woundOf(const GameState& s) {
  const uint32_t hp = s.progress.hp, max = s.progress.hpMax;
  if (max == 0) return Wound::None;
  if (hp * 100u < max * kBadlyWoundedPercent) return Wound::Badly;
  if (hp * 100u < max * kWoundedPercent) return Wound::Wounded;
  return Wound::None;
}

int woundLevels(const Wound w) { return w == Wound::Badly ? 6 : w == Wound::Wounded ? 3 : 0; }

uint8_t woundSlipCut(const Wound w) { return w == Wound::Badly ? 30 : w == Wound::Wounded ? 15 : 0; }

uint8_t winChance(const int d) {
  const int c = 50 + 6 * d;
  return static_cast<uint8_t>(c < 5 ? 5 : c > 95 ? 95 : c);
}

uint8_t lossPercent(const Danger danger) {
  const int i = static_cast<int>(danger);
  return kLossPercent[i < static_cast<int>(Danger::Count) ? i : static_cast<int>(Danger::Count) - 1];
}

bool hasMovement(const GameState& s) {
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if ((sc.flags & scrolls::Scroll::kEquippedTechnique) != 0 && sc.kind == scrolls::Kind::Movement) return true;
  }
  return false;
}

uint8_t bodyCut(const GameState& s) { return trials::bodyCutPercent(s); }

uint32_t stepCounter(const GameState& s) { return counter(s); }

int pendingNemesis(const GameState& s) {
  if (s.exploration[5] != static_cast<uint8_t>(FightKind::Beast)) return -1;
  const uint8_t v = s.exploration[7];
  if ((v & 0xF0) != kNemesisSlot || (v & 0x0F) == 0 || (v & 0x0F) > rivals::kNemesisMax) return -1;
  rivals::Nemesis n;
  return rivals::nemesis(s, static_cast<uint8_t>((v & 0x0F) - 1), n) ? (v & 0x0F) - 1 : -1;
}

uint32_t beastSeed(const GameState& s) {
  const int nemi = pendingNemesis(s);
  rivals::Nemesis n;
  if (nemi >= 0 && rivals::nemesis(s, static_cast<uint8_t>(nemi), n)) return n.seed;  // v0.2.8
  Foe f;
  const int ring = returningFoe(s);
  if (ring >= 0 && foe(s, static_cast<uint8_t>(ring), f)) return f.seed;  // v0.2.1: the same enemy again
  return contentSeed(worldSeedOf(s), kKindBeastLevel, counter(s));
}

bool foe(const GameState& s, const uint8_t index, Foe& out) {
  if (index >= kFoeRing) return false;
  const uint8_t* b = foeBytes(s, index);
  if (b[0] == 0 || b[0] > kRegionCount) return false;
  out.region = static_cast<uint8_t>(b[0] - 1);
  out.returns = static_cast<uint8_t>(b[1] & 0x03);
  out.paid = (b[1] & 0x10) != 0;
  out.seed = static_cast<uint32_t>(b[2]) | (static_cast<uint32_t>(b[3]) << 8) | (static_cast<uint32_t>(b[4]) << 16) |
             (static_cast<uint32_t>(b[5]) << 24);
  return true;
}

int returningFoe(const GameState& s) {
  if (s.exploration[5] != static_cast<uint8_t>(FightKind::Beast)) return -1;
  const uint8_t i = s.exploration[7];
  if (i == 0 || i > kFoeRing) return -1;
  return foeBytes(s, static_cast<uint8_t>(i - 1))[0] == 0 ? -1 : i - 1;
}

uint32_t enemySeed(const GameState& s, const uint8_t region, const uint8_t tile) {
  return contentSeed(
      worldSeedOf(s), kKindEnemy,
      (static_cast<uint32_t>(region) << 16) | (static_cast<uint32_t>(reshapeCount(s, region)) << 8) | tile);
}

Enemy encounterEnemy(const GameState& s, const uint8_t region) { return enemyFromSeed(region, beastSeed(s)); }

Enemy enemyFromSeed(const uint8_t region, const uint32_t seed) {
  Rng rng(mix32(seed, 0x4B494E44u));  // its own stream: the beast's level roll stays as in v0.1.10
  return pickEnemy(region, rng.below(100));
}

Enemy lairEnemy(const GameState& s, const uint8_t region, const uint8_t tile) {
  const uint8_t* w = kEnemyWeights[region < kRegionCount ? region : 0];
  Rng rng(enemySeed(s, region, tile));
  return rng.below(static_cast<uint32_t>(w[0]) + w[1]) < w[0] ? Enemy::Beast : Enemy::Bandit;
}

Enemy guardianEnemy(const GameState& s, const uint8_t region) {
  Rng rng(enemySeed(s, region, kNoTile));
  return pickEnemy(region, rng.below(100));
}

int pendingRegion(const GameState& s) {
  if (s.exploration[5] == 0 || s.exploration[5] > static_cast<uint8_t>(FightKind::Guardian)) return -1;
  if (s.exploration[4] == 0 || s.exploration[4] > kRegionCount) return -1;
  return s.exploration[4] - 1;
}

Fight pendingFight(const GameState& s, const Layout& l) {
  Fight f;
  const FightKind kind = s.exploration[5] <= static_cast<uint8_t>(FightKind::Guardian)
                             ? static_cast<FightKind>(s.exploration[5])
                             : FightKind::None;
  if (kind == FightKind::None || s.exploration[4] == 0 || s.exploration[4] > kRegionCount) return f;
  f.kind = kind;
  f.region = static_cast<uint8_t>(s.exploration[4] - 1);
  f.tile = position(s, l, f.region);
  switch (kind) {
    case FightKind::Beast: {
      const int nemi = pendingNemesis(s);
      rivals::Nemesis n;
      if (nemi >= 0 && rivals::nemesis(s, static_cast<uint8_t>(nemi), n)) {  // v0.2.8: a nemesis
        f.nemesis = static_cast<int8_t>(nemi);
        f.enemy = n.kind;
        f.level = rivals::fightLevel(s, n, rivals::clockHour());
        break;
      }
      f.enemy = encounterEnemy(s, f.region);
      Rng rng(beastSeed(s));
      const int extra = static_cast<int>(rng.below(5));
      f.level = regionBase(f.region) + (f.enemy == Enemy::Cultivator ? 2 * extra : extra);
      Foe back;
      const int ring = returningFoe(s);
      if (ring >= 0 && foe(s, static_cast<uint8_t>(ring), back)) {
        f.returns = back.returns;
        f.level += kFoeLevelStep * back.returns;
      }
      break;
    }
    case FightKind::Lair:
      f.enemy = lairEnemy(s, f.region, f.tile);
      f.level = regionBase(f.region) + 3 + l.lairBonus[f.tile];
      break;
    default:
      f.enemy = guardianEnemy(s, f.region);
      f.level = l.guardianLevel;
      // A cultivator's random extra counts double (the fixed +6 does not).
      if (f.enemy == Enemy::Cultivator) f.level += l.guardianLevel - (regionBase(f.region) + 6);
      break;
  }
  f.level += sects::enemyLevelBonus(s, f.region);  // v0.3.2: a sect war, +2
  f.wound = woundOf(s);                            // v0.2.9
  const int d = playerLevel(s, f.enemy == Enemy::Cultivator) - f.level - woundLevels(f.wound);
  f.danger = dangerFor(d);
  f.chance = winChance(d);
  rivals::Nemesis nm;
  const bool isNemesis = f.nemesis >= 0 && rivals::nemesis(s, static_cast<uint8_t>(f.nemesis), nm);
  f.canSlip = f.enemy != Enemy::Spirit && !(isNemesis && rivals::hasTrait(nm, rivals::Trait::Hunter));
  f.slipChance = !f.canSlip ? 0
                 : hasMovement(s) || (isNemesis && rivals::hasTrait(nm, rivals::Trait::Slow))
                     ? 100
                     : static_cast<uint8_t>(50u + meridians::slipBonus(s) + daily::slipBonus(s));  // Gallbladder, omen
  if (f.canSlip && !(isNemesis && rivals::hasTrait(nm, rivals::Trait::Slow))) {
    // v0.2.9: a wound slows you down, even with a Movement technique (a Slow nemesis still never catches you)
    const uint8_t cut = woundSlipCut(f.wound);
    f.slipChance = static_cast<uint8_t>(f.slipChance > cut + 5u ? f.slipChance - cut : 5u);
  }
  if (isNemesis) {
    f.payCost = rivals::payCost(nm);  // v0.2.8: a bandit, or a Greedy one for half
  } else if (f.enemy == Enemy::Bandit && kind != FightKind::Guardian) {
    f.payCost = (kind == FightKind::Lair ? 30u : 15u) * (f.region + 1u);
  }
  f.canIncense = f.enemy == Enemy::Spirit && inventory::count(s, inventory::Item::CalmIncense) > 0;
  // v0.2.4: reckoned without Lung's HP, then cut by Bladder points.
  const uint32_t raw = ceilPercent(meridians::damageHpBase(s), lossPercent(f.danger));
  f.damage = static_cast<uint16_t>(meridians::scaleUp(ceilPercent(raw, 100u - bodyCut(s)), fightDamagePermille(s)));
  if (isNemesis && rivals::hasTrait(nm, rivals::Trait::Venomous)) f.damage = static_cast<uint16_t>(f.damage * 3u / 2u);
  f.slipDamage = static_cast<uint16_t>((f.damage + 1u) / 2u);
  return f;
}

// --- Stepping ------------------------------------------------------------------------------

StepError canStep(const GameState& s, const Layout& l, const uint8_t region, const uint8_t to) {
  if (s.exploration[5] != 0) return StepError::FightWaiting;
  if (s.progress.hp == 0) return StepError::NoHp;
  if (!adjacent(position(s, l, region), to)) return StepError::NotAdjacent;
  if (!walkable(l, to)) return StepError::Blocked;
  if (s.progress.stamina == 0) return StepError::NoStamina;
  return StepError::None;
}

StepResult step(GameState& s, const Layout& l, const uint8_t region, const uint8_t to) {
  StepResult r;
  if (canStep(s, l, region, to) != StepError::None) return r;
  s.progress.stamina = static_cast<uint8_t>(s.progress.stamina - 1);
  setPosition(s, region, to);
  if (!charted(s, region, to)) daily::note(s, daily::Task::Chart);  // v0.2.6: a daily task
  chart(s, region, to);
  r.moved = true;
  const uint32_t c = counter(s);
  setCounter(s, c + 1);

  const Tile t = l.tiles[to];
  // v0.3.1 (user): the v0.2.1 cap is gone, so fights and meetings may come on consecutive steps.
  // Bit 0x01 is still cleared here, so a save from before v0.3.1 holds no stale cap.
  s.exploration[6] = static_cast<uint8_t>(s.exploration[6] & ~0x01);
  if ((t == Tile::Lair || t == Tile::Guardian) && !used(s, region, to)) {
    setPending(s, region, t == Tile::Lair ? FightKind::Lair : FightKind::Guardian);
    r.fight = true;
    return r;
  }
  // v0.2.8: a nemesis roaming this region finds you (6%, Ambusher 12%), unless resting.
  {
    const int nemi = rivals::ambush(s, region, contentSeed(worldSeedOf(s), kKindNemesisFind, c));
    if (nemi >= 0) {
      s.exploration[7] = static_cast<uint8_t>(kNemesisSlot | (nemi + 1));
      setPending(s, region, FightKind::Beast);
      rivals::Nemesis n;
      rivals::nemesis(s, static_cast<uint8_t>(nemi), n);
      if (rivals::hasTrait(n, rivals::Trait::Coward) && pendingFight(s, l).chance >= 60) {
        // A Coward runs when your chance is 60% or better: no fight, the tile as usual.
        setPending(s, region, FightKind::None);
        s.exploration[7] = 0;
        rivals::itFled(s, static_cast<uint8_t>(nemi));
        r.nemesisFled = static_cast<int8_t>(nemi);
        resolveTile(s, l, region, r);
        checkClear(s, l, region, r);
        return r;
      }
      r.fight = true;
      return r;
    }
  }
  Rng encounter(contentSeed(worldSeedOf(s), kKindEncounter, c));
  if (encounter.below(100) < sects::encounterPercent(s, region, l.encounterPercent)) {  // v0.3.2: beast tide
    // A foe that got away from you here comes back one time in three.
    const int waiting = waitingFoeIn(s, region);
    s.exploration[7] = waiting >= 0 && encounter.below(3) == 0 ? static_cast<uint8_t>(waiting + 1) : 0;
    setPending(s, region, FightKind::Beast);
    // v0.3.2: a sect war calls cultivators, a bandit camp bandits: one more roll toward them (the
    // step counter moves on one, so the waiting enemy stays the same until it is fought).
    const uint8_t favoured = sects::favouredEnemy(s, region);
    if (favoured != 0xFF && s.exploration[7] == 0 && static_cast<uint8_t>(encounterEnemy(s, region)) != favoured) {
      setCounter(s, counter(s) + 1);
    }
    r.fight = true;
    return r;
  }
  // v0.2.2: a cultivator on the road (v0.3.1: 6%, no cap).
  Rng roll(contentSeed(worldSeedOf(s), kKindMeetRoll, c));
  if (t != Tile::Merchant && roll.below(100) < sects::meetPercent(s, region, kMeetPercent)) {  // v0.3.2: x2
    const int waiting = waitingCultIn(s, region, roll.next());
    const bool again = waiting >= 0 && roll.below(3) == 0;  // one meeting in three is a remembered face
    s.exploration[7] = again ? static_cast<uint8_t>(waiting + 1) : 0;
    // v0.2.8: a share of meetings (v0.3.1: 40%) is your rival, once their realm has opened this region.
    if (rivals::canMeetIn(s, region) && roll.below(100) < rivals::kRivalMeetPercent) {
      s.exploration[7] = kRivalSlot;
      rivals::markKnown(s);
    }
    s.exploration[6] = static_cast<uint8_t>(s.exploration[6] & ~kMeetFlags);
    setPending(s, region, FightKind::Meeting);
    r.meeting = true;
    return r;
  }
  resolveTile(s, l, region, r);
  checkClear(s, l, region, r);
  return r;
}

StepResult resolveHere(GameState& s, const Layout& l, const uint8_t region) {
  StepResult r;
  if (s.exploration[5] != 0) return r;
  resolveTile(s, l, region, r);
  checkClear(s, l, region, r);
  return r;
}

FightResult fight(GameState& s, const Layout& l, const FightChoice choice) {
  FightResult res;
  const Fight f = pendingFight(s, l);
  if (f.kind == FightKind::None) return res;
  // v0.3.3: names the cultivator's sect (the same seed the fight screen used).
  const uint32_t personSeed =
      f.kind == FightKind::Beast ? beastSeed(s) : enemySeed(s, f.region, f.kind == FightKind::Lair ? f.tile : kNoTile);
  if ((choice == FightChoice::Slip && !f.canSlip) ||
      (choice == FightChoice::Pay && (f.payCost == 0 || s.progress.stones < f.payCost)) ||
      (choice == FightChoice::Incense && !f.canIncense)) {
    res.refused = true;
    return res;
  }
  // v0.2.1: who this was, before the counter (which names new enemies) moves on.
  const int ring = f.kind == FightKind::Beast ? returningFoe(s) : -1;
  const uint32_t seed = beastSeed(s);
  const int nemi = f.nemesis;  // v0.2.8
  res.nemesis = f.nemesis;
  res.returning = ring >= 0;
  const uint32_t c = counter(s);
  setCounter(s, c + 1);
  setPending(s, f.region, FightKind::None);
  s.exploration[7] = 0;
  Rng rng(contentSeed(worldSeedOf(s), kKindFightRoll, c));
  // A beast or bandit that got away (slipped past or paid off) is remembered.
  auto remember = [&](const bool paid) {
    if (nemi >= 0 || f.kind != FightKind::Beast || (f.enemy != Enemy::Beast && f.enemy != Enemy::Bandit)) return;
    Foe back;
    if (ring >= 0 && foe(s, static_cast<uint8_t>(ring), back)) {
      if (back.returns < kFoeMaxReturns) ++back.returns;
      back.paid = paid;
      writeFoe(s, static_cast<uint8_t>(ring), back);
    } else {
      back.region = f.region;
      back.returns = 1;
      back.paid = paid;
      back.seed = seed;
      addFoe(s, back);
    }
    res.remembered = true;
  };
  res.roll = static_cast<uint8_t>(rng.below(100));
  ProgressBlock& p = s.progress;
  const bool half = trivialYield(s, f.region);
  const uint32_t afterSeed = contentSeed(worldSeedOf(s), kKindNemesisFind, c | 0x80000000u);
  auto noteAfter = [&](const rivals::Aftermath& a) {
    res.nemesisNewTrait = static_cast<uint8_t>(a.newTrait);
    res.nemesisRevealedTrait = static_cast<uint8_t>(a.revealedTrait);
  };

  uint16_t damage = f.damage;
  // v0.2.0: paying off a bandit or driving off a spirit ends it like slipping away.
  if (choice == FightChoice::Pay || choice == FightChoice::Incense) {
    if (choice == FightChoice::Pay) {
      p.stones -= f.payCost;
      res.paid = true;
      res.paidStones = f.payCost;
      remember(true);
      if (nemi >= 0) noteAfter(rivals::youEscaped(s, static_cast<uint8_t>(nemi), true, afterSeed));
    } else {
      inventory::remove(s, inventory::Item::CalmIncense);
      res.drivenOff = true;
    }
    if (f.kind == FightKind::Beast) {
      resolveTile(s, l, f.region, res.tile);
      checkClear(s, l, f.region, res.tile);
    } else {
      res.thrownTo = throwBack(s, l, f.region, rng);
    }
    return res;
  }
  if (choice == FightChoice::Slip) {
    if (res.roll < f.slipChance) {
      res.slipped = true;
      remember(false);
      if (nemi >= 0) noteAfter(rivals::youEscaped(s, static_cast<uint8_t>(nemi), false, afterSeed));
      if (f.kind == FightKind::Beast) {
        resolveTile(s, l, f.region, res.tile);
        checkClear(s, l, f.region, res.tile);
      } else {
        res.thrownTo = throwBack(s, l, f.region, rng);
      }
      return res;
    }
    damage = f.slipDamage;
  } else if (res.roll < f.chance) {
    res.won = true;
    daily::note(s, daily::Task::WinFight);  // v0.2.6
    switch (f.kind) {
      case FightKind::Beast:
        res.reward.stones = halve(kindStones(f.enemy, rng.range(5, 10) * (f.region + 1u)), half);
        if (nemi >= 0) {  // v0.2.8: a nemesis pays like a challenge, three times a beast, and escapes scarred
          res.reward.stones = kindStones(f.enemy, 3u * rng.range(5, 10) * (f.region + 1u));
          noteAfter(rivals::youWon(s, static_cast<uint8_t>(nemi), afterSeed));
        }
        if (ring >= 0) {  // v0.2.1: a returning foe pays half again, and is gone
          res.reward.stones = res.reward.stones * 3u / 2u;
          Foe back;
          // v0.2.8: beaten on its third meeting, one in four survives as a nemesis.
          if (foe(s, static_cast<uint8_t>(ring), back) && back.returns >= kFoeMaxReturns &&
              rng.below(100) < rivals::kReturningBirthPercent) {
            res.nemesisBorn = static_cast<int8_t>(rivals::bear(s, f.enemy, f.region, f.level, back.seed, c));
          }
          forgetFoe(s, static_cast<uint8_t>(ring));
          res.foeBeaten = true;
        }
        // Only beasts carry herbs and ores (v0.3.2: twice as often in a beast tide).
        if (f.enemy == Enemy::Beast && rng.below(100) < sects::beastLootPercent(s, f.region, 25)) {
          give(s, res.reward, rng.below(2) == 0 ? inventory::herbFor(f.region) : inventory::oreFor(f.region), 1);
        }
        res.reward.stones = warStones(s, f.region, res.reward.stones);
        p.stones += res.reward.stones;
        resolveTile(s, l, f.region, res.tile);
        break;
      case FightKind::Lair:
        res.reward.stones = halve(kindStones(f.enemy, 2u * rng.range(5, 10) * (f.region + 1u)), half);
        if (f.enemy == Enemy::Beast && rng.below(100) < sects::beastLootPercent(s, f.region, 50)) {
          give(s, res.reward, rng.below(2) == 0 ? inventory::herbFor(f.region) : inventory::oreFor(f.region), 1);
        }
        res.reward.stones = warStones(s, f.region, res.reward.stones);
        p.stones += res.reward.stones;
        markUsed(s, f.region, f.tile);
        startRegrow(s, f.region);
        break;
      default: {
        uint8_t* b = regionBytes(s, f.region);
        res.reward.stones = warStones(s, f.region, 40u * (f.region + 1u));
        p.stones += res.reward.stones;
        give(s, res.reward, treasure(rng), 1);
        give(s, res.reward, inventory::Item::TonicMinor, 1);
        markUsed(s, f.region, f.tile);
        b[11] = static_cast<uint8_t>(b[11] | kFlagGuardian);
        res.guardianBeaten = true;
        break;
      }
    }
    // v0.3.2/v0.3.3: what the win does to the living world.
    if (nemi < 0) {
      if (f.enemy == Enemy::Bandit) {
        res.world = sects::banditBeaten(s, f.region);
      } else if (f.enemy == Enemy::Beast) {
        res.world = sects::beastBeaten(s, f.region);
      } else if (f.enemy == Enemy::Cultivator) {
        res.world = sects::cultivatorBeaten(s, f.region, sects::personSect(s, f.region, personSeed));
      }
      worldReward(s, res.world, f.region, res.reward, rng);
    }
    // v0.2.0: a beaten cultivator sometimes leaves a recipe slip.
    if (f.enemy == Enemy::Cultivator && alchemy::slipRecipe(s) >= 0 &&
        rng.below(100) < 20u + daos::prizeChanceBonus(s)) {
      res.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
    }
    // v0.2.1: a hard fight sometimes pays with something better. v0.2.8: a
    // nemesis always counts as a hard fight; a Pack leader's prize is likelier.
    uint32_t prize = prizeChance(s, f.danger);
    if (nemi >= 0) {
      rivals::Nemesis nm;
      rivals::nemesis(s, static_cast<uint8_t>(nemi), nm);
      const uint8_t hard = prizeChance(s, Danger::Hazardous);
      if (prize < hard) prize = hard;
      if (rivals::hasTrait(nm, rivals::Trait::PackLeader)) prize += 20u;
    }
    if (rng.below(100) < prize) {
      if (res.reward.recipe < 0 && alchemy::slipRecipe(s) >= 0) {
        res.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
      } else {
        const inventory::Grade g = inventory::gradeOfRegion(f.region);
        const uint8_t from = g == inventory::Grade::Common   ? 3
                             : g == inventory::Grade::Spirit ? 6
                                                             : static_cast<uint8_t>(6 + rng.below(4));
        give(s, res.reward, rng.below(2) == 0 ? inventory::herbFor(from) : inventory::oreFor(from), 2);
      }
    }
    checkClear(s, l, f.region, res.tile);
    return res;
  }

  // Lost (or failed to slip away): HP, then thrown onto a charted neighbour.
  // The step that led here may have charted the region's last tile.
  // v0.2.9 (user): a loss that takes the last HP is a collapse: carried back
  // to the entrance; nothing owned is lost.
  res.collapsed = damage >= p.hp;
  res.hpLost = damage < p.hp ? damage : p.hp;
  p.hp = static_cast<uint16_t>(p.hp - res.hpLost);
  if (nemi >= 0) {  // v0.2.8: it grows from beating you
    noteAfter(rivals::youLost(s, static_cast<uint8_t>(nemi), playerLevel(s), afterSeed));
  } else if (f.kind == FightKind::Beast && choice == FightChoice::Fight && f.enemy != Enemy::Spirit &&
             f.danger >= Danger::Dangerous && rng.below(100) < rivals::kBirthPercent) {
    // v0.2.8: a beast, bandit or cultivator that beats you in a Dangerous or harder fight
    // may become your nemesis (a returning foe leaves the ring for it).
    res.nemesisBorn = static_cast<int8_t>(rivals::bear(s, f.enemy, f.region, f.level, seed, c));
    if (res.nemesisBorn >= 0 && ring >= 0) {
      forgetFoe(s, static_cast<uint8_t>(ring));
    } else if (res.nemesisBorn < 0 && ring < 0 && f.enemy != Enemy::Cultivator) {
      remember(false);  // all three places taken: an ordinary returning foe instead
    }
  }
  res.thrownTo = res.collapsed ? carryToEntrance(s, l, f.region) : throwBack(s, l, f.region, rng);
  checkClear(s, l, f.region, res.tile);
  return res;
}

// --- Meetings (v0.2.2) ---------------------------------------------------------------------

int meetingRegion(const GameState& s) {
  if (s.exploration[5] != static_cast<uint8_t>(FightKind::Meeting)) return -1;
  if (s.exploration[4] == 0 || s.exploration[4] > kRegionCount) return -1;
  return s.exploration[4] - 1;
}

int waitingRegion(const GameState& s) {
  const int fightRegion = pendingRegion(s);
  return fightRegion >= 0 ? fightRegion : meetingRegion(s);
}

namespace {
uint16_t meetAgeYears(const GameState& s) {
  const uint32_t years = s.progress.ageMonths / 12u;
  return static_cast<uint16_t>(years > 0x1FFFu ? 0x1FFFu : years);
}

// The guardian's kind and how you would rate it, for a hint; false when there
// is nothing left to tell (it is beaten).
bool guardianHint(const GameState& s, const Layout& l, const uint8_t region, Enemy& kind, Danger& danger) {
  if (guardianBeaten(s, region) || cleared(s, region)) return false;
  kind = guardianEnemy(s, region);
  int level = l.guardianLevel;
  if (kind == Enemy::Cultivator) level += l.guardianLevel - (regionBase(region) + 6);
  danger = dangerFor(playerLevel(s, kind == Enemy::Cultivator) - level);
  return true;
}

uint16_t percentOf(const uint16_t price, const uint32_t percent) {
  const uint32_t v = (static_cast<uint32_t>(price) * percent + 50u) / 100u;
  return static_cast<uint16_t>(v < 1u ? 1u : v > 0xFFFFu ? 0xFFFFu : v);
}
}  // namespace

Meeting pendingMeeting(const GameState& s, const Layout& l) {
  Meeting m;
  const int region = meetingRegion(s);
  if (region < 0) return m;
  m.active = true;
  m.region = static_cast<uint8_t>(region);
  const uint8_t slot = s.exploration[7];
  const rivals::Rival rv = rivals::rival(s);
  if (slot == kRivalSlot && rv.exists) {  // v0.2.8: your rival, at their own level, never scaled to you
    m.rival = true;
    m.firstRival = !rv.metBefore;
    m.nameSeed = rv.seed;
    m.meets = static_cast<uint8_t>(rv.metBefore ? 1 : 0);
    m.temperament = Temperament::Cautious;  // a fair price, like a cautious traveller
    m.offer = rivals::offersTrade(s, static_cast<uint8_t>(counter(s))) ? Offer::Trade : Offer::Spar;
    m.level = rivals::rivalLevel(s);
    m.wound = woundOf(s);  // v0.2.9: costs nothing in a spar, but you fight worse
    const int d = playerLevel(s, true) - m.level - woundLevels(m.wound);
    m.danger = dangerFor(d);
    m.chance = winChance(d);
    m.dares = false;
    m.damage = 0;  // a spar: no HP lost
    m.talked = (s.exploration[6] & kMeetFlagTalked) != 0;
    m.traded = (s.exploration[6] & kMeetFlagBought) != 0;
    (void)l;
    return m;
  }
  Cult cult;
  if (slot >= 1 && slot <= kMeetRing && readCult(s, static_cast<uint8_t>(slot - 1), cult) && cult.region == region) {
    m.ring = slot - 1;
    m.id = cult.id;
    m.meets = cult.meets;
    m.last = cult.outcome;
    const uint16_t now = meetAgeYears(s);
    m.yearsAgo = now > cult.years ? static_cast<uint16_t>(now - cult.years) : 0;
  } else {
    m.id = static_cast<uint16_t>(mix32(contentSeed(worldSeedOf(s), kKindMeetWho, counter(s)), 0x4D454554u) & 0x3FFu);
  }
  m.nameSeed = contentSeed(worldSeedOf(s), kKindMeetWho, (static_cast<uint32_t>(region) << 10) | m.id);

  const uint32_t t = Rng(mix32(m.nameSeed, 0x54454D50u)).below(100);  // 35 proud, 35 cautious, 30 greedy
  m.temperament = t < 35 ? Temperament::Proud : t < 70 ? Temperament::Cautious : Temperament::Greedy;
  const int offset = -9 + static_cast<int>(Rng(mix32(m.nameSeed, 0x4F464653u)).below(16));  // -9 .. +6
  m.levelsAdded = static_cast<uint8_t>(kMeetLevelStep * (m.meets < 2 ? m.meets : 2));
  // Their level follows yours (techniques left out), so a meeting is always about as hard as the road.
  const int with = playerLevel(s, true);
  // Large Intestine points are yours alone: left out of what their level follows.
  const int plain = 2 * playerLevel(s, false) - with - ownLevelBonus(s);
  m.level = plain + offset + m.levelsAdded;
  if (m.level < 1) m.level = 1;
  const int whole = with - m.level;  // a cultivator: your Sword and Palm/Fist count double
  m.dares = whole <= 3;              // Dangerous or worse: weaker ones do not challenge you
  m.wound = woundOf(s);              // v0.2.9: who dares is judged on you unhurt
  const int d = whole - woundLevels(m.wound);
  m.danger = dangerFor(d);
  m.chance = winChance(d);
  const uint32_t raw = ceilPercent(meridians::damageHpBase(s), lossPercent(m.danger));
  m.damage = static_cast<uint16_t>(meridians::scaleUp(ceilPercent(raw, 100u - bodyCut(s)), fightDamagePermille(s)));

  Rng o(mix32(m.nameSeed, 0x4F464652u + m.meets));
  switch (m.temperament) {
    case Temperament::Proud:
      m.offer = m.dares ? Offer::Challenge : Offer::Spar;
      break;
    case Temperament::Cautious:
      m.offer = o.below(100) < 70 ? Offer::Trade : Offer::Spar;
      break;
    default:
      m.offer = m.dares ? Offer::Challenge : Offer::Trade;
      break;
  }
  (void)l;
  m.talked = (s.exploration[6] & kMeetFlagTalked) != 0;
  m.traded = (s.exploration[6] & kMeetFlagBought) != 0;
  return m;
}

bool metCultivator(const GameState& s, const uint8_t index, uint8_t& region, uint16_t& id, uint8_t& meets,
                   MeetOutcome& outcome) {
  Cult c;
  if (!readCult(s, index, c)) return false;
  region = c.region;
  id = c.id;
  meets = c.meets;
  outcome = c.outcome;
  return true;
}

namespace {
// The meeting is over: remember the cultivator, free the map. `tileToo`: the
// tile's content is still to be taken (everything but a lost challenge).
void finishMeeting(GameState& s, const Layout& l, const Meeting& m, const MeetOutcome outcome, const bool tileToo,
                   MeetResult& res) {
  setCounter(s, counter(s) + 1);
  setPending(s, m.region, FightKind::None);
  s.exploration[6] = static_cast<uint8_t>(s.exploration[6] & ~kMeetFlags);
  if (m.rival) {  // v0.2.8: the rival is remembered in their own block
    rivals::markMet(s);
    res.done = true;
    res.outcome = outcome;
    res.rival = true;
    if (tileToo) resolveTile(s, l, m.region, res.tile);
    checkClear(s, l, m.region, res.tile);
    return;
  }
  Cult c;
  c.region = m.region;
  c.id = m.id;
  c.meets = static_cast<uint8_t>(m.meets < 3 ? m.meets + 1 : 3);
  c.outcome = outcome;
  c.years = meetAgeYears(s);
  if (m.ring >= 0) {
    writeCult(s, static_cast<uint8_t>(m.ring), c);
  } else {
    addCult(s, c);
  }
  res.done = true;
  res.outcome = outcome;
  if (tileToo) resolveTile(s, l, m.region, res.tile);
  checkClear(s, l, m.region, res.tile);
}
}  // namespace

MeetResult meet(GameState& s, const Layout& l, const MeetChoice choice) {
  MeetResult res;
  const Meeting m = pendingMeeting(s, l);
  if (!m.active) {
    res.refused = true;
    return res;
  }
  ProgressBlock& p = s.progress;
  uint8_t& flags = s.exploration[6];

  if (choice == MeetChoice::Talk) {
    if (m.talked) {
      res.refused = true;
      return res;
    }
    Enemy kind = Enemy::Beast;
    Danger danger = Danger::Dangerous;
    const bool hint = guardianHint(s, l, m.region, kind, danger);
    uint32_t cost = 0;
    bool tells = hint;
    if (m.temperament == Temperament::Cautious && !m.rival) tells = hint && m.ring >= 0;  // only to a face they know
    if (m.temperament == Temperament::Greedy && hint) cost = 10u * (m.region + 1u);
    if (tells && cost > p.stones) {
      res.talk = TalkResult::TooPoor;
      res.talkCost = cost;
      return res;  // nothing happened; they will ask again
    }
    flags = static_cast<uint8_t>(flags | kMeetFlagTalked);
    res.rival = m.rival;
    // v0.2.8: talk on the road names a nemesis's trait, when one is still hidden.
    {
      rivals::Trait t = rivals::Trait::None;
      const int named = rivals::revealOne(s, t, counter(s));
      if (named >= 0 && t != rivals::Trait::None) {
        res.nemesisRevealed = static_cast<int8_t>(named);
        res.revealedTrait = static_cast<uint8_t>(t);
      }
    }
    if (tells) {
      p.stones -= cost;
      res.talk = TalkResult::Hint;
      res.hintEnemy = kind;
      res.hintDanger = danger;
      res.talkPaid = cost;
    } else {
      res.talk = TalkResult::Nothing;
    }
    return res;
  }

  if (choice == MeetChoice::WalkOn) {
    const MeetOutcome outcome = m.traded ? MeetOutcome::Traded : m.talked ? MeetOutcome::Talked : MeetOutcome::Passed;
    finishMeeting(s, l, m, outcome, true, res);
    return res;
  }

  // Offer: a spar or a challenge (a trade is the shop).
  if (m.offer == Offer::Trade) {
    res.refused = true;
    return res;
  }
  const uint32_t c = counter(s);
  Rng rng(contentSeed(worldSeedOf(s), kKindFightRoll, c));
  res.roll = static_cast<uint8_t>(rng.below(100));
  res.fought = true;
  res.won = res.roll < m.chance;
  if (res.won) daily::note(s, daily::Task::WinFight);  // v0.2.6
  const bool half = trivialYield(s, m.region);
  if (m.rival) {  // v0.2.8: a spar with your rival: twice a beast's stones, no HP either way
    if (res.won) {
      res.reward.stones = 2u * rng.range(5, 10) * (m.region + 1u);
      p.stones += res.reward.stones;
      rivals::beaten(s, daily::live(s) ? daily::today(s) : StatsSnapshot::kNoDay);
    } else {
      rivals::lostTo(s);
    }
    finishMeeting(s, l, m, res.won ? MeetOutcome::Won : MeetOutcome::Lost, true, res);
    return res;
  }
  // v0.3.3: beating a sect's people on the road counts for (or against) the sects.
  if (res.won) res.world = sects::cultivatorBeaten(s, m.region, sects::personSect(s, m.region, m.nameSeed));
  if (m.offer == Offer::Spar) {
    if (res.won) {
      res.reward.stones = halve(rng.range(5, 10) * (m.region + 1u), half);  // like a beast, no herbs
      p.stones += res.reward.stones;
    }
    finishMeeting(s, l, m, res.won ? MeetOutcome::Won : MeetOutcome::Lost, true, res);
    return res;
  }
  // Challenge: a real fight.
  if (res.won) {
    res.reward.stones = halve(3u * rng.range(5, 10) * (m.region + 1u), half);
    p.stones += res.reward.stones;
    if (alchemy::slipRecipe(s) >= 0 && rng.below(100) < 30u + daos::prizeChanceBonus(s)) {
      res.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
    }
    if (rng.below(100) < prizeChance(s, m.danger)) {  // the hard-fight prize, as in v0.2.1
      if (res.reward.recipe < 0 && alchemy::slipRecipe(s) >= 0) {
        res.reward.recipe = static_cast<int8_t>(alchemy::learnSlip(s));
      } else {
        const inventory::Grade g = inventory::gradeOfRegion(m.region);
        const uint8_t from = g == inventory::Grade::Common   ? 3
                             : g == inventory::Grade::Spirit ? 6
                                                             : static_cast<uint8_t>(6 + rng.below(4));
        give(s, res.reward, rng.below(2) == 0 ? inventory::herbFor(from) : inventory::oreFor(from), 2);
      }
    }
    finishMeeting(s, l, m, MeetOutcome::Won, true, res);
    return res;
  }
  res.collapsed = m.damage >= p.hp;  // v0.2.9
  res.hpLost = m.damage < p.hp ? m.damage : p.hp;
  p.hp = static_cast<uint16_t>(p.hp - res.hpLost);
  res.thrownTo = res.collapsed ? carryToEntrance(s, l, m.region) : throwBack(s, l, m.region, rng);
  // v0.2.8: a road cultivator who beats you in a Dangerous or harder challenge may become your nemesis.
  if (m.danger >= Danger::Dangerous && rng.below(100) < rivals::kBirthPercent) {
    res.nemesisBorn = static_cast<int8_t>(rivals::bear(s, Enemy::Cultivator, m.region, m.level, m.nameSeed, c));
  }
  finishMeeting(s, l, m, MeetOutcome::Lost, false, res);
  if (res.nemesisBorn >= 0) {  // a nemesis now, not a face on the road
    for (uint8_t i = 0; i < kMeetRing; ++i) {
      Cult cult;
      if (readCult(s, i, cult) && cult.region == m.region && cult.id == m.id) {
        forgetCult(s, i);
        break;
      }
    }
  }
  return res;
}

// What a traveller carries (stable for the meeting): the next closed region's
// ingredient at 1.5x (greedy: 2x), a recipe slip you could learn and one
// treasure at the list price (greedy: +30%). Someone who traded with you
// before takes 10% off.
namespace {
// Before v0.2.4's Pericardium discount.
uint16_t listPrice(const GameState& s, const inventory::Item item) {
  if (item == inventory::Item::RecipeSlip) {
    const int r = alchemy::slipRecipe(s);
    return r < 0 ? 0 : alchemy::slipPrice(alchemy::recipesOf(s), static_cast<uint8_t>(r));
  }
  return inventory::price(item);
}

uint16_t buyPrice(const GameState& s, const uint16_t price, const uint16_t regionPermille = 1000) {
  if (price == 0) return 0;
  // v0.2.4: Pericardium points; v0.2.6: the omen "Generous road".
  uint32_t permille = static_cast<uint32_t>(meridians::buyPermille(s)) * daily::buyPermille(s) / 1000u;
  // v0.3.2: a temple fair or a blight in the merchant's region; a fair's discount stops at 30% in all.
  if (regionPermille != 1000) {
    permille = permille * regionPermille / 1000u;
    if (regionPermille < 1000 && permille < sects::kBuyFloorPermille) permille = sects::kBuyFloorPermille;
  }
  const uint32_t p = (static_cast<uint32_t>(price) * permille + 500u) / 1000u;
  return static_cast<uint16_t>(p == 0 ? 1u : p);
}

Stock traderStock(const GameState& s, const Layout& l, const uint8_t region) {
  Stock st;
  const Meeting m = pendingMeeting(s, l);
  if (!m.active || m.region != region) return st;
  const bool greedy = m.temperament == Temperament::Greedy;
  const uint32_t discount = m.last == MeetOutcome::Traded ? 90u : 100u;
  Rng rng(contentSeed(worldSeedOf(s), kKindMeetStock, mix32(m.nameSeed, m.meets)));
  uint8_t next = unlockedCount(s);
  if (next >= kRegionCount) next = kRegionCount - 1;
  const inventory::Item ingredient = rng.below(2) == 0 ? inventory::herbFor(next) : inventory::oreFor(next);
  const uint16_t base = inventory::price(ingredient);
  StockLine line;
  line.item = ingredient;
  line.price = buyPrice(s, percentOf(percentOf(base, greedy ? 200u : 150u), discount));
  const uint8_t bought = static_cast<uint8_t>(s.exploration[6] >> kMeetBoughtShift);
  line.left = bought < kTraderIngredientMax ? static_cast<uint8_t>(kTraderIngredientMax - bought) : 0;
  line.soldOut = line.left == 0;
  st.lines[st.count++] = line;
  const bool slip = alchemy::slipRecipe(s) >= 0;
  if (slip) {
    const uint16_t price = listPrice(s, inventory::Item::RecipeSlip);
    StockLine sl;
    sl.item = inventory::Item::RecipeSlip;
    sl.limited = true;
    sl.soldOut = (s.exploration[6] & kMeetFlagSlip) != 0;
    sl.price = buyPrice(s, percentOf(percentOf(price, greedy ? 130u : 100u), discount));
    st.lines[st.count++] = sl;
  }
  StockLine tl;
  tl.item = rng.below(2) == 0 ? inventory::Item::ThunderTalisman : inventory::Item::CalmIncense;
  tl.limited = true;
  tl.soldOut = (s.exploration[6] & kMeetFlagTreasure) != 0;
  tl.price = buyPrice(s, percentOf(percentOf(inventory::price(tl.item), greedy ? 130u : 100u), discount));
  st.lines[st.count++] = tl;
  return st;
}

bool buyFromTrader(GameState& s, const Layout& l, const uint8_t region, const uint8_t line, const uint16_t count) {
  const Stock st = traderStock(s, l, region);
  if (line >= st.count || st.lines[line].soldOut || count == 0) return false;
  const StockLine& sl = st.lines[line];
  if (sl.limited && count != 1) return false;
  if (!sl.limited && count > affordable(s, sl)) return false;
  const uint32_t cost = static_cast<uint32_t>(sl.price) * count;
  if (cost == 0 || s.progress.stones < cost) return false;
  s.progress.stones -= cost;
  uint8_t bits = kMeetFlagBought;
  if (sl.item == inventory::Item::RecipeSlip) {
    alchemy::learnSlip(s);
    bits = static_cast<uint8_t>(bits | kMeetFlagSlip);
  } else {
    inventory::add(s, sl.item, count);
    if (sl.limited) bits = static_cast<uint8_t>(bits | kMeetFlagTreasure);
  }
  s.exploration[6] = static_cast<uint8_t>(s.exploration[6] | bits);
  if (!sl.limited) {  // the ingredient: count toward the cap (count <= left, so it fits in 3 bits)
    const uint8_t bought = static_cast<uint8_t>((s.exploration[6] >> kMeetBoughtShift) + count);
    s.exploration[6] = static_cast<uint8_t>((s.exploration[6] & 0x1F) | (bought << kMeetBoughtShift));
  }
  daily::note(s, daily::Task::Merchant);  // v0.2.6
  return true;
}
}  // namespace

// --- Regrowth, upheaval ---------------------------------------------------------------------

void reshape(GameState& s, const uint8_t region) {
  if (region >= kRegionCount) return;
  uint8_t* b = regionBytes(s, region);
  const uint8_t next = static_cast<uint8_t>(b[10] + 1);
  const uint8_t cycle = b[17];
  for (size_t i = 0; i < kRegionBytes; ++i) b[i] = 0;
  b[10] = next;
  b[17] = static_cast<uint8_t>(cycle + 1);  // a new layout's merchants have new stock
  if (s.exploration[4] == region + 1) {
    setPending(s, region, FightKind::None);
    s.exploration[6] = static_cast<uint8_t>(s.exploration[6] & ~kMeetFlags);
  }
}

uint16_t tick(GameState& s) {
  uint16_t reshaped = 0;
  for (uint8_t region = 0; region < kRegionCount; ++region) {
    uint8_t* b = regionBytes(s, region);
    const uint16_t regrow = get16(b + 13);
    if (regrow != 0 && unitsSince(s, regrow) >= meridians::regrowUnits(s, kRegrowUnits)) {
      const Layout l = layoutOf(s, region);
      for (uint8_t t = 0; t < kTiles; ++t) {
        if (regrows(l.tiles[t])) clearBit(b + 5, t);
      }
      set16(b + 13, 0);
      b[17] = static_cast<uint8_t>(b[17] + 1);
      b[18] = 0;
    }
    if (cleared(s, region) && unitsSince(s, get16(b + 15)) >= kUpheavalUnits) {
      reshape(s, region);
      reshaped = static_cast<uint16_t>(reshaped | (1u << region));
    }
  }
  return reshaped;
}

bool canUseRegionTalisman(const GameState& s, const uint8_t region) {
  return region < kRegionCount && cleared(s, region) && inventory::count(s, inventory::Item::RegionTalisman) > 0;
}

bool useRegionTalisman(GameState& s, const uint8_t region) {
  if (!canUseRegionTalisman(s, region)) return false;
  inventory::remove(s, inventory::Item::RegionTalisman);
  reshape(s, region);
  return true;
}

// --- Merchants ----------------------------------------------------------------------------------

Stock stockFor(const GameState& s, const Layout& l, const uint8_t region, const uint8_t tile) {
  Stock st;
  if (tile == kTraderTile) return traderStock(s, l, region);  // v0.2.2: the waiting meeting's traveller
  if (tile >= kTiles || l.tiles[tile] != Tile::Merchant) return st;
  const uint8_t* b = regionBytes(s, region);
  Rng rng(contentSeed(worldSeedOf(s), kKindStock,
                      (static_cast<uint32_t>(region) << 24) | (static_cast<uint32_t>(b[10]) << 16) |
                          (static_cast<uint32_t>(tile) << 8) | b[17]));
  st.lines[st.count++] = {inventory::herbFor(region), false, false};
  st.lines[st.count++] = {inventory::oreFor(region), false, false};
  struct Chance {
    inventory::Item item;
    uint8_t percent;
  };
  // How often each limited item is in stock (agreed, v0.1.10).
  static const Chance kChances[] = {{inventory::Item::TonicMinor, 50},  {inventory::Item::TonicGreater, 25},
                                    {inventory::Item::TonicFull, 8},    {inventory::Item::ThunderTalisman, 30},
                                    {inventory::Item::CalmIncense, 30}, {inventory::Item::RegionTalisman, 5}};
  for (const Chance& c : kChances) {
    const bool rolled = rng.below(100) < c.percent;  // every roll is made, so stock is stable per cycle
    if (rolled && st.count < kMerchantStockMax) st.lines[st.count++] = {c.item, true, false};
  }
  if (st.count == 2) st.lines[st.count++] = {inventory::Item::TonicMinor, true, false};
  // v0.2.0: a recipe slip, 15% per restock, while there is a recipe it could teach.
  const bool slipRolled = rng.below(100) < 15;
  if (slipRolled && alchemy::slipRecipe(s) >= 0 && st.count < kMerchantStockMax) {
    st.lines[st.count++] = {inventory::Item::RecipeSlip, true, false};
  }
  const uint8_t base = static_cast<uint8_t>(merchantOrdinal(l, tile) * 3);
  for (uint8_t i = 2; i < st.count; ++i) {
    const uint8_t bitIndex = static_cast<uint8_t>(base + i - 2);
    st.lines[i].soldOut = bitIndex < 8 && (b[18] & (1u << bitIndex)) != 0;
  }
  // v0.3.2: a temple fair or a blight here sets the line's own price.
  for (uint8_t i = 0; i < st.count; ++i) {
    const uint16_t permille = sects::buyPermille(s, region, inventory::isHerb(st.lines[i].item));
    if (permille != 1000) st.lines[i].price = buyPrice(s, listPrice(s, st.lines[i].item), permille);
  }
  return st;
}

uint16_t stockPrice(const GameState& s, const inventory::Item item) { return buyPrice(s, listPrice(s, item)); }

uint8_t prizePercent(const Danger danger) {
  switch (danger) {
    case Danger::Hazardous:
      return 15;
    case Danger::Extreme:
      return 25;
    case Danger::Impossible:
      return 35;
    default:
      return 0;
  }
}

uint16_t affordable(const GameState& s, const inventory::Item item, const uint16_t unitPrice) {
  const uint16_t price = unitPrice != 0 ? unitPrice : stockPrice(s, item);
  if (price == 0 || !inventory::isIngredient(item)) return 0;
  const uint32_t byStones = s.progress.stones / price;
  const uint16_t room = inventory::room(s, item);
  return static_cast<uint16_t>(byStones < room ? byStones : room);
}

bool buy(GameState& s, const Layout& l, const uint8_t region, const uint8_t tile, const uint8_t line,
         const uint16_t count) {
  if (tile == kTraderTile) return buyFromTrader(s, l, region, line, count);
  const Stock st = stockFor(s, l, region, tile);
  if (line >= st.count || st.lines[line].soldOut || count == 0) return false;
  if (st.lines[line].limited && count != 1) return false;
  if (!st.lines[line].limited && count > affordable(s, st.lines[line])) return false;
  const uint32_t cost = static_cast<uint32_t>(linePrice(s, st.lines[line])) * count;  // v0.3.2: a fair or blight
  if (cost == 0 || s.progress.stones < cost) return false;
  s.progress.stones -= cost;
  if (st.lines[line].item == inventory::Item::RecipeSlip) {
    alchemy::learnSlip(s);
  } else {
    inventory::add(s, st.lines[line].item, count);
  }
  daily::note(s, daily::Task::Merchant);  // v0.2.6
  if (st.lines[line].limited) {
    const uint8_t bitIndex = static_cast<uint8_t>(merchantOrdinal(l, tile) * 3 + line - 2);
    if (bitIndex < 8) regionBytes(s, region)[18] = static_cast<uint8_t>(regionBytes(s, region)[18] | (1u << bitIndex));
  }
  return true;
}

uint16_t sellPrice(const GameState& s, const uint8_t region, const inventory::Item item) {
  const uint32_t p = static_cast<uint32_t>(inventory::sellPrice(item)) *
                     (region < kRegionCount ? sects::sellPermille(s, region, inventory::isHerb(item)) : 1000u) / 1000u;
  return static_cast<uint16_t>(p > 0xFFFFu ? 0xFFFFu : p);
}

bool sell(GameState& s, const inventory::Item item, const uint8_t region) {
  const uint16_t price = sellPrice(s, region, item);
  if (price == 0 || !inventory::remove(s, item)) return false;
  s.progress.stones += price;
  daily::note(s, daily::Task::Merchant);  // v0.2.6
  return true;
}

uint8_t prizeChance(const GameState& s, const Danger danger) {
  const uint8_t p = prizePercent(danger);
  return p == 0 ? 0 : static_cast<uint8_t>(p + daos::prizeChanceBonus(s));  // v0.2.6: Fate
}

int ownLevelBonus(const GameState& s) { return meridians::fightLevelBonus(s) + daily::fightLevelBonus(s); }

uint16_t fightDamagePermille(const GameState& s) {
  return static_cast<uint16_t>(static_cast<uint32_t>(meridians::fightDamagePermille(s)) * daos::fightDamagePermille(s) /
                               1000u);
}

}  // namespace explore
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
