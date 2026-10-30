#pragma once

#include <HalStorage.h>
#include <Memory.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "GrammarPack.h"

// Device side of the content packs: the built-in pack (flash) plus SD packs
// in /.crosspoint/cultivation/lists/*.txt and /.crosspoint/cultivation/events/*.txt.
//
// The index of the SD packs is cached in /.crosspoint/cultivation/index.bin.
// It is rebuilt on the device whenever a pack file is added, removed or
// changed (size or modification time differs) or the firmware's built-in pack
// changed. A rebuild also checks the SD packs and writes what it finds to
// pack_report.txt, or deletes an old report when everything is fine.
//
// Memory, all owned by this object (allocated by the activity in onEnter()):
// 384 index records (4.6 KB, heap, because the count is only known at run
// time and 4.6 KB is too much for a task stack), the file table (~0.9 KB) and
// a 128-byte read buffer. A rebuild borrows two more buffers (0.5 KB) for its
// duration and the checker (~0.6 KB); nothing else allocates.
namespace cultivation {

struct ScanFindings;  // problems a rescan found, for pack_report.txt

class PackStore : public PackReader {
 public:
  static constexpr uint16_t kMaxSections = 384;
  static constexpr uint8_t kMaxFiles = 15;  // SD sources 1..15
  static constexpr size_t kNameMax = 48;    // "lists/<name>.txt", relative to the game folder

  struct FileStamp {
    char name[kNameMax] = {};
    uint32_t size = 0;
    uint32_t modified = 0;  // packed FAT date/time, 0 when unknown
  };

  PackStore() = default;
  ~PackStore() override { closeFile(); }
  PackStore(const PackStore&) = delete;
  PackStore& operator=(const PackStore&) = delete;

  // Builds the index: built-in pack, then SD packs from index.bin or a rescan.
  // Returns false only when the index memory could not be allocated.
  bool begin();

  const PackIndex& index() const { return index_; }
  bool readEntry(const PackSection& section, uint16_t entry, char* buf, size_t len) override;
  // Closes the SD file kept open between reads (one open file at a time on the card).
  void closeFile();

  uint8_t sdFileCount() const { return fileCount_; }
  uint16_t sdProblems() const { return sdProblems_; }
  bool rebuilt() const { return rebuilt_; }

  static constexpr char kRoot[] = "/.crosspoint/cultivation";
  static constexpr char kIndexPath[] = "/.crosspoint/cultivation/index.bin";
  static constexpr char kReportPath[] = "/.crosspoint/cultivation/pack_report.txt";

 private:
  void listSdFiles();
  void addFilesFrom(const char* folder);
  bool loadCache(uint16_t builtinCount);
  void rescan(uint16_t builtinCount);
  void parseSdFile(uint8_t source, PackParser& parser, char* line, size_t lineSize);
  void saveCache(uint16_t builtinCount);
  void writeReport(const ScanFindings& findings);
  bool openSource(uint8_t source);
  bool fillBuffer();
  bool readLine(char* out, size_t outLen, size_t& lineLen);
  bool seekTo(uint32_t pos);
  void makePath(uint8_t source, char* out, size_t len) const;

  std::unique_ptr<PackSection[]> sections_;
  PackIndex index_;
  MemoryPackReader builtin_;
  uint32_t builtinHash_ = 0;
  FileStamp files_[kMaxFiles];
  uint8_t fileCount_ = 0;
  uint16_t sdProblems_ = 0;
  bool rebuilt_ = false;

  // Reading state: one open file, a small buffer, and where the last entry
  // read ended, so reading a section in order (the checker) never rewinds.
  HalFile file_;
  int openSource_ = -1;
  char buf_[128] = {};
  size_t bufLen_ = 0;
  size_t bufPos_ = 0;
  uint32_t bufFileOffset_ = 0;  // file offset of buf_[0]
  const PackSection* lastSection_ = nullptr;
  uint16_t lastEntry_ = 0;
  uint32_t afterLastEntry_ = 0;
};

}  // namespace cultivation
