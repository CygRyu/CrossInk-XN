#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Exploration.h"
#include "ExploreText.h"
#include "GameValues.h"
#include "Rng.h"
#include "Simulation.h"

namespace cultivation {
namespace exploretext {
namespace {

constexpr uint32_t kKindRegionName = 81;
constexpr uint32_t kKindGuardianName = 89;
constexpr uint32_t kKindLairName = 90;
constexpr uint32_t kKindIngredientName = 101;  // 100-119: alchemy

struct Names {
  const char* region;
  const char* beast;
  const char* pill = nullptr;
};

bool lookupNames(const void* user, const char* key, const size_t len, char* out, const size_t outLen) {
  const auto* n = static_cast<const Names*>(user);
  if (len == 11 && memcmp(key, "region.name", 11) == 0 && n->region != nullptr) {
    snprintf(out, outLen, "%s", n->region);
    return true;
  }
  if (len == 10 && memcmp(key, "beast.name", 10) == 0 && n->beast != nullptr) {
    snprintf(out, outLen, "%s", n->beast);
    return true;
  }
  if (len == 9 && memcmp(key, "pill.name", 9) == 0 && n->pill != nullptr) {
    snprintf(out, outLen, "%s", n->pill);
    return true;
  }
  return false;
}

// Game values first fall back to the names, so {$player.name} still works.
struct Chain {
  Names names;
  GrammarValues game;
};
bool lookupChain(const void* user, const char* key, const size_t len, char* out, const size_t outLen) {
  const auto* c = static_cast<const Chain*>(user);
  if (lookupNames(&c->names, key, len, out, outLen)) return true;
  return c->game.lookup != nullptr && c->game.lookup(c->game.user, key, len, out, outLen);
}

bool expandName(const GameState& s, const char* tmpl, const uint8_t region, const uint32_t seed, Grammar& grammar,
                char* out, const size_t len) {
  out[0] = '\0';
  GrammarContext ctx;
  ctx.theme = explore::themeKey(region);
  ctx.stable = true;
  ctx.values = gameValues(s);
  grammar.beginScene();
  const char* text = grammar.expand(tmpl, seed, ctx);
  if (grammar.missing() > 0 || text == nullptr || text[0] == '\0') return false;
  snprintf(out, len, "%s", text);
  return true;
}

}  // namespace

bool regionName(const GameState& s, const uint8_t region, Grammar& grammar, char* out, const size_t len) {
  return expandName(s, "{region.name}", region, contentSeed(worldSeedOf(s), kKindRegionName, region), grammar, out,
                    len);
}

namespace {
static const char* const kEnemyLists[static_cast<int>(explore::Enemy::Count)] = {
    "{beast.region}", "{bandit.region}", "{cultivator.region}", "{spirit.region}"};
static const char* const kGuardianLists[static_cast<int>(explore::Enemy::Count)] = {
    "{guardian.beast}", "{guardian.bandit}", "{guardian.cultivator}", "{guardian.spirit}"};
}  // namespace

bool enemyName(const GameState& s, const uint8_t region, const explore::Enemy kind, const uint32_t seed,
               Grammar& grammar, char* out, const size_t len) {
  const int k = static_cast<int>(kind) < static_cast<int>(explore::Enemy::Count) ? static_cast<int>(kind) : 0;
  return expandName(s, kEnemyLists[k], region, seed, grammar, out, len);
}

uint32_t lairSeed(const GameState& s, const uint8_t region, const uint8_t tile) {
  return contentSeed(
      worldSeedOf(s), kKindLairName,
      (static_cast<uint32_t>(region) << 16) | (static_cast<uint32_t>(explore::reshapeCount(s, region)) << 8) | tile);
}

bool guardianName(const GameState& s, const uint8_t region, Grammar& grammar, char* out, const size_t len) {
  const uint32_t seed = contentSeed(worldSeedOf(s), kKindGuardianName,
                                    (static_cast<uint32_t>(region) << 8) | explore::reshapeCount(s, region));
  const int k = static_cast<int>(explore::guardianEnemy(s, region));
  return expandName(s, kGuardianLists[k], region, seed, grammar, out, len);
}

bool ingredientName(const GameState& s, const inventory::Item item, Grammar& grammar, char* out, const size_t len) {
  out[0] = '\0';
  if (!inventory::isIngredient(item)) return false;
  const uint8_t region = inventory::regionOf(item);
  const bool ore = inventory::isOre(item);
  const uint32_t seed = contentSeed(worldSeedOf(s), kKindIngredientName, static_cast<uint32_t>(region) * 2u + ore);
  return expandName(s, ore ? "{ingredient.ore}" : "{ingredient.herb}", region, seed, grammar, out, len);
}

namespace {
bool flavorWith(const GameState& s, const char* listKey, const uint32_t seed, const Names& names, Grammar& grammar,
                char* out, const size_t len) {
  out[0] = '\0';
  Chain chain{names, gameValues(s)};
  GrammarContext ctx;
  ctx.values.lookup = lookupChain;
  ctx.values.user = &chain;
  ctx.stable = true;  // the same tile reads the same after reopening the game
  if (!grammar.hasKey(listKey, ctx)) return false;
  char tmpl[48];
  snprintf(tmpl, sizeof(tmpl), "{%s}", listKey);
  grammar.beginScene();
  const char* text = grammar.expand(tmpl, seed, ctx);
  if (grammar.missing() > 0 || text == nullptr) return false;
  snprintf(out, len, "%s", text);
  return true;
}
}  // namespace

bool flavor(const GameState& s, const char* listKey, const uint32_t seed, const char* region, const char* beast,
            Grammar& grammar, char* out, const size_t len) {
  return flavorWith(s, listKey, seed, Names{region, beast}, grammar, out, len);
}

bool alchemyFlavor(const GameState& s, const char* listKey, const uint32_t seed, const char* pillName, Grammar& grammar,
                   char* out, const size_t len) {
  return flavorWith(s, listKey, seed, Names{nullptr, nullptr, pillName}, grammar, out, len);
}

bool alchemyLine(lifelog::Line& line, const GameState& s, const char* listKey, const uint32_t seed,
                 const char* pillName, Grammar& grammar) {
  Names names{nullptr, nullptr, pillName};
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  return lifelog::eventLine(line, lifelog::LineKind::Event, listKey, seed, s, grammar, &extra);
}

bool chronicleLine(lifelog::Line& line, const GameState& s, const char* listKey, const uint32_t seed,
                   const char* region, const char* beast, Grammar& grammar) {
  Names names{region, beast};
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  return lifelog::eventLine(line, lifelog::LineKind::Event, listKey, seed, s, grammar, &extra);
}

}  // namespace exploretext
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
