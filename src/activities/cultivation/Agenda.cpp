#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Achievements.h"
#include "Agenda.h"
#include "Exploration.h"
#include "Rivals.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace agenda {
namespace {

constexpr uint32_t kKindAgenda = 139;  // contentSeed kind (v0.3.4): the agenda's picks and rolls

// GameState::rival, 11 bytes (layout in Agenda.h).
constexpr uint8_t kFlags = 0;
constexpr uint8_t kGoal = 1;
constexpr uint8_t kRegion = 2;
constexpr uint8_t kUntil = 3;
constexpr uint8_t kOpinion = 5;
constexpr uint8_t kRank = 6;
constexpr uint8_t kWon = 7;
constexpr uint8_t kNemesis = 8;
constexpr uint8_t kLast = 9;
constexpr uint8_t kLastResult = 10;
constexpr uint8_t kStarted = 0x01;
constexpr uint8_t kInherited = 0x02;
constexpr uint8_t kLostLast = 0x04;
constexpr uint8_t kWarWon = 0x08;
constexpr uint8_t kWarLost = 0x10;
constexpr uint8_t kYouTookIt = 0x20;  // you took the inheritance they raced for (news on the next step)
constexpr uint8_t kOpinionZero = 5;
static_assert(kLastResult < sizeof(GameState::rival), "rival block");

constexpr uint8_t kResultWon = 1;
constexpr uint8_t kResultLost = 2;
constexpr uint8_t kResultPromoted = 3;
constexpr uint8_t kResultIgnored = 4;  // a duel refused or ignored: not a goal won

constexpr uint8_t kGoals = static_cast<uint8_t>(Goal::Count);

// How much each temper wants each goal, when the goal is open to them (Goal 1..7:
// tournament, inheritance, war, rise, duel, closed door, hunt).
constexpr uint8_t kWeights[static_cast<int>(rivals::Temper::Count)][kGoals - 1] = {
    {3, 4, 5, 2, 3, 2, 1},  // idle
    {5, 5, 6, 4, 4, 3, 2},  // steady
    {7, 6, 7, 3, 6, 4, 3},  // driven
    {5, 7, 5, 2, 5, 4, 2},  // erratic
    {5, 5, 6, 3, 4, 4, 2},  // late bloomer
};

// The realm and goals won a rank needs (Inner, Core, Elder).
constexpr uint8_t kRankRealm[static_cast<int>(Rank::Count)] = {0, 1, 3, 5};
constexpr uint8_t kRankWon[static_cast<int>(Rank::Count)] = {0, 0, 2, 4};

uint8_t* ag(GameState& s) { return s.rival.data(); }
const uint8_t* ag(const GameState& s) { return s.rival.data(); }

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t* p, const uint32_t v) {
  const uint32_t c = v > 0xFFFFu ? 0xFFFFu : v;
  p[0] = static_cast<uint8_t>(c);
  p[1] = static_cast<uint8_t>(c >> 8);
}

uint16_t yearNow(const GameState& s) {
  const uint32_t y = s.progress.gameReadingSeconds / 3600u;
  return static_cast<uint16_t>(y > 0xFFFFu ? 0xFFFFu : y);
}

// The opinion byte means something once the agenda has started (an all-zero block is "not yet").
void ensureStarted(GameState& s) {
  uint8_t* a = ag(s);
  if ((a[kFlags] & kStarted) != 0) return;
  for (uint8_t i = 0; i < sizeof(GameState::rival); ++i) a[i] = 0;
  a[kFlags] = kStarted;
  a[kOpinion] = kOpinionZero;
}

void moveOpinion(GameState& s, const int delta) {
  ensureStarted(s);
  int v = static_cast<int>(ag(s)[kOpinion]) - kOpinionZero + delta;
  if (v < kOpinionMin) v = kOpinionMin;
  if (v > kOpinionMax) v = kOpinionMax;
  ag(s)[kOpinion] = static_cast<uint8_t>(v + kOpinionZero);
}

int8_t rivalSect(const GameState& s) { return sects::personSect(s, sects::kNoRegion, rivals::rivalSeed(s)); }

uint8_t incByte(const uint8_t v) { return v < 0xFF ? static_cast<uint8_t>(v + 1) : v; }

void push(Steps& out, const Event& e) {
  if (out.count < kMaxEvents) {
    out.events[out.count++] = e;
  } else if (out.dropped < 0xFFFFu) {
    ++out.dropped;
  }
}

bool conditionGoal(const Goal g) { return g == Goal::Tournament || g == Goal::Inheritance || g == Goal::War; }

Event eventFor(const GameState& s, const EventKind kind, const uint16_t year) {
  const uint8_t* a = ag(s);
  Event e;
  e.kind = kind;
  e.goal = static_cast<Goal>(a[kGoal] < kGoals ? a[kGoal] : 0);
  e.region = a[kRegion];
  e.nemesis = a[kNemesis];
  e.rank = static_cast<Rank>(a[kRank] < static_cast<uint8_t>(Rank::Count) ? a[kRank] : 0);
  e.year = year;
  e.byYou = (a[kFlags] & kYouTookIt) != 0;
  // Byte 8 holds the sect of a goal tied to a condition (stored at the start, as the condition ends first).
  if (conditionGoal(e.goal)) e.sect = a[kNemesis] < sects::kSects ? static_cast<int8_t>(a[kNemesis]) : -1;
  return e;
}

// The goal is over: remember it for the card and the rumor, and set when the next one starts.
void finish(GameState& s, const uint16_t year, const uint8_t result, Rng& rng) {
  uint8_t* a = ag(s);
  a[kLast] = a[kGoal];
  a[kLastResult] = result;
  if (result == kResultWon) a[kWon] = incByte(a[kWon]);
  a[kGoal] = 0;
  a[kFlags] = static_cast<uint8_t>(a[kFlags] & ~(kWarWon | kWarLost | kYouTookIt));
  put16(a + kUntil, static_cast<uint32_t>(year) + rng.range(kGapMin, kGapMax));
}

bool conditionGoalOn(const GameState& s, const Goal g, uint8_t& region) {
  const sects::Condition c = sects::condition(s, &region);
  switch (g) {
    case Goal::Tournament:
      return c == sects::Condition::Tournament && rivals::reaches(s, region);
    case Goal::Inheritance:
      return c == sects::Condition::Inheritance && rivals::reaches(s, region);
    case Goal::War: {
      if (c != sects::Condition::SectWar) return false;
      const int8_t own = rivalSect(s);
      return own >= 0 && (own == static_cast<int8_t>(sects::conditionSect(s)) || own == sects::conditionOther(s));
    }
    default:
      return false;
  }
}

// A region of yours the rival can reach for a duel (the one you explored last first), or -1.
int duelRegion(const GameState& s, Rng& rng) {
  const int last = rivals::playerRegion(s);
  if (last >= 0 && explore::unlocked(s, static_cast<uint8_t>(last)) && rivals::reaches(s, static_cast<uint8_t>(last))) {
    return last;
  }
  uint8_t options[explore::kRegionCount];
  uint8_t n = 0;
  for (uint8_t r = 0; r < explore::kRegionCount; ++r) {
    if (explore::unlocked(s, r) && rivals::reaches(s, r)) options[n++] = r;
  }
  return n > 0 ? options[rng.below(n)] : -1;
}

bool open(const GameState& s, const Goal g, const rivals::Rival& r) {
  uint8_t region = 0;
  switch (g) {
    case Goal::Tournament:
    case Goal::Inheritance:
    case Goal::War:
      return conditionGoalOn(s, g, region);
    case Goal::Rise:
      return true;
    case Goal::Duel: {
      if (!r.known || opinion(s) > kBitter) return false;
      for (uint8_t k = 0; k < explore::kRegionCount; ++k) {
        if (explore::unlocked(s, k) && rivals::reaches(s, k)) return true;
      }
      return false;
    }
    case Goal::ClosedDoor:
      return !r.hiatus && r.realm + 1 < kMortalRealmCount && r.stage + 1 >= realmInfo(r.realm).stages;
    case Goal::Hunt:
      return rivals::nemesisCount(s) > 0;
    default:
      return false;
  }
}

// Picks among the open goals by weight; `onlyConditions`: a world event cuts the gap short.
Goal pick(const GameState& s, const rivals::Rival& r, Rng& rng, const bool onlyConditions) {
  const uint8_t ti =
      static_cast<uint8_t>(r.temper) < static_cast<uint8_t>(rivals::Temper::Count) ? static_cast<uint8_t>(r.temper) : 1;
  uint32_t weights[kGoals] = {};
  uint32_t total = 0;
  for (uint8_t g = 1; g < kGoals; ++g) {
    const auto goal = static_cast<Goal>(g);
    if (onlyConditions && !conditionGoal(goal)) continue;
    if (!open(s, goal, r)) continue;
    weights[g] = kWeights[ti][g - 1];
    total += weights[g];
  }
  if (total == 0) return Goal::None;
  uint32_t roll = rng.below(total);
  for (uint8_t g = 1; g < kGoals; ++g) {
    if (roll < weights[g]) return static_cast<Goal>(g);
    roll -= weights[g];
  }
  return Goal::None;
}

void begin(GameState& s, const Goal g, const uint16_t year, Rng& rng, Steps& out) {
  uint8_t* a = ag(s);
  a[kGoal] = static_cast<uint8_t>(g);
  a[kRegion] = 0;
  a[kNemesis] = 0;
  a[kFlags] = static_cast<uint8_t>(a[kFlags] & ~(kWarWon | kWarLost | kYouTookIt));
  uint16_t hours = static_cast<uint16_t>(rng.range(kGoalMin, kGoalMax));
  uint8_t region = 0;
  switch (g) {
    case Goal::Tournament:
    case Goal::Inheritance:
    case Goal::War:
      sects::condition(s, &region);
      a[kRegion] = region;
      if (g == Goal::War) {  // the enemy side
        const int8_t own = rivalSect(s);
        const uint8_t owner = sects::conditionSect(s);
        const int8_t enemy = own == static_cast<int8_t>(owner) ? sects::conditionOther(s) : static_cast<int8_t>(owner);
        a[kNemesis] = static_cast<uint8_t>(enemy >= 0 ? enemy : 0xFF);
      } else {
        a[kNemesis] = sects::conditionSect(s);
      }
      // Ends with it (from the step's own year, so a long visit tells the same story as many short ones).
      hours = sects::conditionEndYear(s) > year ? static_cast<uint16_t>(sects::conditionEndYear(s) - year) : 1;
      break;
    case Goal::Duel: {
      const int r = duelRegion(s, rng);
      a[kRegion] = static_cast<uint8_t>(r >= 0 ? r : 0);
      hours = kDuelHours;
      break;
    }
    case Goal::ClosedDoor:
      hours = static_cast<uint16_t>(rng.range(kClosedMin, kClosedMax));
      break;
    case Goal::Hunt: {
      uint8_t options[rivals::kNemesisMax];
      uint8_t n = 0;
      for (uint8_t i = 0; i < rivals::kNemesisMax; ++i) {
        rivals::Nemesis nm;
        if (rivals::nemesis(s, i, nm)) options[n++] = i;
      }
      a[kNemesis] = n > 0 ? options[rng.below(n)] : 0;
      rivals::Nemesis nm;
      if (rivals::nemesis(s, a[kNemesis], nm)) a[kRegion] = nm.region;
      break;
    }
    default:
      break;
  }
  put16(a + kUntil, static_cast<uint32_t>(year) + hours);
  push(out, eventFor(s, EventKind::Started, year));
}

// The tournament's result: 30% + 10% per realm past the region's, Driven +10 (15-75%).
bool tournamentWon(const GameState& s, const rivals::Rival& r, const uint8_t region, Rng& rng) {
  int chance = 30 + 10 * (static_cast<int>(r.realm) - static_cast<int>(explore::unlockRealm(region)));
  if (r.temper == rivals::Temper::Driven) chance += 10;
  if (chance < 15) chance = 15;
  if (chance > 75) chance = 75;
  (void)s;
  return static_cast<int>(rng.below(100)) < chance;
}

// One year of a goal under way. True when it ended this year.
void advanceGoal(GameState& s, const uint16_t year, sects::Steps& world, Rng& rng, Steps& out) {
  uint8_t* a = ag(s);
  const auto g = static_cast<Goal>(a[kGoal] < kGoals ? a[kGoal] : 0);
  const rivals::Rival r = rivals::rival(s);
  const bool timeUp = year >= get16(a + kUntil);
  uint8_t region = 0;
  switch (g) {
    case Goal::Tournament: {
      const bool on = conditionGoalOn(s, g, region) && region == a[kRegion];
      if (on && !timeUp) return;
      const bool won = tournamentWon(s, r, a[kRegion], rng);
      push(out, eventFor(s, won ? EventKind::Won : EventKind::Lost, year));
      finish(s, year, won ? kResultWon : kResultLost, rng);
      return;
    }
    case Goal::Inheritance: {
      const bool on = conditionGoalOn(s, g, region) && region == a[kRegion];
      const bool take = rng.below(kInheritanceTakeOneIn) == 0;  // rolled every year of the race
      if (on && take) {
        Event e = eventFor(s, EventKind::Won, year);
        sects::takenByRival(s, year, world);
        push(out, e);
        if ((a[kFlags] & kInherited) == 0) {  // once a life: their talent 5% faster, never past 80
          const uint32_t t = r.talentPercent - (r.talentPercent * 5u + 50u) / 100u;
          rivals::setTalent(s, static_cast<uint8_t>(t < kTalentFloor ? kTalentFloor : t));
          a[kFlags] = static_cast<uint8_t>(a[kFlags] | kInherited);
        }
        finish(s, year, kResultWon, rng);
        return;
      }
      if (on && !timeUp) return;
      push(out, eventFor(s, EventKind::Lost, year));
      finish(s, year, kResultLost, rng);
      return;
    }
    case Goal::War: {
      const bool on = conditionGoalOn(s, g, region) && region == a[kRegion];
      if (on && !timeUp) return;
      bool won = (a[kFlags] & kWarWon) != 0;
      if ((a[kFlags] & (kWarWon | kWarLost)) == 0) {
        // The war ran out: their side comes out ahead by the two sects' strength.
        const int8_t own = rivalSect(s);
        const int8_t enemy = a[kNemesis] < sects::kSects ? static_cast<int8_t>(a[kNemesis]) : -1;
        const uint32_t mine = own >= 0 ? sects::sect(s, static_cast<uint8_t>(own)).strength : 5u;
        const uint32_t theirs = enemy >= 0 ? sects::sect(s, static_cast<uint8_t>(enemy)).strength : 5u;
        won = rng.below(mine + theirs) < mine;
      }
      push(out, eventFor(s, won ? EventKind::Won : EventKind::Lost, year));
      finish(s, year, won ? kResultWon : kResultLost, rng);
      return;
    }
    case Goal::Rise: {
      if (!timeUp) return;
      const uint8_t rank = a[kRank];
      const uint8_t next = static_cast<uint8_t>(rank + 1);
      if (next < static_cast<uint8_t>(Rank::Count) && r.realm >= kRankRealm[next] && a[kWon] >= kRankWon[next]) {
        a[kRank] = next;
        push(out, eventFor(s, EventKind::Promoted, year));
        finish(s, year, kResultPromoted, rng);
      } else {
        push(out, eventFor(s, EventKind::Served, year));
        finish(s, year, 0, rng);
      }
      return;
    }
    case Goal::Duel:
      if (!timeUp) return;
      moveOpinion(s, -1);  // ignored
      push(out, eventFor(s, EventKind::DuelIgnored, year));
      finish(s, year, kResultIgnored, rng);
      return;
    case Goal::ClosedDoor:
      if (!timeUp) return;
      rivals::stepAhead(s);
      push(out, eventFor(s, EventKind::CameOut, year));
      finish(s, year, 0, rng);
      return;
    case Goal::Hunt: {
      if (!timeUp) return;
      rivals::Nemesis nm;
      if (!rivals::nemesis(s, a[kNemesis], nm)) {
        finish(s, year, 0, rng);
        return;
      }
      const int d = rivals::rivalLevel(s) - rivals::fightLevel(s, nm, -1);
      const bool won = rng.below(100) < explore::winChance(d);
      if (won) rivals::scarredByRival(s, a[kNemesis], static_cast<uint32_t>(year) * 3600u);
      push(out, eventFor(s, won ? EventKind::Won : EventKind::Lost, year));
      finish(s, year, won ? kResultWon : kResultLost, rng);
      return;
    }
    default:
      a[kGoal] = 0;
      return;
  }
}

void stepWith(GameState& s, const uint16_t year, sects::Steps& world, Steps& out) {
  const rivals::Rival r = rivals::rival(s);
  if (!r.exists) return;
  ensureStarted(s);
  uint8_t* a = ag(s);
  Rng rng(contentSeed(worldSeedOf(s), kKindAgenda, year));
  if (a[kGoal] != 0) {
    advanceGoal(s, year, world, rng, out);
    if (a[kGoal] != 0) return;
  }
  const uint16_t until = get16(a + kUntil);
  if (until == 0) {  // the first step of the agenda in this life
    put16(a + kUntil, static_cast<uint32_t>(year) + rng.range(kGapMin, kGapMax));
    return;
  }
  // Between goals: a goal starts when the gap is over; a world event the rival
  // can join (a tournament, an inheritance, their sect's war) cuts it short.
  const bool due = year >= until;
  const rivals::Rival now = rivals::rival(s);
  const Goal g = pick(s, now, rng, !due);
  if (g != Goal::None) begin(s, g, year, rng, out);
}

struct HookOut {
  Steps* agenda;
  cast::Steps* cast;
};

void hook(GameState& s, const uint16_t year, sects::Steps& world, void* ctx) {
  auto* out = static_cast<HookOut*>(ctx);
  cast::stepYear(s, year, *out->cast);  // v0.3.5
  stepWith(s, year, world, *out->agenda);
}

}  // namespace

State state(const GameState& s) {
  State out;
  const uint8_t* a = ag(s);
  out.started = (a[kFlags] & kStarted) != 0;
  if (!out.started) return out;
  out.goal = static_cast<Goal>(a[kGoal] < kGoals ? a[kGoal] : 0);
  out.region = a[kRegion];
  out.until = get16(a + kUntil);
  out.opinion = opinion(s);
  out.rank = static_cast<Rank>(a[kRank] < static_cast<uint8_t>(Rank::Count) ? a[kRank] : 0);
  out.won = a[kWon];
  out.nemesis = a[kNemesis];
  out.last = static_cast<Goal>(a[kLast] < kGoals ? a[kLast] : 0);
  out.lastResult = a[kLastResult];
  out.inheritedTalent = (a[kFlags] & kInherited) != 0;
  return out;
}

int8_t opinion(const GameState& s) {
  const uint8_t* a = ag(s);
  if ((a[kFlags] & kStarted) == 0) return 0;
  const int v = static_cast<int>(a[kOpinion]) - kOpinionZero;
  return static_cast<int8_t>(v < kOpinionMin ? kOpinionMin : v > kOpinionMax ? kOpinionMax : v);
}

bool bitter(const GameState& s) { return opinion(s) <= kBitter; }
bool friendly(const GameState& s) { return opinion(s) >= kFriendly; }

uint16_t hoursLeft(const GameState& s) {
  const State st = state(s);
  if (st.goal == Goal::None) return 0;
  const uint16_t now = yearNow(s);
  return st.until > now ? static_cast<uint16_t>(st.until - now) : 0;
}

sects::Steps advanceWorld(GameState& s, Steps& agendaOut, cast::Steps& castOut) {
  agendaOut = Steps{};
  castOut = cast::Steps{};
  HookOut out{&agendaOut, &castOut};
  return sects::advance(s, &hook, &out);
}

void stepYear(GameState& s, const uint16_t year, Steps& out) {
  sects::Steps world;
  stepWith(s, year, world, out);
}

void boutWithRival(GameState& s, const bool youWon) {
  if (!rivals::rival(s).exists) return;
  ensureStarted(s);
  uint8_t* a = ag(s);
  if ((a[kFlags] & kLostLast) != 0) {  // you came back after they beat you
    moveOpinion(s, 1);
    a[kFlags] = static_cast<uint8_t>(a[kFlags] & ~kLostLast);
  }
  if (youWon) {
    moveOpinion(s, -1);
  } else {
    a[kFlags] = static_cast<uint8_t>(a[kFlags] | kLostLast);
  }
}

void sawWin(GameState& s, const uint8_t region, const int8_t sect, const sects::Influence& w) {
  if (!rivals::rival(s).exists) return;
  const int8_t own = rivalSect(s);
  if (own < 0) return;
  ensureStarted(s);
  uint8_t* a = ag(s);
  const bool atWar = a[kGoal] == static_cast<uint8_t>(Goal::War) && region == a[kRegion];
  if (atWar && sect >= 0) {
    // Their side or the other: the war's two sides are the only people in its region.
    moveOpinion(s, sect == own ? -1 : 1);
  }
  for (uint8_t i = 0; i < w.count; ++i) {
    const sects::Event& e = w.events[i];
    switch (e.kind) {
      case sects::EventKind::WarDecided:
        if (atWar) {
          a[kFlags] = static_cast<uint8_t>(a[kFlags] | (e.sect == own ? kWarWon : e.other == own ? kWarLost : 0));
        }
        break;
      case sects::EventKind::CampBroken:
      case sects::EventKind::TideBroken:
      case sects::EventKind::CultBroken:
        if (e.sect == own) moveOpinion(s, 1);  // you helped their sect
        break;
      default:
        break;
    }
  }
}

void sawSearch(GameState& s, const sects::Influence& w) {
  if (!rivals::rival(s).exists) return;
  for (uint8_t i = 0; i < w.count; ++i) {
    if (w.events[i].kind != sects::EventKind::YouTookInheritance) continue;
    ensureStarted(s);
    uint8_t* a = ag(s);
    if (a[kGoal] != static_cast<uint8_t>(Goal::Inheritance)) return;
    moveOpinion(s, -2);  // you took it from under their nose: bitter
    a[kFlags] = static_cast<uint8_t>(a[kFlags] | kYouTookIt);
    // The race is over for them; the next visit's world step tells of it (the condition is gone).
    return;
  }
}

bool unseen(const GameState& s) { return state(s).goal == Goal::ClosedDoor; }

uint8_t meetPercent(const GameState& s, const uint8_t region) {
  const State st = state(s);
  if (st.goal == Goal::Tournament && st.region == region) return kTournamentMeetPercent;
  return rivals::kRivalMeetPercent;
}

bool duelWaitingIn(const GameState& s, const uint8_t region) {
  const State st = state(s);
  return st.goal == Goal::Duel && st.region == region && hoursLeft(s) > 0 && rivals::rival(s).known;
}

uint32_t duelStakes(const GameState& s) { return kDuelStonesPerRealm * (static_cast<uint32_t>(s.progress.realm) + 1u); }

DuelResult settleDuel(GameState& s, const bool fought, const bool youWon) {
  DuelResult res;
  uint8_t* a = ag(s);
  if (a[kGoal] != static_cast<uint8_t>(Goal::Duel)) return res;
  res.done = true;
  Rng rng(contentSeed(worldSeedOf(s), kKindAgenda, mix32(yearNow(s), 0x4455454Cu)));
  if (!fought) {
    moveOpinion(s, -1);  // refused
    finish(s, yearNow(s), kResultIgnored, rng);
    return res;
  }
  const uint32_t stakes = duelStakes(s);
  res.won = youWon;
  achievements::earn(s, youWon ? achievements::Id::WinDuel : achievements::Id::LoseDuel);  // v0.3.8
  if (youWon) {
    res.stones = stakes;
    s.progress.stones += stakes;
    finish(s, yearNow(s), kResultLost, rng);
  } else {
    res.stones = stakes < s.progress.stones ? stakes : s.progress.stones;
    s.progress.stones -= res.stones;
    finish(s, yearNow(s), kResultWon, rng);
  }
  return res;
}

}  // namespace agenda
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
