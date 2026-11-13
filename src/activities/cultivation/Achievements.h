#pragma once

#include <cstdint>

#include "GameState.h"
#include "Sects.h"

// v0.3.8: achievements (agreed with the user in session 18), like Steam's or
// RetroAchievements'. For the completionist only: nothing here gives or takes
// anything in the game. They belong to the player, not to one life: kept
// through reincarnation, old age and abandoning a life (Lineage.cpp keeps the
// block). Pure rules over GameState: no I/O, no UI; names and descriptions are
// in GameStrings.inc (CULT_ACH_000 ...).
//
// Most are read from the save whenever it is written (update(), like the
// codex); a few moments the save does not keep (a duel lost, a Coward
// cornered) are earned where they happen (earn()), and a few totals across
// lives are counted here (addCount()). The first update() of a save sets the
// block up and awards what the save already proves, stamped "recognised".
//
// Ids are stable forever: never reorder, never reuse; add new ones at the end
// (room for kCapacity).
//
// Save v10 `achievements` block, 384 bytes (all zero = nothing yet):
//   0     flags: 0x01 set up (the retroactive award was made)
//   1     achievements in the list when it was set up (kCount)
//   2     earned when the list was last opened (the Lineage cell's "(New!)")
//   3     the ranking realm + 1 already counted for "rank 1" this life
//   4-7   reading seconds over all lives (from v0.3.8; seeded with the life then under way)
//   8-11  this life's gameReadingSeconds at the last update
//   12-13 books finished, 14-15 fights won, 16-17 regions cleared, 18-19 guardians beaten,
//   20-21 batches brewed (u16, all lives)
//   22-23 this life's batches begun (timers 6-7) at the last update
//   24-25 days with 60+ minutes (all lives), 26 this life's count at the last update
//   27    rankings with you at rank 1 (one per realm and life)
//   28-31 spare
//   32-51 earned bits (bit i = achievement i)
//   52-63 spare
//   64-383 when each was earned: the reading hour over all lives (u16); 0xFFFF recognised
namespace cultivation {
namespace achievements {

enum class Group : uint8_t { Path = 0, Reading, Techniques, Body, Road, Alchemy, Rivals, People, Lineage, Count };
constexpr uint8_t kGroupCount = static_cast<uint8_t>(Group::Count);
enum class Tier : uint8_t { Mortal = 0, Earth, Heaven, Immortal, Count };
constexpr uint8_t kTierCount = static_cast<uint8_t>(Tier::Count);

enum class Id : uint8_t {
  // The Path
  RealmQi = 0,
  RealmFoundation,
  RealmCore,
  RealmNascent,
  RealmSoul,
  RealmVoid,
  RealmBody,
  RealmMahayana,
  RealmTribulation,
  FirstFailure,
  FourthTime,
  Tribulation,
  HeartDemon,
  GreatPerfection,
  FullConsolidation,
  PerfectFoundation,
  HeavenlyFoundation,
  FirstGradeCore,
  FlawlessCore,
  RadiantSoul,
  ImprintedSoul,
  CrackedToCore,
  // Reading
  Hours1,
  Hours10,
  Hours50,
  Hours100,
  Hours250,
  Hours500,
  Hours1000,
  Streak3,
  Streak7,
  Streak30,
  Streak100,
  Streak365,
  LongDays10,
  LongDays50,
  Day3h,
  Day5h,
  Week14h,
  Night10,
  Morning10,
  Afternoon10,
  Evening10,
  Books1,
  Books5,
  Books10,
  Books25,
  Books50,
  Books100,
  // Techniques
  FirstScroll,
  YellowScroll,
  EarthScroll,
  HeavenScroll,
  SaintScroll,
  PerfectedScroll,
  SacredScroll,
  EquipManual,
  Library20,
  SellScroll,
  BurnScroll,
  // Body and Dao
  Meridian1,
  Meridian10,
  Meridian36,
  DaoHeart,
  Dao1,
  Dao5,
  Dao9,
  DaoLevel5,
  DaoLevel9,
  // The road
  Win1,
  Win100,
  Win500,
  Clear1,
  Clear3,
  Clear5,
  Clear10,
  Guardian1,
  Guardian10,
  Reshaped,
  Cave,
  Ruin,
  Merchant,
  Talisman,
  RoadMeeting,
  Collapse,
  // Alchemy
  Brew1,
  Experiment,
  Recipes5,
  Recipes10,
  HighPill,
  GradePill,
  Brew50,
  // Rivals
  MeetRival,
  BeatRival,
  BeatRival5,
  WinDuel,
  LoseDuel,
  RivalFriend,
  RivalBitter,
  LadderWin,
  LadderTop3,
  LadderFirst,
  LadderFirst5,
  Nemesis1,
  NemesisBeat3,
  Cornered,
  NemesisLeaves,
  Nemesis3,
  // People and the world
  Cast1,
  Cast5,
  Friend,
  Enemy,
  Mourned,
  SectFriend,
  SectEnemy,
  Camp,
  WarDecided,
  // Lineage
  Life2,
  Life3,
  Life10,
  OldAge,
  Abandon,
  Origins5,
  Talents5,
  Elements5,
  HeavenBlessed,
  ChokedCore,
  ChokedNascent,
  Memento1,
  Memento16,
  Memento32,
  Memento64,
  Legendary,
  FullSet,
  FullCodex,
  FirstPerk,
  Count
};
constexpr uint8_t kCount = static_cast<uint8_t>(Id::Count);
constexpr uint8_t kCapacity = 160;
static_assert(kCount <= kCapacity, "room for the earned bits and stamps");

constexpr uint16_t kRecognised = 0xFFFF;  // stamp: awarded from an older save's state

struct Info {
  Group group;
  Tier tier;
  bool hidden;      // "???" with a hint until earned
  uint16_t target;  // a counted one's goal (its progress is shown), 0 = plain
};
const Info& info(uint8_t id);
inline const Info& info(const Id id) { return info(static_cast<uint8_t>(id)); }
// The hidden ones in order: index of `id` among them (its hint), -1 when not hidden.
int hiddenIndex(uint8_t id);
constexpr uint8_t kHiddenCount = 16;

// --- What is earned -------------------------------------------------------------------------
bool earned(const GameState& state, uint8_t id);
inline bool earned(const GameState& state, const Id id) { return earned(state, static_cast<uint8_t>(id)); }
uint16_t earnedHour(const GameState& state, uint8_t id);  // kRecognised for the retroactive ones
uint8_t earnedCount(const GameState& state);
uint8_t earnedIn(const GameState& state, Group group);
uint8_t countIn(Group group);
uint8_t earnedOfTier(const GameState& state, Tier tier);
uint8_t countOfTier(Tier tier);
uint8_t percent(const GameState& state);  // of kCount, rounded down
// The first id of a group (ids are in group order).
uint8_t firstOf(Group group);

// A counted one's progress toward its target (have is capped at the target).
struct Progress {
  uint32_t have = 0;
  uint32_t need = 0;  // 0: not counted
};
Progress progress(const GameState& state, uint8_t id);

// The reading hour over all lives (the stamp of one earned now).
uint32_t readingHours(const GameState& state);

// --- Earning ---------------------------------------------------------------------------------
// A moment the save does not keep (stamped with the reading hour now). Does nothing once earned.
void earn(GameState& state, Id id);
// Totals over all lives.
enum class Counter : uint8_t { Books = 0, FightsWon, Cleared, Guardians };
void addCount(GameState& state, Counter counter, uint16_t n = 1);
uint32_t count(const GameState& state, Counter counter);
// What a win did to the living world (sects::Influence): a bandit camp broken, a sect war decided.
void noteInfluence(GameState& state, const sects::Influence& influence);
// Everything the save shows, and the totals that follow this life's own counters (reading time,
// brews, long days). The first call sets the block up and awards what an older save proves
// (stamped kRecognised). Cheap; called before every save. Returns how many were earned now.
uint8_t update(GameState& state);
// Whether update() has set the block up (an older save's achievements were recognised).
bool isSetUp(const GameState& state);

// The end of a life (Lineage.cpp, before the life is cleared): a last update, then old age or
// abandoning; the per-life bytes are cleared after the reset by restoreAfterLife().
void lifeEnding(GameState& state, bool oldAge, bool abandoned);
// After GameState::reset() in endLife: puts the block back, without this life's bytes.
void restoreAfterLife(GameState& state, const uint8_t* kept);
constexpr uint16_t kBlockSize = 384;

// The Lineage cell's "(New!)": earned since the list was last opened.
bool unseen(const GameState& state);
void markSeen(GameState& state);

// The bits, for the activity to see what is new since it last looked.
constexpr uint8_t kBitsOffset = 32;
constexpr uint8_t kBitBytes = kCapacity / 8;

}  // namespace achievements
}  // namespace cultivation
