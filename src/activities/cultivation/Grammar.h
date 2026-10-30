#pragma once

#include <cstddef>
#include <cstdint>

#include "GrammarPack.h"
#include "Rng.h"

// The grammar engine (DOC: Grammar syntax). Expands templates like
//
//   While crossing {place.region>where}, you find {ruin.desc.a}.
//
// using lists from the content packs:
//
//   {key}        one entry of list `key`, itself expanded
//   [a|b|c]      one option inline; both bracket kinds nest
//   {key.mod}    modifiers after the key: cap, lower, title, a, the, s (chainable)
//   {key>var}    the pick is also stored as `var` for this scene
//   {$var}       a stored variable or a game value ({$player.name}, {$pro.they})
//   \x           the character x literally ("\{", "\[", "\|")
//
// Layers: on plane p, "key@p" is used instead of "key" when it exists; inside
// a themed event, "key~theme" is used first.
//
// Memory: the object holds every buffer it uses (output 1 KB, line arena 1 KB,
// anti-repeat memory ~1 KB, variables ~0.4 KB) and never allocates; the
// activity allocates it once in onEnter() and frees it in onExit().
// Recursion stops at depth 14.
namespace cultivation {

// Game values for {$name}: the callback writes the value of `name` into out
// (NUL-terminated) and returns true, or returns false for unknown names.
struct GrammarValues {
  bool (*lookup)(const void* user, const char* name, size_t nameLen, char* out, size_t outLen) = nullptr;
  const void* user = nullptr;
};

struct GrammarContext {
  uint8_t plane = 0;            // 0 = Mortal, 1 = Immortal, 2 = Divine ...
  const char* theme = nullptr;  // e.g. "demonic"; nullptr outside themed events
  GrammarValues values;
};

class Grammar {
 public:
  static constexpr size_t kOutputSize = 1024;
  static constexpr size_t kArenaSize = 1024;
  static constexpr uint8_t kMaxDepth = 14;
  static constexpr uint8_t kMaxVars = 8;
  static constexpr size_t kVarValueSize = 48;
  static constexpr uint8_t kRecentLists = 24;
  static constexpr uint8_t kRecentMax = 16;

  Grammar(const PackIndex& index, PackReader& reader) : index_(index), reader_(reader) {}

  // Forgets stored {key>var} variables. Call at the start of each event or
  // scene; variables otherwise carry from one expand() to the next, so an
  // event's choices can reuse what its text picked.
  void beginScene();

  // Expands `text` with randomness from `seed` (same seed, same packs and
  // same history -> same result). Returns the internal buffer, valid until
  // the next call. Output is cut at kOutputSize - 1 bytes.
  const char* expand(const char* text, uint32_t seed, const GrammarContext& ctx);

  // True if `key` has entries for this context (any layer).
  bool hasKey(const char* key, const GrammarContext& ctx) const;
  // Entries of `key` in this context (the layer that would be used), 0 if none.
  uint32_t entryCount(const char* key, const GrammarContext& ctx) const;

  // Like expand("{key}"), but the top-level pick is entry `entry` of the list
  // instead of a random one (the life log uses this to go through a list
  // without repeats). Lists inside that entry are still picked from `seed`.
  const char* expandEntry(const char* key, uint32_t entry, uint32_t seed, const GrammarContext& ctx);

  // Problems in the last expand(): keys or values that were not found.
  uint16_t missing() const { return missing_; }
  bool truncated() const { return truncated_; }

  // Modifier names allowed after a key ("{beast.a}"); used by the pack checker too.
  static bool isModifier(const char* name, size_t len);

 private:
  struct Var {
    uint32_t nameHash = 0;
    char value[kVarValueSize] = {};
  };
  struct Recent {
    uint32_t keyHash = 0;
    uint16_t lastUse = 0;
    uint8_t n = 0;
    uint16_t picks[kRecentMax] = {};
  };

  void expandRange(const char* begin, const char* end, uint8_t depth);
  void expandChoice(const char* begin, const char* end, uint8_t depth);
  void expandToken(const char* begin, const char* end, uint8_t depth);
  bool expandKey(const char* key, size_t len, uint8_t depth);
  bool emitValue(const char* name, size_t len);
  uint32_t resolveKey(const char* key, size_t len) const;  // hash of the layer to use, or 0
  uint32_t pick(uint32_t keyHash, uint32_t total);

  void emit(char c);
  void emit(const char* s, size_t len);
  bool insertAt(size_t pos, const char* s, size_t len);
  void applyModifier(const char* mod, size_t len, size_t start);
  void tidy();

  char* arenaAlloc(size_t len);
  void arenaFree(size_t mark) { arenaTop_ = mark; }

  const Var* findVar(const char* name, size_t len) const;
  void setVar(const char* name, size_t len, const char* value, size_t valueLen);

  const PackIndex& index_;
  PackReader& reader_;
  const GrammarContext* ctx_ = nullptr;
  Rng rng_{1};

  char out_[kOutputSize] = {};
  size_t outLen_ = 0;
  char arena_[kArenaSize] = {};
  size_t arenaTop_ = 0;
  Var vars_[kMaxVars];
  uint8_t nextVar_ = 0;
  Recent recent_[kRecentLists];
  uint16_t useClock_ = 0;
  uint32_t forcedHash_ = 0;  // expandEntry(): the first expansion of this key takes forcedEntry_
  uint32_t forcedEntry_ = 0;
  uint16_t missing_ = 0;
  bool truncated_ = false;
};

// Finds the bracket closing the one at `open` ('{' or '['), honouring nesting
// of both kinds and backslash escapes. nullptr if unbalanced.
const char* findClosingBracket(const char* open, const char* end);

}  // namespace cultivation
