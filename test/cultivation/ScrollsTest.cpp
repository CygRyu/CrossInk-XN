#include <BuiltinPack.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <HalStorage.h>
#include <ScrollNames.h>
#include <ScrollStore.h>
#include <Scrolls.h>
#include <Simulation.h>
#include <StatsBridge.h>
#include <gtest/gtest.h>

#include <cctype>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace cultivation;
using namespace cultivation::scrolls;

namespace {

GameState makeCharacter(const uint32_t worldSeed = 0xC0FFEE) {
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = worldSeed;
  GameState s;
  createCharacter(s, d, StatsSample{});
  return s;
}

BookRecord book(const uint32_t hash, const uint32_t hours, const uint8_t share = 95, const uint8_t bucket = 3) {
  BookRecord b;
  b.titleHash = hash;
  b.seconds = hours * 3600u;
  b.bucket = bucket;
  b.shareRead = share;
  return b;
}

Scroll mintOne(GameState& s, const BookRecord& b) {
  Event ev[2];
  const uint8_t n = mint(s, b, false, ev);
  EXPECT_GE(n, 1);
  return ev[0].scroll;
}

}  // namespace

// --- Properties --------------------------------------------------------------------

TEST(Scrolls, TiersFollowHoursReadInTheBook) {
  EXPECT_EQ(tierForHours(0), Tier::Mortal);
  EXPECT_EQ(tierForHours(2), Tier::Mortal);
  EXPECT_EQ(tierForHours(3), Tier::Yellow);
  EXPECT_EQ(tierForHours(7), Tier::Yellow);
  EXPECT_EQ(tierForHours(8), Tier::Earth);
  EXPECT_EQ(tierForHours(14), Tier::Earth);
  EXPECT_EQ(tierForHours(15), Tier::Heaven);
  EXPECT_EQ(tierForHours(24), Tier::Heaven);
  EXPECT_EQ(tierForHours(25), Tier::Saint);
  EXPECT_EQ(tierForHours(255), Tier::Saint);
}

TEST(Scrolls, QualityBands) {
  EXPECT_EQ(qualityFor(0), Quality::Fragmentary);
  EXPECT_EQ(qualityFor(49), Quality::Fragmentary);
  EXPECT_EQ(qualityFor(50), Quality::Incomplete);
  EXPECT_EQ(qualityFor(75), Quality::Complete);
  EXPECT_EQ(qualityFor(92), Quality::Refined);  // DOC example
  EXPECT_EQ(qualityFor(98), Quality::Perfected);
}

TEST(Scrolls, ElementFromTheDominantTimeOfDay) {
  const uint32_t night[4] = {10, 10, 10, 70};
  EXPECT_EQ(elementForBuckets(night), Element::Water);
  const uint32_t morning[4] = {41, 20, 20, 19};
  EXPECT_EQ(elementForBuckets(morning), Element::Wood);
  const uint32_t afternoon[4] = {0, 1, 0, 0};
  EXPECT_EQ(elementForBuckets(afternoon), Element::Fire);
  const uint32_t evening[4] = {0, 0, 50, 50};  // a tie: the first bucket over 40% wins
  EXPECT_EQ(elementForBuckets(evening), Element::Metal);
  const uint32_t balanced[4] = {40, 30, 20, 10};  // nothing over 40%: Earth
  EXPECT_EQ(elementForBuckets(balanced), Element::Earth);
  const uint32_t none[4] = {0, 0, 0, 0};  // no clock: Earth
  EXPECT_EQ(elementForBuckets(none), Element::Earth);
}

TEST(Scrolls, ManualMultiplierIsTierTimesQuality) {
  Scroll m;
  m.kind = Kind::Manual;
  m.hours = 1;
  m.comprehension = 100;
  EXPECT_EQ(manualPermille(m), 1265u);  // 1.15 x 1.10
  m.comprehension = 75;
  EXPECT_EQ(manualPermille(m), 1035u);  // 1.15 x 0.90
  m.comprehension = 10;
  EXPECT_EQ(manualPermille(m), 1035u);  // never below 0.90
  m.hours = 30;
  m.comprehension = 100;
  EXPECT_EQ(manualPermille(m), 2200u);  // Saint 2.00 x 1.10
  m.hours = 10;
  m.comprehension = 87;
  EXPECT_EQ(manualPermille(m), 1494u);  // Earth 1.50 x 0.996
}

TEST(Scrolls, ManualPityAfterFourOthers) {
  // Over many titles about one in six is a manual, and never five non-manuals in a row.
  GameState s = makeCharacter();
  int manuals = 0;
  int run = 0;
  int longestRun = 0;
  for (uint32_t i = 1; i <= 300; ++i) {
    if (count(s) >= kCapacity) removeAt(s, 0);
    const Scroll sc = mintOne(s, book(i * 7919u, 2));
    if (sc.kind == Kind::Manual) {
      ++manuals;
      run = 0;
    } else {
      ++run;
      longestRun = run > longestRun ? run : longestRun;
    }
  }
  EXPECT_LE(longestRun, 4);
  EXPECT_GT(manuals, 300 / 6);  // pity adds to the 1-in-6 luck
  EXPECT_LT(manuals, 300 / 2);
}

TEST(Scrolls, KindDependsOnTheTitleAndTheWorld) {
  std::set<int> kinds;
  for (uint32_t world = 1; world < 40; ++world) kinds.insert(static_cast<int>(kindFor(12345, world, 0)));
  EXPECT_EQ(kinds.size(), static_cast<size_t>(Kind::Count));
  EXPECT_EQ(kindFor(12345, 7, 0), kindFor(12345, 7, 0));
  EXPECT_EQ(kindFor(12345, 7, kManualPity), Kind::Manual);
}

// --- Library ----------------------------------------------------------------------

TEST(Scrolls, SlotRoundTripAndPacking) {
  GameState s = makeCharacter();
  Scroll a;
  a.titleHash = 0xDEADBEEF;
  a.hours = 200;
  a.element = Element::Water;
  a.kind = Kind::Mind;
  a.comprehension = 93;
  a.flags = Scroll::kEquippedTechnique;
  put(s, 0, a);
  s.techniqueScrolls[0] = 1;
  const Scroll b = get(s, 0);
  EXPECT_EQ(b.titleHash, a.titleHash);
  EXPECT_EQ(b.hours, 200);
  EXPECT_EQ(b.element, Element::Water);
  EXPECT_EQ(b.kind, Kind::Mind);
  EXPECT_EQ(b.comprehension, 93);
  EXPECT_EQ(b.flags, Scroll::kEquippedTechnique);
  EXPECT_EQ(s.techniqueScrolls.size(), 1u + kCapacity * kSlotSize);  // the reserved 513 bytes
}

TEST(Scrolls, MintingFillsTheLibraryInOrder) {
  GameState s = makeCharacter();
  const Scroll a = mintOne(s, book(100, 10, 92, 3));
  EXPECT_EQ(count(s), 1);
  EXPECT_EQ(a.hours, 10);
  EXPECT_EQ(tierOf(a), Tier::Earth);
  EXPECT_EQ(a.element, Element::Water);
  EXPECT_EQ(qualityFor(a.comprehension), Quality::Refined);
  mintOne(s, book(200, 1));
  EXPECT_EQ(count(s), 2);
  EXPECT_EQ(get(s, 0).titleHash, 100u);
  EXPECT_EQ(get(s, 1).titleHash, 200u);
}

TEST(Scrolls, RereadingRaisesQualityInsteadOfASecondScroll) {
  GameState s = makeCharacter();
  mintOne(s, book(5, 4, 60));
  Event ev[2];
  ASSERT_EQ(mint(s, book(5, 9, 99), false, ev), 1);
  EXPECT_EQ(ev[0].kind, EventKind::Improved);
  EXPECT_EQ(ev[0].oldComprehension, 60);
  EXPECT_EQ(count(s), 1);
  EXPECT_EQ(get(s, 0).comprehension, 99);
  EXPECT_EQ(get(s, 0).hours, 4);  // rereading never raises the tier (hours would double)
  ASSERT_EQ(mint(s, book(5, 9, 80), false, ev), 1);
  EXPECT_EQ(ev[0].kind, EventKind::Unchanged);
  EXPECT_EQ(get(s, 0).comprehension, 99);
}

TEST(Scrolls, ABookAlreadyUsedThisLifeGivesNothing) {
  GameState s = makeCharacter();
  Event ev[2];
  ASSERT_EQ(mint(s, book(5, 4), true, ev), 1);
  EXPECT_EQ(ev[0].kind, EventKind::AlreadyUsed);
  EXPECT_EQ(count(s), 0);
}

TEST(Scrolls, BooksReadUnderHalfAnHourGiveNothing) {
  GameState s = makeCharacter();
  Event ev[2];
  BookRecord b = book(5, 0);
  b.seconds = kMinBookSeconds - 1;
  ASSERT_EQ(mint(s, b, false, ev), 1);
  EXPECT_EQ(ev[0].kind, EventKind::TooShort);
  EXPECT_EQ(count(s), 0);
  b.seconds = kMinBookSeconds;
  ASSERT_EQ(mint(s, b, false, ev), 1);
  EXPECT_EQ(ev[0].kind, EventKind::Minted);
}

TEST(Scrolls, AFullLibrarySellsTheWeakestUnequippedScroll) {
  GameState s = makeCharacter();
  for (uint32_t i = 0; i < kCapacity; ++i) mintOne(s, book(1000 + i, 5 + (i % 10), 90));
  ASSERT_EQ(count(s), kCapacity);
  // Make slot 3 the weakest, then equip it: the next weakest goes instead.
  Scroll weak = get(s, 3);
  weak.hours = 1;
  weak.comprehension = 10;
  put(s, 3, weak);
  Scroll second = get(s, 9);
  second.hours = 1;
  second.comprehension = 20;
  put(s, 9, second);
  ASSERT_TRUE(equip(s, 3));
  const uint32_t stones = s.progress.stones;

  Event ev[2];
  ASSERT_EQ(mint(s, book(5000, 12), false, ev), 2);
  EXPECT_EQ(ev[0].kind, EventKind::Minted);
  EXPECT_EQ(ev[1].kind, EventKind::AutoSold);
  EXPECT_EQ(ev[1].scroll.titleHash, second.titleHash);
  EXPECT_EQ(ev[1].stones, 25u);  // Mortal
  EXPECT_EQ(s.progress.stones, stones + 25u);
  EXPECT_EQ(count(s), kCapacity);
  EXPECT_LT(find(s, 5000), kCapacity);
  EXPECT_GE(find(s, 5000), 0);
  EXPECT_EQ(find(s, second.titleHash), -1);
  EXPECT_GE(find(s, weak.titleHash), 0);  // equipped: kept
}

TEST(Scrolls, AFullLibrarySellsANewScrollWeakerThanAllOthers) {
  GameState s = makeCharacter();
  for (uint32_t i = 0; i < kCapacity; ++i) mintOne(s, book(1000 + i, 20, 90));
  const uint32_t stones = s.progress.stones;
  Event ev[2];
  ASSERT_EQ(mint(s, book(7, 1, 50), false, ev), 2);
  EXPECT_EQ(ev[0].kind, EventKind::Minted);
  EXPECT_EQ(ev[1].kind, EventKind::AutoSold);
  EXPECT_EQ(ev[1].scroll.titleHash, 7u);
  EXPECT_EQ(find(s, 7), -1);
  EXPECT_EQ(s.progress.stones, stones + 25u);
}

TEST(Scrolls, EquipOneManualAndUpToThreeTechniques) {
  GameState s = makeCharacter();
  for (uint8_t i = 0; i < 6; ++i) {
    Scroll sc;
    sc.titleHash = 10u + i;
    sc.hours = 4;
    sc.kind = i < 2 ? Kind::Manual : Kind::Sword;
    sc.comprehension = 90;
    put(s, i, sc);
  }
  s.techniqueScrolls[0] = 6;
  const uint32_t plain = qiMultiplierPermille(s);
  ASSERT_TRUE(equip(s, 0));
  EXPECT_EQ(equippedManual(s), 0);
  EXPECT_EQ(qiMultiplierPermille(s), plain * manualPermille(get(s, 0)) / 1000u);
  ASSERT_TRUE(equip(s, 1));  // the other manual replaces it
  EXPECT_EQ(equippedManual(s), 1);
  EXPECT_FALSE(get(s, 0).equipped());
  EXPECT_TRUE(equip(s, 2));
  EXPECT_TRUE(equip(s, 3));
  EXPECT_TRUE(equip(s, 4));
  EXPECT_FALSE(equip(s, 5));  // only three techniques
  EXPECT_EQ(equippedTechniques(s), 3);
  unequip(s, 2);
  EXPECT_TRUE(equip(s, 5));
  unequip(s, 1);
  EXPECT_EQ(qiMultiplierPermille(s), plain);
}

TEST(Scrolls, SellingPaysByTierAndNeverSellsEquipped) {
  GameState s = makeCharacter();
  const uint8_t hours[] = {1, 4, 9, 16, 30};
  const uint32_t prices[] = {25, 60, 150, 300, 500};
  for (uint8_t i = 0; i < 5; ++i) {
    Scroll sc;
    sc.titleHash = 50u + i;
    sc.hours = hours[i];
    put(s, i, sc);
    EXPECT_EQ(sellPrice(sc), prices[i]);
  }
  s.techniqueScrolls[0] = 5;
  ASSERT_TRUE(equip(s, 4));
  EXPECT_EQ(sell(s, 4), 0u);
  EXPECT_EQ(count(s), 5);
  const uint32_t stones = s.progress.stones;
  EXPECT_EQ(sell(s, 1), 60u);
  EXPECT_EQ(s.progress.stones, stones + 60u);
  EXPECT_EQ(count(s), 4);
  EXPECT_EQ(get(s, 1).titleHash, 52u);  // later slots move down
  EXPECT_TRUE(get(s, 3).equipped());
  EXPECT_EQ(s.techniqueScrolls[1 + 4 * kSlotSize], 0u);  // the freed slot is zeroed
}

TEST(Scrolls, BestManualIsTheHighestMultiplier) {
  GameState s = makeCharacter();
  EXPECT_EQ(bestManual(s), -1);
  Scroll a;
  a.titleHash = 1;
  a.kind = Kind::Manual;
  a.hours = 9;
  a.comprehension = 75;  // 1.50 x 0.90 = 1.35
  Scroll b = a;
  b.titleHash = 2;
  b.hours = 4;
  b.comprehension = 100;  // 1.30 x 1.10 = 1.43
  Scroll c = a;
  c.titleHash = 3;
  c.kind = Kind::Body;
  c.hours = 40;
  put(s, 0, a);
  put(s, 1, b);
  put(s, 2, c);
  s.techniqueScrolls[0] = 3;
  EXPECT_EQ(bestManual(s), 1);
}

// --- The hook's files --------------------------------------------------------------

class ScrollStoreTest : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(ScrollStoreTest, TitleHashIsStableAndNeverZero) {
  EXPECT_EQ(scrollstore::titleHash("Project Hail Mary"), scrollstore::titleHash("Project Hail Mary"));
  EXPECT_NE(scrollstore::titleHash("Project Hail Mary"), scrollstore::titleHash("Project Hail Mary 2"));
  EXPECT_NE(scrollstore::titleHash(""), 0u);
  EXPECT_NE(scrollstore::titleHash(nullptr), 0u);
}

TEST_F(ScrollStoreTest, RecordFromBookStats) {
  const uint32_t buckets[4] = {100, 100, 100, 900};
  const scrollstore::PendingRecord r = scrollstore::recordFor("Dune", 14 * 3600, buckets, 92.4f, true);
  EXPECT_EQ(r.book.titleHash, scrollstore::titleHash("Dune"));
  EXPECT_EQ(r.book.seconds, 14u * 3600u);
  EXPECT_EQ(r.book.bucket, 3);  // Night
  EXPECT_EQ(r.book.shareRead, 92);
  EXPECT_EQ(scrollstore::recordFor("Dune", 1, buckets, -1.0f, true).book.shareRead, 75);  // unknown
  EXPECT_EQ(scrollstore::recordFor("Dune", 1, buckets, 99.6f, true).book.shareRead, 100);
  const uint32_t even[4] = {25, 25, 25, 25};
  EXPECT_EQ(scrollstore::recordFor("Dune", 1, even, 50.0f, true).book.bucket, 0xFF);  // Earth

  uint8_t bytes[scrollstore::kRecordSize];
  scrollstore::encode(r, bytes);
  scrollstore::PendingRecord back;
  ASSERT_TRUE(scrollstore::decode(bytes, back));
  EXPECT_EQ(back.book.titleHash, r.book.titleHash);
  EXPECT_EQ(back.book.seconds, r.book.seconds);
  EXPECT_EQ(back.book.bucket, r.book.bucket);
  EXPECT_EQ(back.book.shareRead, r.book.shareRead);
  EXPECT_TRUE(back.completed);
}

TEST_F(ScrollStoreTest, ReplayAppliesMarksAndUnmarksInOrder) {
  const uint32_t buckets[4] = {0, 0, 3600, 0};
  scrollstore::append(scrollstore::recordFor("A", 3600, buckets, 100, true));
  scrollstore::append(scrollstore::recordFor("B", 7200, buckets, 100, true));
  scrollstore::append(scrollstore::recordFor("C", 9000, buckets, 100, true));
  scrollstore::append(scrollstore::recordFor("B", 7200, buckets, 100, false));  // unmarked before minting
  scrollstore::append(scrollstore::recordFor("A", 5400, buckets, 80, true));    // marked again: latest numbers
  EXPECT_EQ(Storage.bytesOf(scrollstore::kPendingPath)->size(), 5 * scrollstore::kRecordSize);

  BookRecord out[scrollstore::kMaxPending];
  bool overflow = true;
  ASSERT_EQ(scrollstore::readPending(out, scrollstore::kMaxPending, overflow), 2);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(out[0].titleHash, scrollstore::titleHash("A"));
  EXPECT_EQ(out[0].seconds, 5400u);
  EXPECT_EQ(out[0].shareRead, 80);
  EXPECT_EQ(out[1].titleHash, scrollstore::titleHash("C"));

  EXPECT_TRUE(scrollstore::finishPending());
  EXPECT_FALSE(scrollstore::hasPending());
  EXPECT_EQ(scrollstore::readPending(out, scrollstore::kMaxPending, overflow), 0);
}

TEST_F(ScrollStoreTest, BooksBeyondOneVisitWaitForTheNext) {
  const uint32_t buckets[4] = {0, 0, 3600, 0};
  char title[16];
  for (int i = 0; i < 5; ++i) {
    snprintf(title, sizeof(title), "Book %d", i);
    scrollstore::append(scrollstore::recordFor(title, 3600, buckets, 100, true));
  }
  scrollstore::append(scrollstore::recordFor("Book 1", 3600, buckets, 100, false));  // held: removed now
  scrollstore::append(scrollstore::recordFor("Book 4", 3600, buckets, 100, false));  // waiting: its unmark waits too
  scrollstore::append(scrollstore::recordFor("Book 5", 3600, buckets, 100, true));   // after the list filled: waits
  BookRecord out[3];
  bool overflow = false;
  ASSERT_EQ(scrollstore::readPending(out, 3, overflow), 2);  // Book 0 and Book 2
  EXPECT_TRUE(overflow);
  EXPECT_EQ(out[0].titleHash, scrollstore::titleHash("Book 0"));
  EXPECT_EQ(out[1].titleHash, scrollstore::titleHash("Book 2"));
  ASSERT_TRUE(scrollstore::finishPending());

  // The next visit: Book 3 and Book 5 (Book 4 was unmarked).
  ASSERT_EQ(scrollstore::readPending(out, 3, overflow), 2);
  EXPECT_FALSE(overflow);
  EXPECT_EQ(out[0].titleHash, scrollstore::titleHash("Book 3"));
  EXPECT_EQ(out[1].titleHash, scrollstore::titleHash("Book 5"));
  ASSERT_TRUE(scrollstore::finishPending());
  EXPECT_FALSE(scrollstore::hasPending());
  EXPECT_FALSE(Storage.exists(scrollstore::kPendingRestPath));
}

TEST_F(ScrollStoreTest, AFailedSaveKeepsEveryBook) {
  // readPending() alone changes nothing: without finishPending() (the save
  // failed) the next visit reads the same books again.
  const uint32_t buckets[4] = {0, 0, 3600, 0};
  char title[16];
  for (int i = 0; i < 4; ++i) {
    snprintf(title, sizeof(title), "Book %d", i);
    scrollstore::append(scrollstore::recordFor(title, 3600, buckets, 100, true));
  }
  BookRecord out[2];
  bool overflow = false;
  ASSERT_EQ(scrollstore::readPending(out, 2, overflow), 2);
  ASSERT_EQ(scrollstore::readPending(out, 2, overflow), 2);
  EXPECT_EQ(out[0].titleHash, scrollstore::titleHash("Book 0"));
  EXPECT_EQ(Storage.bytesOf(scrollstore::kPendingPath)->size(), 4 * scrollstore::kRecordSize);
}

TEST_F(ScrollStoreTest, DamagedRecordsAreSkipped) {
  const uint32_t buckets[4] = {0, 0, 3600, 0};
  scrollstore::append(scrollstore::recordFor("A", 3600, buckets, 100, true));
  auto& bytes = *Storage.bytesOf(scrollstore::kPendingPath);
  bytes[11] = 9;       // unknown format
  bytes.push_back(1);  // and a torn tail
  scrollstore::append(scrollstore::recordFor("B", 3600, buckets, 100, true));
  BookRecord out[4];
  bool overflow = false;
  // The torn byte shifts the next record, so only whole, known records count.
  EXPECT_LE(scrollstore::readPending(out, 4, overflow), 1);
}

TEST_F(ScrollStoreTest, MintedListRemembersBooksAndResetsForANewLife) {
  EXPECT_FALSE(scrollstore::isMinted(42));
  const uint32_t hashes[2] = {42, 43};
  EXPECT_TRUE(scrollstore::addMinted(hashes, 2));
  EXPECT_TRUE(scrollstore::addMinted(hashes, 1));  // no duplicates
  EXPECT_EQ(Storage.bytesOf(scrollstore::kMintedPath)->size(), 8u);
  EXPECT_TRUE(scrollstore::isMinted(42));
  EXPECT_TRUE(scrollstore::isMinted(43));
  EXPECT_FALSE(scrollstore::isMinted(44));
  const uint32_t buckets[4] = {0, 0, 3600, 0};
  scrollstore::append(scrollstore::recordFor("A", 3600, buckets, 100, true));
  scrollstore::clearForNewLife();
  EXPECT_FALSE(scrollstore::isMinted(42));
  EXPECT_FALSE(scrollstore::hasPending());
}

// --- Names and lines ----------------------------------------------------------------

namespace {
struct Packs {
  std::string text = std::string(builtinPackText(), builtinPackSize());
  std::vector<PackSection> storage = std::vector<PackSection>(384);
  PackIndex index;
  MemoryPackReader reader;
  std::unique_ptr<Grammar> grammar;
  Packs() {
    index.sections = storage.data();
    index.capacity = static_cast<uint16_t>(storage.size());
    PackParser parser(index, 0);
    parsePackText(text.data(), text.size(), parser);
    reader.setSource(0, text.data(), text.size());
    index.applyReplacements();
    grammar = std::make_unique<Grammar>(index, reader);
  }
};

Scroll sampleScroll(const uint32_t hash, const Kind kind, const uint8_t hours, const Element e) {
  Scroll s;
  s.titleHash = hash;
  s.kind = kind;
  s.hours = hours;
  s.element = e;
  s.comprehension = 90;
  return s;
}
}  // namespace

TEST(ScrollNames, NamesFollowTheDocsPatternsAndNeverChange) {
  Packs packs;
  std::set<std::string> distinct;
  const char* suffixes[] = {" Method", " Art", " Scripture", " Canon", " True Classic"};
  const uint8_t hours[] = {1, 4, 9, 16, 30};
  for (uint32_t i = 0; i < 300; ++i) {
    const Scroll s = sampleScroll(mix32(i + 1), static_cast<Kind>(i % 6), hours[i % 5], static_cast<Element>(i % 5));
    char a[scrollnames::kNameSize];
    char b[scrollnames::kNameSize];
    ASSERT_TRUE(scrollnames::name(s, 0xABCDEF, *packs.grammar, a, sizeof(a)));
    // Other expansions in between (with anti-repetition memory) change nothing.
    GrammarContext ctx;
    packs.grammar->expand("{scroll.image} {scroll.adj} {scroll.name}", i, ctx);
    ASSERT_TRUE(scrollnames::name(s, 0xABCDEF, *packs.grammar, b, sizeof(b)));
    const std::string name = a;
    EXPECT_EQ(name, std::string(b));
    EXPECT_EQ(name.find_first_of("{}[]|$\\"), std::string::npos) << name;
    EXPECT_LT(name.size(), 64u) << name;
    EXPECT_TRUE(std::isupper(static_cast<unsigned char>(name[0]))) << name;
    const std::string suffix = suffixes[i % 5];
    EXPECT_EQ(name.compare(name.size() - suffix.size(), suffix.size(), suffix), 0) << name;
    distinct.insert(name);
  }
  EXPECT_GT(distinct.size(), 280u);
  // A different world names the same book differently.
  const Scroll s = sampleScroll(77, Kind::Sword, 9, Element::Water);
  int differ = 0;
  for (uint32_t world = 1; world <= 10; ++world) {
    char a[scrollnames::kNameSize];
    char b[scrollnames::kNameSize];
    scrollnames::name(s, world, *packs.grammar, a, sizeof(a));
    scrollnames::name(s, world + 100, *packs.grammar, b, sizeof(b));
    if (std::string(a) != b) ++differ;
  }
  EXPECT_GE(differ, 8);
}

TEST(ScrollNames, SampleNamesForReview) {
  Packs packs;
  for (uint32_t i = 0; i < 12; ++i) {
    const Scroll s = sampleScroll(mix32(i + 900), static_cast<Kind>(i % 6), static_cast<uint8_t>(i * 3),
                                  static_cast<Element>(i % 5));
    char n[scrollnames::kNameSize];
    ASSERT_TRUE(scrollnames::name(s, 0x5EED1234, *packs.grammar, n, sizeof(n)));
    printf("  %s\n", n);
  }
}

TEST(ScrollNames, ChronicleLinesNameTheScroll) {
  Packs packs;
  GameState s = makeCharacter();
  const Scroll sc = sampleScroll(99, Kind::Manual, 10, Element::Fire);
  char n[scrollnames::kNameSize];
  ASSERT_TRUE(scrollnames::name(sc, worldSeedOf(s), *packs.grammar, n, sizeof(n)));
  for (const char* key : {"scroll.line.new", "scroll.line.reread", "scroll.line.sold", "scroll.line.key"}) {
    for (uint32_t age = 0; age < 40; ++age) {
      s.progress.ageMonths = 16 * 12 + age * 5;
      lifelog::Line line;
      ASSERT_TRUE(scrollnames::line(line, key, sc, s, *packs.grammar)) << key;
      const std::string text = line.text;
      EXPECT_NE(text.find(n), std::string::npos) << text;
      EXPECT_EQ(text.find_first_of("{}[]|$\\"), std::string::npos) << text;
      EXPECT_EQ(text.back(), '.') << text;
      EXPECT_LT(text.size(), lifelog::kLineSize - 1) << text;
    }
  }
}
