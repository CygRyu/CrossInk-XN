#include <gtest/gtest.h>

#include <cstring>

#include "CultivationEngine.h"

using namespace Cultivation;

TEST(CultivationEngine, QiFormulaWeightsEachReadingStat) {
  ReadingInputs in;
  EXPECT_EQ(qiFromReading(in), 0U);
  in.pages = 100;         // 500
  in.seconds = 3600;      // 60 minutes -> 180
  in.books = 1;           // 600
  in.longestStreak = 4;   // 100
  EXPECT_EQ(qiFromReading(in), 500U + 180U + 600U + 100U);
}

TEST(CultivationEngine, QiFormulaSaturatesInsteadOfWrapping) {
  ReadingInputs in;
  in.pages = 0xFFFFFFFFu;
  in.seconds = 0xFFFFFFFFu;
  in.books = 0xFFFFFFFFu;
  EXPECT_EQ(qiFromReading(in), 0xFFFFFFFFu);
}

TEST(CultivationEngine, ProgressWalksLayersThenRealms) {
  Progress p = progressFor(0);
  EXPECT_EQ(p.realm, 0);
  EXPECT_EQ(p.layer, 1);
  EXPECT_EQ(p.qiForLayer, 100U);

  p = progressFor(100);
  EXPECT_EQ(p.layer, 2);
  EXPECT_EQ(p.qiIntoLayer, 0U);

  p = progressFor(899);
  EXPECT_EQ(p.realm, 0);
  EXPECT_EQ(p.layer, 9);

  p = progressFor(900);
  EXPECT_EQ(p.realm, 1);
  EXPECT_EQ(p.layer, 1);
  EXPECT_EQ(p.qiForLayer, 400U);
}

TEST(CultivationEngine, ProgressMaxesOutInsteadOfOverflowing) {
  const Progress p = progressFor(0xFFFFFFFFu);
  EXPECT_TRUE(p.maxed);
  EXPECT_EQ(p.realm, kRealmCount - 1);
  EXPECT_EQ(p.layer, kLayersPerRealm);
}

TEST(CultivationEngine, StageIndexIsMonotonic) {
  uint16_t last = 0;
  for (uint32_t qi = 0; qi < 700000; qi += 250) {
    const uint16_t s = stageIndex(progressFor(qi));
    EXPECT_GE(s, last);
    last = s;
  }
}

TEST(CultivationEngine, LedgerBanksOnlyIncreases) {
  Ledger l;
  EXPECT_TRUE(applyLedger(l, 500));
  EXPECT_EQ(l.banked, 500U);
  EXPECT_FALSE(applyLedger(l, 500));
  EXPECT_TRUE(applyLedger(l, 800));
  EXPECT_EQ(l.banked, 800U);
}

TEST(CultivationEngine, LedgerSurvivesStatsReset) {
  Ledger l;
  applyLedger(l, 5000);
  EXPECT_TRUE(applyLedger(l, 0));  // reading stats were reset
  EXPECT_EQ(l.banked, 5000U);      // character keeps their Qi
  EXPECT_EQ(l.lastDerived, 0U);
  applyLedger(l, 300);             // new reading counts again
  EXPECT_EQ(l.banked, 5300U);
}

TEST(CultivationEngine, LedgerSaturates) {
  Ledger l;
  l.banked = 0xFFFFFFF0u;
  applyLedger(l, 100);
  EXPECT_EQ(l.banked, 0xFFFFFFFFu);
}

TEST(CultivationEngine, PathChoiceUnlocksAtFoundationEstablishment) {
  EXPECT_FALSE(pathChoiceAvailable(progressFor(899)));
  EXPECT_TRUE(pathChoiceAvailable(progressFor(900)));
}

TEST(CultivationEngine, AttributesAndPowerGrowWithReading) {
  ReadingInputs a;
  ReadingInputs b;
  b.pages = 500;
  b.seconds = 7200 * 5;
  b.books = 2;
  b.longestStreak = 7;
  const Attributes attrB = attributesFor(b);
  EXPECT_EQ(attrB.body, 10U);
  EXPECT_EQ(attrB.insight, 20U);
  EXPECT_EQ(attrB.resolve, 7U);
  EXPECT_EQ(attrB.fortune, 2U);
  EXPECT_GT(combatPower(progressFor(qiFromReading(b)), attrB), combatPower(progressFor(0), attributesFor(a)));
}

TEST(CultivationEngine, CharacterNameIsDeterministicAndSeedDependent) {
  char a[32], b[32], c[32];
  characterName(12345, a, sizeof(a));
  characterName(12345, b, sizeof(b));
  EXPECT_STREQ(a, b);
  EXPECT_GT(std::strlen(a), 3U);
  bool differs = false;
  for (uint32_t s = 1; s < 50 && !differs; ++s) {
    characterName(s, c, sizeof(c));
    differs = std::strcmp(a, c) != 0;
  }
  EXPECT_TRUE(differs);
}

TEST(CultivationEngine, JournalIsEmptyWithoutProgress) {
  JournalEntry out[kMaxJournalEntries];
  EXPECT_EQ(generateJournal(1, Path::Undecided, 100, 100, out, kMaxJournalEntries), 0U);
  EXPECT_EQ(generateJournal(1, Path::Undecided, 200, 100, out, kMaxJournalEntries), 0U);
  EXPECT_EQ(generateJournal(1, Path::Undecided, 0, 50, out, 0), 0U);
}

TEST(CultivationEngine, JournalIsDeterministic) {
  JournalEntry a[kMaxJournalEntries];
  JournalEntry b[kMaxJournalEntries];
  const size_t na = generateJournal(777, Path::Demonic, 1000, 1800, a, kMaxJournalEntries);
  const size_t nb = generateJournal(777, Path::Demonic, 1000, 1800, b, kMaxJournalEntries);
  ASSERT_EQ(na, nb);
  ASSERT_GT(na, 0U);
  for (size_t i = 0; i < na; ++i) EXPECT_STREQ(a[i].text, b[i].text);
}

TEST(CultivationEngine, JournalListsBreakthroughFirst) {
  JournalEntry out[kMaxJournalEntries];
  const size_t n = generateJournal(5, Path::Undecided, 50, 150, out, kMaxJournalEntries);  // crosses layer 1 -> 2
  ASSERT_GE(n, 2U);
  EXPECT_NE(std::strstr(out[0].text, "Breakthrough"), nullptr);
  EXPECT_NE(std::strstr(out[0].text, "layer 2"), nullptr);
}

TEST(CultivationEngine, JournalAnnouncesNewRealm) {
  JournalEntry out[kMaxJournalEntries];
  const size_t n = generateJournal(5, Path::Undecided, 850, 950, out, kMaxJournalEntries);
  ASSERT_GE(n, 1U);
  bool found = false;
  for (size_t i = 0; i < n; ++i) found |= std::strstr(out[i].text, "Foundation Establishment realm") != nullptr;
  EXPECT_TRUE(found);
}

TEST(CultivationEngine, JournalSummarisesHugeJumpsAndStaysBounded) {
  JournalEntry out[kMaxJournalEntries];
  const size_t n = generateJournal(9, Path::Rogue, 0, 600000, out, kMaxJournalEntries);
  EXPECT_LE(n, kMaxJournalEntries);
  EXPECT_NE(std::strstr(out[0].text, "long retreat"), nullptr);
}

TEST(CultivationEngine, JournalTextAlwaysTerminatedAndNonEmpty) {
  JournalEntry out[kMaxJournalEntries];
  for (uint32_t seed = 1; seed < 200; ++seed) {
    for (uint8_t p = 0; p <= 3; ++p) {
      const size_t n = generateJournal(seed, static_cast<Path>(p), seed * 37, seed * 37 + 40000, out,
                                       kMaxJournalEntries);
      for (size_t i = 0; i < n; ++i) {
        const size_t len = strnlen(out[i].text, sizeof(out[i].text));
        EXPECT_LT(len, sizeof(out[i].text));
        EXPECT_GT(len, 10U);
      }
    }
  }
}

TEST(CultivationEngine, PathChangesDuelFlavour) {
  bool differs = false;
  for (uint32_t seed = 1; seed < 100 && !differs; ++seed) {
    JournalEntry a[kMaxJournalEntries];
    JournalEntry b[kMaxJournalEntries];
    const size_t na = generateJournal(seed, Path::Orthodox, 10, 60, a, kMaxJournalEntries);
    const size_t nb = generateJournal(seed, Path::Demonic, 10, 60, b, kMaxJournalEntries);
    if (na != nb) { differs = true; break; }
    for (size_t i = 0; i < na; ++i) differs |= std::strcmp(a[i].text, b[i].text) != 0;
  }
  EXPECT_TRUE(differs);
}
