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
constexpr uint32_t kKindPersonSect = 137;  // v0.3.3: whose sect a person is
constexpr uint32_t kKindYouRumor = 138;    // v0.3.3: which sect's opinion of you a rumor tells

constexpr uint8_t kStepped = 0;
constexpr uint8_t kFlags = 2;
constexpr uint8_t kCond = 3;
constexpr uint8_t kCondRegion = 4;
constexpr uint8_t kCondEnd = 5;
constexpr uint8_t kCondTally = 7;
constexpr uint8_t kCondSect = 8;
constexpr uint8_t kCondOther = 9;
constexpr uint8_t kSectBase = 10;
constexpr uint8_t kSectSize = 10;
constexpr uint8_t kAlliances = 60;  // u16, one bit per pair
constexpr uint8_t kFounded = 0x01;
constexpr uint8_t kLayout033 = 0x02;

constexpr uint8_t kPeopleMask = 0x03;
constexpr uint8_t kDentedBit = 0x04;
constexpr uint8_t kStandingZero = 5;  // standing + 5 in the high nibble

constexpr uint8_t kStoryTypes = static_cast<uint8_t>(Story::Count) - 1;

// Which stories each temper starts, by weight (Story 1..12).
constexpr uint8_t kStoryWeights[static_cast<int>(Temper::Count)][kStoryTypes] = {
    // feud succ band tour fair tide ruin blight inher demon ally gate
    {4, 1, 1, 3, 1, 1, 3, 1, 3, 0, 2, 1},  // ambitious: feuds, tournaments, digging, inheritances
    {1, 2, 3, 1, 2, 3, 1, 2, 1, 1, 2, 1},  // cautious: guarding roads and valleys
    {1, 2, 1, 1, 1, 3, 3, 2, 2, 1, 0, 2},  // reclusive: their own valleys and ruins
    {1, 1, 4, 2, 4, 2, 1, 2, 1, 3, 1, 1},  // righteous: bandits, fairs, demonic cults
    {4, 2, 1, 2, 0, 1, 2, 3, 2, 1, 1, 1},  // ruthless: feuds and hoarding
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

int8_t standingOf(const uint8_t* b) { return static_cast<int8_t>(static_cast<int>(b[9] >> 4) - kStandingZero); }
void moveStanding(uint8_t* b, const int delta) {
  int v = standingOf(b) + delta;
  if (v < kStandingMin) v = kStandingMin;
  if (v > kStandingMax) v = kStandingMax;
  b[9] = static_cast<uint8_t>((b[9] & 0x0F) | ((v + kStandingZero) << 4));
}

// The bit of the pair a < b in the alliance word (10 pairs).
int pairBit(uint8_t a, uint8_t b) {
  if (a == b || a >= kSects || b >= kSects) return -1;
  if (a > b) {
    const uint8_t t = a;
    a = b;
    b = t;
  }
  static const uint8_t kStart[kSects] = {0, 4, 7, 9, 10};
  return kStart[a] + (b - a - 1);
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
    b[9] = kStandingZero << 4;
  }
  s.living[kFlags] = static_cast<uint8_t>(s.living[kFlags] | kFounded | kLayout033);
}

// v0.3.2 kept "stories finished" in byte +9; v0.3.3 keeps your standing there.
void upgradeLayout(GameState& s) {
  if ((s.living[kFlags] & kLayout033) != 0) return;
  for (uint8_t i = 0; i < kSects; ++i) sectBytes(s, i)[9] = kStandingZero << 4;
  s.living[kFlags] = static_cast<uint8_t>(s.living[kFlags] | kLayout033);
}

void push(Steps& out, const Event& e) {
  if (out.count < kMaxEvents) {
    out.events[out.count++] = e;
  } else if (out.dropped < 0xFFFFu) {
    ++out.dropped;
  }
}
void push(Influence& out, const Event& e) {
  if (out.count < 2) out.events[out.count++] = e;
}

Story pickStory(const Temper t, Rng& rng) {
  const int ti = static_cast<int>(t) < static_cast<int>(Temper::Count) ? static_cast<int>(t) : 0;
  const uint8_t* w = kStoryWeights[ti];
  uint32_t total = 0;
  for (uint8_t k = 0; k < kStoryTypes; ++k) total += w[k];
  uint32_t roll = rng.below(total);
  for (uint8_t k = 0; k < kStoryTypes; ++k) {
    if (roll < w[k]) return static_cast<Story>(k + 1);
    roll -= w[k];
  }
  return Story::Feud;
}

void endCondition(GameState& s) {
  s.living[kCond] = 0;
  s.living[kCondRegion] = 0;
  put16(s.living.data() + kCondEnd, 0);
  s.living[kCondTally] = 0;
  s.living[kCondSect] = 0;
  s.living[kCondOther] = 0;
}

Event eventOf(const GameState& s, const EventKind kind, const uint8_t i, const uint16_t year) {
  const uint8_t* b = sectBytes(s, i);
  Event e;
  e.kind = kind;
  e.sect = i;
  e.other = static_cast<int8_t>(b[6]) - 1;
  e.story = static_cast<Story>(b[3] < static_cast<uint8_t>(Story::Count) ? b[3] : 0);
  e.region = b[7];
  e.year = year;
  e.seed = b[8];
  return e;
}

// A story's end and what it does to strength (and, for an alliance and a
// move, to the world). Clears the story.
void finishStory(GameState& s, const uint8_t i, const bool good) {
  uint8_t* b = sectBytes(s, i);
  const auto story = static_cast<Story>(b[3]);
  const int other = static_cast<int>(b[6]) - 1;
  switch (story) {
    case Story::Feud:
      clampStrength(b, good ? 1 : -1);
      if (other >= 0 && other < kSects) {
        clampStrength(sectBytes(s, static_cast<uint8_t>(other)), -1);
        // v0.3.3: a feud between allies costs both of them one more.
        if (allied(s, i, static_cast<uint8_t>(other))) {
          clampStrength(b, -1);
          clampStrength(sectBytes(s, static_cast<uint8_t>(other)), -1);
        }
      }
      break;
    case Story::Succession:
      clampStrength(b, good ? -1 : -2);
      break;
    case Story::Tournament:
    case Story::Fair:
      clampStrength(b, good ? 1 : 0);
      break;
    case Story::Alliance: {
      // A good end seals the alliance (both +1); a bad one breaks it off (no change).
      const int bit = other >= 0 ? pairBit(i, static_cast<uint8_t>(other)) : -1;
      if (good && bit >= 0) {
        put16(s.living.data() + kAlliances, get16(s.living.data() + kAlliances) | (1u << bit));
        clampStrength(b, 1);
        clampStrength(sectBytes(s, static_cast<uint8_t>(other)), 1);
      }
      break;
    }
    case Story::GateMove:
      // A good end: the gate stands in the story's region now. A bad one: the move failed.
      if (good) {
        b[2] = b[7];
      } else {
        clampStrength(b, -1);
      }
      break;
    default:
      clampStrength(b, good ? 1 : -1);
      break;
  }
  b[3] = 0;
  b[4] = 0;
  b[5] = 0;
  b[6] = 0;
  b[9] = static_cast<uint8_t>(b[9] & ~(kPeopleMask | kDentedBit));  // a new story, a new count
}

void stepYear(GameState& s, const uint16_t year, Steps& out) {
  Rng rng(contentSeed(worldSeedOf(s), kKindWorldStep, year));
  // An inheritance: sects keep arriving (2 in 3 a year); the third to arrive,
  // or the end of the time, and the story's sect takes it.
  const bool racing = s.living[kCond] == static_cast<uint8_t>(Condition::Inheritance);
  const bool arrives = rng.below(3) < kArriveOneIn;  // rolled every year, so the stream stays the same
  if (racing && arrives && s.living[kCondTally] < kArrivalsToClaim) ++s.living[kCondTally];
  // A condition whose time is up (or whose race is over) ends first.
  if (s.living[kCond] != 0 &&
      (year >= get16(s.living.data() + kCondEnd) || (racing && s.living[kCondTally] >= kArrivalsToClaim))) {
    const uint8_t owner = s.living[kCondSect] < kSects ? s.living[kCondSect] : 0;
    Event e = eventOf(s, EventKind::ConditionEnded, owner, year);
    e.condition = static_cast<Condition>(s.living[kCond]);
    e.region = s.living[kCondRegion];
    e.other = static_cast<int8_t>(s.living[kCondOther]) - 1;
    push(out, e);
    // The race is lost to the sect: its story ends at once, well.
    if (racing && static_cast<Story>(sectBytes(s, owner)[3]) == Story::Inheritance) {
      Event end = eventOf(s, EventKind::Ended, owner, year);
      end.good = true;
      push(out, end);
      finishStory(s, owner, true);
    }
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
      if (next == Story::Feud || next == Story::Alliance) {
        b[6] = static_cast<uint8_t>((i + 1u + rng.below(kSects - 1u)) % kSects + 1u);
      }
      // Stories that can turn a region happen in one of yours 60% of the time, else at the sect's
      // home (anywhere in the world, so the news is not always about the same few places). A sect
      // moving its gate moves it to a region of its own choosing.
      const bool near = rng.below(100) < kNearPercent;
      int region = conditionOf(next) != Condition::None && near ? openedRegion(s, rng) : -1;
      if (next == Story::GateMove) {
        region = static_cast<int>(rng.below(explore::kRegionCount));
        if (region == b[2]) region = (region + 1) % explore::kRegionCount;
      }
      b[7] = static_cast<uint8_t>(region >= 0 ? region : b[2]);
      b[8] = static_cast<uint8_t>(rng.next() & 0xFFu);
      push(out, eventOf(s, EventKind::Started, i, year));
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
        s.living[kCondTally] = 0;
        s.living[kCondSect] = i;
        s.living[kCondOther] = b[6];
        Event e = eventOf(s, EventKind::ConditionBegan, i, year);
        e.condition = c;
        push(out, e);
      }
    }
    // A story whose condition is still on waits for it to end (or for you).
    if (b[4] < b[5] || (s.living[kCond] != 0 && s.living[kCondSect] == i)) {
      if (b[4] >= b[5]) b[4] = static_cast<uint8_t>(b[5] - 1u);
      continue;
    }
    // The end: good or bad by the sect's strength (strength 5: even odds).
    const bool good = rng.below(10) < b[1];
    Event e = eventOf(s, EventKind::Ended, i, year);
    e.good = good;
    push(out, e);
    finishStory(s, i, good);
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
    case Story::Inheritance:
      return Condition::Inheritance;
    case Story::Demonic:
      return Condition::Demonic;
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
  out.standing = (s.living[kFlags] & kLayout033) != 0 ? standingOf(b) : 0;
  return out;
}

int8_t standing(const GameState& s, const uint8_t index) { return index < kSects ? sect(s, index).standing : 0; }

bool allied(const GameState& s, const uint8_t a, const uint8_t b) {
  const int bit = pairBit(a, b);
  return bit >= 0 && (get16(s.living.data() + kAlliances) & (1u << bit)) != 0;
}

Steps advance(GameState& s) {
  Steps out;
  const uint16_t now = yearsRead(s);
  if (!founded(s)) {
    found(s);
    put16(s.living.data() + kStepped, now);
    return out;
  }
  upgradeLayout(s);
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

uint8_t conditionTally(const GameState& s) { return s.living[kCondTally]; }

namespace {
bool warLike(const Condition c) { return c == Condition::SectWar || c == Condition::Demonic; }
}  // namespace

int enemyLevelBonus(const GameState& s, const uint8_t region) { return warLike(conditionIn(s, region)) ? 2 : 0; }

uint16_t fightStonesPermille(const GameState& s, const uint8_t region) {
  return warLike(conditionIn(s, region)) ? 1500 : 1000;
}

uint8_t encounterPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  return conditionIn(s, region) == Condition::BeastTide ? static_cast<uint8_t>(base * 3u / 2u) : base;
}

uint8_t meetPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  const Condition c = conditionIn(s, region);
  return c == Condition::Tournament || c == Condition::TempleFair ? static_cast<uint8_t>(base * 2u) : base;
}

uint8_t favouredEnemy(const GameState& s, const uint8_t region) {
  const Condition c = conditionIn(s, region);
  if (warLike(c)) return static_cast<uint8_t>(explore::Enemy::Cultivator);
  if (c == Condition::BanditCamp) return static_cast<uint8_t>(explore::Enemy::Bandit);
  return 0xFF;
}

uint8_t beastLootPercent(const GameState& s, const uint8_t region, const uint8_t base) {
  if (conditionIn(s, region) != Condition::BeastTide) return base;
  return static_cast<uint8_t>(base * 2u > 100u ? 100u : base * 2u);
}

uint8_t ruinBonus(const GameState& s, const uint8_t region) {
  const Condition c = conditionIn(s, region);
  return c == Condition::Ruins || c == Condition::Inheritance ? 15 : 0;
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

// --- People and influence ---------------------------------------------------------------------

int8_t personSect(const GameState& s, const uint8_t region, const uint32_t seed) {
  if (!founded(s)) return -1;
  const uint32_t pick = contentSeed(worldSeedOf(s), kKindPersonSect, seed);
  if (region != kNoRegion) {
    const Condition c = conditionIn(s, region);
    if (c == Condition::Demonic) return kCult;
    if (c == Condition::SectWar) {
      const int other = conditionOther(s);
      if (other >= 0) return static_cast<int8_t>((pick & 1u) != 0 ? conditionSect(s) : other);
    }
  }
  return static_cast<int8_t>(pick % kSects);
}

Influence cultivatorBeaten(GameState& s, const uint8_t region, const int8_t who) {
  Influence r;
  if (!founded(s)) return r;
  upgradeLayout(s);
  const uint16_t now = yearsRead(s);
  const Condition c = region != kNoRegion ? conditionIn(s, region) : Condition::None;
  if (c == Condition::Demonic && who == kCult) {
    // Followers of the cult: the third beaten drives it off; the sect fighting it thanks you.
    const uint8_t owner = conditionSect(s);
    if (++s.living[kCondTally] < kWarWinsToDecide) return r;
    Event e = eventOf(s, EventKind::CultBroken, owner, now);
    e.condition = c;
    e.region = region;
    push(r, e);
    moveStanding(sectBytes(s, owner), 1);
    if (static_cast<Story>(sectBytes(s, owner)[3]) == Story::Demonic) {
      Event end = eventOf(s, EventKind::Ended, owner, now);
      end.good = true;
      push(r, end);
      finishStory(s, owner, true);
    }
    endCondition(s);
    return r;
  }
  if (c == Condition::SectWar && who >= 0) {
    // Three of one side's people decide the war for the other side (user: by whom you fight).
    const uint8_t owner = conditionSect(s);
    const int other = conditionOther(s);
    if (other < 0) return r;
    uint8_t& tally = s.living[kCondTally];
    const bool againstOwner = who == owner;
    uint8_t lo = static_cast<uint8_t>(tally & 0x0F);
    uint8_t hi = static_cast<uint8_t>(tally >> 4);
    uint8_t& count = againstOwner ? lo : hi;
    if (count < 15) ++count;
    tally = static_cast<uint8_t>(lo | (hi << 4));
    if (count < kWarWinsToDecide) return r;
    const uint8_t loser = static_cast<uint8_t>(who);
    const uint8_t winner = againstOwner ? static_cast<uint8_t>(other) : owner;
    Event e = eventOf(s, EventKind::WarDecided, winner, now);
    e.other = static_cast<int8_t>(loser);
    e.condition = c;
    e.region = region;
    push(r, e);
    moveStanding(sectBytes(s, winner), 1);
    moveStanding(sectBytes(s, loser), -1);
    // The feud ends now: well if its own sect won.
    if (static_cast<Story>(sectBytes(s, owner)[3]) == Story::Feud) {
      Event end = eventOf(s, EventKind::Ended, owner, now);
      end.good = winner == owner;
      push(r, end);
      finishStory(s, owner, end.good);
    }
    endCondition(s);
    return r;
  }
  if (who < 0 || who >= kSects) return r;
  // Outside a war: three of a sect's people dent it once a story.
  uint8_t* b = sectBytes(s, static_cast<uint8_t>(who));
  if ((b[9] & kDentedBit) != 0) return r;
  const uint8_t people = static_cast<uint8_t>((b[9] & kPeopleMask) + 1u);
  if (people < kPeopleToDent) {
    b[9] = static_cast<uint8_t>((b[9] & ~kPeopleMask) | people);
    return r;
  }
  b[9] = static_cast<uint8_t>((b[9] & ~kPeopleMask) | kDentedBit);
  clampStrength(b, -1);
  moveStanding(b, -1);
  Event e = eventOf(s, EventKind::Dented, static_cast<uint8_t>(who), now);
  e.region = region != kNoRegion ? region : b[2];
  push(r, e);
  return r;
}

Influence banditBeaten(GameState& s, const uint8_t region) {
  Influence r;
  if (conditionIn(s, region) != Condition::BanditCamp) return r;
  upgradeLayout(s);
  if (s.living[kCondTally] < 0xFF) ++s.living[kCondTally];
  if (s.living[kCondTally] < kCampWinsToBreak) return r;
  const uint16_t now = yearsRead(s);
  const uint8_t owner = conditionSect(s);
  r.stones = 50u * (region + 1u);
  r.tonic = true;
  Event e = eventOf(s, EventKind::CampBroken, owner, now);
  e.condition = Condition::BanditCamp;
  e.region = region;
  push(r, e);
  moveStanding(sectBytes(s, owner), 1);
  // v0.3.3: the bandits story ends with the camp, well.
  if (static_cast<Story>(sectBytes(s, owner)[3]) == Story::Bandits) {
    Event end = eventOf(s, EventKind::Ended, owner, now);
    end.good = true;
    push(r, end);
    finishStory(s, owner, true);
  }
  endCondition(s);
  return r;
}

Influence beastBeaten(GameState& s, const uint8_t region) {
  Influence r;
  if (conditionIn(s, region) != Condition::BeastTide) return r;
  upgradeLayout(s);
  if (s.living[kCondTally] < 0xFF) ++s.living[kCondTally];
  if (s.living[kCondTally] < kTideBeastsToEnd) return r;
  const uint16_t now = yearsRead(s);
  const uint8_t owner = conditionSect(s);
  r.herbs = 2;
  r.ores = 2;
  Event e = eventOf(s, EventKind::TideBroken, owner, now);
  e.condition = Condition::BeastTide;
  e.region = region;
  push(r, e);
  moveStanding(sectBytes(s, owner), 1);
  if (static_cast<Story>(sectBytes(s, owner)[3]) == Story::BeastTide) {
    Event end = eventOf(s, EventKind::Ended, owner, now);
    end.good = true;
    push(r, end);
    finishStory(s, owner, true);
  }
  endCondition(s);
  return r;
}

Influence searchWon(GameState& s, const uint8_t region) {
  Influence r;
  if (conditionIn(s, region) != Condition::Inheritance) return r;
  upgradeLayout(s);
  const uint16_t now = yearsRead(s);
  const uint8_t owner = conditionSect(s);
  r.treasure = true;
  r.stones = 100u * (region + 1u);
  Event e = eventOf(s, EventKind::YouTookInheritance, owner, now);
  e.condition = Condition::Inheritance;
  e.region = region;
  push(r, e);
  moveStanding(sectBytes(s, owner), -1);  // you took it from under their noses
  if (static_cast<Story>(sectBytes(s, owner)[3]) == Story::Inheritance) {
    Event end = eventOf(s, EventKind::Ended, owner, now);
    end.good = false;
    push(r, end);
    finishStory(s, owner, false);
  }
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
  if (condition(s) != Condition::None && sectBytes(s, conditionSect(s))[3] != 0) return conditionSect(s);
  return live[Rng(contentSeed(worldSeedOf(s), kKindWorldRumor, when)).below(n)];
}

int standingRumorSect(const GameState& s, const uint32_t when) {
  if (!founded(s) || (s.living[kFlags] & kLayout033) == 0) return -1;
  uint8_t strong[kSects];
  uint8_t n = 0;
  for (uint8_t i = 0; i < kSects; ++i) {
    const int8_t v = standingOf(sectBytes(s, i));
    if (v >= 3 || v <= -3) strong[n++] = i;
  }
  if (n == 0) return -1;
  return strong[Rng(contentSeed(worldSeedOf(s), kKindYouRumor, when)).below(n)];
}

}  // namespace sects
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
