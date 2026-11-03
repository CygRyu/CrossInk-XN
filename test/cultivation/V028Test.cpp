#include <BuiltinPack.h>
#include <Daily.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <GameState.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <HalStorage.h>
#include <Inventory.h>
#include <LifeLog.h>
#include <PackCheck.h>
#include <Rivals.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// v0.2.8 (milestone 9b + nemesis foes; agreed with the user in session 11,
// HANDOFF 14u): the rival, the rival ladder and nemeses.
using namespace cultivation;
using explore::Enemy;
using rivals::Temper;
using rivals::Trait;

namespace {

constexpr uint32_t kMonday = 9802;  // bridge::weekdayOf(9802) == 0

struct Reader {
  StatsSample s;
  std::set<uint32_t> days;

  void read(const uint32_t day, const uint32_t minutes) {
    const uint32_t sec = minutes * 60u;
    s.seconds += sec;
    s.pages += minutes;
    s.sessions += 1;
    s.timeOfDaySeconds[2] += sec;
    s.dayOfWeekSeconds[bridge::weekdayOf(day)] += sec;
    if (minutes > 0) days.insert(day);
  }
  StatsSample at(const uint32_t today) const {
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

  explicit Game(const uint32_t creationDay = kMonday, const uint8_t realm = 1,
                const Origin origin = Origin::StreetOrphan, const uint32_t seed = 0x5EED1234) {
    CharacterDraft d;
    snprintf(d.name, sizeof(d.name), "Lin Yue");
    d.destiny.purity = 50;
    d.destiny.talent = 2;
    d.destiny.element = Element::Fire;
    d.origin = origin;
    d.worldSeed = seed;
    createCharacter(state, d, reader.at(creationDay));
    state.progress.realm = realm;
    state.progress.hpMax = 100;
    state.progress.hp = 100;
  }

  rivals::News visit(const uint32_t today) {
    const StatsSample now = reader.at(today);
    const uint32_t lastDay = state.snapshot.lastVisitDay;
    const VisitGains g = bridge::collect(state.snapshot, now, watch);
    absorb(state, g);
    daily::update(state, g, now, lastDay);
    const rivals::News n = rivals::update(state, now);
    rivals::roam(state);
    return n;
  }

  void setTemper(const Temper t) { state.rivals[1] = static_cast<uint8_t>(t); }
  void setTalent(const uint8_t pct) { state.rivals[2] = pct; }
};

// The temperament a world rolls, without changing the world seed's other draws.
Game gameWith(const Temper t, const uint8_t talent = 100) {
  Game g;
  g.visit(kMonday);  // creates the rival
  g.setTemper(t);
  g.setTalent(talent);
  return g;
}

uint32_t creditedMinutes(const GameState& s) {
  return static_cast<uint32_t>(s.rivals[4]) | (static_cast<uint32_t>(s.rivals[5]) << 8) |
         (static_cast<uint32_t>(s.rivals[6]) << 16) | (static_cast<uint32_t>(s.rivals[7]) << 24);
}

struct Pack {
  std::string text;
  std::vector<PackSection> storage;
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;
  Pack() : text(builtinPackText(), builtinPackSize()), storage(512) {
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    PackParser parser(index, 0);
    parsePackText(text.data(), text.size(), parser);
    reader.setSource(0, text.data(), text.size());
    index.applyReplacements();
    grammar = std::make_unique<Grammar>(index, reader);
  }
};

bool clean(const char* s) { return std::string(s).find_first_of("{}[]|$") == std::string::npos; }

}  // namespace

// --- The rival -------------------------------------------------------------------------------

TEST(Rival, CreatedOnTheFirstVisitWithAHiddenTemperament) {
  Game g;
  const rivals::News n = g.visit(kMonday);
  EXPECT_TRUE(n.created);
  const rivals::Rival r = rivals::rival(g.state);
  EXPECT_TRUE(r.exists);
  EXPECT_FALSE(r.known);  // a Street orphan meets them on the road
  EXPECT_GE(r.talentPercent, 80);
  EXPECT_LE(r.talentPercent, 125);
  EXPECT_LT(static_cast<int>(r.temper), static_cast<int>(Temper::Count));
}

TEST(Rival, TheFallenClanHeirKnowsThemFromDayOne) {
  Game g(kMonday, 1, Origin::FallenClanHeir);
  g.visit(kMonday);
  EXPECT_TRUE(rivals::rival(g.state).known);
}

TEST(Rival, TemperamentWeightsAreAsAgreed) {
  int counts[static_cast<int>(Temper::Count)] = {};
  for (uint32_t seed = 1; seed <= 2000; ++seed) {
    Game g(kMonday, 1, Origin::StreetOrphan, seed * 7919u);
    g.visit(kMonday);
    ++counts[static_cast<int>(rivals::rival(g.state).temper)];
  }
  // 20 / 30 / 20 / 15 / 15
  EXPECT_NEAR(counts[0], 400, 70);
  EXPECT_NEAR(counts[1], 600, 80);
  EXPECT_NEAR(counts[2], 400, 70);
  EXPECT_NEAR(counts[3], 300, 60);
  EXPECT_NEAR(counts[4], 300, 60);
}

TEST(Rival, PacePerTemperament) {
  // Average virtual minutes a day over 1,000 days, before the elastic distance.
  const struct {
    Temper t;
    double lo, hi;
  } cases[] = {{Temper::Idle, 13.5, 16.5},
               {Temper::Steady, 28.5, 31.5},
               {Temper::Driven, 43.5, 46.5},
               {Temper::Erratic, 25, 35}};
  for (const auto& c : cases) {
    Game g = gameWith(c.t);
    uint64_t sum = 0;
    for (uint32_t d = kMonday; d < kMonday + 1000; ++d) sum += rivals::dayMinutes(g.state, d, 1);
    const double perDay = static_cast<double>(sum) / 10.0 / 1000.0;
    EXPECT_GT(perDay, c.lo) << static_cast<int>(c.t);
    EXPECT_LT(perDay, c.hi) << static_cast<int>(c.t);
  }
  // Erratic: most days nothing, the rest two hours.
  Game e = gameWith(Temper::Erratic);
  for (uint32_t d = kMonday; d < kMonday + 50; ++d) {
    const uint32_t m = rivals::dayMinutes(e.state, d, 1);
    EXPECT_TRUE(m == 0 || m == 1200) << m;
  }
}

TEST(Rival, AVisitCreditsEveryDayAtTheirPace) {
  Game g = gameWith(Temper::Steady);
  g.state.progress.realm = 0;  // the player stays within a realm of them: no elastic distance
  for (uint32_t d = kMonday + 1; d <= kMonday + 30; ++d) g.reader.read(d, 30);
  uint32_t expected = 0;
  for (uint32_t d = kMonday + 1; d <= kMonday + 30; ++d) expected += rivals::dayMinutes(g.state, d, 0);
  const rivals::News n = g.visit(kMonday + 30);
  EXPECT_EQ(n.daysCredited, 30u);
  EXPECT_EQ(creditedMinutes(g.state), expected);  // a month at 30 min/day: still within reach of the player
}

TEST(Rival, DayAmountsAreSeededByWorldAndDate) {
  Game a = gameWith(Temper::Steady);
  Game b = gameWith(Temper::Steady);
  for (uint32_t d = kMonday; d < kMonday + 30; ++d) {
    EXPECT_EQ(rivals::dayMinutes(a.state, d, 1), rivals::dayMinutes(b.state, d, 1));
    const uint32_t m = rivals::dayMinutes(a.state, d, 1);
    EXPECT_GE(m, 200u);
    EXPECT_LE(m, 400u);
  }
}

TEST(Rival, LateBloomerSpeedsUpByRealm) {
  Game g = gameWith(Temper::LateBloomer);
  uint32_t sum0 = 0;
  uint32_t sum5 = 0;
  for (uint32_t d = kMonday; d < kMonday + 300; ++d) {
    sum0 += rivals::dayMinutes(g.state, d, 0);
    sum5 += rivals::dayMinutes(g.state, d, 6);  // 15 + 30 = 45 (the cap)
  }
  EXPECT_NEAR(sum0 / 300.0 / 10.0, 15.0, 1.5);
  EXPECT_NEAR(sum5 / 300.0 / 10.0, 45.0, 1.5);
}

TEST(Rival, PositionFollowsTheMedianHoursAndTalent) {
  uint8_t realm = 0;
  uint8_t stage = 0;
  rivals::positionFor(0, 100, realm, stage);
  EXPECT_EQ(realm, 0);
  EXPECT_EQ(stage, 0);
  // 4.0 h finish Body Tempering at talent 100.
  rivals::positionFor(4u * 600u, 100, realm, stage);
  EXPECT_EQ(realm, 1);
  EXPECT_EQ(stage, 0);
  rivals::positionFor(4u * 600u - 1u, 100, realm, stage);
  EXPECT_EQ(realm, 0);
  EXPECT_EQ(stage, 8);
  // A faster talent (80%) needs less.
  rivals::positionFor(4u * 600u * 80u / 100u, 80, realm, stage);
  EXPECT_EQ(realm, 1);
  // Never past the last named realm.
  rivals::positionFor(0xFFFFFFFFu, 100, realm, stage);
  EXPECT_EQ(realm, 9);
}

TEST(Rival, NothingMovesWithoutADate) {
  Game g = gameWith(Temper::Steady);
  const uint32_t before = creditedMinutes(g.state);
  g.reader.read(kMonday + 3, 60);
  g.visit(StatsSnapshot::kNoDay);
  EXPECT_EQ(creditedMinutes(g.state), before);
}

TEST(Rival, PausesAfterSevenDaysWithoutReading) {
  Game g = gameWith(Temper::Steady);
  g.state.progress.realm = 1;
  g.reader.read(kMonday, 30);
  // Reads every day for 10 days, then stops for 20.
  for (uint32_t d = kMonday + 1; d <= kMonday + 10; ++d) g.reader.read(d, 30);
  g.visit(kMonday + 10);
  const uint32_t a = creditedMinutes(g.state);
  const rivals::News n = g.visit(kMonday + 30);
  const uint32_t b = creditedMinutes(g.state);
  // Days 11-17 are unread but credited (the first seven), days 18-30 are a pause.
  uint32_t expected = 0;
  for (uint32_t d = kMonday + 11; d <= kMonday + 17; ++d) expected += rivals::dayMinutes(g.state, d, 1);
  EXPECT_NEAR(static_cast<double>(b - a), static_cast<double>(expected), expected * 0.6);
  EXPECT_TRUE(n.hiatusBegan);
  EXPECT_TRUE(rivals::rival(g.state).hiatus);
  // Reading again ends it, without a catch-up.
  g.reader.read(kMonday + 31, 20);
  const rivals::News back = g.visit(kMonday + 31);
  EXPECT_TRUE(back.hiatusEnded);
  EXPECT_FALSE(rivals::rival(g.state).hiatus);
  EXPECT_LE(creditedMinutes(g.state) - b, 600u);  // one day, at most x1.5
}

TEST(Rival, ElasticDistance) {
  Game g = gameWith(Temper::Steady);
  for (uint32_t d = kMonday + 1; d <= kMonday + 40; ++d) g.reader.read(d, 30);
  // Rival far ahead of the player: half pace.
  g.state.progress.realm = 1;
  g.state.rivals[4] = 0;                // put the rival in Foundation Establishment (5,000 deci-hours ~ 50 h)
  const uint32_t found = 30000u * 10u;  // 300,000 deci-minutes = 500 h: Core Formation or later
  g.state.rivals[4] = static_cast<uint8_t>(found);
  g.state.rivals[5] = static_cast<uint8_t>(found >> 8);
  g.state.rivals[6] = static_cast<uint8_t>(found >> 16);
  g.state.rivals[7] = static_cast<uint8_t>(found >> 24);
  uint32_t plain = 0;
  for (uint32_t d = kMonday + 1; d <= kMonday + 20; ++d) plain += rivals::dayMinutes(g.state, d, 3);
  g.visit(kMonday + 20);
  EXPECT_EQ(creditedMinutes(g.state) - found, plain / 2u);  // half pace
}

TEST(Rival, RecordAndReactions) {
  Game g = gameWith(Temper::Driven);
  rivals::beaten(g.state, kMonday + 2);
  rivals::lostTo(g.state);
  rivals::lostTo(g.state);
  const rivals::Rival r = rivals::rival(g.state);
  EXPECT_EQ(r.wins, 1);
  EXPECT_EQ(r.losses, 2);
  // Driven: +20% for two weeks after the defeat.
  Game plain = gameWith(Temper::Driven);
  EXPECT_EQ(rivals::dayMinutes(g.state, kMonday + 3, 1), rivals::dayMinutes(plain.state, kMonday + 3, 1) * 12u / 10u);
  EXPECT_EQ(rivals::dayMinutes(g.state, kMonday + 15, 1), rivals::dayMinutes(plain.state, kMonday + 15, 1) * 12u / 10u);
  EXPECT_EQ(rivals::dayMinutes(g.state, kMonday + 16, 1), rivals::dayMinutes(plain.state, kMonday + 16, 1));
  // Idle: at most 20 a day while sulking.
  Game idle = gameWith(Temper::Idle);
  rivals::beaten(idle.state, kMonday);
  for (uint32_t d = kMonday; d < kMonday + 14; ++d) EXPECT_LE(rivals::dayMinutes(idle.state, d, 1), 200u);
}

TEST(Rival, MeetsYouOnlyInRegionsTheirRealmHasOpened) {
  Game g = gameWith(Temper::Steady);
  EXPECT_FALSE(rivals::canMeetIn(g.state, 0));  // still in Body Tempering
  const uint32_t m = 4u * 600u + 10u;           // just into Qi Condensation
  memcpy(g.state.rivals.data() + 4, &m, 4);
  EXPECT_TRUE(rivals::canMeetIn(g.state, 0));
  EXPECT_FALSE(rivals::canMeetIn(g.state, 1));
}

TEST(Rival, AMeetingWithTheRivalIsASparWithoutHp) {
  Game g = gameWith(Temper::Steady);
  const uint32_t m = 4u * 600u + 10u;
  memcpy(g.state.rivals.data() + 4, &m, 4);
  const explore::Layout l = explore::layoutOf(g.state, 0);
  explore::enter(g.state, l, 0);
  g.state.exploration[4] = 1;
  g.state.exploration[5] = static_cast<uint8_t>(explore::FightKind::Meeting);
  g.state.exploration[7] = explore::kRivalSlot;
  const explore::Meeting meet = explore::pendingMeeting(g.state, l);
  ASSERT_TRUE(meet.active);
  EXPECT_TRUE(meet.rival);
  EXPECT_TRUE(meet.firstRival);
  EXPECT_EQ(meet.offer, explore::Offer::Spar);
  EXPECT_EQ(meet.damage, 0);
  EXPECT_EQ(meet.level, rivals::rivalLevel(g.state));
  g.state.progress.hp = 50;
  const explore::MeetResult r = explore::meet(g.state, l, explore::MeetChoice::Offer);
  EXPECT_TRUE(r.done);
  EXPECT_TRUE(r.rival);
  EXPECT_EQ(g.state.progress.hp, 50);  // never HP
  const rivals::Rival after = rivals::rival(g.state);
  EXPECT_TRUE(after.known);
  EXPECT_TRUE(after.metBefore);
  EXPECT_EQ(after.wins + after.losses, 1);
  // Not remembered as a road cultivator.
  uint8_t region = 0;
  uint16_t id = 0;
  uint8_t meets = 0;
  explore::MeetOutcome outcome;
  EXPECT_FALSE(explore::metCultivator(g.state, 0, region, id, meets, outcome));
}

TEST(Rival, LevelFromTheirOwnRealmNeverScaledToYou) {
  Game g = gameWith(Temper::Steady);
  const uint32_t m = 4u * 600u + 10u;
  memcpy(g.state.rivals.data() + 4, &m, 4);
  const int before = rivals::rivalLevel(g.state);
  EXPECT_EQ(before, 10 + 0 + 2);  // Qi Condensation, Layer 1, + 2 x realm
  g.state.progress.realm = 5;
  EXPECT_EQ(rivals::rivalLevel(g.state), before);
}

TEST(Rival, OvertakingIsNewsOnceKnown) {
  Game g(kMonday, 1, Origin::FallenClanHeir);
  g.visit(kMonday);
  g.setTemper(Temper::Steady);
  const uint32_t ahead = 4u * 600u + 3000u;  // ahead of a Qi Condensation Layer 1 player
  memcpy(g.state.rivals.data() + 4, &ahead, 4);
  g.state.progress.stage = 0;
  g.visit(kMonday);  // remembers that you are behind
  g.state.progress.stage = 8;
  const rivals::News n = g.visit(kMonday);
  EXPECT_TRUE(n.youOvertook);
  // Level with each other: no news either way.
  uint8_t realm = 0;
  uint8_t stage = 0;
  rivals::positionFor(ahead, rivals::rival(g.state).talentPercent, realm, stage);
  g.state.progress.realm = realm;
  g.state.progress.stage = stage;
  const rivals::News level = g.visit(kMonday);
  EXPECT_FALSE(level.youOvertook);
  EXPECT_FALSE(level.theyOvertook);
}

// --- The ladder ------------------------------------------------------------------------------

namespace {
Game ladderGame() {
  Game g = gameWith(Temper::Steady);
  g.state.progress.realm = 1;
  g.reader.read(kMonday + 1, 10);
  g.visit(kMonday + 1);
  return g;
}
}  // namespace

TEST(Ladder, StartsUnrankedAndClimbsOneRankADay) {
  Game g = ladderGame();
  EXPECT_EQ(rivals::playerRank(g.state), 0);
  rivals::Bout b = rivals::challengeBout(g.state);
  EXPECT_EQ(b.block, rivals::Block::None);
  EXPECT_EQ(b.rank, 20);
  EXPECT_EQ(b.hpCost, 10);
  EXPECT_EQ(b.stones, 40u);  // 20 x (realm + 1)
  int climbed = 0;
  for (uint32_t d = kMonday + 1; d < kMonday + 60; ++d) {
    if (d > kMonday + 1) g.visit(d);
    g.state.progress.hp = 100;
    const uint8_t before = rivals::playerRank(g.state);
    const rivals::BoutResult r = rivals::challenge(g.state);
    ASSERT_TRUE(r.done);
    if (rivals::playerRank(g.state) == 1) break;
    // A second challenge the same day is refused.
    EXPECT_EQ(rivals::challengeBout(g.state).block, rivals::Block::DoneToday);
    EXPECT_FALSE(rivals::challenge(g.state).done);
    if (r.won) {
      ++climbed;
      EXPECT_EQ(rivals::playerRank(g.state), before == 0 ? 20 : before - 1);
    } else {
      EXPECT_EQ(rivals::playerRank(g.state), before);
      EXPECT_EQ(r.hpLost, 10);
    }
  }
  EXPECT_GT(climbed, 3);
}

TEST(Ladder, LevelsRunFromSixBelowToEightAbove) {
  EXPECT_EQ(rivals::rankOffset(20), -6);
  EXPECT_EQ(rivals::rankOffset(1), 8);
  for (uint8_t r = 1; r < 20; ++r) EXPECT_GE(rivals::rankOffset(r), rivals::rankOffset(r + 1));
}

TEST(Ladder, ALossNeedsTheHp) {
  Game g = ladderGame();
  g.state.progress.hp = 9;
  EXPECT_EQ(rivals::challengeBout(g.state).block, rivals::Block::NoHp);
  EXPECT_FALSE(rivals::challenge(g.state).done);
}

TEST(Ladder, NoBoutsWithoutADate) {
  Game g = ladderGame();
  g.visit(StatsSnapshot::kNoDay);
  EXPECT_FALSE(daily::live(g.state));
  EXPECT_EQ(rivals::challengeBout(g.state).block, rivals::Block::NoDate);
}

TEST(Ladder, DefenceCanBeDeclinedForFree) {
  Game g = ladderGame();
  g.state.rivals[17] = 10;  // rank 10
  rivals::Bout d = rivals::defenceBout(g.state);
  EXPECT_EQ(d.block, rivals::Block::None);
  EXPECT_EQ(d.rank, 11);
  const uint16_t hp = g.state.progress.hp;
  EXPECT_TRUE(rivals::declineDefence(g.state));
  EXPECT_EQ(rivals::playerRank(g.state), 10);
  EXPECT_EQ(g.state.progress.hp, hp);
  EXPECT_EQ(rivals::defenceBout(g.state).block, rivals::Block::DoneToday);
}

TEST(Ladder, ALostDefenceSwapsRanks) {
  // Search days until one is lost (the roll is seeded by the day).
  bool lost = false;
  for (uint32_t d = kMonday + 2; d < kMonday + 200 && !lost; ++d) {
    Game g = ladderGame();
    g.state.rivals[17] = 5;
    g.visit(d);
    g.state.progress.hp = 100;
    const rivals::BoutResult r = rivals::acceptDefence(g.state);
    ASSERT_TRUE(r.done);
    if (!r.won) {
      lost = true;
      EXPECT_EQ(rivals::playerRank(g.state), 6);
      EXPECT_EQ(g.state.progress.hp, 90);
    } else {
      EXPECT_EQ(rivals::playerRank(g.state), 5);
    }
  }
  EXPECT_TRUE(lost);
}

TEST(Ladder, NeverLosesRankWhileAway) {
  Game g = ladderGame();
  g.state.rivals[17] = 7;
  for (uint32_t d = kMonday + 2; d < kMonday + 100; ++d) g.visit(d);  // never answering
  EXPECT_EQ(rivals::playerRank(g.state), 7);
}

TEST(Ladder, ANewRealmStartsANewLadder) {
  Game g = ladderGame();
  g.state.rivals[17] = 2;
  EXPECT_EQ(rivals::titleRank(g.state), 2);
  g.state.progress.realm = 2;
  EXPECT_EQ(rivals::playerRank(g.state), 0);
  EXPECT_EQ(rivals::titleRank(g.state), 0);
  g.visit(kMonday + 3);
  EXPECT_EQ(g.state.rivals[16], 3);  // realm + 1
}

TEST(Ladder, OrderShiftsAroundThePlayerAndTheRivalHasASeat) {
  Game g = ladderGame();
  const uint32_t m = 4u * 600u + 10u;  // rival in Qi Condensation, Layer 1: seat 18
  memcpy(g.state.rivals.data() + 4, &m, 4);
  EXPECT_EQ(rivals::rivalSeat(g.state), 18);
  EXPECT_EQ(rivals::rivalRank(g.state), 18);
  g.state.rivals[17] = 10;
  EXPECT_TRUE(rivals::rung(g.state, 10).player);
  EXPECT_EQ(rivals::rung(g.state, 9).seat, 9);
  EXPECT_EQ(rivals::rung(g.state, 11).seat, 10);
  EXPECT_EQ(rivals::rivalRank(g.state), 19);
  EXPECT_TRUE(rivals::rung(g.state, 19).rival);
}

// --- Nemeses ---------------------------------------------------------------------------------

namespace {
GameState explorer() {
  Game g = gameWith(Temper::Steady);
  g.state.progress.realm = 3;
  g.state.progress.hp = g.state.progress.hpMax = 200;
  return g.state;
}
}  // namespace

TEST(Nemesis, AtMostThree) {
  GameState s = explorer();
  for (int i = 0; i < 3; ++i) EXPECT_EQ(rivals::bear(s, Enemy::Beast, 0, 12, 100u + i, 7u), i);
  EXPECT_EQ(rivals::bear(s, Enemy::Bandit, 0, 12, 200u, 7u), -1);
  EXPECT_EQ(rivals::nemesisCount(s), 3);
  EXPECT_EQ(rivals::bear(s, Enemy::Spirit, 0, 12, 1u, 7u), -1);
}

TEST(Nemesis, BornWithOneHiddenTrait) {
  GameState s = explorer();
  const int i = rivals::bear(s, Enemy::Bandit, 1, 23, 4242u, 9u);
  rivals::Nemesis n;
  ASSERT_TRUE(rivals::nemesis(s, static_cast<uint8_t>(i), n));
  EXPECT_EQ(n.kind, Enemy::Bandit);
  EXPECT_EQ(n.region, 1);
  EXPECT_EQ(n.level, 23);
  EXPECT_EQ(n.traitCount, 1);
  EXPECT_EQ(n.revealed, 0);
  EXPECT_FALSE(n.elementKnown);
  EXPECT_EQ(n.losses, 1);
  EXPECT_TRUE(rivals::resting(s, n));
}

TEST(Nemesis, NeverDiesAndGrows) {
  GameState s = explorer();
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Beast, 0, 12, 77u, 1u));
  for (int k = 0; k < 6; ++k) rivals::youWon(s, i, 1000u + k);
  rivals::Nemesis n;
  ASSERT_TRUE(rivals::nemesis(s, i, n));
  EXPECT_EQ(n.wins, 6);
  EXPECT_EQ(n.traitCount, rivals::kMaxTraits);  // one new trait per escape, at most four
  EXPECT_EQ(n.epithet, rivals::EpithetKind::Scar);
  // You noticed traits from the second fight on.
  EXPECT_NE(n.revealed, 0);
  EXPECT_TRUE(n.elementKnown);
  // Winning: +2 levels, the Victor, capped at your level + 12.
  const int you = explore::playerLevel(s);
  for (int k = 0; k < 40; ++k) rivals::youLost(s, i, you, 0u);
  ASSERT_TRUE(rivals::nemesis(s, i, n));
  EXPECT_EQ(n.level, you + 12);
  EXPECT_EQ(n.epithet, rivals::EpithetKind::Victor);
  EXPECT_TRUE(n.wonLast);
}

TEST(Nemesis, TraitsAreHalfStrengthsAndNeverContradict) {
  int strengths = 0;
  int total = 0;
  for (uint32_t seed = 1; seed < 400; ++seed) {
    GameState s = explorer();
    const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Bandit, seed % 9, 20, seed * 31u, seed));
    for (int k = 0; k < 3; ++k) rivals::youWon(s, i, seed * 7u + k);
    rivals::Nemesis n;
    ASSERT_TRUE(rivals::nemesis(s, i, n));
    EXPECT_FALSE(rivals::hasTrait(n, Trait::Hunter) && rivals::hasTrait(n, Trait::Slow));
    EXPECT_FALSE(rivals::hasTrait(n, Trait::Coward) && rivals::hasTrait(n, Trait::Proud));
    EXPECT_FALSE(rivals::hasTrait(n, Trait::Overcoming) && rivals::hasTrait(n, Trait::Overcome));
    if (rivals::hasTrait(n, Trait::Overcoming)) {
      EXPECT_TRUE(rivals::overcomes(n.element, s.character.rootElement));
    }
    if (rivals::hasTrait(n, Trait::Overcome)) {
      EXPECT_TRUE(rivals::overcomes(s.character.rootElement, n.element));
    }
    std::set<int> seen;
    for (uint8_t t = 0; t < n.traitCount; ++t) {
      EXPECT_TRUE(seen.insert(static_cast<int>(n.traits[t])).second);
      strengths += rivals::isStrength(n.traits[t]) ? 1 : 0;
      ++total;
    }
  }
  EXPECT_NEAR(static_cast<double>(strengths) / total, 0.5, 0.06);
}

TEST(Nemesis, TraitsChangeTheFight) {
  GameState s = explorer();
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Beast, 0, 30, 5u, 5u));
  // Replace its traits by hand: Iron hide and Fears the dawn.
  uint8_t* b = s.rivals.data() + 24 + 24 * i;
  b[4] = static_cast<uint8_t>(static_cast<uint8_t>(Trait::IronHide) | (static_cast<uint8_t>(Trait::FearsDawn) << 4));
  b[5] = 0;
  b[6] = 2 << 4;
  rivals::Nemesis n;
  ASSERT_TRUE(rivals::nemesis(s, i, n));
  EXPECT_EQ(rivals::fightLevel(s, n, 20), 33);
  EXPECT_EQ(rivals::fightLevel(s, n, 7), 30);
  EXPECT_EQ(rivals::fightLevel(s, n, -1), 33);
  // Never below the road it walks: region 4 starts at 52.
  b[2] = 4;
  ASSERT_TRUE(rivals::nemesis(s, i, n));
  EXPECT_EQ(rivals::fightLevel(s, n, 20), 52 + 3);
}

TEST(Nemesis, RoamsTowardWhereYouExploreOnReadingTime) {
  GameState s = explorer();
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Beast, 0, 12, 5u, 5u));
  rivals::setPlayerRegion(s, 2);
  rivals::roam(s);
  rivals::Nemesis n;
  rivals::nemesis(s, i, n);
  EXPECT_EQ(n.region, 0);
  s.progress.gameReadingSeconds += 2 * 3600;  // 2 reading hours: one region
  rivals::roam(s);
  rivals::nemesis(s, i, n);
  EXPECT_EQ(n.region, 1);
  s.progress.gameReadingSeconds += 10 * 3600;
  rivals::roam(s);
  rivals::nemesis(s, i, n);
  EXPECT_EQ(n.region, 2);  // and stays with you
  EXPECT_TRUE(rivals::hunting(s, n));
}

TEST(Nemesis, FindsYouOnSixPercentOfStepsUnlessResting) {
  GameState s = explorer();
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Beast, 0, 12, 5u, 5u));
  int found = 0;
  for (uint32_t step = 0; step < 4000; ++step) found += rivals::ambush(s, 0, step * 2654435761u) >= 0 ? 1 : 0;
  EXPECT_EQ(found, 0);  // resting after the fight that made it
  s.progress.gameReadingSeconds += 3 * 3600;
  for (uint32_t step = 0; step < 4000; ++step) found += rivals::ambush(s, 0, step * 2654435761u) >= 0 ? 1 : 0;
  EXPECT_NEAR(found / 4000.0, 0.06, 0.015);
  EXPECT_EQ(rivals::ambush(s, 1, 0u), -1);  // not in another region
  (void)i;
}

TEST(Nemesis, ABeatenReturningFoeOrALostFightCanMakeOne) {
  // Lose Dangerous-or-harder random fights until a nemesis is born (35%).
  int born = 0;
  for (uint32_t seed = 1; seed <= 300; ++seed) {
    Game g(kMonday, 1, Origin::StreetOrphan, seed * 1009u);
    g.state.progress.realm = 1;
    g.state.progress.hp = g.state.progress.hpMax = 1000;
    const explore::Layout l = explore::layoutOf(g.state, 0);
    explore::enter(g.state, l, 0);
    g.state.exploration[4] = 1;
    g.state.exploration[5] = static_cast<uint8_t>(explore::FightKind::Beast);
    const explore::Fight f = explore::pendingFight(g.state, l);
    if (f.enemy == Enemy::Spirit || f.danger < explore::Danger::Dangerous) continue;
    const explore::FightResult r = explore::fight(g.state, l, explore::FightChoice::Fight);
    if (r.won) continue;
    if (r.nemesisBorn >= 0) {
      ++born;
      rivals::Nemesis n;
      ASSERT_TRUE(rivals::nemesis(g.state, static_cast<uint8_t>(r.nemesisBorn), n));
      EXPECT_EQ(n.kind, f.enemy);
      EXPECT_EQ(n.level, f.level);
    }
  }
  EXPECT_GT(born, 5);
}

TEST(Nemesis, AFightWithOneUsesItsTraits) {
  GameState s = explorer();
  s.progress.realm = 1;
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Beast, 0, 12, 5u, 5u));
  uint8_t* b = s.rivals.data() + 24 + 24 * i;
  b[4] = static_cast<uint8_t>(static_cast<uint8_t>(Trait::Hunter) | (static_cast<uint8_t>(Trait::Venomous) << 4));
  b[6] = 2 << 4;
  const explore::Layout l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  s.exploration[4] = 1;
  s.exploration[5] = static_cast<uint8_t>(explore::FightKind::Beast);
  s.exploration[7] = static_cast<uint8_t>(explore::kNemesisSlot | (i + 1));
  const explore::Fight f = explore::pendingFight(s, l);
  EXPECT_EQ(f.nemesis, i);
  EXPECT_FALSE(f.canSlip);  // Hunter
  // Venomous: 1.5x the plain loss.
  b[4] = static_cast<uint8_t>(Trait::Hunter);
  b[6] = 1 << 4;
  const explore::Fight plain = explore::pendingFight(s, l);
  EXPECT_EQ(f.damage, plain.damage * 3 / 2);
  // Losing makes it stronger, winning scars it; it is never gone.
  const explore::FightResult r = explore::fight(s, l, explore::FightChoice::Fight);
  rivals::Nemesis n;
  ASSERT_TRUE(rivals::nemesis(s, i, n));
  EXPECT_EQ(r.nemesis, i);
  if (r.won) {
    EXPECT_EQ(n.wins, 1);
  } else {
    EXPECT_EQ(n.level, 14);
  }
}

TEST(Nemesis, ACowardRunsWhenYourChanceIsGood) {
  // Strong player, Coward nemesis in region 0: a step that finds it does not start a fight.
  for (uint32_t seed = 1; seed < 200; ++seed) {
    Game g(kMonday, 1, Origin::StreetOrphan, seed * 31u);
    g.state.progress.realm = 3;  // far above region 0
    g.state.progress.stamina = 20;
    g.state.progress.hp = g.state.progress.hpMax = 200;
    const auto i = static_cast<uint8_t>(rivals::bear(g.state, Enemy::Beast, 0, 12, 5u, 5u));
    uint8_t* b = g.state.rivals.data() + 24 + 24 * i;
    b[4] = static_cast<uint8_t>(Trait::Coward);
    b[6] = 1 << 4;
    b[20] = b[21] = 0;  // not resting
    const explore::Layout l = explore::layoutOf(g.state, 0);
    explore::enter(g.state, l, 0);
    uint8_t n[4];
    const uint8_t c = explore::neighbours(l.entrance, n);
    for (uint8_t k = 0; k < c; ++k) {
      if (explore::canStep(g.state, l, 0, n[k]) != explore::StepError::None) continue;
      const explore::StepResult r = explore::step(g.state, l, 0, n[k]);
      if (r.nemesisFled >= 0) {
        EXPECT_FALSE(r.fight);
        rivals::Nemesis nm;
        rivals::nemesis(g.state, i, nm);
        EXPECT_EQ(nm.fled, 1);
        SUCCEED();
        return;
      }
      break;
    }
  }
  ADD_FAILURE() << "no Coward ever found the player";
}

TEST(Nemesis, TalkAndRumorsRevealTraits) {
  GameState s = explorer();
  const auto i = static_cast<uint8_t>(rivals::bear(s, Enemy::Bandit, 0, 12, 5u, 5u));
  rivals::Trait t = Trait::None;
  EXPECT_EQ(rivals::revealOne(s, t, 0u), i);
  EXPECT_NE(t, Trait::None);
  rivals::Nemesis n;
  rivals::nemesis(s, i, n);
  EXPECT_TRUE(rivals::traitKnown(n, t));
  EXPECT_TRUE(n.elementKnown);
  EXPECT_EQ(rivals::revealOne(s, t, 0u), -1);  // nothing hidden any more
  // Rumors: one day in four, only while something is hidden.
  GameState r = explorer();
  rivals::bear(r, Enemy::Beast, 0, 12, 9u, 9u);
  int days = 0;
  for (uint32_t d = kMonday; d < kMonday + 400; ++d) days += rivals::rumorPick(r, d, t) >= 0 ? 1 : 0;
  EXPECT_NEAR(days / 400.0, 0.25, 0.07);
}

TEST(Nemesis, PayingCosts) {
  rivals::Nemesis n;
  n.kind = Enemy::Bandit;
  n.region = 2;
  EXPECT_EQ(rivals::payCost(n), 45u);
  n.kind = Enemy::Beast;
  EXPECT_EQ(rivals::payCost(n), 0u);
  n.kind = Enemy::Cultivator;
  n.traits[0] = Trait::Greedy;
  n.traitCount = 1;
  EXPECT_EQ(rivals::payCost(n), 23u);
}

// --- Save --------------------------------------------------------------------------------------

TEST(SaveV6, AVersionFiveSaveLoadsWithNoRivalsYet) {
  Storage.reset();
  Game g = gameWith(Temper::Driven);
  g.state.rivals[13] = 4;
  rivals::bear(g.state, Enemy::Beast, 0, 12, 5u, 5u);
  ASSERT_TRUE(save::store(g.state));
  auto& bytes = *Storage.bytesOf(save::kSavePath);
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV5);
  bytes[2] = 5;
  bytes[3] = 0;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV5);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV5 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV5);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  for (const uint8_t b : s.rivals) EXPECT_EQ(b, 0u);
  EXPECT_FALSE(rivals::rival(s).exists);
  EXPECT_EQ(rivals::nemesisCount(s), 0);
  // The first v0.2.8 visit creates the rival, credited from the creation day.
  Reader reader;
  reader.read(kMonday + 5, 30);
  StatsSample now = reader.at(kMonday + 5);
  rivals::update(s, now);
  EXPECT_TRUE(rivals::rival(s).exists);
  EXPECT_GT(creditedMinutes(s), 0u);
}

TEST(SaveV6, RoundTrip) {
  Storage.reset();
  Game g = gameWith(Temper::Erratic);
  rivals::bear(g.state, Enemy::Cultivator, 1, 22, 99u, 3u);
  g.state.rivals[17] = 4;
  ASSERT_TRUE(save::store(g.state));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(s.rivals, g.state.rivals);
  EXPECT_EQ(Storage.bytesOf(save::kSavePath)->size(), save::kFileSize);
}

// --- Text --------------------------------------------------------------------------------------

TEST(RivalText, NamesAndListsExpandCleanly) {
  Pack pack;
  Grammar& grammar = *pack.grammar;
  for (uint32_t seed = 1; seed < 60; ++seed) {
    Game g(kMonday, 2, Origin::StreetOrphan, seed * 131u);
    g.visit(kMonday);
    char name[64];
    char sect[64];
    ASSERT_TRUE(exploretext::rivalName(g.state, grammar, name, sizeof(name), sect, sizeof(sect)));
    EXPECT_TRUE(clean(name)) << name;
    EXPECT_EQ(std::string(sect).rfind("the ", 0), 0u) << sect;
    char seat[64];
    ASSERT_TRUE(exploretext::seatName(g.state, rivals::seatSeed(g.state, 3), grammar, seat, sizeof(seat)));
    EXPECT_TRUE(clean(seat)) << seat;
    for (const Enemy kind : {Enemy::Beast, Enemy::Bandit, Enemy::Cultivator}) {
      GameState s = g.state;
      const int i = rivals::bear(s, kind, static_cast<uint8_t>(seed % 9), 20, seed * 77u, seed);
      char nm[exploretext::kNameSize];
      ASSERT_TRUE(exploretext::nemesisName(s, static_cast<uint8_t>(i), grammar, nm, sizeof(nm)));
      EXPECT_NE(std::string(nm).find(" the "), std::string::npos) << nm;
      EXPECT_EQ(std::string(nm).find(" of the "), std::string::npos) << nm;
      EXPECT_TRUE(clean(nm)) << nm;
      char kindText[exploretext::kNameSize];
      ASSERT_TRUE(exploretext::nemesisKind(s, static_cast<uint8_t>(i), grammar, kindText, sizeof(kindText)));
    }
  }
}

TEST(RivalText, ARoadCultivatorKeepsTheirNameAsANemesis) {
  Pack pack;
  Game g(kMonday, 2);
  for (uint32_t seed = 1; seed < 40; ++seed) {
    GameState s = g.state;
    char met[exploretext::kNameSize];
    ASSERT_TRUE(exploretext::cultivatorName(s, 1, seed * 911u, *pack.grammar, met, sizeof(met)));
    const int i = rivals::bear(s, Enemy::Cultivator, 1, 20, seed * 911u, 1u);
    char nm[exploretext::kNameSize];
    ASSERT_TRUE(exploretext::nemesisName(s, static_cast<uint8_t>(i), *pack.grammar, nm, sizeof(nm)));
    const std::string base = std::string(met).substr(0, std::string(met).find(" of the "));
    EXPECT_EQ(std::string(nm).rfind(base + " the ", 0), 0u) << met << " / " << nm;
  }
}

TEST(RivalText, EveryNewListFitsTheScreenAndTheChronicle) {
  Pack pack;
  Game g(kMonday, 2);
  const char* screen[] = {"rival.meet.first",   "rival.meet.again",      "rival.meet.trade",
                          "rival.talk",         "rival.sparwon",         "rival.sparlost",
                          "rival.say.idle",     "rival.say.steady",      "rival.say.driven",
                          "rival.say.erratic",  "rival.say.late",        "rival.react.idle",
                          "rival.react.steady", "rival.react.driven",    "rival.react.erratic",
                          "rival.react.late",   "explore.fight.nemesis", "explore.fight.nemesis.person",
                          "rumor.nemesis"};
  for (const char* key : screen) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      char out[300];
      ASSERT_TRUE(exploretext::personFlavor(g.state, key, seed, "the Reedwater Marshes", "Qiu Mingyuan",
                                            "the Azure Cloud Sect", *pack.grammar, out, sizeof(out)))
          << key;
      EXPECT_TRUE(clean(out)) << out;
      EXPECT_LT(strlen(out), 191u) << key << ": " << out;
    }
  }
  const char* chronicle[] = {"rival.line.first",     "rival.line.sparwon",   "rival.line.sparlost",
                             "rival.line.trade",     "rival.line.talk",      "rival.line.realm",
                             "rival.line.seclusion", "rival.line.back",      "rival.line.overtook",
                             "rival.line.overtaken", "nemesis.line.born",    "nemesis.line.won",
                             "nemesis.line.lost",    "nemesis.line.escaped", "nemesis.line.fled",
                             "ladder.line.won",      "ladder.line.lost",     "ladder.line.defended",
                             "ladder.line.fell",     "demon.history.rival",  "demon.history.nemesis"};
  for (const char* key : chronicle) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      lifelog::Line line;
      ASSERT_TRUE(exploretext::personLine(line, g.state, key, seed, "the Reedwater Marshes",
                                          "Knife-Hand Wu the Twice-Burned", "Foundation Establishment", *pack.grammar))
          << key;
      EXPECT_TRUE(clean(line.text)) << line.text;
      EXPECT_LT(strlen(line.text), lifelog::kLineSize - 1) << line.text;
    }
  }
}

TEST(RivalText, TheBuiltInPackStillFitsTheDeviceIndex) {
  Pack pack;
  // PackStore::kMaxSections is 384: leave room for the player's own SD packs.
  EXPECT_LT(pack.index.count, 320u);
}
