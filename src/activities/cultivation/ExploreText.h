#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "Grammar.h"
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
// A random encounter's beast (seed: explore::beastSeed), or a lair's (seed from its tile).
bool beastName(const GameState& state, uint8_t region, uint32_t seed, Grammar& grammar, char* out, size_t len);
uint32_t lairSeed(const GameState& state, uint8_t region, uint8_t tile);
// The guardian of the region's current layout (a reshape brings a new one).
bool guardianName(const GameState& state, uint8_t region, Grammar& grammar, char* out, size_t len);

// One line from `listKey` for the screen (no season label).
bool flavor(const GameState& state, const char* listKey, uint32_t seed, const char* regionName, const char* beastName,
            Grammar& grammar, char* out, size_t len);
// One Chronicle line from `listKey` ("Spring, age 31. ..."), seeded so a retried save writes the same line.
bool chronicleLine(lifelog::Line& line, const GameState& state, const char* listKey, uint32_t seed,
                   const char* regionName, const char* beastName, Grammar& grammar);

}  // namespace exploretext
}  // namespace cultivation
