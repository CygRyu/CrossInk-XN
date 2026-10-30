#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <Logging.h>
#include <strings.h>

#include <cstdio>
#include <cstring>

#include "BuiltinPack.h"
#include "GameState.h"
#include "PackCheck.h"
#include "PackStore.h"

namespace cultivation {
namespace {

// index.bin (little-endian):
//   magic u16 'PX' | version u16 | built-in pack hash u32 | file count u8 | 0 u8 |
//   section count u16 | problems u16 | 0 u16
//   file table: name[48] | size u32 | modified u32       (per SD file, sorted by name)
//   sections: 12-byte PackSection records                 (SD sections only)
//   CRC-32 of everything above
constexpr uint16_t kIndexMagic = 0x5850;  // "PX" as bytes 'P','X'
constexpr uint16_t kIndexVersion = 1;
constexpr size_t kIndexHeaderSize = 16;
constexpr char kIndexTmpPath[] = "/.crosspoint/cultivation/index.bin.tmp";
constexpr size_t kPathMax = 96;
constexpr size_t kScanLineMax = 256;     // longest line the rescan keeps (headers are far shorter)
constexpr uint8_t kReportMessages = 20;  // problems written in full to pack_report.txt
constexpr size_t kReportMessageSize = 160;

void putLe16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void putLe32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t getLe16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t getLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool endsWithTxt(const char* name) {
  const size_t n = strlen(name);
  return n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
}

}  // namespace

// Problems found by a rescan, kept until the report is written (only then,
// with every pack file closed, is the report file opened).
struct ScanFindings {
  uint16_t badHeaders[PackStore::kMaxFiles] = {};
  uint16_t firstBadHeaderLine[PackStore::kMaxFiles] = {};
  uint16_t orphanLines[PackStore::kMaxFiles] = {};
  bool overflowed = false;
  uint32_t checkerProblems = 0;
  uint8_t kept = 0;
  char* messages = nullptr;  // kReportMessages x kReportMessageSize, borrowed for the rescan
  uint8_t sources[kReportMessages] = {};
};

namespace {
void collectProblem(void* user, const uint8_t source, const char* message) {
  auto* f = static_cast<ScanFindings*>(user);
  ++f->checkerProblems;
  if (f->messages == nullptr || f->kept >= kReportMessages) return;
  snprintf(f->messages + f->kept * kReportMessageSize, kReportMessageSize, "%s", message);
  f->sources[f->kept++] = source;
}
}  // namespace

bool PackStore::begin() {
  sections_ = makeUniqueNoThrow<PackSection[]>(kMaxSections);
  if (!sections_) {
    LOG_ERR("CULT", "Out of memory for the pack index (%u bytes)",
            static_cast<unsigned>(kMaxSections * sizeof(PackSection)));
    return false;
  }
  index_ = PackIndex{};
  index_.sections = sections_.get();
  index_.capacity = kMaxSections;

  builtin_.setSource(0, builtinPackText(), builtinPackSize());
  builtinHash_ = packKeyHash(builtinPackText(), builtinPackSize());
  PackParser parser(index_, 0);
  parsePackText(builtinPackText(), builtinPackSize(), parser);
  const uint16_t builtinCount = index_.count;

  listSdFiles();
  if (fileCount_ == 0) {
    // No SD packs: drop a cache or report left from packs that were removed.
    if (Storage.exists(kIndexPath)) Storage.remove(kIndexPath);
    if (Storage.exists(kReportPath)) Storage.remove(kReportPath);
  } else if (!loadCache(builtinCount)) {
    rescan(builtinCount);
  }

  for (uint16_t i = 0; i < index_.count; ++i) index_.sections[i].flags &= ~PackSection::kDisabled;
  index_.applyReplacements();
  closeFile();
  LOG_INF("CULT", "Packs: %u sections (%u built-in), %u SD file(s), %u problem(s)%s", index_.count, builtinCount,
          fileCount_, sdProblems_, rebuilt_ ? ", index rebuilt" : "");
  return true;
}

// --- SD files -----------------------------------------------------------------------

void PackStore::listSdFiles() {
  fileCount_ = 0;
  addFilesFrom("lists");
  addFilesFrom("events");
  // Sorted by name, so the layering order (and index.bin) never depends on
  // the order of entries in the FAT directory.
  for (uint8_t i = 1; i < fileCount_; ++i) {
    for (uint8_t j = i; j > 0 && strcmp(files_[j - 1].name, files_[j].name) > 0; --j) {
      const FileStamp t = files_[j];
      files_[j] = files_[j - 1];
      files_[j - 1] = t;
    }
  }
}

void PackStore::addFilesFrom(const char* folder) {
  char path[kPathMax];
  snprintf(path, sizeof(path), "%s/%s", kRoot, folder);
  HalFile dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  dir.rewindDirectory();
  char name[64];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    const bool usable = !entry.isDirectory() && name[0] != '.' && endsWithTxt(name);
    const uint32_t size = static_cast<uint32_t>(entry.fileSize());
    const uint32_t modified = entry.modificationTime();
    entry.close();
    if (!usable) continue;
    if (fileCount_ >= kMaxFiles || strlen(folder) + 1 + strlen(name) >= kNameMax) {
      LOG_ERR("CULT", "Pack skipped (more than %u files, or name too long): %s/%s", kMaxFiles, folder, name);
      continue;
    }
    FileStamp& f = files_[fileCount_++];
    const size_t folderLen = strlen(folder);
    const size_t nameLen = strlen(name);  // checked above: folder + '/' + name fits in kNameMax
    memcpy(f.name, folder, folderLen);
    f.name[folderLen] = '/';
    memcpy(f.name + folderLen + 1, name, nameLen);
    f.name[folderLen + 1 + nameLen] = '\0';
    f.size = size;
    f.modified = modified;
  }
  dir.close();
}

void PackStore::makePath(const uint8_t source, char* out, const size_t len) const {
  snprintf(out, len, "%s/%s", kRoot, files_[source - 1].name);
}

// --- index.bin ------------------------------------------------------------------------

bool PackStore::loadCache(const uint16_t builtinCount) {
  HalFile f;
  if (!Storage.openFileForRead("CULT", kIndexPath, f)) return false;
  bool ok = true;
  uint32_t crc = 0;
  auto read = [&](void* p, const size_t n) {
    if (!ok || f.read(p, n) != static_cast<int>(n)) {
      ok = false;
      return;
    }
    crc = save::crc32Update(crc, static_cast<const uint8_t*>(p), n);
  };

  uint8_t h[kIndexHeaderSize];
  read(h, sizeof(h));
  const uint16_t sectionCount = getLe16(h + 10);
  ok = ok && getLe16(h) == kIndexMagic && getLe16(h + 2) == kIndexVersion && getLe32(h + 4) == builtinHash_ &&
       h[8] == fileCount_ && builtinCount + sectionCount <= kMaxSections;
  for (uint8_t i = 0; ok && i < fileCount_; ++i) {
    uint8_t stamp[kNameMax + 8];
    read(stamp, sizeof(stamp));
    ok = ok && memcmp(stamp, files_[i].name, kNameMax) == 0 && getLe32(stamp + kNameMax) == files_[i].size &&
         getLe32(stamp + kNameMax + 4) == files_[i].modified;
  }
  if (ok) read(index_.sections + builtinCount, sectionCount * sizeof(PackSection));
  uint8_t stored[4] = {};
  if (ok && f.read(stored, 4) != 4) ok = false;
  ok = ok && getLe32(stored) == crc;
  f.close();

  for (uint16_t i = 0; ok && i < sectionCount; ++i) {
    const PackSection& s = index_.sections[builtinCount + i];
    ok = s.source >= 1 && s.source <= fileCount_;
  }
  if (!ok) {
    LOG_INF("CULT", "Pack index out of date; rebuilding");
    index_.count = builtinCount;
    return false;
  }
  index_.count = static_cast<uint16_t>(builtinCount + sectionCount);
  sdProblems_ = getLe16(h + 12);
  return true;
}

void PackStore::saveCache(const uint16_t builtinCount) {
  HalFile f;
  if (!Storage.openFileForWrite("CULT", kIndexTmpPath, f)) {
    LOG_ERR("CULT", "Could not write the pack index");
    return;
  }
  bool ok = true;
  uint32_t crc = 0;
  auto write = [&](const void* p, const size_t n) {
    if (ok && f.write(static_cast<const uint8_t*>(p), n) != n) ok = false;
    crc = save::crc32Update(crc, static_cast<const uint8_t*>(p), n);
  };
  const uint16_t sectionCount = static_cast<uint16_t>(index_.count - builtinCount);
  uint8_t h[kIndexHeaderSize] = {};
  putLe16(h, kIndexMagic);
  putLe16(h + 2, kIndexVersion);
  putLe32(h + 4, builtinHash_);
  h[8] = fileCount_;
  putLe16(h + 10, sectionCount);
  putLe16(h + 12, sdProblems_);
  write(h, sizeof(h));
  for (uint8_t i = 0; i < fileCount_; ++i) {
    uint8_t stamp[kNameMax + 8] = {};
    memcpy(stamp, files_[i].name, kNameMax);
    putLe32(stamp + kNameMax, files_[i].size);
    putLe32(stamp + kNameMax + 4, files_[i].modified);
    write(stamp, sizeof(stamp));
  }
  // Disabled flags depend on every pack, so they are worked out after loading, not stored.
  for (uint16_t i = builtinCount; i < index_.count; ++i) {
    PackSection s = index_.sections[i];
    s.flags &= ~PackSection::kDisabled;
    write(&s, sizeof(s));
  }
  uint8_t c[4];
  putLe32(c, crc);
  if (ok && f.write(c, 4) != 4) ok = false;
  ok = f.sync() && ok;
  f.close();
  if (!ok) {
    LOG_ERR("CULT", "Writing the pack index failed");
    Storage.remove(kIndexTmpPath);
    return;
  }
  if (Storage.exists(kIndexPath)) Storage.remove(kIndexPath);
  if (!Storage.rename(kIndexTmpPath, kIndexPath)) {
    LOG_ERR("CULT", "Could not publish the pack index");
  }
}

// --- Rescan -----------------------------------------------------------------------------

void PackStore::rescan(const uint16_t builtinCount) {
  rebuilt_ = true;
  index_.count = builtinCount;
  index_.overflowed = false;

  // Borrowed for the rescan only and freed when it returns: one line buffer,
  // the findings, room for the first problems in full (3.2 KB) and the
  // checker (~0.6 KB). A rescan only happens after pack files change.
  auto line = makeUniqueNoThrow<char[]>(kScanLineMax);
  auto findings = makeUniqueNoThrow<ScanFindings>();
  if (!line || !findings) {
    LOG_ERR("CULT", "Out of memory rescanning packs; SD packs skipped this time");
    fileCount_ = 0;
    return;
  }
  auto messages = makeUniqueNoThrow<char[]>(kReportMessages * kReportMessageSize);
  if (!messages) {
    LOG_ERR("CULT", "Out of memory for pack report messages (%u bytes); problems will only be counted",
            static_cast<unsigned>(kReportMessages * kReportMessageSize));
  }
  findings->messages = messages.get();

  for (uint8_t i = 0; i < fileCount_; ++i) {
    PackParser parser(index_, static_cast<uint8_t>(i + 1));
    parseSdFile(static_cast<uint8_t>(i + 1), parser, line.get(), kScanLineMax);
    findings->badHeaders[i] = parser.badHeaders();
    findings->firstBadHeaderLine[i] = parser.firstBadHeaderLine();
    findings->orphanLines[i] = parser.linesOutsideSections();
  }
  findings->overflowed = index_.overflowed;
  line.reset();

  for (uint16_t i = 0; i < index_.count; ++i) index_.sections[i].flags &= ~PackSection::kDisabled;
  index_.applyReplacements();
  auto checker = makeUniqueNoThrow<PackChecker>(index_, *this, PackCheckSink{&collectProblem, findings.get()});
  if (checker) {
    checker->run(1);
  } else {
    LOG_ERR("CULT", "Out of memory for the pack checker; packs not checked");
  }
  checker.reset();
  closeFile();

  uint32_t total = findings->checkerProblems + (findings->overflowed ? 1u : 0u);
  for (uint8_t i = 0; i < fileCount_; ++i) total += findings->badHeaders[i] + findings->orphanLines[i];
  sdProblems_ = static_cast<uint16_t>(total < UINT16_MAX ? total : UINT16_MAX);
  writeReport(*findings);
  saveCache(builtinCount);
}

void PackStore::parseSdFile(const uint8_t source, PackParser& parser, char* line, const size_t lineSize) {
  if (!openSource(source)) {
    parser.finish();
    return;
  }
  seekTo(0);
  size_t lineLen = 0;
  while (readLine(line, lineSize, lineLen)) {
    const size_t kept = lineLen < lineSize - 1 ? lineLen : lineSize - 1;
    parser.feed(line, kept, bufFileOffset_ + static_cast<uint32_t>(bufPos_));
  }
  parser.finish();
}

void PackStore::writeReport(const ScanFindings& f) {
  if (sdProblems_ == 0) {
    if (Storage.exists(kReportPath)) Storage.remove(kReportPath);
    return;
  }
  HalFile out;
  if (!Storage.openFileForWrite("CULT", kReportPath, out)) {
    LOG_ERR("CULT", "Could not write %s", kReportPath);
    return;
  }
  char text[kReportMessageSize + kNameMax + 8];
  auto line = [&](const char* s) {
    out.write(reinterpret_cast<const uint8_t*>(s), strlen(s));
    out.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  };
  line("Path of Immortal Reading: content pack report");
  snprintf(text, sizeof(text), "%u problem(s) in %u SD pack file(s). The built-in pack is not affected.",
           static_cast<unsigned>(sdProblems_), static_cast<unsigned>(fileCount_));
  line(text);
  line("");
  for (uint8_t i = 0; i < fileCount_; ++i) {
    if (f.badHeaders[i] > 0) {
      snprintf(text, sizeof(text), "%s: %u bad [header] line(s), first on line %u (keys use a-z 0-9 . _ - @ ~)",
               files_[i].name, f.badHeaders[i], f.firstBadHeaderLine[i]);
      line(text);
    }
    if (f.orphanLines[i] > 0) {
      snprintf(text, sizeof(text), "%s: %u line(s) outside any [section] are ignored", files_[i].name,
               f.orphanLines[i]);
      line(text);
    }
  }
  if (f.overflowed) {
    snprintf(text, sizeof(text), "Too many sections: only the first %u are used.", kMaxSections);
    line(text);
  }
  for (uint8_t k = 0; k < f.kept; ++k) {
    const uint8_t src = f.sources[k];
    const char* name = (src >= 1 && src <= fileCount_) ? files_[src - 1].name : "built-in pack";
    snprintf(text, sizeof(text), "%s: %s", name, f.messages + k * kReportMessageSize);
    line(text);
  }
  if (f.checkerProblems > f.kept) {
    snprintf(text, sizeof(text), "... and %u more.", static_cast<unsigned>(f.checkerProblems - f.kept));
    line(text);
  }
  out.sync();
  out.close();
}

// --- Reading entries --------------------------------------------------------------------

void PackStore::closeFile() {
  if (file_) file_.close();
  openSource_ = -1;
  lastSection_ = nullptr;
  bufLen_ = bufPos_ = 0;
  bufFileOffset_ = 0;
}

bool PackStore::openSource(const uint8_t source) {
  if (openSource_ == source && file_) return true;
  closeFile();
  if (source < 1 || source > fileCount_) return false;
  char path[kPathMax];
  makePath(source, path, sizeof(path));
  if (!Storage.openFileForRead("CULT", path, file_)) {
    LOG_ERR("CULT", "Could not open pack %s", path);
    return false;
  }
  openSource_ = source;
  return true;
}

bool PackStore::seekTo(const uint32_t pos) {
  if (pos >= bufFileOffset_ && pos <= bufFileOffset_ + bufLen_) {
    bufPos_ = pos - bufFileOffset_;
    return true;
  }
  bufFileOffset_ = pos;
  bufLen_ = bufPos_ = 0;
  return file_.seek(pos);
}

bool PackStore::fillBuffer() {
  bufFileOffset_ += static_cast<uint32_t>(bufLen_);
  const int n = file_.read(buf_, sizeof(buf_));
  bufLen_ = n > 0 ? static_cast<size_t>(n) : 0;
  bufPos_ = 0;
  return bufLen_ > 0;
}

// Reads one line (without its ending) into out, cut at outLen - 1 bytes;
// lineLen is the full length. False at the end of the file.
bool PackStore::readLine(char* out, const size_t outLen, size_t& lineLen) {
  lineLen = 0;
  bool any = false;
  while (true) {
    if (bufPos_ >= bufLen_ && !fillBuffer()) break;
    any = true;
    const char c = buf_[bufPos_++];
    if (c == '\n') break;
    if (lineLen + 1 < outLen) out[lineLen] = c;
    ++lineLen;
  }
  if (outLen > 0) out[lineLen < outLen - 1 ? lineLen : outLen - 1] = '\0';
  return any;
}

bool PackStore::readEntry(const PackSection& section, const uint16_t entry, char* buf, const size_t len) {
  if (buf == nullptr || len == 0) return false;
  buf[0] = '\0';
  if (section.source == 0) return builtin_.readEntry(section, entry, buf, len);
  if (entry >= section.count || !openSource(section.source)) return false;

  uint16_t seen = 0;
  if (lastSection_ == &section && entry > lastEntry_) {
    seekTo(afterLastEntry_);  // reading forward through the same section
    seen = static_cast<uint16_t>(lastEntry_ + 1);
  } else if (!seekTo(section.offset)) {
    return false;
  }
  size_t lineLen = 0;
  while (readLine(buf, len, lineLen)) {
    const char* b = buf;
    const char* e = buf + strlen(buf);
    if (isSkippedLine(b, e)) continue;
    if (seen++ != entry) continue;
    trimRange(b, e);
    const size_t n = static_cast<size_t>(e - b);
    memmove(buf, b, n);
    buf[n] = '\0';
    lastSection_ = &section;
    lastEntry_ = entry;
    afterLastEntry_ = bufFileOffset_ + static_cast<uint32_t>(bufPos_);
    return true;
  }
  return false;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
