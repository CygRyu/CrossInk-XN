#pragma once

#include <cstddef>
#include <cstdint>

#include "Exploration.h"
#include "GameState.h"
#include "Grammar.h"
#include "Inventory.h"
#include "LifeLog.h"

// Exploration text from the packs (v0.1.10): region, beast and guardian names
// (themed by region: "region.name~marsh"), the map's tile and fight lines,
// and the Chronicle lines, with {$region.name} and {$beast.name} set.
// Names are stable picks (the seed alone decides), so they read the same
// every time; a region's name never changes when it reshapes.
namespace cultivation {
namespace exploretext {

constexpr size_t kNameSize = 96;

bool regionName(const GameState& state, uint8_t region, Grammar& grammar, char* out, size_t len);
// A random encounter's enemy (seed: explore::beastSeed), or a lair's (seed from
// its tile), from the list of its kind (v0.2.0): beast.region, bandit.region,
// cultivator.region or spirit.region, themed by region.
bool enemyName(const GameState& state, uint8_t region, explore::Enemy kind, uint32_t seed, Grammar& grammar, char* out,
               size_t len);
uint32_t lairSeed(const GameState& state, uint8_t region, uint8_t tile);
// The guardian of the region's current layout (a reshape brings a new one),
// from guardian.beast / .bandit / .cultivator / .spirit by its kind.
bool guardianName(const GameState& state, uint8_t region, Grammar& grammar, char* out, size_t len);

// v0.2.0: a region's own herb or ore ("marsh bitter-root"), a stable pick
// from ingredient.herb~theme / ingredient.ore~theme seeded by the world, so
// every life has its own. False for anything that is not a regional ingredient.
bool ingredientName(const GameState& state, inventory::Item item, Grammar& grammar, char* out, size_t len);

// v0.2.2: a cultivator met on the road, "Old Bai of the Azure Cloud Sect" (seed: Meeting::nameSeed).
bool cultivatorName(const GameState& state, uint8_t region, uint32_t nameSeed, Grammar& grammar, char* out, size_t len);

// One line from `listKey` for the screen (no season label). v0.2.2: `ago` sets
// {$meet.ago} ("two years ago") for lines about someone met before (may be null).
bool flavor(const GameState& state, const char* listKey, uint32_t seed, const char* regionName, const char* beastName,
            Grammar& grammar, char* out, size_t len, const char* ago = nullptr);
// One Chronicle line from `listKey` ("Spring, age 31. ..."), seeded so a retried save writes the same line.
bool chronicleLine(lifelog::Line& line, const GameState& state, const char* listKey, uint32_t seed,
                   const char* regionName, const char* beastName, Grammar& grammar, const char* ago = nullptr);
// v0.2.1: Heart Demon scenes from the player's history: {$region.name},
// {$beast.name} and {$scroll.name} set (any may be null).
bool historyFlavor(const GameState& state, const char* listKey, uint32_t seed, const char* regionName,
                   const char* beastName, const char* scrollName, Grammar& grammar, char* out, size_t len);
// Alchemy's lines ({$pill.name} set): for the screen, and for the Chronicle.
bool alchemyFlavor(const GameState& state, const char* listKey, uint32_t seed, const char* pillName, Grammar& grammar,
                   char* out, size_t len);
bool alchemyLine(lifelog::Line& line, const GameState& state, const char* listKey, uint32_t seed, const char* pillName,
                 Grammar& grammar);

}  // namespace exploretext
}  // namespace cultivation
