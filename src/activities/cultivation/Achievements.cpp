#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstring>

#include "Achievements.h"
#include "Agenda.h"
#include "Alchemy.h"
#include "Breakthrough.h"
#include "Cast.h"
#include "Codex.h"
#include "Daily.h"
#include "Daos.h"
#include "Exploration.h"
#include "Inventory.h"
#include "Lineage.h"
#include "Meridians.h"
#include "Rivals.h"
#include "Scrolls.h"
#include "Sects.h"
#include "Simulation.h"

namespace cultivation {
namespace achievements {
namespace {

using G = Group;
using T = Tier;

constexpr uint8_t kFlags = 0;
constexpr uint8_t kListSize = 1;
constexpr uint8_t kSeen = 2;
constexpr uint8_t kRankRealm = 3;
constexpr uint8_t kSeconds = 4;
constexpr uint8_t kLifeSeconds = 8;
constexpr uint8_t kBooks = 12;
constexpr uint8_t kFights = 14;
constexpr uint8_t kCleared = 16;
constexpr uint8_t kGuardians = 18;
constexpr uint8_t kBrews = 20;
constexpr uint8_t kLifeBrews = 22;
constexpr uint8_t kLongDays = 24;
constexpr uint8_t kLifeLongDays = 26;
constexpr uint8_t kFirstRanks = 27;
constexpr uint8_t kStamps = 64;
constexpr uint8_t kFlagSetUp = 0x01;
static_assert(kStamps + 2 * kCapacity == kBlockSize, "achievements block layout");
static_assert(sizeof(GameState::achievements) == kBlockSize, "achievements block size");

// Tiers and targets agreed with the user (session 18). Order = Id order.
const Info kInfo[kCount] = {
    // The Path
    {G::Path, T::Mortal, false, 0},    // RealmQi
    {G::Path, T::Mortal, false, 0},    // RealmFoundation
    {G::Path, T::Earth, false, 0},     // RealmCore
    {G::Path, T::Earth, false, 0},     // RealmNascent
    {G::Path, T::Heaven, false, 0},    // RealmSoul
    {G::Path, T::Heaven, false, 0},    // RealmVoid
    {G::Path, T::Heaven, false, 0},    // RealmBody
    {G::Path, T::Immortal, false, 0},  // RealmMahayana
    {G::Path, T::Immortal, false, 0},  // RealmTribulation
    {G::Path, T::Mortal, false, 0},    // FirstFailure
    {G::Path, T::Earth, true, 0},      // FourthTime
    {G::Path, T::Earth, false, 0},     // Tribulation
    {G::Path, T::Earth, false, 0},     // HeartDemon
    {G::Path, T::Mortal, false, 0},    // GreatPerfection
    {G::Path, T::Earth, false, 40},    // FullConsolidation
    {G::Path, T::Earth, false, 0},     // PerfectFoundation
    {G::Path, T::Heaven, false, 0},    // HeavenlyFoundation
    {G::Path, T::Heaven, false, 0},    // FirstGradeCore
    {G::Path, T::Immortal, false, 0},  // FlawlessCore
    {G::Path, T::Heaven, false, 0},    // RadiantSoul
    {G::Path, T::Immortal, false, 0},  // ImprintedSoul
    {G::Path, T::Earth, true, 0},      // CrackedToCore
    // Reading
    {G::Reading, T::Mortal, false, 1},       // Hours1
    {G::Reading, T::Mortal, false, 10},      // Hours10
    {G::Reading, T::Earth, false, 50},       // Hours50
    {G::Reading, T::Earth, false, 100},      // Hours100
    {G::Reading, T::Heaven, false, 250},     // Hours250
    {G::Reading, T::Heaven, false, 500},     // Hours500
    {G::Reading, T::Immortal, false, 1000},  // Hours1000
    {G::Reading, T::Mortal, false, 3},       // Streak3
    {G::Reading, T::Earth, false, 7},        // Streak7
    {G::Reading, T::Earth, false, 30},       // Streak30
    {G::Reading, T::Heaven, false, 100},     // Streak100
    {G::Reading, T::Immortal, false, 365},   // Streak365
    {G::Reading, T::Earth, false, 10},       // LongDays10
    {G::Reading, T::Heaven, false, 50},      // LongDays50
    {G::Reading, T::Earth, false, 0},        // Day3h
    {G::Reading, T::Heaven, true, 0},        // Day5h
    {G::Reading, T::Heaven, false, 0},       // Week14h
    {G::Reading, T::Earth, false, 10},       // Night10
    {G::Reading, T::Earth, false, 10},       // Morning10
    {G::Reading, T::Earth, false, 10},       // Afternoon10
    {G::Reading, T::Earth, false, 10},       // Evening10
    {G::Reading, T::Mortal, false, 0},       // Books1
    {G::Reading, T::Earth, false, 5},        // Books5
    {G::Reading, T::Earth, false, 10},       // Books10
    {G::Reading, T::Heaven, false, 25},      // Books25
    {G::Reading, T::Heaven, false, 50},      // Books50
    {G::Reading, T::Immortal, false, 100},   // Books100
    // Techniques
    {G::Techniques, T::Mortal, false, 0},  // FirstScroll
    {G::Techniques, T::Mortal, false, 0},  // YellowScroll
    {G::Techniques, T::Earth, false, 0},   // EarthScroll
    {G::Techniques, T::Earth, false, 0},   // HeavenScroll
    {G::Techniques, T::Heaven, false, 0},  // SaintScroll
    {G::Techniques, T::Earth, false, 0},   // PerfectedScroll
    {G::Techniques, T::Heaven, false, 0},  // SacredScroll
    {G::Techniques, T::Mortal, false, 0},  // EquipManual
    {G::Techniques, T::Earth, false, 20},  // Library20
    {G::Techniques, T::Mortal, false, 0},  // SellScroll
    {G::Techniques, T::Earth, true, 0},    // BurnScroll
    // Body and Dao
    {G::Body, T::Mortal, false, 0},     // Meridian1
    {G::Body, T::Earth, false, 10},     // Meridian10
    {G::Body, T::Immortal, false, 36},  // Meridian36
    {G::Body, T::Mortal, false, 0},     // DaoHeart
    {G::Body, T::Mortal, false, 0},     // Dao1
    {G::Body, T::Earth, false, 5},      // Dao5
    {G::Body, T::Heaven, false, 9},     // Dao9
    {G::Body, T::Earth, false, 5},      // DaoLevel5
    {G::Body, T::Heaven, false, 9},     // DaoLevel9
    // The road
    {G::Road, T::Mortal, false, 0},    // Win1
    {G::Road, T::Earth, false, 100},   // Win100
    {G::Road, T::Heaven, false, 500},  // Win500
    {G::Road, T::Mortal, false, 0},    // Clear1
    {G::Road, T::Earth, false, 3},     // Clear3
    {G::Road, T::Heaven, false, 5},    // Clear5
    {G::Road, T::Heaven, false, 10},   // Clear10
    {G::Road, T::Mortal, false, 0},    // Guardian1
    {G::Road, T::Earth, false, 10},    // Guardian10
    {G::Road, T::Earth, false, 0},     // Reshaped
    {G::Road, T::Mortal, false, 0},    // Cave
    {G::Road, T::Mortal, false, 0},    // Ruin
    {G::Road, T::Mortal, false, 0},    // Merchant
    {G::Road, T::Earth, false, 0},     // Talisman
    {G::Road, T::Mortal, false, 0},    // RoadMeeting
    {G::Road, T::Mortal, true, 0},     // Collapse
    // Alchemy
    {G::Alchemy, T::Mortal, false, 0},   // Brew1
    {G::Alchemy, T::Mortal, false, 0},   // Experiment
    {G::Alchemy, T::Earth, false, 5},    // Recipes5
    {G::Alchemy, T::Heaven, false, 10},  // Recipes10
    {G::Alchemy, T::Earth, false, 0},    // HighPill
    {G::Alchemy, T::Earth, false, 0},    // GradePill
    {G::Alchemy, T::Heaven, false, 50},  // Brew50
    // Rivals
    {G::Rivals, T::Mortal, false, 0},    // MeetRival
    {G::Rivals, T::Earth, false, 0},     // BeatRival
    {G::Rivals, T::Heaven, false, 5},    // BeatRival5
    {G::Rivals, T::Heaven, false, 0},    // WinDuel
    {G::Rivals, T::Mortal, true, 0},     // LoseDuel
    {G::Rivals, T::Heaven, false, 0},    // RivalFriend
    {G::Rivals, T::Earth, true, 0},      // RivalBitter
    {G::Rivals, T::Mortal, false, 0},    // LadderWin
    {G::Rivals, T::Earth, false, 0},     // LadderTop3
    {G::Rivals, T::Heaven, false, 0},    // LadderFirst
    {G::Rivals, T::Immortal, false, 5},  // LadderFirst5
    {G::Rivals, T::Mortal, false, 0},    // Nemesis1
    {G::Rivals, T::Earth, false, 3},     // NemesisBeat3
    {G::Rivals, T::Earth, false, 0},     // Cornered
    {G::Rivals, T::Earth, false, 0},     // NemesisLeaves
    {G::Rivals, T::Earth, true, 0},      // Nemesis3
    // People and the world
    {G::People, T::Mortal, false, 0},  // Cast1
    {G::People, T::Earth, false, 5},   // Cast5
    {G::People, T::Heaven, false, 0},  // Friend
    {G::People, T::Earth, true, 0},    // Enemy
    {G::People, T::Earth, true, 0},    // Mourned
    {G::People, T::Heaven, false, 0},  // SectFriend
    {G::People, T::Earth, true, 0},    // SectEnemy
    {G::People, T::Earth, false, 0},   // Camp
    {G::People, T::Earth, false, 0},   // WarDecided
    // Lineage
    {G::Lineage, T::Earth, false, 0},      // Life2
    {G::Lineage, T::Heaven, false, 0},     // Life3
    {G::Lineage, T::Immortal, false, 10},  // Life10
    {G::Lineage, T::Earth, true, 0},       // OldAge
    {G::Lineage, T::Mortal, true, 0},      // Abandon
    {G::Lineage, T::Heaven, false, 5},     // Origins5
    {G::Lineage, T::Heaven, false, 5},     // Talents5
    {G::Lineage, T::Heaven, false, 5},     // Elements5
    {G::Lineage, T::Heaven, true, 0},      // HeavenBlessed
    {G::Lineage, T::Heaven, true, 0},      // ChokedCore
    {G::Lineage, T::Immortal, true, 0},    // ChokedNascent
    {G::Lineage, T::Mortal, false, 0},     // Memento1
    {G::Lineage, T::Earth, false, 16},     // Memento16
    {G::Lineage, T::Heaven, false, 32},    // Memento32
    {G::Lineage, T::Immortal, false, 64},  // Memento64
    {G::Lineage, T::Earth, false, 0},      // Legendary
    {G::Lineage, T::Heaven, false, 0},     // FullSet
    {G::Lineage, T::Immortal, false, 0},   // FullCodex
    {G::Lineage, T::Earth, false, 0},      // FirstPerk
};

uint8_t* blk(GameState& s) { return s.achievements.data(); }
const uint8_t* blk(const GameState& s) { return s.achievements.data(); }

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

uint8_t counterOffset(const Counter c) {
  switch (c) {
    case Counter::Books:
      return kBooks;
    case Counter::FightsWon:
      return kFights;
    case Counter::Cleared:
      return kCleared;
    default:
      return kGuardians;
  }
}

void award(GameState& s, const uint8_t id, const uint16_t stamp) {
  if (id >= kCount || earned(s, id)) return;
  uint8_t* b = blk(s);
  b[kBitsOffset + id / 8] = static_cast<uint8_t>(b[kBitsOffset + id / 8] | (1u << (id % 8)));
  put16(b + kStamps + 2 * id, stamp);
}

uint16_t stampNow(const GameState& s) {
  const uint32_t h = readingHours(s);
  return static_cast<uint16_t>(h < kRecognised ? h : kRecognised - 1);
}

// --- What the save shows -------------------------------------------------------------------

uint8_t bestRealmEver(const GameState& s) {
  int best = lineage::bestRealm(s);
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) {
    if (codex::has(s, codex::kRealms + r) && r > best) best = r;
  }
  if (s.isCreated() && s.progress.realm > best) best = s.progress.realm;
  return best < 0 ? 0 : static_cast<uint8_t>(best);
}

bool scrollTierSeen(const GameState& s, const scrolls::Tier tier) {
  for (uint8_t k = 0; k < static_cast<uint8_t>(scrolls::Kind::Count); ++k) {
    if (codex::has(s, static_cast<uint16_t>(codex::kScrolls + k * static_cast<uint8_t>(scrolls::Tier::Count) +
                                            static_cast<uint8_t>(tier)))) {
      return true;
    }
  }
  return false;
}

uint8_t codexGroupFound(const GameState& s, const codex::Group g) { return codex::groupFound(s, g); }

uint8_t bestDaoLevel(const GameState& s) {
  uint8_t best = 0;
  for (uint8_t d = 0; d < daos::kCount; ++d) {
    const auto dao = static_cast<daos::Dao>(d);
    if (!daos::found(s, dao)) continue;
    const uint8_t l = daos::level(s, dao);
    if (l > best) best = l;
  }
  return best;
}

bool holdsPerfected(const GameState& s) {
  const uint8_t n = scrolls::count(s);
  for (uint8_t i = 0; i < n; ++i) {
    const scrolls::Scroll sc = scrolls::get(s, i);
    if (sc.titleHash != 0 && scrolls::qualityFor(sc.comprehension) >= scrolls::Quality::Perfected) return true;
  }
  return false;
}

bool holdsHighPill(const GameState& s) {
  // Breakthrough pills 33-41 (grade * 3 + quality) and grade pills 42-50 (kind * 3 + quality).
  for (uint8_t id = 33; id <= 50; ++id) {
    if ((id - 33) % 3 == 2 && inventory::count(s, static_cast<inventory::Item>(id)) > 0) return true;
  }
  return false;
}

uint8_t clearedNow(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t r = 0; r < explore::kRegionCount; ++r) n = static_cast<uint8_t>(n + (explore::cleared(s, r) ? 1 : 0));
  return n;
}
uint8_t guardiansNow(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t r = 0; r < explore::kRegionCount; ++r) {
    n = static_cast<uint8_t>(n + (explore::guardianBeaten(s, r) ? 1 : 0));
  }
  return n;
}
bool anyReshaped(const GameState& s) {
  for (uint8_t r = 0; r < explore::kRegionCount; ++r) {
    if (explore::reshapeCount(s, r) > 0) return true;
  }
  return false;
}

uint8_t mostNemesisWins(const GameState& s) {
  uint8_t best = 0;
  for (uint8_t i = 0; i < rivals::kNemesisMax; ++i) {
    rivals::Nemesis n;
    if (rivals::nemesis(s, i, n) && n.wins > best) best = n.wins;
  }
  return best;
}

// The cast: someone met with this opinion or beyond (sign of `bound`), and anyone met who died.
bool castOpinion(const GameState& s, const int8_t bound) {
  for (uint8_t i = 0; i < cast::kPeople; ++i) {
    cast::Person p;
    if (!cast::person(s, i, p) || !p.met) continue;
    if (bound > 0 ? p.opinion >= bound : p.opinion <= bound) return true;
  }
  return false;
}
bool castMourned(const GameState& s) {
  for (uint8_t i = 0; i < cast::kPeople; ++i) {
    cast::Person p;
    if (cast::person(s, i, p) && p.used && p.met && !p.alive) return true;
  }
  return false;
}

bool sectStanding(const GameState& s, const int8_t bound) {
  if (!sects::founded(s)) return false;
  for (uint8_t i = 0; i < sects::kSects; ++i) {
    const int8_t st = sects::standing(s, i);
    if (bound > 0 ? st >= bound : st <= bound) return true;
  }
  return false;
}

// The value a counted or plain one measures now (plain ones: 1 = met).
uint32_t measure(const GameState& s, const Id id) {
  const bool live = s.isCreated();
  const ProgressBlock& p = s.progress;
  switch (id) {
    case Id::RealmQi:
    case Id::RealmFoundation:
    case Id::RealmCore:
    case Id::RealmNascent:
    case Id::RealmSoul:
    case Id::RealmVoid:
    case Id::RealmBody:
    case Id::RealmMahayana:
    case Id::RealmTribulation:
      return bestRealmEver(s) >= static_cast<uint8_t>(id) + 1 ? 1 : 0;
    case Id::FirstFailure:
      return live && (p.failures > 0 || (p.progressFlags & ProgressBlock::kHalfStep) != 0) ? 1 : 0;
    case Id::Tribulation:
      return bestRealmEver(s) >= 3 ? 1 : 0;
    case Id::HeartDemon:
      return bestRealmEver(s) >= 4 ? 1 : 0;
    case Id::GreatPerfection:
      return live && breakthrough::inGreatPerfection(s) ? 1 : 0;
    case Id::FullConsolidation:
      return live ? p.consolidation : 0;
    case Id::PerfectFoundation:
      return live && p.foundationGrade >= 4 ? 1 : 0;
    case Id::HeavenlyFoundation:
      return live && p.foundationGrade >= 5 ? 1 : 0;
    case Id::FirstGradeCore:
      return live && p.coreGrade >= 9 ? 1 : 0;
    case Id::FlawlessCore:
      return live && p.coreGrade >= 10 ? 1 : 0;
    case Id::RadiantSoul:
      return live && p.soulClarity >= 3 ? 1 : 0;
    case Id::ImprintedSoul:
      return live && p.soulClarity >= 4 ? 1 : 0;
    case Id::CrackedToCore:
      return live && p.foundationGrade == 1 && p.realm >= 3 ? 1 : 0;

    case Id::Hours1:
    case Id::Hours10:
    case Id::Hours50:
    case Id::Hours100:
    case Id::Hours250:
    case Id::Hours500:
    case Id::Hours1000:
      return readingHours(s);
    case Id::Streak3:
    case Id::Streak7:
    case Id::Streak30:
    case Id::Streak100:
    case Id::Streak365:
      return live ? daily::longestStreak(s) : 0;
    case Id::LongDays10:
    case Id::LongDays50:
      return get16(blk(s) + kLongDays);
    case Id::Day3h:
      return live && (daos::daysOver3h(s) > 0 || daily::minutesToday(s) >= 180) ? 1 : 0;
    case Id::Day5h:
      return live && daily::minutesToday(s) >= 300 ? 1 : 0;
    case Id::Week14h:
      return live && s.snapshot.lastVisitDay != StatsSnapshot::kNoDay &&
                     s.snapshot.secondsCountedInLastVisitWeek >= 14u * 3600u
                 ? 1
                 : 0;
    case Id::Morning10:
      return live ? daos::bucketUnits(s, 0) / 6u : 0;
    case Id::Afternoon10:
      return live ? daos::bucketUnits(s, 1) / 6u : 0;
    case Id::Evening10:
      return live ? daos::bucketUnits(s, 2) / 6u : 0;
    case Id::Night10:
      return live ? daos::bucketUnits(s, 3) / 6u : 0;
    case Id::Books1:
    case Id::Books5:
    case Id::Books10:
    case Id::Books25:
    case Id::Books50:
    case Id::Books100:
      return count(s, Counter::Books);

    case Id::FirstScroll:
      return scrollTierSeen(s, scrolls::Tier::Mortal) || scrollTierSeen(s, scrolls::Tier::Yellow) ||
                     scrollTierSeen(s, scrolls::Tier::Earth) || scrollTierSeen(s, scrolls::Tier::Heaven) ||
                     scrollTierSeen(s, scrolls::Tier::Saint)
                 ? 1
                 : 0;
    case Id::YellowScroll:
      return scrollTierSeen(s, scrolls::Tier::Yellow) ? 1 : 0;
    case Id::EarthScroll:
      return scrollTierSeen(s, scrolls::Tier::Earth) ? 1 : 0;
    case Id::HeavenScroll:
      return scrollTierSeen(s, scrolls::Tier::Heaven) ? 1 : 0;
    case Id::SaintScroll:
      return scrollTierSeen(s, scrolls::Tier::Saint) ? 1 : 0;
    case Id::PerfectedScroll:
      return live && holdsPerfected(s) ? 1 : 0;
    case Id::SacredScroll:
      return codex::has(s, codex::kSacred) ? 1 : 0;
    case Id::EquipManual:
      return live && scrolls::equippedManual(s) >= 0 ? 1 : 0;
    case Id::Library20:
      return live ? scrolls::count(s) : 0;
    case Id::BurnScroll: {
      scrolls::Scroll burned;
      return live && scrolls::lastBurned(s, burned) ? 1 : 0;
    }

    case Id::Meridian1:
    case Id::Meridian10:
    case Id::Meridian36:
      return live ? meridians::openCount(s) : 0;
    case Id::DaoHeart:
      return live && p.daoHeart >= 100 ? 1 : 0;
    case Id::Dao1:
    case Id::Dao5:
    case Id::Dao9:
      return codexGroupFound(s, codex::Group::Daos);
    case Id::DaoLevel5:
    case Id::DaoLevel9:
      return live ? bestDaoLevel(s) : 0;

    case Id::Win1:
    case Id::Win100:
    case Id::Win500:
      return count(s, Counter::FightsWon);
    case Id::Clear1:
    case Id::Clear3:
    case Id::Clear5:
    case Id::Clear10:
      return count(s, Counter::Cleared);
    case Id::Guardian1:
    case Id::Guardian10:
      return count(s, Counter::Guardians);
    case Id::Reshaped:
      return live && anyReshaped(s) ? 1 : 0;
    case Id::RoadMeeting: {
      if (!live) return 0;
      for (uint8_t i = 32; i < 48; i += 4) {
        if (s.memory[i] != 0 || s.memory[i + 1] != 0) return 1;  // a cultivator remembered (v0.2.2 ring)
      }
      return 0;
    }

    case Id::Brew1:
    case Id::Brew50:
      return get16(blk(s) + kBrews);
    case Id::Recipes5:
    case Id::Recipes10:
      return live ? alchemy::knownCount(s) : 0;
    case Id::HighPill:
      return live && holdsHighPill(s) ? 1 : 0;
    case Id::GradePill:
      return live && alchemy::gradePillsTaken(s) > 0 ? 1 : 0;

    case Id::MeetRival: {
      const rivals::Rival r = rivals::rival(s);
      return live && r.exists && r.metBefore ? 1 : 0;
    }
    case Id::BeatRival:
    case Id::BeatRival5: {
      const rivals::Rival r = rivals::rival(s);
      return live && r.exists ? r.wins : 0;
    }
    case Id::RivalFriend:
      return live && rivals::rival(s).exists && agenda::opinion(s) >= 5 ? 1 : 0;
    case Id::RivalBitter:
      return live && rivals::rival(s).exists && agenda::opinion(s) <= -5 ? 1 : 0;
    case Id::LadderWin:
      return live && s.rivals[22] != 0 ? 1 : 0;  // the ladder's best rank (Rivals.cpp kBestRank)
    case Id::LadderTop3:
      return live && s.rivals[22] != 0 && s.rivals[22] <= 3 ? 1 : 0;
    case Id::LadderFirst:
      return live && s.rivals[22] == 1 ? 1 : 0;
    case Id::LadderFirst5:
      return blk(s)[kFirstRanks];
    case Id::Nemesis1:
      return live && rivals::nemesisCount(s) > 0 ? 1 : 0;
    case Id::NemesisBeat3:
      return live ? mostNemesisWins(s) : 0;
    case Id::Nemesis3:
      return live && rivals::nemesisCount(s) >= rivals::kNemesisMax ? 1 : 0;

    case Id::Cast1:
    case Id::Cast5:
      return live ? cast::metCount(s) : 0;
    case Id::Friend:
      return live && castOpinion(s, 5) ? 1 : 0;
    case Id::Enemy:
      return live && castOpinion(s, -5) ? 1 : 0;
    case Id::Mourned:
      return live && castMourned(s) ? 1 : 0;
    case Id::SectFriend:
      return live && sectStanding(s, 5) ? 1 : 0;
    case Id::SectEnemy:
      return live && sectStanding(s, -5) ? 1 : 0;

    case Id::Life2:
      return lineage::livesEnded(s) >= 1 ? 1 : 0;
    case Id::Life3:
      return lineage::livesEnded(s) >= 2 ? 1 : 0;
    case Id::Life10:
      return static_cast<uint32_t>(lineage::livesEnded(s)) + (s.isCreated() ? 1u : 0u);
    case Id::OldAge:
      return lineage::livesEnded(s) > 0 && lineage::lastEnd(s) == lineage::EndKind::OldAge ? 1 : 0;
    case Id::Abandon:
      return lineage::livesEnded(s) > 0 && lineage::lastEnd(s) == lineage::EndKind::Abandoned ? 1 : 0;
    case Id::Origins5:
      return codexGroupFound(s, codex::Group::Origins);
    case Id::Talents5:
      return codexGroupFound(s, codex::Group::Talents);
    case Id::Elements5:
      return codexGroupFound(s, codex::Group::Elements);
    case Id::HeavenBlessed:
      return codex::has(s, codex::kTalents + 4) ? 1 : 0;
    case Id::ChokedCore:
      return live && s.character.talent == 0 && p.realm >= 3 ? 1 : 0;
    case Id::ChokedNascent:
      return live && s.character.talent == 0 && p.realm >= 4 ? 1 : 0;
    case Id::Memento1:
    case Id::Memento16:
    case Id::Memento32:
    case Id::Memento64:
      return codex::mementosFound(s);
    case Id::Legendary:
      return codex::mementosOfRarity(s, codex::Rarity::Legendary) > 0 ? 1 : 0;
    case Id::FullSet: {
      for (uint8_t set = 0; set < codex::kSetCount; ++set) {
        if (codex::mementosInSet(s, set) >= codex::kSetSize) return 1;
      }
      return 0;
    }
    case Id::FullCodex:
      return codex::count(s) >= codex::kCodexEntries + codex::kMementoCount ? 1 : 0;
    case Id::FirstPerk:
      return lineage::imprintsEarned(s) > lineage::imprints(s) ? 1 : 0;

    default:
      return 0;  // earned only where it happens (earn())
  }
}

// The value a measure must reach: the target, else 1.
uint32_t threshold(const Id id) {
  const Info& i = info(id);
  return i.target > 0 ? i.target : 1;
}

// The ones only earn() can give (no measure).
bool eventOnly(const Id id) {
  switch (id) {
    case Id::FourthTime:
    case Id::SellScroll:
    case Id::Cave:
    case Id::Ruin:
    case Id::Merchant:
    case Id::Talisman:
    case Id::Collapse:
    case Id::Experiment:
    case Id::WinDuel:
    case Id::LoseDuel:
    case Id::Cornered:
    case Id::NemesisLeaves:
    case Id::Camp:
    case Id::WarDecided:
      return true;
    default:
      return false;
  }
}

// Totals that follow this life's own counters: they add what this life gained since the last look.
void catchUp(GameState& s) {
  uint8_t* b = blk(s);
  if (!s.isCreated()) return;
  const uint32_t secs = s.progress.gameReadingSeconds;
  uint32_t last = get32(b + kLifeSeconds);
  if (secs < last) last = 0;  // a new life (or a reset save): count from its start
  const uint32_t total = get32(b + kSeconds);
  const uint32_t add = secs - last;
  put32(b + kSeconds, total + add < total ? 0xFFFFFFFFu : total + add);
  put32(b + kLifeSeconds, secs);

  const uint16_t brews = static_cast<uint16_t>(s.timers[6] | (s.timers[7] << 8));
  uint16_t lastBrews = get16(b + kLifeBrews);
  if (brews < lastBrews) lastBrews = 0;
  put16(b + kBrews, static_cast<uint32_t>(get16(b + kBrews)) + (brews - lastBrews));
  put16(b + kLifeBrews, brews);

  const uint8_t longDays = daos::daysOver60(s);
  uint8_t lastLong = b[kLifeLongDays];
  if (longDays < lastLong) lastLong = 0;
  put16(b + kLongDays, static_cast<uint32_t>(get16(b + kLongDays)) + (longDays - lastLong));
  b[kLifeLongDays] = longDays;

  // Rank 1 in a realm's ranking: once per realm and life.
  if (rivals::ladderOpen(s) && rivals::playerRank(s) == 1) {
    const uint8_t realm = static_cast<uint8_t>(rivals::ladderRealm(s) + 1);
    if (b[kRankRealm] != realm) {
      b[kRankRealm] = realm;
      if (b[kFirstRanks] < 0xFF) ++b[kFirstRanks];
    }
  }
}

}  // namespace

const Info& info(const uint8_t id) { return kInfo[id < kCount ? id : 0]; }

int hiddenIndex(const uint8_t id) {
  if (id >= kCount || !kInfo[id].hidden) return -1;
  int n = 0;
  for (uint8_t i = 0; i < id; ++i) n += kInfo[i].hidden ? 1 : 0;
  return n;
}

bool earned(const GameState& s, const uint8_t id) {
  return id < kCount && (blk(s)[kBitsOffset + id / 8] & (1u << (id % 8))) != 0;
}

uint16_t earnedHour(const GameState& s, const uint8_t id) { return id < kCount ? get16(blk(s) + kStamps + 2 * id) : 0; }

uint8_t earnedCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n = static_cast<uint8_t>(n + (earned(s, i) ? 1 : 0));
  return n;
}

uint8_t earnedIn(const GameState& s, const Group g) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n = static_cast<uint8_t>(n + (kInfo[i].group == g && earned(s, i) ? 1 : 0));
  return n;
}

uint8_t countIn(const Group g) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n = static_cast<uint8_t>(n + (kInfo[i].group == g ? 1 : 0));
  return n;
}

uint8_t earnedOfTier(const GameState& s, const Tier t) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n = static_cast<uint8_t>(n + (kInfo[i].tier == t && earned(s, i) ? 1 : 0));
  return n;
}

uint8_t countOfTier(const Tier t) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kCount; ++i) n = static_cast<uint8_t>(n + (kInfo[i].tier == t ? 1 : 0));
  return n;
}

uint8_t percent(const GameState& s) { return static_cast<uint8_t>(earnedCount(s) * 100u / kCount); }

uint8_t firstOf(const Group g) {
  for (uint8_t i = 0; i < kCount; ++i) {
    if (kInfo[i].group == g) return i;
  }
  return kCount;
}

Progress progress(const GameState& s, const uint8_t id) {
  Progress pr;
  if (id >= kCount || kInfo[id].target == 0) return pr;
  pr.need = kInfo[id].target;
  const uint32_t have = earned(s, id) ? pr.need : measure(s, static_cast<Id>(id));
  pr.have = have < pr.need ? have : pr.need;
  return pr;
}

uint32_t readingHours(const GameState& s) {
  // What this life read since the last update counts at once (the stamp of a moment earned now).
  const uint8_t* b = blk(s);
  uint32_t secs = get32(b + kSeconds);
  if (s.isCreated()) {
    const uint32_t last = get32(b + kLifeSeconds);
    const uint32_t life = s.progress.gameReadingSeconds;
    secs += life >= last ? life - last : life;
  }
  return secs / 3600u;
}

void earn(GameState& s, const Id id) { award(s, static_cast<uint8_t>(id), stampNow(s)); }

void addCount(GameState& s, const Counter c, const uint16_t n) {
  uint8_t* p = blk(s) + counterOffset(c);
  put16(p, static_cast<uint32_t>(get16(p)) + n);
}

uint32_t count(const GameState& s, const Counter c) { return get16(blk(s) + counterOffset(c)); }

void noteInfluence(GameState& s, const sects::Influence& w) {
  for (uint8_t i = 0; i < w.count && i < 2; ++i) {
    if (w.events[i].kind == sects::EventKind::CampBroken) earn(s, Id::Camp);
    if (w.events[i].kind == sects::EventKind::WarDecided) earn(s, Id::WarDecided);
  }
}

uint8_t update(GameState& s) {
  uint8_t* b = blk(s);
  const bool setUp = (b[kFlags] & kFlagSetUp) == 0;
  const uint8_t before = earnedCount(s);
  codex::observe(s);  // what this life shows is in the codex first (scroll tiers, realms, Daos)
  if (setUp) {
    // An older save: seed the totals with what it shows (this world's cleared regions and
    // guardians, the scrolls in the library), then award with the stamp "recognised".
    if (s.isCreated()) {
      if (count(s, Counter::Cleared) < clearedNow(s)) put16(b + kCleared, clearedNow(s));
      if (count(s, Counter::Guardians) < guardiansNow(s)) put16(b + kGuardians, guardiansNow(s));
      if (count(s, Counter::Books) < scrolls::count(s)) put16(b + kBooks, scrolls::count(s));
    }
  }
  catchUp(s);
  const uint16_t stamp = setUp ? kRecognised : stampNow(s);
  for (uint8_t i = 0; i < kCount; ++i) {
    const Id id = static_cast<Id>(i);
    if (earned(s, i) || eventOnly(id)) continue;
    if (measure(s, id) >= threshold(id)) award(s, i, stamp);
  }
  if (setUp) {
    b[kFlags] = static_cast<uint8_t>(b[kFlags] | kFlagSetUp);
    b[kListSize] = kCount;
  }
  const uint8_t after = earnedCount(s);
  return static_cast<uint8_t>(after - before);
}

bool isSetUp(const GameState& s) { return (blk(s)[kFlags] & kFlagSetUp) != 0; }

void lifeEnding(GameState& s, const bool oldAge, const bool abandoned) {
  update(s);
  if (oldAge) earn(s, Id::OldAge);
  if (abandoned) earn(s, Id::Abandon);
}

void restoreAfterLife(GameState& s, const uint8_t* kept) {
  memcpy(s.achievements.data(), kept, kBlockSize);
  uint8_t* b = blk(s);
  // This life's own counters start again with the next one.
  b[kRankRealm] = 0;
  put32(b + kLifeSeconds, 0);
  put16(b + kLifeBrews, 0);
  b[kLifeLongDays] = 0;
}

bool unseen(const GameState& s) { return earnedCount(s) > blk(s)[kSeen]; }
void markSeen(GameState& s) { blk(s)[kSeen] = earnedCount(s); }

}  // namespace achievements
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
