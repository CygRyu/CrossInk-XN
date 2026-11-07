#pragma once

#include <cstdint>

#include "Exploration.h"
#include "GameState.h"

// v0.3.5 "The cast" (designed and agreed with the user in session 16, HANDOFF 14o):
// twelve named people per world who live their own lives on the world's clock (one
// step per reading hour = one year, inside sects::advance through agenda::advanceWorld,
// so every reading habit gets the same history and it works without a date). Pure
// rules: no I/O, no UI, no text. Nothing here touches your Qi.
//
// Who: made when the world is founded (or on the first visit of a life after the
// update), the rest regenerated from the world seed and the slot's generation.
// Ages 16-70, mostly Body Tempering to Foundation Establishment, one Core
// Formation elder; about half in a sect. When someone dies a newcomer takes the
// slot 3-10 years later (16-24 years old), one in three of them a disciple of
// someone already in the cast.
//
// Lives (agreed numbers):
//   pace       25-90% of a median reader (by talent; Power ambition +13 points);
//              doubled in seclusion
//   ceiling    fixed at birth: 40% Qi Condensation or Foundation, 40% Core
//              Formation or Nascent Soul, 15% the two realms above, 5% higher
//   breakthroughs  75% - 5% per realm (at least 35%); a failure costs a quarter
//              of the realm's progress, and one in three goes into seclusion
//   death      of old age at the realm's lifespan (as yours), or 1 in 20 a year
//              while their sect fights a war
//   events     about one every 30 years each: join or leave a sect, rise in it,
//              take an apprentice, marry, swear brotherhood, start a feud, fall
//              out, go into seclusion (weighted by ambition)
// Links: one each, to another cast member, your rival (kLinkRival) or, later,
// you (kLinkYou, reserved). A link names what the other is to this person:
// Master (the other teaches them), Disciple, Sibling (sworn), Spouse, Enemy.
//
// You: one road meeting in three in a region where a living, unsecluded cast
// member is, is that person (exploration[7] = 0x30 + i), at their own level,
// never scaled to you. They remember how you parted. Opinion -5..+5: a trade
// or a talk +1, a challenge you win -1. Friendly (+2 or more): 10% off their
// trades and a free Talk. Bitter (-2 or less): they challenge you. They never
// become nemeses and never die by your hand (that waits for karma).
//
// Save v9 (160 bytes, per life): header 16 (0: flags 0x01 founded; 1-15 spare),
// then 12 people x 12 bytes:
//   0  flags: 0x01 alive, 0x02 met, 0x04 in seclusion, 0x08 dead (the record
//      stays until a newcomer takes the slot)
//   1  realm (bits 0-3) | ceiling realm (4-7)
//   2-3 alive: progress in the realm, tenths of an hour; dead: the year they died
//   4-5 birth year + 1000 (the age is the world year minus it)
//   6  sect + 1 (bits 0-2, 0 none) | rank (3-4, 0 outer .. 3 elder) | how they died (5-7)
//   7  temperament (bits 0-1, explore::Temperament) | ambition (2-3) | pace step (4-7, 0-15)
//   8  link type (bits 0-2) | link target (3-7: 0-11 a person, 12 the rival, 13 you, 31 none)
//   9  opinion + 5 (bits 0-3) | how you parted (4-5) | meetings (6-7, 0-3)
//   10 region (bits 0-3) | name variant (4-6) | gender (7: 0 male, 1 female)
//   11 generation (how many people the slot has held)
namespace cultivation {
namespace cast {

constexpr uint8_t kPeople = 12;
constexpr uint8_t kHeader = 16;
constexpr uint8_t kPersonSize = 12;
static_assert(kHeader + kPeople * kPersonSize <= sizeof(GameState::cast), "cast block");

constexpr uint8_t kLinkRival = 12;
constexpr uint8_t kLinkYou = 13;  // reserved for a spouse of your own (to be designed)
constexpr uint8_t kLinkNone = 31;
constexpr uint8_t kSlotBase = 0x30;  // exploration[7]: the waiting meeting is cast member N
constexpr uint8_t kMeetOneIn = 3;
constexpr uint8_t kEventOneIn = 30;
constexpr uint8_t kWarDeathOneIn = 20;
constexpr uint8_t kSeclusionEndOneIn = 3;
constexpr uint8_t kMoveOneIn = 15;
constexpr uint8_t kPaceMin = 25;
constexpr uint8_t kPaceMax = 90;
constexpr int8_t kBitter = -2;
constexpr int8_t kFriendly = 2;
constexpr uint8_t kFriendDiscountPercent = 10;

enum class Link : uint8_t { None = 0, Master, Disciple, Sibling, Spouse, Enemy, Count };
enum class Ambition : uint8_t { Power = 0, Standing, Wealth, Family };
enum class Cause : uint8_t { None = 0, OldAge, War };
enum class Parting : uint8_t { None = 0, YouWon, YouLost, Friendly };

struct Person {
  bool used = false;  // the slot holds someone (alive or a dead record)
  bool alive = false;
  bool met = false;
  bool secluded = false;
  uint8_t realm = 0;
  uint8_t stage = 0;
  uint8_t ceiling = 0;
  uint16_t progress = 0;  // tenths of an hour in the realm
  uint16_t age = 0;       // now, or at death
  int8_t sect = -1;
  uint8_t rank = 0;
  Cause cause = Cause::None;
  explore::Temperament temper = explore::Temperament::Cautious;
  Ambition ambition = Ambition::Power;
  uint8_t pace = 50;  // percent of a median reader
  Link link = Link::None;
  uint8_t linkTo = kLinkNone;
  int8_t opinion = 0;
  Parting parting = Parting::None;
  uint8_t meets = 0;
  uint8_t region = 0;
  uint8_t generation = 0;
  uint32_t seed = 0;  // names and looks (ExploreText)
};
bool person(const GameState& s, uint8_t i, Person& out);
// Their name ("Lin Xueyan"), from the player's name generator: unique in the cast and never yours.
void name(const GameState& s, uint8_t i, char* out, size_t len);
bool founded(const GameState& s);
uint8_t metCount(const GameState& s);  // alive or dead, still in their slot
int level(const Person& p);            // as the rival's: 12 per realm + the stage's
uint16_t lifespan(uint8_t realm);

// --- The world's clock -------------------------------------------------------------------

enum class EventKind : uint8_t {
  Arrived,       // a newcomer (other: their master, if any)
  Breakthrough,  // realm
  Failed,        // a breakthrough failed (realm they tried for)
  Seclusion,
  JoinedSect,  // sect
  LeftSect,    // sect
  Rose,        // rank in sect
  Apprentice,  // took `other` as their disciple
  Married,     // other
  Sworn,       // other
  Feud,        // other
  FellOut,     // other
  DiedOld,
  DiedWar,  // region
  Count
};
struct Event {
  EventKind kind = EventKind::Arrived;
  uint8_t who = 0;
  uint8_t other = kLinkNone;
  uint8_t realm = 0;
  int8_t sect = -1;
  uint8_t rank = 0;
  uint8_t region = 0;
  uint16_t year = 0;
  uint16_t age = 0;
};
constexpr uint8_t kMaxEvents = 10;
struct Steps {
  uint8_t count = 0;
  uint16_t dropped = 0;
  Event events[kMaxEvents];
};

// Founds the cast on the first call of a life, then one year. Called from the world's hook.
void stepYear(GameState& s, uint16_t year, Steps& out);
// Named news: someone you have met, or linked to someone you met or to your rival.
bool newsworthy(const GameState& s, const Event& e);

// --- You ----------------------------------------------------------------------------------

// The person you meet on this step in `region` (one in three when someone lives there), or -1.
int meetingIn(const GameState& s, uint8_t region, uint32_t roll);
// A meeting with them ended (MeetOutcome): met, memory, opinion.
// `challenge`: the bout was a challenge (a won challenge lowers their opinion; a spar does not).
void met(GameState& s, uint8_t i, explore::MeetOutcome outcome, bool challenge);
bool friendly(const Person& p);
bool bitter(const Person& p);

// For the tests and the simulator.
void setOpinion(GameState& s, uint8_t i, int8_t v);

}  // namespace cast
}  // namespace cultivation
