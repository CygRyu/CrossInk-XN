#include <Agenda.h>
#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <GameState.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <HalStorage.h>
#include <LifeLog.h>
#include <PackStore.h>
#include <Rivals.h>
#include <Sects.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// v0.3.4 "The rival's agenda" (designed and agreed with the user in session 15,
// HANDOFF 14o): seven goals on the world's clock, the rival's opinion of you, the
// duel, closed doors, sect ranks, an inheritance's talent.

using namespace cultivation;
using agenda::Goal;

namespace {

constexpr uint32_t kMonday = 9802;

void setRivalMinutes(GameState& s, const uint32_t m) { memcpy(s.rivals.data() + 4, &m, 4); }
void setCounter(GameState& s, const uint32_t c) { memcpy(s.exploration.data(), &c, 4); }  // the step counter

// A character at `realm` with a rival a little ahead (realm 3 Early, so regions 0-2 are theirs too),
// the sects founded, and no date (nothing but reading moves).
GameState world(const uint32_t seed, const uint8_t realm = 3) {
  GameState s;
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.origin = Origin::StreetOrphan;
  d.worldSeed = seed;
  StatsSample at;
  createCharacter(s, d, at);
  s.progress.realm = realm;
  s.progress.hpMax = 200;
  s.progress.hp = 200;
  rivals::update(s, at);  // creates the rival
  rivals::setTalent(s, 100);
  setRivalMinutes(s, rivals::stageStart(3, 0, 100) + 10);
  sects::advance(s);  // founds the sects
  return s;
}

agenda::Steps read(GameState& s, const uint32_t hours) {
  s.progress.gameReadingSeconds += hours * 3600u;
  agenda::Steps out;
  cast::Steps people;
  agenda::advanceWorld(s, out, people);
  return out;
}

uint16_t year(const GameState& s) { return static_cast<uint16_t>(s.progress.gameReadingSeconds / 3600u); }

void condition(GameState& s, const sects::Condition c, const uint8_t region, const uint8_t owner,
               const int8_t other = -1, const uint16_t years = 4) {
  const uint32_t end = year(s) + years;
  s.living[3] = static_cast<uint8_t>(c);
  s.living[4] = region;
  s.living[5] = static_cast<uint8_t>(end);
  s.living[6] = static_cast<uint8_t>(end >> 8);
  s.living[7] = 0;
  s.living[8] = owner;
  s.living[9] = static_cast<uint8_t>(other + 1);
}

void story(GameState& s, const uint8_t i, const sects::Story st, const uint8_t region, const int8_t other = -1) {
  uint8_t* b = s.living.data() + 10 + 10 * i;
  b[3] = static_cast<uint8_t>(st);
  b[4] = 1;
  b[5] = 4;
  b[6] = static_cast<uint8_t>(other + 1);
  b[7] = region;
}

// Sets the goal directly (bytes as Agenda.h lays them out), ending `hours` from now.
void goal(GameState& s, const Goal g, const uint8_t region, const uint16_t hours, const uint8_t extra = 0) {
  s.rival[0] = static_cast<uint8_t>(s.rival[0] | 0x01);
  if (s.rival[5] == 0) s.rival[5] = 5;
  s.rival[1] = static_cast<uint8_t>(g);
  s.rival[2] = region;
  const uint32_t end = year(s) + hours;
  s.rival[3] = static_cast<uint8_t>(end);
  s.rival[4] = static_cast<uint8_t>(end >> 8);
  s.rival[8] = extra;
}

void setOpinion(GameState& s, const int v) {
  s.rival[0] = static_cast<uint8_t>(s.rival[0] | 0x01);
  s.rival[5] = static_cast<uint8_t>(v + 5);
}

bool has(const agenda::Steps& st, const agenda::EventKind k, const Goal g) {
  for (uint8_t i = 0; i < st.count; ++i) {
    if (st.events[i].kind == k && st.events[i].goal == g) return true;
  }
  return false;
}

int8_t rivalSect(const GameState& s) { return sects::personSect(s, sects::kNoRegion, rivals::rivalSeed(s)); }

struct Pack {
  std::string text;
  std::vector<PackSection> storage;
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;
  Pack() : text(builtinPackText(), builtinPackSize()), storage(PackStore::kMaxSections) {
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

// --- The clock --------------------------------------------------------------------------------

TEST(V034Agenda, AnEmptyBlockIsNotStartedAndNeutral) {
  GameState s = world(1);
  EXPECT_FALSE(agenda::state(s).started);
  EXPECT_EQ(agenda::opinion(s), 0);
  EXPECT_FALSE(agenda::bitter(s));
  EXPECT_FALSE(agenda::friendly(s));
  EXPECT_EQ(agenda::hoursLeft(s), 0);
}

TEST(V034Agenda, GoalsComeAfterAGapOfFourToEightHoursAndLastAsAgreed) {
  // Many worlds, 400 reading hours each, read an hour at a time: every goal's length and every gap.
  uint32_t goals = 0;
  uint32_t kinds[agenda::index(Goal::Count)] = {};
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    GameState s = world(seed * 977u);
    rivals::bear(s, explore::Enemy::Beast, 0, 25, seed, seed);  // a nemesis to hunt
    uint16_t startedAt = 0;
    uint16_t endedAt = 0;
    Goal running = Goal::None;
    for (uint32_t h = 0; h < 400; ++h) {
      const agenda::Steps st = read(s, 1);
      for (uint8_t i = 0; i < st.count; ++i) {
        const agenda::Event& e = st.events[i];
        if (e.kind == agenda::EventKind::Started) {
          ASSERT_EQ(running, Goal::None);
          running = e.goal;
          startedAt = e.year;
          if (endedAt != 0 && e.goal != Goal::Tournament && e.goal != Goal::Inheritance && e.goal != Goal::War) {
            EXPECT_GE(e.year - endedAt, agenda::kGapMin);
            EXPECT_LE(e.year - endedAt, agenda::kGapMax);
          }
          ++goals;
          ++kinds[agenda::index(e.goal)];
        } else {
          ASSERT_EQ(running, e.goal);
          const int len = e.year - startedAt;
          if (e.goal == Goal::ClosedDoor) {
            EXPECT_GE(len, agenda::kClosedMin);
            EXPECT_LE(len, agenda::kClosedMax);
          } else if (e.goal == Goal::Duel) {
            EXPECT_EQ(len, agenda::kDuelHours);
          } else if (e.goal == Goal::Rise || e.goal == Goal::Hunt) {
            EXPECT_GE(len, agenda::kGoalMin);
            EXPECT_LE(len, agenda::kGoalMax);
          } else {
            EXPECT_LE(len, sects::kConditionYears);  // ends with its condition
          }
          running = Goal::None;
          endedAt = e.year;
        }
      }
    }
  }
  // About one goal per 10 reading hours (4-8 between, 3-6 each).
  EXPECT_GT(goals, 40u * 400u / 14u);
  EXPECT_LT(goals, 40u * 400u / 7u);
  // (Closed doors need the rival at the last stage of a realm; without a date they never move: V034ClosedDoor.)
  for (const Goal g : {Goal::Tournament, Goal::Inheritance, Goal::War, Goal::Rise, Goal::Hunt}) {
    EXPECT_GT(kinds[agenda::index(g)], 0u) << static_cast<int>(g);
  }
}

TEST(V034Agenda, TheSameHistoryForEveryHabit) {
  // Ten hours in one visit, or ten visits of an hour: the same agenda (no date: the rival stays put).
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    GameState a = world(seed * 31u);
    GameState b = a;
    read(a, 30);
    for (int i = 0; i < 30; ++i) read(b, 1);
    EXPECT_EQ(a.rival, b.rival) << seed;
    EXPECT_EQ(a.living, b.living) << seed;
    EXPECT_EQ(a.rivals, b.rivals) << seed;
  }
}

// --- Goals tied to the world ------------------------------------------------------------------------

TEST(V034Tournament, TheyJoinAtOnceAndAreMetMoreOftenThere) {
  GameState s = world(5);
  read(s, 1);  // the agenda starts (a gap)
  ASSERT_EQ(agenda::state(s).goal, Goal::None);
  condition(s, sects::Condition::Tournament, 1, 2);
  const agenda::Steps st = read(s, 1);
  EXPECT_TRUE(has(st, agenda::EventKind::Started, Goal::Tournament));  // a world event cuts the gap short
  EXPECT_EQ(agenda::state(s).region, 1);
  EXPECT_EQ(agenda::meetPercent(s, 1), agenda::kTournamentMeetPercent);
  EXPECT_EQ(agenda::meetPercent(s, 0), rivals::kRivalMeetPercent);
  // It ends with the tournament, won or lost.
  int ended = 0;
  for (int h = 0; h < 6; ++h) {
    const agenda::Steps e = read(s, 1);
    ended += has(e, agenda::EventKind::Won, Goal::Tournament) + has(e, agenda::EventKind::Lost, Goal::Tournament);
  }
  EXPECT_EQ(ended, 1);
  EXPECT_EQ(agenda::meetPercent(s, 1), rivals::kRivalMeetPercent);
}

TEST(V034Tournament, NotInARegionTheirRealmHasNotOpened) {
  GameState s = world(5);
  read(s, 1);
  setRivalMinutes(s, rivals::stageStart(1, 0, 100) + 10);  // Qi Condensation: only region 0 is theirs
  condition(s, sects::Condition::Tournament, 2, 2);
  const agenda::Steps st = read(s, 1);
  EXPECT_FALSE(has(st, agenda::EventKind::Started, Goal::Tournament));
}

TEST(V034Inheritance, SometimesTheRivalTakesItAndGetsFasterOnceALife) {
  int taken = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    GameState s = world(seed * 53u);
    read(s, 1);
    story(s, 3, sects::Story::Inheritance, 1);
    condition(s, sects::Condition::Inheritance, 1, 3);
    const uint8_t talentBefore = rivals::rival(s).talentPercent;
    int8_t whose = -1;
    for (int h = 0; h < 6 && whose < 0; ++h) {
      const agenda::Steps st = read(s, 1);
      for (uint8_t i = 0; i < st.count; ++i) {
        if (st.events[i].kind == agenda::EventKind::Won && st.events[i].goal == Goal::Inheritance) {
          whose = st.events[i].sect;  // this one, or a later inheritance of the same reading
        }
      }
    }
    if (whose < 0) continue;
    ++taken;
    EXPECT_EQ(sects::condition(s), sects::Condition::None);                            // the race is over
    EXPECT_EQ(sects::sect(s, static_cast<uint8_t>(whose)).story, sects::Story::None);  // the sect lost it
    EXPECT_EQ(rivals::rival(s).talentPercent, talentBefore - 5);                       // 5% faster
    EXPECT_TRUE(agenda::state(s).inheritedTalent);
    // A second inheritance in the same life: no more talent.
    condition(s, sects::Condition::Inheritance, 1, 3);
    goal(s, Goal::Inheritance, 1, 4, 3);
    for (int h = 0; h < 6; ++h) read(s, 1);
    EXPECT_EQ(rivals::rival(s).talentPercent, talentBefore - 5);
  }
  EXPECT_GT(taken, 10);  // about half, against the sects' own race
  EXPECT_LT(taken, 50);
}

TEST(V034Inheritance, TalentNeverBetterThanEighty) {
  GameState s = world(9);
  rivals::setTalent(s, 82);
  read(s, 1);
  for (uint32_t k = 0; k < 40; ++k) {
    condition(s, sects::Condition::Inheritance, 1, 3);
    goal(s, Goal::Inheritance, 1, 4, 3);
    if (has(read(s, 1), agenda::EventKind::Won, Goal::Inheritance)) break;
  }
  EXPECT_EQ(rivals::rival(s).talentPercent, agenda::kTalentFloor);
  rivals::setTalent(s, 60);
  EXPECT_EQ(rivals::rival(s).talentPercent, 80);
}

TEST(V034Inheritance, YouTakingItFirstMakesThemBitter) {
  GameState s = world(11);
  read(s, 1);
  story(s, 3, sects::Story::Inheritance, 1);
  condition(s, sects::Condition::Inheritance, 1, 3);
  goal(s, Goal::Inheritance, 1, 4, 3);
  const sects::Influence w = sects::searchWon(s, 1);
  agenda::sawSearch(s, w);
  EXPECT_EQ(agenda::opinion(s), -2);
  EXPECT_TRUE(agenda::bitter(s));
  const agenda::Steps st = read(s, 1);
  ASSERT_TRUE(has(st, agenda::EventKind::Lost, Goal::Inheritance));
  for (uint8_t i = 0; i < st.count; ++i) {
    if (st.events[i].kind == agenda::EventKind::Lost) {
      EXPECT_TRUE(st.events[i].byYou);
    }
  }
}

TEST(V034War, TheirSideAgainstYouOrWithYou) {
  GameState s = world(13);
  read(s, 1);
  const int8_t own = rivalSect(s);
  ASSERT_GE(own, 0);
  const uint8_t enemy = static_cast<uint8_t>((own + 1) % sects::kSects);
  story(s, static_cast<uint8_t>(own), sects::Story::Feud, 2, static_cast<int8_t>(enemy));
  condition(s, sects::Condition::SectWar, 2, static_cast<uint8_t>(own), static_cast<int8_t>(enemy));
  ASSERT_TRUE(has(read(s, 1), agenda::EventKind::Started, Goal::War));
  EXPECT_EQ(agenda::state(s).nemesis, enemy);  // byte 8: the enemy side
  // One of theirs: -1; one of the enemy: +1.
  agenda::sawWin(s, 2, own, sects::cultivatorBeaten(s, 2, own));
  EXPECT_EQ(agenda::opinion(s), -1);
  agenda::sawWin(s, 2, static_cast<int8_t>(enemy), sects::cultivatorBeaten(s, 2, static_cast<int8_t>(enemy)));
  agenda::sawWin(s, 2, static_cast<int8_t>(enemy), sects::cultivatorBeaten(s, 2, static_cast<int8_t>(enemy)));
  EXPECT_EQ(agenda::opinion(s), 1);
  // The third of the enemy decides the war for their side: their goal is won.
  agenda::sawWin(s, 2, static_cast<int8_t>(enemy), sects::cultivatorBeaten(s, 2, static_cast<int8_t>(enemy)));
  EXPECT_EQ(sects::condition(s), sects::Condition::None);
  const uint8_t wonBefore = agenda::state(s).won;
  EXPECT_TRUE(has(read(s, 1), agenda::EventKind::Won, Goal::War));
  EXPECT_EQ(agenda::state(s).won, wonBefore + 1);
}

TEST(V034War, NotWhenTheirSectIsNotASide) {
  GameState s = world(13);
  read(s, 1);
  const int8_t own = rivalSect(s);
  const auto a = static_cast<uint8_t>((own + 1) % sects::kSects);
  const auto b = static_cast<uint8_t>((own + 2) % sects::kSects);
  condition(s, sects::Condition::SectWar, 2, a, static_cast<int8_t>(b));
  EXPECT_FALSE(has(read(s, 1), agenda::EventKind::Started, Goal::War));
}

TEST(V034World, HelpingTheirSectRaisesTheirOpinion) {
  GameState s = world(17);
  read(s, 1);
  const auto own = static_cast<uint8_t>(rivalSect(s));
  story(s, own, sects::Story::Bandits, 0);
  condition(s, sects::Condition::BanditCamp, 0, own);
  for (int i = 0; i < 3; ++i) agenda::sawWin(s, 0, -1, sects::banditBeaten(s, 0));
  EXPECT_EQ(agenda::opinion(s), 1);
}

// --- Their own goals -------------------------------------------------------------------------------

TEST(V034Rise, PromotionsByRealmAndGoalsWon) {
  GameState s = world(19);
  read(s, 1);
  goal(s, Goal::Rise, 0, 3);
  EXPECT_TRUE(has(read(s, 3), agenda::EventKind::Promoted, Goal::Rise));  // realm 3: inner disciple
  EXPECT_EQ(agenda::state(s).rank, agenda::Rank::Inner);
  goal(s, Goal::Rise, 0, 3);
  EXPECT_TRUE(has(read(s, 3), agenda::EventKind::Served, Goal::Rise));  // core needs 2 goals won
  EXPECT_EQ(agenda::state(s).rank, agenda::Rank::Inner);
  s.rival[7] = 2;
  goal(s, Goal::Rise, 0, 3);
  EXPECT_TRUE(has(read(s, 3), agenda::EventKind::Promoted, Goal::Rise));
  EXPECT_EQ(agenda::state(s).rank, agenda::Rank::Core);
  s.rival[7] = 9;
  goal(s, Goal::Rise, 0, 3);
  EXPECT_TRUE(has(read(s, 3), agenda::EventKind::Served, Goal::Rise));  // elder needs realm 5
  setRivalMinutes(s, rivals::stageStart(5, 0, 100) + 10);
  goal(s, Goal::Rise, 0, 3);
  read(s, 3);
  EXPECT_EQ(agenda::state(s).rank, agenda::Rank::Elder);
}

TEST(V034ClosedDoor, UnseenThenAStageAhead) {
  GameState s = world(23);
  read(s, 1);
  const uint8_t last = static_cast<uint8_t>(realmInfo(3).stages - 1);
  setRivalMinutes(s, rivals::stageStart(3, last, 100) + 10);
  goal(s, Goal::ClosedDoor, 0, 2);
  EXPECT_TRUE(agenda::unseen(s));
  // Unseen: no meeting on the road is the rival.
  const explore::Layout l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  int rivalMet = 0;
  for (int k = 0; k < 4000; ++k) {
    GameState t = s;
    t.progress.stamina = 20;
    setCounter(t, static_cast<uint32_t>(k));
    const uint8_t from = explore::position(t, l, 0);
    uint8_t nb[4];
    const uint8_t n = explore::neighbours(from, nb);
    for (uint8_t i = 0; i < n; ++i) {
      if (explore::canStep(t, l, 0, nb[i]) != explore::StepError::None) continue;
      const explore::StepResult r = explore::step(t, l, 0, nb[i]);
      if (r.meeting && explore::pendingMeeting(t, l).rival) ++rivalMet;
      break;
    }
  }
  EXPECT_EQ(rivalMet, 0);
  EXPECT_TRUE(has(read(s, 2), agenda::EventKind::CameOut, Goal::ClosedDoor));
  const rivals::Rival r = rivals::rival(s);
  EXPECT_EQ(r.realm, 4);
  EXPECT_EQ(r.stage, 0);
  EXPECT_FALSE(agenda::unseen(s));
}

TEST(V034ClosedDoor, StageStartMatchesPositionFor) {
  for (const uint8_t talent : {80, 100, 117, 125}) {
    for (uint8_t realm = 0; realm < kMortalRealmCount; ++realm) {
      for (uint8_t stage = 0; stage < realmInfo(realm).stages; ++stage) {
        uint8_t r = 0;
        uint8_t st = 0;
        rivals::positionFor(rivals::stageStart(realm, stage, talent), talent, r, st);
        EXPECT_EQ(r, realm);
        EXPECT_EQ(st, stage) << int(realm) << "/" << int(stage) << " talent " << int(talent);
      }
    }
  }
}

TEST(V034Hunt, TheyBeatYourNemesisOrLose) {
  int won = 0;
  int lost = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    GameState s = world(seed * 7u);
    read(s, 1);
    const int i = rivals::bear(s, explore::Enemy::Beast, 1, static_cast<int>(25 + seed % 20), seed, seed);
    ASSERT_GE(i, 0);
    goal(s, Goal::Hunt, 1, 3, static_cast<uint8_t>(i));
    const agenda::Steps st = read(s, 3);
    if (has(st, agenda::EventKind::Won, Goal::Hunt)) {
      ++won;
      rivals::Nemesis n;
      ASSERT_TRUE(rivals::nemesis(s, static_cast<uint8_t>(i), n));
      EXPECT_EQ(n.epithet, rivals::EpithetKind::Scar);
      EXPECT_TRUE(rivals::resting(s, n));
      EXPECT_EQ(n.wins, 0);  // your record is yours
    } else if (has(st, agenda::EventKind::Lost, Goal::Hunt)) {
      ++lost;
    }
  }
  EXPECT_GT(won, 0);
  EXPECT_GT(lost, 0);
}

// --- Opinion and the duel ---------------------------------------------------------------------------

TEST(V034Opinion, BeatingThemLowersItAComebackRaisesIt) {
  GameState s = world(29);
  agenda::boutWithRival(s, true);
  EXPECT_EQ(agenda::opinion(s), -1);
  agenda::boutWithRival(s, false);  // they beat you
  EXPECT_EQ(agenda::opinion(s), -1);
  agenda::boutWithRival(s, false);  // you came back: +1 (and lost again)
  EXPECT_EQ(agenda::opinion(s), 0);
  for (int i = 0; i < 20; ++i) agenda::boutWithRival(s, true);
  EXPECT_EQ(agenda::opinion(s), agenda::kOpinionMin);
  setOpinion(s, 9);
  EXPECT_EQ(agenda::opinion(s), agenda::kOpinionMax);
}

TEST(V034Opinion, FriendlyTradesBitterNever) {
  GameState s = world(31);
  s.rivals[1] = static_cast<uint8_t>(rivals::Temper::Driven);  // never trades by temper
  int trades = 0;
  for (uint8_t m = 0; m < 100; ++m) trades += rivals::offersTrade(s, m);
  EXPECT_EQ(trades, 0);
  setOpinion(s, 3);
  trades = 0;
  for (uint8_t m = 0; m < 100; ++m) trades += rivals::offersTrade(s, m);
  EXPECT_GT(trades, 25);
  EXPECT_LT(trades, 75);
  s.rivals[1] = static_cast<uint8_t>(rivals::Temper::Idle);  // always trades by temper
  setOpinion(s, -3);
  trades = 0;
  for (uint8_t m = 0; m < 100; ++m) trades += rivals::offersTrade(s, m);
  EXPECT_EQ(trades, 0);
}

namespace {
// The bitter, known rival's duel waiting in region 0, and the player on its map.
GameState duelWorld(explore::Layout& l) {
  GameState s = world(37);
  read(s, 1);
  rivals::markMet(s);
  setOpinion(s, -3);
  goal(s, Goal::Duel, 0, agenda::kDuelHours);
  l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  s.progress.stamina = 20;
  s.progress.stones = 1000;
  return s;
}
// A step in region 0 onto a tile that is not a lair or the guardian (those fight first), from
// wherever such a step can be taken (region bytes 8 + 12: the player's tile + 1).
bool stepAnywhere(GameState& s, const explore::Layout& l) {
  for (uint8_t from = 0; from < explore::kTiles; ++from) {
    if (!explore::walkable(l, from)) continue;
    uint8_t nb[4];
    const uint8_t n = explore::neighbours(from, nb);
    for (uint8_t i = 0; i < n; ++i) {
      const explore::Tile t = l.tiles[nb[i]];
      if (!explore::walkable(l, nb[i]) || t == explore::Tile::Lair || t == explore::Tile::Guardian) continue;
      s.exploration[8 + 12] = static_cast<uint8_t>(from + 1);
      if (explore::canStep(s, l, 0, nb[i]) != explore::StepError::None) continue;
      return explore::step(s, l, 0, nb[i]).meeting;
    }
  }
  return false;
}
}  // namespace

TEST(V034Duel, ABitterRivalWhoKnowsYouSendsALetter) {
  int duels = 0;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    GameState s = world(seed * 41u);
    read(s, 1);
    setOpinion(s, -3);
    for (int h = 0; h < 30; ++h) duels += has(read(s, 1), agenda::EventKind::Started, Goal::Duel);
  }
  EXPECT_EQ(duels, 0);  // unmet: no letter
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    GameState s = world(seed * 41u);
    rivals::markMet(s);
    read(s, 1);
    setOpinion(s, -3);
    for (int h = 0; h < 30; ++h) {
      const agenda::Steps st = read(s, 1);
      if (has(st, agenda::EventKind::Started, Goal::Duel)) {
        ++duels;
        EXPECT_TRUE(explore::unlocked(s, agenda::state(s).region));
        EXPECT_TRUE(agenda::duelWaitingIn(s, agenda::state(s).region));
      }
    }
  }
  EXPECT_GT(duels, 10);
}

TEST(V034Duel, AStepOnThatMapMeetsThem) {
  explore::Layout l;
  GameState s = duelWorld(l);
  ASSERT_TRUE(stepAnywhere(s, l));
  const explore::Meeting m = explore::pendingMeeting(s, l);
  ASSERT_TRUE(m.active);
  EXPECT_TRUE(m.rival);
  EXPECT_TRUE(m.duel);
  EXPECT_EQ(m.offer, explore::Offer::Duel);
  EXPECT_GT(m.damage, 0);
  EXPECT_EQ(m.stakes, 30u * (s.progress.realm + 1u));
}

TEST(V034Duel, TheWinnerTakesTheStakes) {
  int wins = 0;
  int losses = 0;
  for (uint32_t k = 0; k < 40; ++k) {
    explore::Layout l;
    GameState s = duelWorld(l);
    setCounter(s, k * 13u);
    ASSERT_TRUE(stepAnywhere(s, l));
    const uint32_t stones = s.progress.stones;
    const uint16_t hp = s.progress.hp;
    const explore::MeetResult r = explore::meet(s, l, explore::MeetChoice::Offer);
    ASSERT_TRUE(r.fought);
    ASSERT_TRUE(r.duel);
    const uint32_t stakes = 30u * (s.progress.realm + 1u);
    if (r.won) {  // the step's tile is still taken after the duel (a herb, a ruin)
      ++wins;
      EXPECT_EQ(r.reward.stones, stakes);
      EXPECT_EQ(s.progress.stones, stones + stakes + r.tile.reward.stones + r.tile.clearReward.stones);
      EXPECT_EQ(s.progress.hp, hp - r.tile.hpLost + r.tile.hpHealed);
    } else {
      ++losses;
      EXPECT_EQ(s.progress.stones, stones - stakes);
      EXPECT_EQ(r.duelPaid, stakes);
      EXPECT_LT(s.progress.hp, hp);
    }
    EXPECT_EQ(agenda::state(s).goal, Goal::None);
    EXPECT_FALSE(agenda::duelWaitingIn(s, 0));
    EXPECT_EQ(s.exploration[5], 0);  // the map is free
  }
  EXPECT_GT(wins, 0);
  EXPECT_GT(losses, 0);
}

TEST(V034Duel, ALostDuelTakesOnlyWhatYouHave) {
  for (uint32_t k = 0; k < 40; ++k) {
    explore::Layout l;
    GameState s = duelWorld(l);
    s.progress.stones = 7;
    setCounter(s, k * 13u);
    ASSERT_TRUE(stepAnywhere(s, l));
    const explore::MeetResult r = explore::meet(s, l, explore::MeetChoice::Offer);
    if (r.won) continue;
    EXPECT_EQ(r.duelPaid, 7u);
    EXPECT_EQ(s.progress.stones, 0u);
    return;
  }
  FAIL() << "no lost duel in 40 tries";
}

TEST(V034Duel, RefusedOrIgnoredLowersTheirOpinion) {
  explore::Layout l;
  GameState s = duelWorld(l);
  ASSERT_TRUE(stepAnywhere(s, l));
  const explore::MeetResult r = explore::meet(s, l, explore::MeetChoice::WalkOn);
  EXPECT_TRUE(r.duelRefused);
  EXPECT_EQ(agenda::opinion(s), -4);
  EXPECT_EQ(agenda::state(s).goal, Goal::None);
  EXPECT_EQ(agenda::state(s).won, 0);  // not a goal won

  GameState t = duelWorld(l);
  const agenda::Steps st = read(t, agenda::kDuelHours);
  EXPECT_TRUE(has(st, agenda::EventKind::DuelIgnored, Goal::Duel));
  EXPECT_EQ(agenda::opinion(t), -4);
}

// --- Save and text ----------------------------------------------------------------------------------

TEST(V034Save, TheOldRivalBlockRoundTripsAndANewLifeClearsIt) {
  GameState s = world(43);
  read(s, 9);
  setOpinion(s, 2);
  ASSERT_TRUE(agenda::state(s).started);
  const std::array<uint8_t, 11> block = s.rival;
  Storage.reset();
  ASSERT_TRUE(save::store(s));
  GameState back;
  ASSERT_EQ(save::load(back), save::LoadResult::Loaded);
  EXPECT_EQ(back.rival, block);
  EXPECT_EQ(agenda::opinion(back), 2);
  GameState fresh;
  fresh.reset();
  EXPECT_FALSE(agenda::state(fresh).started);
}

TEST(V034Text, EveryListExpandsForAMetAndAnUnmetRival) {
  Pack pack;
  GameState s = world(47);
  const char* lines[] = {"rival.goal.tournament.start",  "rival.goal.tournament.won",  "rival.goal.tournament.lost",
                         "rival.goal.inheritance.start", "rival.goal.inheritance.won", "rival.goal.inheritance.lost",
                         "rival.goal.inheritance.yours", "rival.goal.war.start",       "rival.goal.war.won",
                         "rival.goal.war.lost",          "rival.goal.rise.start",      "rival.goal.rise.promoted",
                         "rival.goal.rise.served",       "rival.goal.duel.start",      "rival.goal.duel.ignored",
                         "rival.goal.closed.start",      "rival.goal.closed.end",      "rival.goal.hunt.start",
                         "rival.goal.hunt.won",          "rival.goal.hunt.lost",       "rival.line.duel.won",
                         "rival.line.duel.lost",         "rival.line.duel.refused"};
  for (const char* name : {"Qiu Mingyuan", "a young disciple of the Azure Cloud Sect"}) {
    for (const char* key : lines) {
      for (uint32_t seed = 0; seed < 12; ++seed) {
        lifelog::Line line;
        ASSERT_TRUE(exploretext::personLine(line, s, key, seed, "the Reedwater Marshes", name, "the Jade Lotus Sect",
                                            *pack.grammar))
            << key;
        EXPECT_TRUE(clean(line.text)) << line.text;
        EXPECT_LT(strlen(line.text), lifelog::kLineSize - 1) << key << ": " << line.text;
        EXPECT_EQ(std::string(line.text).find("Spirit Stone"), std::string::npos) << line.text;
        // Before you meet them they are "a young disciple of ...": the goal lines never say "your rival".
        if (std::string(key).rfind("rival.goal.", 0) == 0) {
          EXPECT_EQ(std::string(line.text).find("your rival"), std::string::npos) << line.text;
        }
      }
    }
  }
  const char* screen[] = {"rival.rumor.tournament", "rival.rumor.inheritance", "rival.rumor.war",  "rival.rumor.rise",
                          "rival.rumor.duel",       "rival.rumor.closed",      "rival.rumor.hunt", "rival.meet.duel",
                          "rival.meet.bitter",      "rival.meet.friendly",     "rival.duelwon",    "rival.duellost"};
  for (const char* key : screen) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      char out[300];
      ASSERT_TRUE(exploretext::personFlavor(s, key, seed, "the Reedwater Marshes",
                                            "a young disciple of the Azure Cloud Sect", "the Jade Lotus Sect",
                                            *pack.grammar, out, sizeof(out)))
          << key;
      EXPECT_TRUE(clean(out)) << out;
      EXPECT_LT(strlen(out), 199u) << key << ": " << out;  // the rumor of the day holds 200 bytes with its "."
    }
  }
}

TEST(V034Pack, FitsTheRaisedIndex) {
  Pack pack;
  EXPECT_EQ(PackStore::kMaxSections, 512u);  // user, v0.3.4 (448 before)
  EXPECT_LT(pack.index.count, 480u);         // at least 32 left for SD packs
}

namespace {
struct GuideString {
  const char* id;
  const char* text;
};
#define CULT_STR(id, text) {#id, text},
const GuideString kGameStrings[] = {
#include <GameStrings.inc>
};
#undef CULT_STR
}  // namespace

TEST(V034Guide, EveryParagraphFitsThePageBuffer) {
  // renderGuidePage() wraps each paragraph through a 400-byte buffer (found in the simulator in v0.3.4).
  int pages = 0;
  for (const GuideString& g : kGameStrings) {
    const std::string id = g.id;
    if (id.rfind("CULT_GUIDE_T", 0) != 0 || id.find("_P") == std::string::npos) continue;
    ++pages;
    std::string text = g.text;
    size_t start = 0;
    while (start <= text.size()) {
      const size_t end = text.find('\n', start);
      const size_t len = (end == std::string::npos ? text.size() : end) - start;
      EXPECT_LT(len, 399u) << id;
      if (end == std::string::npos) break;
      start = end + 1;
    }
  }
  EXPECT_GE(pages, 20);
}
