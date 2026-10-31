#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cctype>
#include <cstring>

#include "Breakthrough.h"
#include "Rng.h"
#include "Scrolls.h"
#include "Simulation.h"

namespace cultivation {
namespace {

// All tables are static const so they live in flash, not DRAM.

// DOC realm table + target hours from tools/cultivation/balance_sim.py:
// H = 4.5, 10.5, 30, 52, 75, 90, 105, 120, 135, 150 h (772 h total).
static const RealmInfo kRealms[kMortalRealmCount] = {
    {9, true, false, 80, 2700},      {9, true, true, 120, 10080},     {4, false, true, 200, 46080},
    {4, false, true, 320, 127795},   {4, false, true, 500, 294912},   {4, false, true, 800, 566231},
    {4, false, true, 1200, 1056965}, {4, false, true, 2000, 1932735}, {4, false, true, 3000, 3478924},
    {4, false, true, 5000, 6184753},
};

// PROPOSED (balance_sim.py): Choked Meridians x0.6 (35%), Dull Root x0.8
// (30%), Ordinary Root x1.0 (20%), Keen Root x1.2 (11%), Heaven-Blessed Root
// x1.5 (4%). The doc names only the two ends.
static const TalentInfo kTalents[kTalentCount] = {{600, 35}, {800, 30}, {1000, 20}, {1200, 11}, {1500, 4}};

// Origins in Origin order: Farmer's child, Fallen clan heir, Street orphan,
// Sect servant, Herbalist's apprentice.
static const OriginBonus kOriginBonuses[static_cast<int>(Origin::Count)] = {
    {30, 20, 0, 0, 2, false, 0}, {100, 0, 5, 0, 2, false, 0}, {10, 0, 0, 5, 2, false, 0},
    {50, 0, 0, 0, 1, false, 0},  {40, 0, 0, 0, 2, true, 10},
};

constexpr uint32_t kKindDestiny = 1;
constexpr uint32_t kKindHiddenTrait = 2;
constexpr uint32_t kKindName = 3;

// Name pools: generated content, not UI text, so they are not translated.
static const char* const kSurnames[] = {"Lin",  "Su",  "Mo",   "Han",   "Xiao", "Ye",   "Bai",  "Gu",
                                        "Shen", "Lu",  "Qin",  "Jiang", "Wei",  "Yun",  "Feng", "Liu",
                                        "Zhao", "Chu", "Tang", "Ning",  "Xu",   "Fang", "Yan",  "Hua"};
static const char* const kGivenNeutral[] = {"Yue",  "Ming", "Chen", "Xuan", "Ran", "Qing", "Feng", "Yu",  "Lan", "An",
                                            "Ning", "Han",  "Zhi",  "Xing", "Yun", "Shu",  "Ling", "Kai", "Rui", "Jin"};
static const char* const kGivenMale[] = {"Tian", "Hao", "Lei", "Jun", "Wu", "Long", "Gang", "Zhan", "Yi", "Bo"};
static const char* const kGivenFemale[] = {"Xue", "Hua", "Mei", "Lian", "Yan", "Wan", "Ying", "Rou", "Jing", "Li"};

template <size_t N>
constexpr uint32_t countOf(const char* const (&)[N]) {
  return static_cast<uint32_t>(N);
}

uint32_t stageWeight(const uint8_t stage) { return 1000u + 120u * stage; }  // (1 + 0.12 s) in permille

uint32_t satAdd(const uint32_t a, const uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

void writeLe32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// World block layout (GameState::world): bytes 0-3 world seed, 4-5 life
// number. The remaining 31 bytes stay reserved for exploration (milestone 8).
constexpr size_t kWorldSeedOffset = 0;
constexpr size_t kLifeNumberOffset = 4;

}  // namespace

const RealmInfo& realmInfo(const uint8_t realm) {
  return kRealms[realm < kMortalRealmCount ? realm : kMortalRealmCount - 1];
}

QiNumber stageCost(const uint8_t realm, const uint8_t stage) {
  const RealmInfo& info = realmInfo(realm);
  const uint8_t s = stage < info.stages ? stage : info.stages - 1;
  uint32_t total = 0;
  for (uint8_t k = 0; k < info.stages; ++k) total += stageWeight(k);
  const uint64_t cost = static_cast<uint64_t>(info.totalQi) * stageWeight(s) / total;
  return QiNumber::fromU64(cost > 0 ? cost : 1);
}

const TalentInfo& talentInfo(const uint8_t talent) { return kTalents[talent < kTalentCount ? talent : 0]; }

uint16_t purityPermille(const uint8_t purity) {
  const uint8_t p = purity < 1 ? 1 : (purity > 100 ? 100 : purity);
  return static_cast<uint16_t>(850u + 300u * (p - 1u) / 99u);
}

const OriginBonus& originBonus(const Origin origin) {
  const int i = static_cast<int>(origin);
  return kOriginBonuses[i < static_cast<int>(Origin::Count) ? i : 0];
}

OriginGift grantOriginGifts(GameState& state) {
  OriginGift gift;
  CharacterBlock& c = state.character;
  if (!state.isCreated() || (c.flags & CharacterBlock::kOriginGiftsGiven) != 0) return gift;
  const OriginBonus& bonus = originBonus(c.origin);
  ProgressBlock& p = state.progress;
  gift.stones = bonus.stones;
  gift.hp = bonus.extraHp;
  p.stones = satAdd(p.stones, gift.stones);
  p.hpMax = static_cast<uint16_t>(p.hpMax + gift.hp);
  p.hp = static_cast<uint16_t>(p.hp + gift.hp);
  c.flags |= CharacterBlock::kOriginGiftsGiven;
  return gift;
}

uint32_t worldSeed(const uint8_t mac[6], const uint32_t creationDay, const uint16_t lifeNumber) {
  uint32_t h = 0x50495247u;  // "PIRG"
  for (int i = 0; i < 6; ++i) h = mix32(h, mac[i]);
  h = mix32(h, creationDay);
  h = mix32(h, lifeNumber);
  return h != 0 ? h : 1;
}

DestinyRoll rollDestiny(const uint32_t seed, const uint8_t rollIndex) {
  Rng rng(contentSeed(seed, kKindDestiny, rollIndex));
  DestinyRoll roll;
  roll.element = static_cast<Element>(rng.below(static_cast<uint32_t>(Element::Count)));
  roll.purity = static_cast<uint8_t>(rng.range(1, 100));
  uint32_t totalWeight = 0;
  for (const auto& t : kTalents) totalWeight += t.weight;
  uint32_t pick = rng.below(totalWeight);
  for (uint8_t i = 0; i < kTalentCount; ++i) {
    if (pick < kTalents[i].weight) {
      roll.talent = i;
      break;
    }
    pick -= kTalents[i].weight;
  }
  return roll;
}

uint8_t rollHiddenTrait(const uint32_t seed) {
  Rng rng(contentSeed(seed, kKindHiddenTrait, 0));
  return static_cast<uint8_t>(rng.range(1, kHiddenTraitCount));
}

void suggestName(const uint32_t seed, const uint8_t index, const Gender gender, char* buf, const size_t len) {
  if (buf == nullptr || len == 0) return;
  Rng rng(contentSeed(seed, kKindName, index));
  const char* surname = kSurnames[rng.below(countOf(kSurnames))];

  // Male and female names lean on their pools; everyone can draw neutral ones.
  auto pickGiven = [&]() -> const char* {
    const uint32_t roll = rng.below(3);
    if (roll == 0 && gender == Gender::Male) return kGivenMale[rng.below(countOf(kGivenMale))];
    if (roll == 0 && gender == Gender::Female) return kGivenFemale[rng.below(countOf(kGivenFemale))];
    return kGivenNeutral[rng.below(countOf(kGivenNeutral))];
  };
  const char* first = pickGiven();
  const char* second = rng.below(2) == 0 ? pickGiven() : nullptr;

  size_t out = 0;
  auto put = [&](const char* text, const bool lowerFirst) {
    for (size_t i = 0; text[i] != '\0' && out + 1 < len; ++i) {
      const char c = (i == 0 && lowerFirst) ? static_cast<char>(tolower(static_cast<unsigned char>(text[i]))) : text[i];
      buf[out++] = c;
    }
  };
  put(surname, false);
  put(" ", false);
  put(first, false);
  if (second != nullptr && strcmp(second, first) != 0) put(second, true);  // "Xue" + "yan" -> "Xueyan"
  buf[out] = '\0';
}

uint32_t qiMultiplierPermille(const GameState& state) {
  const uint32_t talent = talentInfo(state.character.talent).qiPermille;
  const uint32_t root = talent * purityPermille(state.character.rootPurity) / 1000u;
  const uint32_t graded = root * breakthrough::gradeQiPermille(state) / 1000u;  // Foundation and Golden Core grades
  return static_cast<uint32_t>(static_cast<uint64_t>(graded) * scrolls::equippedManualPermille(state) / 1000u);
}

void createCharacter(GameState& state, const CharacterDraft& draft, const StatsSample& now) {
  state.reset();
  state.snapshot = bridge::takeBaseline(now);
  state.baseline = bridge::baselineTotals(now);

  CharacterBlock& c = state.character;
  const size_t nameLen = strnlen(draft.name, kNameMaxBytes);
  memcpy(c.name.data(), draft.name, nameLen);
  c.gender = draft.gender;
  c.origin = draft.origin;
  c.rootElement = draft.destiny.element;
  c.rootSecondary = draft.destiny.element;
  c.rootPurity = draft.destiny.purity;
  c.talent = draft.destiny.talent;
  c.hiddenTrait = rollHiddenTrait(draft.worldSeed);
  c.rerollsAllowed = draft.rerollsAllowed;
  c.rerollsUsed = draft.rerollsUsed;
  c.creationDay = now.today;
  c.flags = CharacterBlock::kCharacterCreated;
  grantOriginGifts(state);

  writeLe32(state.world.data() + kWorldSeedOffset, draft.worldSeed);
  state.world[kLifeNumberOffset] = 1;  // first life
  state.world[kLifeNumberOffset + 1] = 0;
}

uint32_t worldSeedOf(const GameState& state) { return readLe32(state.world.data() + kWorldSeedOffset); }

uint16_t lifeNumberOf(const GameState& state) {
  return static_cast<uint16_t>(state.world[kLifeNumberOffset] | (state.world[kLifeNumberOffset + 1] << 8));
}

bool atBottleneck(const GameState& state) {
  const RealmInfo& info = realmInfo(state.progress.realm);
  return state.progress.stage + 1 >= info.stages &&
         state.progress.qi >= stageCost(state.progress.realm, state.progress.stage);
}

uint32_t stonesForVisit(const uint32_t minutes, const uint32_t pages, bool& oddPage) {
  const uint32_t pageTotal = satAdd(pages, oddPage ? 1u : 0u);
  const uint32_t limit = minutes / 2;
  uint32_t pageStones = pageTotal / kPagesPerStone;
  if (pageStones >= limit) {
    pageStones = limit;
    oddPage = false;  // pages past the limit are not banked: auto page-turns earn nothing extra
  } else {
    oddPage = (pageTotal % kPagesPerStone) != 0;
  }
  return satAdd(minutes, pageStones);
}

uint8_t daoHeartFor(const uint32_t gameReadingSeconds) {
  if (gameReadingSeconds >= kDaoHeartFullSeconds) return kDaoHeartMax;
  return static_cast<uint8_t>(static_cast<uint64_t>(gameReadingSeconds) * kDaoHeartMax / kDaoHeartFullSeconds);
}

uint16_t ageYears(const GameState& state) { return static_cast<uint16_t>(state.progress.ageMonths / 12u); }

AbsorbResult absorb(GameState& state, const VisitGains& gains) {
  AbsorbResult r;
  if (gains.resetDetected) {
    r.atBottleneck = atBottleneck(state);
    return r;
  }
  ProgressBlock& p = state.progress;

  // Qi = qualifying minutes x 10 x M. Kept exact with a numerator carry:
  // numerator units are Qi * 60 s * 1000 permille.
  const uint64_t numerator =
      static_cast<uint64_t>(gains.tiers.qualifyingSeconds()) * kQiPerMinute * qiMultiplierPermille(state) + p.qiCarry;
  constexpr uint64_t kPerQi = 60ull * 1000ull;
  r.qiGained = QiNumber::fromU64(numerator / kPerQi);
  p.qiCarry = static_cast<uint32_t>(numerator % kPerQi);

  // Time passes with every reading minute, capped or not (DOC: 1 year per reading hour).
  const uint32_t ageSeconds = satAdd(gains.seconds, p.ageSecondsCarry);
  r.monthsPassed = ageSeconds / kSecondsPerMonth;
  p.ageSecondsCarry = static_cast<uint16_t>(ageSeconds % kSecondsPerMonth);
  p.ageMonths = satAdd(p.ageMonths, r.monthsPassed);

  // Stones, stamina and Dao Heart all count whole steps of this character's
  // total reading time, so no extra carry fields are needed: the remainder is
  // gameReadingSeconds itself.
  const uint32_t before = p.gameReadingSeconds;
  p.gameReadingSeconds = satAdd(p.gameReadingSeconds, gains.seconds);
  const uint32_t after = p.gameReadingSeconds;

  bool oddPage = (p.progressFlags & ProgressBlock::kOddPageCarry) != 0;
  r.stonesGained = stonesForVisit(after / kSecondsPerStone - before / kSecondsPerStone, gains.pages, oddPage);
  p.progressFlags = static_cast<uint8_t>(oddPage ? (p.progressFlags | ProgressBlock::kOddPageCarry)
                                                 : (p.progressFlags & ~ProgressBlock::kOddPageCarry));
  p.stones = satAdd(p.stones, r.stonesGained);

  const uint32_t staminaSteps = after / kSecondsPerStamina - before / kSecondsPerStamina;
  const uint8_t room = p.stamina < kStaminaCap ? static_cast<uint8_t>(kStaminaCap - p.stamina) : 0;
  r.staminaGained = static_cast<uint8_t>(staminaSteps < room ? staminaSteps : room);
  p.stamina = static_cast<uint8_t>(p.stamina + r.staminaGained);

  p.daoHeart = daoHeartFor(after);

  // Great Perfection: 2 consolidation points per reading hour, up to 40.
  r.consolidationGained = breakthrough::consolidationGained(state, before, after);
  p.consolidation = static_cast<uint8_t>(p.consolidation + r.consolidationGained);

  // Fill the bar; minor stages advance on their own, Peak waits for a breakthrough.
  if (p.realm >= kMortalRealmCount) p.realm = kMortalRealmCount - 1;
  p.qi.add(r.qiGained);
  const RealmInfo& info = realmInfo(p.realm);
  while (true) {
    const QiNumber cost = stageCost(p.realm, p.stage);
    if (p.qi < cost) break;
    if (p.stage + 1 < info.stages) {
      p.qi.subtract(cost);
      ++p.stage;
      ++r.stagesGained;
    } else {
      // Bottleneck: the bar stays full until a breakthrough. Qi beyond it is
      // not kept; in Great Perfection the reading turns into consolidation.
      p.qi = cost;
      break;
    }
  }
  r.atBottleneck = atBottleneck(state);
  return r;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
