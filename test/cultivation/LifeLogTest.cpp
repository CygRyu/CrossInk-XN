#include <BuiltinPack.h>
#include <Chronicle.h>
#include <GameValues.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <HalStorage.h>
#include <LifeLog.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace cultivation;
using namespace cultivation::lifelog;

namespace {

// The built-in pack, held in memory (source 0).
struct BuiltinPacks {
  std::string text = std::string(builtinPackText(), builtinPackSize());
  std::vector<PackSection> storage = std::vector<PackSection>(384);
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;

  BuiltinPacks() {
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    PackParser parser(index, 0);
    parsePackText(text.data(), text.size(), parser);
    reader.setSource(0, text.data(), text.size());
    index.applyReplacements();
    grammar = std::make_unique<Grammar>(index, reader);
  }
};

GameState makeCharacter(const Origin origin = Origin::FarmersChild, const uint32_t pagesBefore = 0) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.gender = Gender::Female;
  d.origin = origin;
  d.destiny.element = Element::Earth;
  d.destiny.purity = 20;
  d.destiny.talent = 1;
  d.worldSeed = 0x5EED1234;
  StatsSample before;
  before.pages = pagesBefore;
  before.completedBooks = pagesBefore > 0 ? 37 : 0;
  GameState s;
  createCharacter(s, d, before);
  return s;
}

VisitChange seasons(const uint32_t fromMonths, const uint32_t toMonths) {
  VisitChange v;
  v.ageMonthsBefore = fromMonths;
  v.ageMonthsAfter = toMonths;
  return v;
}

std::string chronicleText() {
  const auto* bytes = Storage.bytesOf(chronicle::kPath);
  return bytes == nullptr ? std::string() : std::string(bytes->begin(), bytes->end());
}

size_t countOf(const std::string& haystack, const std::string& needle) {
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) ++n;
  return n;
}

}  // namespace

// --- Counting seasons ---------------------------------------------------------------

TEST(LifeLog, SeasonsAreCountedFromTheAge) {
  // Age 16 starts in spring: 192 months is season 64.
  EXPECT_EQ(seasonOf(192), 64u);
  EXPECT_EQ(seasonInYear(64), 0);
  EXPECT_EQ(ageOfSeason(64), 16u);
  EXPECT_EQ(seasonsPassed(seasons(192, 194)), 0u);  // 10 minutes: no season completed yet
  EXPECT_EQ(seasonsPassed(seasons(194, 195)), 1u);  // 5 more minutes finish the spring
  EXPECT_EQ(seasonsPassed(seasons(192, 216)), 8u);  // two hours: two years
  EXPECT_EQ(seasonsPassed(seasons(200, 200)), 0u);
}

TEST(LifeLog, ManySeasonsAreSpreadEvenly) {
  // Up to 6: every one of them.
  for (uint8_t i = 0; i < 4; ++i) EXPECT_EQ(shownSeason(64, 4, i, 4), 64u + i);
  // 11 seasons shown as 6: first and last included, in order, no repeats.
  uint32_t previous = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    const uint32_t s = shownSeason(64, 11, i, 6);
    if (i > 0) {
      EXPECT_GT(s, previous);
    }
    previous = s;
  }
  EXPECT_EQ(shownSeason(64, 11, 0, 6), 64u);
  EXPECT_EQ(shownSeason(64, 11, 5, 6), 74u);
}

// --- Building lines from the built-in pack ------------------------------------------

TEST(LifeLog, BuildsOneLabelledLinePerSeason) {
  BuiltinPacks packs;
  GameState s = makeCharacter();
  s.progress.ageMonths = 195;
  LifeLog log;
  build(log, seasons(189, 195), 6, s, packs.grammar.get());
  EXPECT_EQ(log.seasonsPassed, 2u);
  EXPECT_EQ(log.monthsPassed, 6u);
  ASSERT_EQ(log.count, 2);
  EXPECT_EQ(std::string(log.lines[0].text).rfind("Winter, age 15. ", 0), 0u) << log.lines[0].text;
  EXPECT_EQ(std::string(log.lines[1].text).rfind("Spring, age 16. ", 0), 0u) << log.lines[1].text;
}

TEST(LifeLog, LongVisitsShowSixLinesAndCountAll) {
  BuiltinPacks packs;
  GameState s = makeCharacter();
  LifeLog log;
  build(log, seasons(192, 225), 33, s, packs.grammar.get());
  EXPECT_EQ(log.seasonsPassed, 11u);
  EXPECT_EQ(log.count, kMaxSeasonLines);
}

TEST(LifeLog, SameSeasonsGiveSameLines) {
  BuiltinPacks first;
  BuiltinPacks second;
  GameState s = makeCharacter();
  LifeLog a;
  LifeLog b;
  build(a, seasons(192, 204), 12, s, first.grammar.get());
  build(b, seasons(192, 204), 12, s, second.grammar.get());
  ASSERT_EQ(a.count, b.count);
  for (uint8_t i = 0; i < a.count; ++i) EXPECT_STREQ(a.lines[i].text, b.lines[i].text);
}

TEST(LifeLog, StageAndBottleneckLinesComeLast) {
  BuiltinPacks packs;
  GameState s = makeCharacter();
  s.progress.stage = 3;
  VisitChange v = seasons(192, 195);
  v.stagesGained = 2;
  v.reachedBottleneck = true;
  LifeLog log;
  build(log, v, 3, s, packs.grammar.get());
  ASSERT_EQ(log.count, 3);
  EXPECT_EQ(log.lines[1].kind, LineKind::Stage);
  EXPECT_NE(std::string(log.lines[1].text).find("Body Tempering, Layer 4"), std::string::npos) << log.lines[1].text;
  EXPECT_EQ(log.lines[2].kind, LineKind::Bottleneck);
  EXPECT_NE(std::string(log.lines[2].text).find("Body Tempering"), std::string::npos) << log.lines[2].text;
}

TEST(LifeLog, StageLineEvenWithoutASeason) {
  BuiltinPacks packs;
  GameState s = makeCharacter();
  VisitChange v = seasons(192, 193);
  v.stagesGained = 1;
  LifeLog log;
  build(log, v, 1, s, packs.grammar.get());
  ASSERT_EQ(log.count, 1);
  EXPECT_EQ(log.lines[0].kind, LineKind::Stage);
  EXPECT_EQ(std::string(log.lines[0].text).rfind("Spring, age 16. ", 0), 0u);
}

TEST(LifeLog, WithoutTheEngineOnlyTheCountsRemain) {
  GameState s = makeCharacter();
  LifeLog log;
  build(log, seasons(192, 204), 12, s, nullptr);
  EXPECT_EQ(log.seasonsPassed, 4u);
  EXPECT_EQ(log.count, 0);
}

TEST(LifeLog, OriginsGetTheirOwnLinesInBodyTempering) {
  BuiltinPacks packs;
  GrammarContext ctx;
  for (int o = 0; o < static_cast<int>(Origin::Count); ++o) {
    GameState s = makeCharacter(static_cast<Origin>(o));
    std::set<std::string> lists;
    for (uint32_t seed = 1; seed < 400; ++seed) lists.insert(seasonListFor(s, seed, *packs.grammar, ctx));
    EXPECT_EQ(lists.size(), 3u) << o;  // origin list, log.body, log.any
    EXPECT_EQ(lists.count("log.body"), 1u);
  }
  GameState qi = makeCharacter();
  qi.progress.realm = 1;
  std::set<std::string> lists;
  for (uint32_t seed = 1; seed < 400; ++seed) lists.insert(seasonListFor(qi, seed, *packs.grammar, ctx));
  EXPECT_EQ(lists, (std::set<std::string>{"log.qi", "log.any"}));
}

// Every list the life log and the Chronicle use: complete sentences, short
// enough for the summary screen, with nothing left unexpanded.
TEST(LifeLog, BuiltinLinesExpandCleanly) {
  BuiltinPacks packs;
  std::set<std::string> distinct;
  for (int o = 0; o < static_cast<int>(Origin::Count); ++o) {
    for (uint8_t realm = 0; realm < 3; ++realm) {
      GameState s = makeCharacter(static_cast<Origin>(o), 48210);
      s.progress.realm = realm;
      for (uint32_t start = 192; start < 192 + 120; start += 3) {
        VisitChange v = seasons(start, start + 18);
        v.stagesGained = 1;
        v.reachedBottleneck = true;
        LifeLog log;
        build(log, v, 18, s, packs.grammar.get());
        EXPECT_EQ(log.count, kMaxSeasonLines + 2);
        for (uint8_t i = 0; i < log.count; ++i) {
          const std::string line = log.lines[i].text;
          EXPECT_EQ(line.find_first_of("{}[]|$\\"), std::string::npos) << line;
          EXPECT_EQ(line.back(), '.') << line;
          EXPECT_LT(line.size(), kLineSize - 1) << line;
          distinct.insert(line.substr(line.find(". ") + 2));
        }
      }
    }
  }
  EXPECT_GT(distinct.size(), 60u);
}

// --- The Chronicle ------------------------------------------------------------------

class ChronicleTest : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(ChronicleTest, OpeningHasTitleBirthAwakeningAndPastLife) {
  BuiltinPacks packs;
  const GameState s = makeCharacter(Origin::StreetOrphan, 48210);
  ASSERT_TRUE(chronicle::writeOpening(s, packs.grammar.get()));
  const std::string text = chronicleText();
  EXPECT_EQ(text.rfind("The Chronicle of Lin Yue\n\nLife 1: Lin Yue\n\n", 0), 0u) << text;
  EXPECT_NE(text.find("alleys of a market town"), std::string::npos) << text;
  EXPECT_NE(text.find("murky ochre"), std::string::npos) << text;  // purity 20, Earth
  EXPECT_NE(text.find("\"Earth root\""), std::string::npos) << text;
  EXPECT_NE(text.find("48,210 pages and finished 37 books."), std::string::npos) << text;
}

TEST_F(ChronicleTest, NoPastLifeLineWithoutEarlierReading) {
  BuiltinPacks packs;
  ASSERT_TRUE(chronicle::writeOpening(makeCharacter(), packs.grammar.get()));
  EXPECT_EQ(chronicleText().find("former life"), std::string::npos);
}

TEST_F(ChronicleTest, ANewLifeAddsAChapterWithoutANewTitle) {
  BuiltinPacks packs;
  const GameState s = makeCharacter();
  ASSERT_TRUE(chronicle::writeOpening(s, packs.grammar.get()));
  ASSERT_TRUE(chronicle::writeOpening(s, packs.grammar.get()));
  const std::string text = chronicleText();
  EXPECT_EQ(countOf(text, "The Chronicle of"), 1u);
  EXPECT_EQ(countOf(text, "Life 1: Lin Yue"), 2u);
}

TEST_F(ChronicleTest, VisitsAppendAtTheBottomWithAgeHeadings) {
  BuiltinPacks packs;
  const GameState s = makeCharacter();
  ASSERT_TRUE(chronicle::writeOpening(s, packs.grammar.get()));
  LifeLog first;
  build(first, seasons(192, 198), 6, s, packs.grammar.get());  // spring and summer at 16
  ASSERT_TRUE(chronicle::appendVisit(first));
  LifeLog second;
  build(second, seasons(198, 207), 9, s, packs.grammar.get());  // autumn, winter at 16, spring at 17
  ASSERT_TRUE(chronicle::appendVisit(second));

  const std::string text = chronicleText();
  EXPECT_EQ(countOf(text, "\nAge 16\n"), 1u) << text;  // not repeated by the second visit
  EXPECT_EQ(countOf(text, "\nAge 17\n"), 1u) << text;
  const size_t spring = text.find("Spring, age 16.");
  const size_t winter = text.find("Winter, age 16.");
  const size_t next = text.find("Spring, age 17.");
  ASSERT_NE(spring, std::string::npos);
  EXPECT_LT(spring, winter);
  EXPECT_LT(winter, next);
  EXPECT_EQ(text.back(), '\n');
}

TEST_F(ChronicleTest, ARetriedVisitIsNotWrittenTwice) {
  // The Chronicle is written before the save; if the save fails, the next
  // visit rebuilds the same lines and must not append them again.
  BuiltinPacks packs;
  const GameState s = makeCharacter();
  LifeLog log;
  build(log, seasons(192, 201), 9, s, packs.grammar.get());
  ASSERT_TRUE(chronicle::appendVisit(log));
  const std::string once = chronicleText();
  BuiltinPacks again;
  LifeLog retry;
  build(retry, seasons(192, 201), 9, s, again.grammar.get());
  ASSERT_TRUE(chronicle::appendVisit(retry));
  EXPECT_EQ(chronicleText(), once);
}

TEST_F(ChronicleTest, ALineCutShortByPowerLossDoesNotSwallowTheNextOne) {
  BuiltinPacks packs;
  const GameState s = makeCharacter();
  Storage.put(chronicle::kPath, "The Chronicle of Lin Yue\n\nAge 16\nSpring, age 16. You spend the");
  LifeLog log;
  build(log, seasons(195, 198), 3, s, packs.grammar.get());
  ASSERT_TRUE(chronicle::appendVisit(log));
  const std::string text = chronicleText();
  EXPECT_NE(text.find("You spend the\n"), std::string::npos) << text;
  EXPECT_NE(text.find("\nSummer, age 16. "), std::string::npos) << text;
}

TEST_F(ChronicleTest, EmptyVisitWritesNothing) {
  LifeLog log;
  ASSERT_TRUE(chronicle::appendVisit(log));
  EXPECT_FALSE(chronicle::exists());
}

TEST(Chronicle, ReadsTheAgeOfASeasonLine) {
  EXPECT_EQ(chronicle::ageInLine("Spring, age 17. You do things."), 17);
  EXPECT_EQ(chronicle::ageInLine("Winter, age 120. Cold."), 120);
  EXPECT_EQ(chronicle::ageInLine("Age 17"), -1);
  EXPECT_EQ(chronicle::ageInLine("Life 1: Lin Yue"), -1);
  EXPECT_EQ(chronicle::ageInLine(""), -1);
}

// --- Game values used by the new lists ----------------------------------------------

TEST(GameValues, PastLifeCountsReadNaturally) {
  GameState s = makeCharacter(Origin::FarmersChild, 48210);
  char out[48];
  ASSERT_TRUE(lookupGameValue(s, "past.pages", 10, out, sizeof(out)));
  EXPECT_STREQ(out, "48,210 pages");
  ASSERT_TRUE(lookupGameValue(s, "past.books", 10, out, sizeof(out)));
  EXPECT_STREQ(out, "37 books");
  s.baseline.completedBooks = 1;
  ASSERT_TRUE(lookupGameValue(s, "past.books", 10, out, sizeof(out)));
  EXPECT_STREQ(out, "1 book");
  s.baseline.seconds = 0;
  ASSERT_TRUE(lookupGameValue(s, "past.hours", 10, out, sizeof(out)));
  EXPECT_STREQ(out, "no hours");
  s.baseline.pages = 1234567;
  ASSERT_TRUE(lookupGameValue(s, "past.pages", 10, out, sizeof(out)));
  EXPECT_STREQ(out, "1,234,567 pages");
  ASSERT_TRUE(lookupGameValue(s, "season", 6, out, sizeof(out)));
  EXPECT_STREQ(out, "spring");
  EXPECT_TRUE(isGameValueName("root.color", 10));
}
