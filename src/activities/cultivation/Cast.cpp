#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstring>

#include "Achievements.h"
#include "Cast.h"
#include "Rivals.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace cast {
namespace {

constexpr uint32_t kKindCast = 140;  // contentSeed kind (v0.3.5): who they are, and every yearly roll

constexpr uint8_t kFounded = 0x01;
constexpr uint8_t kAlive = 0x01;
constexpr uint8_t kMet = 0x02;
constexpr uint8_t kSecluded = 0x04;
constexpr uint8_t kDead = 0x08;
constexpr uint16_t kBirthOffset = 1000;

uint8_t* pb(GameState& s, const uint8_t i) { return s.cast.data() + kHeader + kPersonSize * i; }
const uint8_t* pb(const GameState& s, const uint8_t i) { return s.cast.data() + kHeader + kPersonSize * i; }
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t* p, const uint32_t v) {
  const uint32_t c = v > 0xFFFFu ? 0xFFFFu : v;
  p[0] = static_cast<uint8_t>(c);
  p[1] = static_cast<uint8_t>(c >> 8);
}

uint16_t need(const uint8_t realm) { return rivals::kRealmDeciHours[realm < kMortalRealmCount ? realm : 0]; }
uint8_t paceOf(const uint8_t step) { return static_cast<uint8_t>(kPaceMin + (step * (kPaceMax - kPaceMin)) / 15); }
// The sect rank a realm allows: outer, inner (Foundation), core (Core Formation), elder (Nascent Soul).
uint8_t rankFor(const uint8_t realm) { return realm >= 4 ? 3 : realm >= 3 ? 2 : realm >= 2 ? 1 : 0; }

uint32_t seedOf(const GameState& s, const uint8_t i) {
  return mix32(contentSeed(worldSeedOf(s), kKindCast, i), 0x43415354u + 0x9E3779B9u * pb(s, i)[11]);
}

uint16_t ageAt(const uint8_t* b, const uint16_t year) {
  const int a = static_cast<int>(year) + kBirthOffset - static_cast<int>(get16(b + 4));
  return static_cast<uint16_t>(a < 0 ? 0 : a);
}

void setLink(uint8_t* b, const Link l, const uint8_t to) {
  b[8] = static_cast<uint8_t>(static_cast<uint8_t>(l) | (to << 3));
}
Link linkOf(const uint8_t* b) {
  const uint8_t l = b[8] & 0x07;
  return l < static_cast<uint8_t>(Link::Count) ? static_cast<Link>(l) : Link::None;
}
uint8_t linkTo(const uint8_t* b) { return static_cast<uint8_t>(b[8] >> 3); }
bool isFree(const uint8_t* b) { return linkOf(b) == Link::None; }
int8_t sectOf(const uint8_t* b) { return static_cast<int8_t>((b[6] & 0x07) - 1); }
void setSect(uint8_t* b, const int8_t sect, const uint8_t rank) {
  b[6] = static_cast<uint8_t>((b[6] & 0xE0) | ((sect + 1) & 0x07) | ((rank & 0x03) << 3));
}
uint8_t realmOf(const uint8_t* b) { return b[1] & 0x0F; }
uint8_t ceilingOf(const uint8_t* b) { return static_cast<uint8_t>(b[1] >> 4); }
Ambition ambitionOf(const uint8_t* b) { return static_cast<Ambition>((b[7] >> 2) & 0x03); }

void push(Steps& out, const Event& e) {
  if (out.count < kMaxEvents) {
    out.events[out.count++] = e;
  } else if (out.dropped < 0xFFFFu) {
    ++out.dropped;
  }
}

Event eventOf(const GameState& s, const EventKind k, const uint8_t i, const uint16_t year) {
  const uint8_t* b = pb(s, i);
  Event e;
  e.kind = k;
  e.who = i;
  e.realm = realmOf(b);
  e.sect = sectOf(b);
  e.rank = static_cast<uint8_t>((b[6] >> 3) & 0x03);
  e.region = b[10] & 0x0F;
  e.year = year;
  e.age = ageAt(b, year);
  return e;
}

// A new person in slot i, `age` years old at `year`.
void make(GameState& s, const uint8_t i, const uint16_t year, const uint16_t age, const bool elder) {
  uint8_t* b = pb(s, i);
  const uint8_t generation = b[11];
  for (uint8_t k = 0; k < kPersonSize; ++k) b[k] = 0;
  b[11] = generation;
  Rng rng(seedOf(s, i));
  // The ceiling (agreed): 40% Qi Condensation or Foundation, 40% Core Formation or Nascent Soul,
  // 15% Soul Transformation or Void Refinement, 5% higher.
  const uint32_t c = rng.below(100);
  uint8_t ceiling = c < 40   ? static_cast<uint8_t>(1 + rng.below(2))
                    : c < 80 ? static_cast<uint8_t>(3 + rng.below(2))
                    : c < 95 ? static_cast<uint8_t>(5 + rng.below(2))
                             : static_cast<uint8_t>(7 + rng.below(3));
  if (elder && ceiling < 4) ceiling = 4;
  const auto temper = static_cast<uint8_t>(rng.below(3));
  const auto ambition = static_cast<uint8_t>(rng.below(4));
  uint8_t step = static_cast<uint8_t>(rng.below(16));
  if (ambition == static_cast<uint8_t>(Ambition::Power) && step < 12) step = static_cast<uint8_t>(step + 3);
  b[7] = static_cast<uint8_t>(temper | (ambition << 2) | (step << 4));
  // Where they stand at that age: the years since 16 at their pace, up to their ceiling.
  uint32_t left = static_cast<uint32_t>(age > kStartAgeYears ? age - kStartAgeYears : 0) * paceOf(step) / 10u;
  uint8_t realm = 0;
  while (realm < ceiling && left >= need(realm)) left -= need(realm++);
  if (elder && realm < 3) {
    realm = 3;
    left = need(3) / 3;
  }
  if (left > need(realm)) left = need(realm);
  b[1] = static_cast<uint8_t>(realm | (ceiling << 4));
  put16(b + 2, left);
  put16(b + 4, static_cast<uint32_t>(year) + kBirthOffset - age);
  // About half in a sect once they cultivate (the elder always).
  if (realm >= 1 && (elder || rng.below(100) < 60))
    setSect(b, static_cast<int8_t>(rng.below(sects::kSects)), rankFor(realm));
  b[9] = 5;  // opinion 0
  b[10] = static_cast<uint8_t>(rng.below(static_cast<uint32_t>(realm < 8 ? realm : 8) + 1u));
  // A name of their own (the player's name generator), different from everyone else in the cast and from you.
  const auto gender = static_cast<uint8_t>(rng.below(2));  // Male, Female
  uint8_t variant = 0;
  for (; variant < 7; ++variant) {
    char mine[kNameMaxBytes + 1];
    suggestName(seedOf(s, i), variant, static_cast<Gender>(gender), mine, sizeof(mine));
    bool taken = strncmp(mine, s.character.name.data(), s.character.name.size()) == 0;
    for (uint8_t k = 0; k < kPeople && !taken; ++k) {
      if (k == i || (pb(s, k)[0] & (kAlive | kDead)) == 0) continue;
      char other[kNameMaxBytes + 1];
      name(s, k, other, sizeof(other));
      taken = strcmp(mine, other) == 0;
    }
    if (!taken) break;
  }
  b[10] = static_cast<uint8_t>(b[10] | (variant << 4) | (gender << 7));
  setLink(b, Link::None, kLinkNone);
  b[0] = kAlive;
}

void found(GameState& s, const uint16_t year) {
  s.cast.fill(0);
  for (uint8_t i = 0; i < kPeople; ++i) {
    Rng rng(seedOf(s, i) ^ 0x41474553u);
    const bool elder = i == 0;
    const uint16_t age =
        elder ? static_cast<uint16_t>(120 + rng.below(100)) : static_cast<uint16_t>(16 + rng.below(55));
    make(s, i, year, age, elder);
  }
  // The elder's disciple, and one pair of sworn siblings to start with.
  setLink(pb(s, 1), Link::Master, 0);
  setLink(pb(s, 0), Link::Disciple, 1);
  setLink(pb(s, 2), Link::Sibling, 3);
  setLink(pb(s, 3), Link::Sibling, 2);
  s.cast[0] = kFounded;
}

// Every link to slot i ends (a newcomer takes it).
void forget(GameState& s, const uint8_t i) {
  for (uint8_t k = 0; k < kPeople; ++k) {
    uint8_t* b = pb(s, k);
    if (linkTo(b) == i && linkOf(b) != Link::None) setLink(b, Link::None, kLinkNone);
  }
}

void die(GameState& s, const uint8_t i, const Cause cause, const uint16_t year, Steps& out) {
  push(out, eventOf(s, cause == Cause::War ? EventKind::DiedWar : EventKind::DiedOld, i, year));
  uint8_t* b = pb(s, i);
  if ((b[0] & kMet) != 0)
    achievements::earn(s, achievements::Id::Mourned);  // v0.3.8: before a newcomer takes the place
  b[0] = static_cast<uint8_t>((b[0] & kMet) | kDead);
  b[6] = static_cast<uint8_t>((b[6] & 0x1F) | (static_cast<uint8_t>(cause) << 5));
  put16(b + 2, year);
}

// Another living person who could take part in an event with i (-1 none).
int partner(const GameState& s, const uint8_t i, Rng& rng, const bool needFree, const bool younger) {
  uint8_t options[kPeople];
  uint8_t n = 0;
  const uint8_t* me = pb(s, i);
  for (uint8_t k = 0; k < kPeople; ++k) {
    if (k == i) continue;
    const uint8_t* b = pb(s, k);
    if ((b[0] & kAlive) == 0) continue;
    if (needFree && !isFree(b)) continue;
    if (younger && realmOf(b) >= realmOf(me)) continue;
    options[n++] = k;
  }
  return n > 0 ? options[rng.below(n)] : -1;
}

void lifeEvent(GameState& s, const uint8_t i, const uint16_t year, Rng& rng, Steps& out) {
  uint8_t* b = pb(s, i);
  const Ambition a = ambitionOf(b);
  const int8_t sect = sectOf(b);
  const uint8_t realm = realmOf(b);
  const uint8_t rank = static_cast<uint8_t>((b[6] >> 3) & 0x03);
  enum Pick : uint8_t { Join, Leave, Rise, Apprentice, Marry, Sworn, Feud, FallOut, Seclude, Picks };
  uint8_t w[Picks] = {};
  if (sect < 0 && realm >= 1) w[Join] = a == Ambition::Standing ? 4 : 2;
  if (sect >= 0 && rank < 3) w[Leave] = a == Ambition::Wealth ? 2 : 1;
  if (sect >= 0 && rank < rankFor(realm)) w[Rise] = a == Ambition::Standing ? 5 : 2;
  if (isFree(b) && realm >= 2) w[Apprentice] = a == Ambition::Family ? 3 : 2;
  if (isFree(b) && ageAt(b, year) >= 18) w[Marry] = a == Ambition::Family ? 4 : 1;
  if (isFree(b)) w[Sworn] = a == Ambition::Power ? 1 : 2;
  if (isFree(b)) w[Feud] = a == Ambition::Power ? 3 : 1;
  if (!isFree(b) && linkOf(b) != Link::Enemy) w[FallOut] = 1;
  if (realm >= 1 && (b[0] & kSecluded) == 0) w[Seclude] = a == Ambition::Power ? 3 : 1;
  uint32_t total = 0;
  for (const uint8_t x : w) total += x;
  if (total == 0) return;
  uint32_t roll = rng.below(total);
  uint8_t pick = 0;
  while (roll >= w[pick]) roll -= w[pick++];
  Event e = eventOf(s, EventKind::Arrived, i, year);
  switch (pick) {
    case Join: {
      const auto to = static_cast<int8_t>(rng.below(sects::kSects));
      setSect(b, to, 0);
      e.kind = EventKind::JoinedSect;
      e.sect = to;
      break;
    }
    case Leave:
      e.kind = EventKind::LeftSect;  // e.sect: the one they left
      setSect(b, -1, 0);
      break;
    case Rise:
      setSect(b, sect, static_cast<uint8_t>(rank + 1));
      e.kind = EventKind::Rose;
      e.rank = static_cast<uint8_t>(rank + 1);
      break;
    case Apprentice: {
      const int k = partner(s, i, rng, true, true);
      if (k < 0) return;
      setLink(b, Link::Disciple, static_cast<uint8_t>(k));
      setLink(pb(s, static_cast<uint8_t>(k)), Link::Master, i);
      e.kind = EventKind::Apprentice;
      e.other = static_cast<uint8_t>(k);
      break;
    }
    case Marry:
    case Sworn: {
      const int k = partner(s, i, rng, true, false);
      if (k < 0) return;
      const Link l = pick == Marry ? Link::Spouse : Link::Sibling;
      setLink(b, l, static_cast<uint8_t>(k));
      setLink(pb(s, static_cast<uint8_t>(k)), l, i);
      e.kind = pick == Marry ? EventKind::Married : EventKind::Sworn;
      e.other = static_cast<uint8_t>(k);
      break;
    }
    case Feud: {
      // Sometimes your rival (one in six); the other side may not even know.
      const int k = rng.below(6) == 0 ? kLinkRival : partner(s, i, rng, false, false);
      if (k < 0) return;
      setLink(b, Link::Enemy, static_cast<uint8_t>(k));
      e.kind = EventKind::Feud;
      e.other = static_cast<uint8_t>(k);
      break;
    }
    case FallOut: {
      // Most quarrels mend: one in two ends the link, one in four for spouses.
      if (rng.below(linkOf(b) == Link::Spouse ? 4 : 2) != 0) return;
      const uint8_t k = linkTo(b);
      setLink(b, Link::None, kLinkNone);
      if (k < kPeople && linkTo(pb(s, k)) == i) setLink(pb(s, k), Link::None, kLinkNone);
      e.kind = EventKind::FellOut;
      e.other = k;
      break;
    }
    default:
      b[0] = static_cast<uint8_t>(b[0] | kSecluded);
      e.kind = EventKind::Seclusion;
      break;
  }
  push(out, e);
}

void stepPerson(GameState& s, const uint8_t i, const uint16_t year, Rng& rng, Steps& out) {
  uint8_t* b = pb(s, i);
  if ((b[0] & kDead) != 0 || (b[0] & kAlive) == 0) {
    // A newcomer 3-10 years after a death (deterministic from the old seed).
    const uint16_t died = get16(b + 2);
    if (year < died + 3u + seedOf(s, i) % 8u) return;
    forget(s, i);
    b[11] = static_cast<uint8_t>(b[11] + 1);
    make(s, i, year, static_cast<uint16_t>(16 + rng.below(9)), false);
    Event e = eventOf(s, EventKind::Arrived, i, year);
    if (rng.below(3) == 0) {  // a disciple of someone already in the cast
      int master = -1;
      for (uint8_t k = 0, tries = 0; tries < kPeople && master < 0;
           ++tries, k = static_cast<uint8_t>(rng.below(kPeople))) {
        const uint8_t* m = pb(s, k);
        if (k != i && (m[0] & kAlive) != 0 && isFree(m) && realmOf(m) >= 2) master = k;
      }
      if (master >= 0) {
        setLink(b, Link::Master, static_cast<uint8_t>(master));
        setLink(pb(s, static_cast<uint8_t>(master)), Link::Disciple, i);
        e.other = static_cast<uint8_t>(master);
        const int8_t ms = sectOf(pb(s, static_cast<uint8_t>(master)));
        if (ms >= 0) setSect(b, ms, 0);
        e.sect = sectOf(b);
      }
    }
    push(out, e);
    return;
  }
  // Cultivate: their pace in tenths of an hour a year (the remainder by chance), twice in seclusion.
  const uint8_t pace = paceOf(static_cast<uint8_t>(b[7] >> 4));
  uint32_t gain = pace / 10u + (rng.below(10) < pace % 10u ? 1u : 0u);
  if ((b[0] & kSecluded) != 0) gain *= 2;
  uint8_t realm = realmOf(b);
  uint32_t progress = get16(b + 2) + gain;
  if (progress >= need(realm)) {
    if (realm >= ceilingOf(b) || realm + 1 >= kMortalRealmCount) {
      progress = need(realm);  // their bottleneck
    } else {
      const int chance = 75 - 5 * realm < 35 ? 35 : 75 - 5 * realm;
      if (static_cast<int>(rng.below(100)) < chance) {
        ++realm;
        progress = 0;
        b[1] = static_cast<uint8_t>(realm | (b[1] & 0xF0));
        push(out, eventOf(s, EventKind::Breakthrough, i, year));
      } else {
        progress = need(realm) * 3u / 4u;
        Event e = eventOf(s, EventKind::Failed, i, year);
        e.realm = static_cast<uint8_t>(realm + 1);
        if (rng.below(3) == 0) b[0] = static_cast<uint8_t>(b[0] | kSecluded);
        push(out, e);
      }
    }
  }
  put16(b + 2, progress);
  if ((b[0] & kSecluded) != 0 && rng.below(kSeclusionEndOneIn) == 0) b[0] = static_cast<uint8_t>(b[0] & ~kSecluded);
  // Old age, and their sect's war.
  if (ageAt(b, year) >= lifespan(realm)) {
    die(s, i, Cause::OldAge, year, out);
    return;
  }
  uint8_t warRegion = 0;
  const int8_t sect = sectOf(b);
  if (sects::condition(s, &warRegion) == sects::Condition::SectWar && sect >= 0 &&
      (sect == static_cast<int8_t>(sects::conditionSect(s)) || sect == sects::conditionOther(s)) &&
      rng.below(kWarDeathOneIn) == 0) {
    b[10] = static_cast<uint8_t>((b[10] & 0xF0) | warRegion);
    die(s, i, Cause::War, year, out);
    return;
  }
  if (rng.below(kMoveOneIn) == 0) {
    b[10] = static_cast<uint8_t>((b[10] & 0xF0) | rng.below((realm < 8 ? realm : 8) + 1u));
  }
  if (rng.below(kEventOneIn) == 0) lifeEvent(s, i, year, rng, out);
}

bool metOrRival(const GameState& s, const uint8_t who) {
  if (who == kLinkRival) return true;
  return who < kPeople && (pb(s, who)[0] & kMet) != 0;
}

}  // namespace

void name(const GameState& s, const uint8_t i, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  if (i >= kPeople) return;
  const uint8_t b10 = pb(s, i)[10];
  suggestName(seedOf(s, i), static_cast<uint8_t>((b10 >> 4) & 0x07), static_cast<Gender>(b10 >> 7), out, len);
}

uint16_t lifespan(const uint8_t realm) { return realmInfo(realm < kMortalRealmCount ? realm : 0).lifespanYears; }

bool founded(const GameState& s) { return (s.cast[0] & kFounded) != 0; }

bool person(const GameState& s, const uint8_t i, Person& out) {
  out = Person{};
  if (i >= kPeople || !founded(s)) return false;
  const uint8_t* b = pb(s, i);
  if ((b[0] & (kAlive | kDead)) == 0) return false;
  out.used = true;
  out.alive = (b[0] & kAlive) != 0;
  out.met = (b[0] & kMet) != 0;
  out.secluded = (b[0] & kSecluded) != 0;
  out.realm = realmOf(b);
  out.ceiling = ceilingOf(b);
  const uint16_t now = static_cast<uint16_t>(s.progress.gameReadingSeconds / 3600u);
  if (out.alive) {
    out.progress = get16(b + 2);
    const uint8_t n = realmInfo(out.realm).stages;
    const uint32_t st = static_cast<uint32_t>(out.progress) * n / (need(out.realm) + 1u);
    out.stage = static_cast<uint8_t>(st < n ? st : n - 1);
    out.age = ageAt(b, now);
  } else {
    out.age = ageAt(b, get16(b + 2));
    out.cause = static_cast<Cause>((b[6] >> 5) & 0x07);
  }
  out.sect = sectOf(b);
  out.rank = static_cast<uint8_t>((b[6] >> 3) & 0x03);
  out.temper = static_cast<explore::Temperament>((b[7] & 0x03) < 3 ? (b[7] & 0x03) : 1);
  out.ambition = ambitionOf(b);
  out.pace = paceOf(static_cast<uint8_t>(b[7] >> 4));
  out.link = linkOf(b);
  out.linkTo = linkTo(b);
  const int o = (b[9] & 0x0F) - 5;
  out.opinion = static_cast<int8_t>(o < -5 ? -5 : o > 5 ? 5 : o);
  out.parting = static_cast<Parting>((b[9] >> 4) & 0x03);
  out.meets = static_cast<uint8_t>(b[9] >> 6);
  out.region = b[10] & 0x0F;
  out.generation = b[11];
  out.seed = seedOf(s, i);
  return true;
}

uint8_t metCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kPeople; ++i) {
    Person p;
    if (person(s, i, p) && p.met) ++n;
  }
  return n;
}

int level(const Person& p) {
  const int st = realmInfo(p.realm).layered ? p.stage : p.stage * 3;
  return 12 * p.realm + (st > 9 ? 9 : st);
}

void stepYear(GameState& s, const uint16_t year, Steps& out) {
  if (!founded(s)) {
    found(s, year);
    return;
  }
  for (uint8_t i = 0; i < kPeople; ++i) {
    Rng rng(contentSeed(worldSeedOf(s), kKindCast, mix32(year, 0x59454152u + i)));
    stepPerson(s, i, year, rng, out);
  }
}

bool newsworthy(const GameState& s, const Event& e) {
  if (e.who >= kPeople) return false;
  if ((pb(s, e.who)[0] & kMet) != 0) return true;
  if (e.other != kLinkNone && metOrRival(s, e.other)) return true;
  const uint8_t* b = pb(s, e.who);
  return linkOf(b) != Link::None && metOrRival(s, linkTo(b));
}

int meetingIn(const GameState& s, const uint8_t region, const uint32_t roll) {
  if (!founded(s)) return -1;
  uint8_t options[kPeople];
  uint8_t n = 0;
  for (uint8_t i = 0; i < kPeople; ++i) {
    const uint8_t* b = pb(s, i);
    if ((b[0] & kAlive) != 0 && (b[0] & kSecluded) == 0 && (b[10] & 0x0F) == region) options[n++] = i;
  }
  if (n == 0 || roll % kMeetOneIn != 0) return -1;
  return options[(roll / kMeetOneIn) % n];
}

void met(GameState& s, const uint8_t i, const explore::MeetOutcome outcome, const bool challenge) {
  if (i >= kPeople) return;
  uint8_t* b = pb(s, i);
  if ((b[0] & kAlive) == 0) return;
  b[0] = static_cast<uint8_t>(b[0] | kMet);
  int opinion = (b[9] & 0x0F) - 5;
  Parting parting = Parting::None;
  switch (outcome) {
    case explore::MeetOutcome::Won:
      parting = Parting::YouWon;
      if (challenge) --opinion;
      break;
    case explore::MeetOutcome::Lost:
      parting = Parting::YouLost;
      break;
    case explore::MeetOutcome::Traded:
    case explore::MeetOutcome::Talked:
      parting = Parting::Friendly;
      ++opinion;
      break;
    default:
      break;
  }
  if (opinion > 5) opinion = 5;
  if (opinion < -5) opinion = -5;
  uint8_t meets = static_cast<uint8_t>(b[9] >> 6);
  if (meets < 3) ++meets;
  b[9] = static_cast<uint8_t>((opinion + 5) | (static_cast<uint8_t>(parting) << 4) | (meets << 6));
}

void setOpinion(GameState& s, const uint8_t i, const int8_t v) {
  if (i >= kPeople) return;
  uint8_t* b = pb(s, i);
  const int o = v < -5 ? -5 : v > 5 ? 5 : v;
  b[9] = static_cast<uint8_t>((b[9] & 0xF0) | (o + 5));
}

bool friendly(const Person& p) { return p.opinion >= kFriendly; }
bool bitter(const Person& p) { return p.opinion <= kBitter; }

}  // namespace cast
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
