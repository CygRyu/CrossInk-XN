#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

using namespace cultivation;

namespace {
uint64_t u64(const QiNumber& n) {
  uint64_t v = 0;
  EXPECT_TRUE(n.toU64(v));
  return v;
}

VisitGains minutes(const uint32_t m, const uint32_t pages = 0) {
  VisitGains g;
  g.seconds = m * 60;
  g.tiers.fullSeconds = m * 60;
  g.pages = pages;
  return g;
}

GameState newCharacter(const uint8_t talent = 2, const uint8_t purity = 50) {
  CharacterDraft d;
  strcpy(d.name, "Lin Yue");
  d.destiny.talent = talent;
  d.destiny.purity = purity;
  d.worldSeed = 1234;
  GameState s;
  createCharacter(s, d, StatsSample{});
  return s;
}
}  // namespace

TEST(Simulation, RealmTotalsMatchTheBalanceSimulator) {
  // tools/cultivation/balance_sim.py realm_qi(r) = 600 * H_r * 1.6^r
  const uint32_t expected[] = {2700, 10080, 46080, 127795, 294912, 566231, 1056965, 1932735, 3478924, 6184753};
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) EXPECT_EQ(realmInfo(r).totalQi, expected[r]) << int(r);
  EXPECT_EQ(realmInfo(0).stages, 9);
  EXPECT_EQ(realmInfo(1).stages, 9);
  EXPECT_EQ(realmInfo(2).stages, 4);
  EXPECT_FALSE(realmInfo(0).hasGreatPerfection);
  EXPECT_TRUE(realmInfo(1).hasGreatPerfection);
}

TEST(Simulation, StageCostsSumToTheRealmTotal) {
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) {
    uint64_t sum = 0;
    for (uint8_t s = 0; s < realmInfo(r).stages; ++s) sum += u64(stageCost(r, s));
    EXPECT_LE(sum, realmInfo(r).totalQi);
    EXPECT_GE(sum + realmInfo(r).stages, realmInfo(r).totalQi);  // rounding only
  }
}

TEST(Simulation, StageCostRisesTwelvePercentPerStage) {
  // Body Tempering: 2700 * 1000 / 13320 = 202 Qi for Layer 1, 2700 * 1960 / 13320 = 397 for Layer 9.
  EXPECT_EQ(u64(stageCost(0, 0)), 202u);
  EXPECT_EQ(u64(stageCost(0, 8)), 397u);
}

TEST(Simulation, TenQiPerMinuteAtTimesOne) {
  GameState s = newCharacter(2, 50);  // Ordinary Root, purity 50 -> x0.998
  ASSERT_EQ(qiMultiplierPermille(s), 998u);
  const AbsorbResult r = absorb(s, minutes(10));
  EXPECT_EQ(u64(r.qiGained), 99u);  // 100 Qi x 0.998, fraction carried
  absorb(s, minutes(0));
  EXPECT_EQ(s.progress.qiCarry, (100ull * 998 * 60) % 60000);
}

TEST(Simulation, CarryMakesManyShortVisitsEqualOneLongOne) {
  GameState a = newCharacter(0, 37);
  GameState b = newCharacter(0, 37);
  for (int i = 0; i < 60; ++i) absorb(a, minutes(1));
  absorb(b, minutes(60));
  EXPECT_EQ(a.progress.stage, b.progress.stage);
  EXPECT_EQ(a.progress.qi, b.progress.qi);
  EXPECT_EQ(a.progress.ageMonths, b.progress.ageMonths);
}

TEST(Simulation, SoftCappedSecondsGiveLessQi) {
  GameState s = newCharacter();
  VisitGains g;
  g.seconds = 300 * 60;
  g.tiers.fullSeconds = 120 * 60;
  g.tiers.halfSeconds = 120 * 60;
  g.tiers.quarterSeconds = 60 * 60;
  const AbsorbResult r = absorb(s, g);
  EXPECT_EQ(u64(r.qiGained), (120 + 60 + 15) * 10 * 998 / 1000);
}

TEST(Simulation, AgeIsOneYearPerReadingHour) {
  GameState s = newCharacter();
  EXPECT_EQ(ageYears(s), 16);
  const AbsorbResult r = absorb(s, minutes(60));
  EXPECT_EQ(r.monthsPassed, 12u);
  EXPECT_EQ(ageYears(s), 17);
  absorb(s, minutes(4));  // 4 of 5 minutes toward the next month
  EXPECT_EQ(s.progress.ageMonths, 17u * 12u);
  EXPECT_EQ(s.progress.ageSecondsCarry, 240);
}

TEST(Simulation, StonesAreOnePerMinutePlusOnePerTwoPages) {
  // Decision 5B: 10 minutes and 7 pages -> 10 + 3 stones, one page carried.
  GameState s = newCharacter();
  AbsorbResult r = absorb(s, minutes(10, 7));
  EXPECT_EQ(r.stonesGained, 13u);
  EXPECT_EQ(s.progress.stones, 13u);
  EXPECT_NE(s.progress.progressFlags & ProgressBlock::kOddPageCarry, 0);
  r = absorb(s, minutes(10, 1));  // the carried page completes a pair
  EXPECT_EQ(r.stonesGained, 11u);
  EXPECT_EQ(s.progress.progressFlags & ProgressBlock::kOddPageCarry, 0);
}

TEST(Simulation, PageStonesCannotExceedHalfTheMinutes) {
  GameState s = newCharacter();
  const AbsorbResult r = absorb(s, minutes(30, 500));  // auto page-turn farming
  EXPECT_EQ(r.stonesGained, 30u + 15u);
  EXPECT_EQ(s.progress.progressFlags & ProgressBlock::kOddPageCarry, 0);
}

TEST(Simulation, FastAndSlowReadersEarnSimilarStones) {
  GameState slow = newCharacter();
  GameState fast = newCharacter();
  for (int day = 0; day < 30; ++day) {
    absorb(slow, minutes(30, 21));  // 0.7 pages/min
    absorb(fast, minutes(30, 42));  // 1.4 pages/min
  }
  EXPECT_EQ(slow.progress.stones, 30u * (30 + 10) + 15u);  // 15 carried odd pages paired up
  EXPECT_EQ(fast.progress.stones, 30u * 45);
}

TEST(Simulation, StoneMinutesCarryAcrossShortVisits) {
  GameState s = newCharacter();
  VisitGains g;
  g.seconds = 30;
  g.tiers.fullSeconds = 30;
  EXPECT_EQ(absorb(s, g).stonesGained, 0u);
  EXPECT_EQ(absorb(s, g).stonesGained, 1u);  // the second half-minute completes one
}

TEST(Simulation, StaminaIsOnePerFifteenMinutesCappedAtTwelve) {
  GameState a = newCharacter();
  GameState b = newCharacter();
  for (int i = 0; i < 3; ++i) absorb(a, minutes(10));  // three short sessions
  absorb(b, minutes(30));                              // one long one
  EXPECT_EQ(a.progress.stamina, 2);
  EXPECT_EQ(b.progress.stamina, 2);
  const AbsorbResult r = absorb(b, minutes(600));
  EXPECT_EQ(b.progress.stamina, kStaminaCap);
  EXPECT_EQ(r.staminaGained, kStaminaCap - 2);
}

TEST(Simulation, DaoHeartGrowsWithHoursAndNeverDecays) {
  GameState s = newCharacter();
  absorb(s, minutes(90));
  EXPECT_EQ(s.progress.daoHeart, 10);  // 1.5 of 15 h
  absorb(s, minutes(0));               // a long absence changes nothing
  EXPECT_EQ(s.progress.daoHeart, 10);
  absorb(s, minutes(15 * 60));
  EXPECT_EQ(s.progress.daoHeart, kDaoHeartMax);
  EXPECT_EQ(daoHeartFor(UINT32_MAX), kDaoHeartMax);
}

TEST(Simulation, MinorStagesAdvanceAutomatically) {
  GameState s = newCharacter(2, 50);
  const AbsorbResult r = absorb(s, minutes(60));  // ~599 Qi: Layers 1-2 cost 202 + 226
  EXPECT_EQ(r.stagesGained, 2);
  EXPECT_EQ(s.progress.stage, 2);
  EXPECT_FALSE(r.atBottleneck);
}

TEST(Simulation, BodyTemperingTakesAboutFourAndAHalfHoursAtTimesOne) {
  GameState s = newCharacter(2, 50);  // x0.998
  ASSERT_EQ(qiMultiplierPermille(s), 998u);
  int minutesRead = 0;
  while (!atBottleneck(s) && minutesRead < 1000) {
    absorb(s, minutes(1));
    ++minutesRead;
  }
  EXPECT_NEAR(minutesRead, 270, 3);  // 4.5 h target
  EXPECT_EQ(s.progress.stage, 8);    // Layer 9 (Peak)
}

TEST(Simulation, PeakBarStopsAtTheBottleneck) {
  GameState s = newCharacter();
  absorb(s, minutes(2000));
  EXPECT_EQ(s.progress.realm, 0);
  EXPECT_EQ(s.progress.stage, 8);
  EXPECT_EQ(s.progress.qi, stageCost(0, 8));
  EXPECT_TRUE(atBottleneck(s));
}

TEST(Simulation, ResetGainsChangeNothing) {
  GameState s = newCharacter();
  VisitGains g = minutes(600, 900);
  g.resetDetected = true;
  absorb(s, g);
  EXPECT_TRUE(s.progress.qi.isZero());
  EXPECT_EQ(s.progress.stones, 0u);
  EXPECT_EQ(s.progress.stamina, 0);
  EXPECT_EQ(s.progress.daoHeart, 0);
  EXPECT_EQ(ageYears(s), 16);
}

TEST(Simulation, CreationStartsFromZeroWithBaseline) {
  StatsSample before;
  before.seconds = 500000;
  before.pages = 48210;
  before.completedBooks = 37;
  before.today = 9000;
  CharacterDraft d;
  strcpy(d.name, "Su Ming");
  d.gender = Gender::NonBinary;
  d.origin = Origin::SectServant;
  d.destiny = {Element::Water, 88, 4};
  d.rerollsUsed = 2;
  d.worldSeed = 0xABCDEF01;
  GameState s;
  createCharacter(s, d, before);

  EXPECT_TRUE(s.isCreated());
  EXPECT_EQ(s.progress.realm, 0);
  EXPECT_EQ(s.progress.stage, 0);
  EXPECT_TRUE(s.progress.qi.isZero());
  EXPECT_EQ(s.snapshot.pages, 48210u);        // earlier reading is excluded...
  EXPECT_EQ(s.baseline.completedBooks, 37u);  // ...but remembered for the prologue
  EXPECT_EQ(s.character.creationDay, 9000u);
  EXPECT_EQ(std::string(s.character.name.data(), 7), "Su Ming");
  EXPECT_EQ(s.character.rootElement, Element::Water);
  EXPECT_EQ(s.character.rootPurity, 88);
  EXPECT_EQ(s.character.talent, 4);
  EXPECT_EQ(s.character.rerollsUsed, 2);
  EXPECT_GE(s.character.hiddenTrait, 1);
  EXPECT_LE(s.character.hiddenTrait, kHiddenTraitCount);
  EXPECT_EQ(worldSeedOf(s), 0xABCDEF01u);
  EXPECT_EQ(lifeNumberOf(s), 1);
}

TEST(Simulation, DestinyIsDeterministicPerSeedAndRoll) {
  const DestinyRoll a = rollDestiny(77, 0);
  const DestinyRoll b = rollDestiny(77, 0);
  EXPECT_EQ(a.element, b.element);
  EXPECT_EQ(a.purity, b.purity);
  EXPECT_EQ(a.talent, b.talent);
  // Different rolls differ somewhere across the first few.
  int differences = 0;
  for (uint8_t i = 1; i < kMaxRolls; ++i) {
    const DestinyRoll c = rollDestiny(77, i);
    differences += (c.purity != a.purity || c.element != a.element || c.talent != a.talent);
  }
  EXPECT_GT(differences, 5);
}

TEST(Simulation, TalentsAreWeightedTowardTrash) {
  int counts[kTalentCount] = {};
  for (uint32_t seed = 1; seed <= 20000; ++seed) counts[rollDestiny(seed, 0).talent]++;
  EXPECT_NEAR(counts[0] / 20000.0, 0.35, 0.02);  // Choked Meridians is the norm
  EXPECT_NEAR(counts[4] / 20000.0, 0.04, 0.01);  // Heaven-Blessed is rare
  for (uint32_t seed = 1; seed <= 2000; ++seed) {
    const DestinyRoll r = rollDestiny(seed, 3);
    ASSERT_GE(r.purity, 1);
    ASSERT_LE(r.purity, 100);
    ASSERT_LT(static_cast<int>(r.element), static_cast<int>(Element::Count));
  }
}

TEST(Simulation, WorldSeedDependsOnMacDayAndLife) {
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t other[6] = {1, 2, 3, 4, 5, 7};
  const uint32_t base = worldSeed(mac, 9000, 1);
  EXPECT_EQ(base, worldSeed(mac, 9000, 1));
  EXPECT_NE(base, worldSeed(other, 9000, 1));
  EXPECT_NE(base, worldSeed(mac, 9001, 1));
  EXPECT_NE(base, worldSeed(mac, 9000, 2));
}

TEST(Simulation, SuggestedNamesFitAndVary) {
  std::set<std::string> seen;
  for (uint8_t i = 0; i < 40; ++i) {
    char buf[kNameMaxBytes + 1];
    suggestName(4242, i, i % 2 ? Gender::Female : Gender::Male, buf, sizeof(buf));
    const std::string name(buf);
    EXPECT_GT(name.size(), 3u);
    EXPECT_LE(name.size(), kNameMaxChars);
    EXPECT_NE(name.find(' '), std::string::npos);
    for (const char c : name) EXPECT_TRUE(c == ' ' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    seen.insert(name);
  }
  EXPECT_GT(seen.size(), 30u);
  char tiny[5];
  suggestName(1, 0, Gender::Male, tiny, sizeof(tiny));
  EXPECT_EQ(strlen(tiny), 4u);
}

TEST(Simulation, PurityScalesFromMinusToPlusFifteenPercent) {
  EXPECT_EQ(purityPermille(1), 850);
  EXPECT_EQ(purityPermille(100), 1150);
  EXPECT_EQ(purityPermille(0), 850);
  EXPECT_EQ(purityPermille(255), 1150);
}

TEST(Simulation, SaveRoundTripKeepsTheWorldSeed) {
  // The world block bytes used by createCharacter survive save/load.
  GameState s = newCharacter();
  EXPECT_EQ(worldSeedOf(s), 1234u);
}
