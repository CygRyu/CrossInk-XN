#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <HalStorage.h>
#include <Logging.h>

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

}  // namespace chronicle
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
