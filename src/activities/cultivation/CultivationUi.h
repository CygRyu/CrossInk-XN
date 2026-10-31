#pragma once

#include <cstddef>
#include <cstdint>

#include "Breakthrough.h"
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
void formatPermille(char* buf, size_t len, uint32_t permille);       // "x1.30"
const char* gradeName(breakthrough::GradeKind kind, uint8_t grade);  // "" for none
// "Flawed Foundation, 9th-Grade Golden Core, Half-step Nascent Soul" ("" if none).
void formatTitles(const GameState& s, char* out, size_t len);
void stageName(const GameState& s, char* out, size_t len);  // "Layer 4", "Peak", "Great Perfection"
// "CrossInk 1.6.1 · Immortal Reading M8 · a1b2c3d"
void versionLine(char* out, size_t len);

}  // namespace ui
}  // namespace cultivation
