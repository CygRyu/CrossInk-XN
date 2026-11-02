#include <Alchemy.h>
#include <Breakthrough.h>
#include <BuiltinPack.h>
#include <Exploration.h>
#include <ExploreText.h>
#include <GameState.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <HalStorage.h>
#include <Meridians.h>
#include <Simulation.h>
#include <Trials.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

// v0.2.4 (agreed with the user, session 9): meridians. 36 points opened by
// choice and paid in Qi from the bar (5% of the allowing realm's Qi); each
// gives +10% Qi rate, and limb points one channel effect.
using namespace cultivation;
using meridians::Channel;
using meridians::Check;

namespace {

GameState makeCharacter(const uint8_t realm = 1) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = 0x5EED1234;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.realm = realm;
  s.progress.stage = 0;
  s.progress.hpMax = 100;
  s.progress.hp = 100;
  s.progress.qi = QiNumber::fromU64(5000000);  // plenty
  return s;
}

void openAll(GameState& s, const Channel c) {
  for (uint8_t i = 0; i < meridians::pointsIn(c); ++i) ASSERT_TRUE(meridians::open(s, meridians::nextIn(s, c)));
}

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

VisitGains minutes(const uint32_t m) {
  VisitGains g;
  g.seconds = m * 60;
  g.tiers.fullSeconds = m * 60;
  return g;
}

}  // namespace

TEST(Meridians, PointLayoutAndOrderRules) {
  EXPECT_EQ(meridians::channelOf(0), Channel::Ren);
  EXPECT_EQ(meridians::channelOf(11), Channel::Du);
  EXPECT_EQ(meridians::channelOf(12), Channel::Lung);
  EXPECT_EQ(meridians::channelOf(35), Channel::Gallbladder);
  uint8_t total = 0;
  for (uint8_t c = 0; c < meridians::kChannels; ++c) {
    const Channel ch = static_cast<Channel>(c);
    total = static_cast<uint8_t>(total + meridians::pointsIn(ch));
    for (uint8_t i = 0; i < meridians::pointsIn(ch); ++i) {
      EXPECT_EQ(meridians::channelOf(static_cast<uint8_t>(meridians::firstPoint(ch) + i)), ch);
    }
  }
  EXPECT_EQ(total, meridians::kPoints);
  EXPECT_EQ(meridians::prerequisite(0), meridians::kPoints);  // Sea of Qi first
  EXPECT_EQ(meridians::prerequisite(1), 0);                   // Ren in order
  EXPECT_EQ(meridians::prerequisite(6), 0);                   // Du starts after Sea of Qi
  EXPECT_EQ(meridians::prerequisite(7), 6);
  EXPECT_EQ(meridians::prerequisite(12), 0);   // a limb channel's first point
  EXPECT_EQ(meridians::prerequisite(13), 12);  // its second after it
}

TEST(Meridians, AllowanceAddsUpByRealm) {
  const uint8_t expect[10] = {0, 3, 6, 9, 13, 17, 22, 26, 31, 36};
  for (uint8_t r = 0; r < 10; ++r) EXPECT_EQ(meridians::allowanceThrough(r), expect[r]) << "realm " << int(r);
  EXPECT_EQ(meridians::costRealm(1), 1);
  EXPECT_EQ(meridians::costRealm(3), 1);
  EXPECT_EQ(meridians::costRealm(4), 2);
  EXPECT_EQ(meridians::costRealm(36), 9);
  GameState body = makeCharacter(0);
  EXPECT_EQ(meridians::ready(body), 0);
  EXPECT_EQ(meridians::check(body, 0), Check::NoAllowance);  // none in Body Tempering
}

TEST(Meridians, OpeningPaysFivePercentOfTheAllowingRealmFromTheBar) {
  GameState s = makeCharacter(1);
  s.progress.qi = QiNumber::fromU64(600);
  EXPECT_EQ(meridians::costOfNext(s), QiNumber::fromU64(504));  // 5% of Qi Condensation's 10,080
  EXPECT_EQ(meridians::check(s, 1), Check::Locked);             // Origin Pass needs Sea of Qi
  EXPECT_EQ(meridians::check(s, 12), Check::Locked);            // so does every limb point
  ASSERT_TRUE(meridians::open(s, 0));
  EXPECT_EQ(s.progress.qi, QiNumber::fromU64(96));
  EXPECT_EQ(meridians::check(s, 12), Check::NotEnoughQi);
  EXPECT_FALSE(meridians::open(s, 12));
  EXPECT_EQ(s.progress.qi, QiNumber::fromU64(96));  // nothing paid
  s.progress.qi = QiNumber::fromU64(5000);
  ASSERT_TRUE(meridians::open(s, 12));
  ASSERT_TRUE(meridians::open(s, 6));
  EXPECT_EQ(meridians::ready(s), 0);
  EXPECT_EQ(meridians::check(s, 1), Check::NoAllowance);
  EXPECT_EQ(meridians::check(s, 0), Check::AlreadyOpen);

  // In a later realm, the 4th point is priced by Foundation (2,304), not by the current realm.
  s.progress.realm = 4;
  s.progress.qi = QiNumber::fromU64(100000);
  EXPECT_EQ(meridians::costOfNext(s), QiNumber::fromU64(2304));
  EXPECT_EQ(meridians::ready(s), 10);
}

TEST(Meridians, SavedInTheReservedBytesAndRoundTrips) {
  GameState s = makeCharacter(9);
  ASSERT_TRUE(meridians::open(s, 0));
  ASSERT_TRUE(meridians::open(s, 6));
  ASSERT_TRUE(meridians::open(s, 7));
  ASSERT_TRUE(meridians::open(s, 34));  // Gallbladder's first point
  for (uint8_t i = 0; i < 18; ++i) EXPECT_EQ(s.daosAndMeridians[i], 0) << "Dao byte " << int(i);
  EXPECT_EQ(s.daosAndMeridians[18], 0xC1);  // points 0, 6, 7
  EXPECT_EQ(s.daosAndMeridians[22], 0x04);  // point 34
  EXPECT_EQ(meridians::openCount(s), 4);
  Storage.reset();
  ASSERT_TRUE(save::store(s));
  GameState back;
  ASSERT_EQ(save::load(back), save::LoadResult::Loaded);
  EXPECT_EQ(meridians::openCount(back), 4);
  EXPECT_TRUE(meridians::isOpen(back, 34));
}

TEST(Meridians, QiRateGrowsTenPercentPerPointAndMultiplies) {
  GameState s = makeCharacter(9);
  const uint32_t before = qiMultiplierPermille(s);
  ASSERT_TRUE(meridians::open(s, 0));
  EXPECT_EQ(meridians::qiPermille(s), 1100u);
  EXPECT_EQ(qiMultiplierPermille(s), before * 1100u / 1000u);
  ASSERT_TRUE(meridians::open(s, 1));
  ASSERT_TRUE(meridians::open(s, 2));
  EXPECT_EQ(meridians::qiPermille(s), 1331u);
  for (uint8_t i = 0; i < meridians::kPoints; ++i) {
    if (!meridians::isOpen(s, i)) {
      ASSERT_TRUE(meridians::open(s, i)) << int(i);
    }
  }
  EXPECT_EQ(meridians::openCount(s), 36);
  EXPECT_NEAR(meridians::qiPermille(s), 30913u, 20u);  // 1.1^36

  // More Qi from the same reading.
  GameState a = makeCharacter(1);
  GameState b = makeCharacter(1);
  a.progress.qi = QiNumber{};
  b.progress.qi = QiNumber::fromU64(504);
  ASSERT_TRUE(meridians::open(b, 0));
  const QiNumber qa = absorb(a, minutes(60)).qiGained;
  const QiNumber qb = absorb(b, minutes(60)).qiGained;
  EXPECT_GT(qb, qa);
}

TEST(Meridians, LungAddsHpThatDamageIsNotReckonedFrom) {
  GameState s = makeCharacter(2);  // the next realm, Core Formation, has a tribulation
  ASSERT_TRUE(meridians::open(s, 0));
  const uint16_t baseDamage = trials::currentStrike(s).base;
  ASSERT_GT(baseDamage, 0);
  openAll(s, Channel::Lung);
  EXPECT_EQ(s.progress.hpMax, 120);
  EXPECT_EQ(s.progress.hp, 120);
  EXPECT_EQ(meridians::damageHpBase(s), 100);
  EXPECT_EQ(trials::currentStrike(s).base, baseDamage);
  openAll(s, Channel::SmallIntestine);
  EXPECT_LT(trials::currentStrike(s).base, baseDamage);
}

TEST(Meridians, ChannelEffectNumbers) {
  GameState s = makeCharacter(9);
  ASSERT_TRUE(meridians::open(s, 0));
  EXPECT_EQ(meridians::pillBonus(s, 15), 15u);
  EXPECT_EQ(meridians::regrowUnits(s, 18), 18u);
  EXPECT_EQ(meridians::healHalvesPerMinute(s), 2);
  for (uint8_t c = 2; c < meridians::kChannels; ++c) openAll(s, static_cast<Channel>(c));
  EXPECT_EQ(meridians::fightLevelBonus(s), 2);
  EXPECT_EQ(meridians::pillBonus(s, 15), 18u);    // a breakthrough pill's +15
  EXPECT_EQ(meridians::pillBonus(s, 250), 300u);  // the Qi pill's +25%
  EXPECT_EQ(meridians::pillBonus(s, 10), 12u);    // the best grade pill
  EXPECT_EQ(meridians::brewQualityBonus(s), 10);
  EXPECT_EQ(meridians::heartDemonBonus(s), 10);
  EXPECT_EQ(meridians::lightningPermille(s), 800);
  EXPECT_EQ(meridians::buyPermille(s), 900);
  EXPECT_EQ(meridians::regrowUnits(s, 18), 13u);  // 3 h -> about 2 h 10 min
  EXPECT_EQ(meridians::fightDamagePermille(s), 900);
  EXPECT_EQ(meridians::healHalvesPerMinute(s), 4);
  EXPECT_EQ(meridians::extraHerbs(s), 2);
  EXPECT_EQ(meridians::slipBonus(s), 20);
}

TEST(Meridians, EffectsReachTheGameRules) {
  GameState s = makeCharacter(4);
  s.progress.coreGrade = 1;
  ASSERT_TRUE(meridians::open(s, 0));
  const int level = explore::playerLevel(s);
  const uint8_t chance = trials::sceneChance(s);
  const uint16_t herbPrice = explore::stockPrice(s, inventory::herbFor(0));
  openAll(s, Channel::LargeIntestine);
  openAll(s, Channel::Heart);
  openAll(s, Channel::Pericardium);
  EXPECT_EQ(explore::playerLevel(s), level + 2);
  EXPECT_EQ(trials::sceneChance(s), chance + 10);
  EXPECT_EQ(explore::stockPrice(s, inventory::herbFor(0)), (herbPrice * 900u + 500u) / 1000u);

  // Kidney: HP heals 2 a minute with both points.
  s.progress.hp = 10;
  openAll(s, Channel::Kidney);
  EXPECT_EQ(absorb(s, minutes(10)).hpGained, 20);
  // Grade pills through Stomach: +6 becomes +7 with both points.
  openAll(s, Channel::Stomach);
  EXPECT_EQ(alchemy::gradePillPoints(s, inventory::Item::GradePill), 7);
}

TEST(Meridians, NamesAndChronicleLinesReadCleanly) {
  std::set<std::string> names;
  for (uint8_t i = 0; i < meridians::kPoints; ++i) {
    names.insert(meridians::pointName(i));
    EXPECT_GT(strlen(meridians::pointPlace(i)), 5u);
  }
  EXPECT_EQ(names.size(), 36u) << "every point has its own name";
  for (uint8_t c = 0; c < meridians::kChannels; ++c) {
    const Channel ch = static_cast<Channel>(c);
    EXPECT_EQ(meridians::channelEffect(ch) == nullptr, c < 2) << meridians::channelName(ch);
    EXPECT_EQ(meridians::channelElement(ch) == Element::Count, c < 2);
  }

  BuiltinPacks packs;
  GameState s = makeCharacter(1);
  std::set<std::string> lines;
  for (uint8_t i = 0; i < meridians::kPoints; ++i) {
    lifelog::Line line;
    ASSERT_TRUE(exploretext::meridianLine(line, s, 1000u + i, meridians::pointName(i), meridians::pointPlace(i),
                                          *packs.grammar));
    const std::string t = line.text;
    EXPECT_NE(t.find(meridians::pointName(i)), std::string::npos) << t;
    EXPECT_EQ(t.find_first_of("{}[]|$"), std::string::npos) << t;
    EXPECT_EQ(t.find("Spirit Stone"), std::string::npos) << t;  // life-log rule: mortal money only
    EXPECT_LT(t.size(), 200u) << t;
    lines.insert(t.substr(t.find('.') + 1));
  }
  EXPECT_GE(lines.size(), 30u) << "lines differ from point to point";
}
