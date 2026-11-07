#pragma once

#include <cstdint>

#include "Exploration.h"
#include "GameState.h"
#include "Sects.h"
#include "StatsBridge.h"

// v0.2.8 (milestone 9b plus session-11 idea 1; agreed with the user in
// session 11, HANDOFF 14u): the rival, the rival ladder and nemesis foes.
// Pure rules: no I/O, no UI. Nothing here touches Qi.
//
// THE RIVAL (one per life). Name, sect, gender, a talent factor x0.80-x1.25
// on the hours they need and a hidden temperament come from the world seed.
// They "read" a virtual amount every calendar day from the character's
// creation day (an older character's rival is created on its first v0.2.8
// visit and credited from creationDay): Idle 0-30 min, Steady 20-40, Driven
// 35-55, Erratic 2 h on one day in four, Late bloomer 15 + 5 per realm (at
// most 45) +-10. Their realm and stage come from those minutes: the median
// hours per realm of a simulated 30-min/day reader (balance_sim.py report),
// times their talent factor, split into stages like the player's (+12% per
// stage). Days are seeded by world and date. Without a date nothing moves.
//   Hiatus: on a day that ends 8 days in a row without the player reading
//   (CrossInk's history), the rival pauses; reading resumes them, no catch-up.
//   Elastic: more than one whole realm ahead of the player, half pace; more
//   than one realm behind, +50%.
//   Reactions (14 days): Idle slows to 0-20 min after you beat them; Driven
//   +20% after you overtake or beat them; Erratic bursts one day in two after
//   you beat them.
//
// THE LADDER, one per realm from Qi Condensation: 20 generated cultivators
// in a fixed order (seats 1-20). Only the player's rank is stored: with the
// player at rank P, ranks above P show seats 1..P-1, ranks below show seats
// P..19. The rival takes the seat their stage gives (18 at their realm's
// first stage, 3 at its last) while in the player's realm. Once a calendar day
// the player may challenge the rank above (unranked: rank 20); once a day the
// one below challenges the player, who may decline for free. A won bout
// swaps ranks and pays 20 x (realm + 1) stones; a lost one costs 10% of max
// HP (and needs that much). Rank 20 fights about 6 levels below the player,
// rank 1 about 8 above. Top 3: a title until the next realm.
//
// NEMESES (beasts, bandits and road cultivators, never the rival), at most 3.
// Born when such an enemy beats you in a Dangerous or harder fight (35%), or
// when you beat a returning foe on its third meeting (25%). They never die:
// beaten, they escape with a scar and a new trait (half strengths, half
// weaknesses, at most 4); winning, they gain 2 levels (at most your level +
// 12) and the epithet "the Victor". Every 2 reading hours (1 for a Hunter)
// one moves a region toward the region you explore; in your region it finds
// you on 6% of steps (12% Ambusher), then rests 2 reading hours (1 Quick to
// heal). Traits stay hidden until a cultivator tells you, a rumor names one,
// or from your second fight with it on.
//
// Save (GameState::rivals, 96 bytes, save v6; all zero = nothing yet):
//   0-15 the rival:
//     0 flags (0x01 created, 0x02 known, 0x04 player ahead at the last visit,
//       0x08 in hiatus at the last visit, 0x10 met on the road before)
//     1 temperament, 2 talent percent (80-125), 3 realm last announced
//     4-7 virtual reading in tenths of a minute (u32)
//     8-9 last day credited + 1, 10-11 reaction start day + 1 (u16)
//     12 reaction (0 none, 1 beaten, 2 overtaken), 13 your wins, 14 your losses, 15 spare
//   16-23 the ladder: 16 realm + 1, 17 your rank (0 unranked), 18-19 last
//     challenge day + 1, 20-21 last defence day + 1, 22 best rank, 23 the
//     region you last entered + 1 (where nemeses roam to)
//   24 + 24 x i, nemesis i (layout in Rivals.cpp)
namespace cultivation {
namespace rivals {

// --- The rival -------------------------------------------------------------------------------

enum class Temper : uint8_t { Idle = 0, Steady, Driven, Erratic, LateBloomer, Count };

constexpr uint16_t kHiatusDays = 7;        // unread days before the rival pauses (from the 8th)
constexpr uint16_t kReactionDays = 14;     // two weeks
constexpr uint8_t kRivalMeetPercent = 40;  // v0.3.1 (user; 25 before)
// Median reading hours per realm x 10 (balance_sim.py report, 30 min/day, session 11).
constexpr uint16_t kRealmDeciHours[10] = {40, 122, 374, 518, 793, 1003, 1160, 1357, 1549, 1633};

struct Rival {
  bool exists = false;
  bool known = false;      // met, or the Fallen clan heir's from day one
  bool metBefore = false;  // met on the road at least once
  Temper temper = Temper::Steady;
  uint8_t talentPercent = 100;  // on the hours they need: lower is faster
  Gender gender = Gender::Male;
  uint32_t seed = 0;  // names them
  uint32_t deciMinutes = 0;
  uint8_t realm = 0;
  uint8_t stage = 0;
  bool hiatus = false;
  uint8_t wins = 0;  // yours against them
  uint8_t losses = 0;
};

uint32_t rivalSeed(const GameState& state);
Rival rival(const GameState& state);
// Realm and stage after `deciMinutes` of virtual reading with that talent.
void positionFor(uint32_t deciMinutes, uint8_t talentPercent, uint8_t& realm, uint8_t& stage);
// Realm x 1000 + stage share (comparable for the rival and the player).
int32_t rivalPosition(const GameState& state);
int32_t playerPosition(const GameState& state);
// Stages the rival is ahead (negative: behind), counting every stage of the realms between.
int stageGap(const GameState& state);
// One day's virtual minutes before the elastic distance (exposed for tests).
uint32_t dayMinutes(const GameState& state, uint32_t day, uint8_t rivalRealm);

struct News {
  bool created = false;
  bool realmReached = false;  // a new realm since the last announcement (known rivals only)
  uint8_t realm = 0;
  bool hiatusBegan = false;
  bool hiatusEnded = false;
  bool youOvertook = false;  // you passed them since the last visit
  bool theyOvertook = false;
  uint32_t daysCredited = 0;
};

// One visit: creates the rival if needed and credits every day since the last
// one credited (nothing without a date). Call after absorb().
News update(GameState& state, const StatsSample& now);

// Fight level: 10 x realm + stage (Layers as is, Early..Peak x 3) + 2 x realm
// for their techniques. Never scaled to you.
int rivalLevel(const GameState& state);
// The rival may walk this region's road once their realm has opened it.
bool canMeetIn(const GameState& state, uint8_t region);
// What the rival offers on the road: a trade (Idle; Erratic one meeting in two) or a spar.
bool offersTrade(const GameState& state, uint8_t meeting);
void markKnown(GameState& state);
void markMet(GameState& state);
// v0.3.1: moves every stored day (last credited, reaction, the ladder's bouts) by `delta`,
// for days::resolve() when the date comes back after reading days.
void shiftDays(GameState& state, int32_t delta);

// You beat them (on the road or the ladder) on `today` (kNoDay without a date): record and reaction.
void beaten(GameState& state, uint32_t today);
void lostTo(GameState& state);

// v0.3.4 (the agenda, Agenda.h): an inheritance makes their talent faster (stored 80-125);
// closed-door cultivation puts them at the start of their next stage (the next realm's
// first after their last). Returns false when there is no next stage.
void setTalent(GameState& state, uint8_t talentPercent);
bool stepAhead(GameState& state);
// The virtual reading at which (realm, stage) begins, for that talent.
uint32_t stageStart(uint8_t realm, uint8_t stage, uint8_t talentPercent);

// --- The ladder ------------------------------------------------------------------------------

constexpr uint8_t kLadderSize = 20;
constexpr uint8_t kHpCostPercent = 10;

uint8_t playerRank(const GameState& state);   // 0 = unranked (or no ladder in this realm)
uint8_t rivalSeat(const GameState& state);    // 0 = not on this realm's ladder
uint8_t rivalRank(const GameState& state);    // 0 = not shown
uint8_t titleRank(const GameState& state);    // 1-3 while in the top three, else 0
uint8_t ladderRealm(const GameState& state);  // the ladder's realm (the player's own once set up)
bool ladderOpen(const GameState& state);      // from Qi Condensation
// Rank 1..20: who is there (the player, the rival or seat n) and their level.
struct Rung {
  bool player = false;
  bool rival = false;
  uint8_t seat = 0;  // 1..20 for generated ones
  int level = 0;
};
Rung rung(const GameState& state, uint8_t rank);
uint32_t seatSeed(const GameState& state, uint8_t seat);  // names seat n of the current realm's ladder
int rankOffset(uint8_t rank);                             // -6 at 20 .. +8 at 1

enum class Block : uint8_t { None = 0, NoLadder, NoDate, DoneToday, AtTop, NoOneBelow, NoHp };
struct Bout {
  Block block = Block::NoLadder;
  uint8_t rank = 0;  // the opponent's rank now
  Rung who;
  explore::Danger danger = explore::Danger::Dangerous;
  uint8_t chance = 0;
  uint16_t hpCost = 0;
  uint32_t stones = 0;
  explore::Wound wound = explore::Wound::None;  // v0.2.9: already in danger and chance
};
Bout challengeBout(const GameState& state);
Bout defenceBout(const GameState& state);  // block None: someone below challenges you today

struct BoutResult {
  bool done = false;
  bool won = false;
  bool rival = false;  // the opponent was the rival
  uint8_t roll = 0;
  uint16_t hpLost = 0;
  uint32_t stones = 0;
  uint8_t rankBefore = 0;
  uint8_t rankAfter = 0;
  Rung who;
  sects::Influence world;  // v0.3.3: a seat holder is one of the five sects' people
};
BoutResult challenge(GameState& state);
BoutResult acceptDefence(GameState& state);
bool declineDefence(GameState& state);  // free

// --- Nemeses ---------------------------------------------------------------------------------

constexpr uint8_t kNemesisMax = 3;
constexpr uint8_t kMaxTraits = 4;
constexpr uint8_t kBirthPercent = 35;
constexpr uint8_t kReturningBirthPercent = 25;
constexpr uint8_t kFindPercent = 6;
constexpr uint8_t kLevelsPerWin = 2;
constexpr uint8_t kLevelCapAbove = 12;
constexpr uint16_t kRoamUnits = 12;  // 2 reading hours, in 10-minute units
constexpr uint16_t kRestUnits = 12;

enum class Trait : uint8_t {
  None = 0,
  // strengths
  Overcoming,   // its element beats your root's: +3 levels
  IronHide,     // +3 levels
  Hunter,       // you cannot slip away; roams twice as fast
  Venomous,     // a loss costs 50% more HP
  Ambusher,     // finds you twice as often
  QuickToHeal,  // rests half as long
  PackLeader,   // +2 levels, a better prize when beaten
  // weaknesses
  Overcome,   // your root's element beats its: -3 levels
  Proud,      // -2 levels on the fight after it beat you
  Coward,     // flees without a fight when your chance is 60% or better
  Slow,       // slipping away always works
  Greedy,     // can be paid off, for half
  FearsDawn,  // -3 levels when met 06:00-12:00
  OldWound,   // -1 level per time you have beaten it, at most -4
  Count
};
bool isStrength(Trait t);
// Wood -> Fire -> Earth -> Metal -> Water nourish; Wood beats Earth, Earth Water, Water Fire, Fire Metal, Metal Wood.
bool overcomes(Element a, Element b);

enum class EpithetKind : uint8_t { Birth = 0, Scar, Victor };

struct Nemesis {
  bool active = false;
  explore::Enemy kind = explore::Enemy::Beast;
  Element element = Element::Earth;
  bool elementKnown = false;
  uint8_t region = 0;       // where it is now
  uint8_t birthRegion = 0;  // names its kind ("a marsh crocodile")
  uint8_t level = 0;        // stored; see fightLevel
  Trait traits[kMaxTraits] = {};
  uint8_t traitCount = 0;
  uint8_t revealed = 0;  // bit per trait slot
  uint8_t wins = 0;      // yours
  uint8_t losses = 0;    // yours
  uint8_t escapes = 0;   // you slipped away
  uint8_t fled = 0;      // it fled from you (Coward)
  uint8_t paid = 0;
  uint8_t fights = 0;  // every meeting that came to a choice
  bool wonLast = false;
  EpithetKind epithet = EpithetKind::Birth;
  uint8_t epithetCount = 0;  // seeds the epithet
  uint32_t seed = 0;         // names it
  uint16_t movedStamp = 0;
  uint16_t restUntil = 0;
};

bool nemesis(const GameState& state, uint8_t index, Nemesis& out);
uint8_t nemesisCount(const GameState& state);
bool hasTrait(const Nemesis& n, Trait t);
bool traitKnown(const Nemesis& n, Trait t);
// The level it fights at: its stored level (at least a random encounter's in
// its region, regionBase + 2) and its traits, today and at `hour` (-1 = unknown).
int fightLevel(const GameState& state, const Nemesis& n, int hour);
bool resting(const GameState& state, const Nemesis& n);
// "Hunting you" (in the region you explore) or the region it was last seen in.
bool hunting(const GameState& state, const Nemesis& n);

// The device's clock hour for Fears the dawn (-1 = unknown); set by the activity.
void setClockHour(int hour);
int clockHour();

// Where you explore (the map you opened last), for roaming.
void setPlayerRegion(GameState& state, uint8_t region);
int playerRegion(const GameState& state);  // -1 before any map

// Birth: a new nemesis from an enemy that beat you, or a returning foe that
// survived. Returns its index, or -1 when all three places are taken.
int bear(GameState& state, explore::Enemy kind, uint8_t region, int level, uint32_t seed, uint32_t roll);
// One visit: roaming on reading time.
void roam(GameState& state);
// A step in `region`: a nemesis there that is not resting finds you (rolled
// from `roll`, 0-99 per nemesis in order). Returns its index or -1.
int ambush(const GameState& state, uint8_t region, uint32_t seed);

// After a fight with nemesis i. `playerLevelNow` caps its growth.
struct Aftermath {
  Trait newTrait = Trait::None;       // gained on escaping beaten
  Trait revealedTrait = Trait::None;  // you noticed (second fight on)
};
Aftermath youWon(GameState& state, uint8_t index, uint32_t seed);
Aftermath youLost(GameState& state, uint8_t index, int playerLevelNow, uint32_t seed);
Aftermath youEscaped(GameState& state, uint8_t index, bool paid, uint32_t seed);
void itFled(GameState& state, uint8_t index);
// v0.3.4: your rival beat it on a hunt: a scar (the epithet), then it rests. No trait (traits are about you).
// `atSeconds`: the reading time of the world step it happened in (the rest counts from there).
void scarredByRival(GameState& state, uint8_t index, uint32_t atSeconds);
// Reveals one hidden trait (the one hunting you first). Returns the nemesis
// index (and the trait) or -1 when nothing is hidden.
int revealOne(GameState& state, Trait& trait, uint32_t seed);
// Today's rumor names a nemesis one day in four while something is hidden.
int rumorPick(const GameState& state, uint32_t day, Trait& trait);
// What Pay costs (0 = cannot pay): a bandit what a bandit on the road asks,
// 15 x (region + 1); a Greedy one (bandit or cultivator) half that.
uint32_t payCost(const Nemesis& n);
// Reveals exactly this trait (a rumor named it).
void revealTrait(GameState& state, uint8_t index, Trait t);

}  // namespace rivals
}  // namespace cultivation
