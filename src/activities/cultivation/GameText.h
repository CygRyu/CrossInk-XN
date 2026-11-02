#pragma once

#include <cstdint>

// The game's own string table (v0.2.5). CrossInk's I18n table (StrId, tr())
// has a hard 32,767-byte limit for English, which the game's screens nearly
// filled, so every game string lives here, in flash: GameStrings.inc, one
// CULT_STR(id, "text") line each. CrossInk's own strings (STR_SELECT, ...) and
// the three the game's hooks use outside this folder (STR_CULT_MENU,
// STR_CULT_SLEEP_CULTIVATOR, STR_CULT_SLEEP_CULTIVATOR_LIGHT) stay in I18n.
//
//   ctr(CULT_CONTINUE)                 like tr(), for game strings
//   GameStr::CULT_ELEMENT_WOOD         an id, for tables of names
//
// Ids must not start with "STR_": scripts/gen_i18n.py treats every STR_ name
// in the source as a CrossInk string and stops the build if it is missing.
//   cultivation::gameText(id)              the text of an id
namespace cultivation {

enum class GameStr : uint16_t {
#define CULT_STR(id, text) id,
#include "GameStrings.inc"
#undef CULT_STR
  Count
};

const char* gameText(GameStr id);

}  // namespace cultivation

#define ctr(id) ::cultivation::gameText(::cultivation::GameStr::id)
