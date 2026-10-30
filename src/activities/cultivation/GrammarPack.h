#pragma once

#include <cstddef>
#include <cstdint>

// Content packs for the grammar engine (DOC: Events, grammar engine and
// Chronicle). A pack is plain text:
//
//   # comment
//   [place.region]          a list: one entry per line
//   the Mistveil Marshes
//   {name.mountain} Peak
//
//   [!rumor]                "!" replaces every earlier list with this key
//   [log.stage@1]           plane layer: used instead of log.stage on plane 1
//   [weather~demonic]       theme layer: used inside a themed event
//   [event.ruins]           an event: its lines are the event body
//
// Blank lines and lines starting with '#' are skipped everywhere. The built-in
// pack lives in flash; SD packs come after it, so an SD list with the same key
// adds entries and "[!key]" replaces them.
//
// The index keeps one 12-byte record per section, never per line: reading
// entry i walks the section's lines from its start. Lists are short, so this
// costs a few hundred bytes of reading and saves the per-line offsets.
namespace cultivation {

constexpr size_t kPackKeyMax = 47;    // longest key, without brackets
constexpr size_t kPackLineMax = 191;  // longest line the device reads whole (validator warns)

uint32_t packKeyHash(const char* key, size_t len);  // FNV-1a 32
inline uint32_t packKeyHash(const char* key) {
  size_t n = 0;
  while (key[n] != '\0') ++n;
  return packKeyHash(key, n);
}

struct PackSection {
  uint32_t keyHash = 0;
  uint32_t offset = 0;  // byte offset of the first line after the header, within its source
  uint16_t count = 0;   // entries (non-blank, non-comment lines)
  uint8_t source = 0;   // 0 = built-in pack, 1.. = SD pack file
  uint8_t flags = 0;

  static constexpr uint8_t kReplace = 0x01;   // "[!key]"
  static constexpr uint8_t kDisabled = 0x02;  // replaced by a later "[!key]"
  static constexpr uint8_t kEvent = 0x04;     // key starts with "event."
};
static_assert(sizeof(PackSection) == 12, "index records are written to index.bin as-is");

// A fixed-capacity array of sections. The memory belongs to the caller
// (allocated once in onEnter()), so building never allocates.
struct PackIndex {
  PackSection* sections = nullptr;
  uint16_t count = 0;
  uint16_t capacity = 0;
  bool overflowed = false;  // more sections than capacity; the rest were skipped

  bool add(const PackSection& s);
  // Marks every section that a later "[!key]" section replaces. Call once,
  // after all sources were added in order (built-in first).
  void applyReplacements();
  // Total entries for a key across enabled sections; 0 if none.
  uint32_t entryCount(uint32_t keyHash) const;
  // Section and index within it of the n-th entry of a key (n < entryCount).
  const PackSection* locate(uint32_t keyHash, uint32_t n, uint16_t& indexInSection) const;
};

// Turns pack text into sections, one line at a time, so an SD file can be
// streamed without holding it in memory. Offsets are the byte offset of each
// line within its source.
class PackParser {
 public:
  PackParser(PackIndex& index, uint8_t source) : index_(index), source_(source) {}

  // `line` excludes the line ending. `nextLineOffset` is the offset right
  // after this line's ending (where the next line starts).
  void feed(const char* line, size_t len, uint32_t nextLineOffset);
  void finish();

  uint16_t linesOutsideSections() const { return orphanLines_; }
  uint16_t badHeaders() const { return badHeaders_; }
  uint16_t firstBadHeaderLine() const { return firstBadHeaderLine_; }

 private:
  void flush();

  PackIndex& index_;
  uint8_t source_;
  PackSection current_{};
  bool inSection_ = false;
  uint16_t lineNumber_ = 0;
  uint16_t orphanLines_ = 0;
  uint16_t badHeaders_ = 0;
  uint16_t firstBadHeaderLine_ = 0;
};

// Splits an in-memory pack into lines and feeds them to a parser.
void parsePackText(const char* text, size_t len, PackParser& parser);

// Trims spaces, tabs and '\r' from both ends of [begin, end).
void trimRange(const char*& begin, const char*& end);
// True for lines the parser and readers skip (blank or '#' comment).
bool isSkippedLine(const char* begin, const char* end);
// Parses "[key]" or "[!key]". On success sets key range and replace flag.
bool parseHeader(const char* begin, const char* end, const char*& keyBegin, size_t& keyLen, bool& replace);
bool isValidKeyChar(char c);

// Reads entries of indexed sections. The built-in pack and each SD file are
// sources; the device reader opens SD files, the memory reader serves text
// that is already in memory (built-in pack, host tests).
class PackReader {
 public:
  virtual ~PackReader() = default;
  // Copies entry `entry` of `section` into buf (trimmed, NUL-terminated,
  // cut at len - 1 bytes). Returns false if the entry could not be read.
  virtual bool readEntry(const PackSection& section, uint16_t entry, char* buf, size_t len) = 0;
};

// Reads sources held in memory. Up to kMaxSources texts, indexed by source id.
class MemoryPackReader : public PackReader {
 public:
  static constexpr uint8_t kMaxSources = 8;
  void setSource(uint8_t source, const char* text, size_t len);
  bool readEntry(const PackSection& section, uint16_t entry, char* buf, size_t len) override;

 private:
  const char* texts_[kMaxSources] = {};
  size_t lengths_[kMaxSources] = {};
};

}  // namespace cultivation
