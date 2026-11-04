#pragma once

#include <cstddef>
#include <cstdint>

#include "Alchemy.h"
#include "Breakthrough.h"
#include "Daos.h"
#include "GameState.h"
#include "Inventory.h"
#include "Rivals.h"
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
void stageName(const GameState& s, char* out, size_t len);              // "Layer 4", "Peak", "Great Perfection"
void stageNameOf(uint8_t realm, uint8_t stage, char* out, size_t len);  // v0.2.8: "Layer 4", "Peak"
// "Path of Immortal Reading v0.1.9"
void versionLine(char* out, size_t len);

// v0.2.0: items and pills.
const char* gradeWord(inventory::Grade g);  // "Common", "Spirit", "Rare"
const char* pillName(alchemy::Pill pill);   // "Core-Forming pill", "common breakthrough pill"
// Every item but a region's own herb or ore, whose name comes from the packs
// (the screens look those up first; this gives "Herb of region 3").
void itemName(inventory::Item item, char* out, size_t len);
void itemDescription(inventory::Item item, char* out, size_t len);

// v0.2.6: "Dao of Flowing Water" (CultivationDailyScreens.cpp).
const char* daoName(daos::Dao dao);

// v0.2.8 (CultivationRivalScreens.cpp): a nemesis trait's name, what it does,
// and how a rumor tells it; "iron hide, slow" for the known ones ("" if none).
const char* traitName(rivals::Trait t);
const char* traitEffect(rivals::Trait t);
const char* traitRumor(rivals::Trait t);
void knownTraits(const rivals::Nemesis& n, char* out, size_t len);
// v0.2.9: the same with a short effect each, for the fight screen:
// "iron hide (+3 levels), fears the dawn (-3 levels in the morning)".
void knownTraitsWithEffects(const rivals::Nemesis& n, char* out, size_t len);
// v0.2.9: "Wounded (under 30% HP): you fight 3 levels lower and are 15% less
// likely to slip away." ("" when not wounded).
void woundLine(const GameState& s, char* out, size_t len);
// "First of the Qi Condensation Ranking" while in the top three ("" otherwise).
void ladderTitle(const GameState& s, char* out, size_t len);

}  // namespace ui
}  // namespace cultivation
