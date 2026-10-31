#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "Scrolls.h"

// Small text helpers shared by the game's screen files (all names via tr()).
namespace cultivation {
namespace ui {

const char* realmName(uint8_t realm);
const char* elementName(Element e);
const char* tierName(scrolls::Tier t);
const char* kindName(scrolls::Kind k);
const char* qualityName(scrolls::Quality q);
void formatPermille(char* buf, size_t len, uint32_t permille);  // "x1.30"

}  // namespace ui
}  // namespace cultivation
