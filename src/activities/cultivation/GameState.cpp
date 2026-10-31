#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "GameState.h"

namespace cultivation {
void GameState::reset() {
  snapshot = StatsSnapshot{};
  baseline = BaselineTotals{};
  character = CharacterBlock{};
  progress = ProgressBlock{};
  daosAndMeridians.fill(0);
  techniqueScrolls.fill(0);
  inventory.fill(0);
  world.fill(0);
  timers.fill(0);
  daily.fill(0);
  rival.fill(0);
  flagsAndCodex.fill(0);
  legacy.fill(0);
  techniqueMeta.fill(0);
}

namespace save {
namespace {

// Nibble-table CRC-32 (IEEE 802.3, reflected). 16 entries = 64 bytes of flash
// instead of the usual 1 KB table; a ~1 KB save is hashed in well under a
// millisecond at 160 MHz.
static const uint32_t kCrcNibble[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

constexpr size_t kPathMax = 64;  // "/.crosspoint/cultivation/save.bin.tmp" is 38 bytes

// --- Streaming codecs ---------------------------------------------------------
// One field list (transfer() below) drives both directions, so the reader and
// writer can never disagree about the layout.

class Encoder {
 public:
  explicit Encoder(FsFile* file) : file_(file) {}

  void bytes(const void* data, const size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    crc_ = crc32Update(crc_, p, len);
    count_ += len;
    if (file_ != nullptr && ok_ && file_->write(p, len) != len) ok_ = false;
  }
  void u8(const uint8_t& v) { bytes(&v, 1); }
  void i8(const int8_t& v) { u8(static_cast<uint8_t>(v)); }
  void u16(const uint16_t& v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    bytes(b, 2);
  }
  void u32(const uint32_t& v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                          static_cast<uint8_t>(v >> 24)};
    bytes(b, 4);
  }
  void qi(const QiNumber& v) {
    uint8_t b[QiNumber::kSerializedSize];
    v.serialize(b);
    bytes(b, sizeof(b));
  }
  template <typename E>
  void enum8(const E& v) {
    u8(static_cast<uint8_t>(v));
  }

  uint32_t crc() const { return crc_; }
  size_t count() const { return count_; }
  bool ok() const { return ok_; }

 private:
  FsFile* file_;
  uint32_t crc_ = 0;
  size_t count_ = 0;
  bool ok_ = true;
};

class Decoder {
 public:
  Decoder(FsFile& file, const size_t payloadSize) : file_(file), remaining_(payloadSize) {}

  // Fields past the end of an older, shorter payload read as zero. Callers
  // then keep the default for that field (see readOr helpers).
  void bytes(void* data, const size_t len) {
    auto* p = static_cast<uint8_t*>(data);
    const size_t take = len < remaining_ ? len : remaining_;
    if (take > 0) {
      if (file_.read(p, take) != static_cast<int>(take)) {
        ok_ = false;
        memset(p, 0, take);
      }
      crc_ = crc32Update(crc_, p, take);
      remaining_ -= take;
    }
    if (take < len) memset(p + take, 0, len - take);
  }
  // Each reader keeps the field's default when the payload ended before it.
  void u8(uint8_t& v) {
    keep(v, [&](uint8_t& out) { bytes(&out, 1); });
  }
  void i8(int8_t& v) {
    uint8_t t = static_cast<uint8_t>(v);
    u8(t);
    v = static_cast<int8_t>(t);
  }
  void u16(uint16_t& v) {
    keep(v, [&](uint16_t& out) {
      uint8_t b[2];
      bytes(b, 2);
      out = static_cast<uint16_t>(b[0] | (b[1] << 8));
    });
  }
  void u32(uint32_t& v) {
    keep(v, [&](uint32_t& out) {
      uint8_t b[4];
      bytes(b, 4);
      out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24);
    });
  }
  void qi(QiNumber& v) {
    keep(v, [&](QiNumber& out) {
      uint8_t b[QiNumber::kSerializedSize];
      bytes(b, sizeof(b));
      out = QiNumber::deserialize(b);
    });
  }
  template <typename E>
  void enum8(E& v) {
    uint8_t t = static_cast<uint8_t>(v);
    u8(t);
    if (t < static_cast<uint8_t>(E::Count)) v = static_cast<E>(t);  // else keep the default
  }

  // Consumes the unknown tail of a newer save so its CRC still verifies.
  void skipRest() {
    uint8_t scratch[32];
    while (remaining_ > 0 && ok_) bytes(scratch, remaining_ < sizeof(scratch) ? remaining_ : sizeof(scratch));
  }

  uint32_t crc() const { return crc_; }
  bool ok() const { return ok_; }

 private:
  template <typename T, typename Fn>
  void keep(T& v, Fn&& read) {
    if (remaining_ == 0) return;  // older save: field did not exist yet, keep default
    read(v);
  }

  FsFile& file_;
  size_t remaining_;
  uint32_t crc_ = 0;
  bool ok_ = true;
};

// THE save layout. Append new fields at the end only, then bump kSaveVersion
// and kPayloadSize. Never reorder or resize existing fields.
template <typename Io, typename State>
void transfer(Io& io, State& s) {
  // Stats snapshot: 68 bytes
  io.u32(s.snapshot.seconds);
  io.u32(s.snapshot.pages);
  io.u32(s.snapshot.sessions);
  io.u32(s.snapshot.completedBooks);
  for (auto& v : s.snapshot.timeOfDaySeconds) io.u32(v);
  for (auto& v : s.snapshot.dayOfWeekSeconds) io.u32(v);
  io.u32(s.snapshot.lastVisitDay);
  io.u32(s.snapshot.secondsCountedInLastVisitWeek);
  // Baseline at creation: 16 bytes
  io.u32(s.baseline.seconds);
  io.u32(s.baseline.pages);
  io.u32(s.baseline.sessions);
  io.u32(s.baseline.completedBooks);
  // Character: 42 bytes
  io.bytes(s.character.name.data(), s.character.name.size());
  io.enum8(s.character.gender);
  io.enum8(s.character.origin);
  io.enum8(s.character.rootElement);
  io.enum8(s.character.rootSecondary);
  io.u8(s.character.rootPurity);
  io.u8(s.character.talent);
  io.u8(s.character.hiddenTrait);
  io.u8(s.character.temperament);
  io.u8(s.character.path1);
  io.u8(s.character.path2);
  io.i8(s.character.karma);
  io.u8(s.character.rerollsAllowed);
  io.u8(s.character.rerollsUsed);
  io.u8(s.character.flags);
  io.u32(s.character.creationDay);
  // Progress: 38 bytes
  io.u8(s.progress.realm);
  io.u8(s.progress.stage);
  io.qi(s.progress.qi);
  io.u32(s.progress.stones);
  io.u8(s.progress.stamina);
  io.u16(s.progress.hp);
  io.u16(s.progress.hpMax);
  io.u32(s.progress.ageMonths);
  io.u16(s.progress.ageSecondsCarry);
  io.u32(s.progress.qiCarry);
  io.u8(s.progress.failures);
  io.u8(s.progress.foundationGrade);
  io.u8(s.progress.coreGrade);
  io.u8(s.progress.soulClarity);
  io.u8(s.progress.consolidation);
  io.u8(s.progress.daoHeart);
  io.u8(s.progress.progressFlags);
  io.u32(s.progress.gameReadingSeconds);
  // Reserved blocks for later milestones: 781 bytes
  io.bytes(s.daosAndMeridians.data(), s.daosAndMeridians.size());
  io.bytes(s.techniqueScrolls.data(), s.techniqueScrolls.size());
  io.bytes(s.inventory.data(), s.inventory.size());
  io.bytes(s.world.data(), s.world.size());
  io.bytes(s.timers.data(), s.timers.size());
  io.bytes(s.daily.data(), s.daily.size());
  io.bytes(s.rival.data(), s.rival.size());
  io.bytes(s.flagsAndCodex.data(), s.flagsAndCodex.size());
  io.bytes(s.legacy.data(), s.legacy.size());
  // Save v2 (milestone 7): 4 bytes
  io.bytes(s.techniqueMeta.data(), s.techniqueMeta.size());
}

uint16_t readLe16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Reads one candidate file. Leaves `state` at defaults unless it is valid.
bool tryLoad(const char* path, GameState& state) {
  FsFile f;
  if (!Storage.openFileForRead("CULT", path, f)) return false;

  uint8_t header[kHeaderSize];
  if (f.read(header, kHeaderSize) != static_cast<int>(kHeaderSize)) {
    LOG_ERR("CULT", "Short header in %s", path);
    f.close();
    return false;
  }
  const uint16_t magic = readLe16(header);
  const uint16_t version = readLe16(header + 2);
  const uint16_t payloadSize = readLe16(header + 4);
  const uint32_t storedCrc = readLe32(header + 6);
  if (magic != kMagic || version == 0 || payloadSize < kPayloadSizeV1 ||
      f.fileSize() != kHeaderSize + static_cast<size_t>(payloadSize)) {
    LOG_ERR("CULT", "Invalid save header in %s (magic %04x v%u size %u)", path, magic, version, payloadSize);
    f.close();
    return false;
  }

  state.reset();
  Decoder dec(f, payloadSize);
  transfer(dec, state);
  dec.skipRest();
  f.close();

  if (!dec.ok() || dec.crc() != storedCrc) {
    LOG_ERR("CULT", "Save CRC mismatch in %s", path);
    state.reset();
    return false;
  }
  if (version > kSaveVersion) {
    LOG_INF("CULT", "Loaded newer save v%u with firmware v%u", version, kSaveVersion);
  }
  return true;
}

bool makePath(char* out, const char* base, const char* suffix) {
  const int n = snprintf(out, kPathMax, "%s%s", base, suffix);
  return n > 0 && static_cast<size_t>(n) < kPathMax;
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kCrcNibble[crc & 0x0F];
    crc = (crc >> 4) ^ kCrcNibble[crc & 0x0F];
  }
  return ~crc;
}

LoadResult load(GameState& state, const char* path) {
  state.reset();
  char tmpPath[kPathMax];
  char bakPath[kPathMax];
  if (!makePath(tmpPath, path, ".tmp") || !makePath(bakPath, path, ".bak")) {
    LOG_ERR("CULT", "Save path too long: %s", path);
    return LoadResult::Corrupt;
  }

  if (tryLoad(path, state)) return LoadResult::Loaded;

  bool anyFile = Storage.exists(path);
  // A finished .tmp is newer than the .bak; a partial one fails its CRC and
  // falls through to the older, known-good backup.
  for (const char* candidate : {static_cast<const char*>(tmpPath), static_cast<const char*>(bakPath)}) {
    if (!Storage.exists(candidate)) continue;
    anyFile = true;
    if (!tryLoad(candidate, state)) continue;
    if (Storage.exists(path)) Storage.remove(path);  // the main file was invalid
    if (Storage.rename(candidate, path)) {
      LOG_INF("CULT", "Recovered interrupted save from %s", candidate);
    } else {
      LOG_ERR("CULT", "Could not restore %s; using %s directly", path, candidate);
    }
    return LoadResult::Loaded;
  }
  return anyFile ? LoadResult::Corrupt : LoadResult::NoSave;
}

bool store(const GameState& state, const char* path) {
  char tmpPath[kPathMax];
  char bakPath[kPathMax];
  if (!makePath(tmpPath, path, ".tmp") || !makePath(bakPath, path, ".bak")) {
    LOG_ERR("CULT", "Save path too long: %s", path);
    return false;
  }
  Storage.mkdir(kDirectory);  // no-op when it already exists

  // Pass 1: CRC and size, no I/O.
  Encoder measure(nullptr);
  transfer(measure, state);
  if (measure.count() != kPayloadSize) {
    LOG_ERR("CULT", "Save layout is %u bytes, expected %u", static_cast<unsigned>(measure.count()),
            static_cast<unsigned>(kPayloadSize));
    return false;
  }

  if (Storage.exists(tmpPath) && !Storage.remove(tmpPath)) {
    LOG_ERR("CULT", "Could not remove stale %s", tmpPath);
    return false;
  }
  FsFile f;
  if (!Storage.openFileForWrite("CULT", tmpPath, f)) {
    LOG_ERR("CULT", "Could not open %s for writing", tmpPath);
    return false;
  }
  uint8_t header[kHeaderSize] = {static_cast<uint8_t>(kMagic),
                                 static_cast<uint8_t>(kMagic >> 8),
                                 static_cast<uint8_t>(kSaveVersion),
                                 static_cast<uint8_t>(kSaveVersion >> 8),
                                 static_cast<uint8_t>(kPayloadSize),
                                 static_cast<uint8_t>(kPayloadSize >> 8),
                                 static_cast<uint8_t>(measure.crc()),
                                 static_cast<uint8_t>(measure.crc() >> 8),
                                 static_cast<uint8_t>(measure.crc() >> 16),
                                 static_cast<uint8_t>(measure.crc() >> 24)};
  bool ok = f.write(header, kHeaderSize) == kHeaderSize;
  // Pass 2: stream the payload into the file.
  Encoder writer(&f);
  if (ok) transfer(writer, state);
  ok = ok && writer.ok() && writer.crc() == measure.crc();
  f.flush();
  ok = f.sync() && ok;
  ok = f.close() && ok;
  if (!ok) {
    LOG_ERR("CULT", "Failed writing %s", tmpPath);
    Storage.remove(tmpPath);
    return false;
  }

  const bool hadOriginal = Storage.exists(path);
  if (hadOriginal) {
    if (Storage.exists(bakPath) && !Storage.remove(bakPath)) {
      LOG_ERR("CULT", "Could not remove stale %s", bakPath);
      Storage.remove(tmpPath);
      return false;
    }
    if (!Storage.rename(path, bakPath)) {
      LOG_ERR("CULT", "Could not back up %s", path);
      Storage.remove(tmpPath);
      return false;
    }
  }
  if (!Storage.rename(tmpPath, path)) {
    LOG_ERR("CULT", "Could not publish %s", path);
    if (hadOriginal && !Storage.rename(bakPath, path)) {
      LOG_ERR("CULT", "Could not restore %s", bakPath);
    }
    if (hadOriginal && Storage.exists(path)) Storage.remove(tmpPath);
    return false;
  }
  if (hadOriginal && Storage.exists(bakPath) && !Storage.remove(bakPath)) {
    LOG_ERR("CULT", "Could not remove completed backup %s", bakPath);
  }
  return true;
}

}  // namespace save
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
