#include <BuiltinPack.h>
#include <Grammar.h>
#include <HalStorage.h>
#include <PackStore.h>
#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>

using namespace cultivation;

namespace {

const std::string kLists = "/.crosspoint/cultivation/lists/";
const std::string kEvents = "/.crosspoint/cultivation/events/";

uint16_t builtinSections() {
  std::vector<PackSection> storage(PackStore::kMaxSections);
  PackIndex index{storage.data(), 0, PackStore::kMaxSections, false};
  PackParser parser(index, 0);
  parsePackText(builtinPackText(), builtinPackSize(), parser);
  return index.count;
}

std::string fileText(const char* path) {
  const auto* bytes = Storage.bytesOf(path);
  return bytes == nullptr ? std::string() : std::string(bytes->begin(), bytes->end());
}

std::unique_ptr<PackStore> open() {
  auto store = std::make_unique<PackStore>();
  EXPECT_TRUE(store->begin());
  return store;
}

class PackStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

}  // namespace

TEST_F(PackStoreTest, WithoutSdPacksOnlyTheBuiltInPackIsUsed) {
  Storage.put(PackStore::kIndexPath, "stale");
  Storage.put(PackStore::kReportPath, "stale");
  auto store = open();
  EXPECT_EQ(store->sdFileCount(), 0);
  EXPECT_EQ(store->index().count, builtinSections());
  EXPECT_FALSE(Storage.exists(PackStore::kIndexPath));  // left over from removed packs
  EXPECT_FALSE(Storage.exists(PackStore::kReportPath));
}

TEST_F(PackStoreTest, SdPackExtendsAListAndIsCached) {
  Storage.put(kLists + "extra.txt", "# my pack\n[rumor.body]\nthe test pack works\n");
  const uint32_t before = [] {
    std::vector<PackSection> storage(64);
    PackIndex index{storage.data(), 0, 64, false};
    PackParser parser(index, 0);
    parsePackText(builtinPackText(), builtinPackSize(), parser);
    return index.entryCount(packKeyHash("rumor.body"));
  }();

  auto first = open();
  EXPECT_TRUE(first->rebuilt());
  EXPECT_EQ(first->sdFileCount(), 1);
  EXPECT_EQ(first->sdProblems(), 0);
  EXPECT_TRUE(Storage.exists(PackStore::kIndexPath));
  EXPECT_FALSE(Storage.exists(PackStore::kReportPath));
  EXPECT_EQ(first->index().entryCount(packKeyHash("rumor.body")), before + 1);

  uint16_t at = 0;
  const PackSection* s = first->index().locate(packKeyHash("rumor.body"), before, at);
  ASSERT_NE(s, nullptr);
  char buf[64];
  ASSERT_TRUE(first->readEntry(*s, at, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "the test pack works");
  first.reset();

  auto second = open();  // nothing changed: index.bin is used
  EXPECT_FALSE(second->rebuilt());
  EXPECT_EQ(second->index().entryCount(packKeyHash("rumor.body")), before + 1);
}

TEST_F(PackStoreTest, ChangedAddedOrBrokenCacheRebuilds) {
  Storage.put(kLists + "a.txt", "[x]\none\n", 100);
  open();
  EXPECT_FALSE(open()->rebuilt());

  Storage.put(kLists + "a.txt", "[x]\none\ntwo\n", 101);  // edited
  auto edited = open();
  EXPECT_TRUE(edited->rebuilt());
  EXPECT_EQ(edited->index().entryCount(packKeyHash("x")), 2u);

  Storage.put(kEvents + "b.txt", "[event.y]\nText.\n");  // added
  EXPECT_TRUE(open()->rebuilt());
  EXPECT_FALSE(open()->rebuilt());

  (*Storage.bytesOf(PackStore::kIndexPath))[20] ^= 0x5A;  // corrupted
  EXPECT_TRUE(open()->rebuilt());
}

TEST_F(PackStoreTest, ReplaceHeaderOverridesTheBuiltInList) {
  Storage.put(kLists + "mine.txt", "[!rumor.opening]\nIt is said that\n");
  auto store = open();
  EXPECT_EQ(store->index().entryCount(packKeyHash("rumor.opening")), 1u);
  Grammar g(store->index(), *store);
  for (uint32_t seed = 0; seed < 20; ++seed) {
    const std::string r = g.expand("{rumor}", seed, GrammarContext{});
    EXPECT_EQ(r.rfind("It is said that ", 0), 0u) << r;
  }
}

TEST_F(PackStoreTest, ProblemsGoToTheReportAndClearWhenFixed) {
  Storage.put(kLists + "bad.txt", "stray line\n[good]\n{missing.list}\n{good} and [unclosed\n[Bad]\nx\n", 5);
  auto store = open();
  EXPECT_EQ(store->sdProblems(), 5);  // unknown list, unbalanced bracket, bad header, 2 orphan lines
  const std::string report = fileText(PackStore::kReportPath);
  EXPECT_NE(report.find("lists/bad.txt: unknown list (or unknown modifier) 'missing.list'"), std::string::npos)
      << report;
  EXPECT_NE(report.find("lists/bad.txt: unbalanced bracket"), std::string::npos) << report;
  EXPECT_NE(report.find("lists/bad.txt: 1 bad [header] line(s), first on line 5"), std::string::npos) << report;
  EXPECT_NE(report.find("lists/bad.txt: 2 line(s) outside any [section]"), std::string::npos) << report;
  store.reset();

  EXPECT_EQ(open()->sdProblems(), 5);  // remembered by index.bin

  Storage.put(kLists + "bad.txt", "[good]\nfine now\n", 6);
  auto fixed = open();
  EXPECT_EQ(fixed->sdProblems(), 0);
  EXPECT_FALSE(Storage.exists(PackStore::kReportPath));
}

TEST_F(PackStoreTest, ReadsEntriesInAnyOrder) {
  std::string text = "[n]\n";
  for (int i = 0; i < 40; ++i) text += "# c\n\n  entry " + std::to_string(i) + "  \r\n";
  Storage.put(kLists + "n.txt", text);
  auto store = open();
  uint16_t at = 0;
  const PackSection* s = store->index().locate(packKeyHash("n"), 0, at);
  ASSERT_NE(s, nullptr);
  ASSERT_EQ(s->count, 40);
  char buf[32];
  for (uint16_t i = 0; i < 40; ++i) {  // forward
    ASSERT_TRUE(store->readEntry(*s, i, buf, sizeof(buf)));
    EXPECT_EQ(std::string(buf), "entry " + std::to_string(i));
  }
  for (const uint16_t i : {39, 3, 17, 0, 38, 38, 2}) {  // and jumping around
    ASSERT_TRUE(store->readEntry(*s, i, buf, sizeof(buf)));
    EXPECT_EQ(std::string(buf), "entry " + std::to_string(i));
  }
  EXPECT_FALSE(store->readEntry(*s, 40, buf, sizeof(buf)));
}

TEST_F(PackStoreTest, IgnoresNonTextFilesAndFolders) {
  Storage.put(kLists + "notes.md", "[x]\nno\n");
  Storage.put(kLists + "._extra.txt", "[x]\nmacOS metadata\n");
  Storage.put(kLists + "sub/deep.txt", "[x]\nno\n");
  Storage.put("/.crosspoint/cultivation/chronicle.txt", "[x]\nno\n");
  Storage.put(kLists + "UPPER.TXT", "[x]\nyes\n");
  auto store = open();
  EXPECT_EQ(store->sdFileCount(), 1);
  EXPECT_EQ(store->index().entryCount(packKeyHash("x")), 1u);
}

TEST_F(PackStoreTest, LongLinesAreKeptCountedAndReported) {
  Storage.put(kLists + "long.txt", "[x]\n" + std::string(300, 'y') + "\nshort\n");
  auto store = open();
  EXPECT_EQ(store->index().entryCount(packKeyHash("x")), 2u);
  EXPECT_EQ(store->sdProblems(), 1);
  EXPECT_NE(fileText(PackStore::kReportPath).find("longer than 191"), std::string::npos);
  uint16_t at = 0;
  const PackSection* s = store->index().locate(packKeyHash("x"), 1, at);
  char buf[16];
  ASSERT_TRUE(store->readEntry(*s, at, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "short");
}

TEST_F(PackStoreTest, FilesLayerInNameOrder) {
  Storage.put(kLists + "b.txt", "[!x]\nfrom b\n");
  Storage.put(kLists + "a.txt", "[!x]\nfrom a\n");
  auto store = open();
  EXPECT_EQ(store->index().entryCount(packKeyHash("x")), 1u);  // b.txt comes last and wins
  Grammar g(store->index(), *store);
  EXPECT_STREQ(g.expand("{x}", 1, GrammarContext{}), "from b");
}
