#include <BuiltinPack.h>
#include <GameValues.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <PackCheck.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace cultivation;

namespace {

// Packs held in memory: source 0, 1, ... in the order added.
struct Packs {
  std::vector<std::string> texts;
  std::vector<PackSection> storage = std::vector<PackSection>(256);
  PackIndex index;
  MemoryPackReader reader;

  explicit Packs(std::initializer_list<const char*> sources) {
    for (const char* s : sources) texts.emplace_back(s);
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    for (size_t i = 0; i < texts.size(); ++i) {
      PackParser parser(index, static_cast<uint8_t>(i));
      parsePackText(texts[i].data(), texts[i].size(), parser);
      reader.setSource(static_cast<uint8_t>(i), texts[i].data(), texts[i].size());
    }
    index.applyReplacements();
  }
};

std::string expand(Grammar& g, const char* text, uint32_t seed = 1, const GrammarContext& ctx = GrammarContext{}) {
  return g.expand(text, seed, ctx);
}

}  // namespace

// --- Pack parsing -----------------------------------------------------------------

TEST(GrammarPack, ParsesListsSkippingBlanksAndComments) {
  Packs p({"# header comment\n[colors]\nred\n\n# not an entry\n  green  \r\nblue\n[empty]\n"});
  ASSERT_EQ(p.index.count, 2);
  EXPECT_EQ(p.index.entryCount(packKeyHash("colors")), 3u);
  EXPECT_EQ(p.index.entryCount(packKeyHash("empty")), 0u);
  char buf[32];
  ASSERT_TRUE(p.reader.readEntry(p.index.sections[0], 1, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "green");  // trimmed, \r removed
  ASSERT_TRUE(p.reader.readEntry(p.index.sections[0], 2, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "blue");
  EXPECT_FALSE(p.reader.readEntry(p.index.sections[0], 3, buf, sizeof(buf)));
}

TEST(GrammarPack, InlineChoiceLinesAreEntriesNotHeaders) {
  Packs p({"[town]\n[Willow|Stone] Crossing\n[dawn|dusk]\n"});
  EXPECT_EQ(p.index.count, 1);
  EXPECT_EQ(p.index.entryCount(packKeyHash("town")), 2u);
}

TEST(GrammarPack, BadHeadersAndOrphanLinesAreCounted) {
  std::string text = "orphan line\n[Bad Key]\nx\n[UPPER]\ny\n[good]\nz\n";
  std::vector<PackSection> storage(8);
  PackIndex index{storage.data(), 0, 8, false};
  PackParser parser(index, 0);
  parsePackText(text.data(), text.size(), parser);
  EXPECT_EQ(parser.badHeaders(), 1u);  // "[UPPER]"; "[Bad Key]" has a space, so it reads as an entry
  EXPECT_EQ(parser.firstBadHeaderLine(), 4u);
  EXPECT_EQ(parser.linesOutsideSections(), 4u);  // "orphan line", "[Bad Key]", "x", and "y" after the bad header
  EXPECT_EQ(index.count, 1);
  EXPECT_EQ(index.entryCount(packKeyHash("good")), 1u);
}

TEST(GrammarPack, SdPacksExtendAndReplace) {
  Packs p({"[a]\nbuilt1\nbuilt2\n[b]\nbuilt\n", "[a]\nsd1\n[!b]\nsd-only\n"});
  EXPECT_EQ(p.index.entryCount(packKeyHash("a")), 3u);  // extended
  EXPECT_EQ(p.index.entryCount(packKeyHash("b")), 1u);  // replaced
  uint16_t at = 0;
  const PackSection* s = p.index.locate(packKeyHash("a"), 2, at);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->source, 1);
  EXPECT_EQ(at, 0);
}

TEST(GrammarPack, EventsAreFlagged) {
  Packs p({"[event.ruins]\n@weight 6\nText.\n"});
  ASSERT_EQ(p.index.count, 1);
  EXPECT_NE(p.index.sections[0].flags & PackSection::kEvent, 0);
  EXPECT_EQ(p.index.sections[0].count, 2);
}

TEST(GrammarPack, IndexOverflowIsReportedNotWritten) {
  std::string text = "[a]\n1\n[b]\n2\n[c]\n3\n";
  std::vector<PackSection> storage(2);
  PackIndex index{storage.data(), 0, 2, false};
  PackParser parser(index, 0);
  parsePackText(text.data(), text.size(), parser);
  EXPECT_EQ(index.count, 2);
  EXPECT_TRUE(index.overflowed);
}

// --- Expansion --------------------------------------------------------------------

TEST(Grammar, PlainTextAndEscapes) {
  Packs p({""});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "Nothing to expand."), "Nothing to expand.");
  EXPECT_EQ(expand(g, "Braces \\{like this\\} and \\[these\\]."), "Braces {like this} and [these].");
}

TEST(Grammar, PicksFromListsDeterministically) {
  Packs p({"[c]\nred\ngreen\nblue\n"});
  Grammar a(p.index, p.reader);
  Grammar b(p.index, p.reader);
  std::set<std::string> seen;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    const std::string x = expand(a, "{c}", seed);
    EXPECT_EQ(x, expand(b, "{c}", seed));
    seen.insert(x);
  }
  EXPECT_EQ(seen, (std::set<std::string>{"red", "green", "blue"}));
}

TEST(Grammar, InlineChoicesNest) {
  Packs p({""});
  Grammar g(p.index, p.reader);
  std::set<std::string> seen;
  for (uint32_t seed = 1; seed <= 60; ++seed) seen.insert(expand(g, "[a|b[x|y]|c]", seed));
  EXPECT_EQ(seen, (std::set<std::string>{"a", "bx", "by", "c"}));
}

TEST(Grammar, EntriesExpandRecursively) {
  Packs p({"[place]\n{adj} Peak\n[adj]\nThunder\n"});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "Atop {place}."), "Atop Thunder Peak.");
}

TEST(Grammar, Modifiers) {
  Packs p(
      {"[ox]\nox\n[fox]\nfox\n[hour]\nhour\n[uni]\nunicorn\n[city]\ncity\n[sword]\nsword\n[name]\nlin "
       "yue\n[shout]\nHELLO\n[the]\nthe river\n"});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "{ox.a}"), "an ox");
  EXPECT_EQ(expand(g, "{fox.a}"), "a fox");
  EXPECT_EQ(expand(g, "{hour.a}"), "an hour");
  EXPECT_EQ(expand(g, "{uni.a}"), "a unicorn");
  EXPECT_EQ(expand(g, "{ox.a.cap} walks."), "An ox walks.");
  EXPECT_EQ(expand(g, "{fox.s}, {city.s}, {sword.s}"), "foxes, cities, swords");
  EXPECT_EQ(expand(g, "{name.title}"), "Lin Yue");
  EXPECT_EQ(expand(g, "{name.cap}"), "Lin yue");
  EXPECT_EQ(expand(g, "{shout.lower}"), "hello");
  EXPECT_EQ(expand(g, "{fox.the}, {the.the}"), "the fox, the river");
}

TEST(Grammar, KeysWithDotsAreNotModifiers) {
  Packs p({"[place.region]\nthe Ember Wastes\n"});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "{place.region}"), "the Ember Wastes");
  EXPECT_EQ(expand(g, "{place.region.cap}"), "The Ember Wastes");
}

TEST(Grammar, StoredVariablesLastUntilTheSceneEnds) {
  Packs p({"[where]\nthe Mistveil Marshes\nthe Ember Wastes\nthe Salt Flats\n"});
  Grammar g(p.index, p.reader);
  g.beginScene();
  const std::string first = expand(g, "Crossing {where>w}, again {$w}.", 5);
  const size_t comma = first.find(',');
  const std::string picked = first.substr(9, comma - 9);
  EXPECT_EQ(first, "Crossing " + picked + ", again " + picked + ".");
  EXPECT_EQ(expand(g, "You bow to {$w}.", 99), "You bow to " + picked + ".");  // a later choice line
  g.beginScene();
  EXPECT_EQ(expand(g, "Gone: {$w}."), "Gone:.");
  EXPECT_EQ(g.missing(), 1);
}

TEST(Grammar, GameValuesAndPronouns) {
  CharacterDraft d;
  strcpy(d.name, "Su Lan");
  d.gender = Gender::Female;
  GameState s;
  createCharacter(s, d, StatsSample{});
  Packs p({""});
  Grammar g(p.index, p.reader);
  GrammarContext ctx;
  ctx.values = gameValues(s);
  EXPECT_EQ(expand(g, "{$player.name}: {$pro.they.cap} {$pro.is} {$title.senior}.", 1, ctx),
            "Su Lan: She is Senior Sister.");
  EXPECT_EQ(expand(g, "{$player.realm}, {$player.stage}, age {$player.age}.", 1, ctx),
            "Body Tempering, Layer 1, age 16.");
  s.character.gender = Gender::NonBinary;
  EXPECT_EQ(expand(g, "{$pro.they.cap} {$pro.is} {$title.god}.", 1, ctx), "They are Godsovereign.");
  s.character.gender = Gender::Male;
  EXPECT_EQ(expand(g, "{$pro.they.cap} {$pro.was} {$title.sect}.", 1, ctx), "He was Sect Master.");
}

TEST(Grammar, PlaneAndThemeLayers) {
  Packs p({"[sky]\nmortal sky\n[sky@1]\nimmortal sky\n[sky~demonic]\nblood sky\n[ground]\nearth\n"});
  Grammar g(p.index, p.reader);
  GrammarContext ctx;
  EXPECT_EQ(expand(g, "{sky}", 1, ctx), "mortal sky");
  ctx.plane = 1;
  EXPECT_EQ(expand(g, "{sky}, {ground}", 1, ctx), "immortal sky, earth");  // falls back when no layer
  ctx.theme = "demonic";
  EXPECT_EQ(expand(g, "{sky}", 1, ctx), "blood sky");
  EXPECT_EQ(expand(g, "{sky@1}", 1, ctx), "immortal sky");  // explicit layer
}

TEST(Grammar, AntiRepetitionAvoidsRecentPicks) {
  // 10 entries remember 6 picks, so any 7 picks in a row are all different.
  Packs p({"[n]\n0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n"});
  Grammar g(p.index, p.reader);
  std::vector<std::string> picks;
  for (uint32_t i = 0; i < 200; ++i) picks.push_back(expand(g, "{n}", 1000 + i));
  for (size_t i = 0; i + 7 <= picks.size(); ++i) {
    std::set<std::string> window(picks.begin() + static_cast<long>(i), picks.begin() + static_cast<long>(i) + 7);
    EXPECT_EQ(window.size(), 7u) << "at " << i;
  }
}

TEST(Grammar, RecursionStopsAtDepth14) {
  Packs p({"[loop]\nx{loop}\n"});
  Grammar g(p.index, p.reader);
  const std::string out = expand(g, "{loop}");
  EXPECT_GE(out.size(), 5u);
  EXPECT_LE(out.size(), 14u);
  EXPECT_EQ(out.find_first_not_of('x'), std::string::npos);
}

TEST(Grammar, MissingKeysAndBadBracketsDoNotCrash) {
  Packs p({"[a]\nfine\n[event.e]\nEvent text\n"});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "{nope} and {a}"), "and fine");
  EXPECT_EQ(g.missing(), 1);
  EXPECT_EQ(expand(g, "{event.e}"), "");  // events are not lists
  EXPECT_EQ(g.missing(), 1);
  EXPECT_EQ(expand(g, "open {a and [b"), "open {a and [b");
}

TEST(Grammar, LongOutputIsCutSafely) {
  Packs p({"[long]\n0123456789012345678901234567890123456789012345678901234567890123456789\n"});
  Grammar g(p.index, p.reader);
  std::string text;
  for (int i = 0; i < 40; ++i) text += "{long}";
  const std::string out = expand(g, text.c_str());
  EXPECT_TRUE(g.truncated());
  EXPECT_EQ(out.size(), Grammar::kOutputSize - 1);
}

TEST(Grammar, TidiesSpacingAndDoubledArticles) {
  Packs p({"[river]\nthe river\n[empty]\n\n"});
  Grammar g(p.index, p.reader);
  EXPECT_EQ(expand(g, "  Cross   the {river} , quickly ! "), "Cross the river, quickly!");
  EXPECT_EQ(expand(g, "A {river.the} flows."), "The river flows.");
}

TEST(Grammar, DynamicKeys) {
  Packs p({"[name.m]\nTian\n[name.f]\nXue\n"});
  Grammar g(p.index, p.reader);
  std::set<std::string> seen;
  for (uint32_t seed = 1; seed <= 20; ++seed) seen.insert(expand(g, "{name.[m|f]}", seed));
  EXPECT_EQ(seen, (std::set<std::string>{"Tian", "Xue"}));
}

TEST(Grammar, SdEntriesAreReachable) {
  Packs p({"[r]\nbuilt-in\n", "[r]\nfrom the card\n"});
  Grammar g(p.index, p.reader);
  std::set<std::string> seen;
  for (uint32_t seed = 1; seed <= 20; ++seed) seen.insert(expand(g, "{r}", seed));
  EXPECT_EQ(seen, (std::set<std::string>{"built-in", "from the card"}));
}

// --- Pack checker ------------------------------------------------------------------

namespace {
struct Collected {
  std::vector<std::string> messages;
  static void add(void* user, uint8_t, const char* message) {
    static_cast<Collected*>(user)->messages.emplace_back(message);
  }
};

std::vector<std::string> check(Packs& p, const uint8_t firstSource = 0) {
  Collected c;
  PackChecker checker(p.index, p.reader, PackCheckSink{&Collected::add, &c});
  const uint32_t n = checker.run(firstSource);
  EXPECT_EQ(n, c.messages.size());
  return c.messages;
}

bool mentions(const std::vector<std::string>& messages, const char* text) {
  for (const auto& m : messages) {
    if (m.find(text) != std::string::npos) return true;
  }
  return false;
}
}  // namespace

TEST(PackCheck, BuiltinPackIsClean) {
  Packs p({builtinPackText()});
  const auto messages = check(p);
  for (const auto& m : messages) ADD_FAILURE() << m;
  EXPECT_GT(p.index.entryCount(packKeyHash("rumor.body")), 10u);
}

TEST(PackCheck, FindsProblems) {
  Packs p(
      {"[a]\n{missing}\nopen {a\nstray }\n{$nobody}\n{a.caps}\n{Bad Key}\n{a>v} then {$v}\n{event.e}\n"
       "[event.e]\n@weight lots\n@req realm>=1 mood>2 stamina\n@check charm 40\n@win stones+[5-] text={a}\n"
       "@dance\n> Choice {a}\n"});
  const auto m = check(p);
  EXPECT_TRUE(mentions(m, "unknown list (or unknown modifier) 'missing'"));
  EXPECT_TRUE(mentions(m, "unbalanced bracket"));
  EXPECT_TRUE(mentions(m, "stray closing bracket"));
  EXPECT_TRUE(mentions(m, "unknown value or variable '$nobody'"));
  EXPECT_TRUE(mentions(m, "'a.caps'"));
  EXPECT_TRUE(mentions(m, "bad list key"));
  EXPECT_TRUE(mentions(m, "an event used as a list"));
  EXPECT_TRUE(mentions(m, "@weight needs a number"));
  EXPECT_TRUE(mentions(m, "unknown condition 'mood>2'"));
  EXPECT_TRUE(mentions(m, "condition without an operator 'stamina'"));
  EXPECT_TRUE(mentions(m, "@check kind"));
  EXPECT_TRUE(mentions(m, "bad effect"));
  EXPECT_TRUE(mentions(m, "unknown directive '@dance'"));
  EXPECT_FALSE(mentions(m, "'$v'"));  // stored by {a>v}
  EXPECT_EQ(m.size(), 13u);
}

TEST(PackCheck, OnlyTheRequestedSourcesAreChecked) {
  Packs p({"[a]\n{broken\n", "[b]\n{a} is fine\n"});
  EXPECT_EQ(check(p, 1).size(), 0u);
  EXPECT_EQ(check(p, 0).size(), 1u);
}

TEST(PackCheck, LongLinesAreReported) {
  const std::string text = "[a]\n" + std::string(200, 'x') + "\n";
  Packs p({text.c_str()});
  EXPECT_TRUE(mentions(check(p), "longer than 191"));
}

// --- The rumor of the day, from the built-in pack ----------------------------------

TEST(BuiltinPack, RumorsExpandCleanly) {
  Packs p({builtinPackText()});
  Grammar g(p.index, p.reader);
  std::set<std::string> distinct;
  for (uint32_t day = 0; day < 300; ++day) {
    g.beginScene();
    const std::string r = expand(g, "{rumor}", contentSeed(1234, 50, day));
    EXPECT_EQ(g.missing(), 0) << r;
    EXPECT_EQ(r.find_first_of("{}[]|$\\"), std::string::npos) << r;
    EXPECT_TRUE(r.front() >= 'A' && r.front() <= 'Z') << r;
    EXPECT_EQ(r.back(), '.') << r;
    EXPECT_LT(r.size(), 200u) << r;
    distinct.insert(r);
  }
  EXPECT_GT(distinct.size(), 250u);
}

TEST(BuiltinPack, ImmortalPlaneRumorsUseTheirLayer) {
  Packs p({builtinPackText()});
  Grammar g(p.index, p.reader);
  GrammarContext ctx;
  ctx.plane = 1;
  const std::string r = expand(g, "{rumor}", 7, ctx);
  EXPECT_TRUE(r.find("Immortal Plane") != std::string::npos || r.find("floating island") != std::string::npos) << r;
}
