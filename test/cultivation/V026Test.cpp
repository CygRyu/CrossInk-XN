#include <Alchemy.h>
#include <Breakthrough.h>
#include <Daily.h>
#include <Daos.h>
#include <Exploration.h>
#include <GameState.h>
#include <Inventory.h>
#include <Meridians.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>

// v0.2.6 (milestone 9a, agreed with the user in session 9; steps and rule A
// chosen in session 10): the omen, daily tasks, the game's own streak and Daos.
using namespace cultivation;
using daily::Omen;
using daily::Task;
using daos::Dao;

namespace {

constexpr uint32_t kMonday = 9802;  // bridge::weekdayOf(9802) == 0

// CrossInk's side: totals, weekday and time-of-day buckets, the history bits.
struct Reader {
  StatsSample s;
  std::set<uint32_t> days;

  void read(const uint32_t day, const uint32_t minutes, const uint8_t bucket, const uint32_t sessions = 1) {
    const uint32_t sec = minutes * 60u;
    s.seconds += sec;
    s.pages += minutes;  // a page a minute
    s.sessions += sessions;
    s.timeOfDaySeconds[bucket] += sec;
    s.dayOfWeekSeconds[bridge::weekdayOf(day)] += sec;
    if (minutes > 0) days.insert(day);
  }
  StatsSample at(const uint32_t today) {
    StatsSample out = s;
    out.today = today;
    out.historyBits.fill(0);
    out.historyAnchorDay = days.empty() ? 0 : *days.rbegin();
    for (const uint32_t d : days) {
      const uint32_t delta = out.historyAnchorDay - d;
      if (delta < StatsSample::kHistoryDays) out.historyBits[delta / 8] |= static_cast<uint8_t>(1u << (delta % 8));
    }
    return out;
  }
};

struct Game {
  GameState state;
  Reader reader;
  RevisionWatch watch;

  explicit Game(const uint32_t creationDay = kMonday, const uint8_t realm = 1) {
    CharacterDraft d;
    snprintf(d.name, sizeof(d.name), "Lin Yue");
    d.destiny.purity = 50;
    d.destiny.talent = 2;
    d.origin = Origin::StreetOrphan;
    d.worldSeed = 0x5EED1234;
    createCharacter(state, d, reader.at(creationDay));
    state.progress.realm = realm;
    state.progress.hpMax = 100;
    state.progress.hp = 100;
  }

  // The game's visit, in the activity's order.
  daos::News visit(const uint32_t today) {
    const StatsSample now = reader.at(today);
    const uint32_t lastDay = state.snapshot.lastVisitDay;
    const VisitGains g = bridge::collect(state.snapshot, now, watch);
    absorb(state, g);
    daily::update(state, g, now, lastDay);
    daos::addReading(state, g, state.snapshot);
    return daos::settle(state);
  }

  // `days` days of reading `minutes` in `bucket` (every day, or weekends), one visit a day.
  void habit(const uint32_t from, const uint32_t days, const uint32_t minutes, const uint8_t bucket,
             const bool weekendsOnly = false) {
    for (uint32_t d = from; d < from + days; ++d) {
      const bool weekend = bridge::weekdayOf(d) >= 5;
      if (!weekendsOnly || weekend) reader.read(d, minutes, bucket);
      visit(d);
    }
  }
};

std::set<Dao> foundSet(const GameState& s) {
  std::set<Dao> out;
  for (uint8_t i = 0; i < daos::kCount; ++i) {
    if (daos::found(s, static_cast<Dao>(i))) out.insert(static_cast<Dao>(i));
  }
  return out;
}

void markFound(GameState& s, const Dao d) {
  const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint8_t>(d));
  s.daosAndMeridians[0] = static_cast<uint8_t>(s.daosAndMeridians[0] | (bit & 0xFF));
  s.daosAndMeridians[1] = static_cast<uint8_t>(s.daosAndMeridians[1] | (bit >> 8));
}

void setUnits(GameState& s, const uint8_t bucket, const uint32_t units) {
  s.daosAndMeridians[2 + 2 * bucket] = static_cast<uint8_t>(units & 0xFF);
  s.daosAndMeridians[3 + 2 * bucket] = static_cast<uint8_t>(units >> 8);
}

// Found, with a measure that gives `level` (Stillness, Tide, Water, Fire, Metal).
void giveDao(GameState& s, const Dao d, const uint8_t level) {
  markFound(s, d);
  const daos::Rule& r = daos::ruleOf(d);
  const uint32_t m = r.condition + r.step * (level - 1u);
  switch (d) {
    case Dao::Stillness:
      s.daosAndMeridians[10] = static_cast<uint8_t>(m);
      break;
    case Dao::Tide:
      s.daosAndMeridians[11] = static_cast<uint8_t>(m);
      break;
    case Dao::Fire:
      setUnits(s, 1, m * 6u);
      break;
    case Dao::Metal:
      setUnits(s, 2, m * 6u);
      break;
    case Dao::Water:
      setUnits(s, 3, m * 6u);
      break;
    default:
      break;
  }
}

GameState atPeak(const uint8_t realm) {
  Game g(kMonday, realm);
  GameState s = g.state;
  s.progress.stage = static_cast<uint8_t>(realmInfo(realm).stages - 1);
  s.progress.qi = stageCost(realm, s.progress.stage);
  return s;
}

}  // namespace

// --- Streak --------------------------------------------------------------------------------

TEST(V026Streak, DailyReadingCountsDays) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 10; ++d) r.read(d, 20, 2);
  const daily::Streaks st = daily::computeStreaks(r.at(kMonday + 9), kMonday);
  EXPECT_EQ(st.current, 10);
  EXPECT_EQ(st.longest, 10);
}

TEST(V026Streak, TodayWithoutReadingDoesNotBreakIt) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 5; ++d) r.read(d, 20, 2);
  EXPECT_EQ(daily::computeStreaks(r.at(kMonday + 5), kMonday).current, 5);
}

TEST(V026Streak, OneMissedDayAWeekIsCovered) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 21; ++d) {
    if (d != kMonday + 3 && d != kMonday + 10) r.read(d, 20, 2);  // one miss in week 1, one in week 2
  }
  const daily::Streaks st = daily::computeStreaks(r.at(kMonday + 20), kMonday);
  EXPECT_EQ(st.current, 19);  // missed days are covered, not counted
}

TEST(V026Streak, TwoMissedDaysInAWeekBreakIt) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 14; ++d) {
    if (d != kMonday + 8 && d != kMonday + 10) r.read(d, 20, 2);  // both in week 2
  }
  const daily::Streaks st = daily::computeStreaks(r.at(kMonday + 13), kMonday);
  EXPECT_EQ(st.longest, 9);  // days 0-7 and 9 (day 8 covered)
  EXPECT_EQ(st.current, 3);  // days 11-13
}

TEST(V026Streak, ReadingBeforeCreationDoesNotCount) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 30; ++d) r.read(d, 20, 2);
  EXPECT_EQ(daily::computeStreaks(r.at(kMonday + 29), kMonday + 20).longest, 10);
}

TEST(V026Streak, WeekendReaderHasNoLongStreak) {
  Reader r;
  for (uint32_t d = kMonday; d < kMonday + 56; ++d) {
    if (bridge::weekdayOf(d) >= 5) r.read(d, 105, 1);
  }
  EXPECT_LE(daily::computeStreaks(r.at(kMonday + 55), kMonday).longest, 3);
}

// --- Days, averages, tasks ------------------------------------------------------------------

TEST(V026Daily, NoDateMeansNothingLive) {
  Game g;
  g.reader.read(kMonday, 30, 2);
  StatsSample now = g.reader.at(kMonday);
  now.today = StatsSnapshot::kNoDay;
  const uint32_t last = g.state.snapshot.lastVisitDay;
  const VisitGains gains = bridge::collect(g.state.snapshot, now, g.watch);
  daily::update(g.state, gains, now, last);
  EXPECT_FALSE(daily::live(g.state));
  EXPECT_EQ(daily::omen(g.state), Omen::None);
  EXPECT_EQ(daily::task(g.state, 0), Task::None);
}

TEST(V026Daily, FirstDayHasOmenAndTwoReadingTasks) {
  Game g;
  g.reader.read(kMonday, 14, 2);
  g.visit(kMonday);
  ASSERT_TRUE(daily::live(g.state));
  EXPECT_NE(daily::omen(g.state), Omen::None);
  const Task a = daily::task(g.state, 0);
  const Task b = daily::task(g.state, 1);
  EXPECT_NE(a, b);
  for (const Task t : {a, b}) EXPECT_TRUE(t == Task::ReadMinutes || t == Task::TurnPages || t == Task::TwoSessions);
  EXPECT_EQ(daily::minutesToday(g.state), 14);
  EXPECT_EQ(daily::pagesToday(g.state), 14);
  EXPECT_EQ(daily::sessionsToday(g.state), 1);
}

TEST(V026Daily, StartingTargetsComeFromTheStartingAverages) {
  Game g;
  g.visit(kMonday);
  for (uint8_t i = 0; i < 2; ++i) {
    if (daily::task(g.state, i) == Task::ReadMinutes) {
      EXPECT_EQ(daily::target(g.state, i), 10);
    }  // 60% of 20 -> 10
    if (daily::task(g.state, i) == Task::TurnPages) {
      EXPECT_EQ(daily::target(g.state, i), 10);
    }  // 60% of 15 -> 10
    if (daily::task(g.state, i) == Task::TwoSessions) {
      EXPECT_EQ(daily::target(g.state, i), 2);
    }
  }
}

TEST(V026Daily, OmenIsTheSameAllDayAndChangesWithTheDate) {
  Game g;
  g.visit(kMonday);
  const Omen first = daily::omen(g.state);
  g.reader.read(kMonday, 10, 2);
  g.visit(kMonday);
  EXPECT_EQ(daily::omen(g.state), first);
  std::set<Omen> seen;
  for (uint32_t d = kMonday + 1; d < kMonday + 60; ++d) {
    g.visit(d);
    seen.insert(daily::omen(g.state));
  }
  EXPECT_GE(seen.size(), 8u);
  EXPECT_EQ(seen.count(Omen::None), 0u);
}

TEST(V026Daily, VisitsTheSameDayAddUp) {
  Game g;
  g.reader.read(kMonday, 7, 2);
  g.visit(kMonday);
  g.reader.read(kMonday, 8, 2);
  g.visit(kMonday);
  EXPECT_EQ(daily::minutesToday(g.state), 15);
  EXPECT_EQ(daily::sessionsToday(g.state), 2);
}

TEST(V026Daily, YesterdaysReadingIsNotToday) {
  Game g;
  g.visit(kMonday);
  g.reader.read(kMonday, 40, 2);  // read Monday evening, opened the game Tuesday
  g.reader.read(kMonday + 1, 12, 0);
  g.visit(kMonday + 1);
  EXPECT_EQ(daily::minutesToday(g.state), 12);
}

TEST(V026Daily, LongDaysAreCountedFromTheGap) {
  Game g;
  g.visit(kMonday);
  g.reader.read(kMonday + 1, 70, 2);   // Tuesday: an hour and more
  g.reader.read(kMonday + 2, 200, 2);  // Wednesday: over three hours
  g.reader.read(kMonday + 3, 20, 2);
  g.visit(kMonday + 4);  // opened only on Friday
  EXPECT_EQ(daos::daysOver60(g.state), 2);
  EXPECT_EQ(daos::daysOver3h(g.state), 1);
}

TEST(V026Daily, AGapOfWeeksSpreadsTheWeekdays) {
  Game g;
  g.visit(kMonday);
  g.reader.read(kMonday + 7, 30, 2);   // two Mondays...
  g.reader.read(kMonday + 14, 30, 2);  // ...the second is today
  g.visit(kMonday + 14);
  EXPECT_EQ(daily::minutesToday(g.state), 20);  // 60 minutes over 3 Mondays (day 0, 7, 14)
}

TEST(V026Daily, AveragesFollowTheReader) {
  Game g;
  g.habit(kMonday, 60, 60, 2);  // an hour a day for two months
  const uint16_t avg = daily::averageMinutesX10(g.state);
  EXPECT_GT(avg, 550);
  EXPECT_LE(avg, 600);
  for (uint8_t i = 0; i < 2; ++i) {
    if (daily::task(g.state, i) == Task::ReadMinutes) {
      EXPECT_EQ(daily::target(g.state, i), 35);
    }  // 60% of 58-60
  }
  Game light;  // a light reader gets easy targets, never under 10
  light.habit(kMonday, 60, 5, 2);
  for (uint8_t i = 0; i < 2; ++i) {
    if (daily::task(light.state, i) == Task::ReadMinutes) {
      EXPECT_EQ(daily::target(light.state, i), 10);
    }
  }
}

TEST(V026Daily, DaysWithoutReadingLowerTheAverage) {
  Game g;
  g.habit(kMonday, 30, 60, 2);
  const uint16_t before = daily::averageMinutesX10(g.state);
  g.visit(kMonday + 50);  // three weeks away
  EXPECT_LT(daily::averageMinutesX10(g.state), before / 3);
}

TEST(V026Daily, CollectOnceAndRewardsMatchTheTask) {
  Game g(kMonday, 1);
  g.reader.read(kMonday, 60, 2, 2);
  g.visit(kMonday);
  ASSERT_EQ(daily::waiting(g.state), 2u);  // both reading tasks done
  for (uint8_t i = 0; i < 2; ++i) {
    const uint32_t stones = g.state.progress.stones;
    daily::Reward r;
    ASSERT_TRUE(daily::collect(g.state, i, &r));
    EXPECT_FALSE(daily::collect(g.state, i));
    EXPECT_TRUE(daily::collected(g.state, i));
    const Task t = daily::task(g.state, i);
    if (t == Task::ReadMinutes) {  // realm 1 opens region 0: two of its herbs
      EXPECT_EQ(r.item, inventory::herbFor(0));
      EXPECT_EQ(r.count, 2);
      EXPECT_EQ(inventory::count(g.state, inventory::herbFor(0)), 2);
    } else if (t == Task::TurnPages) {
      EXPECT_EQ(r.item, inventory::oreFor(0));
    } else {
      EXPECT_EQ(r.item, inventory::Item::TonicMinor);
    }
    EXPECT_EQ(g.state.progress.stones, stones + r.stones);
  }
  EXPECT_EQ(daily::waiting(g.state), 0u);
}

TEST(V026Daily, BeforeAnyRegionHerbsBecomeStones) {
  Game g(kMonday, 0);  // Body Tempering: no region yet
  g.reader.read(kMonday, 60, 2, 2);
  g.visit(kMonday);
  for (uint8_t i = 0; i < 2; ++i) {
    const Task t = daily::task(g.state, i);
    if (t == Task::ReadMinutes || t == Task::TurnPages) {
      EXPECT_EQ(daily::rewardFor(g.state, i).stones, 30u);
    }
  }
}

TEST(V026Daily, TwoSessionsNeedTenMinutes) {
  Game g;
  for (uint32_t d = kMonday; d < kMonday + 10; ++d) {
    g.reader.read(d, 4, 2, 2);
    g.visit(d);
    for (uint8_t i = 0; i < 2; ++i) {
      if (daily::task(g.state, i) == Task::TwoSessions) {
        EXPECT_FALSE(daily::done(g.state, i));
      }
    }
  }
}

TEST(V026Daily, TasksExpireWithTheDay) {
  Game g;
  g.reader.read(kMonday, 60, 2, 2);
  g.visit(kMonday);
  ASSERT_TRUE(daily::collect(g.state, 0));
  g.visit(kMonday + 1);
  EXPECT_EQ(daily::minutesToday(g.state), 0);
  for (uint8_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(daily::collected(g.state, i));
  }
  EXPECT_EQ(daily::waiting(g.state), 0u);
}

TEST(V026Daily, OneRerollADay) {
  Game g;
  g.visit(kMonday);
  const Task a = daily::task(g.state, 0);
  const Task b = daily::task(g.state, 1);
  ASSERT_TRUE(daily::canReroll(g.state, 0));
  ASSERT_TRUE(daily::reroll(g.state, 0));
  EXPECT_NE(daily::task(g.state, 0), a);
  EXPECT_NE(daily::task(g.state, 0), b);
  EXPECT_FALSE(daily::canReroll(g.state, 1));
  g.visit(kMonday + 1);
  EXPECT_TRUE(daily::canReroll(g.state, 0));
}

TEST(V026Daily, GameTaskOnlyWhenPossible) {
  Game g(kMonday, 0);  // Body Tempering, Street orphan: no region, recipe or meridian point
  g.visit(kMonday);
  EXPECT_EQ(daily::task(g.state, 2), Task::None);
  EXPECT_FALSE(daily::canReroll(g.state, 2));
  Game q(kMonday, 1);  // Qi Condensation with stamina: region 0 is open
  q.state.progress.stamina = 5;
  q.visit(kMonday);
  EXPECT_NE(daily::task(q.state, 2), Task::None);
  Game tired(kMonday, 1);  // no stamina, no recipe, no point: nothing possible
  tired.state.progress.stamina = 0;
  tired.state.progress.realm = 1;
  tired.visit(kMonday);
  if (meridians::ready(tired.state) == 0) {
    EXPECT_EQ(daily::task(tired.state, 2), Task::None);
  }
}

TEST(V026Daily, GameActionsCountOnlyForTodaysKind) {
  Game g(kMonday, 1);
  g.state.progress.stamina = 5;
  int days = 0;
  for (uint32_t d = kMonday; d < kMonday + 40; ++d) {
    g.visit(d);
    const Task t = daily::task(g.state, 2);
    if (t == Task::None) continue;
    ++days;
    const Task other = t == Task::Merchant ? Task::Chart : Task::Merchant;
    daily::note(g.state, other, 5);
    EXPECT_EQ(daily::progress(g.state, 2), 0);
    daily::note(g.state, t, 1);
    EXPECT_EQ(daily::progress(g.state, 2), 1);
  }
  EXPECT_GT(days, 30);
}

TEST(V026Daily, GameRewardsScaleWithTheRealm) {
  Game g(kMonday, 3);
  g.state.progress.stamina = 5;
  for (uint32_t d = kMonday; d < kMonday + 60; ++d) {
    g.visit(d);
    const Task t = daily::task(g.state, 2);
    if (t == Task::Chart || t == Task::Gather || t == Task::Merchant) {
      EXPECT_EQ(daily::rewardFor(g.state, 2).stones, 160u);  // 40 x (3 + 1)
      return;
    }
  }
  FAIL() << "no stone-paying game task in 60 days";
}

// --- Omen effects ---------------------------------------------------------------------------

TEST(V026Omen, EffectsOnlyOnTheirDay) {
  Game g(kMonday, 1);
  bool sawEach[daily::kOmenCount + 1] = {};
  for (uint32_t d = kMonday; d < kMonday + 120; ++d) {
    g.visit(d);
    const Omen o = daily::omen(g.state);
    sawEach[static_cast<uint8_t>(o)] = true;
    EXPECT_EQ(daily::extraHerbs(g.state), o == Omen::HerbsThick ? 1 : 0);
    EXPECT_EQ(daily::extraOre(g.state), o == Omen::VeinsShallow ? 1 : 0);
    EXPECT_EQ(daily::brewQualityBonus(g.state), o == Omen::SteadyFurnace ? 5 : 0);
    EXPECT_EQ(daily::buyPermille(g.state), o == Omen::GenerousRoad ? 900 : 1000);
    EXPECT_EQ(daily::fightLevelBonus(g.state), o == Omen::SharpBlade ? 2 : 0);
    EXPECT_EQ(daily::slipBonus(g.state), o == Omen::LightFeet ? 15 : 0);
    EXPECT_EQ(daily::heartDemonBonus(g.state), o == Omen::ClearHeart ? 5 : 0);
    EXPECT_EQ(daily::lightningPermille(g.state), o == Omen::CalmSky ? 900 : 1000);
    EXPECT_EQ(daily::ruinBonus(g.state), o == Omen::OldRuins ? 15 : 0);
    EXPECT_EQ(daily::consolidationPermille(g.state), o == Omen::QuietMind ? 1200u : 1000u);
  }
  for (uint8_t i = 1; i <= daily::kOmenCount; ++i) {
    EXPECT_TRUE(sawEach[i]) << "omen " << int(i);
  }
}

TEST(V026Omen, NeverTouchesQi) {
  Game g(kMonday, 1);
  const uint32_t rate = qiMultiplierPermille(g.state);
  for (uint32_t d = kMonday; d < kMonday + 30; ++d) {
    g.visit(d);
    EXPECT_EQ(qiMultiplierPermille(g.state), rate);
  }
}

TEST(V026Omen, GenerousRoadLowersMerchantPrices) {
  Game g(kMonday, 1);
  for (uint32_t d = kMonday; d < kMonday + 120; ++d) {
    g.visit(d);
    if (daily::omen(g.state) != Omen::GenerousRoad) continue;
    EXPECT_EQ(explore::stockPrice(g.state, inventory::Item::TonicMinor),
              (inventory::price(inventory::Item::TonicMinor) * 9 + 5) / 10);
    return;
  }
  FAIL() << "no Generous road day in 120 days";
}

// --- Daos -----------------------------------------------------------------------------------

TEST(V026Daos, NoSlotsInBodyTemperingOrBeforeFiveTrackedHours) {
  Game g(kMonday, 0);
  g.habit(kMonday, 30, 60, 3);
  EXPECT_EQ(daos::slots(g.state), 0);
  EXPECT_EQ(daos::foundCount(g.state), 0);
  Game q(kMonday, 1);
  q.habit(kMonday, 4, 60, 3);  // 4 h tracked
  EXPECT_EQ(daos::slots(q.state), 0);
  q.habit(kMonday + 4, 1, 60, 3);
  EXPECT_EQ(daos::slots(q.state), 2);
}

TEST(V026Daos, SlotsByRealm) {
  Game g(kMonday, 1);
  g.habit(kMonday, 6, 60, 2);
  EXPECT_EQ(daos::slots(g.state), 2);
  for (uint8_t realm = 2; realm <= 6; ++realm) {
    g.state.progress.realm = realm;
    EXPECT_EQ(daos::slots(g.state), realm >= 4 ? 5 : realm + 1) << int(realm);
  }
}

TEST(V026Daos, RuleASlotWaitsForAMetCondition) {
  Game g(kMonday, 1);
  g.habit(kMonday, 8, 60, 3);  // 8 h at night: not yet the 10 h condition
  EXPECT_EQ(daos::slots(g.state), 2);
  EXPECT_FALSE(daos::found(g.state, Dao::Water));
  g.habit(kMonday + 8, 2, 60, 3);  // 10 h
  EXPECT_TRUE(daos::found(g.state, Dao::Water));
  EXPECT_EQ(daos::level(g.state, Dao::Water), 1);
}

TEST(V026Daos, NightReaderBalancedReaderAndBingerDiffer) {
  Game night(kMonday, 4);
  night.habit(kMonday, 200, 30, 3);
  Game balanced(kMonday, 4);
  for (uint32_t d = kMonday; d < kMonday + 200; ++d) {
    balanced.reader.read(d, 30, static_cast<uint8_t>(d % 4));
    balanced.visit(d);
  }
  Game binge(kMonday, 4);
  binge.habit(kMonday, 200, 210, 1, true);

  const std::set<Dao> a = foundSet(night.state);
  const std::set<Dao> b = foundSet(balanced.state);
  const std::set<Dao> c = foundSet(binge.state);
  EXPECT_TRUE(a.count(Dao::Water) && a.count(Dao::Time));
  EXPECT_FALSE(a.count(Dao::Myriad));
  EXPECT_TRUE(b.count(Dao::Myriad) && b.count(Dao::Time));
  EXPECT_FALSE(b.count(Dao::Water) || b.count(Dao::Sun) || b.count(Dao::Fire) || b.count(Dao::Metal));
  EXPECT_TRUE(c.count(Dao::Tide) && c.count(Dao::Stillness) && c.count(Dao::Fire));
  EXPECT_FALSE(c.count(Dao::Time));
  EXPECT_NE(a, b);
  EXPECT_NE(b, c);
  EXPECT_NE(a, c);
}

TEST(V026Daos, LevelSteps) {
  Game g(kMonday, 1);
  g.habit(kMonday, 10, 60, 3);
  ASSERT_TRUE(daos::found(g.state, Dao::Water));
  EXPECT_EQ(daos::level(g.state, Dao::Water), 1);
  EXPECT_EQ(daos::nextLevelAt(g.state, Dao::Water), 35u);  // 10 + 25
  g.habit(kMonday + 10, 25, 60, 3);                        // 35 h
  EXPECT_EQ(daos::level(g.state, Dao::Water), 2);
  g.habit(kMonday + 35, 200, 60, 3);  // 235 h: level 9 at 210
  EXPECT_EQ(daos::level(g.state, Dao::Water), 9);
  EXPECT_EQ(daos::nextLevelAt(g.state, Dao::Water), 0u);
}

TEST(V026Daos, NewsAnnouncesFoundLevelsOnce) {
  Game g(kMonday, 1);
  g.habit(kMonday, 9, 60, 3);
  g.reader.read(kMonday + 9, 60, 3);
  daos::News n = g.visit(kMonday + 9);
  ASSERT_EQ(n.foundCount, 1);
  EXPECT_EQ(n.foundDaos[0], Dao::Water);
  EXPECT_EQ(n.levelCount, 0);
  g.habit(kMonday + 10, 24, 60, 3);
  g.reader.read(kMonday + 34, 60, 3);
  n = g.visit(kMonday + 34);
  ASSERT_EQ(n.levelCount, 1);
  EXPECT_EQ(n.levelDaos[0], Dao::Water);
  EXPECT_EQ(n.levels[0], 2);
  n = g.visit(kMonday + 34);  // nothing new: nothing announced twice
  EXPECT_EQ(n.foundCount + n.levelCount, 0);
}

TEST(V026Daos, FiveAtMostAndThePathSettles) {
  Game g(kMonday, 4);
  bool settledSeen = false;
  for (uint32_t d = kMonday; d < kMonday + 400; ++d) {
    g.reader.read(d, d % 7 == 6 ? 200 : 40, static_cast<uint8_t>(d % 4));
    if (d == kMonday + 100) g.reader.s.completedBooks += 5;
    settledSeen = g.visit(d).settledNow || settledSeen;
    EXPECT_LE(daos::foundCount(g.state), 5);
  }
  EXPECT_EQ(daos::foundCount(g.state), 5);
  EXPECT_TRUE(daos::settled(g.state));
  EXPECT_TRUE(settledSeen);
}

TEST(V026Daos, FateCountsBooksSinceCreation) {
  Game g(kMonday, 1);
  g.reader.s.completedBooks = 0;
  g.habit(kMonday, 6, 60, 3);
  g.reader.s.completedBooks = 2;
  g.reader.read(kMonday + 6, 10, 3);
  g.visit(kMonday + 6);
  EXPECT_TRUE(daos::found(g.state, Dao::Fate));
  EXPECT_EQ(daos::measure(g.state, Dao::Fate), 2u);
}

TEST(V026Daos, TimeOfDayHoursUseWholeTenMinuteSteps) {
  Game g(kMonday, 1);
  for (int i = 0; i < 6; ++i) {  // six visits of 5 minutes: 30 minutes, 3 units
    g.reader.read(kMonday, 5, 0);
    g.visit(kMonday);
  }
  EXPECT_EQ(daos::bucketUnits(g.state, 0), 3);
}

TEST(V026Daos, MyriadNeedsBalance) {
  GameState s = atPeak(2);
  setUnits(s, 0, 30);
  setUnits(s, 1, 30);
  setUnits(s, 2, 30);
  setUnits(s, 3, 30);  // 20 h, 25% each
  EXPECT_TRUE(daos::conditionMet(s, Dao::Myriad));
  setUnits(s, 2, 200);  // evening now holds over 40%
  EXPECT_FALSE(daos::conditionMet(s, Dao::Myriad));
  EXPECT_TRUE(daos::conditionMet(s, Dao::Metal));
}

// --- Keys and effects -------------------------------------------------------------------------

TEST(V026Keys, DaoAtTargetPlusTwoIsTheKeyAndNeverSpent) {
  GameState s = atPeak(2);  // Foundation -> Core needs level 5
  giveDao(s, Dao::Water, 4);
  breakthrough::Chance c = breakthrough::chanceFor(s);
  EXPECT_EQ(c.daoKey, 0xFF);
  EXPECT_FALSE(c.hasKey);
  giveDao(s, Dao::Water, 5);
  c = breakthrough::chanceFor(s);
  EXPECT_EQ(c.daoKey, static_cast<uint8_t>(Dao::Water));
  EXPECT_TRUE(c.hasKey);
  EXPECT_FALSE(c.pillIsKey);
  EXPECT_EQ(daos::keyLevelFor(2), 4);
  EXPECT_EQ(daos::keyLevelFor(9), 9);
}

TEST(V026Keys, DaoKeyKeepsTheScroll) {
  GameState s = atPeak(1);  // into Foundation: level 4, or a Mortal scroll
  scrolls::Scroll sc;
  sc.titleHash = 77;
  sc.hours = 2;
  sc.kind = scrolls::Kind::Sword;
  scrolls::put(s, 0, sc);
  s.techniqueScrolls[0] = 1;
  giveDao(s, Dao::Water, 4);
  const breakthrough::Chance c = breakthrough::chanceFor(s);
  EXPECT_EQ(c.keyIndex, -1);
  EXPECT_EQ(c.daoKey, static_cast<uint8_t>(Dao::Water));
  s.progress.failures = 8;  // pity: the roll will succeed
  const breakthrough::Result r = breakthrough::attempt(s);
  ASSERT_TRUE(r.success);
  EXPECT_FALSE(r.keyUsed);
  EXPECT_EQ(scrolls::count(s), 1);
}

TEST(V026Effects, TidePity) {
  GameState s = atPeak(4);
  s.progress.failures = 1;
  const uint8_t without = breakthrough::chanceFor(s).pity;
  giveDao(s, Dao::Tide, 3);
  const uint8_t with = breakthrough::chanceFor(s).pity;
  EXPECT_EQ(with, without + 6);
  s.progress.failures = 9;
  EXPECT_LE(breakthrough::chanceFor(s).total, breakthrough::kPityCap);
}

TEST(V026Effects, StillnessMakesConsolidationFaster) {
  GameState s = atPeak(2);
  breakthrough::enterGreatPerfection(s);
  ASSERT_TRUE(breakthrough::inGreatPerfection(s));
  s.daily.fill(0);  // no omen
  EXPECT_EQ(breakthrough::consolidationGained(s, 0, 10 * 3600), 20);
  giveDao(s, Dao::Stillness, 5);  // +50%
  EXPECT_EQ(breakthrough::consolidationGained(s, 0, 10 * 3600), 30);
}

TEST(V026Effects, TierBoostAtThreeSixNineNeverForManuals) {
  GameState s = atPeak(2);
  scrolls::Scroll sc;
  sc.hours = 2;  // Mortal
  sc.element = Element::Fire;
  sc.kind = scrolls::Kind::Body;
  EXPECT_EQ(scrolls::effectiveTier(s, sc), scrolls::Tier::Mortal);
  giveDao(s, Dao::Fire, 3);
  EXPECT_EQ(scrolls::effectiveTier(s, sc), scrolls::Tier::Yellow);
  giveDao(s, Dao::Fire, 9);
  EXPECT_EQ(scrolls::effectiveTier(s, sc), scrolls::Tier::Heaven);
  sc.element = Element::Water;
  EXPECT_EQ(scrolls::effectiveTier(s, sc), scrolls::Tier::Mortal);
}

TEST(V026Effects, DaosNeverTouchQi) {
  GameState s = atPeak(3);
  const uint32_t rate = qiMultiplierPermille(s);
  for (const Dao d : {Dao::Stillness, Dao::Tide, Dao::Water, Dao::Fire, Dao::Metal}) giveDao(s, d, 9);
  EXPECT_EQ(qiMultiplierPermille(s), rate);
}

TEST(V026Effects, FateRaisesPrizesOnlyWhereThereAreSome) {
  GameState s = atPeak(2);
  EXPECT_EQ(explore::prizeChance(s, explore::Danger::Weak), 0);
  EXPECT_EQ(explore::prizeChance(s, explore::Danger::Hazardous), 15);
  markFound(s, Dao::Fate);
  s.snapshot.completedBooks = s.baseline.completedBooks + 8;  // 2 + 3 x 2: level 3
  EXPECT_EQ(daos::level(s, Dao::Fate), 3);
  EXPECT_EQ(explore::prizeChance(s, explore::Danger::Hazardous), 21);
  EXPECT_EQ(explore::prizeChance(s, explore::Danger::Weak), 0);
}

TEST(V026Effects, MetalLowersFightDamage) {
  GameState s = atPeak(2);
  EXPECT_EQ(explore::fightDamagePermille(s), 1000);
  giveDao(s, Dao::Metal, 1);
  EXPECT_EQ(explore::fightDamagePermille(s), 980);
}

TEST(V026Effects, MyriadPathsAddsStones) {
  Game g(kMonday, 1);
  markFound(g.state, Dao::Myriad);
  for (uint8_t b = 0; b < 4; ++b) setUnits(g.state, b, 23 + (b == 0 ? 1 : 0));  // 15.x h: level 1
  ASSERT_EQ(daos::level(g.state, Dao::Myriad), 1);
  const uint32_t before = g.state.progress.stones;
  g.reader.read(kMonday, 100, 2);
  g.state.daily.fill(0);
  StatsSample now = g.reader.at(kMonday);
  const VisitGains gains = bridge::collect(g.state.snapshot, now, g.watch);
  const AbsorbResult r = absorb(g.state, gains);
  EXPECT_EQ(r.stonesGained, 157u);  // 100 minutes + 50 for 100 pages = 150, +5%
  EXPECT_EQ(g.state.progress.stones, before + r.stonesGained);
}

TEST(V026Save, DailyAndDaoBytesRoundTrip) {
  Game g(kMonday, 1);
  g.habit(kMonday, 12, 60, 3);
  ASSERT_TRUE(save::store(g.state, "/v026.bin"));
  GameState back;
  ASSERT_EQ(save::load(back, "/v026.bin"), save::LoadResult::Loaded);
  EXPECT_EQ(back.daily, g.state.daily);
  EXPECT_EQ(back.daosAndMeridians, g.state.daosAndMeridians);
  EXPECT_TRUE(daos::found(back, Dao::Water));
}

TEST(V026Daily, RealActionsCountForTheGameTask) {
  Game g(kMonday, 1);
  g.state.progress.stamina = 10;
  g.visit(kMonday);
  auto setGameTask = [&](const Task t) {
    g.state.daily[3 + 2] = static_cast<uint8_t>(t);
    g.state.daily[6 + 2] = t == Task::Chart || t == Task::Gather ? 2 : 1;
    g.state.daily[9] = 0;
    g.state.daily[2] = static_cast<uint8_t>(g.state.daily[2] & ~0x10);
  };
  // Charting: each new tile counts once.
  setGameTask(Task::Chart);
  const explore::Layout l = explore::layoutOf(g.state, 0);
  explore::enter(g.state, l, 0);
  uint8_t next[4];
  const uint8_t n = explore::neighbours(explore::position(g.state, l, 0), next);
  bool stepped = false;
  for (uint8_t i = 0; i < n && !stepped; ++i) {
    if (explore::walkable(l, next[i]) && !explore::charted(g.state, 0, next[i])) {
      explore::step(g.state, l, 0, next[i]);
      stepped = true;
    }
  }
  ASSERT_TRUE(stepped);
  EXPECT_EQ(daily::progress(g.state, 2), 1);
  // Selling at a merchant.
  setGameTask(Task::Merchant);
  inventory::add(g.state, inventory::Item::TonicMinor, 1);
  ASSERT_TRUE(explore::sell(g.state, inventory::Item::TonicMinor));
  EXPECT_EQ(daily::progress(g.state, 2), 1);
  EXPECT_TRUE(daily::done(g.state, 2));
  // Opening a meridian point.
  setGameTask(Task::Meridian);
  g.state.progress.qi = QiNumber::fromU64(5000000);
  ASSERT_TRUE(meridians::open(g.state, 0));
  EXPECT_EQ(daily::progress(g.state, 2), 1);
}

TEST(V026Daily, TasksVaryFromDayToDay) {
  Game g(kMonday, 1);
  g.state.progress.stamina = 10;
  std::set<int> pairs;
  std::set<int> games;
  for (uint32_t d = kMonday; d < kMonday + 30; ++d) {
    g.visit(d);
    pairs.insert(static_cast<int>(daily::task(g.state, 0)) * 16 + static_cast<int>(daily::task(g.state, 1)));
    games.insert(static_cast<int>(daily::task(g.state, 2)));
  }
  EXPECT_GE(pairs.size(), 4u);
  EXPECT_GE(games.size(), 3u);
}
