#include <Alchemy.h>
#include <Chronicle.h>
#include <Codex.h>
#include <Daily.h>
#include <Daos.h>
#include <Exploration.h>
#include <GameState.h>
#include <HalStorage.h>
#include <Inventory.h>
#include <Lineage.h>
#include <Rivals.h>
#include <Rng.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <set>

// v0.3.0 "A New Life" (agreed with the user in session 14): the lineage
// (imprints, perks, the Qi bonus), lifespan and the end of a life, the codex
// and mementos.
using namespace cultivation;
using lineage::EndKind;
using lineage::Perk;

namespace {

CharacterDraft draftFor(const char* name, const uint32_t seed = 0x5EED1234,
                        const Origin origin = Origin::FarmersChild) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "%s", name);
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.destiny.element = Element::Water;
  d.origin = origin;
  d.worldSeed = seed;
  return d;
}

GameState character(const uint8_t realm = 0) {
  GameState s;
  StatsSample now;
  now.today = 9802;
  createCharacter(s, draftFor("Lin Yue"), now);
  s.progress.realm = realm;
  return s;
}

void newLife(GameState& s, const char* name, const uint32_t seed = 0x0BADF00D) {
  StatsSample now;
  now.today = 9900;
  createCharacter(s, draftFor(name, seed, Origin::HerbalistApprentice), now);
}

scrolls::Scroll scroll(const uint32_t hash, const uint8_t hours, const uint8_t comprehension,
                       const scrolls::Kind kind = scrolls::Kind::Sword) {
  scrolls::Scroll sc;
  sc.titleHash = hash;
  sc.hours = hours;
  sc.kind = kind;
  sc.element = Element::Fire;
  sc.comprehension = comprehension;
  return sc;
}

void addScroll(GameState& s, const scrolls::Scroll& sc) {
  const uint8_t n = scrolls::count(s);
  scrolls::put(s, n, sc);
  s.techniqueScrolls[0] = static_cast<uint8_t>(n + 1);
}

}  // namespace

// --- Save v7 ---------------------------------------------------------------------------------------

TEST(V030Save, LineageRoundTripsAndOldSavesLoadEmpty) {
  EXPECT_EQ(save::kSaveVersion, 7);
  EXPECT_EQ(save::kPayloadSize, 1557u);
  GameState s = character(3);
  s.lineage[lineage::kBestRealm] = 5;
  s.lineage[71] = 0xA5;
  ASSERT_TRUE(save::store(s, "/v030.bin"));
  GameState back;
  ASSERT_EQ(save::load(back, "/v030.bin"), save::LoadResult::Loaded);
  EXPECT_EQ(back.lineage, s.lineage);
  EXPECT_EQ(lineage::bestRealm(back), 4);

  // A v6 save (1485 bytes of payload) loads with the lineage zeroed.
  GameState v6 = character(2);
  v6.lineage.fill(0x77);
  ASSERT_TRUE(save::store(v6, "/v6.bin"));
  auto& bytes = *Storage.bytesOf("/v6.bin");
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV6);
  bytes[2] = 6;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV6);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV6 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV6);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));
  GameState old;
  ASSERT_EQ(save::load(old, "/v6.bin"), save::LoadResult::Loaded);
  EXPECT_TRUE(old.isCreated());
  for (const uint8_t b : old.lineage) EXPECT_EQ(b, 0);
  EXPECT_EQ(lineage::qiBonusPermille(old), 0);
  EXPECT_EQ(lineage::bestRealm(old), -1);
}

// --- Qi bonus ------------------------------------------------------------------------------------------

TEST(V030Lineage, QiBonusIsThreePercentARealmPastFoundation) {
  EXPECT_EQ(lineage::qiBonusFor(-1), 0);
  EXPECT_EQ(lineage::qiBonusFor(1), 0);
  EXPECT_EQ(lineage::qiBonusFor(2), 0);   // Foundation gives nothing
  EXPECT_EQ(lineage::qiBonusFor(3), 30);  // Core Formation +3%
  EXPECT_EQ(lineage::qiBonusFor(4), 60);
  EXPECT_EQ(lineage::qiBonusFor(9), 210);  // +21% at most
  EXPECT_EQ(lineage::qiBonusFor(12), 210);

  GameState s = character(0);
  const uint32_t before = qiMultiplierPermille(s);
  s.lineage[lineage::kBestRealm] = 5;  // Nascent Soul in a past life: +6%
  EXPECT_EQ(qiMultiplierPermille(s), before * 1060 / 1000);
}

TEST(V030Lineage, BonusFollowsTheBestRealmNeverAddsUp) {
  GameState s = character(4);  // Nascent Soul
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  EXPECT_EQ(lineage::qiBonusPermille(s), 60);
  newLife(s, "Second");
  s.progress.realm = 2;  // only Foundation this time
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  EXPECT_EQ(lineage::bestRealm(s), 4);
  EXPECT_EQ(lineage::qiBonusPermille(s), 60);  // the best, not a sum
  EXPECT_EQ(lineage::livesEnded(s), 2);
}

// --- Imprints -------------------------------------------------------------------------------------------

TEST(V030Lineage, ImprintsPerLifeFromRealmDaosAndNewCodexEntries) {
  EXPECT_EQ(lineage::realmImprints(0), 0);
  EXPECT_EQ(lineage::realmImprints(1), 0);
  EXPECT_EQ(lineage::realmImprints(2), 2);
  EXPECT_EQ(lineage::realmImprints(4), 7);
  EXPECT_EQ(lineage::realmImprints(9), 28);
  GameState s = character(3);  // Core Formation: 4
  codex::observe(s);           // realms 0-3, talent, origin, element: 7 entries at most
  const uint16_t entries = codex::count(s);
  EXPECT_EQ(lineage::imprintsFor(s), 4 + entries / 8);
  for (uint8_t m = 0; m < 16; ++m) codex::mark(s, codex::kMementos + m);  // 16 more: +2
  EXPECT_EQ(lineage::imprintsFor(s), 4 + (entries + 16) / 8);

  // The next life counts only entries that are new to it (per life, user).
  const lineage::EndResult r = lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  EXPECT_EQ(r.imprints, 4 + (entries + 16) / 8);
  EXPECT_EQ(lineage::imprints(s), r.imprints);
  newLife(s, "Second");
  s.progress.realm = 3;
  EXPECT_EQ(lineage::imprintsFor(s), 4);  // the same realms again earn the realm part only
}

TEST(V030Lineage, ReincarnatingAgainAtFoundationStillEarns) {
  // The user chose per-life imprints: a cosy game, no need to always reach further.
  GameState s = character(2);
  uint16_t total = 0;
  for (int life = 0; life < 3; ++life) {
    total = static_cast<uint16_t>(total + lineage::endLife(s, EndKind::Reincarnated, nullptr, 0).imprints);
    newLife(s, "Again", 0x1000u + life);
    s.progress.realm = 2;
  }
  EXPECT_GE(total, 6);
  EXPECT_EQ(lineage::imprintsEarned(s), total);
}

TEST(V030Lineage, AbandoningEarnsNothingButKeepsTheLineage) {
  GameState s = character(4);
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  const uint16_t kept = lineage::imprints(s);
  newLife(s, "Unlucky");
  s.progress.realm = 1;
  const lineage::EndResult r = lineage::endLife(s, EndKind::Abandoned, nullptr, 0);
  EXPECT_EQ(r.imprints, 0);
  EXPECT_EQ(lineage::imprints(s), kept);
  EXPECT_EQ(lineage::lastEnd(s), EndKind::Abandoned);
  EXPECT_EQ(lineage::qiBonusPermille(s), 60);
  EXPECT_TRUE(lineage::canAbandon(character(0)));
  EXPECT_FALSE(lineage::canReincarnate(character(1)));
  EXPECT_TRUE(lineage::canReincarnate(character(2)));
}

// --- Perks ----------------------------------------------------------------------------------------------

TEST(V030Lineage, PerksCostImprintsAndStopAtTheirMaxRank) {
  GameState s = character(0);
  EXPECT_FALSE(lineage::canBuy(s, Perk::HiddenPurse));
  s.lineage[lineage::kImprints] = 45;  // enough for every perk at its top rank
  uint16_t spent = 0;
  for (uint8_t p = 0; p < lineage::kPerkCount; ++p) {
    const Perk perk = static_cast<Perk>(p);
    while (lineage::buy(s, perk)) spent = static_cast<uint16_t>(spent + lineage::perkInfo(perk).cost);
    EXPECT_EQ(lineage::rank(s, perk), lineage::perkInfo(perk).maxRank);
  }
  EXPECT_EQ(spent, 45);
  EXPECT_EQ(lineage::imprints(s), 0);
  EXPECT_EQ(lineage::fatedLuck(s), 15);
  EXPECT_EQ(lineage::extraRerolls(s), 5);
}

TEST(V030Lineage, NewLifeGetsPurseRecipesOldRoadsAndEchoScrolls) {
  GameState s = character(3);
  s.lineage[lineage::kImprints] = 40;
  for (const Perk p : {Perk::HiddenPurse, Perk::HiddenPurse, Perk::Recipes, Perk::OldRoads, Perk::Echo, Perk::Echo}) {
    ASSERT_TRUE(lineage::buy(s, p));
  }
  addScroll(s, scroll(0x1111, 30, 100));                           // Saint, Perfected
  addScroll(s, scroll(0x2222, 5, scrolls::kSacredComprehension));  // Yellow, Sacred
  addScroll(s, scroll(0x3333, 1, 60));
  const uint8_t echo[] = {0, 1, 2};  // three asked, Echo rank 2 keeps two
  const lineage::EndResult r = lineage::endLife(s, EndKind::Reincarnated, echo, 3);
  EXPECT_EQ(r.echoKept, 2);
  EXPECT_FALSE(s.isCreated());
  EXPECT_EQ(scrolls::count(s), 0);

  newLife(s, "Heir");  // Herbalist: knows the Qi Gathering recipe, Remembered Recipes adds one
  EXPECT_EQ(lifeNumberOf(s), 2);
  EXPECT_EQ(s.progress.stones, originBonus(Origin::HerbalistApprentice).stones + 100u);
  EXPECT_EQ(alchemy::knownCount(s), 2);
  ASSERT_EQ(scrolls::count(s), 2);
  EXPECT_EQ(scrolls::get(s, 0).titleHash, 0x1111u);
  EXPECT_EQ(scrolls::qualityFor(scrolls::get(s, 0).comprehension), scrolls::Quality::Refined);
  EXPECT_EQ(scrolls::qualityFor(scrolls::get(s, 1).comprehension), scrolls::Quality::Perfected);
  EXPECT_FALSE(scrolls::get(s, 0).equipped());
  const explore::Layout l = explore::layoutOf(s, 0);
  EXPECT_EQ(explore::chartedCount(s, 0), l.walkable);
  // Carried once: a later life does not get them again.
  s.progress.realm = 2;
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  newLife(s, "Third");
  EXPECT_EQ(scrolls::count(s), 0);
}

TEST(V030Lineage, AbandoningCarriesNoEchoScroll) {
  GameState s = character(2);
  s.lineage[lineage::kImprints] = 4;
  ASSERT_TRUE(lineage::buy(s, Perk::Echo));
  addScroll(s, scroll(0x1111, 30, 100));
  const uint8_t echo[] = {0};
  EXPECT_EQ(lineage::endLife(s, EndKind::Abandoned, echo, 1).echoKept, 0);
  newLife(s, "Plain");
  EXPECT_EQ(scrolls::count(s), 0);
}

TEST(V030Lineage, EchoDropsOneQualityStep) {
  EXPECT_EQ(scrolls::qualityFor(lineage::echoComprehension(scrolls::kSacredComprehension)),
            scrolls::Quality::Perfected);
  EXPECT_EQ(scrolls::qualityFor(lineage::echoComprehension(99)), scrolls::Quality::Refined);
  EXPECT_EQ(scrolls::qualityFor(lineage::echoComprehension(92)), scrolls::Quality::Complete);
  EXPECT_EQ(scrolls::qualityFor(lineage::echoComprehension(80)), scrolls::Quality::Incomplete);
  EXPECT_EQ(scrolls::qualityFor(lineage::echoComprehension(60)), scrolls::Quality::Fragmentary);
  EXPECT_EQ(lineage::echoComprehension(30), 30);
}

TEST(V030Lineage, FamiliarFaceBringsTheRivalBack) {
  GameState s = character(2);
  const uint32_t first = rivals::rivalSeed(s);
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  newLife(s, "Without");
  EXPECT_NE(rivals::rivalSeed(s), first);  // a new world, a new rival
  s.progress.realm = 2;
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  uint32_t second = 0;
  for (int i = 0; i < 4; ++i) second |= static_cast<uint32_t>(s.lineage[lineage::kRivalSeed + i]) << (8 * i);
  EXPECT_EQ(lineage::previousLife(s), 2);
  s.lineage[lineage::kImprints] = 2;
  ASSERT_TRUE(lineage::buy(s, Perk::FamiliarFace));  // bought in the shop after the life ended
  newLife(s, "With", 0x7777);
  EXPECT_EQ(rivals::rivalSeed(s), second);
  // And again: the same rival stays.
  const uint32_t carried = rivals::rivalSeed(s);
  s.progress.realm = 2;
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  newLife(s, "Again", 0x8888);
  EXPECT_EQ(rivals::rivalSeed(s), carried);
}

TEST(V030Lineage, EndLifeRecordsTheLastLifeAndKeepsCodexAndGuide) {
  GameState s = character(4);
  s.progress.ageMonths = 231 * 12 + 5;
  s.flagsAndCodex[kGuideSeenByte] = kGuideSeenBit;
  s.flagsAndCodex[0] = 0xFF;  // known recipes: per life
  codex::mark(s, codex::kMementos + 7);
  const lineage::EndResult r = lineage::endLife(s, EndKind::OldAge, nullptr, 0);
  EXPECT_EQ(r.lifeNumber, 1);
  char name[32];
  lineage::previousName(s, name, sizeof(name));
  EXPECT_STREQ(name, "Lin Yue");
  EXPECT_EQ(lineage::previousAge(s), 231);
  EXPECT_EQ(lineage::previousRealm(s), 4);
  EXPECT_EQ(lineage::previousLife(s), 1);
  EXPECT_EQ(lineage::lastEnd(s), EndKind::OldAge);
  EXPECT_TRUE(lineage::chronicleMovePending(s));
  EXPECT_TRUE(codex::hasMemento(s, 7));
  EXPECT_TRUE(codex::has(s, codex::kRealms + 4));
  EXPECT_EQ(s.flagsAndCodex[kGuideSeenByte] & kGuideSeenBit, kGuideSeenBit);
  EXPECT_EQ(s.flagsAndCodex[0], 0);
  EXPECT_EQ(s.progress.realm, 0);
  newLife(s, "Kept");
  EXPECT_TRUE(codex::hasMemento(s, 7));
  EXPECT_EQ(s.flagsAndCodex[kGuideSeenByte] & kGuideSeenBit, kGuideSeenBit);
  EXPECT_EQ(lineage::bestRealm(s), 4);
}

// --- Lifespan ----------------------------------------------------------------------------------------

TEST(V030Lifespan, WarningsAtEightyAndNinetyFivePercent) {
  using lineage::Age;
  EXPECT_EQ(lineage::ageStateAt(95, 120), Age::Fine);
  EXPECT_EQ(lineage::ageStateAt(96, 120), Age::Greying);
  EXPECT_EQ(lineage::ageStateAt(114, 120), Age::Failing);
  EXPECT_EQ(lineage::ageStateAt(119, 120), Age::Failing);
  EXPECT_EQ(lineage::ageStateAt(120, 120), Age::Over);
  GameState s = character(1);
  s.progress.ageMonths = 100 * 12;
  EXPECT_EQ(lineage::yearsLeft(s), 20);
  EXPECT_EQ(lineage::ageState(s), Age::Greying);
  s.progress.realm = 2;  // a breakthrough raises the lifespan
  EXPECT_EQ(lineage::ageState(s), Age::Fine);
  EXPECT_EQ(lineage::yearsLeft(s), 100);
}

// --- Codex ---------------------------------------------------------------------------------------------

TEST(V030Codex, ObserveMarksWhatTheLifeShows) {
  GameState s = character(3);
  addScroll(s, scroll(0x1234, 10, scrolls::kSacredComprehension, scrolls::Kind::Manual));
  inventory::add(s, inventory::Item::TonicGreater, 1);
  inventory::add(s, inventory::gradePill(1, inventory::Quality::High), 1);
  codex::observe(s);
  EXPECT_EQ(codex::groupFound(s, codex::Group::Realms), 4);
  EXPECT_EQ(codex::groupFound(s, codex::Group::Talents), 1);
  EXPECT_TRUE(codex::has(s, codex::kTalents + 2));
  EXPECT_TRUE(codex::has(s, codex::kOrigins + static_cast<int>(Origin::FarmersChild)));
  EXPECT_TRUE(codex::has(s, codex::kElements + static_cast<int>(Element::Water)));
  EXPECT_TRUE(codex::has(
      s, codex::kScrolls + static_cast<int>(scrolls::Kind::Manual) * 5 + static_cast<int>(scrolls::Tier::Earth)));
  EXPECT_TRUE(codex::has(s, codex::kSacred));
  EXPECT_TRUE(codex::has(s, codex::kPills + static_cast<int>(alchemy::Pill::TonicGreater)));
  EXPECT_TRUE(codex::has(s, codex::kPills + static_cast<int>(alchemy::Pill::CoreForming)));
  EXPECT_EQ(codex::groupFound(s, codex::Group::Pills), 2);
  uint16_t total = 0;
  for (uint8_t g = 0; g < static_cast<uint8_t>(codex::Group::Count); ++g) {
    total = static_cast<uint16_t>(total + codex::groupInfo(static_cast<codex::Group>(g)).size);
  }
  EXPECT_EQ(total, codex::kCodexEntries);
}

TEST(V030Codex, MementoRaritiesAndSets) {
  EXPECT_EQ(codex::rarityOf(0), codex::Rarity::Common);
  EXPECT_EQ(codex::rarityOf(4), codex::Rarity::Common);
  EXPECT_EQ(codex::rarityOf(5), codex::Rarity::Rare);
  EXPECT_EQ(codex::rarityOf(6), codex::Rarity::Rare);
  EXPECT_EQ(codex::rarityOf(7), codex::Rarity::Legendary);
  EXPECT_EQ(codex::rarityOf(63), codex::Rarity::Legendary);
  EXPECT_EQ(codex::setOf(63), 7);
  int common = 0, rare = 0, legend = 0;
  std::set<uint8_t> seen;
  for (uint32_t i = 0; i < 20000; ++i) {
    const uint8_t m = codex::pick(mix32(i, 77));
    ASSERT_LT(m, codex::kMementoCount);
    seen.insert(m);
    const codex::Rarity r = codex::rarityOf(m);
    (r == codex::Rarity::Common ? common : r == codex::Rarity::Rare ? rare : legend)++;
  }
  EXPECT_EQ(seen.size(), 64u);
  EXPECT_NEAR(common / 200.0, 75.0, 2.0);
  EXPECT_NEAR(rare / 200.0, 20.0, 2.0);
  EXPECT_NEAR(legend / 200.0, 5.0, 1.0);
}

TEST(V030Codex, ChancesAndDuplicatesSell) {
  EXPECT_EQ(codex::chancePercent(codex::Source::RegionCleared), 20);
  EXPECT_EQ(codex::chancePercent(codex::Source::Cave), 10);
  EXPECT_EQ(codex::chancePercent(codex::Source::Ruin), 5);
  EXPECT_EQ(codex::chancePercent(codex::Source::DailyTasks), 1);
  GameState s = character(1);
  int finds = 0, dups = 0;
  uint32_t dupStones = 0;
  const uint32_t stonesBefore = s.progress.stones;
  for (uint32_t i = 0; i < 4000; ++i) {
    const codex::Find f = codex::roll(s, codex::Source::RegionCleared, mix32(i, 9));
    if (f.memento == codex::kNone) continue;
    ++finds;
    if (f.duplicate) {
      ++dups;
      dupStones += f.stones;
      EXPECT_EQ(f.stones, codex::duplicateStones(codex::rarityOf(f.memento)));
    }
  }
  EXPECT_NEAR(finds / 40.0, 20.0, 2.5);
  EXPECT_GT(dups, 0);
  EXPECT_EQ(s.progress.stones, stonesBefore + dupStones);
  EXPECT_EQ(codex::mementosFound(s), finds - dups);
  EXPECT_EQ(codex::duplicateStones(codex::Rarity::Common), 20);
  EXPECT_EQ(codex::duplicateStones(codex::Rarity::Rare), 60);
  EXPECT_EQ(codex::duplicateStones(codex::Rarity::Legendary), 150);
}

TEST(V030Codex, CodexBytesDoNotTouchOtherFlags) {
  GameState s = character(9);
  for (uint16_t b = 0; b < codex::kBytes * 8; ++b) codex::mark(s, b);
  for (uint8_t i = 0; i < codex::kFirstByte; ++i) {
    if (i == 6 || i == 4 || i == 0 || i == 1) continue;  // set by creation
    EXPECT_EQ(s.flagsAndCodex[i], 0) << i;
  }
  for (uint8_t i = codex::kFirstByte + codex::kBytes; i < 48; ++i) EXPECT_EQ(s.flagsAndCodex[i], 0) << i;
}

// --- Fated --------------------------------------------------------------------------------------------

TEST(V030Lineage, FatedAddsToRuinSearches) {
  GameState s = character(1);
  int wonWithout = 0, wonWith = 0;
  for (uint32_t seed = 1; seed < 300; ++seed) {
    for (int fated = 0; fated < 2; ++fated) {
      GameState t = s;
      t.world[0] = static_cast<uint8_t>(seed);
      t.world[1] = static_cast<uint8_t>(seed >> 8);
      if (fated) t.lineage[lineage::kPerksA] = 0x50;  // Fated 5: +15
      const explore::Layout l = explore::layoutOf(t, 0);
      int ruin = -1;
      for (uint8_t i = 0; i < explore::kTiles; ++i) {
        if (l.tiles[i] == explore::Tile::Ruin) ruin = i;
      }
      if (ruin < 0) continue;
      explore::enter(t, l, 0);
      t.exploration[8 + 12] = static_cast<uint8_t>(ruin + 1);  // stand on it
      const explore::StepResult r = explore::resolveHere(t, l, 0);
      if (r.outcome == explore::TileOutcome::RuinWon) (fated ? wonWith : wonWithout)++;
    }
  }
  EXPECT_GT(wonWith, wonWithout);
}

// --- One Chronicle per life ---------------------------------------------------------------------------

TEST(V030Chronicle, LivesMoveAsideAndKeepTheirName) {
  Storage.put(chronicle::kPath, "The Chronicle of Lin Yue\n\nLife 1: Lin Yue\n\nBorn in a village.\n");
  ASSERT_TRUE(chronicle::moveToLives(1));
  EXPECT_FALSE(chronicle::exists());
  char path[64];
  chronicle::lifePath(1, path, sizeof(path));
  EXPECT_STREQ(path, "/.crosspoint/cultivation/lives/life 1.txt");
  ASSERT_TRUE(Storage.exists(path));
  char name[32];
  ASSERT_TRUE(chronicle::lifeName(path, name, sizeof(name)));
  EXPECT_STREQ(name, "Lin Yue");
  EXPECT_TRUE(chronicle::moveToLives(2));  // nothing to move is fine

  GameState s = character(2);
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  newLife(s, "Mo Yan");
  ASSERT_TRUE(chronicle::writeOpening(s, nullptr, "In a former life you were Lin Yue."));
  const auto* text = Storage.bytesOf(chronicle::kPath);
  ASSERT_NE(text, nullptr);
  const std::string t(text->begin(), text->end());
  EXPECT_EQ(t.rfind("The Chronicle of Mo Yan\n\nLife 2: Mo Yan\n\n", 0), 0u) << t;
  EXPECT_NE(t.find("In a former life you were Lin Yue.\n"), std::string::npos);
  chronicle::exportPath(s, path, sizeof(path));
  EXPECT_STREQ(path, "/Chronicles/The Chronicle of Mo Yan (life 2).txt");
}

TEST(V030Lineage, FamiliarFaceCanChooseANewRival) {
  GameState s = character(2);
  const uint32_t first = rivals::rivalSeed(s);
  s.lineage[lineage::kImprints] = 2;
  ASSERT_TRUE(lineage::buy(s, Perk::FamiliarFace));
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  EXPECT_TRUE(lineage::keepsRival(s));  // the default
  lineage::setKeepsRival(s, false);     // asked on the perks screen: someone new
  newLife(s, "Fresh", 0x4444);
  const uint32_t fresh = rivals::rivalSeed(s);
  EXPECT_NE(fresh, first);
  EXPECT_EQ(fresh, contentSeed(worldSeedOf(s), 122, 0));
  // The choice is remembered; the rival of this life is the one a later "keep" brings back.
  s.progress.realm = 2;
  lineage::endLife(s, EndKind::Reincarnated, nullptr, 0);
  EXPECT_FALSE(lineage::keepsRival(s));
  lineage::setKeepsRival(s, true);
  newLife(s, "Again", 0x5555);
  EXPECT_EQ(rivals::rivalSeed(s), fresh);
}
