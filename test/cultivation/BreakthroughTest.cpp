#include <Breakthrough.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>

using namespace cultivation;
using namespace cultivation::breakthrough;

namespace {

GameState makeCharacter(const uint32_t worldSeed = 0x5EED1234, const Origin origin = Origin::FarmersChild) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.origin = origin;
  d.destiny.purity = 50;
  d.destiny.talent = 2;  // Ordinary Root x1.0
  d.worldSeed = worldSeed;
  GameState s;
  createCharacter(s, d, StatsSample{});
  return s;
}

// A full Peak bar in `realm`.
GameState atPeak(const uint8_t realm, const uint32_t worldSeed = 0x5EED1234,
                 const Origin origin = Origin::FarmersChild) {
  GameState s = makeCharacter(worldSeed, origin);
  s.progress.realm = realm;
  s.progress.stage = static_cast<uint8_t>(realmInfo(realm).stages - 1);
  s.progress.qi = stageCost(realm, s.progress.stage);
  return s;
}

VisitGains minutes(const uint32_t m) {
  VisitGains g;
  g.seconds = m * 60;
  g.tiers.fullSeconds = m * 60;
  return g;
}

}  // namespace

// --- Chance -------------------------------------------------------------------------

TEST(Breakthrough, BaseChancesFollowTheDoc) {
  const uint8_t expected[] = {0, 90, 70, 45, 35, 30, 25, 20, 20, 20};
  for (uint8_t realm = 0; realm + 1 < kMortalRealmCount; ++realm) {
    GameState s = atPeak(realm);
    s.progress.daoHeart = 0;
    EXPECT_EQ(chanceFor(s).base, expected[realm + 1]) << int(realm);
    EXPECT_EQ(chanceFor(s).total, expected[realm + 1]) << int(realm);
  }
}

TEST(Breakthrough, DaoHeartAndConsolidationAddUpToFifteenEach) {
  GameState s = atPeak(1);
  s.progress.daoHeart = 100;
  s.progress.consolidation = 40;
  const Chance c = chanceFor(s);
  EXPECT_EQ(c.daoHeart, 15);
  EXPECT_EQ(c.consolidation, 15);
  EXPECT_EQ(c.total, kChanceCap);  // 70 + 15 + 15, capped at 99
  s.progress.daoHeart = 50;
  s.progress.consolidation = 20;
  EXPECT_EQ(chanceFor(s).total, 70 + 7 + 7);
}

TEST(Breakthrough, ChanceNeverReachesCertaintyWithoutPity) {
  GameState s = atPeak(0);
  s.progress.daoHeart = 100;
  EXPECT_EQ(chanceFor(s).total, kChanceCap);  // 90 + 15 capped at 99
}

TEST(Breakthrough, PityAddsFifteenPerFailureUpToNinety) {
  GameState s = atPeak(2);  // into Core Formation: 45
  s.progress.daoHeart = 0;
  s.progress.failures = 1;
  EXPECT_EQ(chanceFor(s).pity, 15);
  EXPECT_EQ(chanceFor(s).total, 60);
  s.progress.failures = 3;
  EXPECT_EQ(chanceFor(s).total, 90);
  s.progress.failures = 9;
  EXPECT_EQ(chanceFor(s).total, 90);
  // Pity never lowers a chance that was already higher.
  GameState high = atPeak(0);
  high.progress.daoHeart = 100;
  high.progress.failures = 2;
  EXPECT_EQ(chanceFor(high).total, kChanceCap);
  EXPECT_EQ(chanceFor(high).pity, 0);
}

// --- Keys ---------------------------------------------------------------------------

TEST(Breakthrough, OnlyTheSmallBottleneckNeedsNoKey) {
  EXPECT_FALSE(chanceFor(atPeak(0)).needsKey);
  EXPECT_TRUE(canAttempt(atPeak(0)));
  for (uint8_t realm = 1; realm + 1 < kMortalRealmCount; ++realm) {
    EXPECT_TRUE(chanceFor(atPeak(realm)).needsKey) << int(realm);
    EXPECT_FALSE(canAttempt(atPeak(realm))) << int(realm);  // an empty library: no key
  }
}

namespace {
// A scroll with `hours` read in its book, put straight into the library.
scrolls::Scroll addScroll(GameState& s, const uint8_t hours, const uint32_t hash,
                          const scrolls::Kind kind = scrolls::Kind::Sword, const uint8_t comprehension = 90) {
  scrolls::Scroll sc;
  sc.titleHash = hash;
  sc.hours = hours;
  sc.kind = kind;
  sc.comprehension = comprehension;
  const uint8_t n = scrolls::count(s);
  scrolls::put(s, n, sc);
  s.techniqueScrolls[0] = static_cast<uint8_t>(n + 1);
  return sc;
}
}  // namespace

TEST(Breakthrough, KeysFollowTheTierTable) {
  // Foundation: any scroll. Core: Yellow (3 h). Nascent Soul: Earth (8 h). Later: Heaven (15 h).
  const uint8_t hoursNeeded[] = {0, 0, 3, 8, 15, 15, 15, 15, 15};  // by the realm the player is in
  for (uint8_t realm = 1; realm + 1 < kMortalRealmCount; ++realm) {
    GameState s = atPeak(realm);
    if (hoursNeeded[realm] > 0) {
      addScroll(s, static_cast<uint8_t>(hoursNeeded[realm] - 1), 11);
      EXPECT_FALSE(canAttempt(s)) << int(realm);
    }
    addScroll(s, hoursNeeded[realm], 12);
    EXPECT_TRUE(canAttempt(s)) << int(realm);
    EXPECT_EQ(scrolls::get(s, static_cast<uint8_t>(chanceFor(s).keyIndex)).titleHash, 12u) << int(realm);
  }
}

TEST(Breakthrough, TheKeyIsTheLowestQualifyingUnequippedScroll) {
  GameState s = atPeak(2);  // into Core Formation: Yellow or better
  addScroll(s, 20, 1);      // Heaven
  addScroll(s, 1, 2);       // Mortal: does not qualify
  addScroll(s, 5, 3, scrolls::Kind::Sword, 95);
  addScroll(s, 4, 4, scrolls::Kind::Sword, 80);  // Yellow, lower comprehension: this one
  addScroll(s, 6, 5, scrolls::Kind::Sword, 80);  // same strength, newer
  EXPECT_EQ(scrolls::get(s, static_cast<uint8_t>(chanceFor(s).keyIndex)).titleHash, 4u);
  ASSERT_TRUE(scrolls::equip(s, 3));  // equipped scrolls are never spent
  EXPECT_EQ(scrolls::get(s, static_cast<uint8_t>(chanceFor(s).keyIndex)).titleHash, 5u);
}

TEST(Breakthrough, TheKeyIsSpentOnlyOnSuccess) {
  bool sawFailure = false;
  bool sawSuccess = false;
  for (uint32_t seed = 1; seed < 80 && !(sawFailure && sawSuccess); ++seed) {
    GameState s = atPeak(1, seed);  // into Foundation: 70%
    addScroll(s, 2, 77);
    const Result r = attempt(s);
    ASSERT_TRUE(r.attempted);
    if (r.success) {
      sawSuccess = true;
      EXPECT_TRUE(r.keyUsed);
      EXPECT_EQ(r.key.titleHash, 77u);
      EXPECT_EQ(scrolls::count(s), 0);
    } else {
      sawFailure = true;
      EXPECT_FALSE(r.keyUsed);
      EXPECT_EQ(scrolls::count(s), 1);
    }
  }
  EXPECT_TRUE(sawFailure);
  EXPECT_TRUE(sawSuccess);
}

TEST(Breakthrough, NoKeyIsSpentIntoQiCondensation) {
  for (uint32_t seed = 1; seed < 50; ++seed) {
    GameState s = atPeak(0, seed);
    addScroll(s, 2, 9);
    const Result r = attempt(s);
    if (!r.success) continue;
    EXPECT_FALSE(r.keyUsed);
    EXPECT_EQ(scrolls::count(s), 1);
    return;
  }
  FAIL();
}

TEST(Breakthrough, AManualOfTheRootsElementAddsChanceAndGradeScore) {
  GameState s = atPeak(1);
  s.progress.daoHeart = 0;
  s.character.rootElement = Element::Water;
  scrolls::Scroll m = addScroll(s, 9, 5, scrolls::Kind::Manual);
  m.element = Element::Fire;
  scrolls::put(s, 0, m);
  ASSERT_TRUE(scrolls::equip(s, 0));
  EXPECT_EQ(chanceFor(s).element, 0);
  EXPECT_EQ(gradeScore(s, 2), 0);
  m = scrolls::get(s, 0);
  m.element = Element::Water;
  scrolls::put(s, 0, m);
  EXPECT_EQ(chanceFor(s).element, 5);
  EXPECT_EQ(chanceFor(s).total, 75);
  EXPECT_EQ(gradeScore(s, 2), 15);
}

TEST(Breakthrough, NeedsAFullPeakBar) {
  GameState s = atPeak(0);
  s.progress.qi = QiNumber::fromU64(1);
  EXPECT_FALSE(canAttempt(s));
  EXPECT_FALSE(attempt(s).attempted);
  s.progress.stage = 3;
  s.progress.qi = stageCost(0, 3);
  EXPECT_FALSE(canAttempt(s));
}

// --- Attempts -----------------------------------------------------------------------

TEST(Breakthrough, SuccessEntersTheNextRealmAtItsFirstStage) {
  // Find a world whose first roll succeeds (90% for most).
  for (uint32_t seed = 1; seed < 50; ++seed) {
    GameState s = atPeak(0, seed);
    s.progress.failures = 0;
    const Result r = attempt(s);
    ASSERT_TRUE(r.attempted);
    if (!r.success) continue;
    EXPECT_LT(r.roll, r.chance);
    EXPECT_EQ(s.progress.realm, 1);
    EXPECT_EQ(s.progress.stage, 0);
    EXPECT_TRUE(s.progress.qi.isZero());
    EXPECT_EQ(s.progress.failures, 0);
    EXPECT_FALSE(isHalfStep(s));
    EXPECT_EQ(r.gradeKind, GradeKind::None);  // grades start with Foundation
    return;
  }
  FAIL() << "no successful roll in 49 worlds";
}

TEST(Breakthrough, FailureCostsAQuarterOfTheBarAndGivesPity) {
  for (uint32_t seed = 1; seed < 500; ++seed) {
    GameState s = atPeak(0, seed);
    const Result r = attempt(s);
    if (r.success) continue;
    EXPECT_GE(r.roll, r.chance);
    EXPECT_EQ(s.progress.realm, 0);
    EXPECT_EQ(s.progress.stage, 8);  // never a stage
    QiNumber left = stageCost(0, 8);
    left.mulRatio(3, 4);
    EXPECT_EQ(s.progress.qi, left);
    EXPECT_EQ(s.progress.failures, 1);
    EXPECT_TRUE(isHalfStep(s));
    EXPECT_FALSE(canAttempt(s));  // the bar has to refill first
    return;
  }
  FAIL() << "no failed roll in 499 worlds";
}

TEST(Breakthrough, TheSameAttemptAlwaysRollsTheSame) {
  for (uint32_t seed = 1; seed < 40; ++seed) {
    GameState a = atPeak(0, seed);
    GameState b = atPeak(0, seed);
    EXPECT_EQ(attempt(a).roll, attempt(b).roll);
  }
}

TEST(Breakthrough, AFailureChangesTheNextRoll) {
  // The roll is seeded by the failure count, so a retry is a new roll.
  int differs = 0;
  for (uint32_t seed = 1; seed < 40; ++seed) {
    GameState first = atPeak(0, seed);
    GameState second = atPeak(0, seed);
    second.progress.failures = 1;
    if (attempt(first).roll != attempt(second).roll) ++differs;
  }
  EXPECT_GT(differs, 30);
}

TEST(Breakthrough, SuccessRateMatchesTheChance) {
  int successes = 0;
  for (uint32_t seed = 1; seed <= 2000; ++seed) {
    GameState s = atPeak(0, seed);
    s.progress.daoHeart = 0;  // 90%
    if (attempt(s).success) ++successes;
  }
  EXPECT_NEAR(successes, 1800, 60);
}

// --- Great Perfection ---------------------------------------------------------------

TEST(Breakthrough, GreatPerfectionStartsFromQiCondensation) {
  EXPECT_FALSE(canEnterGreatPerfection(atPeak(0)));  // Body Tempering has none
  GameState s = atPeak(1);
  EXPECT_TRUE(canEnterGreatPerfection(s));
  enterGreatPerfection(s);
  EXPECT_TRUE(inGreatPerfection(s));
  EXPECT_FALSE(canEnterGreatPerfection(s));
  GameState notFull = atPeak(1);
  notFull.progress.qi = QiNumber::fromU64(5);
  EXPECT_FALSE(canEnterGreatPerfection(notFull));
}

TEST(Breakthrough, ReadingInGreatPerfectionConsolidatesTwoPointsAnHour) {
  GameState s = atPeak(1);
  enterGreatPerfection(s);
  const AbsorbResult r = absorb(s, minutes(90));
  EXPECT_EQ(r.consolidationGained, 3);
  EXPECT_EQ(s.progress.consolidation, 3);
  absorb(s, minutes(20));
  absorb(s, minutes(20));  // 40 min in two visits: one more step, across the visits
  EXPECT_EQ(s.progress.consolidation, 4);
  absorb(s, minutes(60 * 30));
  EXPECT_EQ(s.progress.consolidation, kConsolidationMax);
  EXPECT_TRUE(atBottleneck(s));  // the bar stays full
}

TEST(Breakthrough, NoConsolidationOutsideGreatPerfection) {
  GameState s = atPeak(1);
  absorb(s, minutes(120));
  EXPECT_EQ(s.progress.consolidation, 0);
}

TEST(Breakthrough, AFailureInGreatPerfectionKeepsIt) {
  for (uint32_t seed = 1; seed < 500; ++seed) {
    GameState s = atPeak(0, seed);
    // Body Tempering has no Great Perfection; set the flag directly to test the rule.
    s.progress.progressFlags |= ProgressBlock::kInGreatPerfection;
    s.progress.consolidation = 12;
    if (attempt(s).success) continue;
    EXPECT_TRUE(inGreatPerfection(s));
    EXPECT_EQ(s.progress.consolidation, 12);
    return;
  }
  FAIL();
}

// --- Grades -------------------------------------------------------------------------

TEST(Breakthrough, GradeBandsFollowTheDoc) {
  EXPECT_EQ(foundationGradeFor(0), 1);
  EXPECT_EQ(foundationGradeFor(19), 1);
  EXPECT_EQ(foundationGradeFor(20), 2);
  EXPECT_EQ(foundationGradeFor(40), 3);
  EXPECT_EQ(foundationGradeFor(69), 3);
  EXPECT_EQ(foundationGradeFor(70), 4);
  EXPECT_EQ(foundationGradeFor(90), 5);
  EXPECT_EQ(coreGradeFor(0), 1);   // 9th grade
  EXPECT_EQ(coreGradeFor(85), 9);  // 1st grade
  EXPECT_EQ(coreGradeFor(94), 9);
  EXPECT_EQ(coreGradeFor(95), 10);  // Flawless
  EXPECT_EQ(soulClarityFor(39), 1);
  EXPECT_EQ(soulClarityFor(40), 2);
  EXPECT_EQ(soulClarityFor(70), 3);
  EXPECT_EQ(soulClarityFor(90), 4);
}

TEST(Breakthrough, GradeScoreAddsConsolidationDaoHeartAndTheClanHeirBonus) {
  GameState s = atPeak(1);
  s.progress.consolidation = 40;
  s.progress.daoHeart = 100;
  EXPECT_EQ(gradeScore(s, 2), 65);
  GameState heir = atPeak(1, 7, Origin::FallenClanHeir);
  heir.progress.consolidation = 40;
  heir.progress.daoHeart = 100;
  EXPECT_EQ(gradeScore(heir, 2), 70);  // Perfect Foundation
  EXPECT_EQ(gradeScore(heir, 3), 65);  // the bonus is for the Foundation grade only
}

TEST(Breakthrough, GradesChangeTheQiRate) {
  GameState s = makeCharacter();
  const uint32_t plain = qiMultiplierPermille(s);
  s.progress.foundationGrade = 1;  // Cracked: -20%
  EXPECT_EQ(qiMultiplierPermille(s), plain * 800 / 1000);
  s.progress.foundationGrade = 5;  // Heavenly Dao: +50%
  EXPECT_EQ(qiMultiplierPermille(s), plain * 1500 / 1000);
  s.progress.foundationGrade = 3;
  s.progress.coreGrade = 10;  // Flawless: +40%
  EXPECT_EQ(qiMultiplierPermille(s), plain * 1400 / 1000);
  s.progress.coreGrade = 1;  // 9th grade: +0%
  EXPECT_EQ(qiMultiplierPermille(s), plain);
}

TEST(Breakthrough, FoundationGradeFromTypicalNumbers) {
  // 30 consolidation + 60 Dao Heart (15) = 45.
  GameState s = atPeak(1);
  s.progress.consolidation = 30;
  s.progress.daoHeart = 60;
  EXPECT_EQ(gradeScore(s, 2), 45);
  EXPECT_EQ(foundationGradeFor(gradeScore(s, 2)), 3);  // Standard
}
