#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Breakthrough.h"
#include "GameValues.h"
#include "GrammarPack.h"
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
  const GrammarValues* extra = nullptr;
};

bool lookupLineValue(const void* user, const char* name, const size_t len, char* out, const size_t outLen) {
  const auto* v = static_cast<const LineValues*>(user);
  if (v->extra != nullptr && v->extra->lookup != nullptr && v->extra->lookup(v->extra->user, name, len, out, outLen)) {
    return true;
  }
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
bool fill(Line& line, const char* key, const uint32_t seed, Grammar& grammar, const GrammarContext& ctx,
          const int32_t entry = -1) {
  line.text[0] = '\0';
  if (key == nullptr || !grammar.hasKey(key, ctx)) return false;
  grammar.beginScene();
  const char* body = nullptr;
  if (entry >= 0) {
    body = grammar.expandEntry(key, static_cast<uint32_t>(entry), seed, ctx);
  } else {
    char tmpl[64];
    snprintf(tmpl, sizeof(tmpl), "{%s}", key);
    body = grammar.expand(tmpl, seed, ctx);
  }
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

namespace {

// Which list a season draws from, from three independent rolls seeded by the
// season alone (never by the character's state):
//   any      30%: "log.any"
//   special  35% of the rest: Great Perfection or Half-step lines, when in that state
//   origin   3 in 7 of the rest, in Body Tempering: the origin's own list (else "log.body")
// Each list counts its uses by its own roll alone, so entering Great
// Perfection, failing a breakthrough or changing realm never shifts the order
// of a list (found in the simulator: replaying weighted picks did).
constexpr uint32_t kSpecialPermille = 350;
constexpr uint32_t kAnyPermille = 300;
constexpr uint32_t kOriginOf7 = 3;

struct SeasonRolls {
  bool special;
  bool any;
  bool origin;
};

SeasonRolls rollsFor(const uint32_t world, const uint32_t season) {
  Rng rng(mix32(contentSeed(world, kKindSeasonLine, season)));
  SeasonRolls r;
  r.special = rng.below(1000) < kSpecialPermille;
  r.any = rng.below(1000) < kAnyPermille;
  r.origin = rng.below(7) < kOriginOf7;
  return r;
}

enum class Slot : uint8_t { Special, Any, Realm, Origin, Body };

// Does season s count toward slot `slot`'s position? A superset of the
// seasons that really used it is fine: positions only have to grow by at
// least one between two real uses.
bool countsFor(const Slot slot, const SeasonRolls& r) {
  switch (slot) {
    case Slot::Special:
      return !r.any && r.special;
    case Slot::Any:
      return r.any;
    case Slot::Realm:
      return !r.any;
    case Slot::Origin:
      return !r.any && r.origin;
    case Slot::Body:
      return !r.any && !r.origin;
  }
  return false;
}

uint32_t gcd(uint32_t a, uint32_t b) {
  while (b != 0) {
    const uint32_t t = a % b;
    a = b;
    b = t;
  }
  return a;
}

}  // namespace

SeasonPick seasonPick(const GameState& state, const uint32_t season, const Grammar& grammar,
                      const GrammarContext& ctx) {
  SeasonPick pick;
  const uint32_t world = worldSeedOf(state);
  const SeasonRolls rolls = rollsFor(world, season);
  const uint8_t realm = state.progress.realm;
  auto has = [&](const char* key) { return key != nullptr && grammar.hasKey(key, ctx); };

  const char* key = nullptr;
  Slot slot = Slot::Any;
  bool everySeason = false;  // the list is the only one: its position is the season count
  const char* special = breakthrough::inGreatPerfection(state) ? "log.gp"
                        : breakthrough::isHalfStep(state)      ? "log.halfstep"
                                                               : nullptr;
  // Special lines only ever take a realm-list season, so log.any (shared by
  // every realm and state) never skips a position.
  if (!rolls.any && special != nullptr && rolls.special && has(special)) {
    key = special;
    slot = Slot::Special;
  } else if (!rolls.any) {
    if (realm == 0) {
      const int origin = static_cast<int>(state.character.origin);
      const char* own = origin < static_cast<int>(Origin::Count) ? kOriginLists[origin] : nullptr;
      if (rolls.origin && has(own)) {
        key = own;
        slot = Slot::Origin;
      } else if (has("log.body")) {
        key = "log.body";
        slot = Slot::Body;
      }
    } else if (realm == 1 && has("log.qi")) {
      key = "log.qi";
      slot = Slot::Realm;
    } else if (realm == 2 && has("log.foundation")) {
      key = "log.foundation";
      slot = Slot::Realm;
    } else if (realm == 3 && has("log.core")) {
      key = "log.core";
      slot = Slot::Realm;
    }
  }
  if (key == nullptr) {
    if (!has("log.any")) return pick;
    key = "log.any";
    slot = Slot::Any;
    // Realms without lists of their own use log.any every season.
    everySeason = realm > 3;
  }
  pick.key = key;

  // No early repeats: the list's position is how many seasons before this
  // one counted toward its slot (recomputed, never stored), and the list is
  // walked in one fixed shuffled order per world. An entry only comes back
  // after every other entry of its list has been used.
  uint32_t position = season;
  if (!everySeason) {
    position = 0;
    for (uint32_t s = 0; s < season; ++s) {
      if (countsFor(slot, rollsFor(world, s))) ++position;
    }
  }
  const uint32_t size = grammar.entryCount(key, ctx);
  if (size <= 1) return pick;
  // position -> (a * position + b) mod size, a coprime to size: one fixed order per list and world.
  Rng rng(contentSeed(world, packKeyHash(key), 0));
  uint32_t a = 1 + rng.below(size - 1);
  while (gcd(a, size) != 1) a = a % (size - 1) + 1;
  const uint32_t b = rng.below(size);
  pick.entry = static_cast<uint32_t>((static_cast<uint64_t>(a) * (position % size) + b) % size);
  return pick;
}

bool eventLine(Line& line, const LineKind kind, const char* key, const uint32_t seed, const GameState& state,
               Grammar& grammar, const GrammarValues* extra) {
  LineValues values{&state, seasonOf(state.progress.ageMonths), extra};
  GrammarContext ctx;
  ctx.values.lookup = lookupLineValue;
  ctx.values.user = &values;
  line.kind = kind;
  line.season = values.season;
  return fill(line, key, seed, grammar, ctx);
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
    const SeasonPick pick = seasonPick(state, line.season, *grammar, ctx);
    if (fill(line, pick.key, seed, *grammar, ctx, static_cast<int32_t>(pick.entry))) ++log.count;
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
