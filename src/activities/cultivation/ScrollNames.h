#pragma once

#include <cstddef>
#include <cstdint>

#include "Grammar.h"
#include "LifeLog.h"
#include "Scrolls.h"

// Names and Chronicle lines for Technique Scrolls (DOC: Naming).
//
// A name is "{scroll.name} {scroll.kind.<kind>} {scroll.tier.<tier>}" from
// the packs, themed by the scroll's element and seeded by the title hash and
// the world, with stable picks: the same scroll always gets the same name,
// and the book's own title never contributes a word. Names are not stored in
// the save; they are rebuilt whenever they are shown (a few lists, read from
// flash or the SD index).
namespace cultivation {
namespace scrollnames {

constexpr size_t kNameSize = 72;  // the longest pattern is about 60 bytes

// False (and out = "") without a grammar engine or when the lists are missing.
bool name(const scrolls::Scroll& s, uint32_t worldSeed, Grammar& grammar, char* out, size_t outLen);

// A Chronicle line about a scroll from list `key` (scroll.line.new, .reread,
// .sold, .key), labelled with the character's current season.
bool line(lifelog::Line& out, const char* key, const scrolls::Scroll& s, const GameState& state, Grammar& grammar);

}  // namespace scrollnames
}  // namespace cultivation
