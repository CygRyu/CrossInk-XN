#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "QiNumber.h"

namespace cultivation {

enum class Gender : uint8_t { Male = 0, Female, NonBinary, Unspecified, Count };
enum class Origin : uint8_t { FarmersChild = 0, FallenClanHeir, StreetOrphan, SectServant, HerbalistApprentice, Count };
enum class Element : uint8_t { Wood = 0, Fire, Earth, Metal, Water, Count };

// Totals CrossInk had already recorded when the game last consumed them.
// Each visit converts (current - snapshot) into resources, then advances the
// snapshot. Only local stats are ever recorded here (never synced devices).
struct StatsSnapshot {
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  uint32_t completedBooks = 0;
  std::array<uint32_t, 4> timeOfDaySeconds{};  // Morning, Afternoon, Evening, Night
  std::array<uint32_t, 7> dayOfWeekSeconds{};  // Monday = 0; lets the allowance attribute reading to weeks
  uint32_t lastVisitDay = kNoDay;              // readingStatsDayIndex() of the last visit
  // Reading already credited in the calendar week (Mon-Sun) containing
  // lastVisitDay, for the weekly allowance. Save v1 stored "seconds credited
  // on that day" here; read as a week total it only under-counts, never more.
  uint32_t secondsCountedInLastVisitWeek = 0;

  static constexpr uint32_t kNoDay = 0xFFFFFFFFu;
};

// Totals at character creation, kept for the Past Life Memories prologue.
struct BaselineTotals {
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  uint32_t completedBooks = 0;
};

struct CharacterBlock {
  std::array<char, 24> name{};  // UTF-8, NUL-padded; may fill all 24 bytes (no terminator)
  Gender gender = Gender::Unspecified;
  Origin origin = Origin::FarmersChild;
  Element rootElement = Element::Earth;
  Element rootSecondary = Element::Earth;  // drifts with time-of-day reading
  uint8_t rootPurity = 1;                  // 1..100
  uint8_t talent = 0;                      // index into the talent table
  uint8_t hiddenTrait = 0;
  uint8_t temperament = 0;  // 0 none, 1 Deliberate, 2 Swift
  uint8_t path1 = 0;        // 0 none; set at Foundation Establishment
  uint8_t path2 = 0;
  int8_t karma = 0;  // -100..100
  uint8_t rerollsAllowed = 0;
  uint8_t rerollsUsed = 0;
  uint8_t flags = 0;                   // kCharacterCreated, kHiddenTraitRevealed
  uint32_t creationDay = 0xFFFFFFFFu;  // readingStatsDayIndex() at creation, if the clock was valid

  static constexpr uint8_t kCharacterCreated = 0x01;
  static constexpr uint8_t kHiddenTraitRevealed = 0x02;
  // The origin's starting gifts were granted. Characters made before origin
  // gifts existed have it clear and receive them once on their next visit.
  static constexpr uint8_t kOriginGiftsGiven = 0x04;
};

struct ProgressBlock {
  uint8_t realm = 0;  // 0 = Body Tempering
  uint8_t stage = 0;  // minor stage within the realm, 0-based
  QiNumber qi;        // Qi in the current stage's bar
  uint32_t stones = 0;
  uint8_t stamina = 0;
  uint16_t hp = 100;
  uint16_t hpMax = 100;
  uint32_t ageMonths = 16 * 12;  // 1 in-game month per 5 reading minutes
  uint16_t ageSecondsCarry = 0;  // reading seconds toward the next month (< 300)
  uint32_t qiCarry = 0;          // sub-Qi remainder from the last conversion (numerator units)
  uint8_t failures = 0;          // failed breakthroughs at the current bottleneck (pity)
  uint8_t foundationGrade = 0;   // 0 = not formed yet
  uint8_t coreGrade = 0;
  uint8_t soulClarity = 0;
  uint8_t consolidation = 0;        // 0..40
  uint8_t daoHeart = 0;             // 0..100
  uint8_t progressFlags = 0;        // kInGreatPerfection, kHalfStep, kOddPageCarry
  uint32_t gameReadingSeconds = 0;  // reading counted by this character since creation

  static constexpr uint8_t kInGreatPerfection = 0x01;
  static constexpr uint8_t kHalfStep = 0x02;
  // One page toward the next "1 stone per 2 pages" (decision 5B). A bit in a
  // byte that already exists, so the save layout does not change.
  static constexpr uint8_t kOddPageCarry = 0x04;
};

// The save file, about 950 bytes. Blocks that later milestones fill in are
// reserved here at their full design-document size now, so adding their
// meaning later never moves a byte or needs a format change: an all-zero
// block must mean "empty" for every future field.
struct GameState {
  StatsSnapshot snapshot;
  BaselineTotals baseline;
  CharacterBlock character;
  ProgressBlock progress;
  std::array<uint8_t, 23> daosAndMeridians{};   // 9 Daos x 2 bytes, 36 acupoint bits (milestone 9)
  std::array<uint8_t, 513> techniqueScrolls{};  // count + 64 x 8 bytes (milestone 6)
  std::array<uint8_t, 96> inventory{};          // 32 slots x 3 bytes (milestone 8)
  std::array<uint8_t, 37> world{};              // seed, life, region state (milestone 8)
  std::array<uint8_t, 16> timers{};             // expedition + brew (milestone 8)
  std::array<uint8_t, 25> daily{};              // almanac, tasks, seclusion (milestone 9)
  std::array<uint8_t, 11> rival{};              // milestone 9
  std::array<uint8_t, 48> flagsAndCodex{};      // 128 event flags + 256 codex bits
  std::array<uint8_t, 12> legacy{};             // imprints, perk ranks (milestone 10)

  bool isCreated() const { return (character.flags & CharacterBlock::kCharacterCreated) != 0; }
  // Field by field, so no ~1 KB temporary GameState lands on the stack.
  void reset();
};

// File format (little-endian):
//   magic u16 'PI' | version u16 | payload size u16 | CRC-32 of payload u32 | payload
// Older saves with a shorter payload load with the missing fields defaulted.
// Newer saves load their known prefix. Bump kSaveVersion when appending fields.
namespace save {

constexpr uint16_t kMagic = 0x4950;  // "PI" when read as bytes 'P','I'
constexpr uint16_t kSaveVersion = 1;
constexpr size_t kHeaderSize = 10;
constexpr size_t kPayloadSizeV1 = 945;
constexpr size_t kPayloadSize = kPayloadSizeV1;
constexpr size_t kFileSize = kHeaderSize + kPayloadSize;

constexpr char kDirectory[] = "/.crosspoint/cultivation";
constexpr char kSavePath[] = "/.crosspoint/cultivation/save.bin";

enum class LoadResult : uint8_t {
  Loaded,   // valid save (possibly recovered from .tmp/.bak)
  NoSave,   // nothing on the card: start character creation
  Corrupt,  // files exist but none is valid; state left as defaults
};

// Loads into `state` (reset to defaults first). Recovers an interrupted save:
// a complete .tmp wins over .bak, and a partial .tmp is ignored.
LoadResult load(GameState& state, const char* path = kSavePath);

// Writes path.tmp, moves the old file to path.bak, publishes the .tmp, then
// deletes the .bak. Uses no heap and no payload-sized stack buffer: the
// payload is streamed twice (once for the CRC, once to the file).
bool store(const GameState& state, const char* path = kSavePath);

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);

}  // namespace save
}  // namespace cultivation
