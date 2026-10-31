#pragma once

#include <cstddef>

#include "GameState.h"
#include "Grammar.h"

// Game values the grammar reads as {$name} (DOC: "The grammar reads these as
// {$pro.they}, {$pro.them}, {$pro.their} and {$title.senior}").
//
//   player.name     the cultivator's name
//   player.realm    realm name ("Qi Condensation"); also "realm"
//   player.stage    "Layer 3" or "Early" ... "Peak"
//   player.age      age in years
//   player.element  root element ("Wood")
//   season          the in-game season ("spring"); the life log sets it per line
//   root.glow / root.color   the Awakening Test stone ("murky", "green")
//   past.pages / past.books / past.hours   reading recorded before this life
//                   began (the baseline), with their noun: "48,210 pages",
//                   "1 book", "no hours"
//   scroll.name     a Technique Scroll's name, only in the scroll's own
//                   Chronicle lines (set by ScrollNames; unknown elsewhere)
//   pro.they / pro.them / pro.their / pro.theirs / pro.themself
//   pro.is / pro.was / pro.has     verb forms that agree ("they are", "she is")
//   title.senior / title.sect / title.god   gendered titles from the doc's table
//
// These are generated story text in English, like the name pools and the
// packs themselves, so they are not UI strings and do not go through tr().
namespace cultivation {

// True for every name lookupGameValue() knows; the pack checker uses it.
bool isGameValueName(const char* name, size_t nameLen);

bool lookupGameValue(const GameState& state, const char* name, size_t nameLen, char* out, size_t outLen);

// Adapter for GrammarContext::values; `state` must outlive the expansion.
GrammarValues gameValues(const GameState& state);

}  // namespace cultivation
