#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Daily.h"
#include "Exploration.h"
#include "Lineage.h"
#include "Rivals.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace rivals {
namespace {

// contentSeed kinds 122-129: v0.2.8 (122 rival, 123 its days, 124 ladder
// rolls, 125 ladder seats, 126 nemesis traits and epithets, 127 nemesis rumors).
constexpr uint32_t kKindRival = 122;
constexpr uint32_t kKindRivalDay = 123;
constexpr uint32_t kKindLadder = 124;
constexpr uint32_t kKindSeat = 125;
constexpr uint32_t kKindRumor = 127;

constexpr uint32_t kNoDay = StatsSnapshot::kNoDay;
constexpr uint32_t kMaxDaysCredited = 3650;  // a longer gap only credits its last ten years

// The rival's 16 bytes.
constexpr uint8_t kFlags = 0;
constexpr uint8_t kTemper = 1;
constexpr uint8_t kTalent = 2;
constexpr uint8_t kAnnounced = 3;
constexpr uint8_t kMinutes = 4;
constexpr uint8_t kLastDay = 8;
constexpr uint8_t kReactDay = 10;
constexpr uint8_t kReact = 12;
constexpr uint8_t kWins = 13;
constexpr uint8_t kLosses = 14;
constexpr uint8_t kCreated = 0x01;
constexpr uint8_t kKnown = 0x02;
constexpr uint8_t kAhead = 0x04;
constexpr uint8_t kHiatus = 0x08;
constexpr uint8_t kMet = 0x10;
constexpr uint8_t kReactBeaten = 1;
constexpr uint8_t kReactOvertaken = 2;

// The ladder's 8 bytes.
constexpr uint8_t kLadder = 16;
constexpr uint8_t kLadderRealm = kLadder + 0;
constexpr uint8_t kRank = kLadder + 1;
constexpr uint8_t kChallengeDay = kLadder + 2;
constexpr uint8_t kDefenceDay = kLadder + 4;
constexpr uint8_t kBestRank = kLadder + 6;
constexpr uint8_t kPlayerRegion = kLadder + 7;

// A nemesis's 24 bytes.
constexpr uint8_t kNemBase = 24;
constexpr uint8_t kNemBytes = 24;
constexpr uint8_t kNFlags = 0;   // 0x01 active, 0x02 element known, 0x04 it won the last fight
constexpr uint8_t kNKind = 1;    // kind bits 0-1, element 2-4, epithet kind 5-6
constexpr uint8_t kNRegion = 2;  // region bits 0-3, birth region 4-7
constexpr uint8_t kNLevel = 3;
constexpr uint8_t kNTraits = 4;    // 4 nibbles
constexpr uint8_t kNRevealed = 6;  // bits 0-3 revealed slots, 4-6 trait count
constexpr uint8_t kNWins = 7;
constexpr uint8_t kNLosses = 8;
constexpr uint8_t kNEscapes = 9;
constexpr uint8_t kNFled = 10;
constexpr uint8_t kNPaid = 11;
constexpr uint8_t kNFights = 12;
constexpr uint8_t kNEpithet = 13;
constexpr uint8_t kNSeed = 14;
constexpr uint8_t kNMoved = 18;
constexpr uint8_t kNRest = 20;
static_assert(kNemBase + kNemBytes * kNemesisMax <= sizeof(GameState::rivals), "rivals block");

int g_clockHour = -1;

uint8_t* rv(GameState& s) { return s.rivals.data(); }
const uint8_t* rv(const GameState& s) { return s.rivals.data(); }
uint8_t* nem(GameState& s, const uint8_t i) { return s.rivals.data() + kNemBase + kNemBytes * i; }
const uint8_t* nem(const GameState& s, const uint8_t i) { return s.rivals.data() + kNemBase + kNemBytes * i; }

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t* p, const uint32_t v) {
  const uint16_t c = static_cast<uint16_t>(v > 0xFFFFu ? 0xFFFFu : v);
  p[0] = static_cast<uint8_t>(c);
  p[1] = static_cast<uint8_t>(c >> 8);
}
uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
void put32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint8_t inc(const uint8_t v) { return v < 0xFF ? static_cast<uint8_t>(v + 1) : v; }

// Reading time in 10-minute units, + 1 so that 0 means "not set" (as exploration does).
uint16_t stampNow(const GameState& s) {
  const uint32_t units = s.progress.gameReadingSeconds / 600u + 1u;
  return static_cast<uint16_t>(units < 0xFFFFu ? units : 0xFFFFu);
}

// Day stored + 1 in a u16 (0 = none).
uint32_t dayOf(const uint8_t* p) {
  const uint16_t v = get16(p);
  return v == 0 ? kNoDay : static_cast<uint32_t>(v - 1u);
}
void putDay(uint8_t* p, const uint32_t day) { put16(p, day == kNoDay ? 0u : day + 1u); }

uint32_t todayOf(const GameState& s) { return daily::live(s) ? daily::today(s) : kNoDay; }

uint8_t stagesOf(const uint8_t realm) { return realmInfo(realm).stages; }

int32_t positionOf(const uint8_t realm, const uint8_t stage) {
  const uint8_t n = stagesOf(realm);
  const uint8_t st = stage < n ? stage : static_cast<uint8_t>(n - 1);
  return static_cast<int32_t>(realm) * 1000 + static_cast<int32_t>(st) * 1000 / n;
}

int stageIndex(const uint8_t realm, const uint8_t stage) {
  int n = 0;
  for (uint8_t r = 0; r < realm && r < kMortalRealmCount; ++r) n += stagesOf(r);
  return n + stage;
}

int stageLevel(const uint8_t realm, const uint8_t stage) {
  const int st = realmInfo(realm).layered ? stage : stage * 3;
  return st > 9 ? 9 : st;
}

Temper temperOf(const GameState& s) {
  const uint8_t t = rv(s)[kTemper];
  return t < static_cast<uint8_t>(Temper::Count) ? static_cast<Temper>(t) : Temper::Steady;
}

void create(GameState& s, const StatsSample& now) {
  uint8_t* r = rv(s);
  Rng rng(rivalSeed(s));
  const uint32_t t = rng.below(100);  // 20 / 30 / 20 / 15 / 15 (agreed)
  const Temper temper = t < 20   ? Temper::Idle
                        : t < 50 ? Temper::Steady
                        : t < 70 ? Temper::Driven
                        : t < 85 ? Temper::Erratic
                                 : Temper::LateBloomer;
  r[kTemper] = static_cast<uint8_t>(temper);
  r[kTalent] = static_cast<uint8_t>(rng.range(80, 125));
  r[kFlags] = kCreated;
  // The Fallen clan heir knows the rival from day one (DOC).
  if (s.character.origin == Origin::FallenClanHeir) r[kFlags] = static_cast<uint8_t>(r[kFlags] | kKnown);
  // Credited from the character's creation day; without one, from the first day known.
  const uint32_t from = s.character.creationDay != kNoDay ? s.character.creationDay : now.today;
  if (from != kNoDay) putDay(r + kLastDay, from);
  r[kAnnounced] = 0;
}

// Days d-7 .. d all without reading, and all on or after `start`: the rival rests on d.
bool hiatusOn(const StatsSample& now, const uint32_t d, const uint32_t start) {
  if (start == kNoDay || d < start + kHiatusDays) return false;
  for (uint32_t k = 0; k <= kHiatusDays; ++k) {
    if (now.readOn(d - k)) return false;
  }
  return true;
}

// The ladder belongs to the player's realm: a new realm starts it again, unranked.
void syncLadder(GameState& s) {
  uint8_t* r = rv(s);
  const uint8_t realm = s.progress.realm;
  if (realm < 1) return;
  if (r[kLadderRealm] != realm + 1) {
    r[kLadderRealm] = static_cast<uint8_t>(realm + 1);
    r[kRank] = 0;
    r[kBestRank] = 0;
  }
}

int plainLevel(const GameState& s) {
  // As for a cultivator on the road (Exploration.cpp, pendingMeeting): their
  // level follows yours without your techniques and your own bonuses.
  const int with = explore::playerLevel(s, true);
  return 2 * explore::playerLevel(s, false) - with - explore::ownLevelBonus(s);
}

uint16_t hpCostOf(const GameState& s) {
  return static_cast<uint16_t>((static_cast<uint32_t>(s.progress.hpMax) * kHpCostPercent + 99u) / 100u);
}

// --- Nemesis bytes --------------------------------------------------------------------------

Trait traitAt(const uint8_t* b, const uint8_t slot) {
  const uint8_t v = static_cast<uint8_t>((b[kNTraits + slot / 2] >> ((slot % 2) * 4)) & 0x0F);
  return v < static_cast<uint8_t>(Trait::Count) ? static_cast<Trait>(v) : Trait::None;
}
void setTraitAt(uint8_t* b, const uint8_t slot, const Trait t) {
  uint8_t& byte = b[kNTraits + slot / 2];
  const uint8_t shift = static_cast<uint8_t>((slot % 2) * 4);
  byte = static_cast<uint8_t>((byte & ~(0x0F << shift)) | ((static_cast<uint8_t>(t) & 0x0F) << shift));
}
uint8_t traitCountOf(const uint8_t* b) { return static_cast<uint8_t>((b[kNRevealed] >> 4) & 0x07); }

// The elements a beast or bandit of each region may carry (two per theme).
static const Element kRegionElements[explore::kRegionCount][2] = {
    {Element::Water, Element::Wood},   // marsh
    {Element::Earth, Element::Metal},  // wastes
    {Element::Wood, Element::Earth},   // forest
    {Element::Earth, Element::Metal},  // gorge
    {Element::Water, Element::Metal},  // snow
    {Element::Water, Element::Wood},   // delta
    {Element::Fire, Element::Earth},   // volcano
    {Element::Metal, Element::Fire},   // battlefield
    {Element::Earth, Element::Water},  // salt
    {Element::Metal, Element::Wood},   // ridge
};

bool eligible(const Nemesis& n, const Element root, const Trait t) {
  if (t == Trait::None || hasTrait(n, t)) return false;
  if (t == Trait::Overcoming) return overcomes(n.element, root) && !hasTrait(n, Trait::Overcome);
  if (t == Trait::Overcome) return overcomes(root, n.element) && !hasTrait(n, Trait::Overcoming);
  if (t == Trait::Hunter) return !hasTrait(n, Trait::Slow);
  if (t == Trait::Slow) return !hasTrait(n, Trait::Hunter);
  if (t == Trait::Coward) return !hasTrait(n, Trait::Proud);  // too proud to run, or not
  if (t == Trait::Proud) return !hasTrait(n, Trait::Coward);
  if (t == Trait::Greedy) return n.kind != explore::Enemy::Beast;  // beasts take no money
  return true;
}

// One new trait, half the time a strength (the other kind when none is left). None when full.
Trait pickTrait(const Nemesis& n, const Element root, const uint32_t seed) {
  if (n.traitCount >= kMaxTraits) return Trait::None;
  Rng rng(seed);
  const bool strength = rng.below(2) == 0;
  for (int pass = 0; pass < 2; ++pass) {
    const bool want = pass == 0 ? strength : !strength;
    Trait options[static_cast<int>(Trait::Count)];
    uint8_t count = 0;
    for (uint8_t k = 1; k < static_cast<uint8_t>(Trait::Count); ++k) {
      const auto t = static_cast<Trait>(k);
      if (isStrength(t) == want && eligible(n, root, t)) options[count++] = t;
    }
    if (count > 0) return options[rng.below(count)];
  }
  return Trait::None;
}

void addTrait(GameState& s, const uint8_t index, const Trait t) {
  if (t == Trait::None) return;
  uint8_t* b = nem(s, index);
  const uint8_t count = traitCountOf(b);
  if (count >= kMaxTraits) return;
  setTraitAt(b, count, t);
  b[kNRevealed] = static_cast<uint8_t>((b[kNRevealed] & 0x0F) | ((count + 1) << 4));
}

// Reveals the first hidden trait of nemesis i; returns it (None when all are known).
Trait revealFirst(GameState& s, const uint8_t index) {
  uint8_t* b = nem(s, index);
  const uint8_t count = traitCountOf(b);
  b[kNFlags] = static_cast<uint8_t>(b[kNFlags] | 0x02);  // anything learnt about it shows its element
  for (uint8_t i = 0; i < count; ++i) {
    if ((b[kNRevealed] & (1u << i)) != 0) continue;
    b[kNRevealed] = static_cast<uint8_t>(b[kNRevealed] | (1u << i));
    return traitAt(b, i);
  }
  return Trait::None;
}

bool anyHidden(const Nemesis& n) {
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if ((n.revealed & (1u << i)) == 0) return true;
  }
  return false;
}

void setEpithet(GameState& s, const uint8_t index, const EpithetKind kind) {
  uint8_t* b = nem(s, index);
  b[kNKind] = static_cast<uint8_t>((b[kNKind] & 0x1F) | (static_cast<uint8_t>(kind) << 5));
  b[kNEpithet] = inc(b[kNEpithet]);
}

void rest(GameState& s, const uint8_t index) {
  Nemesis n;
  if (!nemesis(s, index, n)) return;
  const uint16_t units = hasTrait(n, Trait::QuickToHeal) ? kRestUnits / 2 : kRestUnits;
  put16(nem(s, index) + kNRest, static_cast<uint32_t>(stampNow(s)) + units);
}

// From the second fight on, you notice one trait a fight.
Trait countFight(GameState& s, const uint8_t index) {
  uint8_t* b = nem(s, index);
  b[kNFights] = inc(b[kNFights]);
  return b[kNFights] >= 2 ? revealFirst(s, index) : Trait::None;
}

}  // namespace

// --- The rival -------------------------------------------------------------------------------

uint32_t rivalSeed(const GameState& s) {
  // v0.3.0: Familiar Face brings the last life's rival back (same name, temper and talent).
  const uint32_t carried = lineage::carriedRivalSeed(s);
  return carried != 0 ? carried : contentSeed(worldSeedOf(s), kKindRival, 0);
}

void positionFor(const uint32_t deciMinutes, const uint8_t talentPercent, uint8_t& realm, uint8_t& stage) {
  uint64_t left = deciMinutes;
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) {
    // deci-hours x 60 = deci-minutes, times the talent factor.
    const uint64_t need = static_cast<uint64_t>(kRealmDeciHours[r]) * 60u * talentPercent / 100u;
    if (left < need || r + 1 == kMortalRealmCount) {
      realm = r;
      const uint8_t n = stagesOf(r);
      uint32_t total = 0;
      for (uint8_t k = 0; k < n; ++k) total += 100u + 12u * k;
      uint64_t acc = 0;
      stage = static_cast<uint8_t>(n - 1);
      for (uint8_t k = 0; k < n; ++k) {
        acc += need * (100u + 12u * k) / total;
        if (left < acc) {
          stage = k;
          break;
        }
      }
      return;
    }
    left -= need;
  }
}

Rival rival(const GameState& s) {
  Rival out;
  const uint8_t* r = rv(s);
  if ((r[kFlags] & kCreated) == 0) return out;
  out.exists = true;
  out.known = (r[kFlags] & kKnown) != 0;
  out.metBefore = (r[kFlags] & kMet) != 0;
  out.temper = temperOf(s);
  out.talentPercent = r[kTalent] >= 80 && r[kTalent] <= 125 ? r[kTalent] : 100;
  out.seed = rivalSeed(s);
  out.gender = (mix32(out.seed, 0x47454E44u) & 1u) == 0 ? Gender::Male : Gender::Female;
  out.deciMinutes = get32(r + kMinutes);
  positionFor(out.deciMinutes, out.talentPercent, out.realm, out.stage);
  out.hiatus = (r[kFlags] & kHiatus) != 0;
  out.wins = r[kWins];
  out.losses = r[kLosses];
  return out;
}

int32_t rivalPosition(const GameState& s) {
  const Rival r = rival(s);
  return positionOf(r.realm, r.stage);
}

int32_t playerPosition(const GameState& s) { return positionOf(s.progress.realm, s.progress.stage); }

int stageGap(const GameState& s) {
  const Rival r = rival(s);
  return stageIndex(r.realm, r.stage) - stageIndex(s.progress.realm, s.progress.stage);
}

uint32_t dayMinutes(const GameState& s, const uint32_t day, const uint8_t rivalRealm) {
  const uint8_t* r = rv(s);
  Rng rng(contentSeed(worldSeedOf(s), kKindRivalDay, day));
  const uint32_t reactDay = dayOf(r + kReactDay);
  const bool reacting = reactDay != kNoDay && day >= reactDay && day < reactDay + kReactionDays;
  const uint8_t reaction = reacting ? r[kReact] : 0;
  switch (temperOf(s)) {
    case Temper::Idle:  // slows to about 10 a day for two weeks after you beat them
      return 10u * (reaction == kReactBeaten ? rng.range(0, 20) : rng.range(0, 30));
    case Temper::Steady:
      return 10u * rng.range(20, 40);
    case Temper::Driven: {  // +20% for two weeks after you overtake or beat them
      const uint32_t m = 10u * rng.range(35, 55);
      return reaction != 0 ? m * 12u / 10u : m;
    }
    case Temper::Erratic:  // most days nothing, one day in four two hours; one in two after a defeat
      return rng.below(100) < (reaction == kReactBeaten ? 50u : 25u) ? 1200u : 0u;
    case Temper::LateBloomer: {
      uint32_t base = 15u + 5u * rivalRealm;
      if (base > 45u) base = 45u;
      return 10u * rng.range(base - 10u, base + 10u);
    }
    default:
      return 300u;
  }
}

News update(GameState& s, const StatsSample& now) {
  News news;
  uint8_t* r = rv(s);
  if ((r[kFlags] & kCreated) == 0) {
    create(s, now);
    news.created = true;
  }
  syncLadder(s);
  const int32_t player = playerPosition(s);
  if (now.today != kNoDay) {
    uint32_t last = dayOf(r + kLastDay);
    if (last == kNoDay) {  // no date until now: credit from today on
      putDay(r + kLastDay, now.today);
      last = now.today;
    }
    const uint32_t start =
        s.character.creationDay != kNoDay && s.character.creationDay <= now.today ? s.character.creationDay : last;
    if (now.today > last) {
      uint32_t from = last + 1;
      if (now.today - last > kMaxDaysCredited) from = now.today - kMaxDaysCredited + 1;
      uint32_t minutes = get32(r + kMinutes);
      const uint8_t talent = rival(s).talentPercent;
      bool hiatus = (r[kFlags] & kHiatus) != 0;
      for (uint32_t d = from; d <= now.today; ++d) {
        if (hiatusOn(now, d, start)) {
          hiatus = true;
          continue;
        }
        hiatus = false;
        uint8_t realm = 0;
        uint8_t stage = 0;
        positionFor(minutes, talent, realm, stage);
        uint32_t m = dayMinutes(s, d, realm);
        // Elastic distance (agreed): over a whole realm ahead, half pace; behind, +50%.
        const int32_t diff = positionOf(realm, stage) - player;
        if (diff > 1000) {
          m /= 2u;
        } else if (diff < -1000) {
          m = m * 3u / 2u;
        }
        minutes = minutes + m < minutes ? 0xFFFFFFFFu : minutes + m;
      }
      news.daysCredited = now.today - from + 1;
      put32(r + kMinutes, minutes);
      putDay(r + kLastDay, now.today);
      const bool was = (r[kFlags] & kHiatus) != 0;
      if (hiatus && !was) news.hiatusBegan = true;
      if (!hiatus && was) news.hiatusEnded = true;
      r[kFlags] = static_cast<uint8_t>(hiatus ? (r[kFlags] | kHiatus) : (r[kFlags] & ~kHiatus));
    }
  }
  const Rival rv2 = rival(s);
  if (rv2.realm > r[kAnnounced]) {
    if (rv2.known) {
      news.realmReached = true;
      news.realm = rv2.realm;
    }
    r[kAnnounced] = rv2.realm;
  }
  // Overtaking: only news once you know who they are. Level with each other changes nothing.
  const int32_t theirs = positionOf(rv2.realm, rv2.stage);
  const bool wasAhead = (r[kFlags] & kAhead) != 0;
  const bool ahead = player > theirs ? true : player < theirs ? false : wasAhead;
  if (ahead != wasAhead && rv2.known && !news.created) {
    if (ahead) {
      news.youOvertook = true;
      if (rv2.temper == Temper::Driven && now.today != kNoDay) {
        putDay(r + kReactDay, now.today);
        r[kReact] = kReactOvertaken;
      }
    } else {
      news.theyOvertook = true;
    }
  }
  r[kFlags] = static_cast<uint8_t>(ahead ? (r[kFlags] | kAhead) : (r[kFlags] & ~kAhead));
  return news;
}

int rivalLevel(const GameState& s) {
  const Rival r = rival(s);
  return 10 * r.realm + stageLevel(r.realm, r.stage) + 2 * r.realm;
}

void shiftDays(GameState& s, const int32_t delta) {
  uint8_t* r = rv(s);
  for (const uint8_t at : {kLastDay, kReactDay, kChallengeDay, kDefenceDay}) {
    const uint16_t d = get16(r + at);
    if (d == 0) continue;
    const int32_t v = static_cast<int32_t>(d) + delta;
    put16(r + at, static_cast<uint32_t>(v < 1 ? 1 : v));
  }
}

bool canMeetIn(const GameState& s, const uint8_t region) {
  const Rival r = rival(s);
  return r.exists && r.realm >= explore::unlockRealm(region);
}

bool offersTrade(const GameState& s, const uint8_t meeting) {
  switch (temperOf(s)) {
    case Temper::Idle:
      return true;
    case Temper::Erratic:
      return Rng(mix32(rivalSeed(s), 0x54524144u + meeting)).below(2) == 0;
    default:
      return false;
  }
}

void markKnown(GameState& s) { rv(s)[kFlags] = static_cast<uint8_t>(rv(s)[kFlags] | kKnown); }
void markMet(GameState& s) { rv(s)[kFlags] = static_cast<uint8_t>(rv(s)[kFlags] | kKnown | kMet); }

void beaten(GameState& s, const uint32_t today) {
  uint8_t* r = rv(s);
  r[kWins] = inc(r[kWins]);
  const Temper t = temperOf(s);
  if (today != kNoDay && (t == Temper::Idle || t == Temper::Driven || t == Temper::Erratic)) {
    putDay(r + kReactDay, today);
    r[kReact] = kReactBeaten;
  }
}

void lostTo(GameState& s) { rv(s)[kLosses] = inc(rv(s)[kLosses]); }

// --- The ladder ------------------------------------------------------------------------------

bool ladderOpen(const GameState& s) { return s.progress.realm >= 1; }

uint8_t ladderRealm(const GameState& s) { return s.progress.realm; }

uint8_t playerRank(const GameState& s) {
  const uint8_t* r = rv(s);
  if (!ladderOpen(s) || r[kLadderRealm] != s.progress.realm + 1) return 0;
  return r[kRank] <= kLadderSize ? r[kRank] : 0;
}

uint8_t rivalSeat(const GameState& s) {
  const Rival r = rival(s);
  if (!r.exists || !ladderOpen(s) || r.realm != s.progress.realm) return 0;
  const uint8_t n = stagesOf(r.realm);
  return static_cast<uint8_t>(18 - (n > 1 ? r.stage * 15 / (n - 1) : 0));
}

uint8_t rivalRank(const GameState& s) {
  const uint8_t seat = rivalSeat(s);
  if (seat == 0) return 0;
  const uint8_t p = playerRank(s);
  const uint8_t rank = p != 0 && seat >= p ? static_cast<uint8_t>(seat + 1) : seat;
  return rank <= kLadderSize ? rank : 0;
}

uint8_t titleRank(const GameState& s) {
  const uint8_t p = playerRank(s);
  return p >= 1 && p <= 3 ? p : 0;
}

int rankOffset(const uint8_t rank) {
  const int k = rank < 1 ? 1 : rank > kLadderSize ? kLadderSize : rank;
  // -6 at rank 20, +8 at rank 1 (agreed), in even steps.
  return -6 + ((kLadderSize - k) * 14 + 9) / 19;
}

uint32_t seatSeed(const GameState& s, const uint8_t seat) {
  return contentSeed(worldSeedOf(s), kKindSeat, (static_cast<uint32_t>(s.progress.realm) << 8) | seat);
}

Rung rung(const GameState& s, const uint8_t rank) {
  Rung out;
  const uint8_t p = playerRank(s);
  if (p != 0 && rank == p) {
    out.player = true;
    out.level = explore::playerLevel(s, true);
    return out;
  }
  out.seat = p != 0 && rank > p ? static_cast<uint8_t>(rank - 1) : rank;
  if (out.seat != 0 && out.seat == rivalSeat(s)) {
    out.rival = true;
    out.level = rivalLevel(s);
  } else {
    out.level = plainLevel(s) + rankOffset(rank);
    if (out.level < 1) out.level = 1;
  }
  return out;
}

namespace {
Bout boutAgainst(const GameState& s, const uint8_t rank) {
  Bout b;
  b.block = Block::None;
  b.rank = rank;
  b.who = rung(s, rank);
  b.wound = explore::woundOf(s);  // v0.2.9
  const int d = explore::playerLevel(s, true) - b.who.level -
                explore::woundLevels(b.wound);  // a cultivator: your techniques count double
  b.danger = explore::dangerFor(d);
  b.chance = explore::winChance(d);
  b.hpCost = hpCostOf(s);
  b.stones = 20u * (s.progress.realm + 1u);
  if (sects::tournamentOn(s)) b.stones *= 2u;  // v0.3.2: a tournament somewhere doubles the ranking's prizes
  return b;
}
}  // namespace

Bout challengeBout(const GameState& s) {
  Bout b;
  if (!ladderOpen(s)) return b;
  const uint8_t p = playerRank(s);
  if (p == 1) {
    b.block = Block::AtTop;
    return b;
  }
  b = boutAgainst(s, p == 0 ? kLadderSize : static_cast<uint8_t>(p - 1));
  const uint32_t today = todayOf(s);
  if (today == kNoDay) {
    b.block = Block::NoDate;
  } else if (dayOf(rv(s) + kChallengeDay) == today) {
    b.block = Block::DoneToday;
  } else if (s.progress.hp < b.hpCost) {
    b.block = Block::NoHp;
  }
  return b;
}

Bout defenceBout(const GameState& s) {
  Bout b;
  if (!ladderOpen(s)) return b;
  const uint8_t p = playerRank(s);
  if (p == 0 || p >= kLadderSize) {
    b.block = Block::NoOneBelow;
    return b;
  }
  b = boutAgainst(s, static_cast<uint8_t>(p + 1));
  const uint32_t today = todayOf(s);
  if (today == kNoDay) {
    b.block = Block::NoDate;
  } else if (dayOf(rv(s) + kDefenceDay) == today) {
    b.block = Block::DoneToday;
  }
  return b;
}

namespace {
BoutResult play(GameState& s, const Bout& b, const bool defence) {
  BoutResult res;
  uint8_t* r = rv(s);
  const uint32_t today = todayOf(s);
  res.done = true;
  res.who = b.who;
  res.rival = b.who.rival;
  res.rankBefore = playerRank(s);
  Rng rng(contentSeed(worldSeedOf(s), kKindLadder, (today << 8) | (static_cast<uint32_t>(b.rank) << 1) | defence));
  res.roll = static_cast<uint8_t>(rng.below(100));
  res.won = res.roll < b.chance;
  putDay(r + (defence ? kDefenceDay : kChallengeDay), today);
  if (res.won) {
    res.stones = b.stones;
    s.progress.stones += res.stones;
    daily::note(s, daily::Task::WinFight);
    if (!defence) r[kRank] = b.rank;  // the bout swaps ranks
    if (res.rival) beaten(s, today);
    if (!res.rival && !b.who.player && b.who.seat > 0) {  // v0.3.3: the seat holder's sect
      res.world =
          sects::cultivatorBeaten(s, sects::kNoRegion, sects::personSect(s, sects::kNoRegion, seatSeed(s, b.who.seat)));
    }
  } else {
    res.hpLost = b.hpCost < s.progress.hp ? b.hpCost : s.progress.hp;
    s.progress.hp = static_cast<uint16_t>(s.progress.hp - res.hpLost);
    if (defence) r[kRank] = b.rank;
    if (res.rival) lostTo(s);
  }
  res.rankAfter = playerRank(s);
  if (res.rankAfter != 0 && (r[kBestRank] == 0 || res.rankAfter < r[kBestRank])) r[kBestRank] = res.rankAfter;
  return res;
}
}  // namespace

BoutResult challenge(GameState& s) {
  syncLadder(s);
  const Bout b = challengeBout(s);
  if (b.block != Block::None) return {};
  return play(s, b, false);
}

BoutResult acceptDefence(GameState& s) {
  syncLadder(s);
  const Bout b = defenceBout(s);
  if (b.block != Block::None || s.progress.hp < b.hpCost) return {};
  return play(s, b, true);
}

bool declineDefence(GameState& s) {
  const Bout b = defenceBout(s);
  if (b.block != Block::None) return false;
  putDay(rv(s) + kDefenceDay, todayOf(s));
  return true;
}

// --- Nemeses ---------------------------------------------------------------------------------

bool isStrength(const Trait t) { return t >= Trait::Overcoming && t <= Trait::PackLeader; }

bool overcomes(const Element a, const Element b) {
  switch (a) {
    case Element::Wood:
      return b == Element::Earth;
    case Element::Earth:
      return b == Element::Water;
    case Element::Water:
      return b == Element::Fire;
    case Element::Fire:
      return b == Element::Metal;
    case Element::Metal:
      return b == Element::Wood;
    default:
      return false;
  }
}

bool nemesis(const GameState& s, const uint8_t index, Nemesis& out) {
  if (index >= kNemesisMax) return false;
  const uint8_t* b = nem(s, index);
  if ((b[kNFlags] & 0x01) == 0) return false;
  out = Nemesis{};
  out.active = true;
  out.kind = static_cast<explore::Enemy>(b[kNKind] & 0x03);
  const uint8_t e = static_cast<uint8_t>((b[kNKind] >> 2) & 0x07);
  out.element = e < static_cast<uint8_t>(Element::Count) ? static_cast<Element>(e) : Element::Earth;
  const uint8_t ep = static_cast<uint8_t>((b[kNKind] >> 5) & 0x03);
  out.epithet = ep <= static_cast<uint8_t>(EpithetKind::Victor) ? static_cast<EpithetKind>(ep) : EpithetKind::Birth;
  out.elementKnown = (b[kNFlags] & 0x02) != 0;
  out.wonLast = (b[kNFlags] & 0x04) != 0;
  out.region = static_cast<uint8_t>(b[kNRegion] & 0x0F);
  out.birthRegion = static_cast<uint8_t>(b[kNRegion] >> 4);
  if (out.region >= explore::kRegionCount) out.region = 0;
  if (out.birthRegion >= explore::kRegionCount) out.birthRegion = 0;
  out.level = b[kNLevel];
  out.traitCount = traitCountOf(b) <= kMaxTraits ? traitCountOf(b) : kMaxTraits;
  for (uint8_t i = 0; i < out.traitCount; ++i) out.traits[i] = traitAt(b, i);
  out.revealed = static_cast<uint8_t>(b[kNRevealed] & 0x0F);
  out.wins = b[kNWins];
  out.losses = b[kNLosses];
  out.escapes = b[kNEscapes];
  out.fled = b[kNFled];
  out.paid = b[kNPaid];
  out.fights = b[kNFights];
  out.epithetCount = b[kNEpithet];
  out.seed = get32(b + kNSeed);
  out.movedStamp = get16(b + kNMoved);
  out.restUntil = get16(b + kNRest);
  return true;
}

uint8_t nemesisCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kNemesisMax; ++i) n = static_cast<uint8_t>(n + ((nem(s, i)[kNFlags] & 0x01) != 0 ? 1 : 0));
  return n;
}

bool hasTrait(const Nemesis& n, const Trait t) {
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if (n.traits[i] == t) return true;
  }
  return false;
}

bool traitKnown(const Nemesis& n, const Trait t) {
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if (n.traits[i] == t) return (n.revealed & (1u << i)) != 0;
  }
  return false;
}

int fightLevel(const GameState& s, const Nemesis& n, const int hour) {
  (void)s;
  int level = n.level;
  const int floor = explore::regionBase(n.region) + 2;  // never below the road it walks
  if (level < floor) level = floor;
  if (hasTrait(n, Trait::IronHide)) level += 3;
  if (hasTrait(n, Trait::PackLeader)) level += 2;
  if (hasTrait(n, Trait::Overcoming)) level += 3;
  if (hasTrait(n, Trait::Overcome)) level -= 3;
  if (hasTrait(n, Trait::Proud) && n.wonLast) level -= 2;
  if (hasTrait(n, Trait::FearsDawn) && hour >= 6 && hour < 12) level -= 3;
  if (hasTrait(n, Trait::OldWound)) level -= n.wins < 4 ? n.wins : 4;
  return level < 1 ? 1 : level;
}

bool resting(const GameState& s, const Nemesis& n) { return n.restUntil != 0 && stampNow(s) < n.restUntil; }

bool hunting(const GameState& s, const Nemesis& n) { return playerRegion(s) == n.region; }

void setClockHour(const int hour) { g_clockHour = hour >= 0 && hour < 24 ? hour : -1; }
int clockHour() { return g_clockHour; }

void setPlayerRegion(GameState& s, const uint8_t region) {
  if (region < explore::kRegionCount) rv(s)[kPlayerRegion] = static_cast<uint8_t>(region + 1);
}

int playerRegion(const GameState& s) {
  const uint8_t v = rv(s)[kPlayerRegion];
  return v >= 1 && v <= explore::kRegionCount ? v - 1 : -1;
}

int bear(GameState& s, const explore::Enemy kind, const uint8_t region, const int level, const uint32_t seed,
         const uint32_t traitSeed) {
  if (kind == explore::Enemy::Spirit) return -1;
  int index = -1;
  for (uint8_t i = 0; i < kNemesisMax && index < 0; ++i) {
    if ((nem(s, i)[kNFlags] & 0x01) == 0) index = i;
  }
  if (index < 0) return -1;
  uint8_t* b = nem(s, static_cast<uint8_t>(index));
  for (uint8_t i = 0; i < kNemBytes; ++i) b[i] = 0;
  Rng rng(mix32(seed, traitSeed));
  Element element;
  if (kind == explore::Enemy::Cultivator) {  // a cultivator's own root
    element = static_cast<Element>(rng.below(static_cast<uint32_t>(Element::Count)));
  } else {  // the region's beasts and bandits
    element = kRegionElements[region < explore::kRegionCount ? region : 0][rng.below(2)];
  }
  b[kNFlags] = 0x01 | 0x04;  // it won the fight that made it
  b[kNKind] = static_cast<uint8_t>((static_cast<uint8_t>(kind) & 0x03) | (static_cast<uint8_t>(element) << 2));
  b[kNRegion] = static_cast<uint8_t>((region & 0x0F) | ((region & 0x0F) << 4));
  b[kNLevel] = static_cast<uint8_t>(level < 1 ? 1 : level > 255 ? 255 : level);
  b[kNLosses] = 1;
  put32(b + kNSeed, seed);
  put16(b + kNMoved, stampNow(s));
  Nemesis n;
  nemesis(s, static_cast<uint8_t>(index), n);
  addTrait(s, static_cast<uint8_t>(index), pickTrait(n, s.character.rootElement, rng.next()));
  rest(s, static_cast<uint8_t>(index));
  return index;
}

void roam(GameState& s) {
  const int target = playerRegion(s);
  const uint16_t now = stampNow(s);
  for (uint8_t i = 0; i < kNemesisMax; ++i) {
    Nemesis n;
    if (!nemesis(s, i, n)) continue;
    uint8_t* b = nem(s, i);
    const uint16_t period = hasTrait(n, Trait::Hunter) ? kRoamUnits / 2 : kRoamUnits;
    if (n.movedStamp == 0 || now <= n.movedStamp) {
      if (n.movedStamp == 0) put16(b + kNMoved, now);
      continue;
    }
    const uint32_t steps = static_cast<uint32_t>(now - n.movedStamp) / period;
    if (steps == 0) continue;
    put16(b + kNMoved, static_cast<uint32_t>(n.movedStamp) + steps * period);
    if (target < 0) continue;
    int region = n.region;
    for (uint32_t k = 0; k < steps && region != target; ++k) region += region < target ? 1 : -1;
    b[kNRegion] = static_cast<uint8_t>((b[kNRegion] & 0xF0) | (region & 0x0F));
  }
}

int ambush(const GameState& s, const uint8_t region, const uint32_t seed) {
  for (uint8_t i = 0; i < kNemesisMax; ++i) {
    Nemesis n;
    if (!nemesis(s, i, n) || n.region != region || resting(s, n)) continue;
    const uint32_t chance = hasTrait(n, Trait::Ambusher) ? 2u * kFindPercent : kFindPercent;
    if (Rng(mix32(seed, i)).below(100) < chance) return i;
  }
  return -1;
}

Aftermath youWon(GameState& s, const uint8_t index, const uint32_t seed) {
  Aftermath a;
  Nemesis n;
  if (!nemesis(s, index, n)) return a;
  uint8_t* b = nem(s, index);
  b[kNWins] = inc(b[kNWins]);
  b[kNFlags] = static_cast<uint8_t>(b[kNFlags] & ~0x04);
  // It escapes with a scar and learns something from it.
  a.newTrait = pickTrait(n, s.character.rootElement, mix32(seed, 0x53434152u));
  addTrait(s, index, a.newTrait);
  setEpithet(s, index, EpithetKind::Scar);
  a.revealedTrait = countFight(s, index);
  rest(s, index);
  return a;
}

Aftermath youLost(GameState& s, const uint8_t index, const int playerLevelNow, const uint32_t seed) {
  (void)seed;
  Aftermath a;
  Nemesis n;
  if (!nemesis(s, index, n)) return a;
  uint8_t* b = nem(s, index);
  b[kNLosses] = inc(b[kNLosses]);
  b[kNFlags] = static_cast<uint8_t>(b[kNFlags] | 0x04);
  int level = n.level + kLevelsPerWin;
  const int cap = playerLevelNow + kLevelCapAbove;
  if (level > cap) level = cap > n.level ? cap : n.level;
  b[kNLevel] = static_cast<uint8_t>(level < 1 ? 1 : level > 255 ? 255 : level);
  setEpithet(s, index, EpithetKind::Victor);
  a.revealedTrait = countFight(s, index);
  rest(s, index);
  return a;
}

Aftermath youEscaped(GameState& s, const uint8_t index, const bool paid, const uint32_t seed) {
  (void)seed;
  Aftermath a;
  Nemesis n;
  if (!nemesis(s, index, n)) return a;
  uint8_t* b = nem(s, index);
  if (paid) {
    b[kNPaid] = inc(b[kNPaid]);
  } else {
    b[kNEscapes] = inc(b[kNEscapes]);
  }
  a.revealedTrait = countFight(s, index);
  rest(s, index);
  return a;
}

void itFled(GameState& s, const uint8_t index) {
  Nemesis n;
  if (!nemesis(s, index, n)) return;
  uint8_t* b = nem(s, index);
  b[kNFled] = inc(b[kNFled]);
  rest(s, index);
}

int revealOne(GameState& s, Trait& trait, const uint32_t seed) {
  (void)seed;
  trait = Trait::None;
  int pick = -1;
  for (uint8_t i = 0; i < kNemesisMax; ++i) {
    Nemesis n;
    if (!nemesis(s, i, n) || !anyHidden(n)) continue;
    if (pick < 0 || hunting(s, n)) pick = i;  // the one hunting you first
    if (hunting(s, n)) break;
  }
  if (pick < 0) return -1;
  trait = revealFirst(s, static_cast<uint8_t>(pick));
  return pick;
}

int rumorPick(const GameState& s, const uint32_t day, Trait& trait) {
  trait = Trait::None;
  Rng rng(contentSeed(worldSeedOf(s), kKindRumor, day));
  if (rng.below(4) != 0) return -1;
  int options[kNemesisMax];
  uint8_t count = 0;
  for (uint8_t i = 0; i < kNemesisMax; ++i) {
    Nemesis n;
    if (nemesis(s, i, n) && anyHidden(n)) options[count++] = i;
  }
  if (count == 0) return -1;
  const int pick = options[rng.below(count)];
  Nemesis n;
  nemesis(s, static_cast<uint8_t>(pick), n);
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if ((n.revealed & (1u << i)) == 0) {
      trait = n.traits[i];
      break;
    }
  }
  return pick;
}

uint32_t payCost(const Nemesis& n) {
  const uint32_t base = 15u * (n.region + 1u);  // what a bandit on the road asks
  if (hasTrait(n, Trait::Greedy)) return (base + 1u) / 2u;
  return n.kind == explore::Enemy::Bandit ? base : 0u;
}

// Used by the rumor: reveal exactly the trait it named.
void revealTrait(GameState& s, const uint8_t index, const Trait t) {
  Nemesis n;
  if (!nemesis(s, index, n)) return;
  uint8_t* b = nem(s, index);
  b[kNFlags] = static_cast<uint8_t>(b[kNFlags] | 0x02);
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if (n.traits[i] == t) b[kNRevealed] = static_cast<uint8_t>(b[kNRevealed] | (1u << i));
  }
}

}  // namespace rivals
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
