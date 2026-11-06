#include <Daily.h>
#include <Exploration.h>
#include <GameState.h>
#include <Inventory.h>
#include <Rivals.h>
#include <Sects.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstdio>

// v0.3.3 "The Living World, part 2" (agreed with the user in session 15):
// people belong to the sects, your wins decide stories, standing, four more
// story types, a condition about every 12 reading hours.

using namespace cultivation;

namespace {

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
  sects::advance(s);  // founds the sects
  return s;
}

uint8_t* sectBytes(GameState& s, const uint8_t i) { return s.living.data() + 10 + 10 * i; }

// Gives sect `i` a story at `stage` of `stages` in `region` (other: a feud's other sect).
void story(GameState& s, const uint8_t i, const sects::Story st, const uint8_t region, const int8_t other = -1,
           const uint8_t stage = 1, const uint8_t stages = 4) {
  uint8_t* b = sectBytes(s, i);
  b[3] = static_cast<uint8_t>(st);
  b[4] = stage;
  b[5] = stages;
  b[6] = static_cast<uint8_t>(other + 1);
  b[7] = region;
}

void condition(GameState& s, const sects::Condition c, const uint8_t region, const uint8_t owner,
               const int8_t other = -1, const uint16_t years = 4) {
  const uint32_t now = s.progress.gameReadingSeconds / 3600u;
  s.living[3] = static_cast<uint8_t>(c);
  s.living[4] = region;
  s.living[5] = static_cast<uint8_t>(now + years);
  s.living[6] = static_cast<uint8_t>((now + years) >> 8);
  s.living[7] = 0;
  s.living[8] = owner;
  s.living[9] = static_cast<uint8_t>(other + 1);
}

bool has(const sects::Influence& w, const sects::EventKind k) {
  for (uint8_t i = 0; i < w.count; ++i) {
    if (w.events[i].kind == k) return true;
  }
  return false;
}

}  // namespace

TEST(V033People, EveryoneBelongsToASectOrToTheCult) {
  GameState s = world(1);
  uint32_t seen[sects::kSects] = {};
  for (uint32_t seed = 0; seed < 500; ++seed) {
    const int8_t k = sects::personSect(s, 0, seed);
    ASSERT_GE(k, 0);
    ASSERT_LT(k, sects::kSects);
    ++seen[k];
    EXPECT_EQ(sects::personSect(s, 0, seed), k);  // the same person, the same sect
  }
  for (uint32_t n : seen) EXPECT_GT(n, 60u);
  // In a war, one of the two sides; in a cult's region, a follower; the ranking ignores both.
  condition(s, sects::Condition::SectWar, 1, 0, 3);
  uint32_t a = 0;
  uint32_t b = 0;
  for (uint32_t seed = 0; seed < 400; ++seed) {
    const int8_t k = sects::personSect(s, 1, seed);
    ASSERT_TRUE(k == 0 || k == 3) << +k;
    (k == 0 ? a : b) += 1;
    EXPECT_GE(sects::personSect(s, sects::kNoRegion, seed), 0);
  }
  EXPECT_GT(a, 150u);
  EXPECT_GT(b, 150u);
  condition(s, sects::Condition::Demonic, 2, 1);
  EXPECT_EQ(sects::personSect(s, 2, 77), sects::kCult);
  EXPECT_NE(sects::personSect(s, 1, 77), sects::kCult);
  GameState none;
  EXPECT_EQ(sects::personSect(none, 0, 1), -1);
}

TEST(V033Influence, ThreeOfOneSideDecideTheWar) {
  for (const bool ownerLoses : {false, true}) {
    GameState s = world(2);
    story(s, 0, sects::Story::Feud, 1, 3);
    condition(s, sects::Condition::SectWar, 1, 0, 3);
    const uint8_t loser = ownerLoses ? 0 : 3;
    const uint8_t winner = ownerLoses ? 3 : 0;
    const uint8_t strengthWinner = sects::sect(s, winner).strength;
    EXPECT_EQ(sects::cultivatorBeaten(s, 1, static_cast<int8_t>(loser)).count, 0);
    EXPECT_EQ(sects::cultivatorBeaten(s, 1, static_cast<int8_t>(winner)).count, 0);  // the other side: a separate count
    EXPECT_EQ(sects::cultivatorBeaten(s, 1, static_cast<int8_t>(loser)).count, 0);
    const sects::Influence w = sects::cultivatorBeaten(s, 1, static_cast<int8_t>(loser));
    ASSERT_GE(w.count, 2);
    EXPECT_EQ(w.events[0].kind, sects::EventKind::WarDecided);
    EXPECT_EQ(w.events[0].sect, winner);
    EXPECT_EQ(w.events[0].other, loser);
    EXPECT_EQ(w.events[1].kind, sects::EventKind::Ended);
    EXPECT_EQ(w.events[1].good, !ownerLoses);
    EXPECT_EQ(sects::condition(s), sects::Condition::None);
    EXPECT_EQ(sects::sect(s, 0).story, sects::Story::None);
    EXPECT_EQ(sects::standing(s, winner), 1);
    EXPECT_EQ(sects::standing(s, loser), -1);
    EXPECT_GE(sects::sect(s, winner).strength + 1, strengthWinner);
  }
}

TEST(V033Influence, CampTideAndCultEndTheirStoriesWell) {
  GameState s = world(3);
  story(s, 1, sects::Story::Bandits, 2);
  condition(s, sects::Condition::BanditCamp, 2, 1);
  sects::banditBeaten(s, 2);
  sects::banditBeaten(s, 2);
  sects::Influence w = sects::banditBeaten(s, 2);
  EXPECT_TRUE(has(w, sects::EventKind::CampBroken));
  EXPECT_TRUE(has(w, sects::EventKind::Ended));
  EXPECT_EQ(sects::sect(s, 1).story, sects::Story::None);
  EXPECT_EQ(sects::standing(s, 1), 1);

  story(s, 2, sects::Story::BeastTide, 0);
  condition(s, sects::Condition::BeastTide, 0, 2);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(sects::beastBeaten(s, 0).count, 0);
  w = sects::beastBeaten(s, 0);
  EXPECT_TRUE(has(w, sects::EventKind::TideBroken));
  EXPECT_EQ(w.herbs, 2);
  EXPECT_EQ(w.ores, 2);
  EXPECT_EQ(sects::standing(s, 2), 1);

  story(s, 4, sects::Story::Demonic, 1);
  condition(s, sects::Condition::Demonic, 1, 4);
  EXPECT_EQ(sects::enemyLevelBonus(s, 1), 2);
  EXPECT_EQ(sects::fightStonesPermille(s, 1), 1500);
  EXPECT_EQ(sects::favouredEnemy(s, 1), static_cast<uint8_t>(explore::Enemy::Cultivator));
  sects::cultivatorBeaten(s, 1, sects::kCult);
  sects::cultivatorBeaten(s, 1, sects::kCult);
  w = sects::cultivatorBeaten(s, 1, sects::kCult);
  EXPECT_TRUE(has(w, sects::EventKind::CultBroken));
  EXPECT_EQ(sects::condition(s), sects::Condition::None);
  EXPECT_EQ(sects::standing(s, 4), 1);
}

TEST(V033Influence, ThreeOfASectsPeopleDentItOnceAStory) {
  GameState s = world(4);
  const uint8_t before = sects::sect(s, 2).strength;
  EXPECT_EQ(sects::cultivatorBeaten(s, 0, 2).count, 0);
  EXPECT_EQ(sects::cultivatorBeaten(s, sects::kNoRegion, 2).count, 0);  // the ranking counts too
  const sects::Influence w = sects::cultivatorBeaten(s, 0, 2);
  ASSERT_EQ(w.count, 1);
  EXPECT_EQ(w.events[0].kind, sects::EventKind::Dented);
  EXPECT_EQ(sects::sect(s, 2).strength, before > 1 ? before - 1 : 1);
  EXPECT_EQ(sects::standing(s, 2), -1);
  for (int i = 0; i < 6; ++i) EXPECT_EQ(sects::cultivatorBeaten(s, 0, 2).count, 0);  // once a story
  // A new story, a new count.
  story(s, 2, sects::Story::Fair, 9, -1, 3, 4);
  s.progress.gameReadingSeconds += 3600;
  for (int h = 0; h < 200 && sects::sect(s, 2).story != sects::Story::None; ++h) {
    s.progress.gameReadingSeconds += 3600;
    sects::advance(s);
  }
  for (int i = 0; i < 2; ++i) EXPECT_EQ(sects::cultivatorBeaten(s, 0, 2).count, 0);
  EXPECT_EQ(sects::cultivatorBeaten(s, 0, 2).count, 1);
}

TEST(V033Influence, StandingStaysWithinFiveEitherWay) {
  GameState s = world(5);
  for (int round = 0; round < 12; ++round) {
    story(s, 1, sects::Story::Bandits, 0);
    condition(s, sects::Condition::BanditCamp, 0, 1);
    for (int i = 0; i < 3; ++i) sects::banditBeaten(s, 0);
  }
  EXPECT_EQ(sects::standing(s, 1), sects::kStandingMax);
  EXPECT_GE(sects::standingRumorSect(s, 1), 0);
  for (int round = 0; round < 15; ++round) {
    story(s, 3, sects::Story::Fair, 9);  // a fresh story each round, so it can be dented again
    s.living[10 + 30 + 9] = static_cast<uint8_t>(s.living[10 + 30 + 9] & 0xF0);
    for (int i = 0; i < 3; ++i) sects::cultivatorBeaten(s, 0, 3);
  }
  EXPECT_EQ(sects::standing(s, 3), sects::kStandingMin);
  EXPECT_GE(sects::sect(s, 3).strength, 1);
}

TEST(V033Inheritance, YoursIfYouWinASearchFirst) {
  GameState s = world(6);
  story(s, 0, sects::Story::Inheritance, 2);
  condition(s, sects::Condition::Inheritance, 2, 0);
  EXPECT_EQ(sects::ruinBonus(s, 2), 15);
  EXPECT_EQ(sects::searchWon(s, 1).count, 0);  // another region
  const sects::Influence w = sects::searchWon(s, 2);
  EXPECT_TRUE(has(w, sects::EventKind::YouTookInheritance));
  EXPECT_TRUE(w.treasure);
  EXPECT_EQ(w.stones, 300u);
  EXPECT_EQ(sects::condition(s), sects::Condition::None);
  EXPECT_EQ(sects::sect(s, 0).story, sects::Story::None);
  EXPECT_EQ(sects::standing(s, 0), -1);
}

TEST(V033Inheritance, TheSectsRaceInReadingTime) {
  uint32_t early = 0;
  uint32_t total = 0;
  for (uint32_t w = 1; w <= 40; ++w) {
    GameState s = world(100 + w);
    story(s, 0, sects::Story::Inheritance, 2);
    condition(s, sects::Condition::Inheritance, 2, 0);
    for (uint16_t h = 1; h <= sects::kConditionYears; ++h) {
      s.progress.gameReadingSeconds += 3600;
      const sects::Steps st = sects::advance(s);
      bool ended = false;
      bool taken = false;
      for (uint8_t i = 0; i < st.count; ++i) {
        if (st.events[i].kind == sects::EventKind::ConditionEnded) {
          ended = true;
          EXPECT_EQ(st.events[i].condition, sects::Condition::Inheritance);
        }
        if (st.events[i].kind == sects::EventKind::Ended && st.events[i].sect == 0) {
          taken = st.events[i].story == sects::Story::Inheritance && st.events[i].good;  // the sect took it
        }
      }
      if (ended) {
        ++total;
        if (h < sects::kConditionYears) ++early;
        EXPECT_TRUE(taken);
        break;
      }
      EXPECT_LE(sects::conditionTally(s), sects::kArrivalsToClaim);
    }
  }
  EXPECT_EQ(total, 40u);
  EXPECT_GT(early, 10u);  // often before the four hours are up: a race
}

TEST(V033Stories, AlliancesAndGates) {
  GameState s = world(7);
  EXPECT_FALSE(sects::allied(s, 1, 3));
  // Run worlds until an alliance ends well and a gate moves.
  bool sealed = false;
  bool moved = false;
  for (uint32_t w = 1; w <= 40 && !(sealed && moved); ++w) {
    GameState g = world(w * 31u);
    uint8_t homes[sects::kSects];
    for (uint8_t i = 0; i < sects::kSects; ++i) homes[i] = sects::sect(g, i).home;
    for (int h = 0; h < 300; ++h) {
      g.progress.gameReadingSeconds += 3600;
      const sects::Steps st = sects::advance(g);
      for (uint8_t i = 0; i < st.count; ++i) {
        const sects::Event& e = st.events[i];
        if (e.kind != sects::EventKind::Ended || !e.good) continue;
        if (e.story == sects::Story::Alliance) {
          ASSERT_GE(e.other, 0);
          EXPECT_TRUE(sects::allied(g, e.sect, static_cast<uint8_t>(e.other)));
          EXPECT_TRUE(sects::allied(g, static_cast<uint8_t>(e.other), e.sect));
          sealed = true;
        }
        if (e.story == sects::Story::GateMove) {
          EXPECT_EQ(sects::sect(g, e.sect).home, e.region);
          EXPECT_NE(e.region, homes[e.sect]);
          homes[e.sect] = e.region;
          moved = true;
        }
      }
    }
  }
  EXPECT_TRUE(sealed);
  EXPECT_TRUE(moved);
}

TEST(V033Stories, AStoryWaitsForItsCondition) {
  GameState s = world(8);
  story(s, 0, sects::Story::Ruins, 1, -1, 2, 3);
  condition(s, sects::Condition::Ruins, 1, 0, -1, 30);
  for (int h = 0; h < 20; ++h) {
    s.progress.gameReadingSeconds += 3600;
    sects::advance(s);
    ASSERT_EQ(sects::sect(s, 0).story, sects::Story::Ruins) << h;
  }
}

TEST(V033Save, AV032WorldKeepsItsSectsAndStartsAtNeutral) {
  GameState s = world(9);
  // As v0.3.2 left it: no v0.3.3 layout flag, "stories finished" in byte +9.
  s.living[2] = 0x01;
  for (uint8_t i = 0; i < sects::kSects; ++i) sectBytes(s, i)[9] = static_cast<uint8_t>(i + 1);
  const uint8_t strength = sects::sect(s, 2).strength;
  for (uint8_t i = 0; i < sects::kSects; ++i) EXPECT_EQ(sects::standing(s, i), 0);
  s.progress.gameReadingSeconds += 3600;
  sects::advance(s);
  EXPECT_EQ(s.living[2] & 0x02, 0x02);
  for (uint8_t i = 0; i < sects::kSects; ++i) EXPECT_EQ(sects::standing(s, i), 0);
  EXPECT_GE(sects::sect(s, 2).strength + 1, strength);
}

TEST(V033Ranking, BeatingSeatHoldersDentsTheirSects) {
  GameState s = world(10, 2);
  s.progress.stage = 9;
  s.progress.hpMax = 5000;
  uint32_t dents = 0;
  uint32_t wins = 0;
  for (uint32_t day = 9802; day < 9802 + 60; ++day) {
    s.progress.hp = s.progress.hpMax;
    StatsSample now;
    now.today = day;  // a new day for the ranking's once-a-day bouts
    daily::update(s, VisitGains{}, now, day);
    rivals::update(s, now);
    const rivals::BoutResult b = rivals::challenge(s);
    if (b.won && !b.rival) ++wins;
    if (has(b.world, sects::EventKind::Dented)) ++dents;
  }
  printf("ranking: %u wins, %u dents\n", wins, dents);
  EXPECT_GT(wins, 5u);
  EXPECT_GE(dents, 1u);  // three seat holders of one sect beaten
  for (uint8_t i = 0; i < sects::kSects; ++i) EXPECT_GE(sects::standing(s, i), sects::kStandingMin);
}
