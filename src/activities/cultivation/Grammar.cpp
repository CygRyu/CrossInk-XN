#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Grammar.h"

namespace cultivation {
namespace {

constexpr size_t kTokenMax = 64;      // "{...}" contents after expansion
constexpr size_t kValueMax = 64;      // one game value
constexpr size_t kLayerKeyMax = 96;   // key + "~theme" + "@p"
constexpr uint8_t kMaxModifiers = 4;  // per token

bool isAlpha(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char toUpper(const char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
char toLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool isVowel(const char c) {
  const char l = toLower(c);
  return l == 'a' || l == 'e' || l == 'i' || l == 'o' || l == 'u';
}

// Case-insensitive: does [s, s+n) start with the lowercase word `w`?
bool startsWithWord(const char* s, const size_t n, const char* w) {
  size_t i = 0;
  for (; w[i] != '\0'; ++i) {
    if (i >= n || toLower(s[i]) != w[i]) return false;
  }
  return true;
}

bool equalsWord(const char* s, const size_t n, const char* w) { return strlen(w) == n && startsWithWord(s, n, w); }

bool isArticle(const char* s, const size_t n) {
  return equalsWord(s, n, "a") || equalsWord(s, n, "an") || equalsWord(s, n, "the");
}

// "a" or "an" for a word: the vowel rule plus the usual exceptions.
bool needsAn(const char* s, const size_t n) {
  static const char* const kAnWords[] = {"hour", "honest", "honor", "honour", "heir"};
  static const char* const kAWords[] = {"uni", "use", "usu", "one", "once", "eu", "ewe"};
  for (const char* w : kAnWords) {
    if (startsWithWord(s, n, w)) return true;
  }
  for (const char* w : kAWords) {
    if (startsWithWord(s, n, w)) return false;
  }
  return n > 0 && isVowel(s[0]);
}

}  // namespace

const char* findClosingBracket(const char* open, const char* end) {
  char stack[32];
  int top = 0;
  for (const char* p = open; p < end; ++p) {
    const char c = *p;
    if (c == '\\') {
      ++p;
      continue;
    }
    if (c == '{' || c == '[') {
      if (top >= static_cast<int>(sizeof(stack))) return nullptr;
      stack[top++] = c == '{' ? '}' : ']';
    } else if (c == '}' || c == ']') {
      if (top == 0 || stack[top - 1] != c) return nullptr;
      if (--top == 0) return p;
    }
  }
  return nullptr;
}

bool Grammar::isModifier(const char* name, const size_t len) {
  static const char* const kModifiers[] = {"cap", "lower", "title", "a", "the", "s"};
  for (const char* m : kModifiers) {
    if (strlen(m) == len && memcmp(m, name, len) == 0) return true;
  }
  return false;
}

void Grammar::beginScene() {
  for (Var& v : vars_) v = Var{};
  nextVar_ = 0;
}

const char* Grammar::expand(const char* text, const uint32_t seed, const GrammarContext& ctx) {
  ctx_ = &ctx;
  rng_ = Rng(mix32(seed, 0x4752414Du));  // mixed, so neighbouring seeds (day 1, day 2) differ fully
  outLen_ = 0;
  arenaTop_ = 0;
  missing_ = 0;
  truncated_ = false;
  if (text != nullptr) expandRange(text, text + strlen(text), 0);
  out_[outLen_] = '\0';
  tidy();
  ctx_ = nullptr;
  return out_;
}

uint32_t Grammar::entryCount(const char* key, const GrammarContext& ctx) const {
  const GrammarContext* saved = ctx_;
  const_cast<Grammar*>(this)->ctx_ = &ctx;
  const uint32_t h = resolveKey(key, strlen(key));
  const_cast<Grammar*>(this)->ctx_ = saved;
  return h != 0 ? index_.entryCount(h) : 0;
}

const char* Grammar::expandEntry(const char* key, const uint32_t entry, const uint32_t seed,
                                 const GrammarContext& ctx) {
  char tmpl[kPackKeyMax + 3];  // "{" key "}" NUL
  const int n = snprintf(tmpl, sizeof(tmpl), "{%s}", key);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(tmpl)) {
    out_[0] = '\0';
    missing_ = 1;
    return out_;
  }
  ctx_ = &ctx;
  forcedHash_ = resolveKey(key, strlen(key));
  ctx_ = nullptr;
  forcedEntry_ = entry;
  const char* result = expand(tmpl, seed, ctx);
  forcedHash_ = 0;
  return result;
}

bool Grammar::hasKey(const char* key, const GrammarContext& ctx) const {
  const GrammarContext* saved = ctx_;
  const_cast<Grammar*>(this)->ctx_ = &ctx;
  const bool found = resolveKey(key, strlen(key)) != 0;
  const_cast<Grammar*>(this)->ctx_ = saved;
  return found;
}

// --- Output -------------------------------------------------------------------

void Grammar::emit(const char c) {
  if (outLen_ + 1 < kOutputSize) {
    out_[outLen_++] = c;
  } else {
    truncated_ = true;
  }
}

void Grammar::emit(const char* s, const size_t len) {
  for (size_t i = 0; i < len; ++i) emit(s[i]);
}

bool Grammar::insertAt(const size_t pos, const char* s, const size_t len) {
  if (outLen_ + len + 1 > kOutputSize) {
    truncated_ = true;
    return false;
  }
  memmove(out_ + pos + len, out_ + pos, outLen_ - pos);
  memcpy(out_ + pos, s, len);
  outLen_ += len;
  return true;
}

char* Grammar::arenaAlloc(const size_t len) {
  if (arenaTop_ + len > kArenaSize) return nullptr;
  char* p = arena_ + arenaTop_;
  arenaTop_ += len;
  return p;
}

// --- Expansion ------------------------------------------------------------------

void Grammar::expandRange(const char* begin, const char* end, const uint8_t depth) {
  const char* p = begin;
  while (p < end) {
    const char c = *p;
    if (c == '\\' && p + 1 < end) {
      emit(p[1]);
      p += 2;
      continue;
    }
    if (c == '{' || c == '[') {
      const char* close = findClosingBracket(p, end);
      if (close == nullptr) {  // unbalanced: show the text rather than lose it
        emit(c);
        ++p;
        continue;
      }
      if (depth < kMaxDepth) {
        if (c == '{') {
          expandToken(p + 1, close, depth);
        } else {
          expandChoice(p + 1, close, depth);
        }
      }
      p = close + 1;
      continue;
    }
    emit(c);
    ++p;
  }
}

void Grammar::expandChoice(const char* begin, const char* end, const uint8_t depth) {
  // Count the top-level options, then expand the one picked.
  uint32_t options = 1;
  int nest = 0;
  for (const char* p = begin; p < end; ++p) {
    if (*p == '\\') {
      ++p;
    } else if (*p == '{' || *p == '[') {
      ++nest;
    } else if (*p == '}' || *p == ']') {
      --nest;
    } else if (*p == '|' && nest == 0) {
      ++options;
    }
  }
  uint32_t target = rng_.below(options);
  const char* optionBegin = begin;
  nest = 0;
  for (const char* p = begin; p <= end; ++p) {
    const bool atEnd = p == end;
    if (!atEnd && *p == '\\') {
      ++p;
      continue;
    }
    if (!atEnd && (*p == '{' || *p == '[')) {
      ++nest;
    } else if (!atEnd && (*p == '}' || *p == ']')) {
      --nest;
    } else if (atEnd || (*p == '|' && nest == 0)) {
      if (target == 0) {
        expandRange(optionBegin, p, static_cast<uint8_t>(depth + 1));
        return;
      }
      --target;
      optionBegin = p + 1;
    }
  }
}

void Grammar::expandToken(const char* begin, const char* end, const uint8_t depth) {
  const size_t mark = arenaTop_;
  char* token = arenaAlloc(kTokenMax);
  if (token == nullptr) {
    truncated_ = true;
    return;
  }

  // A token may itself contain brackets ("{name.[male|female]}"): expand it
  // into the output, copy it out, and roll the output back.
  size_t tokenLen = 0;
  bool dynamic = false;
  for (const char* p = begin; p < end; ++p) dynamic = dynamic || *p == '{' || *p == '[';
  if (dynamic) {
    const size_t outMark = outLen_;
    expandRange(begin, end, static_cast<uint8_t>(depth + 1));
    tokenLen = outLen_ - outMark;
    if (tokenLen > kTokenMax - 1) tokenLen = kTokenMax - 1;
    memcpy(token, out_ + outMark, tokenLen);
    outLen_ = outMark;
  } else {
    tokenLen = static_cast<size_t>(end - begin);
    if (tokenLen > kTokenMax - 1) tokenLen = kTokenMax - 1;
    memcpy(token, begin, tokenLen);
  }
  token[tokenLen] = '\0';
  const char* tb = token;
  const char* te = token + tokenLen;
  trimRange(tb, te);

  // "{key>var}": store the result as var.
  const char* storeName = nullptr;
  size_t storeLen = 0;
  for (const char* p = tb; p < te; ++p) {
    if (*p == '>') {
      storeName = p + 1;
      storeLen = static_cast<size_t>(te - storeName);
      te = p;
      break;
    }
  }

  // Trailing modifiers: "{beast.a.cap}" -> key "beast", modifiers a, cap.
  const char* mods[kMaxModifiers];
  size_t modLens[kMaxModifiers];
  uint8_t modCount = 0;
  while (modCount < kMaxModifiers) {
    const char* dot = nullptr;
    for (const char* p = te; p > tb; --p) {
      if (p[-1] == '.') {
        dot = p - 1;
        break;
      }
    }
    if (dot == nullptr || !isModifier(dot + 1, static_cast<size_t>(te - dot - 1))) break;
    mods[modCount] = dot + 1;
    modLens[modCount] = static_cast<size_t>(te - dot - 1);
    ++modCount;
    te = dot;
  }

  const size_t start = outLen_;
  bool found = false;
  if (tb < te && *tb == '$') {
    found = emitValue(tb + 1, static_cast<size_t>(te - tb - 1));
  } else if (tb < te) {
    found = expandKey(tb, static_cast<size_t>(te - tb), depth);
  }
  if (!found) ++missing_;

  // Modifiers were collected last-first; apply them in written order.
  for (int i = modCount - 1; i >= 0; --i) applyModifier(mods[i], modLens[i], start);
  if (storeName != nullptr && storeLen > 0) setVar(storeName, storeLen, out_ + start, outLen_ - start);
  arenaFree(mark);
}

uint32_t Grammar::resolveKey(const char* key, const size_t len) const {
  // Explicit layers ("log.stage@1") are used as written.
  for (size_t i = 0; i < len; ++i) {
    if (key[i] == '@' || key[i] == '~') {
      const uint32_t h = packKeyHash(key, len);
      return index_.entryCount(h) > 0 ? h : 0;
    }
  }
  char name[kLayerKeyMax];
  if (len >= kLayerKeyMax - 1) return 0;
  const char* theme = ctx_ != nullptr ? ctx_->theme : nullptr;
  const uint8_t plane = ctx_ != nullptr ? ctx_->plane : 0;
  const size_t themeLen = theme != nullptr ? strlen(theme) : 0;

  // Most specific first: key~theme@p, key~theme, key@p, key.
  for (int variant = 0; variant < 4; ++variant) {
    const bool withTheme = variant < 2;
    const bool withPlane = variant == 0 || variant == 2;
    if (withTheme && themeLen == 0) continue;
    if (withPlane && plane == 0) continue;
    size_t n = 0;
    memcpy(name, key, len);
    n = len;
    if (withTheme) {
      if (n + 1 + themeLen >= sizeof(name)) continue;
      name[n++] = '~';
      memcpy(name + n, theme, themeLen);
      n += themeLen;
    }
    if (withPlane) {
      if (n + 4 >= sizeof(name)) continue;
      name[n++] = '@';
      if (plane >= 100) name[n++] = static_cast<char>('0' + plane / 100);
      if (plane >= 10) name[n++] = static_cast<char>('0' + (plane / 10) % 10);
      name[n++] = static_cast<char>('0' + plane % 10);
    }
    const uint32_t h = packKeyHash(name, n);
    if (index_.entryCount(h) > 0) return h;
  }
  return 0;
}

bool Grammar::expandKey(const char* key, const size_t len, const uint8_t depth) {
  const uint32_t h = resolveKey(key, len);
  if (h == 0) return false;
  const uint32_t total = index_.entryCount(h);
  uint16_t indexInSection = 0;
  uint32_t n = 0;
  if (forcedHash_ != 0 && h == forcedHash_) {
    n = forcedEntry_ < total ? forcedEntry_ : 0;
    forcedHash_ = 0;  // only the top-level pick is forced
  } else {
    n = pick(h, total);
  }
  const PackSection* section = index_.locate(h, n, indexInSection);
  if (section == nullptr) return false;
  if ((section->flags & PackSection::kEvent) != 0) return false;  // events are not lists

  // Read the entry into the arena, expand it, then give the space back.
  const size_t mark = arenaTop_;
  const size_t room = kArenaSize - arenaTop_;
  const size_t want = kPackLineMax + 1 < room ? kPackLineMax + 1 : room;
  if (want < 2) {
    truncated_ = true;
    return false;
  }
  char* line = arenaAlloc(want);
  if (!reader_.readEntry(*section, indexInSection, line, want)) {
    arenaFree(mark);
    return false;
  }
  const size_t lineLen = strlen(line);
  arenaTop_ = mark + lineLen + 1;  // keep only what the line uses
  expandRange(line, line + lineLen, static_cast<uint8_t>(depth + 1));
  arenaFree(mark);
  return true;
}

bool Grammar::emitValue(const char* name, const size_t len) {
  if (const Var* v = findVar(name, len)) {
    emit(v->value, strlen(v->value));
    return true;
  }
  if (ctx_ == nullptr || ctx_->values.lookup == nullptr) return false;
  const size_t mark = arenaTop_;
  char* value = arenaAlloc(kValueMax);
  if (value == nullptr) {
    truncated_ = true;
    return false;
  }
  value[0] = '\0';
  const bool found = ctx_->values.lookup(ctx_->values.user, name, len, value, kValueMax);
  if (found) emit(value, strnlen(value, kValueMax));
  arenaFree(mark);
  return found;
}

// --- Anti-repetition --------------------------------------------------------------

uint32_t Grammar::pick(const uint32_t keyHash, const uint32_t total) {
  if (total <= 1) return 0;
  // DOC: remember up to 60% of the list's length, at most 16 picks.
  uint32_t limit = total * 60 / 100;
  if (limit > kRecentMax) limit = kRecentMax;

  Recent* slot = nullptr;
  for (Recent& r : recent_) {
    if (r.keyHash == keyHash) {
      slot = &r;
      break;
    }
  }
  if (slot == nullptr) {
    // A free slot, else the least recently used list.
    Recent* victim = &recent_[0];
    for (Recent& r : recent_) {
      if (r.keyHash == 0) {
        victim = &r;
        break;
      }
      if (r.lastUse < victim->lastUse) victim = &r;
    }
    slot = victim;
    *slot = Recent{};
    slot->keyHash = keyHash;
  }
  slot->lastUse = ++useClock_;
  if (useClock_ == UINT16_MAX) {  // keep the ordering, avoid wrap-around
    for (Recent& r : recent_) r.lastUse = static_cast<uint16_t>(r.lastUse / 2);
    useClock_ = static_cast<uint16_t>(useClock_ / 2);
  }

  // Forget picks a shrunken limit or a changed list no longer allows.
  while (slot->n > limit) {
    memmove(slot->picks, slot->picks + 1, (slot->n - 1) * sizeof(slot->picks[0]));
    --slot->n;
  }
  auto recent = [&](const uint32_t i) {
    for (uint8_t k = 0; k < slot->n; ++k) {
      if (slot->picks[k] == i) return true;
    }
    return false;
  };
  uint32_t fresh = total;
  for (uint8_t k = 0; k < slot->n; ++k) {
    if (slot->picks[k] < total) --fresh;
  }
  uint32_t k = rng_.below(fresh > 0 ? fresh : total);
  uint32_t chosen = 0;
  for (uint32_t i = 0; i < total; ++i) {
    if (fresh > 0 && recent(i)) continue;
    if (k == 0) {
      chosen = i;
      break;
    }
    --k;
  }

  if (limit > 0) {
    if (slot->n >= limit) {
      memmove(slot->picks, slot->picks + 1, (slot->n - 1) * sizeof(slot->picks[0]));
      --slot->n;
    }
    slot->picks[slot->n++] = static_cast<uint16_t>(chosen);
  }
  return chosen;
}

// --- Variables ----------------------------------------------------------------

const Grammar::Var* Grammar::findVar(const char* name, const size_t len) const {
  const uint32_t h = packKeyHash(name, len);
  for (const Var& v : vars_) {
    if (v.nameHash == h && v.nameHash != 0) return &v;
  }
  return nullptr;
}

void Grammar::setVar(const char* name, const size_t len, const char* value, size_t valueLen) {
  const uint32_t h = packKeyHash(name, len);
  Var* slot = nullptr;
  for (Var& v : vars_) {
    if (v.nameHash == h) {
      slot = &v;
      break;
    }
  }
  if (slot == nullptr) {
    slot = &vars_[nextVar_];
    nextVar_ = static_cast<uint8_t>((nextVar_ + 1) % kMaxVars);
  }
  if (valueLen > kVarValueSize - 1) valueLen = kVarValueSize - 1;
  slot->nameHash = h;
  memcpy(slot->value, value, valueLen);
  slot->value[valueLen] = '\0';
}

// --- Modifiers and tidying -----------------------------------------------------

void Grammar::applyModifier(const char* mod, const size_t len, const size_t start) {
  if (start >= outLen_) return;
  char* s = out_ + start;
  const size_t n = outLen_ - start;
  auto is = [&](const char* m) { return strlen(m) == len && memcmp(m, mod, len) == 0; };

  if (is("cap")) {
    for (size_t i = 0; i < n; ++i) {
      if (isAlpha(s[i])) {
        s[i] = toUpper(s[i]);
        break;
      }
    }
  } else if (is("lower")) {
    for (size_t i = 0; i < n; ++i) s[i] = toLower(s[i]);
  } else if (is("title")) {
    for (size_t i = 0; i < n; ++i) {
      if (isAlpha(s[i]) && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '-')) s[i] = toUpper(s[i]);
    }
  } else if (is("a")) {
    size_t i = 0;
    while (i < n && s[i] == ' ') ++i;
    if (needsAn(s + i, n - i)) {
      insertAt(start, "an ", 3);
    } else {
      insertAt(start, "a ", 2);
    }
  } else if (is("the")) {
    if (!startsWithWord(s, n, "the ")) insertAt(start, "the ", 4);
  } else if (is("s")) {
    size_t e = n;
    while (e > 0 && s[e - 1] == ' ') --e;
    if (e == 0) return;
    const char last = toLower(s[e - 1]);
    const char prev = e >= 2 ? toLower(s[e - 2]) : ' ';
    if (e != n) return;  // keep it simple: only pluralise when the text ends with the word
    if (last == 'y' && isAlpha(prev) && !isVowel(prev)) {
      outLen_ = start + e - 1;
      emit("ies", 3);
    } else if (last == 's' || last == 'x' || last == 'z' || (last == 'h' && (prev == 'c' || prev == 's'))) {
      emit("es", 2);
    } else {
      emit('s');
    }
  }
}

void Grammar::tidy() {
  // 1. Collapse whitespace, drop spaces before punctuation, trim.
  size_t w = 0;
  for (size_t r = 0; r < outLen_; ++r) {
    char c = out_[r];
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    if (c == ' ') {
      if (w > 0 && out_[w - 1] != ' ') out_[w++] = ' ';
      continue;
    }
    const bool closing = c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' || c == ')';
    if (closing && w > 0 && out_[w - 1] == ' ') --w;
    out_[w++] = c;
  }
  while (w > 0 && out_[w - 1] == ' ') --w;
  outLen_ = w;

  // 2. Doubled articles ("the the", "a the"): drop the first, keep its capital.
  w = 0;
  size_t r = 0;
  while (r < outLen_) {
    const size_t wordStart = r;
    while (r < outLen_ && out_[r] != ' ') ++r;
    const size_t wordLen = r - wordStart;
    bool drop = false;
    if (isArticle(out_ + wordStart, wordLen) && r < outLen_) {
      size_t next = r + 1;
      size_t nextEnd = next;
      while (nextEnd < outLen_ && out_[nextEnd] != ' ') ++nextEnd;
      if (isArticle(out_ + next, nextEnd - next)) {
        drop = true;
        if (out_[wordStart] >= 'A' && out_[wordStart] <= 'Z') out_[next] = toUpper(out_[next]);
      }
    }
    if (!drop) {
      memmove(out_ + w, out_ + wordStart, wordLen);
      w += wordLen;
      if (r < outLen_) out_[w++] = ' ';
    }
    if (r < outLen_) ++r;  // skip the single space
  }
  while (w > 0 && out_[w - 1] == ' ') --w;
  outLen_ = w;
  out_[outLen_] = '\0';
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
