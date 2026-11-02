#pragma once

#include <cstdint>

#include "GameState.h"
#include "QiNumber.h"

// Meridians (v0.2.4, agreed with the user in session 9; DOC "Meridian gates").
//
// 36 acupoints: Ren 6 (front midline), Du 6 (spine) and 12 limb channels of 2.
// Every point gives +10% Qi rate (they multiply: 1.1^n); a limb point also
// strengthens one system (the channel effects below). Points are opened by
// the player on the Meridians screen, paid in Qi from the bar.
//
//   - Allowance: 0, 3, 3, 3, 4, 4, 5, 4, 5, 5 points in realms 0-9, adding up
//     (3 in Qi Condensation, 6 by Foundation ... 36). Unopened points stay
//     allowed forever.
//   - Cost: the n-th point opened costs 5% of the Qi of the realm whose
//     allowance holds it (points 1-3: Qi Condensation, 4-6: Foundation ...),
//     so waiting never makes a point dearer. Paid from the current bar, never
//     below 0: a stage is never lost.
//   - Order: Sea of Qi (Ren 1) first; Ren and Du in order 1-6; a limb
//     channel's second point after its first.
//
// Save: 36 bits in daosAndMeridians[18..22] (bit i of point i); the bytes were
// reserved since milestone 1, so the save version stays 5. Everything else is
// worked out from the bits. Lung's max HP is added to hpMax when opened.
namespace cultivation {
namespace meridians {

constexpr uint8_t kPoints = 36;
constexpr uint8_t kChannels = 14;
constexpr uint32_t kCostPermille = 50;  // 5% of a realm's Qi per point (user, session 9; the doc said 2%)
constexpr uint32_t kQiPerPointPermille = 1100;
constexpr uint8_t kBitsOffset = 18;  // in daosAndMeridians, after the 9 Daos x 2 bytes

enum class Channel : uint8_t {
  Ren,
  Du,
  Lung,
  LargeIntestine,
  Stomach,
  Spleen,
  Heart,
  SmallIntestine,
  Pericardium,
  TripleBurner,
  Bladder,
  Kidney,
  Liver,
  Gallbladder,
};

// Point ids: Ren 0-5, Du 6-11, then limb channel c (Lung = 0) at 12 + 2c and 13 + 2c.
Channel channelOf(uint8_t point);
uint8_t firstPoint(Channel channel);
uint8_t pointsIn(Channel channel);    // 6 for Ren and Du, 2 for a limb channel
uint8_t prerequisite(uint8_t point);  // kPoints for Sea of Qi (none)

bool isOpen(const GameState& state, uint8_t point);
uint8_t openCount(const GameState& state);
uint8_t openIn(const GameState& state, Channel channel);
// The channel's next point to open, or kPoints when it is full.
uint8_t nextIn(const GameState& state, Channel channel);

uint8_t allowanceThrough(uint8_t realm);  // points allowed by realms 0..realm
uint8_t allowance(const GameState& state);
uint8_t ready(const GameState& state);  // allowed and not yet opened
// The realm whose allowance holds the n-th point (1-based), and what it costs.
uint8_t costRealm(uint8_t n);
QiNumber costOfNext(const GameState& state);

enum class Check : uint8_t {
  Ok,
  AlreadyOpen,
  Locked,       // its prerequisite is not open
  NoAllowance,  // every allowed point is open: break through for more
  NotEnoughQi,  // the bar holds less than the cost
};
Check check(const GameState& state, uint8_t point);
// Opens the point if check() allows it: pays, sets the bit, and for Lung adds
// to max HP (and HP). Returns false and changes nothing otherwise.
bool open(GameState& state, uint8_t point);

// Qi rate from meridians, 1.1^open in permille (x1.0 .. x30.9).
uint32_t qiPermille(const GameState& state);

// --- Channel effects (per point opened in the channel; user, session 9) ----------
constexpr uint16_t kLungHp = 10;
int fightLevelBonus(const GameState& state);                  // Large Intestine: +1 level
uint32_t pillBonus(const GameState& state, uint32_t bonus);   // Stomach: +10% each, rounded down
uint8_t brewQualityBonus(const GameState& state);             // Spleen: +5 on the quality roll
uint8_t heartDemonBonus(const GameState& state);              // Heart: +5 on each scene's check
uint16_t lightningPermille(const GameState& state);           // Small Intestine: 10% less lightning damage
uint16_t buyPermille(const GameState& state);                 // Pericardium: 5% cheaper to buy
uint32_t regrowUnits(const GameState& state, uint32_t base);  // Triple Burner: 15% sooner
uint16_t fightDamagePermille(const GameState& state);         // Bladder: 5% less HP lost in fights
uint8_t healHalvesPerMinute(const GameState& state);          // Kidney: 2 = 1 HP a minute, +1 each
uint8_t extraHerbs(const GameState& state);                   // Liver: +1 herb per herb tile
uint8_t slipBonus(const GameState& state);                    // Gallbladder: +10 points to slip away
// Names and places (English, in flash: game content like the built-in pack;
// CrossInk's UI string table is nearly full, see HANDOFF). Places read after
// a comma: "Sea of Qi, two fingers below the navel".
const char* pointName(uint8_t point);
const char* pointPlace(uint8_t point);
const char* channelName(Channel channel);
// A limb channel's effect per point ("+10 max HP"); nullptr for Ren and Du.
const char* channelEffect(Channel channel);
// The element a limb channel belongs to; Element::Count for Ren and Du.
Element channelElement(Channel channel);

// Max HP that damage is reckoned from: without Lung's HP, so the extra HP is
// a real cushion (damage is a share of max HP).
uint16_t damageHpBase(const GameState& state);
// `value` x permille / 1000, rounded up, at least 1 when value > 0.
uint32_t scaleUp(uint32_t value, uint32_t permille);

}  // namespace meridians
}  // namespace cultivation
