#include <Breakthrough.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>

using namespace cultivation;
using namespace cultivation::trials;

namespace {

GameState atPeak(const uint8_t realm, const uint32_t worldSeed = 0x5EED1234) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = worldSeed;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.realm = realm;
  s.progress.stage = static_cast<uint8_t>(realmInfo(realm).stages - 1);
  s.progress.qi = stageCost(realm, s.progress.stage);
  s.progress.hpMax = 100;
  s.progress.hp = 100;
  return s;
}

uint8_t addScroll(GameState& s, const scrolls::Kind kind, const uint8_t hours, const uint32_t hash,
                  const uint8_t flags = 0) {
  scrolls::Scroll sc;
  sc.titleHash = hash;
  sc.kind = kind;
  sc.hours = hours;
  sc.comprehension = 90;
  sc.flags = flags;
  const uint8_t n = scrolls::count(s);
  scrolls::put(s, n, sc);
  s.techniqueScrolls[0] = static_cast<uint8_t>(n + 1);
  return n;
}

constexpr uint8_t kTech = scrolls::Scroll::kEquippedTechnique;

}  // namespace

TEST(Trials, WhichRealmsHaveWhichTrial) {
  EXPECT_EQ(kindFor(1), Kind::None);
  EXPECT_EQ(kindFor(2), Kind::None);
  EXPECT_EQ(kindFor(3), Kind::Tribulation);
  EXPECT_EQ(kindFor(4), Kind::HeartDemon);
  EXPECT_EQ(kindFor(5), Kind::Tribulation);
  EXPECT_EQ(kindFor(6), Kind::HeartDemon);
  EXPECT_EQ(kindFor(7), Kind::Tribulation);
  EXPECT_EQ(kindFor(8), Kind::HeartDemon);
  EXPECT_EQ(kindFor(9), Kind::Tribulation);
  EXPECT_EQ(strikesFor(3), 3);
  EXPECT_EQ(strikesFor(5), 5);
  EXPECT_EQ(strikesFor(7), 7);
  EXPECT_EQ(strikesFor(9), 9);
  EXPECT_EQ(strikesFor(4), 0);
}

TEST(Trials, StrikeDamageTotals) {
  const struct {
    uint8_t strikes;
    uint32_t total;
  } expected[] = {{3, 90}, {5, 105}, {7, 147}, {9, 198}};
  for (const auto& e : expected) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < e.strikes; ++i) total += strikePercent(e.strikes, i);
    EXPECT_EQ(total, e.total) << int(e.strikes);
  }
}

TEST(Trials, EnduringTheFirstTribulationSurvivesAtFullHp) {
  GameState s = atPeak(2);
  addScroll(s, scrolls::Kind::Sword, 4, 1);  // the Yellow key into Core Formation
  ASSERT_TRUE(begin(s));
  EXPECT_TRUE(inTrial(s));
  const uint16_t expect[] = {25, 30, 35};
  for (int i = 0; i < 3; ++i) {
    const Strike st = currentStrike(s);
    EXPECT_EQ(st.index, i);
    EXPECT_EQ(st.base, expect[i]);
    ASSERT_GE(st.optionCount, 1);
    EXPECT_EQ(st.options[0].answer, Answer::Endure);
    const StrikeResult r = answerStrike(s, 0);
    EXPECT_EQ(r.progress, i < 2 ? Progress::Continue : Progress::Won);
  }
  EXPECT_EQ(s.progress.hp, 10);
  EXPECT_FALSE(inTrial(s));
  EXPECT_EQ(scrolls::count(s), 1);  // the key is untouched: the roll spends it on success
}

TEST(Trials, ZeroHpLosesAndBlocksTheNextStart) {
  GameState s = atPeak(2);
  addScroll(s, scrolls::Kind::Sword, 4, 1);
  s.progress.hp = 50;
  ASSERT_TRUE(begin(s));
  EXPECT_EQ(answerStrike(s, 0).progress, Progress::Continue);  // 25
  EXPECT_EQ(answerStrike(s, 0).progress, Progress::Lost);      // 30 > 25 left
  EXPECT_EQ(s.progress.hp, 0);
  EXPECT_FALSE(inTrial(s));
  breakthrough::failAttempt(s);
  EXPECT_EQ(s.progress.failures, 1);
  EXPECT_TRUE(breakthrough::isHalfStep(s));
  s.progress.qi = stageCost(2, s.progress.stage);  // refilled by reading
  EXPECT_FALSE(begin(s));                          // no HP: cannot face lightning
}

TEST(Trials, BodyTechniqueCutsAndTurnHalves) {
  GameState s = atPeak(4);  // into Soul Transformation: 5 strikes
  s.progress.hpMax = 200;
  s.progress.hp = 200;
  addScroll(s, scrolls::Kind::Sword, 16, 1);           // Heaven key, unequipped
  addScroll(s, scrolls::Kind::Body, 9, 2, kTech);      // Earth: -30%
  addScroll(s, scrolls::Kind::Movement, 1, 3, kTech);  // can turn
  addScroll(s, scrolls::Kind::Mind, 1, 4, kTech);      // cannot turn
  addScroll(s, scrolls::Kind::PalmFist, 1, 5);         // unequipped: the burn candidate
  ASSERT_TRUE(begin(s));
  Strike st = currentStrike(s);
  EXPECT_EQ(st.base, 30);  // 15% of 200
  ASSERT_EQ(st.optionCount, 3);
  EXPECT_EQ(st.options[0].answer, Answer::Endure);
  EXPECT_EQ(st.options[0].damage, 21);  // 30 x 70%
  EXPECT_EQ(st.options[1].answer, Answer::Turn);
  EXPECT_EQ(st.options[1].slot, 2);
  EXPECT_EQ(st.options[1].damage, 15);
  EXPECT_EQ(st.options[2].answer, Answer::Burn);
  EXPECT_EQ(st.options[2].slot, 4);  // never the key (slot 0)
  EXPECT_EQ(st.options[2].damage, 0);

  answerStrike(s, 1);  // turn with the Movement technique
  EXPECT_EQ(s.progress.hp, 185);
  st = currentStrike(s);
  ASSERT_EQ(st.optionCount, 2);  // it is used up for this tribulation
  EXPECT_EQ(st.options[1].answer, Answer::Burn);
  const StrikeResult r = answerStrike(s, 1);
  EXPECT_TRUE(r.didBurn);
  EXPECT_EQ(r.burned.titleHash, 5u);
  EXPECT_EQ(scrolls::count(s), 4);
  EXPECT_EQ(s.progress.hp, 185);
  st = currentStrike(s);
  EXPECT_EQ(st.optionCount, 1);  // nothing left to burn but the key
}

TEST(Trials, ATrialResumesWhereItStopped) {
  GameState s = atPeak(2);
  addScroll(s, scrolls::Kind::Sword, 4, 1);
  ASSERT_TRUE(begin(s));
  answerStrike(s, 0);
  const GameState saved = s;  // as if saved and reloaded
  GameState loaded = saved;
  EXPECT_TRUE(inTrial(loaded));
  EXPECT_EQ(step(loaded), 1);
  EXPECT_TRUE(begin(loaded));  // begin() on a trial under way keeps its place
  EXPECT_EQ(step(loaded), 1);
  EXPECT_EQ(currentStrike(loaded).base, 30);
}

TEST(Trials, HeartDemonChance) {
  GameState s = atPeak(3);
  s.progress.daoHeart = 0;
  EXPECT_EQ(sceneChance(s), 40);
  s.progress.daoHeart = 100;
  EXPECT_EQ(sceneChance(s), 70);
  s.progress.coreGrade = 1;  // 9th grade
  EXPECT_EQ(sceneChance(s), 70);
  s.progress.coreGrade = 9;  // 1st grade
  EXPECT_EQ(sceneChance(s), 90);
  addScroll(s, scrolls::Kind::Mind, 1, 9, kTech);
  EXPECT_EQ(sceneChance(s), 95);  // capped
}

TEST(Trials, HeartDemonEndsAfterTwoWinsOrTwoLosses) {
  int wins = 0;
  int losses = 0;
  for (uint32_t world = 1; world < 400; ++world) {
    GameState s = atPeak(3, world);
    addScroll(s, scrolls::Kind::Sword, 9, 1);  // Earth key into Nascent Soul
    s.progress.daoHeart = 0;                   // 40%: both outcomes are common
    ASSERT_TRUE(begin(s));
    Progress p = Progress::Continue;
    int scenes = 0;
    while (p == Progress::Continue) {
      const SceneResult r = answerScene(s, false);
      ASSERT_LT(r.roll, 100);
      p = r.progress;
      ++scenes;
    }
    EXPECT_GE(scenes, 2);
    EXPECT_LE(scenes, 3);
    EXPECT_FALSE(inTrial(s));
    (p == Progress::Won ? wins : losses)++;
  }
  // P(win) at 40% per scene = 0.4^2 + 2 x 0.4^2 x 0.6 = 35.2%
  EXPECT_GT(wins, 400 * 25 / 100);
  EXPECT_LT(wins, 400 * 45 / 100);
  EXPECT_GT(losses, 0);
}

TEST(Trials, BurningAMindScrollPassesAScene) {
  GameState s = atPeak(3);
  addScroll(s, scrolls::Kind::Sword, 9, 1);  // the key
  addScroll(s, scrolls::Kind::Mind, 9, 2);   // a Mind scroll that would also qualify as key: not used for keys
  addScroll(s, scrolls::Kind::Mind, 1, 3);   // the weaker Mind scroll: burned first
  ASSERT_TRUE(begin(s));
  EXPECT_EQ(burnableMindScroll(s), 2);
  const SceneResult r = answerScene(s, true);
  EXPECT_TRUE(r.passed);
  EXPECT_TRUE(r.didBurn);
  EXPECT_EQ(r.burned.titleHash, 3u);
  EXPECT_EQ(scenesWon(s), 1);
  EXPECT_EQ(scenesLost(s), 0);
}

TEST(Trials, TheSameSceneAlwaysRollsTheSame) {
  GameState a = atPeak(3, 77);
  addScroll(a, scrolls::Kind::Sword, 9, 1);
  GameState b = a;
  ASSERT_TRUE(begin(a));
  ASSERT_TRUE(begin(b));
  EXPECT_EQ(answerScene(a, false).roll, answerScene(b, false).roll);
}

TEST(Trials, NoTrialBeforeCoreFormation) {
  GameState s = atPeak(1);
  addScroll(s, scrolls::Kind::Sword, 1, 1);
  EXPECT_FALSE(begin(s));
  EXPECT_FALSE(inTrial(s));
}

TEST(Trials, HpHealsOnePerMinuteRead) {
  GameState s = atPeak(0);
  s.progress.hp = 10;
  s.progress.hpMax = 120;
  VisitGains g;
  g.seconds = 45 * 60 + 30;
  g.tiers.fullSeconds = g.seconds;
  const AbsorbResult r = absorb(s, g);
  EXPECT_EQ(r.hpGained, 45);
  EXPECT_EQ(s.progress.hp, 55);
  g.seconds = 5 * 3600;
  g.tiers.fullSeconds = g.seconds;
  absorb(s, g);
  EXPECT_EQ(s.progress.hp, 120);  // never past max
}
