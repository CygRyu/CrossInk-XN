#pragma once

#include <cstddef>
#include <cstdint>

#include "Alchemy.h"
#include "Breakthrough.h"
#include "GameState.h"
#include "Inventory.h"
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
// "Path of Immortal Reading v0.1.9"
void versionLine(char* out, size_t len);

// v0.2.0: items and pills.
const char* gradeWord(inventory::Grade g);  // "Common", "Spirit", "Rare"
const char* pillName(alchemy::Pill pill);   // "Core-Forming pill", "common breakthrough pill"
// Every item but a region's own herb or ore, whose name comes from the packs
// (the screens look those up first; this gives "Herb of region 3").
void itemName(inventory::Item item, char* out, size_t len);
void itemDescription(inventory::Item item, char* out, size_t len);

}  // namespace ui
}  // namespace cultivation
