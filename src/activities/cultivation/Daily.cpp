#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Alchemy.h"
#include "Codex.h"
#include "Daily.h"
#include "Daos.h"
#include "Exploration.h"
#include "Meridians.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace daily {
namespace {

constexpr uint32_t kKindOmen = 97;  // contentSeed kinds (97-99 were free)
constexpr uint32_t kKindTasks = 98;
constexpr uint32_t kKindDailyMemento = 131;  // v0.3.0: a memento for collecting all three tasks
constexpr uint32_t kKindReroll = 99;
constexpr uint32_t kMaxDaysFinished = 120;  // a longer gap only finishes its last 120 days (averages are ~0 by then)

constexpr uint8_t kDay = 0;
constexpr uint8_t kFlags = 2;
constexpr uint8_t kKinds = 3;
constexpr uint8_t kTargets = 6;
constexpr uint8_t kGameCount = 9;
constexpr uint8_t kUnitsToday = 10;
constexpr uint8_t kPagesToday = 12;
constexpr uint8_t kSessionsToday = 14;
constexpr uint8_t kAvgMinutes = 15;
constexpr uint8_t kAvgPages = 17;
constexpr uint8_t kLongest = 19;
constexpr uint8_t kCurrent = 21;

constexpr uint8_t kLive = 0x01;
constexpr uint8_t kRerolled = 0x02;
constexpr uint8_t kCollectedBase = 0x04;  // << slot
constexpr uint8_t kAveragesSet = 0x20;

constexpr uint32_t kNoDay = StatsSnapshot::kNoDay;

uint16_t get16(const GameState& s, const uint8_t at) {
  return static_cast<uint16_t>(s.daily[at] | (s.daily[at + 1] << 8));
}
void put16(GameState& s, const uint8_t at, const uint32_t v) {
  const uint16_t c = static_cast<uint16_t>(v > 0xFFFFu ? 0xFFFFu : v);
  s.daily[at] = static_cast<uint8_t>(c & 0xFF);
  s.daily[at + 1] = static_cast<uint8_t>(c >> 8);
}
uint8_t& flags(GameState& s) { return s.daily[kFlags]; }
uint8_t flags(const GameState& s) { return s.daily[kFlags]; }

bool readingTask(const Task t) { return t == Task::ReadMinutes || t == Task::TurnPages || t == Task::TwoSessions; }

// A game task the player can do today.
bool possible(const GameState& s, const Task t) {
  const uint8_t regions = explore::unlockedCount(s);
  const bool canWalk = regions > 0 && s.progress.stamina > 0;
  switch (t) {
    case Task::Chart:
      if (!canWalk) return false;
      for (uint8_t r = 0; r < regions; ++r) {
        if (!explore::cleared(s, r)) return true;
      }
      return false;
    case Task::Gather:
    case Task::WinFight:
    case Task::Merchant:
      return canWalk;
    case Task::Brew:
      return alchemy::knownCount(s) > 0 || alchemy::brewing(s);
    case Task::Meridian:
      return meridians::ready(s) > 0;
    default:
      return false;
  }
}

uint8_t gameCandidates(const GameState& s, Task* out, const Task except) {
  uint8_t n = 0;
  for (uint8_t k = static_cast<uint8_t>(Task::Chart); k < static_cast<uint8_t>(Task::Count); ++k) {
    const Task t = static_cast<Task>(k);
    if (t != except && possible(s, t)) out[n++] = t;
  }
  return n;
}

// 60% of the reader's own average, rounded to 5 minutes, 10-45; pages 10-60.
uint16_t targetFor(const GameState& s, const Task t) {
  switch (t) {
    case Task::ReadMinutes: {
      uint32_t m = static_cast<uint32_t>(averageMinutesX10(s)) * 6u / 100u;
      m = (m + 2u) / 5u * 5u;
      return static_cast<uint16_t>(m < 10u ? 10u : m > 45u ? 45u : m);
    }
    case Task::TurnPages: {
      const uint32_t p = static_cast<uint32_t>(averagePagesX10(s)) * 6u / 100u;
      return static_cast<uint16_t>(p < 10u ? 10u : p > 60u ? 60u : p);
    }
    case Task::TwoSessions:
    case Task::Chart:
    case Task::Gather:
      return 2;
    case Task::None:
      return 0;
    default:
      return 1;
  }
}

void setTask(GameState& s, const uint8_t slot, const Task t) {
  s.daily[kKinds + slot] = static_cast<uint8_t>(t);
  const uint16_t target = targetFor(s, t);
  s.daily[kTargets + slot] = static_cast<uint8_t>(target > 0xFF ? 0xFF : target);
}

void newDay(GameState& s, const uint32_t day) {
  put16(s, kDay, day + 1u);
  flags(s) = static_cast<uint8_t>(flags(s) & kAveragesSet);
  s.daily[kGameCount] = 0;
  put16(s, kUnitsToday, 0);
  put16(s, kPagesToday, 0);
  s.daily[kSessionsToday] = 0;
  Rng rng(contentSeed(worldSeedOf(s), kKindTasks, day));
  const uint8_t first = static_cast<uint8_t>(rng.below(3));
  const uint8_t second = static_cast<uint8_t>((first + 1u + rng.below(2)) % 3u);
  setTask(s, 0, static_cast<Task>(static_cast<uint8_t>(Task::ReadMinutes) + first));
  setTask(s, 1, static_cast<Task>(static_cast<uint8_t>(Task::ReadMinutes) + second));
  Task game[static_cast<uint8_t>(Task::Count)];
  const uint8_t n = gameCandidates(s, game, Task::None);
  setTask(s, 2, n > 0 ? game[rng.below(n)] : Task::None);
}

// A day that is over: the averages (14-day running average, days without
// reading included) and the long days the Daos count.
void finishDay(GameState& s, const uint32_t seconds, const uint32_t pages) {
  const uint32_t minutes = seconds / 60u;
  const uint32_t m =
      (static_cast<uint32_t>(kAverageDays - 1u) * averageMinutesX10(s) + minutes * 10u + 7u) / kAverageDays;
  const uint32_t p = (static_cast<uint32_t>(kAverageDays - 1u) * averagePagesX10(s) + pages * 10u + 7u) / kAverageDays;
  put16(s, kAvgMinutes, m);
  put16(s, kAvgPages, p);
  daos::addLongDay(s, seconds);
}

}  // namespace

bool live(const GameState& s) { return (flags(s) & kLive) != 0 && get16(s, kDay) != 0; }

uint32_t today(const GameState& s) {
  const uint16_t d = get16(s, kDay);
  return d == 0 ? kNoDay : static_cast<uint32_t>(d - 1u);
}

Omen omen(const GameState& s) {
  if (!live(s)) return Omen::None;
  Rng rng(contentSeed(worldSeedOf(s), kKindOmen, today(s)));
  return static_cast<Omen>(1u + rng.below(kOmenCount));
}

void update(GameState& s, const VisitGains& g, const StatsSample& now, const uint32_t lastVisitDay) {
  if (now.today == kNoDay || now.today >= 0xFFFEu) {
    flags(s) = static_cast<uint8_t>(flags(s) & ~kLive);
    return;
  }
  if ((flags(s) & kAveragesSet) == 0) {
    put16(s, kAvgMinutes, kStartAverageMinutes * 10u);
    put16(s, kAvgPages, kStartAveragePages * 10u);
    flags(s) |= kAveragesSet;
  }
  const uint32_t T = now.today;
  const uint32_t D = today(s);
  const uint32_t L = (lastVisitDay != kNoDay && lastVisitDay <= T) ? lastVisitDay : T;

  // This visit's reading per date in L..T: each weekday's seconds spread over
  // its dates; the remainder, and reading on a weekday outside the gap or
  // without a date, counts today.
  uint32_t stray = 0;
  uint32_t attributed = 0;
  for (uint8_t w = 0; w < 7; ++w) {
    attributed += g.dayOfWeekSeconds[w];
    if (bridge::weekdayOccurrences(L, T, w) == 0) stray += g.dayOfWeekSeconds[w];
  }
  if (g.seconds > attributed) stray += g.seconds - attributed;
  auto secondsOn = [&](const uint32_t day) -> uint32_t {
    if (g.resetDetected || day < L || day > T) return 0;
    const uint8_t w = bridge::weekdayOf(day);
    const uint32_t occ = bridge::weekdayOccurrences(L, T, w);
    if (occ == 0) return 0;
    const uint32_t share = g.dayOfWeekSeconds[w] / occ;
    return day == T ? share + (g.dayOfWeekSeconds[w] - share * occ) + stray : share;
  };
  auto pagesFor = [&](const uint32_t seconds) -> uint32_t {
    return g.seconds > 0 ? static_cast<uint32_t>(static_cast<uint64_t>(g.pages) * seconds / g.seconds) : 0u;
  };

  if (D == kNoDay || D < T) {
    // Finish the days before today: the stored day (with what it already had)
    // and any day of this visit's gap.
    uint32_t first = D == kNoDay ? L : D;
    if (T - first > kMaxDaysFinished) first = T - kMaxDaysFinished;
    for (uint32_t day = first; day < T; ++day) {
      const uint32_t mine = secondsOn(day);
      uint32_t seconds = mine;
      uint32_t pages = pagesFor(mine);
      if (day == D) {
        seconds += static_cast<uint32_t>(get16(s, kUnitsToday)) * 10u;
        pages += get16(s, kPagesToday);
      }
      finishDay(s, seconds, pages);
    }
    newDay(s, T);
  } else if (D > T) {
    newDay(s, T);  // the clock went back: start the day again
  }

  const uint32_t mine = secondsOn(T);
  put16(s, kUnitsToday, get16(s, kUnitsToday) + (mine + 5u) / 10u);
  put16(s, kPagesToday, get16(s, kPagesToday) + pagesFor(mine));
  if (mine > 0 && g.sessions > 0 && g.seconds > 0) {
    uint32_t n = static_cast<uint32_t>((static_cast<uint64_t>(g.sessions) * mine + g.seconds / 2u) / g.seconds);
    if (n == 0) n = 1;
    const uint32_t total = s.daily[kSessionsToday] + n;
    s.daily[kSessionsToday] = static_cast<uint8_t>(total > 0xFF ? 0xFF : total);
  }
  flags(s) |= kLive;

  const uint32_t from = s.character.creationDay != kNoDay ? s.character.creationDay : 0u;
  const Streaks st = computeStreaks(now, from);
  put16(s, kCurrent, st.current);
  if (st.longest > get16(s, kLongest)) put16(s, kLongest, st.longest);
}

Task task(const GameState& s, const uint8_t slot) {
  if (slot >= kTaskSlots || today(s) == kNoDay) return Task::None;
  const uint8_t k = s.daily[kKinds + slot];
  return k < static_cast<uint8_t>(Task::Count) ? static_cast<Task>(k) : Task::None;
}

uint16_t target(const GameState& s, const uint8_t slot) {
  return slot < kTaskSlots && task(s, slot) != Task::None ? s.daily[kTargets + slot] : 0;
}

uint16_t progress(const GameState& s, const uint8_t slot) {
  uint32_t have = 0;
  switch (task(s, slot)) {
    case Task::None:
      return 0;
    case Task::ReadMinutes:
      have = minutesToday(s);
      break;
    case Task::TurnPages:
      have = pagesToday(s);
      break;
    case Task::TwoSessions:
      have = sessionsToday(s);
      break;
    default:
      have = s.daily[kGameCount];
      break;
  }
  const uint16_t t = target(s, slot);
  return static_cast<uint16_t>(have < t ? have : t);
}

bool done(const GameState& s, const uint8_t slot) {
  const Task t = task(s, slot);
  if (t == Task::None) return false;
  if (progress(s, slot) < target(s, slot)) return false;
  return t != Task::TwoSessions || minutesToday(s) >= 10;
}

bool collected(const GameState& s, const uint8_t slot) {
  return slot < kTaskSlots && (flags(s) & (kCollectedBase << slot)) != 0;
}

uint8_t waiting(const GameState& s) {
  if (!live(s)) return 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < kTaskSlots; ++i) n += (done(s, i) && !collected(s, i)) ? 1 : 0;
  return n;
}

Reward rewardFor(const GameState& s, const uint8_t slot) {
  Reward r;
  const uint8_t regions = explore::unlockedCount(s);
  const uint8_t newest = regions > 0 ? static_cast<uint8_t>(regions - 1u) : 0u;
  const uint32_t realmStones = s.progress.realm + 1u;
  switch (task(s, slot)) {
    case Task::ReadMinutes:
    case Task::TurnPages:
    case Task::Brew: {
      if (regions == 0) {
        r.stones = kRegionlessStones;
        break;
      }
      const bool ore = task(s, slot) == Task::TurnPages;
      r.item = ore ? inventory::oreFor(newest) : inventory::herbFor(newest);
      r.count = task(s, slot) == Task::Brew ? 1 : 2;
      break;
    }
    case Task::TwoSessions:
    case Task::WinFight:
      r.item = inventory::Item::TonicMinor;
      r.count = 1;
      break;
    case Task::Chart:
    case Task::Gather:
    case Task::Merchant:
      r.stones = 40u * realmStones;
      break;
    case Task::Meridian:
      r.stones = 60u * realmStones;
      break;
    default:
      break;
  }
  return r;
}

bool collect(GameState& s, const uint8_t slot, Reward* out) {
  if (!live(s) || !done(s, slot) || collected(s, slot)) return false;
  const Reward r = rewardFor(s, slot);
  s.progress.stones = s.progress.stones + r.stones < s.progress.stones ? 0xFFFFFFFFu : s.progress.stones + r.stones;
  if (r.item != inventory::Item::None && r.count > 0) inventory::add(s, r.item, r.count);
  flags(s) = static_cast<uint8_t>(flags(s) | (kCollectedBase << slot));
  Reward given = r;
  // v0.3.0: the third task collected today, 1 in 100 for a memento.
  bool all = true;
  for (uint8_t i = 0; i < kTaskSlots; ++i) all = all && collected(s, i);
  if (all) {
    const codex::Find f =
        codex::roll(s, codex::Source::DailyTasks, contentSeed(worldSeedOf(s), kKindDailyMemento, today(s)));
    given.memento = f.memento;
    given.mementoDuplicate = f.duplicate;
    given.mementoStones = f.stones;
  }
  if (out != nullptr) *out = given;
  return true;
}

bool canReroll(const GameState& s, const uint8_t slot) {
  if (!live(s) || slot >= kTaskSlots || (flags(s) & kRerolled) != 0 || collected(s, slot)) return false;
  const Task t = task(s, slot);
  if (t == Task::None) return false;
  if (readingTask(t)) return true;  // the third reading task is always there
  Task game[static_cast<uint8_t>(Task::Count)];
  return gameCandidates(s, game, t) > 0;
}

bool reroll(GameState& s, const uint8_t slot) {
  if (!canReroll(s, slot)) return false;
  const Task t = task(s, slot);
  if (readingTask(t)) {
    const Task other = task(s, slot == 0 ? 1 : 0);
    for (uint8_t k = static_cast<uint8_t>(Task::ReadMinutes); k <= static_cast<uint8_t>(Task::TwoSessions); ++k) {
      const Task c = static_cast<Task>(k);
      if (c != t && c != other) {
        setTask(s, slot, c);
        break;
      }
    }
  } else {
    Task game[static_cast<uint8_t>(Task::Count)];
    const uint8_t n = gameCandidates(s, game, t);
    Rng rng(contentSeed(worldSeedOf(s), kKindReroll, today(s)));
    setTask(s, slot, game[rng.below(n)]);
    s.daily[kGameCount] = 0;
  }
  flags(s) |= kRerolled;
  return true;
}

void note(GameState& s, const Task kind, const uint16_t n) {
  if (!live(s) || task(s, 2) != kind || collected(s, 2)) return;
  const uint32_t c = static_cast<uint32_t>(s.daily[kGameCount]) + n;
  s.daily[kGameCount] = static_cast<uint8_t>(c > 0xFF ? 0xFF : c);
}

uint16_t minutesToday(const GameState& s) { return static_cast<uint16_t>(get16(s, kUnitsToday) / 6u); }
uint16_t pagesToday(const GameState& s) { return get16(s, kPagesToday); }
uint8_t sessionsToday(const GameState& s) { return s.daily[kSessionsToday]; }
uint16_t averageMinutesX10(const GameState& s) {
  return (flags(s) & kAveragesSet) != 0 ? get16(s, kAvgMinutes) : static_cast<uint16_t>(kStartAverageMinutes * 10u);
}
uint16_t averagePagesX10(const GameState& s) {
  return (flags(s) & kAveragesSet) != 0 ? get16(s, kAvgPages) : static_cast<uint16_t>(kStartAveragePages * 10u);
}

uint16_t currentStreak(const GameState& s) { return get16(s, kCurrent); }
uint16_t longestStreak(const GameState& s) { return get16(s, kLongest); }

Streaks computeStreaks(const StatsSample& now, const uint32_t fromDay) {
  Streaks r;
  if (now.today == kNoDay) return r;
  const uint32_t t = now.today;
  uint32_t first = t >= StatsSample::kHistoryDays - 1u ? t - (StatsSample::kHistoryDays - 1u) : 0u;
  if (fromDay > first) first = fromDay;
  uint32_t charmWeek = kNoDay;
  uint32_t current = 0;
  uint32_t longest = 0;
  for (uint32_t day = first; day <= t; ++day) {
    if (now.readOn(day)) {
      ++current;
      if (current > longest) longest = current;
    } else if (day == t) {
      // today is not over: no reading yet does not break the streak
    } else if (current > 0 && bridge::weekOf(day) != charmWeek) {
      charmWeek = bridge::weekOf(day);  // the week's Stillness Charm covers it
    } else {
      current = 0;
    }
  }
  r.current = static_cast<uint16_t>(current > 0xFFFFu ? 0xFFFFu : current);
  r.longest = static_cast<uint16_t>(longest > 0xFFFFu ? 0xFFFFu : longest);
  return r;
}

uint8_t extraHerbs(const GameState& s) { return omen(s) == Omen::HerbsThick ? 1 : 0; }
uint8_t extraOre(const GameState& s) { return omen(s) == Omen::VeinsShallow ? 1 : 0; }
uint8_t brewQualityBonus(const GameState& s) { return omen(s) == Omen::SteadyFurnace ? 5 : 0; }
uint16_t buyPermille(const GameState& s) { return omen(s) == Omen::GenerousRoad ? 900 : 1000; }
int fightLevelBonus(const GameState& s) { return omen(s) == Omen::SharpBlade ? 2 : 0; }
uint8_t slipBonus(const GameState& s) { return omen(s) == Omen::LightFeet ? 15 : 0; }
uint8_t heartDemonBonus(const GameState& s) { return omen(s) == Omen::ClearHeart ? 5 : 0; }
uint16_t lightningPermille(const GameState& s) { return omen(s) == Omen::CalmSky ? 900 : 1000; }
uint8_t ruinBonus(const GameState& s) { return omen(s) == Omen::OldRuins ? 15 : 0; }
uint32_t consolidationPermille(const GameState& s) { return omen(s) == Omen::QuietMind ? 1200u : 1000u; }

}  // namespace daily
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
