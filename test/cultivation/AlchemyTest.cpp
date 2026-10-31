#include <Alchemy.h>
#include <Breakthrough.h>
#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <Inventory.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace cultivation;
using inventory::Grade;
using inventory::Item;
using inventory::Quality;

namespace {

GameState makeCharacter(const uint8_t realm = 1, const uint32_t worldSeed = 0x5EED1234,
                        const Origin origin = Origin::FarmersChild) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = worldSeed;
  d.origin = origin;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.realm = realm;
  s.progress.stage = 0;
  s.progress.hpMax = 100;
  s.progress.hp = 100;
  s.progress.stamina = 12;
  return s;
}

void atPeak(GameState& s, const uint8_t realm) {
  s.progress.realm = realm;
  s.progress.stage = static_cast<uint8_t>(realmInfo(realm).stages - 1);
  s.progress.qi = stageCost(realm, s.progress.stage);
}

void giveParts(GameState& s, const alchemy::Recipe& r, const uint16_t batches = 1) {
  for (uint8_t p = 0; p < r.parts; ++p) inventory::add(s, r.part[p], batches);
}

void readFor(GameState& s, const uint32_t minutes) { s.progress.gameReadingSeconds += minutes * 60u; }

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

}  // namespace

// --- Recipes ----------------------------------------------------------------------------

TEST(Alchemy, RecipesAreDistinctAndDrawnFromTheirRegions) {
  for (uint32_t world = 1; world <= 2000; ++world) {
    const alchemy::RecipeBook book = alchemy::recipesFor(world * 2654435761u);
    std::set<uint32_t> seen;
    for (uint8_t i = 0; i < alchemy::kRecipeCount; ++i) {
      const alchemy::Recipe& r = book.recipes[i];
      ASSERT_GE(r.parts, 2);
      ASSERT_LE(r.parts, 3);
      uint32_t key = r.parts;
      bool herb = false;
      bool newest = false;
      uint8_t weakest = 255;
      for (uint8_t p = 0; p < r.parts; ++p) {
        ASSERT_TRUE(inventory::isIngredient(r.part[p]));
        const uint8_t region = inventory::regionOf(r.part[p]);
        EXPECT_GE(region, r.lowRegion);
        EXPECT_LE(region, r.highRegion);
        herb = herb || inventory::isHerb(r.part[p]);
        newest = newest || region == r.highRegion;
        weakest = region < weakest ? region : weakest;
        if (p > 0) {
          EXPECT_LE(r.part[p - 1], r.part[p]) << "sorted";
        }
        key = (key << 8) | static_cast<uint8_t>(r.part[p]);
      }
      EXPECT_TRUE(herb) << "at least one herb";
      EXPECT_TRUE(newest) << "a part from the newest region in range";
      EXPECT_EQ(r.grade, inventory::gradeOfRegion(weakest));
      EXPECT_GE(r.brewMinutes, 20);
      EXPECT_LE(r.brewMinutes, 60);
      EXPECT_TRUE(seen.insert(key).second) << "world " << world << " recipe " << int(i) << " repeats a combination";
    }
  }
}

TEST(Alchemy, RecipesDependOnTheWorldOnly) {
  const alchemy::RecipeBook a = alchemy::recipesFor(42);
  const alchemy::RecipeBook b = alchemy::recipesFor(42);
  const alchemy::RecipeBook c = alchemy::recipesFor(43);
  bool differs = false;
  for (uint8_t i = 0; i < alchemy::kRecipeCount; ++i) {
    EXPECT_EQ(memcmp(a.recipes[i].part, b.recipes[i].part, sizeof(a.recipes[i].part)), 0);
    differs = differs || memcmp(a.recipes[i].part, c.recipes[i].part, sizeof(a.recipes[i].part)) != 0;
  }
  EXPECT_TRUE(differs);
}

TEST(Alchemy, BreakthroughPillGradesFollowTheirRegions) {
  const alchemy::RecipeBook book = alchemy::recipesFor(7);
  using alchemy::Pill;
  EXPECT_EQ(book.recipes[static_cast<int>(Pill::BreakthroughCommon)].grade, Grade::Common);
  EXPECT_EQ(book.recipes[static_cast<int>(Pill::BreakthroughSpirit)].grade, Grade::Spirit);
  EXPECT_EQ(book.recipes[static_cast<int>(Pill::BreakthroughRare)].grade, Grade::Rare);
}

// --- Experiments, brewing, quality ------------------------------------------------------

TEST(Alchemy, AnExperimentThatMatchesLearnsAndBrews) {
  GameState s = makeCharacter(1);
  const alchemy::Recipe r = alchemy::recipesOf(s).recipes[0];
  giveParts(s, r);
  EXPECT_FALSE(alchemy::known(s, 0));
  const alchemy::Experiment e = alchemy::experiment(s, r.part, r.parts);
  ASSERT_TRUE(e.valid);
  EXPECT_EQ(e.recipe, 0);
  EXPECT_TRUE(e.learned);
  EXPECT_TRUE(alchemy::known(s, 0));
  EXPECT_TRUE(alchemy::brewing(s));
  EXPECT_EQ(alchemy::brewingRecipe(s), 0);
  for (uint8_t p = 0; p < r.parts; ++p) EXPECT_EQ(inventory::count(s, r.part[p]), 0) << "parts used up";
}

TEST(Alchemy, AMissedExperimentUsesThePartsAndCountsMatches) {
  for (uint32_t world = 1; world < 200; ++world) {
    GameState s = makeCharacter(4, world * 7919u);
    const alchemy::RecipeBook book = alchemy::recipesOf(s);
    // Two parts that are not a recipe: take a 2-part recipe and swap one part for another region's ore.
    const alchemy::Recipe& qi = book.recipes[static_cast<int>(alchemy::Pill::QiGathering)];
    Item parts[2] = {qi.part[0], inventory::oreFor(3)};
    bool isRecipe = false;
    for (const auto& r : book.recipes) {
      Item sorted[2] = {parts[0] < parts[1] ? parts[0] : parts[1], parts[0] < parts[1] ? parts[1] : parts[0]};
      isRecipe = isRecipe || (r.parts == 2 && r.part[0] == sorted[0] && r.part[1] == sorted[1]);
    }
    if (isRecipe) continue;
    inventory::add(s, parts[0]);
    inventory::add(s, parts[1]);
    const alchemy::Experiment e = alchemy::experiment(s, parts, 2);
    ASSERT_TRUE(e.valid);
    EXPECT_EQ(e.recipe, -1);
    EXPECT_FALSE(alchemy::brewing(s)) << "a miss never brews";
    EXPECT_EQ(e.matches, 1) << "one part of the Qi Gathering recipe was right";
    EXPECT_EQ(inventory::count(s, parts[0]), 0);
    EXPECT_EQ(inventory::count(s, parts[1]), 0);
  }
}

TEST(Alchemy, MissesSayWhenEveryRecipeOfThatSizeIsKnown) {
  GameState s = makeCharacter(1);
  const alchemy::RecipeBook book = alchemy::recipesOf(s);
  for (uint8_t i = 0; i < alchemy::kRecipeCount; ++i) {
    if (book.recipes[i].parts == 2) {
      alchemy::learn(s, i);
    }
  }
  const Item parts[2] = {inventory::oreFor(9), inventory::oreFor(9)};
  inventory::add(s, parts[0], 2);
  const alchemy::Experiment e = alchemy::experiment(s, parts, 2);
  ASSERT_TRUE(e.valid);
  EXPECT_TRUE(e.noneUnknown);
}

TEST(Alchemy, ExperimentsNeedThePartsAndAnEmptyCauldron) {
  GameState s = makeCharacter(1);
  const alchemy::Recipe r = alchemy::recipesOf(s).recipes[1];
  EXPECT_FALSE(alchemy::experiment(s, r.part, r.parts).valid) << "parts not held";
  giveParts(s, r, 2);
  ASSERT_TRUE(alchemy::experiment(s, r.part, r.parts).valid);
  EXPECT_FALSE(alchemy::experiment(s, r.part, r.parts).valid) << "the cauldron is busy";
  const Item one[1] = {r.part[0]};
  EXPECT_FALSE(alchemy::experiment(s, one, 1).valid) << "at least two parts";
}

TEST(Alchemy, BrewingTakesReadingTimeAndCollectingGivesThePill) {
  GameState s = makeCharacter(1);
  const uint8_t recipe = static_cast<uint8_t>(alchemy::Pill::BreakthroughCommon);
  const alchemy::Recipe r = alchemy::recipesOf(s).recipes[recipe];
  alchemy::learn(s, recipe);
  EXPECT_FALSE(alchemy::canBrew(s, recipe)) << "no parts";
  giveParts(s, r);
  EXPECT_EQ(alchemy::batchesHeld(s, r), 1);
  ASSERT_TRUE(alchemy::brew(s, recipe));
  EXPECT_EQ(alchemy::brewMinutesLeft(s), 40u);
  EXPECT_FALSE(alchemy::collect(s).ok);
  readFor(s, 39);
  EXPECT_FALSE(alchemy::ready(s));
  readFor(s, 1);
  EXPECT_TRUE(alchemy::ready(s));
  const alchemy::Collected c = alchemy::collect(s);
  ASSERT_TRUE(c.ok);
  EXPECT_EQ(c.count, 1);
  EXPECT_TRUE(inventory::isBreakthroughPill(c.item));
  EXPECT_EQ(inventory::pillGroup(c.item), 0) << "common";
  EXPECT_EQ(inventory::count(s, c.item), 1);
  EXPECT_FALSE(alchemy::brewing(s));
}

TEST(Alchemy, QualityBandsAndBatchSizes) {
  std::map<Quality, int> bands;
  for (uint32_t world = 1; world < 400; ++world) {
    GameState s = makeCharacter(1, world);
    const uint8_t recipe = static_cast<uint8_t>(alchemy::Pill::TonicMinor);
    const alchemy::Recipe r = alchemy::recipesOf(s).recipes[recipe];
    alchemy::learn(s, recipe);
    giveParts(s, r);
    ASSERT_TRUE(alchemy::brew(s, recipe));
    readFor(s, 20);
    const alchemy::Collected c = alchemy::collect(s);
    ASSERT_TRUE(c.ok);
    EXPECT_EQ(c.item, Item::TonicMinor);
    EXPECT_EQ(c.count, static_cast<uint16_t>(static_cast<uint8_t>(c.quality) + 1));
    EXPECT_EQ(c.quality, c.roll >= 85 ? Quality::High : c.roll >= 50 ? Quality::Medium : Quality::Low);
    ++bands[c.quality];
  }
  // Common ingredients, no Herbalist: about 50% Low, 35% Medium, 15% High.
  EXPECT_GT(bands[Quality::Low], 150);
  EXPECT_GT(bands[Quality::Medium], 100);
  EXPECT_GT(bands[Quality::High], 30);
}

TEST(Alchemy, TheHerbalistStartsWithTheQiRecipeAndBrewsBetter) {
  GameState herbalist = makeCharacter(1, 99, Origin::HerbalistApprentice);
  GameState farmer = makeCharacter(1, 99, Origin::FarmersChild);
  EXPECT_TRUE(alchemy::known(herbalist, static_cast<uint8_t>(alchemy::Pill::QiGathering)));
  EXPECT_EQ(alchemy::knownCount(herbalist), 1);
  EXPECT_EQ(alchemy::knownCount(farmer), 0);
  const alchemy::Recipe r = alchemy::recipesOf(farmer).recipes[0];
  EXPECT_EQ(alchemy::qualityBonus(herbalist, r), alchemy::qualityBonus(farmer, r) + 10);
  // A Herbalist made before v0.2.0 gets the recipe once, on the next visit.
  herbalist.flagsAndCodex.fill(0);
  EXPECT_TRUE(alchemy::grantStartingRecipe(herbalist));
  EXPECT_FALSE(alchemy::grantStartingRecipe(herbalist));
  EXPECT_FALSE(alchemy::grantStartingRecipe(farmer));
}

// --- Recipe slips ------------------------------------------------------------------------

TEST(Alchemy, SlipsTeachTheFirstUnknownRecipeOfOpenRegions) {
  GameState s = makeCharacter(1);  // region 0 open
  EXPECT_EQ(alchemy::slipRecipe(s), 0);
  for (uint8_t i = 0; i < 4; ++i) EXPECT_EQ(alchemy::learnSlip(s), i);
  EXPECT_EQ(alchemy::slipRecipe(s), -1) << "the rest need regions 1 and later";
  s.progress.realm = 2;
  EXPECT_EQ(alchemy::slipRecipe(s), static_cast<int>(alchemy::Pill::CoreForming));
  const alchemy::RecipeBook book = alchemy::recipesOf(s);
  EXPECT_EQ(alchemy::slipPrice(book, static_cast<uint8_t>(alchemy::Pill::CoreForming)), 60);
  EXPECT_EQ(alchemy::slipPrice(book, static_cast<uint8_t>(alchemy::Pill::BreakthroughSpirit)), 150);
  EXPECT_EQ(alchemy::slipPrice(book, static_cast<uint8_t>(alchemy::Pill::BreakthroughRare)), 300);
}

TEST(Alchemy, MerchantsSellSlipsSometimes) {
  int withSlip = 0;
  int merchants = 0;
  for (uint32_t world = 1; world < 300; ++world) {
    GameState s = makeCharacter(1, world * 7919u);
    const explore::Layout l = explore::layoutOf(s, 0);
    for (uint8_t t = 0; t < explore::kTiles; ++t) {
      if (l.tiles[t] != explore::Tile::Merchant) continue;
      ++merchants;
      const explore::Stock st = explore::stockFor(s, l, 0, t);
      for (uint8_t i = 0; i < st.count; ++i) {
        if (st.lines[i].item != Item::RecipeSlip) continue;
        ++withSlip;
        s.progress.stones = 1000;
        const int recipe = alchemy::slipRecipe(s);
        ASSERT_TRUE(explore::buy(s, l, 0, t, i));
        EXPECT_EQ(s.progress.stones, 940u);
        EXPECT_TRUE(alchemy::known(s, static_cast<uint8_t>(recipe)));
        EXPECT_EQ(inventory::count(s, Item::RecipeSlip), 0) << "a slip never sits in the bag";
      }
    }
  }
  EXPECT_GT(withSlip, merchants / 20);
  EXPECT_LT(withSlip, merchants / 4);
}

// --- Breakthrough pills ----------------------------------------------------------------------

TEST(Alchemy, BreakthroughPillsFitTheirRealms) {
  const Item common = inventory::breakthroughPill(Grade::Common, Quality::Medium);
  const Item spirit = inventory::breakthroughPill(Grade::Spirit, Quality::Low);
  const Item rare = inventory::breakthroughPill(Grade::Rare, Quality::High);
  EXPECT_FALSE(alchemy::pillFits(common, 1)) << "the small bottleneck needs no key";
  EXPECT_TRUE(alchemy::pillFits(common, 2));
  EXPECT_TRUE(alchemy::pillFits(common, 4));
  EXPECT_FALSE(alchemy::pillFits(common, 5));
  EXPECT_TRUE(alchemy::pillFits(spirit, 7));
  EXPECT_FALSE(alchemy::pillFits(spirit, 8));
  EXPECT_TRUE(alchemy::pillFits(rare, 9));
  EXPECT_EQ(alchemy::breakthroughBonus(common), 17);
  EXPECT_EQ(alchemy::breakthroughBonus(spirit), 10);
  EXPECT_EQ(alchemy::breakthroughBonus(rare), 25);
}

TEST(Alchemy, EachGradeIsBrewableBeforeItsRealmsBottleneck) {
  // Agreed with the user: a pill's grade must be brewable with the regions
  // open at the peak before the realm it opens (region i opens at realm i + 1).
  const alchemy::RecipeBook book = alchemy::recipesFor(1);
  for (uint8_t target = 2; target <= 9; ++target) {
    const uint8_t open = static_cast<uint8_t>(target - 1);  // regions open at the peak of realm target - 1
    bool brewable = false;
    for (const alchemy::Pill p :
         {alchemy::Pill::BreakthroughCommon, alchemy::Pill::BreakthroughSpirit, alchemy::Pill::BreakthroughRare}) {
      const alchemy::Recipe& r = book.recipes[static_cast<int>(p)];
      const Item pill = alchemy::pillItem(p, Quality::Low);
      brewable = brewable || (r.highRegion < open && alchemy::pillFits(pill, target));
    }
    EXPECT_TRUE(brewable) << "no breakthrough pill can be brewed for realm " << int(target);
  }
}

TEST(Alchemy, APillIsTheKeyWhenNoScrollIs) {
  GameState s = makeCharacter(1);
  atPeak(s, 1);
  EXPECT_FALSE(breakthrough::canAttempt(s)) << "Foundation needs a key";
  const Item pill = inventory::breakthroughPill(Grade::Common, Quality::High);
  inventory::add(s, pill);
  const breakthrough::Chance c = breakthrough::chanceFor(s);
  EXPECT_TRUE(c.hasKey);
  EXPECT_TRUE(c.pillIsKey);
  EXPECT_EQ(c.pill, 25);
  EXPECT_EQ(c.pillItem, pill);
  EXPECT_TRUE(breakthrough::canAttempt(s));
}

TEST(Alchemy, ThePillIsUsedUpOnEveryAttempt) {
  int successes = 0;
  int failures = 0;
  for (uint32_t world = 1; world < 60; ++world) {
    GameState s = makeCharacter(1, world);
    atPeak(s, 1);
    const Item pill = inventory::breakthroughPill(Grade::Common, Quality::Low);
    inventory::add(s, pill, 2);
    const breakthrough::Result r = breakthrough::attempt(s);
    ASSERT_TRUE(r.attempted);
    EXPECT_EQ(r.pillUsed, pill);
    EXPECT_EQ(inventory::count(s, pill), 1);
    EXPECT_EQ(alchemy::pillInUse(s), Item::None);
    if (r.success) {
      ++successes;
    } else {
      ++failures;
    }
  }
  EXPECT_GT(successes, 0);
  EXPECT_GT(failures, 0);
}

TEST(Alchemy, NoPillChoiceKeepsThePillWhenAScrollIsTheKey) {
  GameState s = makeCharacter(1);
  atPeak(s, 1);
  scrolls::Scroll key;
  key.titleHash = 5;
  key.kind = scrolls::Kind::Mind;
  key.hours = 1;
  scrolls::put(s, 0, key);
  s.techniqueScrolls[0] = 1;
  const Item a = inventory::breakthroughPill(Grade::Common, Quality::Low);
  const Item b = inventory::breakthroughPill(Grade::Common, Quality::High);
  inventory::add(s, a);
  inventory::add(s, b);
  EXPECT_EQ(alchemy::chosenPill(s, 2), b) << "the best fitting pill by default";
  alchemy::cycleChoice(s, 2, false);
  EXPECT_EQ(alchemy::chosenPill(s, 2), Item::None) << "after the last pill comes none";
  alchemy::cycleChoice(s, 2, false);
  EXPECT_EQ(alchemy::chosenPill(s, 2), a);
  alchemy::cycleChoice(s, 2, false);
  alchemy::cycleChoice(s, 2, false);
  EXPECT_EQ(alchemy::chosenPill(s, 2), Item::None);
  const breakthrough::Chance c = breakthrough::chanceFor(s);
  EXPECT_FALSE(c.pillIsKey);
  EXPECT_TRUE(c.hasKey);
  EXPECT_EQ(c.pill, 0);
  breakthrough::attempt(s);
  EXPECT_EQ(inventory::count(s, a) + inventory::count(s, b), 2) << "no pill taken";
}

TEST(Alchemy, ATrialTakesThePillAndLosingItLosesThePill) {
  GameState s = makeCharacter(2);
  atPeak(s, 2);
  s.progress.hp = 120;
  s.progress.hpMax = 120;
  const Item pill = inventory::breakthroughPill(Grade::Common, Quality::Medium);
  inventory::add(s, pill);
  ASSERT_TRUE(trials::begin(s));
  EXPECT_EQ(alchemy::pillInUse(s), pill);
  EXPECT_EQ(inventory::count(s, pill), 0);
  EXPECT_EQ(breakthrough::chanceFor(s).pill, 17) << "the pill taken still counts";
  EXPECT_TRUE(breakthrough::chanceFor(s).hasKey);
  breakthrough::failAttempt(s);  // what a lost trial does
  EXPECT_EQ(alchemy::pillInUse(s), Item::None);
}

// --- Grade pills and Qi pills ------------------------------------------------------------------

TEST(Alchemy, GradePillsCountTowardTheNextGradeOnly) {
  GameState s = makeCharacter(1);
  const Item foundation = inventory::gradePill(0, Quality::High);
  const Item core = inventory::gradePill(1, Quality::Low);
  inventory::add(s, foundation, 3);
  inventory::add(s, core);
  EXPECT_EQ(alchemy::canUse(s, core), alchemy::UseError::WrongRealm);
  const uint8_t before = breakthrough::gradeScore(s, 2);
  EXPECT_EQ(alchemy::use(s, foundation), alchemy::UseError::None);
  EXPECT_EQ(alchemy::use(s, foundation), alchemy::UseError::None);
  EXPECT_EQ(alchemy::use(s, foundation), alchemy::UseError::Enough);
  EXPECT_EQ(inventory::count(s, foundation), 1);
  EXPECT_EQ(alchemy::gradePillScore(s), 20);
  EXPECT_EQ(breakthrough::gradeScore(s, 2), before + 20);
  // A successful breakthrough into Foundation spends them.
  atPeak(s, 1);
  inventory::add(s, inventory::breakthroughPill(Grade::Common, Quality::High), 20);
  for (int i = 0; i < 20 && s.progress.realm == 1; ++i) {
    breakthrough::attempt(s);
    if (s.progress.realm == 1) s.progress.qi = stageCost(1, s.progress.stage);
  }
  ASSERT_EQ(s.progress.realm, 2);
  EXPECT_EQ(alchemy::gradePillScore(s), 0);
  EXPECT_EQ(alchemy::canUse(s, core), alchemy::UseError::None);
}

TEST(Alchemy, BreakingIntoQiCondensationKeepsFoundationPills) {
  GameState s = makeCharacter(0);
  inventory::add(s, inventory::gradePill(0, Quality::Low));
  ASSERT_EQ(alchemy::use(s, inventory::gradePill(0, Quality::Low)), alchemy::UseError::None);
  atPeak(s, 0);
  for (int i = 0; i < 20 && s.progress.realm == 0; ++i) {
    breakthrough::attempt(s);
    if (s.progress.realm == 0) s.progress.qi = stageCost(0, s.progress.stage);
  }
  ASSERT_EQ(s.progress.realm, 1);
  EXPECT_EQ(alchemy::gradePillScore(s), 6);
}

TEST(Alchemy, QiGatheringPillAddsAQuarterForAnHourOfReading) {
  GameState plain = makeCharacter(1);
  GameState pill = makeCharacter(1);
  inventory::add(pill, Item::QiPill, 4);
  ASSERT_EQ(alchemy::use(pill, Item::QiPill), alchemy::UseError::None);
  ASSERT_EQ(alchemy::use(pill, Item::QiPill), alchemy::UseError::None);
  ASSERT_EQ(alchemy::use(pill, Item::QiPill), alchemy::UseError::None);
  EXPECT_EQ(alchemy::use(pill, Item::QiPill), alchemy::UseError::QiFull) << "up to 180 minutes";
  EXPECT_EQ(alchemy::qiPillSecondsLeft(pill), 10800u);
  VisitGains g;
  g.seconds = 3600;
  g.tiers.fullSeconds = 3600;
  const AbsorbResult a = absorb(plain, g);
  const AbsorbResult b = absorb(pill, g);
  EXPECT_EQ(b.qiPillSeconds, 3600u);
  EXPECT_EQ(alchemy::qiPillSecondsLeft(pill), 7200u);
  // 600 Qi x multiplier vs 750 Qi x multiplier.
  uint64_t qa = 0;
  uint64_t qb = 0;
  ASSERT_TRUE(a.qiGained.toU64(qa));
  ASSERT_TRUE(b.qiGained.toU64(qb));
  EXPECT_NEAR(static_cast<double>(qb), static_cast<double>(qa) * 1.25, 2.0);
}

// --- The old bag ---------------------------------------------------------------------------------

TEST(Alchemy, AV0110BagMovesToTheNewOne) {
  GameState s = makeCharacter(5);  // regions 0-4 open
  auto put = [&](const uint8_t slot, const uint8_t id, const uint16_t n) {
    s.inventory[slot * 3] = id;
    s.inventory[slot * 3 + 1] = static_cast<uint8_t>(n);
    s.inventory[slot * 3 + 2] = static_cast<uint8_t>(n >> 8);
  };
  put(0, 1, 5);    // common herbs
  put(1, 5, 300);  // spirit ore
  put(2, 3, 2);    // rare herbs (no rare region open yet)
  put(3, 7, 4);    // minor tonics
  put(4, 12, 1);   // Region Talisman
  ASSERT_TRUE(inventory::migrateLegacyBag(s));
  EXPECT_EQ(inventory::count(s, inventory::herbFor(2)), 5) << "newest open common region";
  EXPECT_EQ(inventory::count(s, inventory::oreFor(4)), 300) << "newest open spirit region";
  EXPECT_EQ(inventory::count(s, inventory::herbFor(6)), 2) << "the first rare region";
  EXPECT_EQ(inventory::count(s, Item::TonicMinor), 4);
  EXPECT_EQ(inventory::count(s, Item::RegionTalisman), 1);
  for (const uint8_t b : s.inventory) EXPECT_EQ(b, 0u);
  EXPECT_FALSE(inventory::migrateLegacyBag(s)) << "once";
}

TEST(Alchemy, PricesAndSellBack) {
  EXPECT_EQ(inventory::price(inventory::herbFor(0)), 10);
  EXPECT_EQ(inventory::price(inventory::oreFor(4)), 30);
  EXPECT_EQ(inventory::price(inventory::herbFor(9)), 80);
  EXPECT_EQ(inventory::sellPrice(Item::QiPill), 20);
  EXPECT_EQ(inventory::sellPrice(inventory::breakthroughPill(Grade::Common, Quality::Low)), 40);
  EXPECT_EQ(inventory::sellPrice(inventory::breakthroughPill(Grade::Spirit, Quality::Medium)), 150);
  EXPECT_EQ(inventory::sellPrice(inventory::breakthroughPill(Grade::Rare, Quality::High)), 450);
  EXPECT_EQ(inventory::sellPrice(inventory::gradePill(2, Quality::Medium)), 70);
  EXPECT_EQ(inventory::sellPrice(Item::TonicFull), 66) << "unchanged";
  EXPECT_EQ(inventory::price(Item::RecipeSlip), 0) << "priced by the recipe it teaches";
}

// --- Enemy kinds -----------------------------------------------------------------------------------

TEST(Enemies, KindsFollowTheThemeWeights) {
  // Region 7 (battlefield) leans to the dead, region 9 (ridge) to cultivators.
  std::map<explore::Enemy, int> battlefield;
  std::map<explore::Enemy, int> ridge;
  for (uint32_t step = 0; step < 4000; ++step) {
    GameState s = makeCharacter(9);
    s.exploration[0] = static_cast<uint8_t>(step);
    s.exploration[1] = static_cast<uint8_t>(step >> 8);
    ++battlefield[explore::encounterEnemy(s, 7)];
    ++ridge[explore::encounterEnemy(s, 9)];
  }
  EXPECT_NEAR(battlefield[explore::Enemy::Spirit], 1800, 200);
  EXPECT_NEAR(ridge[explore::Enemy::Cultivator], 1800, 200);
  EXPECT_NEAR(ridge[explore::Enemy::Spirit], 400, 120);
}

TEST(Enemies, LairsAreDensOrHideouts) {
  std::set<explore::Enemy> kinds;
  for (uint32_t world = 1; world < 100; ++world) {
    GameState s = makeCharacter(4, world);
    for (uint8_t t = 0; t < explore::kTiles; ++t) kinds.insert(explore::lairEnemy(s, 3, t));
  }
  EXPECT_EQ(kinds.size(), 2u);
  EXPECT_EQ(kinds.count(explore::Enemy::Cultivator), 0u);
  EXPECT_EQ(kinds.count(explore::Enemy::Spirit), 0u);
}

namespace {
// Steps around region `region` until a random encounter of `kind` waits.
bool findEncounter(GameState& s, const explore::Layout& l, const uint8_t region, const explore::Enemy kind) {
  explore::enter(s, l, region);
  for (int i = 0; i < 4000; ++i) {
    s.progress.stamina = 12;
    s.progress.hp = s.progress.hpMax;
    const uint8_t here = explore::position(s, l, region);
    uint8_t n[4];
    const uint8_t c = explore::neighbours(here, n);
    uint8_t to = explore::kNoTile;
    for (uint8_t k = 0; k < c; ++k) {
      const explore::Tile t = l.tiles[n[(k + i) % c]];
      if (explore::walkable(l, n[(k + i) % c]) && t != explore::Tile::Lair && t != explore::Tile::Guardian) {
        to = n[(k + i) % c];
        break;
      }
    }
    if (to == explore::kNoTile) return false;
    explore::step(s, l, region, to);
    const explore::Fight f = explore::pendingFight(s, l);
    if (f.kind == explore::FightKind::Beast && f.enemy == kind) return true;
    if (f.kind != explore::FightKind::None) explore::fight(s, l, explore::FightChoice::Fight);
  }
  return false;
}
}  // namespace

TEST(Enemies, BanditsCanBePaidOff) {
  GameState s = makeCharacter(6);
  const explore::Layout l = explore::layoutOf(s, 3);  // the gorge: bandits are common
  ASSERT_TRUE(findEncounter(s, l, 3, explore::Enemy::Bandit));
  const explore::Fight f = explore::pendingFight(s, l);
  EXPECT_EQ(f.payCost, 60u);  // 15 x (3 + 1)
  s.progress.stones = 59;
  EXPECT_TRUE(explore::fight(s, l, explore::FightChoice::Pay).refused);
  s.progress.stones = 100;
  const uint16_t hp = s.progress.hp;
  const explore::FightResult r = explore::fight(s, l, explore::FightChoice::Pay);
  EXPECT_TRUE(r.paid);
  EXPECT_EQ(r.paidStones, 60u);
  EXPECT_GE(s.progress.stones, 40u);  // the tile may have held stones too
  EXPECT_EQ(s.progress.hp, hp);
  EXPECT_EQ(explore::pendingFight(s, l).kind, explore::FightKind::None);
}

TEST(Enemies, SpiritsCannotBeOutrunButIncenseDrivesThemOff) {
  GameState s = makeCharacter(8);
  const explore::Layout l = explore::layoutOf(s, 7);  // the battlefield
  ASSERT_TRUE(findEncounter(s, l, 7, explore::Enemy::Spirit));
  explore::Fight f = explore::pendingFight(s, l);
  EXPECT_FALSE(f.canSlip);
  EXPECT_EQ(f.payCost, 0u);
  EXPECT_FALSE(f.canIncense) << "no incense held";
  EXPECT_TRUE(explore::fight(s, l, explore::FightChoice::Slip).refused);
  inventory::add(s, Item::CalmIncense);
  f = explore::pendingFight(s, l);
  EXPECT_TRUE(f.canIncense);
  const explore::FightResult r = explore::fight(s, l, explore::FightChoice::Incense);
  EXPECT_TRUE(r.drivenOff);
  EXPECT_EQ(r.hpLost, 0);
  EXPECT_EQ(inventory::count(s, Item::CalmIncense), 0);
}

TEST(Enemies, CultivatorsDoubleTechniquesOnBothSides) {
  GameState s = makeCharacter(10);
  const explore::Layout l = explore::layoutOf(s, 9);  // the ridge
  scrolls::Scroll sword;
  sword.titleHash = 3;
  sword.kind = scrolls::Kind::Sword;
  sword.hours = 20;  // Heaven: +4, +8 against a cultivator
  sword.flags = scrolls::Scroll::kEquippedTechnique;
  scrolls::put(s, 0, sword);
  s.techniqueScrolls[0] = 1;
  EXPECT_EQ(explore::playerLevel(s, true), explore::playerLevel(s) + 4);
  ASSERT_TRUE(findEncounter(s, l, 9, explore::Enemy::Cultivator));
  const explore::Fight f = explore::pendingFight(s, l);
  const int extra = f.level - explore::regionBase(9);
  EXPECT_EQ(extra % 2, 0) << "the enemy's random extra counts double";
  EXPECT_LE(extra, 8);
}

TEST(Enemies, BeatenBanditsPayDoubleAndCarryNoHerbs) {
  int wins = 0;
  for (uint32_t world = 1; world < 40 && wins < 10; ++world) {
    GameState s = makeCharacter(9, world);  // far above the gorge: wins every time
    const explore::Layout l = explore::layoutOf(s, 3);
    if (!findEncounter(s, l, 3, explore::Enemy::Bandit)) continue;
    const explore::FightResult r = explore::fight(s, l, explore::FightChoice::Fight);
    if (!r.won) continue;  // 95% at most
    EXPECT_EQ(r.reward.item, Item::None);
    EXPECT_GE(r.reward.stones, 2u * 5u * 4u / 2u);  // twice 5-10 x 4, halved in a Trivial region
    ++wins;
  }
  EXPECT_GT(wins, 3);
}

// --- Text ------------------------------------------------------------------------------------------

TEST(Alchemy, IngredientNamesDifferPerRegionAndExpandCleanly) {
  BuiltinPacks packs;
  for (uint32_t world = 1; world < 30; ++world) {
    GameState s = makeCharacter(1, world * 31u);
    std::set<std::string> names;
    for (uint8_t region = 0; region < 10; ++region) {
      for (const Item item : {inventory::herbFor(region), inventory::oreFor(region)}) {
        char name[exploretext::kNameSize];
        ASSERT_TRUE(exploretext::ingredientName(s, item, *packs.grammar, name, sizeof(name)));
        EXPECT_EQ(std::string(name).find_first_of("{}[]|$"), std::string::npos) << name;
        EXPECT_LT(strlen(name), 32u) << name;
        names.insert(name);
      }
    }
    EXPECT_EQ(names.size(), 20u) << "every region has its own herb and ore";
  }
}

TEST(Alchemy, LinesExpandCleanly) {
  BuiltinPacks packs;
  GameState s = makeCharacter(1);
  for (const char* key : {"alchemy.learned", "alchemy.miss", "alchemy.collect"}) {
    for (uint32_t seed = 0; seed < 10; ++seed) {
      char out[240];
      ASSERT_TRUE(exploretext::alchemyFlavor(s, key, seed, "Core-Forming pill", *packs.grammar, out, sizeof(out)))
          << key;
      EXPECT_EQ(std::string(out).find_first_of("{}[]|$"), std::string::npos) << out;
    }
  }
  for (const char* key : {"alchemy.line.learned", "alchemy.line.slip"}) {
    lifelog::Line line;
    ASSERT_TRUE(exploretext::alchemyLine(line, s, key, 5, "Qi Gathering pill", *packs.grammar)) << key;
    EXPECT_NE(std::string(line.text).find("Qi Gathering pill"), std::string::npos) << line.text;
  }
}
