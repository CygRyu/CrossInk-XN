#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Exploration.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace sects {
namespace {

constexpr uint32_t kKindFounding = 132;    // v0.3.2: the five sects of a world
constexpr uint32_t kKindWorldStep = 133;   // v0.3.2: one world year
constexpr uint32_t kKindWorldRumor = 135;  // v0.3.2: which story the rumor of the day follows

constexpr uint8_t kStepped = 0;
constexpr uint8_t kFlags = 2;
constexpr uint8_t kCond = 3;
constexpr uint8_t kCondRegion = 4;
constexpr uint8_t kCondEnd = 5;
constexpr uint8_t kCondWins = 7;
constexpr uint8_t kCondSect = 8;
constexpr uint8_t kCondOther = 9;
constexpr uint8_t kSectBase = 10;
constexpr uint8_t kSectSize = 10;
constexpr uint8_t kFounded = 0x01;

// Which stories each temper starts, by weight (Story 1..8).
constexpr uint8_t kStoryWeights[static_cast<int>(Temper::Count)][8] = {
    {4, 1, 1, 3, 1, 1, 3, 1},  // ambitious: feuds, tournaments, digging
    {1, 2, 3, 1, 2, 3, 1, 2},  // cautious: guarding roads and valleys
    {1, 2, 1, 1, 1, 3, 3, 2},  // reclusive: their own valleys and ruins
    {1, 1, 4, 2, 4, 2, 1, 2},  // righteous: bandits and fairs
    {4, 2, 1, 2, 0, 1, 2, 3},  // ruthless: feuds and hoarding
};

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t* p, const uint32_t v) {
  const uint32_t c = v > 0xFFFFu ? 0xFFFFu : v;
  p[0] = static_cast<uint8_t>(c);
  p[1] = static_cast<uint8_t>(c >> 8);
}
uint8_t* sectBytes(GameState& s, const uint8_t i) { return s.living.data() + kSectBase + kSectSize * i; }
const uint8_t* sectBytes(const GameState& s, const uint8_t i) { return s.living.data() + kSectBase + kSectSize * i; }

uint16_t yearsRead(const GameState& s) {
  const uint32_t y = s.progress.gameReadingSeconds / 3600u;
  return static_cast<uint16_t>(y > 0xFFFFu ? 0xFFFFu : y);
}

void clampStrength(uint8_t* b, const int delta) {
  int v = static_cast<int>(b[1]) + delta;
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  b[1] = static_cast<uint8_t>(v);
}

// A random opened region, or -1 when none is open (Body Tempering).
int openedRegion(const GameState& s, Rng& rng) {
  const uint8_t n = explore::unlockedCount(s);
  if (n == 0) return -1;
  return static_cast<int>(rng.below(n));
}

void found(GameState& s) {
  Rng rng(contentSeed(worldSeedOf(s), kKindFounding, 0));
  for (uint8_t i = 0; i < kSects; ++i) {
    uint8_t* b = sectBytes(s, i);
    for (uint8_t k = 0; k < kSectSize; ++k) b[k] = 0;
    b[0] = static_cast<uint8_t>(rng.below(static_cast<uint32_t>(Temper::Count)));
    b[1] = static_cast<uint8_t>(rng.range(3, 7));
    b[2] = static_cast<uint8_t>(rng.below(explore::kRegionCount));
  }
  s.living[kFlags] = static_cast<uint8_t>(s.living[kFlags] | kFounded);
}

void push(Steps& out, const Event& e) {
  if (out.count < kMaxEvents) {
    out.events[out.count++] = e;
  } else if (out.dropped < 0xFFFFu) {
    ++out.dropped;
  }
}

Story pickStory(const Temper t, Rng& rng) {
  const uint8_t* w = kStoryWeights[static_cast<int>(t) < static_cast<int>(Temper::Count) ? static_cast<int>(t) : 0];
  uint32_t total = 0;
  for (uint8_t k = 0; k < 8; ++k) total += w[k];
  uint32_t roll = rng.below(total);
  for (uint8_t k = 0; k < 8; ++k) {
    if (roll < w[k]) return static_cast<Story>(k + 1);
    roll -= w[k];
  }
  return Story::Feud;
}

void endCondition(GameState& s) {
  s.living[kCond] = 0;
  s.living[kCondRegion] = 0;
  put16(s.living.data() + kCondEnd, 0);
  s.living[kCondWins] = 0;
  s.living[kCondSect] = 0;
  s.living[kCondOther] = 0;
}

void stepYear(GameState& s, const uint16_t year, Steps& out) {
  Rng rng(contentSeed(worldSeedOf(s), kKindWorldStep, year));
  // A condition whose time is up ends first.
  if (s.living[kCond] != 0 && year >= get16(s.living.data() + kCondEnd)) {
    Event e;
    e.kind = EventKind::ConditionEnded;
    e.condition = static_cast<Condition>(s.living[kCond]);
    e.region = s.living[kCondRegion];
    e.sect = s.living[kCondSect];
    e.other = static_cast<int8_t>(s.living[kCondOther]) - 1;
    e.year = year;
    e.seed = sectBytes(s, e.sect < kSects ? e.sect : 0)[8];
    push(out, e);
    endCondition(s);
  }
  for (uint8_t i = 0; i < kSects; ++i) {
    uint8_t* b = sectBytes(s, i);
    const auto story = static_cast<Story>(b[3]);
    if (story == Story::None) {
      if (rng.below(kStartOneIn) != 0) continue;
      const Story next = pickStory(static_cast<Temper>(b[0]), rng);
      b[3] = static_cast<uint8_t>(next);
      b[4] = 0;
      b[5] = static_cast<uint8_t>(rng.range(3, 5));
      b[6] = 0;
      if (next == Story::Feud) b[6] = static_cast<uint8_t>((i + 1u + rng.below(kSects - 1u)) % kSects + 1u);
      // Stories that can turn a region happen in one of yours 60% of the time, else at the sect's
      // home (anywhere in the world, so the news is not always about the same few places).
      const bool near = rng.below(100) < kNearPercent;
      const int region = conditionOf(next) != Condition::None && near ? openedRegion(s, rng) : -1;
      b[7] = static_cast<uint8_t>(region >= 0 ? region : b[2]);
      b[8] = static_cast<uint8_t>(rng.next() & 0xFFu);
      Event e;
      e.kind = EventKind::Started;
      e.sect = i;
      e.other = static_cast<int8_t>(b[6]) - 1;
      e.story = next;
      e.region = b[7];
      e.year = year;
      e.seed = b[8];
      push(out, e);
      continue;
    }
    if (rng.below(kAdvanceOneIn) != 0) continue;
    b[4] = static_cast<uint8_t>(b[4] + 1u);
    if (b[4] == 1) {
      const Condition c = conditionOf(story);
      const bool roll = rng.below(100) < kConditionPercent;  // rolled every time, so the stream stays the same
      if (c != Condition::None && s.living[kCond] == 0 && explore::unlocked(s, b[7]) && roll) {
        s.living[kCond] = static_cast<uint8_t>(c);
        s.living[kCondRegion] = b[7];
        put16(s.living.data() + kCondEnd, static_cast<uint32_t>(year) + kConditionYears);
        s.living[kCondWins] = 0;
        s.living[kCondSect] = i;
        s.living[kCondOther] = b[6];
        Event e;
        e.kind = EventKind::ConditionBegan;
        e.sect = i;
        e.other = static_cast<int8_t>(b[6]) - 1;
        e.story = story;
        e.condition = c;
        e.region = b[7];
        e.year = year;
        e.seed = b[8];
        push(out, e);
      }
    }
    if (b[4] < b[5]) continue;
    // The end: good or bad by the sect's strength (strength 5: even odds).
    const bool good = rng.below(10) < b[1];
    Event e;
    e.kind = EventKind::Ended;
    e.sect = i;
    e.other = static_cast<int8_t>(b[6]) - 1;
    e.story = story;
    e.region = b[7];
    e.good = good;
    e.year = year;
    e.seed = b[8];
    push(out, e);
    switch (story) {
      case Story::Feud:
        clampStrength(b, good ? 1 : -1);
        if (e.other >= 0 && e.other < kSects) clampStrength(sectBytes(s, static_cast<uint8_t>(e.other)), -1);
        break;
      case Story::Succession:
        clampStrength(b, good ? -1 : -2);
        break;
      case Story::Tournament:
      case Story::Fair:
        clampStrength(b, good ? 1 : 0);
        break;
      default:
        clampStrength(b, good ? 1 : -1);
        break;
    }
    b[3] = 0;
    b[4] = 0;
    b[5] = 0;
    b[6] = 0;
    if (b[9] < 0xFF) ++b[9];
  }
}

}  // namespace

Condition conditionOf(const Story story) {
  switch (story) {
    case Story::Feud:
      return Condition::SectWar;
    case Story::Bandits:
      return Condition::BanditCamp;
    case Story::Tournament:
      return Condition::Tournament;
    case Story::Fair:
      return Condition::TempleFair;
    case Story::BeastTide:
      return Condition::BeastTide;
    case Story::Ruins:
      return Condition::Ruins;
    case Story::Blight:
      return Condition::Blight;
    default:
      return Condition::None;
  }
}

bool founded(const GameState& s) { return (s.living[kFlags] & kFounded) != 0; }

Sect sect(const GameState& s, const uint8_t index) {
  Sect out;
  if (index >= kSects) return out;
  const uint8_t* b = sectBytes(s, index);
  out.temper = static_cast<Temper>(b[0] < static_cast<uint8_t>(Temper::Count) ? b[0] : 0);
  out.strength = b[1];
  out.home = b[2];
  out.story = static_cast<Story>(b[3] < static_cast<uint8_t>(Story::Count) ? b[3] : 0);
  out.stage = b[4];
  out.stages = b[5];
  out.other = static_cast<int8_t>(b[6]) - 1;
  out.region = b[7];
  out.seed = b[8];
  out.finished = b[9];
  return out;
}

Steps advance(GameState& s) {
  Steps out;
  const uint16_t now = yearsRead(s);
  if (!founded(s)) {
    found(s);
    put16(s.living.data() + kStepped, now);
    return out;
  }
  uint16_t done = get16(s.living.data() + kStepped);
  if (now <= done) {
    if (now < done) put16(s.living.data() + kStepped, now);  // cannot happen in one life; be safe
    return out;
  }
  if (now - done > kMaxStepsPerVisit) done = static_cast<uint16_t>(now - kMaxStepsPerVisit);
  for (uint16_t y = static_cast<uint16_t>(done + 1u); y <= now && y != 0; ++y) {
    stepYear(s, y, out);
    ++out.years;
  }
  put16(s.living.data() + kStepped, now);
  return out;
}

Condition condition(const GameState& s, uint8_t* region) {
  const uint8_t c = s.living[kCond];
  if (c == 0 || c >= static_cast<uint8_t>(Condition::Count)) return Condition::None;
  if (region != nullptr) *region = s.living[kCondRegion];
  return static_cast<Condition>(c);
}

Condition conditionIn(const GameState& s, const uint8_t region) {
  uint8_t r = 0;
  const Condition c = condition(s, &r);
  return c != Condition::None && r == region ? c : Condition::None;
}

uint16_t conditionYearsLeft(const GameState& s) {
  if (condition(s) == Condition::None) return 0;
  const uint16_t end = get16(s.living.data() + kCondEnd);
  const uint16_t now = yearsRead(s);
  return end > now ? static_cast<uint16_t>(end - now) : 0;
}

uint8_t conditionSect(const GameState& s) { return s.living[kCondSect] < kSects ? s.living[kCondSect] : 0; }

int8_t conditionOther(const GameState& s) {
  const int v = static_cast<int>(s.living[kCondOther]) - 1;
  return static_cast<int8_t>(v >= 0 && v < kSects ? v : -1);
}

int enemyLevelBonus(const GameState& s, const uint8_t region) {
  return conditionIn(s, region) == Condition::SectWar ? 2 : 0;
}

uint16_t fightStonesPermille(const GameState& s, const uint8_t region) {
  return conditionIn(s, region) == Condition::SectWar ? 1500 : 1000;
}

uint8_t encounterPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  return conditionIn(s, region) == Condition::BeastTide ? static_cast<uint8_t>(base * 3u / 2u) : base;
}

uint8_t meetPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  const Condition c = conditionIn(s, region);
  return c == Condition::Tournament || c == Condition::TempleFair ? static_cast<uint8_t>(base * 2u) : base;
}

uint8_t favouredEnemy(const GameState& s, const uint8_t region) {
  switch (conditionIn(s, region)) {
    case Condition::SectWar:
      return static_cast<uint8_t>(explore::Enemy::Cultivator);
    case Condition::BanditCamp:
      return static_cast<uint8_t>(explore::Enemy::Bandit);
    default:
      return 0xFF;
  }
}

uint8_t beastLootPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  if (conditionIn(s, region) != Condition::BeastTide) return base;
  return static_cast<uint8_t>(base * 2u > 100u ? 100u : base * 2u);
}

uint8_t ruinBonus(const GameState& s, const uint8_t region) {
  return conditionIn(s, region) == Condition::Ruins ? 15 : 0;
}

bool herbsHalved(const GameState& s, const uint8_t region) { return conditionIn(s, region) == Condition::Blight; }

uint16_t buyPermille(const GameState& s, const uint8_t region, const bool herb) {
  const Condition c = conditionIn(s, region);
  if (c == Condition::TempleFair) return 850;
  if (c == Condition::Blight && herb) return 1500;
  return 1000;
}

uint16_t sellPermille(const GameState& s, const uint8_t region, const bool herb) {
  return conditionIn(s, region) == Condition::Blight && herb ? 1500 : 1000;
}

bool tournamentOn(const GameState& s) { return condition(s) == Condition::Tournament; }

CampResult banditWon(GameState& s, const uint8_t region) {
  CampResult r;
  if (conditionIn(s, region) != Condition::BanditCamp) return r;
  if (s.living[kCondWins] < 0xFF) ++s.living[kCondWins];
  if (s.living[kCondWins] < kCampWinsToBreak) return r;
  r.broken = true;
  r.stones = 50u * (region + 1u);
  r.sect = conditionSect(s);
  endCondition(s);
  return r;
}

int rumorSect(const GameState& s, const uint32_t when) {
  if (!founded(s)) return -1;
  uint8_t live[kSects];
  uint8_t n = 0;
  for (uint8_t i = 0; i < kSects; ++i) {
    if (sectBytes(s, i)[3] != 0) live[n++] = i;
  }
  if (n == 0) return -1;
  // A live condition's story first, so the rumor explains what the region is going through.
  if (condition(s) != Condition::None) return conditionSect(s);
  return live[Rng(contentSeed(worldSeedOf(s), kKindWorldRumor, when)).below(n)];
}

}  // namespace sects
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
