#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Daily.h"
#include "Daos.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace daos {
namespace {

constexpr uint32_t kKindDaoTie = 121;  // contentSeed kind: which Dao wins a tie for a slot
constexpr uint8_t kFoundOffset = 0;
constexpr uint8_t kBucketOffset = 2;
constexpr uint8_t kLong60Offset = 10;
constexpr uint8_t kLong3hOffset = 11;
constexpr uint8_t kAnnouncedOffset = 12;
constexpr uint8_t kFlagsOffset = 17;
constexpr uint8_t kSettledWritten = 0x01;
constexpr uint16_t kUnitsPerHour = 6;  // 10-minute units

// Session 10 (user): slower steps than the first proposal, so levels grow
// through the Mortal Plane (level 9 at about 200-400 reading hours for a
// reader of that habit) and Dao keys arrive around Soul Transformation.
constexpr Rule kRules[kCount] = {
    {14, 50},  // Time: days of longest streak
    {8, 20},   // Stillness: days with 60+ minutes
    {3, 8},    // Tide: days with 3+ hours
    {10, 25},  // Flowing Water: hours at night
    {10, 25},  // Rising Sun: hours in the morning
    {10, 25},  // Fire: hours in the afternoon
    {10, 25},  // Metal: hours in the evening
    {15, 25},  // Myriad Paths: hours read
    {2, 3},    // Fate: books finished
};

uint8_t* bytes(GameState& s) { return s.daosAndMeridians.data(); }
const uint8_t* bytes(const GameState& s) { return s.daosAndMeridians.data(); }

uint16_t foundBits(const GameState& s) {
  return static_cast<uint16_t>(bytes(s)[kFoundOffset] | (bytes(s)[kFoundOffset + 1] << 8));
}
void setFoundBits(GameState& s, const uint16_t v) {
  bytes(s)[kFoundOffset] = static_cast<uint8_t>(v & 0xFF);
  bytes(s)[kFoundOffset + 1] = static_cast<uint8_t>(v >> 8);
}

// The bucket of a time-of-day Dao (Morning 0 .. Night 3), or 4 for the others.
uint8_t bucketOf(const Dao d) {
  switch (d) {
    case Dao::Sun:
      return 0;
    case Dao::Fire:
      return 1;
    case Dao::Metal:
      return 2;
    case Dao::Water:
      return 3;
    default:
      return 4;
  }
}

uint8_t announced(const GameState& s, const uint8_t i) {
  const uint8_t b = bytes(s)[kAnnouncedOffset + i / 2];
  return (i % 2 == 0) ? (b & 0x0F) : (b >> 4);
}
void setAnnounced(GameState& s, const uint8_t i, const uint8_t level) {
  uint8_t& b = bytes(s)[kAnnouncedOffset + i / 2];
  b = (i % 2 == 0) ? static_cast<uint8_t>((b & 0xF0) | (level & 0x0F))
                   : static_cast<uint8_t>((b & 0x0F) | ((level & 0x0F) << 4));
}

uint16_t largestBucket(const GameState& s) {
  uint16_t top = 0;
  for (uint8_t b = 0; b < 4; ++b) {
    const uint16_t u = bucketUnits(s, b);
    if (u > top) top = u;
  }
  return top;
}

// The measure in fine units: 10-minute units for hour-based Daos, else the measure itself.
uint32_t fineMeasure(const GameState& s, const Dao d) {
  const uint8_t b = bucketOf(d);
  if (b < 4) return bucketUnits(s, b);
  if (d == Dao::Myriad) return trackedUnits(s);
  return measure(s, d);
}
uint32_t fineScale(const Dao d) { return (bucketOf(d) < 4 || d == Dao::Myriad) ? kUnitsPerHour : 1u; }

}  // namespace

const Rule& ruleOf(const Dao dao) { return kRules[static_cast<uint8_t>(dao) < kCount ? static_cast<uint8_t>(dao) : 0]; }

bool found(const GameState& s, const Dao dao) {
  return static_cast<uint8_t>(dao) < kCount && (foundBits(s) & (1u << static_cast<uint8_t>(dao))) != 0;
}

uint8_t foundCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n += found(s, static_cast<Dao>(i)) ? 1 : 0;
  return n;
}

uint8_t slots(const GameState& s) {
  if (trackedUnits(s) < kTrackedUnitsForSlots) return 0;
  const uint8_t realm = s.progress.realm;
  if (realm < 1) return 0;
  const uint8_t n = static_cast<uint8_t>(2u + (realm >= 2) + (realm >= 3) + (realm >= 4));
  return n;
}

bool settled(const GameState& s) { return foundCount(s) >= kPerLife; }

uint16_t bucketUnits(const GameState& s, const uint8_t bucket) {
  if (bucket >= 4) return 0;
  const uint8_t* p = bytes(s) + kBucketOffset + 2 * bucket;
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t trackedUnits(const GameState& s) {
  uint32_t total = 0;
  for (uint8_t b = 0; b < 4; ++b) total += bucketUnits(s, b);
  return total;
}

uint8_t daysOver60(const GameState& s) { return bytes(s)[kLong60Offset]; }
uint8_t daysOver3h(const GameState& s) { return bytes(s)[kLong3hOffset]; }

void addLongDay(GameState& s, const uint32_t seconds) {
  if (seconds >= 3600u && bytes(s)[kLong60Offset] < 0xFF) ++bytes(s)[kLong60Offset];
  if (seconds >= 3u * 3600u && bytes(s)[kLong3hOffset] < 0xFF) ++bytes(s)[kLong3hOffset];
}

uint32_t measure(const GameState& s, const Dao dao) {
  switch (dao) {
    case Dao::Time:
      return daily::longestStreak(s);
    case Dao::Stillness:
      return daysOver60(s);
    case Dao::Tide:
      return daysOver3h(s);
    case Dao::Myriad:
      return trackedUnits(s) / kUnitsPerHour;
    case Dao::Fate: {
      // Books finished since the character was created (unmarking one lowers it).
      const uint32_t now = s.snapshot.completedBooks;
      const uint32_t base = s.baseline.completedBooks;
      return now > base ? now - base : 0u;
    }
    default:
      return bucketUnits(s, bucketOf(dao)) / kUnitsPerHour;
  }
}

uint32_t progressPermille(const GameState& s, const Dao dao) {
  const Rule& r = ruleOf(dao);
  const uint32_t need = static_cast<uint32_t>(r.condition) * fineScale(dao);
  uint64_t p = static_cast<uint64_t>(fineMeasure(s, dao)) * 1000u / need;
  const uint32_t total = trackedUnits(s);
  const uint8_t b = bucketOf(dao);
  if (b < 4) {
    // Dominance: the full measure counts only at a 40% share or more.
    const uint32_t share =
        total > 0 ? static_cast<uint32_t>(static_cast<uint64_t>(bucketUnits(s, b)) * 1000u / total) : 0u;
    const uint32_t factor = share >= kSharePercent * 10u ? 1000u : share * 1000u / (kSharePercent * 10u);
    p = p * factor / 1000u;
  } else if (dao == Dao::Myriad) {
    // Balance: lowered (squared) while one time of day holds more than 40%.
    const uint32_t top = largestBucket(s);
    if (total == 0 || top == 0) return 0;
    const uint32_t topShare = static_cast<uint32_t>(static_cast<uint64_t>(top) * 1000u / total);
    if (topShare > kSharePercent * 10u) {
      const uint64_t factor = static_cast<uint64_t>(kSharePercent * 10u) * 1000u / topShare;  // < 1000
      p = p * factor / 1000u * factor / 1000u;
    }
  }
  return p > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(p);
}

bool conditionMet(const GameState& s, const Dao dao) {
  const Rule& r = ruleOf(dao);
  if (fineMeasure(s, dao) < static_cast<uint32_t>(r.condition) * fineScale(dao)) return false;
  const uint32_t total = trackedUnits(s);
  const uint8_t b = bucketOf(dao);
  if (b < 4) return static_cast<uint64_t>(bucketUnits(s, b)) * 100u >= static_cast<uint64_t>(total) * kSharePercent;
  if (dao == Dao::Myriad) {
    return static_cast<uint64_t>(largestBucket(s)) * 100u <= static_cast<uint64_t>(total) * kSharePercent;
  }
  return true;
}

uint8_t level(const GameState& s, const Dao dao) {
  if (!found(s, dao)) return 0;
  const Rule& r = ruleOf(dao);
  const uint32_t scale = fineScale(dao);
  const uint32_t m = fineMeasure(s, dao);
  const uint32_t cond = static_cast<uint32_t>(r.condition) * scale;
  const uint32_t extra = m > cond ? (m - cond) / (static_cast<uint32_t>(r.step) * scale) : 0u;
  return static_cast<uint8_t>(extra + 1u >= kMaxLevel ? kMaxLevel : extra + 1u);
}

uint32_t nextLevelAt(const GameState& s, const Dao dao) {
  const uint8_t lv = level(s, dao);
  if (lv == 0 || lv >= kMaxLevel) return 0;
  const Rule& r = ruleOf(dao);
  return static_cast<uint32_t>(r.condition) + static_cast<uint32_t>(r.step) * lv;
}

void addReading(GameState& s, const VisitGains& g, const StatsSnapshot& after) {
  if (g.resetDetected) return;
  for (uint8_t b = 0; b < 4; ++b) {
    const uint32_t now = after.timeOfDaySeconds[b];
    const uint32_t delta = g.timeOfDaySeconds[b];
    const uint32_t before = now >= delta ? now - delta : 0u;
    const uint32_t steps = now / 600u - before / 600u;
    const uint32_t units = bucketUnits(s, b) + steps;
    const uint16_t v = static_cast<uint16_t>(units > 0xFFFFu ? 0xFFFFu : units);
    uint8_t* p = bytes(s) + kBucketOffset + 2 * b;
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
}

News settle(GameState& s) {
  News n;
  const uint8_t open = slots(s);
  while (foundCount(s) < open && foundCount(s) < kPerLife) {
    int best = -1;
    uint32_t bestProgress = 0;
    uint32_t bestTie = 0;
    for (uint8_t i = 0; i < kCount; ++i) {
      const Dao d = static_cast<Dao>(i);
      if (found(s, d) || !conditionMet(s, d)) continue;
      const uint32_t p = progressPermille(s, d);
      const uint32_t tie = contentSeed(worldSeedOf(s), kKindDaoTie, i);
      if (best < 0 || p > bestProgress || (p == bestProgress && tie > bestTie)) {
        best = i;
        bestProgress = p;
        bestTie = tie;
      }
    }
    if (best < 0) break;
    setFoundBits(s, static_cast<uint16_t>(foundBits(s) | (1u << best)));
    n.foundDaos[n.foundCount++] = static_cast<Dao>(best);
  }
  for (uint8_t i = 0; i < kCount; ++i) {
    const Dao d = static_cast<Dao>(i);
    if (!found(s, d)) continue;
    const uint8_t lv = level(s, d);
    const uint8_t was = announced(s, i);
    if (lv <= was) continue;
    if (was != 0) {  // a newly found Dao is announced as found, at whatever level
      n.levelDaos[n.levelCount] = d;
      n.levels[n.levelCount] = lv;
      ++n.levelCount;
    }
    setAnnounced(s, i, lv);
  }
  if (settled(s) && (bytes(s)[kFlagsOffset] & kSettledWritten) == 0) {
    bytes(s)[kFlagsOffset] |= kSettledWritten;
    n.settledNow = true;
  }
  return n;
}

uint8_t keyLevelFor(const uint8_t target) {
  const uint32_t need = static_cast<uint32_t>(target) + 2u;
  return static_cast<uint8_t>(need > kMaxLevel ? kMaxLevel : need);
}

Dao keyFor(const GameState& s, const uint8_t target) {
  const uint8_t need = keyLevelFor(target);
  Dao best = Dao::Count;
  uint8_t bestLevel = 0;
  for (uint8_t i = 0; i < kCount; ++i) {
    const uint8_t lv = level(s, static_cast<Dao>(i));
    if (lv >= need && lv > bestLevel) {
      best = static_cast<Dao>(i);
      bestLevel = lv;
    }
  }
  return best;
}

Element elementOf(const Dao dao) {
  switch (dao) {
    case Dao::Water:
      return Element::Water;
    case Dao::Sun:
      return Element::Wood;
    case Dao::Fire:
      return Element::Fire;
    case Dao::Metal:
      return Element::Metal;
    case Dao::Myriad:
      return Element::Earth;
    default:
      return Element::Count;
  }
}

uint8_t tierBoost(const GameState& s, const Element element) {
  for (uint8_t i = 0; i < kCount; ++i) {
    const Dao d = static_cast<Dao>(i);
    if (elementOf(d) != element) continue;
    const uint8_t lv = level(s, d);
    return static_cast<uint8_t>((lv >= 3) + (lv >= 6) + (lv >= 9));
  }
  return 0;
}

uint8_t ruinBonus(const GameState& s) { return static_cast<uint8_t>(3u * level(s, Dao::Time)); }
uint32_t consolidationPermille(const GameState& s) { return 1000u + 100u * level(s, Dao::Stillness); }
uint8_t pityPerFailureBonus(const GameState& s) { return static_cast<uint8_t>(2u * level(s, Dao::Tide)); }
uint8_t heartDemonBonus(const GameState& s) { return static_cast<uint8_t>(2u * level(s, Dao::Water)); }
uint32_t healPermille(const GameState& s) { return 1000u + 100u * level(s, Dao::Sun); }
uint8_t brewQualityBonus(const GameState& s) { return static_cast<uint8_t>(2u * level(s, Dao::Fire)); }
uint16_t fightDamagePermille(const GameState& s) { return static_cast<uint16_t>(1000u - 20u * level(s, Dao::Metal)); }
uint32_t stonesPercentBonus(const GameState& s) { return 5u * level(s, Dao::Myriad); }
uint8_t prizeChanceBonus(const GameState& s) { return static_cast<uint8_t>(2u * level(s, Dao::Fate)); }

}  // namespace daos
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
