#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "GameValues.h"
#include "Grammar.h"
#include "PackCheck.h"

namespace cultivation {
namespace {

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

bool allKeyChars(const char* b, const char* e) {
  if (b >= e) return false;
  for (const char* p = b; p < e; ++p) {
    if (!isValidKeyChar(*p)) return false;
  }
  return true;
}

bool isNumber(const char* b, const char* e) {
  if (b < e && (*b == '-' || *b == '+')) ++b;
  if (b >= e) return false;
  for (const char* p = b; p < e; ++p) {
    if (!isDigit(*p)) return false;
  }
  return true;
}

// "[20-60]" as an amount range.
bool isRange(const char* b, const char* e) {
  if (e - b < 5 || *b != '[' || e[-1] != ']') return false;
  const char* dash = nullptr;
  for (const char* p = b + 1; p < e - 1; ++p) {
    if (*p == '-') {
      dash = p;
      break;
    }
  }
  return dash != nullptr && isNumber(b + 1, dash) && isNumber(dash + 1, e - 1);
}

bool isWord(const char* b, const char* e, const char* w) {
  return static_cast<size_t>(e - b) == strlen(w) && memcmp(b, w, strlen(w)) == 0;
}

bool hasPrefix(const char* b, const char* e, const char* prefix) {
  const size_t n = strlen(prefix);
  return static_cast<size_t>(e - b) > n && memcmp(b, prefix, n) == 0;
}

// Conditions an event's @req may test (DOC: realm, element, karma, Dao
// levels, path, book tier, plane, flags; plus the obvious resources).
bool isConditionField(const char* b, const char* e) {
  static const char* const kFields[] = {"realm",   "stage",  "plane", "element", "karma",  "path",  "tier",
                                        "stamina", "stones", "hp",    "origin",  "gender", "night", "luck"};
  for (const char* f : kFields) {
    if (isWord(b, e, f)) return true;
  }
  return (hasPrefix(b, e, "flag.") || hasPrefix(b, e, "dao.")) && allKeyChars(b, e);
}

}  // namespace

void PackChecker::report(const uint8_t source, const char* what, const char* detail, const size_t detailLen,
                         const char* line) {
  ++problems_;
  if (sink_.problem == nullptr) return;
  char excerpt[48];
  const size_t lineLen = strlen(line);
  const size_t take = lineLen < sizeof(excerpt) - 4 ? lineLen : sizeof(excerpt) - 4;
  memcpy(excerpt, line, take);
  if (take < lineLen) {
    memcpy(excerpt + take, "...", 3);
    excerpt[take + 3] = '\0';
  } else {
    excerpt[take] = '\0';
  }
  const int shown = static_cast<int>(detailLen < 60 ? detailLen : 60);
  if (detail != nullptr && detailLen > 0) {
    snprintf(message_, sizeof(message_), "%s '%.*s' in: %s", what, shown, detail, excerpt);
  } else {
    snprintf(message_, sizeof(message_), "%s in: %s", what, excerpt);
  }
  sink_.problem(sink_.user, source, message_);
}

void PackChecker::collectVarNames(const char* line) {
  // Every "...>name}" stores a variable some later text may read.
  for (const char* p = line; *p != '\0'; ++p) {
    if (*p != '>' || p == line || p[-1] == ' ') continue;
    const char* nameBegin = p + 1;
    const char* q = nameBegin;
    while (*q != '\0' && *q != '}') ++q;
    if (*q != '}' || q == nameBegin || !allKeyChars(nameBegin, q)) continue;
    const uint32_t h = packKeyHash(nameBegin, static_cast<size_t>(q - nameBegin));
    bool known = false;
    for (uint8_t i = 0; i < varCount_; ++i) known = known || varNames_[i] == h;
    if (!known && varCount_ < kMaxVarNames) varNames_[varCount_++] = h;
  }
}

uint32_t PackChecker::run(const uint8_t firstSource) {
  problems_ = 0;
  varCount_ = 0;
  // Pass 1: variable names, from every pack (an SD pack may read a variable
  // that a built-in event stores).
  for (uint16_t i = 0; i < index_.count; ++i) {
    const PackSection& s = index_.sections[i];
    if ((s.flags & PackSection::kDisabled) != 0) continue;
    for (uint16_t e = 0; e < s.count; ++e) {
      if (reader_.readEntry(s, e, line_, sizeof(line_))) collectVarNames(line_);
    }
  }
  // Pass 2: every entry of the packs being checked.
  for (uint16_t i = 0; i < index_.count; ++i) {
    const PackSection& s = index_.sections[i];
    if (s.source < firstSource || (s.flags & PackSection::kDisabled) != 0) continue;
    for (uint16_t e = 0; e < s.count; ++e) {
      if (!reader_.readEntry(s, e, line_, sizeof(line_))) {
        report(s.source, "could not read an entry", nullptr, 0, "");
        continue;
      }
      const size_t len = strlen(line_);
      if (len > kPackLineMax) report(s.source, "line longer than 191 bytes (cut on the device)", nullptr, 0, line_);
      if ((s.flags & PackSection::kEvent) != 0) {
        checkEventLine(line_, s.source);
      } else {
        checkText(line_, line_ + len, s.source, line_);
      }
    }
  }
  return problems_;
}

void PackChecker::checkText(const char* begin, const char* end, const uint8_t source, const char* line) {
  for (const char* p = begin; p < end; ++p) {
    const char c = *p;
    if (c == '\\') {
      ++p;
      continue;
    }
    if (c == '{' || c == '[') {
      const char* close = findClosingBracket(p, end);
      if (close == nullptr) {
        report(source, "unbalanced bracket", p, 1, line);
        return;
      }
      if (c == '{') {
        checkToken(p + 1, close, source, line);
      } else {
        checkText(p + 1, close, source, line);
      }
      p = close;
      continue;
    }
    if (c == '}' || c == ']') report(source, "stray closing bracket", p, 1, line);
  }
}

void PackChecker::checkToken(const char* begin, const char* end, const uint8_t source, const char* line) {
  trimRange(begin, end);
  for (const char* p = begin; p < end; ++p) {
    if (*p == '{' || *p == '[') {  // built at run time: check its parts only
      checkText(begin, end, source, line);
      return;
    }
  }
  for (const char* p = begin; p < end; ++p) {
    if (*p == '>') {
      if (!allKeyChars(p + 1, end)) report(source, "bad variable name", p + 1, static_cast<size_t>(end - p - 1), line);
      end = p;
      break;
    }
  }
  while (true) {  // trailing modifiers
    const char* dot = nullptr;
    for (const char* p = end; p > begin; --p) {
      if (p[-1] == '.') {
        dot = p - 1;
        break;
      }
    }
    if (dot == nullptr || !Grammar::isModifier(dot + 1, static_cast<size_t>(end - dot - 1))) break;
    end = dot;
  }
  if (begin >= end) {
    report(source, "empty {}", nullptr, 0, line);
    return;
  }
  const size_t len = static_cast<size_t>(end - begin);
  if (*begin == '$') {
    const char* name = begin + 1;
    const size_t nameLen = len - 1;
    if (isGameValueName(name, nameLen)) return;
    const uint32_t h = packKeyHash(name, nameLen);
    for (uint8_t i = 0; i < varCount_; ++i) {
      if (varNames_[i] == h) return;
    }
    report(source, "unknown value or variable", begin, len, line);
    return;
  }
  if (!allKeyChars(begin, end)) {
    report(source, "bad list key (use a-z 0-9 . _ - @ ~)", begin, len, line);
    return;
  }
  const uint32_t h = packKeyHash(begin, len);
  if (index_.entryCount(h) == 0) {
    report(source, "unknown list (or unknown modifier)", begin, len, line);
    return;
  }
  uint16_t unused = 0;
  const PackSection* s = index_.locate(h, 0, unused);
  if (s != nullptr && (s->flags & PackSection::kEvent) != 0)
    report(source, "an event used as a list", begin, len, line);
}

void PackChecker::checkEventLine(const char* line, const uint8_t source) {
  const char* b = line;
  const char* e = line + strlen(line);
  trimRange(b, e);
  if (b == e) return;
  if (*b == '>') {
    checkText(b + 1, e, source, line);
    return;
  }
  if (*b != '@') {
    checkText(b, e, source, line);
    return;
  }
  const char* word = b + 1;
  const char* wordEnd = word;
  while (wordEnd < e && *wordEnd != ' ') ++wordEnd;
  const char* rest = wordEnd;
  trimRange(rest, e);

  if (isWord(word, wordEnd, "weight")) {
    if (!isNumber(rest, e)) report(source, "@weight needs a number", rest, static_cast<size_t>(e - rest), line);
  } else if (isWord(word, wordEnd, "req")) {
    const char* p = rest;
    while (p < e) {
      const char* q = p;
      while (q < e && *q != ' ') ++q;
      if (q > p) checkRequirement(p, q, source, line);
      p = q + 1;
    }
  } else if (isWord(word, wordEnd, "check")) {
    const char* kind = rest;
    const char* kindEnd = kind;
    while (kindEnd < e && *kindEnd != ' ') ++kindEnd;
    const char* number = kindEnd;
    trimRange(number, e);
    const bool kindOk = isWord(kind, kindEnd, "luck") || isWord(kind, kindEnd, "heart") ||
                        isWord(kind, kindEnd, "karma") || isWord(kind, kindEnd, "power");
    if (!kindOk) report(source, "@check kind must be luck, heart, karma or power", kind, kindEnd - kind, line);
    if (!isNumber(number, e)) report(source, "@check needs a target number", number, e - number, line);
  } else if (isWord(word, wordEnd, "win") || isWord(word, wordEnd, "lose") || isWord(word, wordEnd, "effect")) {
    checkEffects(rest, e, source, line);
  } else {
    report(source, "unknown directive", word - 1, static_cast<size_t>(wordEnd - word + 1), line);
  }
}

void PackChecker::checkRequirement(const char* begin, const char* end, const uint8_t source, const char* line) {
  static const char* const kOps[] = {">=", "<=", "!=", ">", "<", "="};
  for (const char* p = begin; p < end; ++p) {
    for (const char* op : kOps) {
      const size_t n = strlen(op);
      if (static_cast<size_t>(end - p) < n || memcmp(p, op, n) != 0) continue;
      if (!isConditionField(begin, p)) {
        report(source, "unknown condition", begin, static_cast<size_t>(end - begin), line);
      } else if (p + n >= end) {
        report(source, "condition without a value", begin, static_cast<size_t>(end - begin), line);
      }
      return;
    }
  }
  report(source, "condition without an operator", begin, static_cast<size_t>(end - begin), line);
}

void PackChecker::checkEffects(const char* begin, const char* end, const uint8_t source, const char* line) {
  const char* p = begin;
  while (p < end) {
    while (p < end && *p == ' ') ++p;
    if (p >= end) break;
    if (hasPrefix(p, end, "text=")) {  // the rest of the line is the Chronicle text
      checkText(p + 5, end, source, line);
      return;
    }
    const char* q = p;
    while (q < end && *q != ' ') ++q;
    // name+N, name-N, name+[a-b], flag=name, codex=name
    const char* sign = p;
    while (sign < q && *sign != '+' && *sign != '-' && *sign != '=') ++sign;
    bool ok = sign < q && sign > p && allKeyChars(p, sign);
    if (ok && *sign == '=') {
      ok = (isWord(p, sign, "flag") || isWord(p, sign, "unflag") || isWord(p, sign, "codex")) &&
           allKeyChars(sign + 1, q);
    } else if (ok) {
      ok = isNumber(sign + 1, q) || isRange(sign + 1, q);
    }
    if (!ok) report(source, "bad effect (name+N, name-N, name+[a-b], flag=x, codex=x)", p, q - p, line);
    p = q;
  }
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
