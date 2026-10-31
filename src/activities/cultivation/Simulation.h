#pragma once

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "QiNumber.h"
#include "StatsBridge.h"

// Game rules that turn reading into progress. Pure functions over GameState:
// no I/O, no UI, no clock, so every rule has a host test.
namespace cultivation {

constexpr uint8_t kMortalRealmCount = 10;   // Body Tempering .. Tribulation Transcendence
constexpr uint32_t kQiPerMinute = 10;       // DOC: base Qi per qualifying minute
constexpr uint32_t kSecondsPerMonth = 300;  // DOC: 1 in-game month per 5 reading minutes
constexpr uint8_t kStartAgeYears = 16;
constexpr uint8_t kBaseRerolls = 3;  // DOC: 3 rerolls per life (+1 per Tested Again rank, max 8)
constexpr uint8_t kMaxRolls = 9;     // 1 + 8 rerolls
constexpr uint8_t kNameMaxBytes = 24;
constexpr uint8_t kNameMaxChars = 20;
constexpr uint8_t kHiddenTraitCount = 8;

// Fairness decisions (session 1, see tools/cultivation/README.md):
constexpr uint32_t kSecondsPerStamina = 15 * 60;      // 4A: +1 stamina per 15 min read, nothing per session
constexpr uint8_t kStaminaCap = 12;                   // DOC: bank cap
constexpr uint32_t kSecondsPerStone = 60;             // 5B: 1 stone per minute read ...
constexpr uint32_t kPagesPerStone = 2;                // ... + 1 per 2 pages, at most half the minutes of the visit
constexpr uint32_t kDaoHeartFullSeconds = 15 * 3600;  // 2B: Dao Heart 100 after 15 h read, never decays
constexpr uint8_t kDaoHeartMax = 100;

struct RealmInfo {
  uint8_t stages;           // minor stages before Great Perfection
  bool layered;             // "Layer n" (realms 0-1) or Early/Middle/Late/Peak
  bool hasGreatPerfection;  // optional stage after Peak (see Breakthrough.h)
  uint16_t lifespanYears;
  uint32_t totalQi;  // Q_r = 600 * H_r * 1.6^r, from tools/cultivation/balance_sim.py
};

const RealmInfo& realmInfo(uint8_t realm);  // clamps to the last named realm

// cost(r, s) = Q_r * (1 + 0.12 s) / sum_k (1 + 0.12 k)   (DOC)
QiNumber stageCost(uint8_t realm, uint8_t stage);

// --- Destiny (Awakening Test) ------------------------------------------------

struct TalentInfo {
  uint16_t qiPermille;  // Qi multiplier, 1000 = x1.0
  uint8_t weight;       // relative chance
};
constexpr uint8_t kTalentCount = 5;
const TalentInfo& talentInfo(uint8_t talent);

struct DestinyRoll {
  Element element = Element::Earth;
  uint8_t purity = 1;  // 1..100
  uint8_t talent = 0;
};

// DOC: world seed = hash(device MAC, character creation date, life number).
uint32_t worldSeed(const uint8_t mac[6], uint32_t creationDay, uint16_t lifeNumber);
// Root and talent are one roll. Roll i is a pure function of the seed, so
// rerolling cannot be farmed by leaving and coming back.
DestinyRoll rollDestiny(uint32_t worldSeed, uint8_t rollIndex);
uint8_t rollHiddenTrait(uint32_t worldSeed);  // 1..kHiddenTraitCount

// Writes a generated name ("Lin Xueyan") into buf; always NUL-terminated.
void suggestName(uint32_t worldSeed, uint8_t index, Gender gender, char* buf, size_t len);

// Qi multiplier in permille: talent x root purity x grades x equipped manual (more sources join later).
uint32_t qiMultiplierPermille(const GameState& state);
uint16_t purityPermille(uint8_t purity);  // 850..1150

// --- Origins ------------------------------------------------------------------

// Starting gifts per origin (agreed in session 2). No origin changes the Qi
// rate. Stones and max HP are granted now; the other fields are recorded for
// the systems that will read them (grades, events, sects, alchemy).
struct OriginBonus {
  uint16_t stones;               // starting Spirit Stones
  uint8_t extraHp;               // added to HP and max HP
  uint8_t foundationGradeScore;  // added to the Foundation grade score (milestone 5)
  uint8_t eventLuck;             // added to luck checks (milestone 8)
  uint8_t sectGateRealm;         // realm that opens the sect gate (default 2, milestone 8)
  bool knowsQiPillRecipe;        // starts with one Qi pill recipe (milestone 8)
  uint8_t brewQualityPercent;    // added to brew quality (milestone 8)
};
const OriginBonus& originBonus(Origin origin);

struct OriginGift {
  uint32_t stones = 0;
  uint8_t hp = 0;
};
// Grants the origin's stones and HP once per life (CharacterBlock::kOriginGiftsGiven).
// Returns what was granted: zeros if it had been granted already.
OriginGift grantOriginGifts(GameState& state);

// --- Character creation ---------------------------------------------------------

struct CharacterDraft {
  char name[kNameMaxBytes + 1] = {};
  Gender gender = Gender::Unspecified;
  Origin origin = Origin::FarmersChild;
  DestinyRoll destiny;
  uint8_t rerollsAllowed = kBaseRerolls;
  uint8_t rerollsUsed = 0;
  uint32_t worldSeed = 0;
};

// Starts a new life at Body Tempering Layer 1 with zero Qi, however much was
// read before: takes the baseline snapshot of `now`. Grants the origin gifts.
void createCharacter(GameState& state, const CharacterDraft& draft, const StatsSample& now);

uint32_t worldSeedOf(const GameState& state);
uint16_t lifeNumberOf(const GameState& state);

// --- A visit ----------------------------------------------------------------

struct AbsorbResult {
  QiNumber qiGained;
  uint32_t monthsPassed = 0;
  uint8_t stagesGained = 0;
  uint32_t stonesGained = 0;
  uint8_t staminaGained = 0;        // after the bank cap
  uint16_t hpGained = 0;            // v0.1.9: 1 HP per minute read, up to max HP
  uint8_t consolidationGained = 0;  // in Great Perfection only
  uint8_t depthGained = 0;          // v0.2.1: at a full Peak bar outside Great Perfection
  uint32_t qiPillSeconds = 0;       // v0.2.0: reading seconds a Qi Gathering pill boosted
  bool atBottleneck = false;        // full Peak bar: needs a breakthrough
};

// Converts one visit's gains: Qi from qualifying (weekly-allowance) seconds;
// age, stamina and Dao Heart from all reading seconds; Spirit Stones from
// minutes plus pages (decision 5B). Minor stages advance automatically; at
// Peak the bar fills and stops.
AbsorbResult absorb(GameState& state, const VisitGains& gains);

// Decision 5B: 1 per whole minute in `minutes` + 1 per 2 pages, the page part
// capped at minutes / 2. `oddPage` carries one page between visits.
uint32_t stonesForVisit(uint32_t minutes, uint32_t pages, bool& oddPage);
// Decision 2B: grows linearly with reading time, full after 15 h.
uint8_t daoHeartFor(uint32_t gameReadingSeconds);

bool atBottleneck(const GameState& state);
uint16_t ageYears(const GameState& state);

}  // namespace cultivation
