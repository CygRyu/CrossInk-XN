#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>
#include <cstring>

#include "Exploration.h"
#include "ExploreText.h"
#include "GameValues.h"
#include "Rivals.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace exploretext {
namespace {

constexpr uint32_t kKindRegionName = 81;
constexpr uint32_t kKindSectName = 136;  // v0.3.2: the five sects of a world
constexpr uint32_t kKindGuardianName = 89;
constexpr uint32_t kKindLairName = 90;
constexpr uint32_t kKindIngredientName = 101;  // 100-119: alchemy

struct Names {
  const char* region;
  const char* beast;
  const char* pill = nullptr;
  const char* scroll = nullptr;  // v0.2.1: Heart Demon scenes
  const char* ago = nullptr;     // v0.2.2: "two years ago"
  const char* point = nullptr;   // v0.2.4: a meridian point and where it is
  const char* place = nullptr;
  const char* what = nullptr;  // v0.2.8: a realm, a rank, a trait
  const char* sect = nullptr;  // v0.3.2: the living world's sects
  const char* sect2 = nullptr;
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
  if (len == 11 && memcmp(key, "scroll.name", 11) == 0 && n->scroll != nullptr) {
    snprintf(out, outLen, "%s", n->scroll);
    return true;
  }
  if (len == 8 && memcmp(key, "meet.ago", 8) == 0 && n->ago != nullptr) {
    snprintf(out, outLen, "%s", n->ago);
    return true;
  }
  if (len == 10 && memcmp(key, "point.name", 10) == 0 && n->point != nullptr) {
    snprintf(out, outLen, "%s", n->point);
    return true;
  }
  if (len == 11 && memcmp(key, "point.place", 11) == 0 && n->place != nullptr) {
    snprintf(out, outLen, "%s", n->place);
    return true;
  }
  if (len == 4 && memcmp(key, "what", 4) == 0 && n->what != nullptr) {
    snprintf(out, outLen, "%s", n->what);
    return true;
  }
  if (len == 4 && memcmp(key, "sect", 4) == 0 && n->sect != nullptr) {
    snprintf(out, outLen, "%s", n->sect);
    return true;
  }
  if (len == 5 && memcmp(key, "sect2", 5) == 0 && n->sect2 != nullptr) {
    snprintf(out, outLen, "%s", n->sect2);
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
            Grammar& grammar, char* out, const size_t len, const char* ago) {
  Names names{region, beast};
  names.ago = ago;
  return flavorWith(s, listKey, seed, names, grammar, out, len);
}

bool cultivatorName(const GameState& s, const uint8_t region, const uint32_t nameSeed, Grammar& grammar, char* out,
                    const size_t len) {
  // v0.3.3: one of the world's five sects (in a war, one side; in a cult's region, a follower).
  char sect[kNameSize];
  if (personSectName(s, region, nameSeed, grammar, sect, sizeof(sect))) {
    char who[kNameSize];
    if (!expandName(s, "{cultivator.name}", region, nameSeed, grammar, who, sizeof(who))) return false;
    snprintf(out, len, sects::personSect(s, region, nameSeed) == sects::kCult ? "%s, a follower of %s" : "%s of %s",
             who, sect);
    return true;
  }
  return expandName(s, "{meet.who}", region, nameSeed, grammar, out, len);
}

bool historyFlavor(const GameState& s, const char* listKey, const uint32_t seed, const char* region, const char* beast,
                   const char* scroll, Grammar& grammar, char* out, const size_t len) {
  return flavorWith(s, listKey, seed, Names{region, beast, nullptr, scroll}, grammar, out, len);
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

bool meridianLine(lifelog::Line& line, const GameState& s, const uint32_t seed, const char* point, const char* place,
                  Grammar& grammar) {
  Names names{nullptr, nullptr};
  names.point = point;
  names.place = place;
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  return lifelog::eventLine(line, lifelog::LineKind::Event, "meridian.line", seed, s, grammar, &extra);
}

bool chronicleLine(lifelog::Line& line, const GameState& s, const char* listKey, const uint32_t seed,
                   const char* region, const char* beast, Grammar& grammar, const char* ago) {
  Names names{region, beast};
  names.ago = ago;
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  return lifelog::eventLine(line, lifelog::LineKind::Event, listKey, seed, s, grammar, &extra);
}

bool personFlavor(const GameState& s, const char* listKey, const uint32_t seed, const char* region, const char* name,
                  const char* what, Grammar& grammar, char* out, const size_t len) {
  Names names{region, name};
  names.what = what;
  return flavorWith(s, listKey, seed, names, grammar, out, len);
}

bool personLine(lifelog::Line& line, const GameState& s, const char* listKey, const uint32_t seed, const char* region,
                const char* name, const char* what, Grammar& grammar) {
  Names names{region, name};
  names.what = what;
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  return lifelog::eventLine(line, lifelog::LineKind::Event, listKey, seed, s, grammar, &extra);
}

bool rivalName(const GameState& s, Grammar& grammar, char* name, const size_t nameLen, char* sect,
               const size_t sectLen) {
  const rivals::Rival r = rivals::rival(s);
  const uint32_t seed = r.seed != 0 ? r.seed : rivals::rivalSeed(s);
  const bool a = expandName(s, r.gender == Gender::Female ? "{rival.name.female}" : "{rival.name.male}", 0, seed,
                            grammar, name, nameLen);
  // v0.3.3: the rival belongs to one of the world's five sects (user).
  const int8_t own = sects::personSect(s, sects::kNoRegion, seed);
  const bool b = own >= 0 ? sectName(s, static_cast<uint8_t>(own), grammar, sect, sectLen)
                          : expandName(s, "the {sect.name}", 0, mix32(seed, 0x53454354u), grammar, sect, sectLen);
  return a && b;
}

bool seatName(const GameState& s, const uint32_t seed, Grammar& grammar, char* out, const size_t len) {
  const uint8_t region = s.progress.realm >= 1 ? static_cast<uint8_t>(s.progress.realm - 1) : 0;
  // v0.3.3: a seat holder belongs to one of the five sects (no war or cult on the ranking).
  char sect[kNameSize];
  if (personSectName(s, sects::kNoRegion, seed, grammar, sect, sizeof(sect))) {
    char who[kNameSize];
    // v0.3.7: four seats in five carry a real name; the six road nicknames repeated across twenty
    // seats ("the one-armed swordsman ..." three times in a top four).
    if (Rng(mix32(seed, 0x53454154u)).below(5) != 0) {
      const Gender g = (seed >> 9) & 1u ? Gender::Female : Gender::Male;
      suggestName(seed, 0, g, who, sizeof(who));
      if (strcmp(who, s.character.name.data()) == 0) suggestName(seed, 1, g, who, sizeof(who));
    } else if (!expandName(s, "{cultivator.name}", region, seed, grammar, who, sizeof(who))) {
      return false;
    }
    snprintf(out, len, "%s of %s", who, sect);
    return true;
  }
  return expandName(s, "{meet.who}", region, seed, grammar, out, len);
}

namespace {
bool nemesisBase(const GameState& s, const rivals::Nemesis& n, Grammar& grammar, char* out, const size_t len) {
  switch (n.kind) {
    case explore::Enemy::Cultivator: {
      // The name a meeting gave them (the part before "of the ..."), so a road cultivator keeps theirs.
      if (!expandName(s, "{meet.who}", n.birthRegion, n.seed, grammar, out, len)) return false;
      char* cut = strstr(out, " of the ");
      if (cut != nullptr) *cut = '\0';
      return true;
    }
    case explore::Enemy::Bandit:
      return expandName(s, "{nemesis.name.bandit}", n.birthRegion, n.seed, grammar, out, len);
    default:
      return expandName(s, "{nemesis.name.beast}", n.birthRegion, n.seed, grammar, out, len);
  }
}
}  // namespace

bool nemesisName(const GameState& s, const uint8_t index, Grammar& grammar, char* out, const size_t len) {
  out[0] = '\0';
  rivals::Nemesis n;
  if (!rivals::nemesis(s, index, n)) return false;
  char base[64];
  char epithet[48];
  if (!nemesisBase(s, n, grammar, base, sizeof(base))) return false;
  static const char* const kLists[3] = {"{nemesis.epithet}", "{nemesis.epithet.scar}", "{nemesis.epithet.victor}"};
  const int k = static_cast<int>(n.epithet) < 3 ? static_cast<int>(n.epithet) : 0;
  if (!expandName(s, kLists[k], n.birthRegion, mix32(n.seed, 0x45504900u + n.epithetCount), grammar, epithet,
                  sizeof(epithet))) {
    snprintf(out, len, "%s", base);
    return true;
  }
  snprintf(out, len, "%s the %s", base, epithet);
  return true;
}

bool nemesisKind(const GameState& s, const uint8_t index, Grammar& grammar, char* out, const size_t len) {
  out[0] = '\0';
  rivals::Nemesis n;
  if (!rivals::nemesis(s, index, n)) return false;
  return enemyName(s, n.birthRegion, n.kind, n.seed, grammar, out, len);
}

}  // namespace exploretext
}  // namespace cultivation

namespace cultivation {
namespace exploretext {

namespace {
// A sentence never starts in lower case: the first letter, and any after ". "
// (lines put "the Azure Cloud Sect" at the start of a sentence).
void sentenceCase(char* t) {
  if (t[0] >= 'a' && t[0] <= 'z') t[0] = static_cast<char>(t[0] - 'a' + 'A');
  for (char* p = t; (p = strstr(p, ". ")) != nullptr; p += 2) {
    if (p[2] >= 'a' && p[2] <= 'z') p[2] = static_cast<char>(p[2] - 'a' + 'A');
  }
}

// "the Azure Cloud Sect" -> the two words that tell sects apart, "Azure" and "Cloud".
bool sameWord(const char* a, const char* b, const int word) {
  for (int w = 0; w <= word; ++w) {  // skip "the " and earlier words
    a = strchr(a, ' ');
    b = strchr(b, ' ');
    if (a == nullptr || b == nullptr) return false;
    ++a;
    ++b;
  }
  const char* ae = strchr(a, ' ');
  const char* be = strchr(b, ' ');
  const size_t al = ae != nullptr ? static_cast<size_t>(ae - a) : strlen(a);
  const size_t bl = be != nullptr ? static_cast<size_t>(be - b) : strlen(b);
  return al == bl && memcmp(a, b, al) == 0;
}
}  // namespace

bool sectName(const GameState& s, const uint8_t index, Grammar& grammar, char* out, const size_t len) {
  // Five names easy to tell apart: each sect's first word ("Azure", "Jade") is
  // its own. Worked out in order, so sect i needs the names before it.
  if (index >= 5) return false;
  char names[5][kNameSize];
  for (uint8_t i = 0; i <= index; ++i) {
    bool found = false;
    for (uint8_t attempt = 0; attempt < 48 && !found; ++attempt) {
      if (!expandName(s, "the {sect.name}", 0, contentSeed(worldSeedOf(s), kKindSectName, i * 64u + attempt), grammar,
                      names[i], sizeof(names[i]))) {
        return false;
      }
      found = true;
      for (uint8_t j = 0; j < i && found; ++j) found = !sameWord(names[i], names[j], 0);
    }
  }
  snprintf(out, len, "%s", names[index]);
  return true;
}

bool cultName(const GameState& s, const uint8_t sect, Grammar& grammar, char* out, const size_t len) {
  const sects::Sect k = sects::sect(s, sect);
  return expandName(s, "{world.cult.name}", 0,
                    contentSeed(worldSeedOf(s), kKindSectName, 0x1000u + k.seed + 256u * sect), grammar, out, len);
}

bool personSectName(const GameState& s, const uint8_t region, const uint32_t seed, Grammar& grammar, char* out,
                    const size_t len) {
  const int8_t who = sects::personSect(s, region, seed);
  if (who == sects::kCult) return cultName(s, sects::conditionSect(s), grammar, out, len);
  if (who < 0) return false;
  return sectName(s, static_cast<uint8_t>(who), grammar, out, len);
}

namespace {
struct WorldNames {
  char region[kNameSize];
  char sect[kNameSize];
  char sect2[kNameSize];
};
bool worldNames(const GameState& s, const uint8_t region, const uint8_t sect, const int8_t sect2, Grammar& grammar,
                WorldNames& n) {
  n.region[0] = n.sect[0] = n.sect2[0] = '\0';
  if (!regionName(s, region, grammar, n.region, sizeof(n.region))) return false;
  if (!sectName(s, sect, grammar, n.sect, sizeof(n.sect))) return false;
  if (sect2 == sects::kCult) return cultName(s, sect, grammar, n.sect2, sizeof(n.sect2));  // v0.3.3
  if (sect2 >= 0 && !sectName(s, static_cast<uint8_t>(sect2), grammar, n.sect2, sizeof(n.sect2))) return false;
  return true;
}
}  // namespace

bool worldLine(lifelog::Line& line, const GameState& s, const char* listKey, const uint32_t seed, const uint8_t region,
               const uint8_t sect, const int8_t sect2, Grammar& grammar) {
  WorldNames wn;
  if (!worldNames(s, region, sect, sect2, grammar, wn)) return false;
  Names names{wn.region, nullptr};
  names.sect = wn.sect;
  names.sect2 = sect2 >= 0 || sect2 == sects::kCult ? wn.sect2 : nullptr;
  GrammarValues extra;
  extra.lookup = lookupNames;
  extra.user = &names;
  if (!lifelog::eventLine(line, lifelog::LineKind::Event, listKey, seed, s, grammar, &extra)) return false;
  sentenceCase(line.text);
  return true;
}

bool worldRumor(const GameState& s, const char* listKey, const uint32_t seed, const uint8_t region, const uint8_t sect,
                const int8_t sect2, Grammar& grammar, char* out, const size_t len) {
  out[0] = '\0';
  WorldNames wn;
  if (!worldNames(s, region, sect, sect2, grammar, wn)) return false;
  Names names{wn.region, nullptr};
  names.sect = wn.sect;
  names.sect2 = sect2 >= 0 || sect2 == sects::kCult ? wn.sect2 : nullptr;
  Chain chain{names, gameValues(s)};
  GrammarContext ctx;
  ctx.values.lookup = lookupChain;
  ctx.values.user = &chain;
  if (!grammar.hasKey(listKey, ctx)) return false;
  char tmpl[64];
  snprintf(tmpl, sizeof(tmpl), "{world.rumor.opening} {%s}.", listKey);
  grammar.beginScene();
  const char* text = grammar.expand(tmpl, seed, ctx);
  if (grammar.missing() > 0 || text == nullptr) return false;
  snprintf(out, len, "%s", text);
  sentenceCase(out);
  return true;
}

}  // namespace exploretext
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
