#pragma once

#include <cstddef>
#include <cstdint>

#include "GrammarPack.h"

// Validates content packs (DOC: "validates packs: missing keys, unbalanced
// brackets, unknown conditions"). One checker serves three places:
// - host tests check the built-in pack on every CI run;
// - the device checks SD packs whenever it rebuilds index.bin and writes the
//   findings to /.crosspoint/cultivation/pack_report.txt;
// - the desktop `cultivation_pack_check` tool (test/cultivation) checks pack
//   files before they go on the card.
//
// Messages are for pack authors (a report file or a terminal), not UI text.
namespace cultivation {

struct PackCheckSink {
  // Called once per problem. `source` is the pack the entry came from.
  void (*problem)(void* user, uint8_t source, const char* message) = nullptr;
  void* user = nullptr;
};

class PackChecker {
 public:
  static constexpr uint8_t kMaxVarNames = 64;

  PackChecker(const PackIndex& index, PackReader& reader, PackCheckSink sink)
      : index_(index), reader_(reader), sink_(sink) {}

  // Checks every enabled section whose source is >= firstSource (0 = all).
  // Returns the number of problems reported.
  uint32_t run(uint8_t firstSource = 0);

 private:
  void collectVarNames(const char* line);
  void checkText(const char* begin, const char* end, uint8_t source, const char* line);
  void checkToken(const char* begin, const char* end, uint8_t source, const char* line);
  void checkEventLine(const char* line, uint8_t source);
  void checkRequirement(const char* begin, const char* end, uint8_t source, const char* line);
  void checkEffects(const char* begin, const char* end, uint8_t source, const char* line);
  void report(uint8_t source, const char* what, const char* detail, size_t detailLen, const char* line);

  const PackIndex& index_;
  PackReader& reader_;
  PackCheckSink sink_;
  uint32_t problems_ = 0;
  uint32_t varNames_[kMaxVarNames] = {};
  uint8_t varCount_ = 0;
  char line_[kPackLineMax + 2] = {};
  char message_[256] = {};
};

}  // namespace cultivation
