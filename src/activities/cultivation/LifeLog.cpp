#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "GameValues.h"
#include "LifeLog.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace lifelog {
namespace {

constexpr uint32_t kKindSeasonLine = 51;  // contentSeed kinds; 50 is the rumor of the day
constexpr uint32_t kKindStageLine = 52;
constexpr uint32_t kKindBottleneckLine = 53;

static const char* const kSeasonLabels[4] = {"Spring", "Summer", "Autumn", "Winter"};
static const char* const kSeasonProse[4] = {"spring", "summer", "autumn", "winter"};

// Origin lists for Body Tempering, in Origin order.
static const char* const kOriginLists[static_cast<int>(Origin::Count)] = {
    "log.body.farmer", "log.body.clan", "log.body.orphan", "log.body.servant", "log.body.herbalist"};

// A line describes its own season: {$season} and {$player.age} are the
// season's, not today's. Everything else comes from the character.
struct LineValues {
  const GameState* state;
  uint32_t season;
};

bool lookupLineValue(const void* user, const char* name, const size_t len, char* out, const size_t outLen) {
  const auto* v = static_cast<const LineValues*>(user);
  if (len == 6 && memcmp(name, "season", 6) == 0) {
    snprintf(out, outLen, "%s", kSeasonProse[seasonInYear(v->season)]);
    return true;
  }
  if (len == 10 && memcmp(name, "player.age", 10) == 0) {
    snprintf(out, outLen, "%lu", static_cast<unsigned long>(ageOfSeason(v->season)));
    return true;
  }
  return lookupGameValue(*v->state, name, len, out, outLen);
}

// Expands "{key}" into line.text after its label. Returns false (and leaves
// the line empty) if the engine is missing, the key has no entries, or the
// expansion referred to something that does not exist.
bool fill(Line& line, const char* key, const uint32_t seed, Grammar& grammar, const GrammarContext& ctx) {
  line.text[0] = '\0';
  if (key == nullptr || !grammar.hasKey(key, ctx)) return false;
  char tmpl[64];
  snprintf(tmpl, sizeof(tmpl), "{%s}", key);
  grammar.beginScene();
  const char* body = grammar.expand(tmpl, seed, ctx);
  if (grammar.missing() > 0 || body == nullptr || body[0] == '\0') return false;
  formatLabel(line.season, line.text, sizeof(line.text));
  const size_t used = strlen(line.text);
  snprintf(line.text + used, sizeof(line.text) - used, "%s", body);
  return true;
}

}  // namespace

uint32_t shownSeason(const uint32_t firstSeason, const uint32_t count, const uint8_t i, const uint8_t n) {
  if (count == 0 || n == 0) return firstSeason;
  if (count <= n) return firstSeason + i;
  if (n == 1) return firstSeason + count - 1;
  // Evenly spaced, rounding down: i = 0 gives the first, i = n - 1 the last.
  return firstSeason + static_cast<uint32_t>(static_cast<uint64_t>(count - 1) * i / (n - 1));
}

void formatLabel(const uint32_t season, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s, age %lu. ", kSeasonLabels[seasonInYear(season)],
           static_cast<unsigned long>(ageOfSeason(season)));
}

const char* seasonListFor(const GameState& state, const uint32_t seed, const Grammar& grammar,
                          const GrammarContext& ctx) {
  struct Candidate {
    const char* key;
    uint8_t weight;
  };
  Candidate candidates[3] = {};
  uint8_t n = 0;
  const uint8_t realm = state.progress.realm;
  if (realm == 0) {
    const int origin = static_cast<int>(state.character.origin);
    if (origin < static_cast<int>(Origin::Count)) candidates[n++] = {kOriginLists[origin], 3};
    candidates[n++] = {"log.body", 4};
  } else if (realm == 1) {
    candidates[n++] = {"log.qi", 6};
  }
  candidates[n++] = {"log.any", 3};

  uint32_t total = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (!grammar.hasKey(candidates[i].key, ctx)) candidates[i].weight = 0;
    total += candidates[i].weight;
  }
  if (total == 0) return nullptr;
  Rng rng(mix32(seed));
  uint32_t roll = rng.below(total);
  for (uint8_t i = 0; i < n; ++i) {
    if (roll < candidates[i].weight) return candidates[i].key;
    roll -= candidates[i].weight;
  }
  return nullptr;
}

void build(LifeLog& log, const VisitChange& change, const uint32_t monthsPassed, const GameState& state,
           Grammar* grammar) {
  log.monthsPassed = monthsPassed;
  log.seasonsPassed = seasonsPassed(change);
  log.count = 0;
  if (grammar == nullptr) return;

  const uint32_t world = worldSeedOf(state);
  LineValues values{&state, 0};
  GrammarContext ctx;
  ctx.plane = 0;  // the Mortal Plane until ascension exists
  ctx.values.lookup = lookupLineValue;
  ctx.values.user = &values;

  const uint32_t first = seasonOf(change.ageMonthsBefore);
  const uint8_t shown = static_cast<uint8_t>(log.seasonsPassed < kMaxSeasonLines ? log.seasonsPassed : kMaxSeasonLines);
  for (uint8_t i = 0; i < shown; ++i) {
    Line& line = log.lines[log.count];
    line.kind = LineKind::Season;
    line.season = shownSeason(first, log.seasonsPassed, i, shown);
    values.season = line.season;
    const uint32_t seed = contentSeed(world, kKindSeasonLine, line.season);
    if (fill(line, seasonListFor(state, seed, *grammar, ctx), seed, *grammar, ctx)) ++log.count;
  }

  // Stage and bottleneck lines belong to the season the character is in now.
  const uint32_t now = seasonOf(change.ageMonthsAfter);
  values.season = now;
  if (change.stagesGained > 0) {
    Line& line = log.lines[log.count];
    line.kind = LineKind::Stage;
    line.season = now;
    const uint32_t seed = contentSeed(world, kKindStageLine,
                                      now ^ (static_cast<uint32_t>(state.progress.realm) << 24) ^
                                          (static_cast<uint32_t>(state.progress.stage) << 16));
    if (fill(line, "log.stage", seed, *grammar, ctx)) ++log.count;
  }
  if (change.reachedBottleneck) {
    Line& line = log.lines[log.count];
    line.kind = LineKind::Bottleneck;
    line.season = now;
    const uint32_t seed = contentSeed(world, kKindBottleneckLine, state.progress.realm);
    if (fill(line, "log.bottleneck", seed, *grammar, ctx)) ++log.count;
  }
}

}  // namespace lifelog
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
