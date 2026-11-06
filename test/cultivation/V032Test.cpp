#include <Exploration.h>
#include <GameState.h>
#include <HalStorage.h>
#include <Sects.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <cstdio>

// v0.3.2 "The Living World" (agreed with the user in sessions 14 and 15).

using namespace cultivation;

namespace {

GameState world(const uint32_t seed, const uint8_t realm) {
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
  return s;
}

}  // namespace

TEST(V032Pacing, AConditionAboutEveryTwentyReadingHours) {
  for (uint8_t realm : {1, 3, 6}) {
    uint32_t began = 0;
    uint32_t hours = 0;
    uint32_t onHours = 0;
    for (uint32_t w = 1; w <= 60; ++w) {
      GameState s = world(0x1000u + w * 7919u, realm);
      sects::advance(s);
      for (uint32_t h = 0; h < 2000; ++h) {
        s.progress.gameReadingSeconds += 3600;
        const sects::Steps st = sects::advance(s);
        for (uint8_t i = 0; i < st.count; ++i) {
          if (st.events[i].kind == sects::EventKind::ConditionBegan) ++began;
        }
        if (sects::condition(s) != sects::Condition::None) ++onHours;
        ++hours;
      }
    }
    const double every = static_cast<double>(hours) / began;
    printf("realm %u: a condition every %.1f reading hours, on %.0f%% of hours\n", realm, every,
           100.0 * onHours / hours);
    // User: a condition about every 15-25 reading hours (Claude tuned to 20 at realm 3).
    EXPECT_GT(every, 15.0) << +realm;
    EXPECT_LT(every, 25.0) << +realm;
    if (realm == 3) {
      EXPECT_GT(every, 18.0);
      EXPECT_LT(every, 22.0);
    }
  }
}

#include <Inventory.h>
#include <Lineage.h>
#include <Rivals.h>

namespace {

// Puts a condition on `region` directly (the save layout in Sects.h).
void setCondition(GameState& s, const sects::Condition c, const uint8_t region, const uint16_t years = 4) {
  if (!sects::founded(s)) sects::advance(s);
  const uint32_t now = s.progress.gameReadingSeconds / 3600u;
  s.living[3] = static_cast<uint8_t>(c);
  s.living[4] = region;
  s.living[5] = static_cast<uint8_t>(now + years);
  s.living[6] = static_cast<uint8_t>((now + years) >> 8);
  s.living[7] = 0;
  s.living[8] = 0;
}

}  // namespace

TEST(V032Sects, FoundedOnTheFirstVisitWithoutHistory) {
  GameState s = world(42, 1);
  s.progress.gameReadingSeconds = 30 * 3600;
  EXPECT_FALSE(sects::founded(s));
  const sects::Steps st = sects::advance(s);
  EXPECT_TRUE(sects::founded(s));
  EXPECT_EQ(st.years, 0);
  EXPECT_EQ(st.count, 0);
  for (uint8_t i = 0; i < sects::kSects; ++i) {
    const sects::Sect k = sects::sect(s, i);
    EXPECT_GE(k.strength, 3);
    EXPECT_LE(k.strength, 7);
    EXPECT_LT(k.home, explore::kRegionCount);
    EXPECT_EQ(k.story, sects::Story::None);
  }
  // Not reading: nothing moves.
  EXPECT_EQ(sects::advance(s).years, 0);
  s.progress.gameReadingSeconds += 3599;
  EXPECT_EQ(sects::advance(s).years, 0);
  s.progress.gameReadingSeconds += 1;
  EXPECT_EQ(sects::advance(s).years, 1);
}

TEST(V032Sects, TheSameReadingWritesTheSameHistory) {
  GameState a = world(77, 2);
  sects::advance(a);
  GameState b = a;
  // One visit of 30 hours, or thirty visits of one: the same world.
  a.progress.gameReadingSeconds += 30 * 3600;
  const sects::Steps one = sects::advance(a);
  uint32_t events = 0;
  for (int h = 0; h < 30; ++h) {
    b.progress.gameReadingSeconds += 3600;
    events += sects::advance(b).count;
  }
  EXPECT_EQ(a.living, b.living);
  EXPECT_EQ(one.years, 30);
  EXPECT_EQ(one.count + one.dropped, events);
}

TEST(V032Sects, StoriesRunTheirStagesAndMoveStrength) {
  uint32_t started = 0;
  uint32_t ended = 0;
  uint32_t kinds[static_cast<int>(sects::Story::Count)] = {};
  for (uint32_t w = 1; w <= 20; ++w) {
    GameState s = world(w * 131u, 3);
    sects::advance(s);
    for (int h = 0; h < 500; ++h) {
      s.progress.gameReadingSeconds += 3600;
      const sects::Steps st = sects::advance(s);
      for (uint8_t i = 0; i < st.count; ++i) {
        const sects::Event& e = st.events[i];
        if (e.kind == sects::EventKind::Started) {
          ++started;
          ++kinds[static_cast<int>(e.story)];
          if (e.story == sects::Story::Feud) {
            EXPECT_GE(e.other, 0);
            EXPECT_NE(e.other, e.sect);
          }
        }
        if (e.kind == sects::EventKind::Ended) ++ended;
      }
      for (uint8_t i = 0; i < sects::kSects; ++i) {
        const sects::Sect k = sects::sect(s, i);
        ASSERT_GE(k.strength, 1);
        ASSERT_LE(k.strength, 10);
        if (k.story != sects::Story::None) {
          ASSERT_GE(k.stages, 3);
          ASSERT_LE(k.stages, 5);
          ASSERT_LT(k.stage, k.stages);
        }
      }
    }
  }
  // About one story every 20 years per sect: 20 worlds x 500 years x 5 sects / 20.
  EXPECT_GT(started, 2000u);
  EXPECT_LT(started, 3200u);
  EXPECT_GT(ended, started - 120);
  for (int k = 1; k < static_cast<int>(sects::Story::Count); ++k) EXPECT_GT(kinds[k], 50u) << k;
}

TEST(V032Conditions, OnlyInOpenedRegionsOneAtATimeForFourHours) {
  // Body Tempering: no region is open, so no condition, but stories still run.
  GameState body = world(5, 0);
  sects::advance(body);
  uint32_t stories = 0;
  for (int h = 0; h < 400; ++h) {
    body.progress.gameReadingSeconds += 3600;
    const sects::Steps st = sects::advance(body);
    for (uint8_t i = 0; i < st.count; ++i) {
      EXPECT_NE(st.events[i].kind, sects::EventKind::ConditionBegan);
      if (st.events[i].kind == sects::EventKind::Started) ++stories;
    }
    EXPECT_EQ(sects::condition(body), sects::Condition::None);
  }
  EXPECT_GT(stories, 30u);

  GameState s = world(9, 2);
  sects::advance(s);
  uint32_t began = 0;
  uint32_t beganAt = 0;
  for (uint32_t h = 1; h <= 600; ++h) {
    s.progress.gameReadingSeconds += 3600;
    const sects::Steps st = sects::advance(s);
    for (uint8_t i = 0; i < st.count; ++i) {
      const sects::Event& e = st.events[i];
      if (e.kind == sects::EventKind::ConditionBegan) {
        ++began;
        beganAt = h;
        EXPECT_TRUE(explore::unlocked(s, e.region));
        EXPECT_NE(e.condition, sects::Condition::None);
        EXPECT_EQ(e.condition, sects::conditionOf(e.story));
      }
      if (e.kind == sects::EventKind::ConditionEnded) {
        EXPECT_EQ(h - beganAt, sects::kConditionYears);
      }
    }
    if (sects::condition(s) != sects::Condition::None) {
      EXPECT_LE(sects::conditionYearsLeft(s), sects::kConditionYears);
      EXPECT_GE(sects::conditionYearsLeft(s), 1);
    }
  }
  EXPECT_GT(began, 15u);
  EXPECT_EQ(sects::conditionOf(sects::Story::Succession), sects::Condition::None);
}

TEST(V032Conditions, SectWarRaisesLevelsStonesAndCultivators) {
  GameState s = world(11, 3);
  setCondition(s, sects::Condition::SectWar, 1);
  EXPECT_EQ(sects::enemyLevelBonus(s, 1), 2);
  EXPECT_EQ(sects::enemyLevelBonus(s, 0), 0);
  EXPECT_EQ(sects::fightStonesPermille(s, 1), 1500);
  EXPECT_EQ(sects::fightStonesPermille(s, 2), 1000);
  EXPECT_EQ(sects::favouredEnemy(s, 1), static_cast<uint8_t>(explore::Enemy::Cultivator));
  EXPECT_EQ(sects::favouredEnemy(s, 0), 0xFF);
}

TEST(V032Conditions, ACampCallsBanditsAndBreaksOnTheThirdWin) {
  GameState s = world(12, 3);
  setCondition(s, sects::Condition::BanditCamp, 2);
  EXPECT_EQ(sects::favouredEnemy(s, 2), static_cast<uint8_t>(explore::Enemy::Bandit));
  EXPECT_FALSE(sects::banditWon(s, 1).broken);  // another region
  EXPECT_FALSE(sects::banditWon(s, 2).broken);
  EXPECT_FALSE(sects::banditWon(s, 2).broken);
  const sects::CampResult r = sects::banditWon(s, 2);
  EXPECT_TRUE(r.broken);
  EXPECT_EQ(r.stones, 150u);
  EXPECT_EQ(sects::condition(s), sects::Condition::None);
  EXPECT_FALSE(sects::banditWon(s, 2).broken);
}

TEST(V032Conditions, RandomFightsLeanTowardTheFavouredEnemy) {
  // Walk region 2 (gorge: bandits 45%) with and without a camp and count the waiting enemies.
  auto share = [](const bool camp) {
    uint32_t fights = 0;
    uint32_t bandits = 0;
    for (uint32_t w = 1; w <= 40; ++w) {
      GameState s = world(w * 1009u, 3);
      if (camp) setCondition(s, sects::Condition::BanditCamp, 2, 60000);
      const explore::Layout l = explore::layoutOf(s, 2);
      explore::enter(s, l, 2);
      for (int i = 0; i < 200; ++i) {
        s.progress.stamina = 6;
        s.progress.hp = s.progress.hpMax;
        uint8_t n[4];
        const uint8_t here = explore::position(s, l, 2);
        const uint8_t k = explore::neighbours(here, n);
        uint8_t to = here;
        for (uint8_t j = 0; j < k; ++j) {
          if (explore::walkable(l, n[(i + j) % k])) {
            to = n[(i + j) % k];
            break;
          }
        }
        if (to == here) break;
        const explore::StepResult r = explore::step(s, l, 2, to);
        if (r.meeting) explore::meet(s, l, explore::MeetChoice::WalkOn);
        if (r.fight) {
          const explore::Fight f = explore::pendingFight(s, l);
          if (f.kind == explore::FightKind::Beast && f.nemesis < 0 && f.returns == 0) {
            ++fights;
            if (f.enemy == explore::Enemy::Bandit) ++bandits;
          }
          explore::fight(s, l, explore::FightChoice::Fight);
          if (sects::condition(s) == sects::Condition::None && camp)
            setCondition(s, sects::Condition::BanditCamp, 2, 60000);
        }
      }
    }
    return fights == 0 ? 0.0 : static_cast<double>(bandits) / fights;
  };
  const double without = share(false);
  const double with = share(true);
  printf("bandit share: %.2f without a camp, %.2f with one\n", without, with);
  // One reroll: a share w becomes w(2 - w), 0.16 -> about 0.29.
  EXPECT_GT(with, without * 1.6);
}

TEST(V032Conditions, TournamentFairTideRuinsAndBlight) {
  GameState s = world(13, 4);
  setCondition(s, sects::Condition::Tournament, 0);
  EXPECT_EQ(sects::meetPercent(s, 0, explore::kMeetPercent), 12);
  EXPECT_EQ(sects::meetPercent(s, 1, explore::kMeetPercent), 6);
  EXPECT_TRUE(sects::tournamentOn(s));
  EXPECT_EQ(rivals::challengeBout(s).stones, 2u * 20u * (s.progress.realm + 1u));

  setCondition(s, sects::Condition::TempleFair, 1);
  EXPECT_FALSE(sects::tournamentOn(s));
  EXPECT_EQ(rivals::challengeBout(s).stones, 20u * (s.progress.realm + 1u));
  EXPECT_EQ(sects::meetPercent(s, 1, 6), 12);
  EXPECT_EQ(sects::buyPermille(s, 1, false), 850);
  EXPECT_EQ(sects::buyPermille(s, 1, true), 850);
  EXPECT_EQ(sects::sellPermille(s, 1, true), 1000);

  setCondition(s, sects::Condition::BeastTide, 2);
  EXPECT_EQ(sects::encounterPercent(s, 2, 12), 18);
  EXPECT_EQ(sects::encounterPercent(s, 1, 12), 12);
  EXPECT_EQ(sects::beastLootPercent(s, 2, 25), 50);
  EXPECT_EQ(sects::beastLootPercent(s, 2, 50), 100);
  EXPECT_EQ(sects::beastLootPercent(s, 2, 60), 100);

  setCondition(s, sects::Condition::Ruins, 3);
  EXPECT_EQ(sects::ruinBonus(s, 3), 15);
  EXPECT_EQ(sects::ruinBonus(s, 2), 0);

  setCondition(s, sects::Condition::Blight, 0);
  EXPECT_TRUE(sects::herbsHalved(s, 0));
  EXPECT_EQ(sects::buyPermille(s, 0, true), 1500);
  EXPECT_EQ(sects::buyPermille(s, 0, false), 1000);
  const inventory::Item herb = inventory::herbFor(0);
  EXPECT_EQ(explore::sellPrice(s, 0, herb), inventory::sellPrice(herb) * 3 / 2);
  EXPECT_EQ(explore::sellPrice(s, 1, herb), inventory::sellPrice(herb));
  EXPECT_EQ(explore::sellPrice(s, 0xFF, herb), inventory::sellPrice(herb));
}

TEST(V032Conditions, MerchantsPriceTheFairAndTheBlight) {
  // Find a region layout with a merchant.
  for (uint32_t w = 1; w < 200; ++w) {
    GameState s = world(w * 271u, 3);
    for (uint8_t region = 0; region < 3; ++region) {
      const explore::Layout l = explore::layoutOf(s, region);
      for (uint8_t t = 0; t < explore::kTiles; ++t) {
        if (l.tiles[t] != explore::Tile::Merchant) continue;
        const explore::Stock plain = explore::stockFor(s, l, region, t);
        setCondition(s, sects::Condition::TempleFair, region);
        const explore::Stock fair = explore::stockFor(s, l, region, t);
        setCondition(s, sects::Condition::Blight, region);
        const explore::Stock blight = explore::stockFor(s, l, region, t);
        ASSERT_EQ(plain.count, fair.count);
        for (uint8_t i = 0; i < plain.count; ++i) {
          const uint32_t base = explore::linePrice(s, plain.lines[i]);
          EXPECT_EQ(plain.lines[i].price, 0);
          EXPECT_EQ(explore::linePrice(s, fair.lines[i]), (base * 850 + 500) / 1000) << +i;
          if (inventory::isHerb(plain.lines[i].item)) {
            EXPECT_EQ(explore::linePrice(s, blight.lines[i]), (base * 1500 + 500) / 1000);
          } else {
            EXPECT_EQ(explore::linePrice(s, blight.lines[i]), base);
          }
        }
        // Buying the herb line at the blight price.
        s.progress.stones = 10000;
        const uint32_t before = s.progress.stones;
        ASSERT_TRUE(explore::buy(s, l, region, t, 0, 2));
        EXPECT_EQ(before - s.progress.stones, 2u * explore::linePrice(s, blight.lines[0]));
        return;
      }
    }
  }
  FAIL() << "no merchant found";
}

TEST(V032Conditions, ConditionsEndWithTheirTime) {
  GameState s = world(14, 3);
  setCondition(s, sects::Condition::BeastTide, 0, 2);
  s.progress.gameReadingSeconds += 3600;
  sects::advance(s);
  EXPECT_EQ(sects::conditionYearsLeft(s), 1);
  s.progress.gameReadingSeconds += 3600;
  const sects::Steps st = sects::advance(s);
  EXPECT_EQ(sects::condition(s), sects::Condition::None);
  bool ended = false;
  for (uint8_t i = 0; i < st.count; ++i) ended |= st.events[i].kind == sects::EventKind::ConditionEnded;
  EXPECT_TRUE(ended);
}

TEST(V032Sects, AHugeBingeStepsAtMostTheCap) {
  GameState s = world(15, 2);
  sects::advance(s);
  s.progress.gameReadingSeconds += 5000u * 3600u;
  const sects::Steps st = sects::advance(s);
  EXPECT_EQ(st.years, sects::kMaxStepsPerVisit);
  EXPECT_EQ(st.count, sects::kMaxEvents);
  EXPECT_GT(st.dropped, 0);
  EXPECT_EQ(sects::advance(s).years, 0);
}

TEST(V032Sects, RumorFollowsALiveStoryOrTheCondition) {
  GameState s = world(16, 2);
  EXPECT_EQ(sects::rumorSect(s, 1), -1);
  sects::advance(s);
  EXPECT_EQ(sects::rumorSect(s, 1), -1);  // no story yet
  for (int h = 0; h < 60 && sects::rumorSect(s, 1) < 0; ++h) {
    s.progress.gameReadingSeconds += 3600;
    sects::advance(s);
  }
  const int r = sects::rumorSect(s, 1);
  ASSERT_GE(r, 0);
  EXPECT_NE(sects::sect(s, static_cast<uint8_t>(r)).story, sects::Story::None);
}

TEST(V032Save, LivingRoundTripsAndOldSavesLoadEmpty) {
  EXPECT_EQ(save::kSaveVersion, 8);
  EXPECT_EQ(save::kPayloadSize, 1621u);
  GameState s = world(17, 2);
  sects::advance(s);
  s.living[63] = 0xA5;
  ASSERT_TRUE(save::store(s, "/v032.bin"));
  GameState back;
  ASSERT_EQ(save::load(back, "/v032.bin"), save::LoadResult::Loaded);
  EXPECT_EQ(back.living, s.living);

  GameState v7 = world(18, 2);
  v7.living.fill(0x77);
  ASSERT_TRUE(save::store(v7, "/v7.bin"));
  auto& bytes = *Storage.bytesOf("/v7.bin");
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV7);
  bytes[2] = 7;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV7);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV7 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV7);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));
  GameState old;
  ASSERT_EQ(save::load(old, "/v7.bin"), save::LoadResult::Loaded);
  EXPECT_TRUE(old.isCreated());
  for (const uint8_t b : old.living) EXPECT_EQ(b, 0);
  EXPECT_FALSE(sects::founded(old));
}

TEST(V032Save, ANewLifeHasANewWorld) {
  GameState s = world(19, 3);
  sects::advance(s);
  s.progress.gameReadingSeconds += 40 * 3600;
  sects::advance(s);
  ASSERT_TRUE(sects::founded(s));
  lineage::endLife(s, lineage::EndKind::Abandoned, nullptr, 0);
  EXPECT_FALSE(sects::founded(s));
  for (const uint8_t b : s.living) EXPECT_EQ(b, 0);
}

// --- Text ------------------------------------------------------------------------------------------

#include <BuiltinPack.h>
#include <ExploreText.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <LifeLog.h>
#include <PackStore.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
struct WorldPack {
  std::string text;
  std::vector<PackSection> storage;
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;
  WorldPack() : text(builtinPackText(), builtinPackSize()), storage(512) {
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    PackParser parser(index, 0);
    parsePackText(text.data(), text.size(), parser);
    reader.setSource(0, text.data(), text.size());
    index.applyReplacements();
    grammar = std::make_unique<Grammar>(index, reader);
  }
};
bool cleanText(const std::string& s) { return s.find_first_of("{}[]|$") == std::string::npos; }
bool sentencesCapitalised(const std::string& s) {
  for (size_t p = s.find(". "); p != std::string::npos; p = s.find(". ", p + 2)) {
    if (p + 2 < s.size() && s[p + 2] >= 'a' && s[p + 2] <= 'z') return false;
  }
  return true;
}
const char* const kStories[] = {"feud", "succession", "bandits", "tournament", "fair", "tide", "ruins", "blight"};
const char* const kConds[] = {"war", "camp", "tournament", "fair", "tide", "ruins", "blight"};
}  // namespace

TEST(V032Text, FiveDifferentSectNames) {
  WorldPack pack;
  for (uint32_t w = 1; w <= 50; ++w) {
    GameState s = world(w * 13u, 2);
    std::set<std::string> names;
    std::set<std::string> firstWords;
    for (uint8_t i = 0; i < sects::kSects; ++i) {
      char name[exploretext::kNameSize];
      ASSERT_TRUE(exploretext::sectName(s, i, *pack.grammar, name, sizeof(name)));
      const std::string n = name;
      EXPECT_EQ(n.rfind("the ", 0), 0u) << name;
      names.insert(n);
      firstWords.insert(n.substr(4, n.find(' ', 4) - 4));
    }
    EXPECT_EQ(names.size(), 5u) << w;
    EXPECT_EQ(firstWords.size(), 5u) << w;  // easy to tell apart
  }
}

TEST(V032Text, EveryWorldLineAndRumorExpands) {
  WorldPack pack;
  GameState s = world(99, 3);
  sects::advance(s);
  std::set<std::string> seen;
  uint32_t lines = 0;
  uint32_t rumors = 0;
  uint32_t longRumors = 0;  // over about three lines of the Cultivator screen
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    std::vector<std::string> keys;
    for (const char* k : kStories) {
      for (const char* end : {"start", "good", "bad"}) keys.push_back(std::string("world.line.") + k + "." + end);
    }
    for (const char* c : kConds) {
      for (const char* end : {"start", "end"}) keys.push_back(std::string("world.line.cond.") + c + "." + end);
    }
    keys.emplace_back("world.line.cond.camp.broken");
    for (const std::string& key : keys) {
      lifelog::Line line;
      ASSERT_TRUE(exploretext::worldLine(line, s, key.c_str(), seed * 7919u, static_cast<uint8_t>(seed % 3),
                                         static_cast<uint8_t>(seed % 5), static_cast<int8_t>((seed + 2) % 5),
                                         *pack.grammar))
          << key;
      const std::string t = line.text;
      EXPECT_TRUE(cleanText(t)) << key << ": " << t;
      EXPECT_TRUE(t[0] >= 'A' && t[0] <= 'Z') << t;
      EXPECT_TRUE(sentencesCapitalised(t)) << t;
      EXPECT_EQ(t.back(), '.') << t;
      EXPECT_LT(t.size(), 180u) << t;
      seen.insert(t);
      ++lines;
    }
    for (const char* k : kStories) {
      for (const char* stage : {"early", "late"}) {
        const std::string key = std::string("world.rumor.") + k + "." + stage;
        char out[256];
        ASSERT_TRUE(exploretext::worldRumor(s, key.c_str(), seed * 104729u, static_cast<uint8_t>(seed % 3),
                                            static_cast<uint8_t>(seed % 5), static_cast<int8_t>((seed + 1) % 5),
                                            *pack.grammar, out, sizeof(out)))
            << key;
        const std::string t = out;
        EXPECT_TRUE(cleanText(t)) << t;
        EXPECT_TRUE(t[0] >= 'A' && t[0] <= 'Z') << t;
        EXPECT_TRUE(sentencesCapitalised(t)) << t;
        EXPECT_EQ(t.back(), '.') << t;
        EXPECT_LT(t.size(), 200u) << t;  // rumor_ is 200 bytes
        if (t.size() > 170) ++longRumors;
        ++rumors;
      }
    }
  }
  EXPECT_GT(seen.size(), lines / 3);
  printf("world rumors over 170 bytes: %u of %u\n", longRumors, rumors);
  EXPECT_LT(longRumors * 10, rumors);  // under one in ten
}

TEST(V032Conditions, WinningTheThirdBanditFightBreaksTheCamp) {
  for (uint32_t w = 1; w <= 60; ++w) {
    GameState s = world(w * 7u + 3u, 3);
    s.progress.realm = 9;  // strong enough to win
    s.progress.hpMax = 2000;
    setCondition(s, sects::Condition::BanditCamp, 2, 60000);
    s.living[7] = 2;  // two bandits beaten there already
    const explore::Layout l = explore::layoutOf(s, 2);
    explore::enter(s, l, 2);
    for (int i = 0; i < 300; ++i) {
      s.progress.stamina = 6;
      s.progress.hp = s.progress.hpMax;
      uint8_t n[4];
      const uint8_t here = explore::position(s, l, 2);
      const uint8_t k = explore::neighbours(here, n);
      uint8_t to = here;
      for (uint8_t j = 0; j < k; ++j) {
        if (explore::walkable(l, n[(i + j) % k])) {
          to = n[(i + j) % k];
          break;
        }
      }
      if (to == here) break;
      const explore::StepResult r = explore::step(s, l, 2, to);
      if (r.meeting) explore::meet(s, l, explore::MeetChoice::WalkOn);
      if (!r.fight) continue;
      const explore::Fight f = explore::pendingFight(s, l);
      const uint32_t stones = s.progress.stones;
      const uint16_t tonics = inventory::count(s, inventory::Item::TonicGreater);
      const explore::FightResult res = explore::fight(s, l, explore::FightChoice::Fight);
      if (!res.won || f.enemy != explore::Enemy::Bandit) {
        EXPECT_FALSE(res.campBroken);
        continue;
      }
      EXPECT_TRUE(res.campBroken);
      EXPECT_EQ(res.campSect, 0);
      EXPECT_EQ(sects::condition(s), sects::Condition::None);
      EXPECT_GE(s.progress.stones - stones, 150u);
      EXPECT_EQ(inventory::count(s, inventory::Item::TonicGreater), tonics + 1);
      return;
    }
  }
  FAIL() << "no bandit fight won";
}
