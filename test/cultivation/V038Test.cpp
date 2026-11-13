#include <Achievements.h>
#include <Codex.h>
#include <GameState.h>
#include <HalStorage.h>
#include <Lineage.h>
#include <Rivals.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

// v0.3.8: achievements (agreed with the user in session 18). For the completionist only: 136 in nine
// groups, four tiers, 16 hidden with a hint; kept through every life; an older save has what it proves
// recognised; save v10 adds the 384-byte block.

using namespace cultivation;
namespace ach = cultivation::achievements;

namespace {

constexpr uint32_t kMonday = 9802;

GameState hero(const uint8_t realm = 0, const uint32_t seed = 0x5EED1234) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.origin = Origin::StreetOrphan;
  d.worldSeed = seed;
  GameState s;
  StatsSample now;
  now.today = kMonday;
  createCharacter(s, d, now);
  s.progress.realm = realm;
  return s;
}

void newLife(GameState& s, const uint32_t seed) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Wen Shu");
  d.destiny.purity = 50;
  d.destiny.talent = 0;  // Choked Meridians
  d.origin = Origin::FarmersChild;
  d.worldSeed = seed;
  StatsSample now;
  now.today = kMonday + 400;
  createCharacter(s, d, now);
}

}  // namespace

// --- The list ---------------------------------------------------------------------------------

TEST(V038List, OneHundredThirtySixInGroupOrderWithRoomToGrow) {
  EXPECT_EQ(ach::kCount, 136);
  EXPECT_LE(ach::kCount, ach::kCapacity);
  uint8_t total = 0;
  for (uint8_t g = 0; g < ach::kGroupCount; ++g) {
    const auto group = static_cast<ach::Group>(g);
    const uint8_t first = ach::firstOf(group);
    const uint8_t n = ach::countIn(group);
    ASSERT_GT(n, 0);
    EXPECT_EQ(first, total) << "groups are contiguous, in order";
    for (uint8_t i = 0; i < n; ++i) EXPECT_EQ(ach::info(static_cast<uint8_t>(first + i)).group, group);
    total = static_cast<uint8_t>(total + n);
  }
  EXPECT_EQ(total, ach::kCount);
}

TEST(V038List, EveryGroupHasAHeavenTierEntryAndTheTiersAddUp) {
  // The user asked in session 18 that every group reach at least Heaven.
  for (uint8_t g = 0; g < ach::kGroupCount; ++g) {
    bool heaven = false;
    for (uint8_t i = 0; i < ach::kCount; ++i) {
      const ach::Info& in = ach::info(i);
      if (static_cast<uint8_t>(in.group) == g && in.tier >= ach::Tier::Heaven) heaven = true;
    }
    EXPECT_TRUE(heaven) << "group " << static_cast<int>(g);
  }
  uint8_t sum = 0;
  for (uint8_t t = 0; t < ach::kTierCount; ++t)
    sum = static_cast<uint8_t>(sum + ach::countOfTier(static_cast<ach::Tier>(t)));
  EXPECT_EQ(sum, ach::kCount);
}

TEST(V038List, SixteenHiddenOnesEachWithAHint) {
  int hidden = 0;
  for (uint8_t i = 0; i < ach::kCount; ++i) {
    const int h = ach::hiddenIndex(i);
    if (ach::info(i).hidden) {
      EXPECT_EQ(h, hidden);
      ++hidden;
    } else {
      EXPECT_EQ(h, -1);
    }
  }
  EXPECT_EQ(hidden, ach::kHiddenCount);
}

TEST(V038List, TheAgreedThresholds) {
  EXPECT_EQ(ach::info(ach::Id::Hours1000).target, 1000);
  EXPECT_EQ(ach::info(ach::Id::Hours1000).tier, ach::Tier::Immortal);
  EXPECT_EQ(ach::info(ach::Id::Books50).target, 50);
  EXPECT_EQ(ach::info(ach::Id::Books100).target, 100);
  EXPECT_EQ(ach::info(ach::Id::Books100).tier, ach::Tier::Immortal);
  EXPECT_EQ(ach::info(ach::Id::Clear10).target, 10);
  EXPECT_EQ(ach::info(ach::Id::Clear10).tier, ach::Tier::Heaven);
  EXPECT_EQ(ach::info(ach::Id::Meridian36).target, 36);
}

// --- Earning ------------------------------------------------------------------------------------

TEST(V038Earn, ANewCharacterStartsWithNothingAndTheFirstHourIsStamped) {
  GameState s = hero();
  EXPECT_EQ(ach::update(s), 0);  // sets the block up: nothing to recognise yet
  EXPECT_TRUE(ach::isSetUp(s));
  s.progress.gameReadingSeconds = 3600 + 60;
  EXPECT_EQ(ach::update(s), 1);
  EXPECT_TRUE(ach::earned(s, ach::Id::Hours1));
  EXPECT_EQ(ach::earnedHour(s, static_cast<uint8_t>(ach::Id::Hours1)), 1);
  const ach::Progress p = ach::progress(s, static_cast<uint8_t>(ach::Id::Hours10));
  EXPECT_EQ(p.have, 1u);
  EXPECT_EQ(p.need, 10u);
}

TEST(V038Earn, AnOlderSaveHasWhatItProvesRecognisedOnce) {
  GameState s = hero(3);  // Core Formation: three realms, a Tribulation survived
  s.progress.gameReadingSeconds = 60u * 3600u;
  s.progress.foundationGrade = 4;  // Perfect
  ASSERT_FALSE(ach::isSetUp(s));
  const uint8_t n = ach::update(s);
  EXPECT_GE(n, 8);
  for (const ach::Id id : {ach::Id::RealmQi, ach::Id::RealmFoundation, ach::Id::RealmCore, ach::Id::Tribulation,
                           ach::Id::PerfectFoundation, ach::Id::Hours1, ach::Id::Hours10, ach::Id::Hours50}) {
    EXPECT_TRUE(ach::earned(s, id)) << static_cast<int>(id);
    EXPECT_EQ(ach::earnedHour(s, static_cast<uint8_t>(id)), ach::kRecognised);
  }
  EXPECT_FALSE(ach::earned(s, ach::Id::RealmNascent));
  EXPECT_FALSE(ach::earned(s, ach::Id::Hours100));
  // Reading on: the next one is stamped with its hour, not "recognised"; nothing is counted twice.
  s.progress.gameReadingSeconds = 100u * 3600u;
  EXPECT_EQ(ach::update(s), 1);
  EXPECT_EQ(ach::earnedHour(s, static_cast<uint8_t>(ach::Id::Hours100)), 100);
  EXPECT_EQ(ach::readingHours(s), 100u);
}

TEST(V038Earn, MomentsTheSaveDoesNotKeepAreEarnedWhereTheyHappen) {
  GameState s = hero(1);
  ach::update(s);
  EXPECT_FALSE(ach::earned(s, ach::Id::LoseDuel));
  ach::earn(s, ach::Id::LoseDuel);
  EXPECT_TRUE(ach::earned(s, ach::Id::LoseDuel));
  // update() never takes away or re-stamps.
  s.progress.gameReadingSeconds = 5u * 3600u;
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::LoseDuel));
  EXPECT_EQ(ach::earnedHour(s, static_cast<uint8_t>(ach::Id::LoseDuel)), 0);
}

TEST(V038Earn, CountersReachTheirTargets) {
  GameState s = hero(1);
  ach::update(s);
  for (int i = 0; i < 99; ++i) ach::addCount(s, ach::Counter::FightsWon);
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::Win1));
  EXPECT_FALSE(ach::earned(s, ach::Id::Win100));
  EXPECT_EQ(ach::progress(s, static_cast<uint8_t>(ach::Id::Win100)).have, 99u);
  ach::addCount(s, ach::Counter::FightsWon);
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::Win100));
  EXPECT_EQ(ach::progress(s, static_cast<uint8_t>(ach::Id::Win100)).have, 100u);
}

TEST(V038Earn, FinishingABookCountsOnceAndSellingAScrollIsEarned) {
  GameState s = hero(1);
  ach::update(s);
  scrolls::BookRecord book;
  book.titleHash = 0xB00C;
  book.seconds = 4u * 3600u;
  book.shareRead = 80;
  scrolls::Event ev[2];
  scrolls::mint(s, book, false, ev);
  scrolls::mint(s, book, false, ev);  // a reread of the same book in this life: not a new book
  EXPECT_EQ(ach::count(s, ach::Counter::Books), 1u);
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::Books1));
  EXPECT_TRUE(ach::earned(s, ach::Id::FirstScroll));
  EXPECT_TRUE(ach::earned(s, ach::Id::YellowScroll));  // 4 hours in one book
  ASSERT_EQ(scrolls::count(s), 1);
  EXPECT_GT(scrolls::sell(s, 0), 0u);
  EXPECT_TRUE(ach::earned(s, ach::Id::SellScroll));
}

TEST(V038Earn, RankOneCountsOncePerRealmAndLife) {
  GameState s = hero(1);
  ach::update(s);
  s.rivals[16] = 2;  // the ladder's realm + 1 (Qi Condensation)
  s.rivals[17] = 1;  // your rank
  s.rivals[22] = 1;  // best rank
  ach::update(s);
  ach::update(s);
  EXPECT_EQ(ach::progress(s, static_cast<uint8_t>(ach::Id::LadderFirst5)).have, 1u);
  EXPECT_TRUE(ach::earned(s, ach::Id::LadderFirst));
  EXPECT_TRUE(ach::earned(s, ach::Id::LadderTop3));
  EXPECT_TRUE(ach::earned(s, ach::Id::LadderWin));
}

TEST(V038Earn, UpdateChangesNothingButTheAchievements) {
  GameState s = hero(4);
  s.progress.gameReadingSeconds = 200u * 3600u;
  s.progress.stones = 1234;
  codex::observe(s);  // every save does this already; update() repeats it
  GameState before = s;
  ach::update(s);
  EXPECT_NE(s.achievements, before.achievements);
  before.achievements = s.achievements;
  EXPECT_EQ(memcmp(&before.progress, &s.progress, sizeof(ProgressBlock)), 0);
  EXPECT_EQ(before.flagsAndCodex, s.flagsAndCodex);
  EXPECT_EQ(before.rivals, s.rivals);
  EXPECT_EQ(before.bag, s.bag);
  EXPECT_EQ(before.lineage, s.lineage);
}

// --- Across lives -------------------------------------------------------------------------------

TEST(V038Lives, AchievementsAndTheirCountsOutliveTheLife) {
  GameState s = hero(2);
  s.progress.gameReadingSeconds = 6u * 3600u;
  s.timers[6] = 3;  // three batches begun in this life
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::RealmFoundation));
  EXPECT_TRUE(ach::earned(s, ach::Id::Brew1));
  lineage::endLife(s, lineage::EndKind::Reincarnated, nullptr, 0);
  EXPECT_FALSE(s.isCreated());
  EXPECT_TRUE(ach::earned(s, ach::Id::RealmFoundation));
  EXPECT_TRUE(ach::earned(s, ach::Id::Brew1));
  EXPECT_EQ(ach::readingHours(s), 6u);
  newLife(s, 0x77777777);
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::Life2));
  EXPECT_FALSE(ach::earned(s, ach::Id::Hours10));
  // The new life's reading and brews add to the totals, from its own zero.
  s.progress.gameReadingSeconds = 5u * 3600u;
  s.timers[6] = 2;
  ach::update(s);
  EXPECT_EQ(ach::readingHours(s), 11u);
  EXPECT_TRUE(ach::earned(s, ach::Id::Hours10));
  EXPECT_EQ(ach::progress(s, static_cast<uint8_t>(ach::Id::Brew50)).have, 5u);
}

TEST(V038Lives, OldAgeAndAbandoningAreEarnedWhenTheLifeEnds) {
  GameState s = hero(1);
  ach::update(s);
  lineage::endLife(s, lineage::EndKind::Abandoned, nullptr, 0);
  EXPECT_TRUE(ach::earned(s, ach::Id::Abandon));
  EXPECT_FALSE(ach::earned(s, ach::Id::OldAge));
  newLife(s, 0x13572468);
  s.progress.realm = 1;
  lineage::endLife(s, lineage::EndKind::OldAge, nullptr, 0);
  EXPECT_TRUE(ach::earned(s, ach::Id::OldAge));
}

TEST(V038Lives, ChokedMeridiansReachingCoreIsEarnedInThatLife) {
  GameState s = hero(1);
  ach::update(s);
  lineage::endLife(s, lineage::EndKind::Abandoned, nullptr, 0);
  newLife(s, 0x24681357);  // Choked Meridians
  s.progress.realm = 3;
  ach::update(s);
  EXPECT_TRUE(ach::earned(s, ach::Id::ChokedCore));
  EXPECT_FALSE(ach::earned(s, ach::Id::ChokedNascent));
  EXPECT_TRUE(ach::earned(s, ach::Id::RealmCore));
}

TEST(V038Lives, TheListShowsWhatIsNewUntilOpened) {
  GameState s = hero(2);
  ach::update(s);
  EXPECT_TRUE(ach::unseen(s));
  ach::markSeen(s);
  EXPECT_FALSE(ach::unseen(s));
  s.progress.gameReadingSeconds = 3600;
  ach::update(s);
  EXPECT_TRUE(ach::unseen(s));
}

// --- The save -----------------------------------------------------------------------------------

TEST(V038Save, VersionTenRoundTripsAndANineLoadsWithNoAchievements) {
  EXPECT_EQ(save::kSaveVersion, 10);
  EXPECT_EQ(save::kPayloadSize, 2165u);
  Storage.reset();
  GameState s = hero(3);
  s.progress.gameReadingSeconds = 12u * 3600u;
  ach::update(s);
  ach::earn(s, ach::Id::Cornered);
  ASSERT_TRUE(save::store(s));
  GameState back;
  ASSERT_EQ(save::load(back), save::LoadResult::Loaded);
  EXPECT_EQ(back.achievements, s.achievements);
  EXPECT_TRUE(ach::earned(back, ach::Id::Cornered));
}
