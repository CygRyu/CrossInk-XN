#pragma once

#include <cstdint>

#include "GameState.h"
#include "Rivals.h"
#include "Sects.h"

// v0.3.4 "The rival's agenda" (designed and agreed with the user in session 15,
// HANDOFF 14o): the rival pursues one goal at a time on the world's clock (one
// step per reading hour, inside sects::advance, so it works without a date and
// gives every reading habit the same history). Pure rules: no I/O, no UI, no text.
// Nothing here touches your Qi. Only the duel puts your stones at stake.
//
// Goals, picked by the rival's temperament and the world's situation:
//   Tournament   a Tournament condition is on in a region the rival can reach:
//                they enter; you meet them more often there (80% of meetings);
//                when it ends they win it or not (30% + 10% per realm they are
//                past the region's, Driven +10, 15-75%).
//   Inheritance  an Inheritance condition is on in a region they can reach:
//                they race the sects for it (1 in 5 each reading hour after the
//                sects' own step). Taken: their talent 5% faster for the rest of
//                the life (never better than 80%), once a life. Taken by you
//                first: their opinion of you -2.
//   War          their sect is a side of the sect war: they fight for it. Each
//                of their side you beat: opinion -1; each of the other side: +1.
//                Their side wins (you decided it, or by the sects' strength when
//                the war runs out) or loses.
//   Rise         any time: at the end, a promotion when their realm and goals
//                won allow it: inner disciple (realm 1), core (realm 3, 2 won),
//                elder (realm 5, 4 won). Shown on the rival card.
//   Duel         they are bitter about you (opinion -2 or lower) and know you: a
//                challenge letter; they wait in one of your opened regions for 4
//                reading hours. Step on that region's map to meet them. The
//                winner takes 30 x (your realm + 1) stones (a lost duel costs
//                HP as a challenge does). Refused or ignored: opinion -1.
//   Closed door  at the last stage of their realm: 2-3 reading hours unseen
//                (not met on the road), then out a stage ahead (their next realm).
//   Hunt         you have a nemesis: they hunt one; at the end they beat it (it
//                gains a scar and rests) or lose, by levels as in a fight.
// After a goal ends the next starts in 4-8 reading hours; a goal lasts 3-6
// (closed door 2-3, a duel 4); a goal tied to a condition ends with it.
//
// Opinion of you, -5..+5 (0 to start): down when you beat them, take an
// inheritance from them, beat their side in a war, ignore a duel; up when you
// help their sect (beat the other side, end a camp, a tide or a cult their
// sect's story made) or lose to them and come back (your next bout with them).
// Friendly (+2 or more): they offer trades as well as spars. Bitter (-2 or
// less): no trades, and duels. The tone of their road lines follows it.
//
// Save: GameState::rival (11 bytes, unused since milestone 9; per life):
//   0  flags: 0x01 started, 0x02 their talent came from an inheritance,
//      0x04 you lost to them last (a comeback raises opinion), 0x08 their side
//      won the war of the goal, 0x10 their side lost it
//   1  goal (Goal, 0 = between goals)
//   2  the goal's region
//   3-4 the year (reading hour of this life) the goal ends, or the next starts
//   5  opinion + 5 (0-10; meaningful once started)
//   6  sect rank (0 outer, 1 inner, 2 core, 3 elder)
//   7  goals won
//   8  the hunted nemesis; for a tournament or an inheritance the condition's
//      sect, for a war the enemy side (0xFF none)
//   9  the last goal (for the card and the rumor after it ended)
//   10 the last result: 1 won, 2 lost, 3 promoted, 4 a duel refused or ignored, 0 nothing
//   flags 0x20: you took the inheritance they raced for (told on the next step)
namespace cultivation {
namespace agenda {

enum class Goal : uint8_t { None = 0, Tournament, Inheritance, War, Rise, Duel, ClosedDoor, Hunt, Count };
enum class Rank : uint8_t { Outer = 0, Inner, Core, Elder, Count };

constexpr uint16_t kGapMin = 4;  // reading hours between goals (agreed)
constexpr uint16_t kGapMax = 8;
constexpr uint16_t kGoalMin = 3;  // a goal lasts 3-6 reading hours (agreed)
constexpr uint16_t kGoalMax = 6;
constexpr uint16_t kClosedMin = 2;  // closed-door cultivation (agreed)
constexpr uint16_t kClosedMax = 3;
constexpr uint16_t kDuelHours = 4;  // the duel waits 4 reading hours (agreed)
constexpr uint8_t kDuelStonesPerRealm = 30;
constexpr int8_t kOpinionMin = -5;
constexpr int8_t kOpinionMax = 5;
constexpr int8_t kBitter = -2;
constexpr int8_t kFriendly = 2;
constexpr uint8_t kTalentFloor = 80;            // the rival's talent never better than this
constexpr uint8_t kInheritanceTakeOneIn = 5;    // each reading hour of the race
constexpr uint8_t kTournamentMeetPercent = 80;  // the rival's share of meetings in the tournament's region

struct State {
  bool started = false;
  Goal goal = Goal::None;
  uint8_t region = 0;
  uint16_t until = 0;  // the year the goal ends (or the next one starts)
  int8_t opinion = 0;
  Rank rank = Rank::Outer;
  uint8_t won = 0;
  uint8_t nemesis = 0;
  Goal last = Goal::None;
  uint8_t lastResult = 0;
  bool inheritedTalent = false;
};
State state(const GameState& s);
int8_t opinion(const GameState& s);
bool bitter(const GameState& s);
bool friendly(const GameState& s);
// Reading hours left in the goal (0 between goals).
uint16_t hoursLeft(const GameState& s);

// --- The world's clock ------------------------------------------------------------------

enum class EventKind : uint8_t {
  Started,     // a goal began (region, nemesis)
  Won,         // the goal ended in the rival's favour
  Lost,        // ... or not
  Promoted,    // Rise ended with a promotion (rank)
  Served,      // Rise ended without one
  CameOut,     // closed-door cultivation ended (they are a stage ahead)
  DuelIgnored  // the letter ran out unanswered
};
struct Event {
  EventKind kind = EventKind::Started;
  Goal goal = Goal::None;
  uint8_t region = 0;
  uint8_t nemesis = 0;
  int8_t sect = -1;  // the condition's sect (tournament, inheritance), the enemy side (war)
  Rank rank = Rank::Outer;
  uint16_t year = 0;
  bool byYou = false;  // an inheritance race lost because you took it
};
constexpr uint8_t kMaxEvents = 8;
struct Steps {
  uint8_t count = 0;
  uint16_t dropped = 0;
  Event events[kMaxEvents];
};

// The living world and the agenda together: sects::advance with the agenda's
// step after each world year. Call instead of sects::advance once per visit.
sects::Steps advanceWorld(GameState& s, Steps& agendaOut);
// One year of the agenda (after the sects' step of that year). For the tests.
void stepYear(GameState& s, uint16_t year, Steps& out);

// --- What you do --------------------------------------------------------------------------

// A bout with the rival (road spar, ranking, duel) is over.
void boutWithRival(GameState& s, bool youWon);
// You beat a cultivator of `sect` (sects::personSect) in `region`, and the
// world changed by `w` (from sects::cultivatorBeaten, banditBeaten, beastBeaten).
void sawWin(GameState& s, uint8_t region, int8_t sect, const sects::Influence& w);
// You won a ruin or cave search; `w` from sects::searchWon.
void sawSearch(GameState& s, const sects::Influence& w);

// The rival is out of sight (closed-door cultivation): no road meetings.
bool unseen(const GameState& s);
// The rival's share of road meetings in `region` (percent).
uint8_t meetPercent(const GameState& s, uint8_t region);
// The duel waiting in `region` (true while the letter stands there).
bool duelWaitingIn(const GameState& s, uint8_t region);
// The duel was fought (won by you or not) or refused on the road.
struct DuelResult {
  bool done = false;
  bool won = false;
  uint32_t stones = 0;  // what you took, or what you paid
};
DuelResult settleDuel(GameState& s, bool fought, bool youWon);
uint32_t duelStakes(const GameState& s);

// A rank's or goal's position in the text tables.
constexpr uint8_t index(const Goal g) { return static_cast<uint8_t>(g); }
constexpr uint8_t index(const Rank r) { return static_cast<uint8_t>(r); }

}  // namespace agenda
}  // namespace cultivation
