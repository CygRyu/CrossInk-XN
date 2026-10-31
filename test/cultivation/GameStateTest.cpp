#include <GameState.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace cultivation;

namespace {
const std::string kPath = save::kSavePath;
const std::string kTmp = kPath + ".tmp";
const std::string kBak = kPath + ".bak";

GameState sampleState(const uint32_t marker) {
  GameState s;
  s.snapshot.seconds = 36000;
  s.snapshot.pages = 1234;
  s.snapshot.sessions = 77;
  s.snapshot.completedBooks = 5;
  s.snapshot.timeOfDaySeconds = {1, 2, 3, 4};
  s.snapshot.dayOfWeekSeconds = {10, 20, 30, 40, 50, 60, 70};
  s.snapshot.lastVisitDay = 9800;
  s.snapshot.secondsCountedInLastVisitWeek = 1800;
  s.baseline = {1000, 200, 30, 4};
  const char name[] = "Lin Yue";
  memcpy(s.character.name.data(), name, sizeof(name) - 1);
  s.character.gender = Gender::Female;
  s.character.origin = Origin::StreetOrphan;
  s.character.rootElement = Element::Water;
  s.character.rootSecondary = Element::Metal;
  s.character.rootPurity = 73;
  s.character.talent = 2;
  s.character.karma = -12;
  s.character.rerollsAllowed = 3;
  s.character.rerollsUsed = 2;
  s.character.flags = CharacterBlock::kCharacterCreated;
  s.character.creationDay = 9790;
  s.progress.realm = 1;
  s.progress.stage = 4;
  s.progress.qi = QiNumber::fromU64(marker);
  s.progress.stones = 4321;
  s.progress.stamina = 9;
  s.progress.ageMonths = 16 * 12 + 30;
  s.progress.ageSecondsCarry = 299;
  s.progress.qiCarry = 59;
  s.progress.gameReadingSeconds = 9000;
  s.legacy[11] = 0xAB;
  s.techniqueScrolls[0] = 1;
  s.techniqueMeta[0] = 3;
  s.exploration[0] = 0x2A;
  s.exploration[199] = 0x5C;
  return s;
}

void expectSame(const GameState& a, const GameState& b) {
  EXPECT_EQ(a.snapshot.seconds, b.snapshot.seconds);
  EXPECT_EQ(a.snapshot.pages, b.snapshot.pages);
  EXPECT_EQ(a.snapshot.sessions, b.snapshot.sessions);
  EXPECT_EQ(a.snapshot.completedBooks, b.snapshot.completedBooks);
  EXPECT_EQ(a.snapshot.timeOfDaySeconds, b.snapshot.timeOfDaySeconds);
  EXPECT_EQ(a.snapshot.dayOfWeekSeconds, b.snapshot.dayOfWeekSeconds);
  EXPECT_EQ(a.snapshot.lastVisitDay, b.snapshot.lastVisitDay);
  EXPECT_EQ(a.snapshot.secondsCountedInLastVisitWeek, b.snapshot.secondsCountedInLastVisitWeek);
  EXPECT_EQ(a.baseline.seconds, b.baseline.seconds);
  EXPECT_EQ(a.baseline.completedBooks, b.baseline.completedBooks);
  EXPECT_EQ(a.character.name, b.character.name);
  EXPECT_EQ(a.character.gender, b.character.gender);
  EXPECT_EQ(a.character.origin, b.character.origin);
  EXPECT_EQ(a.character.rootElement, b.character.rootElement);
  EXPECT_EQ(a.character.rootSecondary, b.character.rootSecondary);
  EXPECT_EQ(a.character.rootPurity, b.character.rootPurity);
  EXPECT_EQ(a.character.karma, b.character.karma);
  EXPECT_EQ(a.character.rerollsUsed, b.character.rerollsUsed);
  EXPECT_EQ(a.character.flags, b.character.flags);
  EXPECT_EQ(a.character.creationDay, b.character.creationDay);
  EXPECT_EQ(a.progress.realm, b.progress.realm);
  EXPECT_EQ(a.progress.stage, b.progress.stage);
  EXPECT_EQ(a.progress.qi, b.progress.qi);
  EXPECT_EQ(a.progress.stones, b.progress.stones);
  EXPECT_EQ(a.progress.ageMonths, b.progress.ageMonths);
  EXPECT_EQ(a.progress.ageSecondsCarry, b.progress.ageSecondsCarry);
  EXPECT_EQ(a.progress.qiCarry, b.progress.qiCarry);
  EXPECT_EQ(a.progress.gameReadingSeconds, b.progress.gameReadingSeconds);
  EXPECT_EQ(a.techniqueScrolls, b.techniqueScrolls);
  EXPECT_EQ(a.legacy, b.legacy);
  EXPECT_EQ(a.techniqueMeta, b.techniqueMeta);
  EXPECT_EQ(a.exploration, b.exploration);
}

uint32_t qiOf(const GameState& s) {
  uint64_t v = 0;
  s.progress.qi.toU64(v);
  return static_cast<uint32_t>(v);
}
}  // namespace

class GameStateTest : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(GameStateTest, FileIsAboutOnePointThreeFiveKilobytes) {
  ASSERT_TRUE(save::store(sampleState(1)));
  const auto* bytes = Storage.bytesOf(kPath);
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(bytes->size(), save::kFileSize);
  // v2: + 4 bytes techniqueMeta; v3: + 200 bytes exploration; v4: + 192 bytes bag
  EXPECT_EQ(save::kFileSize, 1351u);
  EXPECT_LT(save::kFileSize, 1400u);  // over the doc's 1 KB since v0.1.10 (told to the user)
}

TEST_F(GameStateTest, RoundTrip) {
  const GameState original = sampleState(4242);
  ASSERT_TRUE(save::store(original));
  GameState loaded;
  EXPECT_EQ(save::load(loaded), save::LoadResult::Loaded);
  expectSame(original, loaded);
  EXPECT_TRUE(loaded.isCreated());
  EXPECT_FALSE(Storage.exists(kTmp.c_str()));
  EXPECT_FALSE(Storage.exists(kBak.c_str()));
}

TEST_F(GameStateTest, MissingSaveMeansNewGame) {
  GameState s = sampleState(1);
  EXPECT_EQ(save::load(s), save::LoadResult::NoSave);
  EXPECT_FALSE(s.isCreated());  // reset to defaults
}

TEST_F(GameStateTest, DetectsCorruption) {
  ASSERT_TRUE(save::store(sampleState(7)));
  (*Storage.bytesOf(kPath))[200] ^= 0x40;
  GameState s;
  EXPECT_EQ(save::load(s), save::LoadResult::Corrupt);
  EXPECT_FALSE(s.isCreated());
}

TEST_F(GameStateTest, RejectsWrongMagicAndTruncatedFiles) {
  ASSERT_TRUE(save::store(sampleState(7)));
  (*Storage.bytesOf(kPath))[0] = 'X';
  GameState s;
  EXPECT_EQ(save::load(s), save::LoadResult::Corrupt);

  Storage.reset();
  ASSERT_TRUE(save::store(sampleState(7)));
  Storage.bytesOf(kPath)->resize(500);
  EXPECT_EQ(save::load(s), save::LoadResult::Corrupt);
}

TEST_F(GameStateTest, SecondSaveReplacesFirst) {
  ASSERT_TRUE(save::store(sampleState(1)));
  ASSERT_TRUE(save::store(sampleState(2)));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 2u);
}

TEST_F(GameStateTest, KeepsOldSaveWhenPublishFails) {
  ASSERT_TRUE(save::store(sampleState(1)));
  Storage.failNextRenameFrom(kTmp);
  EXPECT_FALSE(save::store(sampleState(2)));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 1u);
  EXPECT_FALSE(Storage.exists(kTmp.c_str()));
  EXPECT_FALSE(Storage.exists(kBak.c_str()));
}

TEST_F(GameStateTest, RecoversBackupAfterInterruptedSave) {
  // Power lost after the old save moved to .bak, before .tmp was published.
  ASSERT_TRUE(save::store(sampleState(1)));
  ASSERT_TRUE(Storage.rename(kPath.c_str(), kBak.c_str()));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 1u);
  EXPECT_TRUE(Storage.exists(kPath.c_str()));
  EXPECT_FALSE(Storage.exists(kBak.c_str()));
}

TEST_F(GameStateTest, PrefersCompleteTmpOverOlderBackup) {
  ASSERT_TRUE(save::store(sampleState(1)));
  ASSERT_TRUE(Storage.rename(kPath.c_str(), kBak.c_str()));
  ASSERT_TRUE(save::store(sampleState(2)));
  ASSERT_TRUE(Storage.rename(kPath.c_str(), kTmp.c_str()));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 2u);
}

TEST_F(GameStateTest, IgnoresPartialTmpAndUsesBackup) {
  ASSERT_TRUE(save::store(sampleState(1)));
  ASSERT_TRUE(Storage.rename(kPath.c_str(), kBak.c_str()));
  ASSERT_TRUE(save::store(sampleState(2)));
  ASSERT_TRUE(Storage.rename(kPath.c_str(), kTmp.c_str()));
  Storage.bytesOf(kTmp)->resize(300);  // write cut off mid-file
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 1u);
}

TEST_F(GameStateTest, LoadsNewerSaveWithExtraFields) {
  // A future version appended 4 bytes; this firmware keeps the known prefix.
  ASSERT_TRUE(save::store(sampleState(99)));
  auto& bytes = *Storage.bytesOf(kPath);
  const uint8_t extra[4] = {1, 2, 3, 4};
  bytes.insert(bytes.end(), extra, extra + 4);
  const uint16_t newSize = static_cast<uint16_t>(save::kPayloadSize + 4);
  bytes[2] = 3;  // version 3
  bytes[4] = static_cast<uint8_t>(newSize);
  bytes[5] = static_cast<uint8_t>(newSize >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, newSize);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));

  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 99u);
}

TEST_F(GameStateTest, Crc32MatchesStandardCheckValue) {
  const char check[] = "123456789";
  EXPECT_EQ(save::crc32Update(0, reinterpret_cast<const uint8_t*>(check), 9), 0xCBF43926u);
}

TEST_F(GameStateTest, OutOfRangeEnumsLoadAsDefaults) {
  ASSERT_TRUE(save::store(sampleState(5)));
  auto& bytes = *Storage.bytesOf(kPath);
  // Gender is the first byte after name: header 10 + snapshot 68 + baseline 16 + name 24.
  bytes[10 + 68 + 16 + 24] = 200;
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSize);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));
  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(s.character.gender, Gender::Unspecified);
}

TEST_F(GameStateTest, LoadsVersionOneSaveWithTechniqueMetaDefaulted) {
  // A milestone 2-6 save: version 1, 945-byte payload, no techniqueMeta.
  GameState original = sampleState(77);
  ASSERT_TRUE(save::store(original));
  auto& bytes = *Storage.bytesOf(kPath);
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV1);
  bytes[2] = 1;
  bytes[3] = 0;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV1);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV1 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV1);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));

  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 77u);
  EXPECT_EQ(s.techniqueScrolls, original.techniqueScrolls);
  EXPECT_EQ(s.techniqueMeta[0], 0u);
  // Saving again writes the current version.
  ASSERT_TRUE(save::store(s));
  EXPECT_EQ(Storage.bytesOf(kPath)->size(), save::kFileSize);
}

TEST_F(GameStateTest, LoadsVersionTwoSaveWithExplorationEmpty) {
  // A milestone 7 - v0.1.9 save: version 2, 949-byte payload, no exploration block.
  GameState original = sampleState(78);
  ASSERT_TRUE(save::store(original));
  auto& bytes = *Storage.bytesOf(kPath);
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV2);
  bytes[2] = 2;
  bytes[3] = 0;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV2);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV2 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV2);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));

  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 78u);
  EXPECT_EQ(s.techniqueMeta, original.techniqueMeta);
  for (const uint8_t b : s.exploration) EXPECT_EQ(b, 0u);
  ASSERT_TRUE(save::store(s));
  EXPECT_EQ(Storage.bytesOf(kPath)->size(), save::kFileSize);
}

TEST_F(GameStateTest, LoadsVersionThreeSaveWithTheNewBagEmpty) {
  // A v0.1.10 save: version 3, 1149-byte payload, the bag still in the old block.
  GameState original = sampleState(79);
  original.inventory[0] = 1;  // three common herbs in the v0.1.10 bag
  original.inventory[1] = 3;
  ASSERT_TRUE(save::store(original));
  auto& bytes = *Storage.bytesOf(kPath);
  bytes.resize(save::kHeaderSize + save::kPayloadSizeV3);
  bytes[2] = 3;
  bytes[3] = 0;
  bytes[4] = static_cast<uint8_t>(save::kPayloadSizeV3);
  bytes[5] = static_cast<uint8_t>(save::kPayloadSizeV3 >> 8);
  const uint32_t crc = save::crc32Update(0, bytes.data() + save::kHeaderSize, save::kPayloadSizeV3);
  for (int i = 0; i < 4; ++i) bytes[6 + i] = static_cast<uint8_t>(crc >> (8 * i));

  GameState s;
  ASSERT_EQ(save::load(s), save::LoadResult::Loaded);
  EXPECT_EQ(qiOf(s), 79u);
  EXPECT_EQ(s.inventory, original.inventory);
  EXPECT_EQ(s.exploration, original.exploration);
  for (const uint8_t b : s.bag) EXPECT_EQ(b, 0u);
  ASSERT_TRUE(save::store(s));
  EXPECT_EQ(Storage.bytesOf(kPath)->size(), save::kFileSize);
}
