#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <HalStorage.h>
#include <Logging.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Chronicle.h"
#include "GameValues.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace chronicle {
namespace {

constexpr uint32_t kKindOpening = 54;  // contentSeed kind; 50-53 are the rumor and life log
constexpr size_t kTailSize = 256;      // longer than any line the game writes

static const char* const kSeasonLabels[4] = {"Spring", "Summer", "Autumn", "Winter"};
static const char* const kBirthLists[static_cast<int>(Origin::Count)] = {
    "chronicle.birth.farmer", "chronicle.birth.clan", "chronicle.birth.orphan", "chronicle.birth.servant",
    "chronicle.birth.herbalist"};

struct Tail {
  bool empty = true;              // no file, or a file with nothing in it
  bool endsWithNewline = true;    // false after a write cut short by a power loss
  char lastLine[kTailSize] = {};  // the last non-empty line, if it fit in the tail
};

Tail readTail() {
  Tail tail;
  FsFile f;
  if (!Storage.exists(kPath) || !Storage.openFileForRead("CULT", kPath, f)) return tail;
  const size_t size = f.fileSize();
  if (size == 0) {
    f.close();
    return tail;
  }
  tail.empty = false;
  char buf[kTailSize];
  const size_t take = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
  if (!f.seek(size - take) || f.read(buf, take) != static_cast<int>(take)) {
    f.close();
    tail.endsWithNewline = false;  // be safe: start the next write on a fresh line
    return tail;
  }
  f.close();
  buf[take] = '\0';
  tail.endsWithNewline = buf[take - 1] == '\n';

  size_t end = take;
  while (end > 0 && (buf[end - 1] == '\n' || buf[end - 1] == '\r')) --end;
  size_t start = end;
  while (start > 0 && buf[start - 1] != '\n') --start;
  // A line that began before the tail is not known completely; leave it out.
  if (start == 0 && take < size) return tail;
  memcpy(tail.lastLine, buf + start, end - start);
  tail.lastLine[end - start] = '\0';
  return tail;
}

class Appender {
 public:
  Appender() { file_ = Storage.open(kPath, O_WRONLY | O_CREAT | O_APPEND); }
  ~Appender() { close(); }

  bool isOpen() const { return static_cast<bool>(file_); }
  void text(const char* s) {
    const size_t len = strlen(s);
    if (ok_ && len > 0 && file_.write(reinterpret_cast<const uint8_t*>(s), len) != len) ok_ = false;
  }
  void line(const char* s) {
    text(s);
    text("\n");
  }
  bool close() {
    if (!file_) return ok_;
    file_.flush();
    ok_ = file_.close() && ok_;
    return ok_;
  }

 private:
  FsFile file_;
  bool ok_ = true;
};

// One paragraph of the opening from list `key`, or nothing if it is missing.
void openingLine(Appender& out, const char* key, const uint32_t seed, Grammar& grammar, const GrammarContext& ctx) {
  if (!grammar.hasKey(key, ctx)) return;
  char tmpl[64];
  snprintf(tmpl, sizeof(tmpl), "{%s}", key);
  grammar.beginScene();
  const char* text = grammar.expand(tmpl, seed, ctx);
  if (grammar.missing() > 0 || text == nullptr || text[0] == '\0') return;
  out.line(text);
}

}  // namespace

bool exists() { return Storage.exists(kPath); }

int32_t ageInLine(const char* line) {
  if (line == nullptr) return -1;
  for (const char* label : kSeasonLabels) {
    const size_t n = strlen(label);
    if (strncmp(line, label, n) != 0 || strncmp(line + n, ", age ", 6) != 0) continue;
    const char* digits = line + n + 6;
    if (*digits < '0' || *digits > '9') return -1;
    return static_cast<int32_t>(strtol(digits, nullptr, 10));
  }
  return -1;
}

bool writeOpening(const GameState& state, Grammar* grammar) {
  Storage.mkdir(save::kDirectory);
  const Tail tail = readTail();
  Appender out;
  if (!out.isOpen()) {
    LOG_ERR("CULT", "Could not open %s", kPath);
    return false;
  }
  char name[kNameMaxBytes + 1] = {};
  memcpy(name, state.character.name.data(), kNameMaxBytes);

  char line[96];
  if (tail.empty) {
    snprintf(line, sizeof(line), "The Chronicle of %s", name);
    out.line(line);
  } else if (!tail.endsWithNewline) {
    out.text("\n");
  }
  out.text("\n");
  snprintf(line, sizeof(line), "Life %u: %s", static_cast<unsigned>(lifeNumberOf(state)), name);
  out.line(line);
  out.text("\n");

  if (grammar != nullptr) {
    GrammarContext ctx;
    ctx.values = gameValues(state);
    const uint32_t world = worldSeedOf(state);
    const int origin = static_cast<int>(state.character.origin);
    if (origin < static_cast<int>(Origin::Count)) {
      openingLine(out, kBirthLists[origin], contentSeed(world, kKindOpening, 0), *grammar, ctx);
    }
    openingLine(out, "chronicle.awakening", contentSeed(world, kKindOpening, 1), *grammar, ctx);
    // DOC: Past Life Memories, from the totals recorded before this life began.
    if (state.baseline.pages > 0 || state.baseline.completedBooks > 0) {
      openingLine(out, "chronicle.past", contentSeed(world, kKindOpening, 2), *grammar, ctx);
    }
  }
  const bool ok = out.close();
  if (!ok) {
    LOG_ERR("CULT", "Writing the Chronicle opening failed");
  }
  return ok;
}

bool appendVisit(const lifelog::LifeLog& log) { return appendLines(log.lines, log.count); }

bool appendLines(const lifelog::Line* lines, const uint8_t count) {
  if (lines == nullptr || count == 0) return true;
  const Tail tail = readTail();
  const char* last = lines[count - 1].text;
  if (strcmp(tail.lastLine, last) == 0) {
    LOG_INF("CULT", "Chronicle already has this visit's lines");
    return true;
  }

  Storage.mkdir(save::kDirectory);
  Appender out;
  if (!out.isOpen()) {
    LOG_ERR("CULT", "Could not open %s", kPath);
    return false;
  }
  if (!tail.empty && !tail.endsWithNewline) out.text("\n");
  int32_t age = ageInLine(tail.lastLine);
  char heading[24];
  for (uint8_t i = 0; i < count; ++i) {
    const lifelog::Line& line = lines[i];
    const auto lineAge = static_cast<int32_t>(lifelog::ageOfSeason(line.season));
    if (lineAge != age) {
      snprintf(heading, sizeof(heading), "\nAge %ld", static_cast<long>(lineAge));
      out.line(heading);
      age = lineAge;
    }
    out.line(line.text);
  }
  const bool ok = out.close();
  if (!ok) {
    LOG_ERR("CULT", "Appending to the Chronicle failed");
  }
  return ok;
}

void exportPath(const GameState& state, char* out, const size_t len) {
  char name[kNameMaxBytes + 1];
  memcpy(name, state.character.name.data(), kNameMaxBytes);
  name[kNameMaxBytes] = '\0';
  for (char* c = name; *c != '\0'; ++c) {
    if (strchr("\\/:*?\"<>|", *c) != nullptr || static_cast<unsigned char>(*c) < 0x20) *c = '_';
  }
  if (name[0] == '\0' || strspn(name, " .") == strlen(name)) snprintf(name, sizeof(name), "%s", "Nameless");
  snprintf(out, len, "%s/The Chronicle of %s.txt", kExportDirectory, name);
}

bool exportCopy(const GameState& state, char* pathOut, const size_t pathLen) {
  char path[96];
  exportPath(state, path, sizeof(path));
  if (pathOut != nullptr && pathLen > 0) snprintf(pathOut, pathLen, "%s", path);
  char tmpPath[104];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);

  FsFile in;
  if (!Storage.openFileForRead("CULT", kPath, in)) return false;
  Storage.mkdir(kExportDirectory);
  if (Storage.exists(tmpPath)) Storage.remove(tmpPath);
  FsFile out;
  if (!Storage.openFileForWrite("CULT", tmpPath, out)) {
    in.close();
    LOG_ERR("CULT", "Could not create %s", tmpPath);
    return false;
  }
  const size_t total = in.fileSize();
  uint8_t buf[256];
  bool ok = true;
  size_t copied = 0;
  while (ok) {
    const int got = in.read(buf, sizeof(buf));
    if (got <= 0) break;
    ok = out.write(buf, static_cast<size_t>(got)) == static_cast<size_t>(got);
    copied += static_cast<size_t>(got);
  }
  in.close();
  ok = out.close() && ok && copied == total;
  if (ok && Storage.exists(path)) ok = Storage.remove(path);
  if (ok) ok = Storage.rename(tmpPath, path);
  if (!ok) {
    LOG_ERR("CULT", "Exporting the Chronicle to %s failed", path);
    if (Storage.exists(tmpPath)) Storage.remove(tmpPath);
    return false;
  }
  LOG_INF("CULT", "Chronicle exported to %s (%lu bytes)", path, static_cast<unsigned long>(copied));
  return true;
}

// --- Reading helpers for the viewer (v0.2.3) -----------------------------------

namespace {

constexpr uint32_t kWindow = 256;  // bytes looked at per read
constexpr uint32_t kPeek = 24;     // bytes past a line start, enough for "Age 12345\r\n"

// Calls visit(offset, text, len) for every line start in [lowest, before),
// newest first; `text` is the first `len` (<= kPeek) bytes of that line.
// visit returns false to stop. Reads at most kWindow + kPeek + 1 bytes a time.
template <typename Visit>
void scanBack(FsFile& f, const uint32_t size, uint32_t before, const uint32_t lowest, Visit visit) {
  char buf[kWindow + kPeek + 1];
  uint32_t hi = before < size ? before : size;
  while (hi > lowest) {
    const uint32_t lo = hi - lowest > kWindow ? hi - kWindow : lowest;
    const uint32_t readFrom = lo > 0 ? lo - 1 : 0;
    const uint32_t readTo = hi + kPeek < size ? hi + kPeek : size;
    const uint32_t n = readTo - readFrom;
    if (!f.seek(readFrom) || f.read(buf, n) != static_cast<int>(n)) return;
    for (uint32_t p = hi; p-- > lo;) {
      const uint32_t i = p - readFrom;
      if (p != 0 && buf[i - 1] != '\n') continue;
      const uint32_t avail = n - i < kPeek ? n - i : kPeek;
      if (!visit(p, buf + i, static_cast<size_t>(avail))) return;
    }
    hi = lo;
  }
}

// The same going forward: every line start in [from, size), oldest first.
template <typename Visit>
void scanForward(FsFile& f, const uint32_t size, const uint32_t from, Visit visit) {
  char buf[kWindow + kPeek + 1];
  uint32_t lo = from;
  while (lo < size) {
    const uint32_t hi = size - lo > kWindow ? lo + kWindow : size;
    const uint32_t readFrom = lo > 0 ? lo - 1 : 0;
    const uint32_t readTo = hi + kPeek < size ? hi + kPeek : size;
    const uint32_t n = readTo - readFrom;
    if (!f.seek(readFrom) || f.read(buf, n) != static_cast<int>(n)) return;
    for (uint32_t p = lo; p < hi; ++p) {
      const uint32_t i = p - readFrom;
      if (p != 0 && buf[i - 1] != '\n') continue;
      const uint32_t avail = n - i < kPeek ? n - i : kPeek;
      if (!visit(p, buf + i, static_cast<size_t>(avail))) return;
    }
    lo = hi;
  }
}

bool blankLine(const char* text, const size_t len) {
  return len == 0 || text[0] == '\n' || (text[0] == '\r' && (len == 1 || text[1] == '\n'));
}

}  // namespace

int32_t headingAge(const char* text, const size_t len) {
  if (text == nullptr || len < 5 || strncmp(text, "Age ", 4) != 0) return -1;
  int32_t age = 0;
  size_t i = 4;
  for (; i < len && text[i] >= '0' && text[i] <= '9'; ++i) {
    if (age > 100000000) return -1;
    age = age * 10 + (text[i] - '0');
  }
  if (i == 4) return -1;                                         // no digits
  if (i < len && text[i] != '\n' && text[i] != '\r') return -1;  // "Age 5x": not a heading
  return age;
}

uint32_t lineStartAtOrBefore(FsFile& f, const uint32_t size, const uint32_t pos) {
  uint32_t found = 0;
  scanBack(f, size, (pos < size ? pos : size) + 1, 0, [&](const uint32_t p, const char*, size_t) {
    found = p;
    return false;
  });
  return found;
}

uint8_t lineStartsIn(FsFile& f, const uint32_t size, const uint32_t from, const uint32_t to, uint32_t* out,
                     const uint8_t max) {
  if (max == 0) return 0;
  uint8_t count = 0;
  scanBack(f, size, to, from, [&](const uint32_t p, const char*, size_t) {
    out[count++] = p;  // newest first for now
    return count < max;
  });
  for (uint8_t i = 0; i < count / 2; ++i) {  // oldest first
    const uint32_t t = out[i];
    out[i] = out[count - 1 - i];
    out[count - 1 - i] = t;
  }
  return count;
}

uint32_t findHeadingForward(FsFile& f, const uint32_t size, const uint32_t from, const int32_t minAge) {
  uint32_t found = kNotFound;
  scanForward(f, size, from, [&](const uint32_t p, const char* text, const size_t len) {
    if (headingAge(text, len) < minAge) return true;  // -1 for non-headings, always below
    found = p;
    return false;
  });
  return found;
}

uint32_t findHeadingBackward(FsFile& f, const uint32_t size, const uint32_t before, const int32_t maxAge,
                             int32_t* ageOut) {
  uint32_t found = kNotFound;
  scanBack(f, size, before, 0, [&](const uint32_t p, const char* text, const size_t len) {
    const int32_t age = headingAge(text, len);
    if (age < 0 || age > maxAge) return true;
    found = p;
    if (ageOut != nullptr) *ageOut = age;
    return false;
  });
  return found;
}

uint32_t withHeadingAbove(FsFile& f, const uint32_t size, const uint32_t start) {
  uint32_t result = start;
  scanBack(f, size, start, 0, [&](const uint32_t p, const char* text, const size_t len) {
    if (blankLine(text, len)) return true;
    if (headingAge(text, len) >= 0 || (len >= 5 && strncmp(text, "Life ", 5) == 0)) result = p;
    return false;
  });
  return result;
}

int32_t pageAge(FsFile& f, const uint32_t size, const uint32_t start) {
  // The page's first line that is not blank.
  uint32_t first = kNotFound;
  scanForward(f, size, start, [&](const uint32_t p, const char* text, const size_t len) {
    if (blankLine(text, len)) return true;
    first = p;
    return false;
  });
  if (first != kNotFound) {
    char line[48];
    if (f.seek(first)) {
      const int got = f.read(line, sizeof(line) - 1);
      if (got > 0) {
        line[got] = '\0';
        const int32_t heading = headingAge(line, static_cast<size_t>(got));
        if (heading >= 0) return heading;
        const int32_t season = ageInLine(line);
        if (season >= 0) return season;
      }
    }
  }
  int32_t age = -1;
  findHeadingBackward(f, size, start, INT32_MAX, &age);
  return age;
}

uint32_t jumpBack(FsFile& f, const uint32_t size, const uint32_t start, const int32_t age) {
  if (age < 0) return 0;  // already in the opening chapter
  const uint32_t at = findHeadingBackward(f, size, start, age - kJumpYears);
  return at == kNotFound ? 0 : at;
}

uint32_t jumpForward(FsFile& f, const uint32_t size, const uint32_t start, const int32_t age) {
  const int32_t from = age < 0 ? static_cast<int32_t>(kStartAgeYears) : age;
  return findHeadingForward(f, size, start + 1, from + kJumpYears);
}

}  // namespace chronicle
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
