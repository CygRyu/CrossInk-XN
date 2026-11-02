#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "GameValues.h"
#include "Simulation.h"

namespace cultivation {
namespace {

static const char* const kRealmProse[kMortalRealmCount] = {
    "Body Tempering", "Qi Condensation",          "Foundation Establishment", "Core Formation",
    "Nascent Soul",   "Soul Transformation",      "Void Refinement",          "Body Integration",
    "Mahayana",       "Tribulation Transcendence"};
static const char* const kStageProse[4] = {"Early", "Middle", "Late", "Peak"};
static const char* const kElementProse[static_cast<int>(Element::Count)] = {"Wood", "Fire", "Earth", "Metal", "Water"};
static const char* const kElementColorProse[static_cast<int>(Element::Count)] = {"green", "red", "ochre", "silver",
                                                                                 "blue"};
static const char* const kSeasonProse[4] = {"spring", "summer", "autumn", "winter"};

// Rows: Male, Female, Non-binary, Prefer not to say (DOC table).
struct GenderWords {
  const char* they;
  const char* them;
  const char* their;
  const char* theirs;
  const char* themself;
  const char* is;
  const char* was;
  const char* has;
  const char* senior;
  const char* sect;
  const char* god;
};
static const GenderWords kGenderWords[static_cast<int>(Gender::Count)] = {
    {"he", "him", "his", "his", "himself", "is", "was", "has", "Senior Brother", "Sect Master", "Godking"},
    {"she", "her", "her", "hers", "herself", "is", "was", "has", "Senior Sister", "Sect Mistress", "Godqueen"},
    {"they", "them", "their", "theirs", "themself", "are", "were", "have", "Senior Sibling", "Sect Head",
     "Godsovereign"},
    {"they", "them", "their", "theirs", "themself", "are", "were", "have", "Senior", "Sect Head", "Godsovereign"},
};

bool is(const char* name, const size_t len, const char* want) {
  return strlen(want) == len && memcmp(name, want, len) == 0;
}

bool put(char* out, const size_t outLen, const char* value) {
  snprintf(out, outLen, "%s", value);
  return true;
}

static const char* const kValueNames[] = {
    "player.name", "player.realm", "realm",        "player.stage", "player.age",   "player.element",
    "pro.they",    "pro.them",     "pro.their",    "pro.theirs",   "pro.themself", "pro.is",
    "pro.was",     "pro.has",      "title.senior", "title.sect",   "title.god",    "season",
    "root.glow",   "root.color",   "past.pages",   "past.books",   "past.hours",   "scroll.name",
    "region.name", "beast.name",   "pill.name",    "meet.ago",     "point.name",   "point.place"};

// "48,210 pages", "1 page", "no pages": the Past Life Memories numbers.
bool putCount(char* out, const size_t outLen, const uint32_t n, const char* one, const char* many) {
  if (n == 0) {
    snprintf(out, outLen, "no %s", many);
    return true;
  }
  if (n == 1) {
    snprintf(out, outLen, "1 %s", one);
    return true;
  }
  char digits[12];
  const int len = snprintf(digits, sizeof(digits), "%lu", static_cast<unsigned long>(n));
  char grouped[16];
  size_t o = 0;
  for (int i = 0; i < len; ++i) {
    if (i > 0 && (len - i) % 3 == 0) grouped[o++] = ',';
    grouped[o++] = digits[i];
  }
  grouped[o] = '\0';
  snprintf(out, outLen, "%s %s", grouped, many);
  return true;
}

}  // namespace

bool isGameValueName(const char* name, const size_t len) {
  for (const char* v : kValueNames) {
    if (is(name, len, v)) return true;
  }
  return false;
}

bool lookupGameValue(const GameState& state, const char* name, const size_t len, char* out, const size_t outLen) {
  if (out == nullptr || outLen == 0) return false;
  const ProgressBlock& p = state.progress;
  const int g = static_cast<int>(state.character.gender) < static_cast<int>(Gender::Count)
                    ? static_cast<int>(state.character.gender)
                    : static_cast<int>(Gender::Unspecified);
  const GenderWords& w = kGenderWords[g];

  if (is(name, len, "player.name")) {
    const size_t n = strnlen(state.character.name.data(), state.character.name.size());
    const size_t take = n < outLen - 1 ? n : outLen - 1;
    memcpy(out, state.character.name.data(), take);
    out[take] = '\0';
    return true;
  }
  if (is(name, len, "player.realm") || is(name, len, "realm")) {
    return put(out, outLen, kRealmProse[p.realm < kMortalRealmCount ? p.realm : kMortalRealmCount - 1]);
  }
  if (is(name, len, "player.stage")) {
    if (realmInfo(p.realm).layered) {
      snprintf(out, outLen, "Layer %u", static_cast<unsigned>(p.stage + 1));
      return true;
    }
    return put(out, outLen, kStageProse[p.stage < 4 ? p.stage : 3]);
  }
  if (is(name, len, "player.age")) {
    snprintf(out, outLen, "%u", static_cast<unsigned>(ageYears(state)));
    return true;
  }
  if (is(name, len, "player.element")) {
    const int e = static_cast<int>(state.character.rootElement);
    return put(out, outLen, kElementProse[e < static_cast<int>(Element::Count) ? e : 0]);
  }
  if (is(name, len, "season")) return put(out, outLen, kSeasonProse[(p.ageMonths / 3) % 4]);
  if (is(name, len, "root.glow")) {
    const uint8_t purity = state.character.rootPurity;  // same bands as the Awakening Test screen
    return put(out, outLen, purity < 34 ? "murky" : purity < 67 ? "steady" : "brilliant");
  }
  if (is(name, len, "root.color")) {
    const int e = static_cast<int>(state.character.rootElement);
    return put(out, outLen, kElementColorProse[e < static_cast<int>(Element::Count) ? e : 0]);
  }
  if (is(name, len, "past.pages")) return putCount(out, outLen, state.baseline.pages, "page", "pages");
  if (is(name, len, "past.books")) return putCount(out, outLen, state.baseline.completedBooks, "book", "books");
  if (is(name, len, "past.hours")) return putCount(out, outLen, state.baseline.seconds / 3600u, "hour", "hours");
  if (is(name, len, "pro.they")) return put(out, outLen, w.they);
  if (is(name, len, "pro.them")) return put(out, outLen, w.them);
  if (is(name, len, "pro.their")) return put(out, outLen, w.their);
  if (is(name, len, "pro.theirs")) return put(out, outLen, w.theirs);
  if (is(name, len, "pro.themself")) return put(out, outLen, w.themself);
  if (is(name, len, "pro.is")) return put(out, outLen, w.is);
  if (is(name, len, "pro.was")) return put(out, outLen, w.was);
  if (is(name, len, "pro.has")) return put(out, outLen, w.has);
  if (is(name, len, "title.senior")) return put(out, outLen, w.senior);
  if (is(name, len, "title.sect")) return put(out, outLen, w.sect);
  if (is(name, len, "title.god")) return put(out, outLen, w.god);
  return false;
}

namespace {
bool lookupAdapter(const void* user, const char* name, const size_t len, char* out, const size_t outLen) {
  return lookupGameValue(*static_cast<const GameState*>(user), name, len, out, outLen);
}
}  // namespace

GrammarValues gameValues(const GameState& state) {
  GrammarValues v;
  v.lookup = lookupAdapter;
  v.user = &state;
  return v;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
