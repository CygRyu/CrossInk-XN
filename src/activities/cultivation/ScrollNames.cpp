#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Rng.h"
#include "ScrollNames.h"
#include "Simulation.h"

namespace cultivation {
namespace scrollnames {
namespace {

constexpr uint32_t kKindName = 61;  // contentSeed kinds: 60 is the scroll's kind roll
constexpr uint32_t kKindLine = 62;

static const char* const kKindKeys[static_cast<int>(scrolls::Kind::Count)] = {
    "scroll.kind.sword",    "scroll.kind.palm", "scroll.kind.body",
    "scroll.kind.movement", "scroll.kind.mind", "scroll.kind.manual"};
static const char* const kTierKeys[static_cast<int>(scrolls::Tier::Count)] = {
    "scroll.tier.mortal", "scroll.tier.yellow", "scroll.tier.earth", "scroll.tier.heaven", "scroll.tier.saint"};
static const char* const kThemes[static_cast<int>(Element::Count)] = {"wood", "fire", "earth", "metal", "water"};

struct NameValue {
  const char* name;
};

bool lookupScrollName(const void* user, const char* key, const size_t len, char* out, const size_t outLen) {
  if (len != 11 || memcmp(key, "scroll.name", 11) != 0) return false;
  snprintf(out, outLen, "%s", static_cast<const NameValue*>(user)->name);
  return true;
}

}  // namespace

bool name(const scrolls::Scroll& s, const uint32_t worldSeed, Grammar& grammar, char* out, const size_t outLen) {
  if (out == nullptr || outLen == 0) return false;
  out[0] = '\0';
  const int kind = static_cast<int>(s.kind) < static_cast<int>(scrolls::Kind::Count) ? static_cast<int>(s.kind) : 0;
  const int tier = static_cast<int>(scrolls::tierOf(s));
  const int element = static_cast<int>(s.element) < static_cast<int>(Element::Count) ? static_cast<int>(s.element) : 2;
  char tmpl[96];
  snprintf(tmpl, sizeof(tmpl), "{scroll.name} {%s} {%s}", kKindKeys[kind], kTierKeys[tier]);
  GrammarContext ctx;
  ctx.theme = kThemes[element];
  ctx.stable = true;
  grammar.beginScene();
  const char* text = grammar.expand(tmpl, contentSeed(worldSeed, kKindName, s.titleHash), ctx);
  if (grammar.missing() > 0 || text == nullptr || text[0] == '\0') return false;
  snprintf(out, outLen, "%s", text);
  return true;
}

bool line(lifelog::Line& out, const char* key, const scrolls::Scroll& s, const GameState& state, Grammar& grammar) {
  char scrollName[kNameSize];
  out.text[0] = '\0';
  if (!name(s, worldSeedOf(state), grammar, scrollName, sizeof(scrollName))) return false;
  NameValue value{scrollName};
  GrammarValues extra;
  extra.lookup = lookupScrollName;
  extra.user = &value;
  // Seeded by the list, the scroll and the age, so a retried visit writes the same line.
  const uint32_t seed =
      contentSeed(worldSeedOf(state), kKindLine,
                  mix32(s.titleHash, static_cast<uint32_t>(strlen(key)) * 131u + state.progress.ageMonths));
  return lifelog::eventLine(out, lifelog::LineKind::Event, key, seed, state, grammar, &extra);
}

}  // namespace scrollnames
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
