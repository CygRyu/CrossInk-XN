#include <Agenda.h>
#include <BuiltinPack.h>
#include <Cast.h>
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

// v0.3.5 "The cast" (designed and agreed with the user in session 16, HANDOFF 14o):
// twelve people with lives on the world's clock, who remember you.

using namespace cultivation;

namespace {

GameState world(const uint32_t seed, const uint8_t realm = 5) {
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
  s.progress.hpMax = 300;
  s.progress.hp = 300;
  rivals::update(s, at);
  sects::advance(s);
  return s;
}

cast::Steps read(GameState& s, const uint32_t hours) {
  s.progress.gameReadingSeconds += hours * 3600u;
  agenda::Steps a;
  cast::Steps c;
  agenda::advanceWorld(s, a, c);
  return c;
}

uint16_t year(const GameState& s) { return static_cast<uint16_t>(s.progress.gameReadingSeconds / 3600u); }
uint8_t* pb(GameState& s, const uint8_t i) { return s.cast.data() + cast::kHeader + cast::kPersonSize * i; }

// Person i: realm, ceiling, region, age now; alive, not secluded, opinion 0.
void setPerson(GameState& s, const uint8_t i, const uint8_t realm, const uint8_t ceiling, const uint8_t region,
               const uint16_t age) {
  uint8_t* b = pb(s, i);
  b[0] = 0x01 | (b[0] & 0x02);
  b[1] = static_cast<uint8_t>(realm | (ceiling << 4));
  b[2] = b[3] = 0;
  const uint32_t birth = year(s) + 1000u - age;
  b[4] = static_cast<uint8_t>(birth);
  b[5] = static_cast<uint8_t>(birth >> 8);
  b[10] = region;
}
void setSect(GameState& s, const uint8_t i, const int8_t sect) {
  uint8_t* b = pb(s, i);
  b[6] = static_cast<uint8_t>((b[6] & 0xF8) | ((sect + 1) & 0x07));
}
void setTemper(GameState& s, const uint8_t i, const explore::Temperament t) {
  uint8_t* b = pb(s, i);
  b[7] = static_cast<uint8_t>((b[7] & 0xFC) | static_cast<uint8_t>(t));
}
// Everyone else away in region 8 and secluded: only person i walks region `region`.
void alone(GameState& s, const uint8_t i, const uint8_t region) {
  for (uint8_t k = 0; k < cast::kPeople; ++k) {
    if (k == i) continue;
    pb(s, k)[10] = 8;
  }
  pb(s, i)[10] = region;
  pb(s, i)[0] = static_cast<uint8_t>(pb(s, i)[0] & ~0x04);
}

bool has(const cast::Steps& st, const cast::EventKind k, const uint8_t who) {
  for (uint8_t i = 0; i < st.count; ++i) {
    if (st.events[i].kind == k && st.events[i].who == who) return true;
  }
  return false;
}

void setCounter(GameState& s, const uint32_t c) { memcpy(s.exploration.data(), &c, 4); }

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

// --- Who they are --------------------------------------------------------------------------------

TEST(V035Cast, FoundedOnTheFirstWorldStepAsAgreed) {
  int inSect = 0;
  int total = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    GameState s = world(seed * 131u);
    EXPECT_FALSE(cast::founded(s));
    read(s, 1);
    ASSERT_TRUE(cast::founded(s));
    for (uint8_t i = 0; i < cast::kPeople; ++i) {
      cast::Person p;
      ASSERT_TRUE(cast::person(s, i, p));
      EXPECT_TRUE(p.alive);
      EXPECT_FALSE(p.met);
      EXPECT_EQ(p.opinion, 0);
      EXPECT_GE(p.pace, cast::kPaceMin);
      EXPECT_LE(p.pace, cast::kPaceMax);
      EXPECT_LE(p.realm, p.ceiling);
      if (i == 0) {
        EXPECT_GE(p.realm, 3);  // the Core Formation elder
        EXPECT_GE(p.age, 120);
        EXPECT_GE(p.sect, 0);
      } else {
        EXPECT_GE(p.age, 16);
        EXPECT_LE(p.age, 71);
        EXPECT_LE(p.realm, 3);
      }
      inSect += p.sect >= 0;
      ++total;
    }
    cast::Person a;
    cast::person(s, 1, a);
    EXPECT_EQ(a.link, cast::Link::Master);  // the elder's disciple
    EXPECT_EQ(a.linkTo, 0);
    cast::person(s, 2, a);
    EXPECT_EQ(a.link, cast::Link::Sibling);
  }
  EXPECT_GT(inSect * 100 / total, 30);  // about half
  EXPECT_LT(inSect * 100 / total, 70);
}

TEST(V035Cast, CeilingsAsAgreed) {
  int low = 0;
  int mid = 0;
  int high = 0;
  int top = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    GameState s = world(seed * 17u);
    read(s, 1);
    for (uint8_t i = 1; i < cast::kPeople; ++i) {
      cast::Person p;
      cast::person(s, i, p);
      (p.ceiling <= 2 ? low : p.ceiling <= 4 ? mid : p.ceiling <= 6 ? high : top) += 1;
    }
  }
  const int n = low + mid + high + top;
  EXPECT_NEAR(low * 100.0 / n, 40, 4);
  EXPECT_NEAR(mid * 100.0 / n, 40, 4);
  EXPECT_NEAR(high * 100.0 / n, 15, 3);
  EXPECT_NEAR(top * 100.0 / n, 5, 2);
}

TEST(V035Cast, LifespansAreYours) {
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) EXPECT_EQ(cast::lifespan(r), realmInfo(r).lifespanYears);
}

TEST(V035Cast, TheSameHistoryForEveryHabit) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    GameState a = world(seed * 71u);
    GameState b = a;
    read(a, 60);
    for (int i = 0; i < 60; ++i) read(b, 1);
    EXPECT_EQ(a.cast, b.cast) << seed;
  }
}

// --- Their lives --------------------------------------------------------------------------------

TEST(V035Lives, OldAgeThenANewcomerInTheSlot) {
  GameState s = world(3);
  read(s, 1);
  setPerson(s, 5, 1, 1, 0, cast::lifespan(1) - 1);  // Qi Condensation, a year to live, at their ceiling
  pb(s, 6)[8] = static_cast<uint8_t>(static_cast<uint8_t>(cast::Link::Spouse) | (5 << 3));
  const uint8_t generation = pb(s, 5)[11];
  cast::Steps st = read(s, 1);
  ASSERT_TRUE(has(st, cast::EventKind::DiedOld, 5));
  cast::Person p;
  ASSERT_TRUE(cast::person(s, 5, p));
  EXPECT_FALSE(p.alive);
  EXPECT_EQ(p.cause, cast::Cause::OldAge);
  EXPECT_EQ(p.age, cast::lifespan(1));
  cast::Person widow;
  cast::person(s, 6, widow);
  EXPECT_EQ(widow.link, cast::Link::Spouse);  // still the late husband or wife
  int arrivedAfter = -1;
  for (int y = 1; y <= 12 && arrivedAfter < 0; ++y) {
    if (has(read(s, 1), cast::EventKind::Arrived, 5)) arrivedAfter = y;
  }
  ASSERT_GE(arrivedAfter, 3);
  EXPECT_LE(arrivedAfter, 10);
  cast::person(s, 5, p);
  EXPECT_TRUE(p.alive);
  EXPECT_EQ(p.generation, generation + 1);
  EXPECT_GE(p.age, 16);
  EXPECT_LE(p.age, 24 + 1);
  cast::person(s, 6, widow);
  if (widow.alive && widow.linkTo == 5) {
    EXPECT_NE(widow.link, cast::Link::Spouse);  // not married to the newcomer
  }
}

TEST(V035Lives, TheCeilingStopsThemAndBreakthroughsCanFail) {
  int broke = 0;
  int failed = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    GameState s = world(seed * 29u);
    read(s, 1);
    setPerson(s, 4, 2, 2, 0, 20);  // at their ceiling
    setPerson(s, 7, 2, 6, 0, 20);  // room to grow
    bool fourGone = false;         // a war can take them; a newcomer in the slot starts over
    bool sevenGone = false;
    for (int y = 0; y < 120; ++y) {
      const cast::Steps st = read(s, 1);
      if (!sevenGone) {
        broke += has(st, cast::EventKind::Breakthrough, 7);
        failed += has(st, cast::EventKind::Failed, 7);
      }
      if (!fourGone) {
        EXPECT_FALSE(has(st, cast::EventKind::Breakthrough, 4));
      }
      fourGone = fourGone || has(st, cast::EventKind::DiedWar, 4) || has(st, cast::EventKind::DiedOld, 4);
      sevenGone = sevenGone || has(st, cast::EventKind::DiedWar, 7) || has(st, cast::EventKind::DiedOld, 7);
    }
    cast::Person p;
    cast::person(s, 4, p);
    if (!fourGone) {
      EXPECT_EQ(p.realm, 2);
    }
  }
  EXPECT_GT(broke, 0);
  EXPECT_GT(failed, 0);
  // 70% at Foundation, 65% at Core Formation ...: more successes than failures.
  EXPECT_GT(broke, failed);
}

TEST(V035Lives, TheirSectsWarsKillSome) {
  int died = 0;
  int years = 0;
  for (uint32_t seed = 1; seed <= 80; ++seed) {
    GameState s = world(seed * 37u);
    read(s, 1);
    setPerson(s, 9, 4, 4, 2, 40);
    setSect(s, 9, 1);
    for (int y = 0; y < 4; ++y) {
      const uint32_t end = year(s) + 4;
      s.living[3] = static_cast<uint8_t>(sects::Condition::SectWar);
      s.living[4] = 2;
      s.living[5] = static_cast<uint8_t>(end);
      s.living[6] = static_cast<uint8_t>(end >> 8);
      s.living[8] = 1;
      s.living[9] = 3;  // sect 2 + 1
      const cast::Steps st = read(s, 1);
      ++years;
      if (has(st, cast::EventKind::DiedWar, 9)) {
        ++died;
        break;
      }
    }
  }
  EXPECT_NEAR(died * 100.0 / years, 100.0 / cast::kWarDeathOneIn, 3.5);
}

TEST(V035Lives, AboutOneEventEachEveryThirtyYears) {
  uint32_t events = 0;
  uint32_t personYears = 0;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    GameState s = world(seed * 43u);
    read(s, 1);
    for (int y = 0; y < 200; ++y) {
      const cast::Steps st = read(s, 1);
      for (uint8_t i = 0; i < st.count; ++i) {
        const cast::EventKind k = st.events[i].kind;
        if (k != cast::EventKind::Breakthrough && k != cast::EventKind::Failed && k != cast::EventKind::Arrived &&
            k != cast::EventKind::DiedOld && k != cast::EventKind::DiedWar) {
          ++events;
        }
      }
      personYears += cast::kPeople;
    }
  }
  const double perThirty = events * 30.0 / personYears;
  EXPECT_GT(perThirty, 0.5);  // some picks find no partner, some quarrels mend
  EXPECT_LT(perThirty, 1.2);
}

TEST(V035Lives, NewsOnlyAboutPeopleYouKnow) {
  GameState s = world(5);
  read(s, 1);
  cast::Event e;
  e.kind = cast::EventKind::Breakthrough;
  e.who = 7;
  pb(s, 7)[8] = static_cast<uint8_t>(cast::kLinkNone << 3);
  EXPECT_FALSE(cast::newsworthy(s, e));
  pb(s, 7)[0] |= 0x02;  // met
  EXPECT_TRUE(cast::newsworthy(s, e));
  pb(s, 7)[0] &= ~0x02;
  pb(s, 7)[8] = static_cast<uint8_t>(static_cast<uint8_t>(cast::Link::Enemy) | (cast::kLinkRival << 3));
  EXPECT_TRUE(cast::newsworthy(s, e));  // tied to your rival
  pb(s, 7)[8] = static_cast<uint8_t>(static_cast<uint8_t>(cast::Link::Sibling) | (8 << 3));
  EXPECT_FALSE(cast::newsworthy(s, e));
  pb(s, 8)[0] |= 0x02;
  EXPECT_TRUE(cast::newsworthy(s, e));  // tied to someone you met
}

// --- Meeting them ---------------------------------------------------------------------------------

TEST(V035Meet, OneMeetingInThreeWhereSomeoneLives) {
  GameState s = world(7);
  read(s, 1);
  alone(s, 4, 1);
  int hits = 0;
  for (uint32_t roll = 0; roll < 3000; ++roll) {
    const int who = cast::meetingIn(s, 1, roll);
    if (who >= 0) {
      EXPECT_EQ(who, 4);
      ++hits;
    }
    EXPECT_EQ(cast::meetingIn(s, 3, roll), -1);
  }
  EXPECT_EQ(hits, 1000);
  pb(s, 4)[0] |= 0x04;  // seclusion
  for (uint32_t roll = 0; roll < 30; ++roll) EXPECT_EQ(cast::meetingIn(s, 1, roll), -1);
}

namespace {
// A waiting meeting with person i in region 0, the player on its map.
GameState meetWorld(const uint8_t i, explore::Layout& l) {
  GameState s = world(11);
  read(s, 1);
  setPerson(s, i, 3, 5, 0, 60);
  alone(s, i, 0);
  l = explore::layoutOf(s, 0);
  explore::enter(s, l, 0);
  s.progress.stamina = 20;
  s.progress.stones = 1000;
  s.exploration[4] = 1;  // region 0 + 1
  s.exploration[5] = 4;  // a meeting waits
  s.exploration[6] = 0;
  s.exploration[7] = static_cast<uint8_t>(cast::kSlotBase + i);
  return s;
}
}  // namespace

TEST(V035Meet, AtTheirOwnLevelNotScaledToYou) {
  explore::Layout l;
  for (const uint8_t realm : {2, 5, 7}) {
    GameState s = meetWorld(4, l);
    s.progress.realm = realm;
    const explore::Meeting m = explore::pendingMeeting(s, l);
    ASSERT_TRUE(m.active);
    EXPECT_EQ(m.cast, 4);
    EXPECT_FALSE(m.rival);
    cast::Person p;
    cast::person(s, 4, p);
    EXPECT_EQ(m.level, cast::level(p));
  }
}

TEST(V035Meet, BitterOnesChallengeFriendsTakeTenPercentOff) {
  explore::Layout l;
  GameState s = meetWorld(4, l);
  cast::setOpinion(s, 4, -3);
  explore::Meeting m = explore::pendingMeeting(s, l);
  EXPECT_EQ(m.offer, explore::Offer::Challenge);
  EXPECT_TRUE(m.dares);

  GameState f = meetWorld(4, l);
  setTemper(f, 4, explore::Temperament::Greedy);
  const explore::Stock before = explore::stockFor(f, l, 0, explore::kTraderTile);
  cast::setOpinion(f, 4, 3);
  m = explore::pendingMeeting(f, l);
  EXPECT_TRUE(m.friendly);
  EXPECT_NE(m.offer, explore::Offer::Challenge);
  const explore::Stock after = explore::stockFor(f, l, 0, explore::kTraderTile);
  ASSERT_GT(before.count, 0);
  EXPECT_LT(after.lines[0].price, before.lines[0].price);
  EXPECT_NEAR(after.lines[0].price, before.lines[0].price * 0.9, 1.5);
}

TEST(V035Meet, TheyRememberHowYouParted) {
  GameState s = world(13);
  read(s, 1);
  cast::met(s, 3, explore::MeetOutcome::Traded, false);
  cast::Person p;
  cast::person(s, 3, p);
  EXPECT_TRUE(p.met);
  EXPECT_EQ(p.opinion, 1);
  EXPECT_EQ(p.parting, cast::Parting::Friendly);
  EXPECT_EQ(p.meets, 1);
  cast::met(s, 3, explore::MeetOutcome::Won, false);  // a spar
  cast::person(s, 3, p);
  EXPECT_EQ(p.opinion, 1);
  EXPECT_EQ(p.parting, cast::Parting::YouWon);
  cast::met(s, 3, explore::MeetOutcome::Won, true);  // a challenge
  cast::met(s, 3, explore::MeetOutcome::Won, true);
  cast::person(s, 3, p);
  EXPECT_EQ(p.opinion, -1);
  EXPECT_EQ(p.meets, 3);  // counts to three
  cast::met(s, 3, explore::MeetOutcome::Lost, true);
  cast::person(s, 3, p);
  EXPECT_EQ(p.parting, cast::Parting::YouLost);
  EXPECT_EQ(p.opinion, -1);
}

TEST(V035Meet, ARealMeetingEndsInTheirMemoryAndNeverMakesANemesis) {
  int lost = 0;
  for (uint32_t k = 0; k < 40; ++k) {
    explore::Layout l;
    GameState s = meetWorld(4, l);
    cast::setOpinion(s, 4, -3);  // a challenge
    s.progress.realm = 2;        // they are far stronger
    setCounter(s, k * 7u);
    const explore::MeetResult r = explore::meet(s, l, explore::MeetChoice::Offer);
    ASSERT_TRUE(r.fought);
    EXPECT_EQ(r.cast, 4);
    EXPECT_EQ(r.nemesisBorn, -1);
    cast::Person p;
    cast::person(s, 4, p);
    EXPECT_TRUE(p.met);
    EXPECT_EQ(p.meets, 1);
    if (!r.won) ++lost;
    EXPECT_EQ(rivals::nemesisCount(s), 0);
  }
  EXPECT_GT(lost, 20);
}

TEST(V035Meet, AStepCanMeetThem) {
  int met = 0;
  for (uint32_t k = 0; k < 3000 && met == 0; ++k) {
    GameState s = world(17);
    read(s, 1);
    setPerson(s, 4, 2, 4, 0, 30);
    alone(s, 4, 0);
    explore::Layout l = explore::layoutOf(s, 0);
    const uint8_t from = explore::enter(s, l, 0);
    s.progress.stamina = 20;
    setCounter(s, k);
    uint8_t nb[4];
    const uint8_t n = explore::neighbours(from, nb);
    for (uint8_t i = 0; i < n; ++i) {
      const explore::Tile t = l.tiles[nb[i]];
      if (t == explore::Tile::Lair || t == explore::Tile::Guardian) continue;
      if (explore::canStep(s, l, 0, nb[i]) != explore::StepError::None) continue;
      if (explore::step(s, l, 0, nb[i]).meeting && explore::pendingMeeting(s, l).cast == 4) ++met;
      break;
    }
  }
  EXPECT_GT(met, 0);
}

// --- Save and text ---------------------------------------------------------------------------------

TEST(V035Save, VersionNineRoundTripsAndAnOlderSaveFoundsTheCastLater) {
  EXPECT_EQ(save::kSaveVersion, 9);
  EXPECT_EQ(save::kPayloadSize, 1781u);
  GameState s = world(19);
  read(s, 5);
  cast::met(s, 2, explore::MeetOutcome::Talked, false);
  Storage.reset();
  ASSERT_TRUE(save::store(s));
  GameState back;
  ASSERT_EQ(save::load(back), save::LoadResult::Loaded);
  EXPECT_EQ(back.cast, s.cast);
  // A v8 save: the cast block is empty and is founded on the next world step.
  GameState old = world(19);
  read(old, 5);
  old.cast.fill(0);
  EXPECT_FALSE(cast::founded(old));
  read(old, 1);
  EXPECT_TRUE(cast::founded(old));
  GameState fresh;
  fresh.reset();
  EXPECT_FALSE(cast::founded(fresh));
}

TEST(V035Text, EveryListExpands) {
  Pack pack;
  GameState s = world(23);
  const char* lines[] = {
      "cast.line.arrived",    "cast.line.arrived.disciple", "cast.line.breakthrough", "cast.line.failed",
      "cast.line.seclusion",  "cast.line.joined",           "cast.line.left",         "cast.line.rose",
      "cast.line.apprentice", "cast.line.married",          "cast.line.sworn",        "cast.line.feud",
      "cast.line.fellout",    "cast.line.died.old",         "cast.line.died.war"};
  for (const char* key : lines) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      lifelog::Line line;
      ASSERT_TRUE(exploretext::personLine(line, s, key, seed, "the Reedwater Marshes", "Old Bai",
                                          "an inner disciple of the Azure Cloud Sect", *pack.grammar))
          << key;
      EXPECT_TRUE(clean(line.text)) << line.text;
      EXPECT_LT(strlen(line.text), lifelog::kLineSize - 1) << key << ": " << line.text;
      EXPECT_EQ(std::string(line.text).find("Spirit Stone"), std::string::npos) << line.text;
    }
  }
  const char* screen[] = {"cast.rumor.met", "cast.rumor.unmet", "cast.meet.first",    "cast.meet.again",
                          "cast.meet.won",  "cast.meet.lost",   "cast.meet.friendly", "cast.meet.bitter"};
  for (const char* key : screen) {
    for (uint32_t seed = 0; seed < 12; ++seed) {
      char out[300];
      ASSERT_TRUE(exploretext::personFlavor(s, key, seed, "the Reedwater Marshes",
                                            "a cultivator of the Azure Cloud Sect", "Foundation Establishment",
                                            *pack.grammar, out, sizeof(out)))
          << key;
      EXPECT_TRUE(clean(out)) << out;
      EXPECT_LT(strlen(out), 199u) << key << ": " << out;
    }
  }
}

TEST(V035Cast, EveryoneHasTheirOwnName) {
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    GameState s = world(seed * 389u);
    for (int y = 0; y < 300; ++y) {  // newcomers too
      read(s, 1);
      if (y % 50 != 0) continue;
      std::vector<std::string> names;
      for (uint8_t i = 0; i < cast::kPeople; ++i) {
        char n[32];
        cast::name(s, i, n, sizeof(n));
        ASSERT_GT(strlen(n), 2u);
        EXPECT_NE(std::string(n), std::string(s.character.name.data()));
        for (const std::string& o : names) EXPECT_NE(o, std::string(n)) << seed;
        names.emplace_back(n);
      }
    }
  }
}

TEST(V035Pack, StillUnderTheBudget) {
  Pack pack;
  EXPECT_LT(pack.index.count, 480u);  // at least 32 of the 512 left for SD packs
}
