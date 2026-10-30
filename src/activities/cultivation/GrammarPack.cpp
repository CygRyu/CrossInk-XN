#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstring>

#include "GrammarPack.h"

namespace cultivation {

uint32_t packKeyHash(const char* key, const size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(key[i]);
    h *= 16777619u;
  }
  return h;
}

bool PackIndex::add(const PackSection& s) {
  if (count >= capacity || sections == nullptr) {
    overflowed = true;
    return false;
  }
  sections[count++] = s;
  return true;
}

void PackIndex::applyReplacements() {
  for (uint16_t i = 0; i < count; ++i) {
    if ((sections[i].flags & PackSection::kReplace) == 0) continue;
    for (uint16_t j = 0; j < i; ++j) {
      if (sections[j].keyHash == sections[i].keyHash) sections[j].flags |= PackSection::kDisabled;
    }
  }
}

uint32_t PackIndex::entryCount(const uint32_t keyHash) const {
  uint32_t n = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const PackSection& s = sections[i];
    if (s.keyHash == keyHash && (s.flags & PackSection::kDisabled) == 0) n += s.count;
  }
  return n;
}

const PackSection* PackIndex::locate(const uint32_t keyHash, uint32_t n, uint16_t& indexInSection) const {
  for (uint16_t i = 0; i < count; ++i) {
    const PackSection& s = sections[i];
    if (s.keyHash != keyHash || (s.flags & PackSection::kDisabled) != 0) continue;
    if (n < s.count) {
      indexInSection = static_cast<uint16_t>(n);
      return &s;
    }
    n -= s.count;
  }
  return nullptr;
}

bool isValidKeyChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '@' || c == '~';
}

void trimRange(const char*& begin, const char*& end) {
  while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) ++begin;
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
}

bool isSkippedLine(const char* begin, const char* end) {
  trimRange(begin, end);
  return begin == end || *begin == '#';
}

bool parseHeader(const char* begin, const char* end, const char*& keyBegin, size_t& keyLen, bool& replace) {
  trimRange(begin, end);
  if (end - begin < 3 || *begin != '[' || end[-1] != ']') return false;
  const char* k = begin + 1;
  const char* kEnd = end - 1;
  replace = false;
  if (*k == '!') {
    replace = true;
    ++k;
  }
  if (k >= kEnd || static_cast<size_t>(kEnd - k) > kPackKeyMax) return false;
  for (const char* p = k; p < kEnd; ++p) {
    if (!isValidKeyChar(*p)) return false;
  }
  keyBegin = k;
  keyLen = static_cast<size_t>(kEnd - k);
  return true;
}

namespace {

// A line shaped like a header ("[...]" with no spaces, '|', '{' or '[' inside)
// is meant as one. If it does not parse, it is a bad header, not an entry.
bool looksLikeHeader(const char* begin, const char* end) {
  trimRange(begin, end);
  if (end - begin < 2 || *begin != '[' || end[-1] != ']') return false;
  for (const char* p = begin + 1; p < end - 1; ++p) {
    if (*p == ' ' || *p == '|' || *p == '{' || *p == '[' || *p == ']' || *p == '\\') return false;
  }
  return true;
}

}  // namespace

void PackParser::flush() {
  if (inSection_) index_.add(current_);
  inSection_ = false;
}

void PackParser::feed(const char* line, const size_t len, const uint32_t nextLineOffset) {
  ++lineNumber_;
  const char* begin = line;
  const char* end = line + len;
  if (isSkippedLine(begin, end)) return;

  const char* key = nullptr;
  size_t keyLen = 0;
  bool replace = false;
  if (parseHeader(begin, end, key, keyLen, replace)) {
    flush();
    current_ = PackSection{};
    current_.keyHash = packKeyHash(key, keyLen);
    current_.offset = nextLineOffset;
    current_.source = source_;
    if (replace) current_.flags |= PackSection::kReplace;
    if (keyLen > 6 && memcmp(key, "event.", 6) == 0) current_.flags |= PackSection::kEvent;
    inSection_ = true;
    return;
  }
  if (looksLikeHeader(begin, end)) {
    if (badHeaders_ == 0) firstBadHeaderLine_ = lineNumber_;
    ++badHeaders_;
    flush();  // entries after a bad header belong to no section
    return;
  }
  if (!inSection_) {
    ++orphanLines_;
    return;
  }
  if (current_.count < UINT16_MAX) ++current_.count;
}

void PackParser::finish() { flush(); }

void parsePackText(const char* text, const size_t len, PackParser& parser) {
  size_t start = 0;
  while (start < len) {
    size_t stop = start;
    while (stop < len && text[stop] != '\n') ++stop;
    const size_t next = stop < len ? stop + 1 : stop;
    parser.feed(text + start, stop - start, static_cast<uint32_t>(next));
    start = next;
  }
  parser.finish();
}

void MemoryPackReader::setSource(const uint8_t source, const char* text, const size_t len) {
  if (source >= kMaxSources) return;
  texts_[source] = text;
  lengths_[source] = len;
}

bool MemoryPackReader::readEntry(const PackSection& section, const uint16_t entry, char* buf, const size_t len) {
  if (buf == nullptr || len == 0) return false;
  buf[0] = '\0';
  if (section.source >= kMaxSources || texts_[section.source] == nullptr || entry >= section.count) return false;
  const char* text = texts_[section.source];
  const size_t total = lengths_[section.source];
  size_t pos = section.offset;
  uint16_t seen = 0;
  while (pos < total) {
    size_t stop = pos;
    while (stop < total && text[stop] != '\n') ++stop;
    const char* b = text + pos;
    const char* e = text + stop;
    pos = stop < total ? stop + 1 : stop;
    if (isSkippedLine(b, e)) continue;
    if (seen++ != entry) continue;
    trimRange(b, e);
    size_t n = static_cast<size_t>(e - b);
    if (n > len - 1) n = len - 1;
    memcpy(buf, b, n);
    buf[n] = '\0';
    return true;
  }
  return false;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
