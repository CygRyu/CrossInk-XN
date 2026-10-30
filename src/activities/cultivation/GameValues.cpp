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

static const char* const kValueNames[] = {"player.name",    "player.realm", "realm",    "player.stage", "player.age",
                                          "player.element", "pro.they",     "pro.them", "pro.their",    "pro.theirs",
                                          "pro.themself",   "pro.is",       "pro.was",  "pro.has",      "title.senior",
                                          "title.sect",     "title.god"};

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
