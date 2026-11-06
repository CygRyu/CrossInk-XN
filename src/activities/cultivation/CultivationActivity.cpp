#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <esp_err.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Breakthrough.h"
#include "Chronicle.h"
#include "Codex.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "CultivatorSleepScreen.h"
#include "Days.h"
#include "GameText.h"
#include "GameValues.h"
#include "MappedInputManager.h"
#include "Meridians.h"
#include "Rng.h"
#include "StatsBridge.h"
#include "StatsSource.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

using namespace cultivation;

namespace {

// GlobalReadingStats::localResetRevision() only means something within one
// boot, so the last value seen lives in RAM (8 bytes), not in the save.
RevisionWatch s_revisionWatch;
// v0.3.1: false until the first visit of this boot. A wake is a new boot (the
// X3 and X4 restart from deep sleep), so the first visit after one has slept.
bool s_visitedThisBoot = false;

constexpr char kDamagedSavePath[] = "/.crosspoint/cultivation/save.damaged.bin";

// Text tables: static const, so they stay in flash.
static const GameStr kRealmNames[kMortalRealmCount] = {
    GameStr::CULT_REALM_0, GameStr::CULT_REALM_1, GameStr::CULT_REALM_2, GameStr::CULT_REALM_3, GameStr::CULT_REALM_4,
    GameStr::CULT_REALM_5, GameStr::CULT_REALM_6, GameStr::CULT_REALM_7, GameStr::CULT_REALM_8, GameStr::CULT_REALM_9};
static const GameStr kStageNames[4] = {GameStr::CULT_STAGE_EARLY, GameStr::CULT_STAGE_MIDDLE, GameStr::CULT_STAGE_LATE,
                                       GameStr::CULT_STAGE_PEAK};
static const GameStr kGenderNames[static_cast<int>(Gender::Count)] = {
    GameStr::CULT_GENDER_MALE, GameStr::CULT_GENDER_FEMALE, GameStr::CULT_GENDER_NONBINARY,
    GameStr::CULT_GENDER_UNSPECIFIED};
static const GameStr kOriginNames[static_cast<int>(Origin::Count)] = {
    GameStr::CULT_ORIGIN_FARMER, GameStr::CULT_ORIGIN_CLAN_HEIR, GameStr::CULT_ORIGIN_ORPHAN,
    GameStr::CULT_ORIGIN_SECT_SERVANT, GameStr::CULT_ORIGIN_HERBALIST};
static const GameStr kBirthLines[static_cast<int>(Origin::Count)] = {
    GameStr::CULT_BIRTH_FARMER, GameStr::CULT_BIRTH_CLAN_HEIR, GameStr::CULT_BIRTH_ORPHAN,
    GameStr::CULT_BIRTH_SECT_SERVANT, GameStr::CULT_BIRTH_HERBALIST};
static const GameStr kOriginGifts[static_cast<int>(Origin::Count)] = {
    GameStr::CULT_GIFT_FARMER, GameStr::CULT_GIFT_CLAN_HEIR, GameStr::CULT_GIFT_ORPHAN, GameStr::CULT_GIFT_SECT_SERVANT,
    GameStr::CULT_GIFT_HERBALIST};
static const GameStr kElementNames[static_cast<int>(Element::Count)] = {
    GameStr::CULT_ELEMENT_WOOD, GameStr::CULT_ELEMENT_FIRE, GameStr::CULT_ELEMENT_EARTH, GameStr::CULT_ELEMENT_METAL,
    GameStr::CULT_ELEMENT_WATER};
static const GameStr kElementColors[static_cast<int>(Element::Count)] = {
    GameStr::CULT_COLOR_WOOD, GameStr::CULT_COLOR_FIRE, GameStr::CULT_COLOR_EARTH, GameStr::CULT_COLOR_METAL,
    GameStr::CULT_COLOR_WATER};
static const GameStr kFoundationGrades[5] = {GameStr::CULT_FOUNDATION_1, GameStr::CULT_FOUNDATION_2,
                                             GameStr::CULT_FOUNDATION_3, GameStr::CULT_FOUNDATION_4,
                                             GameStr::CULT_FOUNDATION_5};
static const GameStr kCoreGrades[10] = {
    GameStr::CULT_CORE_1, GameStr::CULT_CORE_2, GameStr::CULT_CORE_3, GameStr::CULT_CORE_4, GameStr::CULT_CORE_5,
    GameStr::CULT_CORE_6, GameStr::CULT_CORE_7, GameStr::CULT_CORE_8, GameStr::CULT_CORE_9, GameStr::CULT_CORE_10};
static const GameStr kSoulClarities[4] = {GameStr::CULT_SOUL_1, GameStr::CULT_SOUL_2, GameStr::CULT_SOUL_3,
                                          GameStr::CULT_SOUL_4};
constexpr uint32_t kKindOutcomeLine = 56;  // contentSeed kinds: 55 is the breakthrough roll
constexpr uint32_t kKindGreatPerfectionLine = 57;

static const GameStr kTalentNames[kTalentCount] = {GameStr::CULT_TALENT_CHOKED, GameStr::CULT_TALENT_DULL,
                                                   GameStr::CULT_TALENT_ORDINARY, GameStr::CULT_TALENT_KEEN,
                                                   GameStr::CULT_TALENT_HEAVEN_BLESSED};

constexpr int kNameRows = 3;  // Name, Suggest, Continue
constexpr int kNameRowName = 0;
constexpr int kNameRowSuggest = 1;

const char* text(const GameStr id) { return gameText(id); }

// "x0.60" style multiplier from permille.
void formatRate(char* buf, const size_t len, const uint32_t permille) {
  snprintf(buf, len, ctr(CULT_QI_RATE_FORMAT), static_cast<unsigned>(permille / 1000),
           static_cast<unsigned>((permille % 1000) / 10));
}

uint32_t destinyPermille(const DestinyRoll& roll) {
  return static_cast<uint32_t>(talentInfo(roll.talent).qiPermille) * purityPermille(roll.purity) / 1000u;
}

// DOC: "Only characters the UI font can draw are accepted." Trims spaces,
// then requires 1-20 characters, none of them control characters, all
// present in the bold UI font used to show the name.
bool normalizeName(const GfxRenderer& renderer, const std::string& input, char out[kNameMaxBytes + 1]) {
  size_t start = 0;
  size_t end = input.size();
  while (start < end && input[start] == ' ') ++start;
  while (end > start && input[end - 1] == ' ') --end;
  const size_t len = end - start;
  if (len == 0 || len > kNameMaxBytes) return false;

  char candidate[kNameMaxBytes + 1] = {};
  memcpy(candidate, input.data() + start, len);

  const auto& fonts = renderer.getFontMap();
  const auto font = fonts.find(UI_12_FONT_ID);
  const auto* cursor = reinterpret_cast<const unsigned char*>(candidate);
  int chars = 0;
  while (*cursor != 0) {
    const uint32_t cp = utf8NextCodepoint(&cursor);
    if (cp < 0x20 || cp == 0x7F || cp == REPLACEMENT_GLYPH) return false;
    if (font != fonts.end() && !font->second.hasCodepoint(cp, EpdFontFamily::BOLD)) return false;
    if (++chars > kNameMaxChars) return false;
  }
  memcpy(out, candidate, len + 1);
  return true;
}

// The save's name field holds up to 24 bytes without a terminator.
void nameOf(const GameState& state, char out[kNameMaxBytes + 1]) {
  memcpy(out, state.character.name.data(), kNameMaxBytes);
  out[kNameMaxBytes] = '\0';
}

}  // namespace

namespace cultivation {
namespace ui {
const char* realmName(const uint8_t realm) { return text(kRealmNames[realm < kMortalRealmCount ? realm : 0]); }
const char* elementName(const Element e) {
  return text(kElementNames[static_cast<int>(e) < static_cast<int>(Element::Count) ? static_cast<int>(e) : 2]);
}
const char* gradeName(const breakthrough::GradeKind kind, const uint8_t grade) {
  switch (kind) {
    case breakthrough::GradeKind::Foundation:
      return grade >= 1 && grade <= 5 ? text(kFoundationGrades[grade - 1]) : "";
    case breakthrough::GradeKind::Core:
      return grade >= 1 && grade <= 10 ? text(kCoreGrades[grade - 1]) : "";
    case breakthrough::GradeKind::Soul:
      return grade >= 1 && grade <= 4 ? text(kSoulClarities[grade - 1]) : "";
    default:
      return "";
  }
}
void formatTitles(const GameState& s, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  const ProgressBlock& p = s.progress;
  auto add = [&](const char* title) {
    if (title == nullptr || title[0] == '\0') return;
    const size_t used = strlen(out);
    snprintf(out + used, len - used, "%s%s", used > 0 ? ", " : "", title);
  };
  add(gradeName(breakthrough::GradeKind::Foundation, p.foundationGrade));
  add(gradeName(breakthrough::GradeKind::Core, p.coreGrade));
  add(gradeName(breakthrough::GradeKind::Soul, p.soulClarity));
  if (breakthrough::isHalfStep(s) && p.realm + 1 < kMortalRealmCount) {
    char halfStep[64];
    snprintf(halfStep, sizeof(halfStep), ctr(CULT_HALF_STEP_FORMAT), text(kRealmNames[p.realm + 1]));
    add(halfStep);
  }
  char ladder[64];  // v0.2.8: the top three of the realm's ranking, until the next realm
  ladderTitle(s, ladder, sizeof(ladder));
  add(ladder);
}
void stageName(const GameState& s, char* out, const size_t len) {
  const ProgressBlock& p = s.progress;
  if (breakthrough::inGreatPerfection(s)) {
    snprintf(out, len, "%s", ctr(CULT_GREAT_PERFECTION));
  } else if (realmInfo(p.realm).layered) {
    snprintf(out, len, ctr(CULT_LAYER_FORMAT), static_cast<unsigned>(p.stage + 1));
  } else {
    snprintf(out, len, "%s", text(kStageNames[p.stage < 4 ? p.stage : 3]));
  }
}
void stageNameOf(const uint8_t realm, const uint8_t stage, char* out, const size_t len) {
  if (realmInfo(realm).layered) {
    snprintf(out, len, ctr(CULT_LAYER_FORMAT), static_cast<unsigned>(stage + 1));
  } else {
    snprintf(out, len, "%s", text(kStageNames[stage < 4 ? stage : 3]));
  }
}
void versionLine(char* out, const size_t len) {
  snprintf(out, len, ctr(CULT_VERSION_FORMAT), CULTIVATION_GAME_VERSION);
}
void formatPermille(char* buf, const size_t len, const uint32_t permille) {
  snprintf(buf, len, "x%u.%02u", static_cast<unsigned>(permille / 1000), static_cast<unsigned>((permille % 1000) / 10));
}

const char* gradeWord(const inventory::Grade g) {
  switch (g) {
    case inventory::Grade::Spirit:
      return ctr(CULT_GRADE_SPIRIT);
    case inventory::Grade::Rare:
      return ctr(CULT_GRADE_RARE);
    default:
      return ctr(CULT_GRADE_COMMON);
  }
}

const char* pillName(const alchemy::Pill pill) {
  using alchemy::Pill;
  switch (pill) {
    case Pill::QiGathering:
      return ctr(CULT_PILL_QI);
    case Pill::TonicMinor:
      return ctr(CULT_ITEM_TONIC_MINOR);
    case Pill::BreakthroughCommon:
      return ctr(CULT_PILL_BT_COMMON);
    case Pill::FoundationBuilding:
      return ctr(CULT_PILL_FOUNDATION);
    case Pill::CoreForming:
      return ctr(CULT_PILL_CORE);
    case Pill::TonicGreater:
      return ctr(CULT_ITEM_TONIC_GREATER);
    case Pill::SoulClearing:
      return ctr(CULT_PILL_SOUL);
    case Pill::BreakthroughSpirit:
      return ctr(CULT_PILL_BT_SPIRIT);
    case Pill::TonicFull:
      return ctr(CULT_ITEM_TONIC_FULL);
    case Pill::BreakthroughRare:
      return ctr(CULT_PILL_BT_RARE);
    default:
      return "";
  }
}

namespace {
const char* qualityWord(const inventory::Quality q) {
  switch (q) {
    case inventory::Quality::High:
      return ctr(CULT_PILL_HIGH);
    case inventory::Quality::Medium:
      return ctr(CULT_PILL_MEDIUM);
    default:
      return ctr(CULT_PILL_LOW);
  }
}
// "Core-Forming pill · high", with the first letter capitalised.
void withQuality(const char* base, const inventory::Quality q, char* out, const size_t len) {
  snprintf(out, len, ctr(CULT_PILL_QUALITY_FORMAT), base, qualityWord(q));
  if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
}
static const alchemy::Pill kBreakthroughPills[3] = {alchemy::Pill::BreakthroughCommon,
                                                    alchemy::Pill::BreakthroughSpirit, alchemy::Pill::BreakthroughRare};
static const alchemy::Pill kGradePills[3] = {alchemy::Pill::FoundationBuilding, alchemy::Pill::CoreForming,
                                             alchemy::Pill::SoulClearing};
}  // namespace

void itemName(const inventory::Item item, char* out, const size_t len) {
  using inventory::Item;
  out[0] = '\0';
  if (inventory::isHerb(item)) {
    snprintf(out, len, ctr(CULT_ITEM_HERB_FALLBACK_FORMAT), static_cast<unsigned>(inventory::regionOf(item) + 1));
  } else if (inventory::isOre(item)) {
    snprintf(out, len, ctr(CULT_ITEM_ORE_FALLBACK_FORMAT), static_cast<unsigned>(inventory::regionOf(item) + 1));
  } else if (inventory::isBreakthroughPill(item)) {
    withQuality(pillName(kBreakthroughPills[inventory::pillGroup(item)]), inventory::pillQuality(item), out, len);
  } else if (inventory::isGradePill(item)) {
    withQuality(pillName(kGradePills[inventory::pillGroup(item)]), inventory::pillQuality(item), out, len);
  } else {
    const char* name = "";
    switch (item) {
      case Item::TonicMinor:
        name = ctr(CULT_ITEM_TONIC_MINOR);
        break;
      case Item::TonicGreater:
        name = ctr(CULT_ITEM_TONIC_GREATER);
        break;
      case Item::TonicFull:
        name = ctr(CULT_ITEM_TONIC_FULL);
        break;
      case Item::ThunderTalisman:
        name = ctr(CULT_ITEM_THUNDER);
        break;
      case Item::CalmIncense:
        name = ctr(CULT_ITEM_INCENSE);
        break;
      case Item::RegionTalisman:
        name = ctr(CULT_ITEM_REGION);
        break;
      case Item::QiPill:
        name = ctr(CULT_PILL_QI);
        break;
      case Item::RecipeSlip:
        name = ctr(CULT_ITEM_SLIP);
        break;
      case Item::Dross:
        name = ctr(CULT_ITEM_DROSS);
        break;
      default:
        break;
    }
    snprintf(out, len, "%s", name);
  }
}

void itemDescription(const inventory::Item item, char* out, const size_t len) {
  using inventory::Item;
  out[0] = '\0';
  if (inventory::isIngredient(item)) {
    snprintf(out, len, inventory::isHerb(item) ? ctr(CULT_ITEM_HERB_DESC_FORMAT) : ctr(CULT_ITEM_ORE_DESC_FORMAT),
             gradeWord(inventory::gradeOfRegion(inventory::regionOf(item))));
    return;
  }
  if (inventory::isBreakthroughPill(item)) {
    const unsigned bonus = alchemy::breakthroughBonus(item);
    const uint8_t group = inventory::pillGroup(item);
    if (group >= 2) {
      snprintf(out, len, ctr(CULT_ITEM_BT_DESC_ANY_FORMAT), bonus);
    } else {
      snprintf(out, len, ctr(CULT_ITEM_BT_DESC_FORMAT), bonus, realmName(group == 0 ? 4 : 7));
    }
    return;
  }
  if (inventory::isGradePill(item)) {
    static const GameStr kKinds[3] = {GameStr::CULT_GRADE_KIND_FOUNDATION, GameStr::CULT_GRADE_KIND_CORE,
                                      GameStr::CULT_GRADE_KIND_SOUL};
    snprintf(out, len, ctr(CULT_ITEM_GRADE_DESC_FORMAT), static_cast<unsigned>(alchemy::gradePillPoints(item)),
             gameText(kKinds[inventory::pillGroup(item)]));
    return;
  }
  switch (item) {
    case Item::TonicMinor:
    case Item::TonicGreater:
    case Item::TonicFull:
      snprintf(out, len, ctr(CULT_ITEM_TONIC_DESC_FORMAT), static_cast<unsigned>(inventory::healPercent(item)));
      return;
    case Item::ThunderTalisman:
      snprintf(out, len, "%s", ctr(CULT_ITEM_THUNDER_DESC));
      return;
    case Item::CalmIncense:
      snprintf(out, len, "%s", ctr(CULT_ITEM_INCENSE_DESC));
      return;
    case Item::RegionTalisman:
      snprintf(out, len, "%s", ctr(CULT_ITEM_REGION_DESC));
      return;
    case Item::QiPill:
      snprintf(out, len, "%s", ctr(CULT_ITEM_QI_DESC));
      return;
    case Item::Dross:
      snprintf(out, len, "%s", ctr(CULT_ITEM_DROSS_DESC));
      return;
    default:
      return;
  }
}
}  // namespace ui
}  // namespace cultivation

// --- Lifecycle ------------------------------------------------------------------

void CultivationActivity::onEnter() {
  Activity::onEnter();
  ignoreInitialBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignoreInitialConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialPowerRelease_ = mappedInput.isPressed(MappedInputManager::Button::Power);
  previousOrientation_ = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  state_ = makeUniqueNoThrow<GameState>();
  if (!state_) {
    LOG_ERR("CULT", "Out of memory allocating GameState (%u bytes)", static_cast<unsigned>(sizeof(GameState)));
    screen_ = Screen::Error;
    requestUpdate();
    return;
  }

  loadPacks();

  switch (save::load(*state_)) {
    case save::LoadResult::Loaded:
      if (state_->isCreated()) {
        startVisit();
      } else {
        startCreation();
      }
      break;
    case save::LoadResult::NoSave:
      startCreation();
      break;
    case save::LoadResult::Corrupt:
      LOG_ERR("CULT", "Save unreadable; it will be kept as %s", kDamagedSavePath);
      damagedSave_ = true;
      startCreation();
      break;
  }
  requestUpdate();
}

void CultivationActivity::onExit() {
  explore_.reset();
  rivals_.reset();
  lineage_.reset();
  alchemy_.reset();
  ingredients_.reset();
  library_.reset();
  news_.reset();
  chronicle_.reset();
  lifeLog_.reset();
  grammar_.reset();  // the engine reads through packs_, so it goes first
  packs_.reset();
  state_.reset();  // frees the ~1 KB GameState
  renderer.setOrientation(previousOrientation_);
  Activity::onExit();
}

void CultivationActivity::loadPacks() {
  packs_ = makeUniqueNoThrow<PackStore>();
  if (!packs_ || !packs_->begin()) {
    LOG_ERR("CULT", "Content packs unavailable (out of memory); continuing without generated text");
    packs_.reset();
    return;
  }
  grammar_ = makeUniqueNoThrow<Grammar>(packs_->index(), *packs_);
  if (!grammar_) {
    LOG_ERR("CULT", "Out of memory for the grammar engine (%u bytes)", static_cast<unsigned>(sizeof(Grammar)));
  }
}

// One rumor per day (DOC: "Rumor of the day"): seeded by the world and the
// date, so it stays the same all day and differs between worlds. Without a
// clock it follows the reading day (v0.3.1), or before any, each reading hour.
void CultivationActivity::updateRumor() {
  rumor_[0] = '\0';
  if (!grammar_ || !state_ || !state_->isCreated()) return;
  constexpr uint32_t kKindRumor = 50;
  uint32_t day = state_->snapshot.lastVisitDay;
  if (day == StatsSnapshot::kNoDay && days::inUse(*state_) && daily::live(*state_)) day = daily::today(*state_);
  const uint32_t when = day != StatsSnapshot::kNoDay ? day : 0x80000000u + state_->progress.gameReadingSeconds / 3600u;
  GrammarContext ctx;
  ctx.plane = 0;  // the Mortal Plane until ascension exists
  ctx.values = gameValues(*state_);
  grammar_->beginScene();
  // v0.2.9: one day in three, a rumor of your own realm (Nascent Soul's list serves the later realms too).
  constexpr uint32_t kKindRealmRumor = 129;
  static const char* const kRealmRumors[] = {"{rumor.opening} {rumor.realm1}.", "{rumor.opening} {rumor.realm2}.",
                                             "{rumor.opening} {rumor.realm3}.", "{rumor.opening} {rumor.realm4}."};
  static const char* const kRealmLists[] = {"rumor.realm1", "rumor.realm2", "rumor.realm3", "rumor.realm4"};
  const char* pattern = "{rumor}";
  const uint8_t realm = state_->progress.realm;
  if (realm >= 1) {
    const uint8_t i = static_cast<uint8_t>((realm < 4 ? realm : 4) - 1);
    if (Rng(contentSeed(worldSeedOf(*state_), kKindRealmRumor, when)).below(3) == 0 &&
        grammar_->hasKey(kRealmLists[i], ctx)) {
      pattern = kRealmRumors[i];
    }
  }
  const char* text = grammar_->expand(pattern, contentSeed(worldSeedOf(*state_), kKindRumor, when), ctx);
  snprintf(rumor_, sizeof(rumor_), "%s", text);
  packs_->closeFile();
  // v0.3.2: one day in three, a live story of the living world.
  worldRumorOfTheDay(when);
  if (packs_) packs_->closeFile();
  // v0.2.8: one day in four the rumor is about a nemesis (and tells you one of its ways).
  if (day != StatsSnapshot::kNoDay) nemesisRumor(day);
  if (grammar_->missing() > 0) {
    LOG_ERR("CULT", "Rumor had %u missing key(s)", grammar_->missing());
  }
  if (Storage.exists(sleepscreen::kOldRumorPath)) Storage.remove(sleepscreen::kOldRumorPath);  // v0.1.8 leftover
}

bool CultivationActivity::persist() {
  codex::observe(*state_);  // v0.3.0: the codex follows what the life shows
  saveFailed_ = !save::store(*state_);
  if (saveFailed_) {
    LOG_ERR("CULT", "Saving the game failed");
  }
  return !saveFailed_;
}

// One visit: convert reading since the last visit, then save. If the save
// fails, the snapshot on the card is unchanged too, so the same reading is
// simply counted again next time: nothing is lost or doubled.
void CultivationActivity::startVisit() {
  StatsSample now = readLocalStats();
  const uint32_t lastVisitDay = state_->snapshot.lastVisitDay;  // v0.2.6: the daily systems need it
  const VisitGains gains = bridge::collect(state_->snapshot, now, s_revisionWatch);
  originGift_ = grantOriginGifts(*state_);  // only for characters made before origin gifts existed
  setUpSeenFlags();                         // v0.2.7: saved with the visit
  // v0.2.0: a v0.1.10 bag moves to the new one; an older Herbalist learns the Qi Gathering recipe.
  if (inventory::migrateLegacyBag(*state_)) LOG_INF("CULT", "Moved the v0.1.10 bag to the v0.2.0 one");
  alchemy::grantStartingRecipe(*state_);
  lifelog::VisitChange change;
  change.ageMonthsBefore = state_->progress.ageMonths;
  const lineage::Age ageBefore = lineage::ageState(*state_);
  const bool bottleneckBefore = atBottleneck(*state_);
  visit_ = absorb(*state_, gains);
  change.ageMonthsAfter = state_->progress.ageMonths;
  // v0.3.0: a line when the years left cross 80% or 95% of the realm's lifespan.
  {
    const lineage::Age ageAfter = lineage::ageState(*state_);
    ageWarning_ = ageAfter != ageBefore && (ageAfter == lineage::Age::Greying || ageAfter == lineage::Age::Failing)
                      ? ageAfter
                      : lineage::Age::Fine;
  }
  change.stagesGained = visit_.stagesGained;
  // Only the first time the bar fills in a realm: after a failed breakthrough,
  // refilling it is not news (found in the simulator).
  change.reachedBottleneck = !bottleneckBefore && atBottleneck(*state_) && state_->progress.failures == 0;
  // v0.3.1: without a date, the reading day (after the allowance, which stays off without a date).
  const days::Result dayResult = days::resolve(*state_, now, !s_visitedThisBoot);
  s_visitedThisBoot = true;
  if (dayResult.rolled || dayResult.switched) {
    LOG_INF("CULT", "Day: %s %lu%s, shift %ld", dayResult.readingDays ? "reading day" : "calendar day",
            static_cast<unsigned long>(now.today), dayResult.rolled ? " (new)" : "",
            static_cast<long>(dayResult.shift));
  }
  // v0.2.6: the day's omen and tasks, the streak, time of day for the Daos, then any Dao found.
  daily::update(*state_, gains, now, lastVisitDay);
  daos::addReading(*state_, gains, state_->snapshot);
  daoNews_ = daos::settle(*state_);
  // v0.2.8: the rival's days since the last visit, the ranking's realm, nemeses roaming on reading time.
  rivals::setClockHour(currentLocalHour());
  rivalNews_ = rivals::update(*state_, now);
  rivals::roam(*state_);
  // v0.3.2: the living world takes one step per reading hour (before the regions tick).
  worldSteps_ = sects::advance(*state_);
  worldSummary_ = -1;
  if (worldSteps_.years > 0) {
    LOG_INF("CULT", "World: %u years, %u events", static_cast<unsigned>(worldSteps_.years),
            static_cast<unsigned>(worldSteps_.count + worldSteps_.dropped));
  }
  // v0.1.10: herbs regrow and cleared regions reshape on reading time.
  upheavals_ = explore::tick(*state_);
  updateRumor();
  visitSeconds_ = gains.seconds;
  visitReset_ = gains.resetDetected;
  LOG_INF("CULT", "Visit: %lu s read, %lu qualifying, %lu pages, reset=%d, stages+%u, stones+%lu, stamina+%u",
          static_cast<unsigned long>(gains.seconds), static_cast<unsigned long>(gains.tiers.qualifyingSeconds()),
          static_cast<unsigned long>(gains.pages), gains.resetDetected ? 1 : 0,
          static_cast<unsigned>(visit_.stagesGained), static_cast<unsigned long>(visit_.stonesGained),
          static_cast<unsigned>(visit_.staminaGained));
  // The Chronicle is written before the save: if the save fails, the next
  // visit rebuilds the same lines and chronicle::appendVisit() skips them.
  const bool summary = visitSeconds_ > 0 && !visitReset_;
  if (summary) {
    writeLifeLog(change);
  } else {
    appendUpheavalLines(upheavals_);  // straight into the Chronicle
    appendRivalLines(rivalNews_);
    appendWorldChronicleLines();  // v0.3.2
  }
  // Books finished since the last visit become scrolls (Chronicle lines first,
  // like the life log). minted.bin and the pending list are only updated
  // once the save holds the new scrolls, so a failed save mints them again.
  writeDaoNews(daoNews_);  // Chronicle lines before the save, like the life log
  auto minting = mintScrolls();
  persist();
  if (minting && !saveFailed_) finishMinting(*minting);
  minting.reset();
  chronicleExists_ = chronicle::exists();
  refreshActions();
  summaryPage_ = 0;
  summaryPages_[0] = 0;
  if (summary) {
    screen_ = Screen::Summary;
  } else {
    afterSummary();
  }
}

// Builds the visit's life log for the Return summary and appends it to the
// Chronicle. A character made before the Chronicle existed gets its opening
// chapter first. Without memory for the log, the summary shows the numbers only.
void CultivationActivity::writeLifeLog(const lifelog::VisitChange& change) {
  lifeLog_ = makeUniqueNoThrow<lifelog::LifeLog>();
  if (!lifeLog_) {
    LOG_ERR("CULT", "Out of memory for the life log (%u bytes)", static_cast<unsigned>(sizeof(lifelog::LifeLog)));
    appendWorldChronicleLines();  // v0.3.2: the world's news still reaches the Chronicle
    return;
  }
  lifelog::build(*lifeLog_, change, visit_.monthsPassed, *state_, grammar_.get());
  appendUpheavalLines(upheavals_);  // Spatial Upheaval: after the visit's own lines
  appendRivalLines(rivalNews_);     // v0.2.8: then the rival's news
  if (ageWarning_ != lineage::Age::Fine && lifeLog_->count < lifelog::kMaxLines) {  // v0.3.0
    lifelog::Line& line = lifeLog_->lines[lifeLog_->count++];
    line.kind = lifelog::LineKind::Event;
    line.season = lifelog::seasonOf(state_->progress.ageMonths);
    snprintf(line.text, sizeof(line.text), "%s",
             ageWarning_ == lineage::Age::Failing ? ctr(CULT_AGE_FAILING) : ctr(CULT_AGE_GREYING));
  }
  // v0.3.2: last, one line of the world's news; the Chronicle gets all of it, in order, after the visit.
  appendWorldSummaryLine();
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  if (worldSummary_ >= 0) --lifeLog_->count;
  chronicle::appendVisit(*lifeLog_);
  if (worldSummary_ >= 0) ++lifeLog_->count;
  appendWorldChronicleLines();
  if (packs_) packs_->closeFile();
}

// The render task may be drawing the summary or the Chronicle from these
// blocks, so they are swapped and freed under the render lock.
void CultivationActivity::leaveSummary() {
  {
    RenderLock lock(*this);
    lifeLog_.reset();
    selected_ = 0;
    afterSummary();
  }
  requestUpdate();
}

void CultivationActivity::openChronicle() {
  auto view = makeUniqueNoThrow<ChronicleView>();
  if (!view) {
    LOG_ERR("CULT", "Out of memory for the Chronicle viewer (%u bytes)", static_cast<unsigned>(sizeof(ChronicleView)));
    return;
  }
  {
    // v0.2.3 (user): open on the newest page. Under the lock: laying a page
    // out uses the renderer's fonts, and the render task reads the same file.
    RenderLock lock(*this);
    notice_[0] = '\0';
    FsFile f;
    if (Storage.openFileForRead("CULT", chroniclePath(), f)) {
      view->size = static_cast<uint32_t>(f.fileSize());
      view->starts[0] = chronicleNewestStart(f, view->size);
      view->age = chronicle::pageAge(f, view->size, view->starts[0]);
      f.close();
    }
    chronicle_ = std::move(view);
    screen_ = Screen::Chronicle;
  }
  requestUpdate();
}

// --- Chronicle viewer (v0.2.3): pages laid out by the renderer ------------------

// The page area of the Chronicle screen, as render() works it out.
uint32_t CultivationActivity::chronicleNextStart(FsFile& f, const uint32_t size, const uint32_t start) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  bool drewAny = false;
  return chronicleLayout(f, size, start, x, top, width, bottom, false, &drewAny);
}

// The newest page is the one that ends at the end of the file (`size`) and
// holds as much as fits: the earliest of the last lines whose page still
// reaches the end (a binary search over at most 48 line starts, about 6 page
// layouts). With `size` set to a page's start it finds the page before it.
uint32_t CultivationActivity::chronicleNewestStart(FsFile& f, const uint32_t size) {
  if (size == 0) return 0;
  constexpr uint8_t kCandidates = 48;
  uint32_t window = 2048;  // a page holds about 1 KB of text
  for (;;) {
    const uint32_t from = size > window ? size - window : 0;
    uint32_t candidates[kCandidates];
    const uint8_t n = chronicle::lineStartsIn(f, size, from, size, candidates, kCandidates);
    if (n == 0) return 0;
    if (chronicleNextStart(f, size, candidates[0]) >= size) {
      // Even the earliest fits: look further back unless that is the top or already a long way.
      if (candidates[0] == 0 || n == kCandidates || window >= 16384) return candidates[0];
      window *= 2;
      continue;
    }
    int lo = 0;      // its page stops short of the end
    int hi = n - 1;  // its page reaches the end (one paragraph always fits)
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      if (chronicleNextStart(f, size, candidates[mid]) >= size) {
        hi = mid;
      } else {
        lo = mid;
      }
    }
    // Never begin right under a heading: the page before would end on the
    // heading apart from its entries. Begin an entry later instead.
    while (hi + 1 < n && chronicle::withHeadingAbove(f, size, candidates[hi]) != candidates[hi]) ++hi;
    return candidates[hi];
  }
}

// The page before one starting at `start`, without reading from the top: the
// fullest page that ends exactly at `start`, found as the newest page is, with
// the file treated as ending there. Nothing is shown twice and nothing skipped.
uint32_t CultivationActivity::chroniclePreviousStart(FsFile& f, const uint32_t start) {
  return start == 0 ? 0 : chronicleNewestStart(f, start);
}

void CultivationActivity::chronicleRefreshAge() {
  if (!chronicle_) return;
  ChronicleView& v = *chronicle_;
  FsFile f;
  if (!Storage.openFileForRead("CULT", chroniclePath(), f)) return;
  v.age = chronicle::pageAge(f, v.size, v.starts[v.cur]);
  f.close();
}

void CultivationActivity::chronicleShow(const uint32_t start) {
  if (!chronicle_) return;
  ChronicleView& v = *chronicle_;
  v.starts[0] = start;
  v.count = 1;
  v.cur = 0;
  notice_[0] = '\0';
  chronicleRefreshAge();
}

// The jump menu's rows, in order (user, session 9).
enum ChronicleMenuRow : uint8_t {
  kRowNewest,
  kRowBeginning,
  kRowBack,
  kRowForward,
  kRowExport,
  kRowPast,  // v0.3.0
  kChronicleMenuRows
};

void CultivationActivity::chronicleMenuChoose(const int row) {
  if (!chronicle_) return;
  ChronicleView& v = *chronicle_;
  RenderLock lock(*this);
  selected_ = v.savedSelected;
  screen_ = Screen::Chronicle;
  if (row == kRowPast) {  // v0.3.0
    lock.unlock();
    openPastLives();
    return;
  }
  if (row == kRowExport) {
    // A copy the player can see and copy off the card; the notice says where.
    char path[96];
    if (chronicleLife_ != 0) {  // a past life is already a file of its own on the card
      snprintf(notice_, sizeof(notice_), ctr(CULT_EXPORTED_FORMAT), chroniclePath_);
    } else if (chronicle::exportCopy(*state_, path, sizeof(path))) {
      snprintf(notice_, sizeof(notice_), ctr(CULT_EXPORTED_FORMAT), path);
    } else {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_EXPORT_FAILED));
    }
    requestUpdate();
    return;
  }
  FsFile f;
  if (!Storage.openFileForRead("CULT", chroniclePath(), f)) {
    requestUpdate();
    return;
  }
  const uint32_t here = v.starts[v.cur];
  uint32_t target = here;
  switch (row) {
    case kRowNewest:
      target = chronicleNewestStart(f, v.size);
      break;
    case kRowBeginning:
      target = 0;
      break;
    case kRowBack:
      target = chronicle::jumpBack(f, v.size, here, v.age);
      break;
    case kRowForward:
      target = chronicle::jumpForward(f, v.size, here, v.age);
      if (target == chronicle::kNotFound) target = chronicleNewestStart(f, v.size);
      break;
    default:
      break;
  }
  f.close();
  // Already on the newest page and asked for it (or for a jump that cannot
  // move): stay, keeping the pages around it.
  if (target != here) chronicleShow(target);
  requestUpdate();
}

// The Cultivator screen's list: what can be done right now.
void CultivationActivity::refreshActions() {
  actionCount_ = 0;
  if (state_ && state_->isCreated() && trials::inTrial(*state_)) {
    // A Tribulation or Heart Demon under way: carry on with it first (equipping is blocked meanwhile).
    actions_[actionCount_++] = Action::Trial;
  } else if (state_ && state_->isCreated()) {
    // Shown even when a key is missing: the Breakthrough screen says which.
    if (atBottleneck(*state_)) actions_[actionCount_++] = Action::Breakthrough;
    if (breakthrough::canEnterGreatPerfection(*state_)) actions_[actionCount_++] = Action::GreatPerfection;
    // v0.2.7: each only once it has something to show (actionShown).
    for (const Action a : {Action::Techniques, Action::Explore, Action::Alchemy, Action::Meridians, Action::Daily,
                           Action::Daos, Action::Rivals}) {
      if (actionShown(a)) actions_[actionCount_++] = a;
    }
  }
  if (chronicleExists_) actions_[actionCount_++] = Action::Chronicle;
  if (state_ && state_->isCreated()) actions_[actionCount_++] = Action::Guide;    // v0.2.9
  if (state_ && state_->isCreated()) actions_[actionCount_++] = Action::Lineage;  // v0.3.0
  if (selected_ >= actionCount_) selected_ = 0;
}

bool CultivationActivity::actionShown(const Action action) const {
  const GameState& s = *state_;
  switch (action) {
    case Action::Techniques:  // the first scroll comes from the first finished book
      return scrolls::count(s) > 0;
    case Action::Explore:  // the first region opens with Qi Condensation
      return explore::unlockedCount(s) > 0;
    case Action::Alchemy:  // a recipe known (the Herbalist's apprentice), a brew, or regions to gather in
      return alchemy::knownCount(s) > 0 || alchemy::brewing(s) || alchemy::ready(s) || explore::unlockedCount(s) > 0;
    case Action::Meridians:  // v0.2.4: from Qi Condensation (the first realm that allows a point)
      return meridians::allowance(s) > 0 || meridians::openCount(s) > 0;
    case Action::Daily:  // v0.2.6: reading tasks from the first day
      return true;
    case Action::Daos:  // v0.2.6: slots open with Qi Condensation
      return s.progress.realm >= 1 || daos::foundCount(s) > 0;
    case Action::Rivals:  // v0.2.8: the ranking opens with Qi Condensation
      return s.progress.realm >= 1 || rivals::nemesisCount(s) > 0;
    default:
      return true;
  }
}

uint8_t CultivationActivity::seenBit(const Action action) {
  switch (action) {
    case Action::Techniques:
      return 0x01;
    case Action::Explore:
      return 0x02;
    case Action::Alchemy:
      return 0x04;
    case Action::Meridians:
      return 0x08;
    case Action::Daily:
      return 0x10;
    case Action::Daos:
      return 0x20;
    case Action::Rivals:  // v0.2.8
      return 0x40;
    default:
      return 0;
  }
}

bool CultivationActivity::isNew(const Action action) const {
  if (action == Action::Guide) return (state_->flagsAndCodex[kGuideSeenByte] & kGuideSeenBit) == 0;  // v0.2.9
  // v0.3.0: new once reincarnation opens (Foundation Establishment); the bit outlives the life.
  if (action == Action::Lineage) return lineage::canReincarnate(*state_) && !lineage::cellSeen(*state_);
  const uint8_t bit = seenBit(action);
  return bit != 0 && (state_->flagsAndCodex[6] & bit) == 0;
}

void CultivationActivity::markSeen(const Action action) {
  if (!isNew(action)) return;
  if (action == Action::Lineage) {
    lineage::markCellSeen(*state_);
  } else if (action == Action::Guide) {  // v0.2.9: its own bit, so every character sees "(New!)" once
    state_->flagsAndCodex[kGuideSeenByte] = static_cast<uint8_t>(state_->flagsAndCodex[kGuideSeenByte] | kGuideSeenBit);
  } else {
    state_->flagsAndCodex[6] = static_cast<uint8_t>(state_->flagsAndCodex[6] | seenBit(action));
  }
  persist();  // so "(New!)" does not come back if nothing else is saved before leaving
}

// A save from before v0.2.7: what is already shown counts as seen, so an old
// character does not see "(New!)" on everything. New characters start with
// only bit 7 set (createCharacter), so everything is new to them.
void CultivationActivity::setUpSeenFlags() {
  uint8_t& flags = state_->flagsAndCodex[6];
  if ((flags & kSeenFlagsSetUp) != 0) return;
  for (const Action a : {Action::Techniques, Action::Explore, Action::Alchemy, Action::Meridians, Action::Daily,
                         Action::Daos, Action::Rivals}) {
    if (actionShown(a)) flags = static_cast<uint8_t>(flags | seenBit(a));
  }
  flags = static_cast<uint8_t>(flags | kSeenFlagsSetUp);
}

void CultivationActivity::runAction() {
  if (selected_ < 0 || selected_ >= actionCount_) return;
  markSeen(actions_[selected_]);  // v0.2.7
  switch (actions_[selected_]) {
    case Action::Breakthrough: {
      RenderLock lock(*this);
      prepareKeyName();
      trialSummary_[0] = '\0';
      screen_ = Screen::Breakthrough;
    }
      requestUpdate();
      return;
    case Action::Techniques:
      openTechniques();
      return;
    case Action::Explore:
      openRegions();
      return;
    case Action::Alchemy:
      openAlchemy();
      return;
    case Action::Meridians:
      openMeridians();
      return;
    case Action::Daily:
      openDaily();
      return;
    case Action::Daos:
      openDaos();
      return;
    case Action::Rivals:
      openRivals();
      return;
    case Action::GreatPerfection:
      screen_ = Screen::GreatPerfection;
      requestUpdate();
      return;
    case Action::Chronicle:
      openChronicle();
      return;
    case Action::Trial:
      trialLast_[0] = '\0';
      openTrial();
      return;
    case Action::Guide:  // v0.2.9
      openGuide();
      return;
    case Action::Lineage:  // v0.3.0
      openLineage(false);
      return;
  }
}

// One breakthrough attempt: roll, write its line to the Chronicle, then save.
// The roll is seeded by the failure count, so if the save fails the retry
// rolls the same and appendLines() skips the line already written.
void CultivationActivity::attemptBreakthrough() {
  if (!breakthrough::canAttempt(*state_)) return;
  {
    RenderLock lock(*this);
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindOutcomeLine,
                                      (static_cast<uint32_t>(state_->progress.realm) << 8) | state_->progress.failures);
    outcome_ = breakthrough::attempt(*state_);
    outcomeKind_ = outcome_.success ? OutcomeKind::BreakthroughSuccess : OutcomeKind::BreakthroughFailure;
    recordOutcome(outcome_.success ? "log.breakthrough" : "log.failure", seed);
    // v0.2.6: a new realm may open a Dao slot that a habit fills at once.
    daoNews_ = outcome_.success ? daos::settle(*state_) : daos::News{};
    writeDaoNews(daoNews_);
    if (outcome_.keyUsed) {  // the Chronicle records which scroll opened which realm
      scrollName(outcome_.key, nameBuf_, sizeof(nameBuf_));
      appendScrollLine("scroll.line.key", outcome_.key);
    }
    LOG_INF("CULT", "Breakthrough into realm %u: roll %u, chance %u, %s", static_cast<unsigned>(outcome_.toRealm),
            static_cast<unsigned>(outcome_.roll), static_cast<unsigned>(outcome_.chance),
            outcome_.success ? "success" : "failure");
    persist();
    refreshActions();
    screen_ = Screen::Outcome;
  }
  requestUpdate();
}

void CultivationActivity::enterGreatPerfection() {
  if (!breakthrough::canEnterGreatPerfection(*state_)) return;
  {
    RenderLock lock(*this);
    breakthrough::enterGreatPerfection(*state_);
    outcomeKind_ = OutcomeKind::EnteredGreatPerfection;
    recordOutcome("log.gp.enter", contentSeed(worldSeedOf(*state_), kKindGreatPerfectionLine, state_->progress.realm));
    persist();
    refreshActions();
    screen_ = Screen::Outcome;
  }
  requestUpdate();
}

// The outcome's line from the packs, shown on the Outcome screen and added
// to the bottom of the Chronicle.
void CultivationActivity::recordOutcome(const char* listKey, const uint32_t seed) {
  outcomeLine_.text[0] = '\0';
  if (!grammar_) return;
  if (lifelog::eventLine(outcomeLine_, lifelog::LineKind::Event, listKey, seed, *state_, *grammar_)) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&outcomeLine_, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::closeChronicle() {
  {
    RenderLock lock(*this);
    chronicle_.reset();
    chronicleLife_ = 0;  // v0.3.0: back to the current life's Chronicle
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

void CultivationActivity::startCreation() {
  // v0.3.0: after a life has ended, the legacy perks come first. A Chronicle
  // still to be moved aside (power lost right after the save) goes now.
  if (lineage::inUse(*state_)) {
    if (lineage::chronicleMovePending(*state_) && chronicle::moveToLives(lineage::previousLife(*state_))) {
      lineage::setChronicleMovePending(*state_, false);
      persist();
    }
    chronicleExists_ = false;
    lineage_ = makeUniqueNoThrow<LineageView>();  // the shop needs none of it, but its screens share the view
    selected_ = 0;
    screen_ = Screen::Perks;
    return;
  }
  beginCreation();
}

void CultivationActivity::beginCreation() {
  draft_ = CharacterDraft{};
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    LOG_ERR("CULT", "Could not read the device MAC; using zeros");
  }
  const StatsSample now = readLocalStats();
  // DOC: world seed = hash(device, creation date, life number).
  draft_.worldSeed = worldSeed(mac, now.today, static_cast<uint16_t>(lineage::livesEnded(*state_) + 1u));
  const uint32_t rerolls = kBaseRerolls + lineage::extraRerolls(*state_);  // v0.3.0: + Tested Again
  draft_.rerollsAllowed = static_cast<uint8_t>(rerolls < kMaxRolls - 1u ? rerolls : kMaxRolls - 1u);
  rollCount_ = 0;
  rollView_ = 0;
  nameSuggestions_ = 0;
  nameInvalid_ = false;
  selected_ = 0;
  screen_ = Screen::Name;
}

void CultivationActivity::finishCreation() {
  draft_.destiny = rolls_[rollView_];
  draft_.rerollsUsed = static_cast<uint8_t>(rollCount_ - 1);
  // The baseline is taken now, at the moment the destiny is kept.
  createCharacter(*state_, draft_, readLocalStats());
  if (damagedSave_) {
    if (Storage.exists(kDamagedSavePath)) Storage.remove(kDamagedSavePath);
    if (!Storage.rename(save::kSavePath, kDamagedSavePath)) {
      LOG_ERR("CULT", "Could not keep the damaged save aside");
    }
  }
  scrollstore::clearForNewLife();  // books finished before this life give nothing (baseline rule)
  {  // a new chapter; the book's title too if it is new (v0.3.0: a new file per life, with the life before)
    char pastLife[200];
    pastLifeLine(pastLife, sizeof(pastLife));
    chronicle::writeOpening(*state_, grammar_.get(), pastLife);
  }
  if (packs_) packs_->closeFile();
  chronicleExists_ = chronicle::exists();
  persist();
  refreshActions();
  justCreated_ = true;
  visit_ = AbsorbResult{};
  const OriginBonus& bonus = originBonus(state_->character.origin);
  originGift_.stones = bonus.stones;
  originGift_.hp = bonus.extraHp;
  updateRumor();
  screen_ = Screen::Cultivator;
}

// --- Input ------------------------------------------------------------------------

void CultivationActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Back,
                                               ignoreInitialBackRelease_) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Power,
                                               ignoreInitialPowerRelease_) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm,
                                               ignoreInitialConfirmRelease_)) {
    return;
  }

  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                    TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer));
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                  mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                    mappedInput.wasPressed(MappedInputManager::Button::Right);
  if (back) mappedInput.suppressNextBackRelease();
  if (confirm) mappedInput.suppressNextConfirmRelease();

  if (handleExplore(back, confirm, up, down)) return;    // exploration screens (CultivationExploreScreens.cpp)
  if (handleAlchemy(back, confirm, up, down)) return;    // alchemy screens (CultivationAlchemyScreens.cpp)
  if (handleMeridians(back, confirm, up, down)) return;  // v0.2.4 (CultivationMeridianScreens.cpp)
  if (handleDaily(back, confirm, up, down)) return;      // v0.2.6 (CultivationDailyScreens.cpp)
  if (handleRivals(back, confirm, up, down)) return;     // v0.2.8 (CultivationRivalScreens.cpp)
  if (handleGuide(back, confirm, up, down)) return;      // v0.2.9 (CultivationGuideScreens.cpp)
  if (handleLineage(back, confirm, up, down)) return;    // v0.3.0 (CultivationLineageScreens.cpp)

  switch (screen_) {
    case Screen::Error:
      if (back || confirm) finish();
      return;

    case Screen::Cultivator:
      if (back) {
        finish();
      } else if (confirm) {
        if (actionCount_ > 0) {
          runAction();
        } else {
          finish();
        }
      } else {
        handleList(actionCount_, up, down);
      }
      return;

    case Screen::Breakthrough:
      if (back) {
        screen_ = Screen::Cultivator;
        requestUpdate();
      } else if (up || down) {  // v0.2.0: which breakthrough pill to take (or none)
        RenderLock lock(*this);
        const breakthrough::Chance c = breakthrough::chanceFor(*state_);
        alchemy::cycleChoice(*state_, c.target, c.needsKey && c.keyIndex < 0 && c.daoKey == 0xFF);
        persist();
        requestUpdate();
      } else if (confirm) {
        // Realms with a Tribulation or Heart Demon face it first (v0.1.9).
        if (trials::kindFor(state_->progress.realm + 1) != trials::Kind::None) {
          startTrial();
        } else {
          attemptBreakthrough();
        }
      }
      return;

    case Screen::Trial:
      if (back) {  // the trial waits in the save; the Cultivator screen offers to continue it
        RenderLock lock(*this);
        refreshActions();
        selected_ = 0;
        screen_ = Screen::Cultivator;
        requestUpdate();
      } else if (confirm) {
        answerTrial();
      } else {
        handleList(trialChoices_, up, down);
      }
      return;

    case Screen::GreatPerfection:
      if (back) {
        screen_ = Screen::Cultivator;
        requestUpdate();
      } else if (confirm) {
        enterGreatPerfection();
      }
      return;

    case Screen::Outcome:
      if (back || confirm) {
        selected_ = 0;
        screen_ = Screen::Cultivator;
        requestUpdate();
      }
      return;

    case Screen::Summary:
      if (back || confirm) {
        leaveSummary();
      } else if (down && summaryNext_ > summaryPages_[summaryPage_] && lifeLog_ && summaryNext_ < lifeLog_->count &&
                 summaryPage_ + 1 < static_cast<int>(sizeof(summaryPages_))) {
        summaryPages_[++summaryPage_] = summaryNext_;
        requestUpdate();
      } else if (up && summaryPage_ > 0) {
        --summaryPage_;
        requestUpdate();
      }
      return;

    case Screen::Chronicle: {
      if (!chronicle_) {
        closeChronicle();
        return;
      }
      ChronicleView& v = *chronicle_;
      if (back) {
        closeChronicle();
      } else if (confirm) {  // v0.2.3: the jump menu (Export moved into it)
        RenderLock lock(*this);
        v.savedSelected = selected_;
        selected_ = 0;
        screen_ = Screen::ChronicleMenu;
        requestUpdate();
      } else if (down && (v.cur + 1 < v.count || v.more)) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        // The next page as drawn. A cached one is used only if it skips nothing
        // (an export notice makes the page shorter than it was laid out).
        if (v.cur + 1 >= v.count || v.starts[v.cur + 1] > v.next) {
          if (v.cur + 1 >= ChronicleView::kCache) {  // full: forget the oldest page
            memmove(v.starts, v.starts + 1, sizeof(v.starts) - sizeof(v.starts[0]));
            --v.cur;
          }
          v.starts[v.cur + 1] = v.next;
          v.count = static_cast<uint8_t>(v.cur + 2);
        }
        ++v.cur;
        chronicleRefreshAge();
        requestUpdate();
      } else if (up && (v.cur > 0 || v.starts[0] > 0)) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        if (v.cur > 0) {
          --v.cur;
        } else {  // before the first page known: lay it out from a little earlier
          FsFile f;
          if (Storage.openFileForRead("CULT", chroniclePath(), f)) {
            const uint32_t previous = chroniclePreviousStart(f, v.starts[0]);
            f.close();
            if (v.count >= ChronicleView::kCache) --v.count;  // full: forget the newest page
            memmove(v.starts + 1, v.starts, v.count * sizeof(v.starts[0]));
            v.starts[0] = previous;
            ++v.count;
          }
        }
        chronicleRefreshAge();
        requestUpdate();
      }
      return;
    }

    case Screen::ChronicleMenu:
      if (!chronicle_) {
        closeChronicle();
      } else if (back) {
        RenderLock lock(*this);
        selected_ = chronicle_->savedSelected;
        screen_ = Screen::Chronicle;
        requestUpdate();
      } else if (confirm) {
        chronicleMenuChoose(selected_);
      } else {
        handleList(kChronicleMenuRows, up, down);
      }
      return;

    case Screen::ScrollNews:
      if (back || confirm) {
        if (confirm && news_ && news_->next > news_->pageStarts[news_->page] && news_->next < news_->count &&
            news_->page + 1 < static_cast<int>(sizeof(news_->pageStarts))) {
          RenderLock lock(*this);
          news_->pageStarts[++news_->page] = news_->next;
        } else {
          leaveScrollNews();
        }
        requestUpdate();
      } else if (down && news_ && news_->next < news_->count && news_->next > news_->pageStarts[news_->page] &&
                 news_->page + 1 < static_cast<int>(sizeof(news_->pageStarts))) {
        RenderLock lock(*this);
        news_->pageStarts[++news_->page] = news_->next;
        requestUpdate();
      } else if (up && news_ && news_->page > 0) {
        RenderLock lock(*this);
        --news_->page;
        requestUpdate();
      }
      return;

    case Screen::BetterManual:
      if (confirm) {
        equipBetterManual();
      } else if (back) {
        betterManualHash_ = 0;
        screen_ = Screen::Cultivator;
        requestUpdate();
      }
      return;

    case Screen::Techniques:
      handleTechniques(back, confirm, up, down);
      return;

    case Screen::ScrollDetail:
      handleScrollDetail(back, confirm, up, down);
      return;

    case Screen::SellConfirm:
      if (confirm) {
        sellSelected();
      } else if (back) {
        selected_ = 0;
        screen_ = Screen::ScrollDetail;
        requestUpdate();
      }
      return;

    case Screen::Name:
      if (back) {
        finish();  // nothing saved until a destiny is kept
      } else if (confirm) {
        onNameConfirm();
      } else {
        handleList(kNameRows, up, down);
      }
      return;

    case Screen::Gender:
      if (back) {
        selected_ = kNameRowName;
        screen_ = Screen::Name;
        requestUpdate();
      } else if (confirm) {
        draft_.gender = static_cast<Gender>(selected_);
        selected_ = static_cast<int>(draft_.origin);
        screen_ = Screen::Origin;
        requestUpdate();
      } else {
        handleList(static_cast<int>(Gender::Count), up, down);
      }
      return;

    case Screen::Origin:
      if (back) {
        selected_ = static_cast<int>(draft_.gender);
        screen_ = Screen::Gender;
        requestUpdate();
      } else if (confirm) {
        draft_.origin = static_cast<Origin>(selected_);
        if (rollCount_ == 0) {
          rolls_[0] = rollDestiny(draft_.worldSeed, 0);
          rollCount_ = 1;
        }
        screen_ = Screen::Awakening;
        showAwakening(rollView_);
      } else {
        handleList(static_cast<int>(Origin::Count), up, down);
      }
      return;

    case Screen::Awakening:
      if (back) {
        selected_ = static_cast<int>(draft_.origin);
        screen_ = Screen::Origin;
        requestUpdate();
      } else if (confirm) {
        finishCreation();
        requestUpdate();
      } else if (up) {
        if (rollView_ > 0) showAwakening(rollView_ - 1);
      } else if (down) {
        rerollOrNext();
      }
      return;
  }
}

void CultivationActivity::handleList(const int count, const bool up, const bool down) {
  if (count <= 0 || (!up && !down)) return;
  selected_ = up ? (selected_ + count - 1) % count : (selected_ + 1) % count;
  requestUpdate();
}

void CultivationActivity::onNameConfirm() {
  switch (selected_) {
    case kNameRowName:
      openKeyboard();
      return;
    case kNameRowSuggest:
      suggestName(draft_.worldSeed, nameSuggestions_++, draft_.gender, draft_.name, sizeof(draft_.name));
      nameInvalid_ = false;
      requestUpdate();
      return;
    default:  // Continue
      if (draft_.name[0] == '\0') {
        nameInvalid_ = true;
        selected_ = kNameRowName;
      } else {
        selected_ = static_cast<int>(draft_.gender);
        screen_ = Screen::Gender;
      }
      requestUpdate();
      return;
  }
}

void CultivationActivity::openKeyboard() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, ctr(CULT_NAME_PROMPT),
                                                           std::string(draft_.name), kNameMaxBytes, InputType::Text);
  if (!keyboard) {
    LOG_ERR("CULT", "Could not allocate the keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    mappedInput.suppressNextConfirmRelease();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);  // the keyboard may follow the reader orientation
    const auto* typed = std::get_if<KeyboardResult>(&result.data);
    if (!result.isCancelled && typed != nullptr) {
      char name[kNameMaxBytes + 1] = {};
      nameInvalid_ = !normalizeName(renderer, typed->text, name);
      if (!nameInvalid_) memcpy(draft_.name, name, sizeof(name));
    }
    requestUpdate();
  });
}

void CultivationActivity::showAwakening(const uint8_t view) {
  rollView_ = view < rollCount_ ? view : static_cast<uint8_t>(rollCount_ - 1);
  requestUpdate();
}

// Next moves through destinies already seen; past the last one it spends a
// reroll. Every destiny seen stays selectable (DOC: keep any of them).
void CultivationActivity::rerollOrNext() {
  if (rollView_ + 1 < rollCount_) {
    showAwakening(rollView_ + 1);
    return;
  }
  const uint8_t rerollsUsed = static_cast<uint8_t>(rollCount_ - 1);
  if (rerollsUsed >= draft_.rerollsAllowed || rollCount_ >= kMaxRolls) return;
  rolls_[rollCount_] = rollDestiny(draft_.worldSeed, rollCount_);
  ++rollCount_;
  showAwakening(rollCount_ - 1);
}

// --- Drawing ------------------------------------------------------------------------

int CultivationActivity::drawWrapped(const int fontId, const int x, int y, const int width, const char* str,
                                     const int maxLines) const {
  // wrappedText returns a short-lived vector of a few lines; render-time only.
  const auto lines = renderer.wrappedText(fontId, str, width, maxLines);
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& line : lines) {
    renderer.drawText(fontId, x, y, line.c_str());
    y += lineHeight;
  }
  return y;
}

void CultivationActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();

  const char* title = ctr(CULT_TITLE);
  if (screen_ == Screen::Name || screen_ == Screen::Gender || screen_ == Screen::Origin) title = ctr(CULT_NEW_LIFE);
  if (screen_ == Screen::Awakening) title = ctr(CULT_AWAKENING);
  if (screen_ == Screen::Breakthrough) title = ctr(CULT_BREAKTHROUGH);
  if (screen_ == Screen::Trial) {
    title = trials::kindFor(state_->progress.realm + 1) == trials::Kind::HeartDemon ? ctr(CULT_HEART_DEMON)
                                                                                    : ctr(CULT_TRIBULATION);
  }
  if (screen_ == Screen::GreatPerfection) title = ctr(CULT_GREAT_PERFECTION);
  if (screen_ == Screen::Outcome) {
    title = outcomeKind_ == OutcomeKind::EnteredGreatPerfection ? ctr(CULT_GREAT_PERFECTION) : ctr(CULT_BREAKTHROUGH);
    if (outcomeKind_ == OutcomeKind::TrialLost) {
      title =
          trials::kindFor(outcome_.toRealm) == trials::Kind::HeartDemon ? ctr(CULT_HEART_DEMON) : ctr(CULT_TRIBULATION);
    }
  }
  if (screen_ == Screen::ScrollNews) title = ctr(CULT_SCROLLS_TITLE);
  if (screen_ == Screen::BetterManual) title = ctr(CULT_BETTER_MANUAL);
  if (screen_ == Screen::Techniques || screen_ == Screen::ScrollDetail) title = ctr(CULT_TECHNIQUES);
  if (screen_ == Screen::SellConfirm) title = ctr(CULT_SELL);
  char exploreTitleBuf[ExploreView::kRegionNameSize];
  if (screen_ == Screen::Regions || screen_ == Screen::Map || screen_ == Screen::Fight || screen_ == Screen::Meeting ||
      screen_ == Screen::Merchant || screen_ == Screen::MerchantSell || screen_ == Screen::Bag ||
      screen_ == Screen::Talisman || screen_ == Screen::MerchantQty) {
    title = exploreTitle(exploreTitleBuf, sizeof(exploreTitleBuf));
  }
  if (screen_ == Screen::Alchemy || screen_ == Screen::Recipes) title = ctr(CULT_ALCHEMY_TITLE);
  if (screen_ == Screen::Experiment) title = ctr(CULT_ALCHEMY_EXPERIMENT);
  if (screen_ == Screen::Meridians) title = ctr(CULT_ACTION_MERIDIANS);
  if (screen_ == Screen::Daily) title = ctr(CULT_ACTION_DAILY);
  if (screen_ == Screen::Daos) title = ctr(CULT_ACTION_DAOS);
  char ladderTitle[64];
  if (screen_ == Screen::Rivals || screen_ == Screen::Nemeses) {
    title = screen_ == Screen::Rivals ? ctr(CULT_RIVALS_TITLE) : ctr(CULT_RIVALS_ROW_NEMESES);
  }
  if (screen_ == Screen::RivalCard) title = ctr(CULT_RIVALS_ROW_RIVAL);
  if (screen_ == Screen::Ladder) {
    snprintf(ladderTitle, sizeof(ladderTitle), ctr(CULT_RIVALS_ROW_LADDER_FORMAT),
             ui::realmName(state_->progress.realm));
    title = ladderTitle;
  }
  char recipeTitle[64];
  if (screen_ == Screen::RecipeDetail && alchemy_) {
    snprintf(recipeTitle, sizeof(recipeTitle), "%s",
             pillLabel(alchemy::recipesOf(*state_).recipes[alchemy_->recipe].pill));
    if (recipeTitle[0] >= 'a' && recipeTitle[0] <= 'z') recipeTitle[0] = static_cast<char>(recipeTitle[0] - 'a' + 'A');
    title = recipeTitle;
  }
  char chronicleTitle[48];
  if (screen_ == Screen::Chronicle && chronicle_) {  // v0.2.3: the page's age, not a page number
    if (chronicle_->age >= 0) {
      snprintf(chronicleTitle, sizeof(chronicleTitle), ctr(CULT_CHRONICLE_AGE_FORMAT),
               static_cast<long>(chronicle_->age));
    } else {
      snprintf(chronicleTitle, sizeof(chronicleTitle), "%s", ctr(CULT_CHRONICLE_BIRTH));
    }
    title = chronicleTitle;
  }
  if (screen_ == Screen::ChronicleMenu) title = ctr(CULT_ACTION_CHRONICLE);
  char guideTitleBuf[72];
  if (screen_ == Screen::Guide) title = ctr(CULT_GUIDE_TITLE);  // v0.2.9
  if (screen_ == Screen::GuidePage) title = guideTitle(guideTitleBuf, sizeof(guideTitleBuf));
  if (const char* t = lineageTitle()) title = t;  // v0.3.0
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, title);
  } else {
    CompactHeader::drawTitle(renderer, title);
  }

  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const char* confirmLabel = tr(STR_SELECT);
  const char* upLabel = tr(STR_DIR_UP);
  const char* downLabel = tr(STR_DIR_DOWN);
  switch (screen_) {
    case Screen::Name:
      renderName(top, bottom);
      break;
    case Screen::Gender:
      renderGender(top, bottom);
      break;
    case Screen::Origin:
      renderOrigin(top, bottom);
      break;
    case Screen::Awakening: {
      renderAwakening(x, top, width);
      confirmLabel = ctr(CULT_KEEP);
      upLabel = rollView_ > 0 ? ctr(CULT_PREVIOUS) : "";
      const bool canReroll = static_cast<uint8_t>(rollCount_ - 1) < draft_.rerollsAllowed && rollCount_ < kMaxRolls;
      downLabel = rollView_ + 1 < rollCount_ ? ctr(CULT_NEXT) : (canReroll ? ctr(CULT_REROLL) : "");
      break;
    }
    case Screen::Summary:
      renderSummary(x, top, width, bottom);
      confirmLabel = ctr(CULT_CONTINUE);
      upLabel = summaryPage_ > 0 ? ctr(CULT_PREVIOUS) : "";
      downLabel = lifeLog_ && summaryNext_ < lifeLog_->count ? ctr(CULT_NEXT) : "";
      break;
    case Screen::Cultivator:
      renderCultivator(x, top, width, bottom);
      confirmLabel = actionCount_ > 0 ? tr(STR_SELECT) : "";
      upLabel = actionCount_ > 1 ? tr(STR_DIR_UP) : "";
      downLabel = actionCount_ > 1 ? tr(STR_DIR_DOWN) : "";
      break;
    case Screen::Breakthrough: {
      renderBreakthrough(x, top, width);
      const bool lightning = trials::kindFor(state_->progress.realm + 1) == trials::Kind::Tribulation;
      confirmLabel =
          breakthrough::canAttempt(*state_) && !(lightning && state_->progress.hp == 0) ? ctr(CULT_ATTEMPT) : "";
      upLabel = "";
      downLabel = "";
      {
        // v0.2.0: Up/Down picks the breakthrough pill when there is a choice to make.
        const breakthrough::Chance c = breakthrough::chanceFor(*state_);
        uint8_t fitting = 0;
        for (uint8_t i = static_cast<uint8_t>(inventory::Item::BreakthroughPill);
             i < static_cast<uint8_t>(inventory::Item::GradePill); ++i) {
          const auto pill = static_cast<inventory::Item>(i);
          if (inventory::count(*state_, pill) > 0 && alchemy::pillFits(pill, c.target)) ++fitting;
        }
        const bool onlyKey = c.needsKey && c.keyIndex < 0 && c.daoKey == 0xFF;
        if (fitting + (onlyKey ? 0 : 1) > 1) downLabel = ctr(CULT_CHANGE_PILL);
      }
      break;
    }
    case Screen::Trial:
      renderTrial(x, top, width, bottom);
      confirmLabel = trialChoices_ > 0 ? tr(STR_SELECT) : "";
      upLabel = trialChoices_ > 1 ? tr(STR_DIR_UP) : "";
      downLabel = trialChoices_ > 1 ? tr(STR_DIR_DOWN) : "";
      break;
    case Screen::GreatPerfection:
      renderGreatPerfection(x, top, width);
      confirmLabel = ctr(CULT_ENTER);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Outcome:
      renderOutcome(x, top, width);
      confirmLabel = ctr(CULT_CONTINUE);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Chronicle:
      renderChronicle(x, top, width, bottom);
      confirmLabel = tr(STR_MENU);
      upLabel = chronicle_ && (chronicle_->cur > 0 || chronicle_->starts[0] > 0) ? ctr(CULT_PREVIOUS) : "";
      downLabel = chronicle_ && (chronicle_->cur + 1 < chronicle_->count || chronicle_->more) ? ctr(CULT_NEXT) : "";
      break;
    case Screen::ChronicleMenu:
      renderChronicleMenu(top, bottom);
      break;
    case Screen::ScrollNews: {
      renderScrollNews(x, top, width, bottom);
      const bool more = news_ && news_->next < news_->count && news_->next > news_->pageStarts[news_->page];
      confirmLabel = more ? ctr(CULT_NEXT) : ctr(CULT_CONTINUE);
      upLabel = news_ && news_->page > 0 ? ctr(CULT_PREVIOUS) : "";
      downLabel = more ? ctr(CULT_NEXT) : "";
      break;
    }
    case Screen::BetterManual:
      renderBetterManual(x, top, width);
      confirmLabel = ctr(CULT_EQUIP);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Techniques:
      renderTechniques(x, top, width, bottom);
      confirmLabel = library_ && library_->count > 0 ? tr(STR_SELECT) : "";
      upLabel = library_ && library_->count > 1 ? tr(STR_DIR_UP) : "";
      downLabel = library_ && library_->count > 1 ? tr(STR_DIR_DOWN) : "";
      break;
    case Screen::ScrollDetail:
      renderScrollDetail(x, top, width, bottom);
      confirmLabel = detailActionCount_ > 0 ? tr(STR_SELECT) : "";
      upLabel = detailActionCount_ > 1 ? tr(STR_DIR_UP) : "";
      downLabel = detailActionCount_ > 1 ? tr(STR_DIR_DOWN) : "";
      break;
    case Screen::SellConfirm:
      renderSellConfirm(x, top, width);
      confirmLabel = ctr(CULT_SELL);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Regions:
      renderRegions(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Map:
      renderMap(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Fight:
      renderFight(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Meeting:
      renderMeeting(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Merchant:
      renderMerchant(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::MerchantSell:
      renderSell(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Bag:
      renderBag(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Talisman:
      renderTalisman(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::MerchantQty:
      renderMerchantQty(x, top, width, bottom);
      exploreLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Alchemy:
      renderAlchemy(x, top, width, bottom);
      alchemyLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Meridians:
      renderMeridians(x, top, width, bottom);
      confirmLabel = ctr(CULT_OPEN);
      break;
    case Screen::Daily:
      renderDaily(x, top, width, bottom);
      dailyLabels(confirmLabel);
      break;
    case Screen::Daos:
      renderDaos(x, top, width, bottom);
      confirmLabel = "";
      break;
    case Screen::Rivals:
      renderRivals(x, top, width, bottom);
      rivalLabels(confirmLabel);
      break;
    case Screen::RivalCard:
      renderRivalCard(x, top, width, bottom);
      rivalLabels(confirmLabel);
      break;
    case Screen::Ladder:
      renderLadder(x, top, width, bottom);
      rivalLabels(confirmLabel);
      break;
    case Screen::Nemeses:
      renderNemeses(x, top, width, bottom);
      rivalLabels(confirmLabel);
      break;
    case Screen::Guide:  // v0.2.9
      renderGuide(x, top, width, bottom);
      confirmLabel = ctr(CULT_GUIDE_READ);
      break;
    case Screen::GuidePage: {
      renderGuidePage(x, top, width, bottom);
      const bool more = guidePage_ + 1 < guidePageCount(guideTopic_);
      confirmLabel = more ? ctr(CULT_NEXT) : ctr(CULT_GUIDE_TOPICS);
      upLabel = guidePage_ > 0 ? ctr(CULT_PREVIOUS) : "";
      downLabel = more ? ctr(CULT_NEXT) : "";
      break;
    }
    case Screen::Lineage:  // v0.3.0
      renderLineage(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::LineageExport:
      renderLineageExport(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::LineageConfirm:
      renderLineageConfirm(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Echo:
      renderEcho(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Perks:
      renderPerks(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Collection:
      renderCollection(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::CollectionPage:
      renderCollectionPage(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::PastLives:
      renderPastLives(x, top, width, bottom);
      lineageLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Recipes:
      renderRecipes(x, top, width, bottom);
      alchemyLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::RecipeDetail:
      renderRecipeDetail(x, top, width, bottom);
      alchemyLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Experiment:
      renderExperiment(x, top, width, bottom);
      alchemyLabels(confirmLabel, upLabel, downLabel);
      break;
    case Screen::Error:
      drawWrapped(UI_10_FONT_ID, x, top, width, ctr(CULT_OUT_OF_MEMORY), 4);
      confirmLabel = "";
      upLabel = "";
      downLabel = "";
      break;
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), confirmLabel, upLabel, downLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void CultivationActivity::renderName(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int listBottom = bottom;
  if (nameInvalid_) {
    const int x = metrics.contentSidePadding;
    const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
    const int noteHeight = renderer.getLineHeight(SMALL_FONT_ID) * 2;
    listBottom = bottom - noteHeight;
    drawWrapped(SMALL_FONT_ID, x, listBottom, width, ctr(CULT_NAME_INVALID), 2);
  }
  GUI.drawList(
      renderer, Rect{0, top, renderer.getScreenWidth(), listBottom - top}, kNameRows, selected_,
      [](const int i) -> std::string {
        if (i == kNameRowName) return ctr(CULT_NAME);
        if (i == kNameRowSuggest) return ctr(CULT_NAME_SUGGEST);
        return ctr(CULT_CONTINUE);
      },
      nullptr, nullptr,
      [this](const int i) -> std::string {
        if (i != kNameRowName) return {};
        return draft_.name[0] != '\0' ? std::string(draft_.name) : std::string(ctr(CULT_NAME_NONE));
      });
}

void CultivationActivity::renderGender(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int noteTop = bottom - renderer.getLineHeight(SMALL_FONT_ID) * 2;
  drawWrapped(SMALL_FONT_ID, x, noteTop, width, ctr(CULT_GENDER_HINT), 2);
  GUI.drawList(renderer, Rect{0, top, renderer.getScreenWidth(), noteTop - top}, static_cast<int>(Gender::Count),
               selected_, [](const int i) -> std::string { return text(kGenderNames[i]); });
}

void CultivationActivity::renderOrigin(const int top, const int bottom) {
  // The selected origin's gifts go under the list, in three lines at most.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int noteHeight = renderer.getLineHeight(UI_10_FONT_ID) * 3 + metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, top, renderer.getScreenWidth(), bottom - top - noteHeight}, static_cast<int>(Origin::Count),
      selected_, [](const int i) -> std::string { return text(kOriginNames[i]); },
      [](const int i) -> std::string { return text(kBirthLines[i]); });
  const int index = selected_ >= 0 && selected_ < static_cast<int>(Origin::Count) ? selected_ : 0;
  drawWrapped(UI_10_FONT_ID, metrics.contentSidePadding, bottom - noteHeight + metrics.verticalSpacing,
              renderer.getScreenWidth() - metrics.contentSidePadding * 2, text(kOriginGifts[index]), 3);
}

void CultivationActivity::renderAwakening(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const DestinyRoll& roll = rolls_[rollView_];
  char line[128];

  snprintf(line, sizeof(line), ctr(CULT_ROLL_FORMAT), static_cast<unsigned>(rollView_ + 1),
           static_cast<unsigned>(rollCount_));
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  // DOC: "The elder's testing stone glowed a murky green."
  const GameStr glow = roll.purity < 34   ? GameStr::CULT_GLOW_MURKY
                       : roll.purity < 67 ? GameStr::CULT_GLOW_STEADY
                                          : GameStr::CULT_GLOW_BRILLIANT;
  snprintf(line, sizeof(line), ctr(CULT_STONE_FORMAT), text(glow),
           text(kElementColors[static_cast<int>(roll.element)]));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;

  snprintf(line, sizeof(line), ctr(CULT_ROOT_FORMAT), text(kElementNames[static_cast<int>(roll.element)]),
           static_cast<unsigned>(roll.purity));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), ctr(CULT_TALENT_FORMAT), text(kTalentNames[roll.talent]));
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  formatRate(line, sizeof(line), destinyPermille(roll));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

  const unsigned rerollsLeft = draft_.rerollsAllowed - static_cast<unsigned>(rollCount_ - 1);
  snprintf(line, sizeof(line), ctr(CULT_REROLLS_LEFT_FORMAT), rerollsLeft);
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  drawWrapped(SMALL_FONT_ID, x, y, width, ctr(CULT_AWAKENING_HINT), 3);
}

// "Layer 4", "Peak" or "Great Perfection".
void CultivationActivity::stageText(char* out, const size_t len) const { ui::stageName(*state_, out, len); }

void CultivationActivity::renderCultivator(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const RealmInfo& realm = realmInfo(p.realm);
  char line[224];  // fits "Rumor of the day: " plus a rumor (under 200 bytes)

  char name[kNameMaxBytes + 1];
  nameOf(s, name);
  renderer.drawText(UI_12_FONT_ID, x, y, name, true, EpdFontFamily::BOLD);
  // The build on the name's line, right-aligned, so builds can be told apart
  // (user request: "v0.1.9"); left out if a long name leaves no room.
  {
    char version[24];
    snprintf(version, sizeof(version), "v%s", CULTIVATION_GAME_VERSION);
    const int nameWidth = renderer.getTextWidth(UI_12_FONT_ID, name, EpdFontFamily::BOLD);
    const int versionWidth = renderer.getTextWidth(SMALL_FONT_ID, version);
    if (nameWidth + versionWidth + 16 <= width) {
      const int baseline = renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(SMALL_FONT_ID);
      renderer.drawText(SMALL_FONT_ID, x + width - versionWidth, y + baseline, version);
    }
  }
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

  // Titles: the grades formed so far, and "Half-step <next realm>" after a
  // failed breakthrough (DOC: grades are shown as titles).
  char titles[160];
  ui::formatTitles(s, titles, sizeof(titles));
  if (titles[0] != '\0') {
    const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, titles, width);
    renderer.drawText(SMALL_FONT_ID, x, y, fitted.c_str());
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
  }

  // Realm on the left, stage on the right.
  renderer.drawText(UI_10_FONT_ID, x, y, text(kRealmNames[p.realm < kMortalRealmCount ? p.realm : 0]), true,
                    EpdFontFamily::BOLD);
  stageText(line, sizeof(line));
  renderer.drawText(UI_10_FONT_ID, x + width - renderer.getTextWidth(UI_10_FONT_ID, line), y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  // The bar is drawn here rather than with GUI.drawProgressBar, which adds a
  // centred percentage 15 px under the bar: on the X3 that landed between the
  // Qi line and the next one, on its own baseline. Qi on the left and the
  // percentage on the right now share one line.
  const QiNumber cost = stageCost(p.realm, p.stage);
  const uint32_t fill = QiNumber::fillPermille(p.qi, cost);  // 0..1000
  const int barHeight = metrics.progressBarHeight;
  renderer.drawRect(x, y, width, barHeight);
  const int fillWidth = static_cast<int>(static_cast<int64_t>(width - 4) * fill / 1000);
  if (fillWidth > 0) renderer.fillRect(x + 2, y + 2, fillWidth, barHeight - 4);
  y += barHeight + 4;
  char have[24];
  char need[24];
  p.qi.format(have, sizeof(have));
  cost.format(need, sizeof(need));
  snprintf(line, sizeof(line), ctr(CULT_QI_FORMAT), have, need);
  renderer.drawText(SMALL_FONT_ID, x, y, line);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(fill / 10));
  renderer.drawText(SMALL_FONT_ID, x + width - renderer.getTextWidth(SMALL_FONT_ID, percent), y, percent);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  // v0.2.0 (user's choice): the actions sit in a two-column grid of fixed rows
  // at the bottom. The rumor's room (in the small font) and a missing-key line
  // are reserved before the stats, which drop the talent and then the root
  // line rather than cut the rumor.
  const int rowStep = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int textLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int gridRows = (actionCount_ + 1) / 2;
  const int cellHeight = metrics.listRowHeight;
  const int listTop = bottom - cellHeight * gridRows;
  auto fits = [&](const int lineHeight, const int wanted) {
    const int room = (listTop - 6 - y) / lineHeight;  // a few pixels clear of the grid
    return room < wanted ? (room > 0 ? room : 0) : wanted;
  };

  int rumorReserve = 6;
  if (rumor_[0] != '\0') {
    snprintf(line, sizeof(line), ctr(CULT_RUMOR_FORMAT), rumor_);
    rumorReserve += static_cast<int>(renderer.wrappedText(SMALL_FONT_ID, line, width, 5).size()) * smallLine +
                    metrics.verticalSpacing * 2;
  }
  if (atBottleneck(s)) {
    const breakthrough::Chance c = breakthrough::chanceFor(s);
    if (c.needsKey && !c.hasKey) rumorReserve += smallLine + 4;
  }

  // Short lines share a row, the second on the right; a pair too wide for one row takes two.
  auto pairRows = [&](const char* left, const char* right) {
    return renderer.getTextWidth(UI_10_FONT_ID, left) + renderer.getTextWidth(UI_10_FONT_ID, right) + 16 > width ? 2
                                                                                                                 : 1;
  };
  auto pair = [&](const char* left, const char* right) {
    renderer.drawText(UI_10_FONT_ID, x, y, left);
    if (pairRows(left, right) == 2) y += rowStep;
    renderer.drawText(UI_10_FONT_ID, x + width - renderer.getTextWidth(UI_10_FONT_ID, right), y, right);
    y += rowStep;
  };
  char ageLine[64], daoLine[64], rootLine[96], talentLine[96], rateLine[96], stonesLine[64], staminaLine[64],
      hpLine[64];
  if (lineage::ageState(s) >= lineage::Age::Greying) {  // v0.3.0: past 80% of the lifespan, the years left
    snprintf(ageLine, sizeof(ageLine), ctr(CULT_AGE_LEFT_FORMAT), static_cast<unsigned>(ageYears(s)),
             static_cast<unsigned>(realm.lifespanYears), static_cast<unsigned>(lineage::yearsLeft(s)));
  } else {
    snprintf(ageLine, sizeof(ageLine), ctr(CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
             static_cast<unsigned>(realm.lifespanYears));
  }
  snprintf(daoLine, sizeof(daoLine), ctr(CULT_DAO_HEART_FORMAT), static_cast<unsigned>(p.daoHeart));
  snprintf(rootLine, sizeof(rootLine), ctr(CULT_ROOT_FORMAT),
           text(kElementNames[static_cast<int>(s.character.rootElement)]),
           static_cast<unsigned>(s.character.rootPurity));
  snprintf(talentLine, sizeof(talentLine), ctr(CULT_TALENT_FORMAT),
           text(kTalentNames[s.character.talent % kTalentCount]));
  const uint32_t rate = qiMultiplierPermille(s);
  if (scrolls::equippedManual(s) >= 0) {
    const uint32_t manual = scrolls::equippedManualPermille(s);
    snprintf(rateLine, sizeof(rateLine), ctr(CULT_QI_RATE_MANUAL_FORMAT), static_cast<unsigned>(rate / 1000),
             static_cast<unsigned>((rate % 1000) / 10), static_cast<unsigned>(manual / 1000),
             static_cast<unsigned>((manual % 1000) / 10));
  } else {
    formatRate(rateLine, sizeof(rateLine), rate);
  }
  snprintf(stonesLine, sizeof(stonesLine), ctr(CULT_STONES_FORMAT), static_cast<unsigned long>(p.stones));
  // HP on the Stamina line (v0.1.9: Tribulations spend it, reading heals it).
  snprintf(staminaLine, sizeof(staminaLine), ctr(CULT_STAMINA_FORMAT), static_cast<unsigned>(p.stamina),
           static_cast<unsigned>(kStaminaCap));
  {  // v0.2.9: low HP shows on the Cultivator screen
    const explore::Wound wound = p.hp > 0 ? explore::woundOf(s) : explore::Wound::None;
    snprintf(hpLine, sizeof(hpLine),
             wound == explore::Wound::Badly     ? ctr(CULT_HP_BADLY_FORMAT)
             : wound == explore::Wound::Wounded ? ctr(CULT_HP_WOUNDED_FORMAT)
                                                : ctr(CULT_HP_FORMAT),
             static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
  }

  const bool gp = breakthrough::inGreatPerfection(s);
  // v0.2.1: depth, from the first moment the Peak bar is full (it is kept after a failure too).
  const bool showDepth = breakthrough::depth(s) > 0 || (atBottleneck(s) && !gp);
  const int fixedStats =
      rowStep * (pairRows(ageLine, daoLine) + pairRows(rateLine, stonesLine) + pairRows(staminaLine, hpLine)) +
      ((gp || showDepth) ? textLine + 4 : 0) + metrics.verticalSpacing * 2 - 4;
  bool showRoot = true;
  bool showTalent = true;
  if (y + fixedStats + 2 * rowStep + rumorReserve > listTop) showTalent = false;
  if (y + fixedStats + rowStep + rumorReserve > listTop) showRoot = false;

  pair(ageLine, daoLine);
  if (showRoot) {
    renderer.drawText(UI_10_FONT_ID, x, y, rootLine);
    y += rowStep;
  }
  if (showTalent) {
    renderer.drawText(UI_10_FONT_ID, x, y, talentLine);
    y += rowStep;
  }
  pair(rateLine, stonesLine);
  pair(staminaLine, hpLine);

  if (gp || showDepth) {
    char depthLine[48] = {};
    if (showDepth) {
      snprintf(depthLine, sizeof(depthLine), ctr(CULT_DEPTH_FORMAT), static_cast<unsigned>(breakthrough::depth(s)),
               static_cast<unsigned>(breakthrough::kDepthMax));
    }
    if (gp) {
      snprintf(line, sizeof(line), ctr(CULT_CONSOLIDATION_FORMAT), static_cast<unsigned>(p.consolidation),
               static_cast<unsigned>(breakthrough::kConsolidationMax));
      if (showDepth) {
        pair(line, depthLine);
      } else {
        renderer.drawText(UI_10_FONT_ID, x, y, line);
        y += textLine + 4;
      }
    } else {
      renderer.drawText(UI_10_FONT_ID, x, y, depthLine);
      y += textLine + 4;
    }
  }
  y += metrics.verticalSpacing * 2 - 4;

  // What this visit brought.
  if (justCreated_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_WELCOME), fits(textLine, 3));
  } else if (visitReset_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_VISIT_RESET), fits(textLine, 3));
  } else if (visitSeconds_ == 0) {
    const int needed =
        static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, ctr(CULT_VISIT_NONE), width, 3).size()) * textLine;
    if (y + needed + rumorReserve <= listTop) y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_VISIT_NONE), 3);
  }  // otherwise the Return summary already showed what this visit brought
  if (originGift_.stones > 0 || originGift_.hp > 0) {
    if (originGift_.hp > 0) {
      snprintf(line, sizeof(line), ctr(CULT_ORIGIN_GIFT_HP_FORMAT), static_cast<unsigned long>(originGift_.stones),
               static_cast<unsigned>(originGift_.hp));
    } else {
      snprintf(line, sizeof(line), ctr(CULT_ORIGIN_GIFT_FORMAT), static_cast<unsigned long>(originGift_.stones));
    }
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  }
  // At a bottleneck the "Break through" row says what to do; only a missing
  // key needs a line of its own (the Breakthrough screen explains which keys).
  if (atBottleneck(s)) {
    const breakthrough::Chance c = breakthrough::chanceFor(s);
    if (c.needsKey && !c.hasKey && c.target < kMortalRealmCount) {
      snprintf(line, sizeof(line), ctr(CULT_BOTTLENECK_KEY_SHORT_FORMAT), text(kRealmNames[c.target]));
      y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, line, fits(smallLine, 1));
    }
  }
  if (rumor_[0] != '\0') {
    snprintf(line, sizeof(line), ctr(CULT_RUMOR_FORMAT), rumor_);
    y += metrics.verticalSpacing * 2;
    // On the busiest screens (Great Perfection, a missing key, two actions)
    // the rumor drops to the small font rather than lose its end.
    const int wanted = static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, line, width, 6).size());
    if (fits(textLine, wanted) >= wanted) {
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, wanted);
    } else {
      y = drawWrapped(SMALL_FONT_ID, x, y, width, line, fits(smallLine, 5));
    }
  }
  if (packs_ && packs_->sdProblems() > 0) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_PACK_PROBLEMS), 3);
  if (damagedSave_ && justCreated_) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_SAVE_DAMAGED), 3);
  if (saveFailed_) drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_SAVE_FAILED), 2);

  if (actionCount_ > 0) {
    // Two columns, reading order; Up/Down walk the cells.
    const int screenWidth = renderer.getScreenWidth();
    const int cellWidth = screenWidth / 2;
    const int pad = metrics.contentSidePadding;
    for (int i = 0; i < actionCount_; ++i) {
      const int cx = (i % 2) * cellWidth;
      const int cy = listTop + (i / 2) * cellHeight;
      const char* label = "";
      char meridianLabel[40];
      char dailyLabel[40];
      switch (actions_[i]) {
        case Action::Breakthrough:
          label = ctr(CULT_ACTION_BREAKTHROUGH);
          break;
        case Action::GreatPerfection:
          label = ctr(CULT_ACTION_GREAT_PERFECTION);
          break;
        case Action::Techniques:
          label = ctr(CULT_ACTION_TECHNIQUES);
          break;
        case Action::Explore:
          label = ctr(CULT_ACTION_EXPLORE);
          break;
        case Action::Alchemy:
          label = ctr(CULT_ACTION_ALCHEMY);
          break;
        case Action::Meridians: {
          const unsigned ready = meridians::ready(*state_);
          if (ready > 0) {
            snprintf(meridianLabel, sizeof(meridianLabel), ctr(CULT_ACTION_MERIDIANS_READY_FORMAT), ready);
            label = meridianLabel;
          } else {
            label = ctr(CULT_ACTION_MERIDIANS);
          }
          break;
        }
        case Action::Daily: {
          const unsigned waiting = daily::waiting(*state_);
          if (waiting > 0) {
            snprintf(dailyLabel, sizeof(dailyLabel), ctr(CULT_ACTION_DAILY_COLLECT_FORMAT), waiting);
            label = dailyLabel;
          } else {
            label = ctr(CULT_ACTION_DAILY);
          }
          break;
        }
        case Action::Daos:
          label = ctr(CULT_ACTION_DAOS);
          break;
        case Action::Rivals:  // v0.2.8: someone below challenges you today
          label = rivals::defenceBout(*state_).block == rivals::Block::None ? ctr(CULT_ACTION_RIVALS_CHALLENGED)
                                                                            : ctr(CULT_ACTION_RIVALS);
          break;
        case Action::Chronicle:
          label = ctr(CULT_ACTION_CHRONICLE);
          break;
        case Action::Trial:
          label = trials::kindFor(state_->progress.realm + 1) == trials::Kind::HeartDemon
                      ? ctr(CULT_ACTION_DEMON)
                      : ctr(CULT_ACTION_TRIBULATION);
          break;
        case Action::Guide:  // v0.2.9
          label = ctr(CULT_ACTION_GUIDE);
          break;
        case Action::Lineage:  // v0.3.0
          label = ctr(CULT_ACTION_LINEAGE);
          break;
      }
      // v0.2.7: a cell not opened yet reads "Explore (New!)" instead of its counts.
      char newLabel[48];
      if (isNew(actions_[i])) {
        const char* base = actions_[i] == Action::Meridians ? ctr(CULT_ACTION_MERIDIANS)
                           : actions_[i] == Action::Daily   ? ctr(CULT_ACTION_DAILY)
                           : actions_[i] == Action::Rivals  ? ctr(CULT_ACTION_RIVALS)
                                                            : label;
        snprintf(newLabel, sizeof(newLabel), ctr(CULT_ACTION_NEW_FORMAT), base);
        label = newLabel;
      }
      const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, label, cellWidth - pad * 2);
      const int ty = cy + (cellHeight - textLine) / 2;
      if (i == selected_) {
        renderer.fillRect(cx, cy, cellWidth, cellHeight - 2);
        renderer.drawText(UI_10_FONT_ID, cx + pad, ty, fitted.c_str(), false, EpdFontFamily::BOLD);
      } else {
        renderer.drawRect(cx + 4, cy + 2, cellWidth - 8, cellHeight - 6);
        renderer.drawText(UI_10_FONT_ID, cx + pad, ty, fitted.c_str());
      }
    }
  }
}

// The next attempt's chance, part by part (DOC: base, Dao Heart,
// consolidation, pity), and what a failure costs.
void CultivationActivity::renderBreakthrough(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const breakthrough::Chance c = breakthrough::chanceFor(s);
  char line[224];
  if (c.target >= kMortalRealmCount) return;  // ascension is a later milestone

  snprintf(line, sizeof(line), ctr(CULT_INTO_FORMAT), text(kRealmNames[c.target]));
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  snprintf(line, sizeof(line), ctr(CULT_CHANCE_FORMAT), static_cast<unsigned>(c.total));
  renderer.drawText(UI_12_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

  auto part = [&](const char* textLine) {
    renderer.drawText(UI_10_FONT_ID, x + 16, y, textLine);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  };
  snprintf(line, sizeof(line), ctr(CULT_CHANCE_BASE_FORMAT), static_cast<unsigned>(c.base));
  part(line);
  snprintf(line, sizeof(line), ctr(CULT_CHANCE_DAO_HEART_FORMAT), static_cast<unsigned>(c.daoHeart));
  part(line);
  if (breakthrough::inGreatPerfection(s) || c.consolidation > 0) {
    snprintf(line, sizeof(line), ctr(CULT_CHANCE_CONSOLIDATION_FORMAT), static_cast<unsigned>(c.consolidation));
    part(line);
  }
  if (breakthrough::depth(s) > 0) {
    snprintf(line, sizeof(line), ctr(CULT_CHANCE_DEPTH_FORMAT), static_cast<unsigned>(c.depth),
             static_cast<unsigned>(breakthrough::depth(s)));
    part(line);
  }
  if (c.element > 0) {
    snprintf(line, sizeof(line), ctr(CULT_CHANCE_ELEMENT_FORMAT), static_cast<unsigned>(c.element));
    part(line);
  }
  if (c.pill > 0) {
    snprintf(line, sizeof(line), ctr(CULT_CHANCE_PILL_FORMAT), static_cast<unsigned>(c.pill));
    part(line);
  }
  if (c.pity > 0) {
    snprintf(line, sizeof(line), ctr(CULT_CHANCE_PITY_FORMAT), static_cast<unsigned>(s.progress.failures),
             static_cast<unsigned>(c.pity));
    part(line);
  }
  y += metrics.verticalSpacing;

  // v0.1.9: what comes before the roll.
  const trials::Kind trial = trials::kindFor(c.target);
  if (trial == trials::Kind::Tribulation) {
    const uint8_t strikes = trials::strikesFor(c.target);
    uint32_t total = 0;
    for (uint8_t i = 0; i < strikes; ++i) total += trials::strikePercent(strikes, i);
    const ProgressBlock& p = s.progress;
    snprintf(line, sizeof(line), ctr(CULT_BT_TRIB_FORMAT), static_cast<unsigned>(strikes), static_cast<unsigned>(total),
             static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4);
    if (p.hp == 0) {
      y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_BT_NO_HP), 3);
    } else if (p.hp < p.hpMax) {
      snprintf(line, sizeof(line), ctr(CULT_BT_HEAL_FORMAT), static_cast<unsigned>(p.hpMax - p.hp));
      y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
    }
    y += metrics.verticalSpacing;
  } else if (trial == trials::Kind::HeartDemon) {
    snprintf(line, sizeof(line), ctr(CULT_BT_DEMON_FORMAT), static_cast<unsigned>(trials::sceneChance(s)));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + metrics.verticalSpacing;
  }

  // v0.2.0: the breakthrough pill this attempt would take (Up/Down picks another or none).
  char pillName[64] = {};
  if (c.pillItem != inventory::Item::None) ui::itemName(c.pillItem, pillName, sizeof(pillName));
  if (c.needsKey && !c.hasKey) {
    const scrolls::Tier need = scrolls::keyTierFor(c.target);
    static const uint8_t kTierHours[] = {0, 3, 8, 15, 25};
    if (need == scrolls::Tier::Mortal) {
      snprintf(line, sizeof(line), ctr(CULT_BOTTLENECK_KEY_ANY_FORMAT), text(kRealmNames[c.target]));
    } else {
      snprintf(line, sizeof(line), ctr(CULT_BOTTLENECK_KEY_TIER_FORMAT), text(kRealmNames[c.target]),
               cultivation::ui::tierName(need), static_cast<unsigned>(kTierHours[static_cast<int>(need)]));
    }
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 5);
    if (daos::foundCount(s) > 0) {  // v0.2.6
      snprintf(line, sizeof(line), ctr(CULT_BT_DAO_HINT_FORMAT), static_cast<unsigned>(daos::keyLevelFor(c.target)));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    const alchemy::Pill pill = c.target <= 4   ? alchemy::Pill::BreakthroughCommon
                               : c.target <= 7 ? alchemy::Pill::BreakthroughSpirit
                                               : alchemy::Pill::BreakthroughRare;
    snprintf(line, sizeof(line), ctr(CULT_BT_PILL_HINT_FORMAT), ui::pillName(pill));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  } else {
    if (c.needsKey && c.daoKey < daos::kCount) {  // v0.2.6: a Dao, never spent
      const auto dao = static_cast<daos::Dao>(c.daoKey);
      snprintf(line, sizeof(line), ctr(CULT_BT_DAO_KEY_FORMAT), ui::daoName(dao),
               static_cast<unsigned>(daos::level(s, dao)));
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + 4;
    } else if (c.needsKey && c.keyIndex >= 0 && nameBuf_[0] != '\0') {
      snprintf(line, sizeof(line), ctr(CULT_KEY_FORMAT), nameBuf_);
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + 4;
    }
    if (c.pillItem != inventory::Item::None) {
      snprintf(line, sizeof(line), c.pillIsKey ? ctr(CULT_BT_PILL_KEY_FORMAT) : ctr(CULT_BT_PILL_FORMAT), pillName);
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + 4;
    } else if (c.target >= 2 && alchemy::chosenPill(s, c.target) == inventory::Item::None &&
               state_->timers[9] == alchemy::kNoPillChoice) {
      y = drawWrapped(SMALL_FONT_ID, x, y, width, ctr(CULT_BT_PILL_NONE), 2) + 4;
    }
    y += metrics.verticalSpacing - 4;
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_BREAKTHROUGH_RISK), 4);
  }
  if (breakthrough::canEnterGreatPerfection(s)) {
    drawWrapped(SMALL_FONT_ID, x, y + metrics.verticalSpacing, width, ctr(CULT_GP_HINT), 3);
  }
}

void CultivationActivity::renderGreatPerfection(const int x, const int y, const int width) {
  char line[400];
  snprintf(line, sizeof(line), ctr(CULT_GP_EXPLAIN_FORMAT),
           text(kRealmNames[state_->progress.realm < kMortalRealmCount ? state_->progress.realm : 0]));
  drawWrapped(UI_10_FONT_ID, x, y, width, line, 12);
}

void CultivationActivity::renderOutcome(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];
  switch (outcomeKind_) {
    case OutcomeKind::BreakthroughSuccess:
      snprintf(line, sizeof(line), ctr(CULT_BREAKTHROUGH_SUCCESS_FORMAT),
               text(kRealmNames[outcome_.toRealm < kMortalRealmCount ? outcome_.toRealm : 0]));
      break;
    case OutcomeKind::BreakthroughFailure:
      snprintf(line, sizeof(line), "%s", ctr(CULT_BREAKTHROUGH_FAILED));
      break;
    case OutcomeKind::EnteredGreatPerfection:
      snprintf(line, sizeof(line), "%s", ctr(CULT_GP_ENTERED));
      break;
    case OutcomeKind::TrialLost:
      snprintf(
          line, sizeof(line), "%s",
          trials::kindFor(outcome_.toRealm) == trials::Kind::HeartDemon ? ctr(CULT_DEMON_LOST) : ctr(CULT_TRIB_LOST));
      break;
  }
  const auto header = renderer.wrappedText(UI_12_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : header) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += metrics.verticalSpacing;

  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess || outcomeKind_ == OutcomeKind::BreakthroughFailure) {
    if (trialSummary_[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, trialSummary_, 2) + 4;
    snprintf(line, sizeof(line), ctr(CULT_BT_ROLL_FORMAT), static_cast<unsigned>(outcome_.roll),
             static_cast<unsigned>(outcome_.chance));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
  if (outcomeLine_.text[0] != '\0') {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, outcomeLine_.text, 6) + metrics.verticalSpacing;
  }
  const char* formed = ui::gradeName(outcome_.gradeKind, outcome_.grade);
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && formed[0] != '\0') {
    snprintf(line, sizeof(line),
             outcome_.gradeKind == breakthrough::GradeKind::Foundation ? ctr(CULT_FOUNDATION_FORMED_FORMAT)
                                                                       : ctr(CULT_GRADE_FORMED_FORMAT),
             formed, static_cast<unsigned>(outcome_.gradeScore));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  if ((outcomeKind_ == OutcomeKind::BreakthroughSuccess || outcomeKind_ == OutcomeKind::BreakthroughFailure) &&
      outcome_.pillUsed != inventory::Item::None) {
    char pill[64];
    ui::itemName(outcome_.pillUsed, pill, sizeof(pill));
    snprintf(line, sizeof(line), ctr(CULT_PILL_USED_FORMAT), pill);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && outcome_.keyUsed && nameBuf_[0] != '\0') {
    snprintf(line, sizeof(line), ctr(CULT_KEY_USED_FORMAT), nameBuf_);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess) {  // v0.2.6: a Dao found in the new realm's slot
    const int before = y;
    y = drawDaoNews(x, y - 4, width);
    if (y != before - 4)
      y += metrics.verticalSpacing;
    else
      y = before;
  }
  // v0.1.10: each realm opens the next region.
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && outcome_.toRealm >= 1 &&
      outcome_.toRealm <= explore::kRegionCount) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_REGION_OPENED), 3);
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughFailure || outcomeKind_ == OutcomeKind::TrialLost) {
    snprintf(line, sizeof(line), ctr(CULT_NEXT_CHANCE_FORMAT), static_cast<unsigned>(breakthrough::chanceFor(s).total));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::TrialLost && s.progress.hp < s.progress.hpMax) {
    snprintf(line, sizeof(line), ctr(CULT_HP_HEAL_FORMAT), static_cast<unsigned>(s.progress.hp),
             static_cast<unsigned>(s.progress.hpMax), static_cast<unsigned>(s.progress.hpMax - s.progress.hp));
    drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);
  }
}

// Return summary (DOC: "Months passed, resources gained, 3-6 life-log
// lines"). The first page carries the time and the numbers; long logs carry
// over to further pages.
void CultivationActivity::renderSummary(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const uint32_t seasons = lifeLog_ ? lifeLog_->seasonsPassed : visit_.monthsPassed / 3;
  const uint32_t months = visit_.monthsPassed;
  const uint8_t first = summaryPages_[summaryPage_];
  char line[224];

  if (summaryPage_ == 0) {
    if (seasons >= 2) {
      snprintf(line, sizeof(line), ctr(CULT_SEASONS_PASSED_FORMAT), static_cast<unsigned long>(seasons));
    } else if (seasons == 1) {
      snprintf(line, sizeof(line), "%s", ctr(CULT_SEASON_PASSED));
    } else if (months >= 2) {
      snprintf(line, sizeof(line), ctr(CULT_MONTHS_PASSED_FORMAT), static_cast<unsigned long>(months));
    } else if (months == 1) {
      snprintf(line, sizeof(line), "%s", ctr(CULT_MONTH_PASSED));
    } else {
      snprintf(line, sizeof(line), "%s", ctr(CULT_MOMENT_PASSED));
    }
    const auto header = renderer.wrappedText(UI_12_FONT_ID, line, width, 3, EpdFontFamily::BOLD);
    for (const auto& h : header) {
      renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    y += metrics.verticalSpacing;

    // Realm and stage on the left, age on the right, as on the Cultivator screen.
    const RealmInfo& realm = realmInfo(p.realm);
    char stage[32];
    stageText(stage, sizeof(stage));
    snprintf(line, sizeof(line), "%s, %s", text(kRealmNames[p.realm < kMortalRealmCount ? p.realm : 0]), stage);
    renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
    const int realmWidth = renderer.getTextWidth(UI_10_FONT_ID, line, EpdFontFamily::BOLD);
    snprintf(line, sizeof(line), ctr(CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
             static_cast<unsigned>(realm.lifespanYears));
    const int ageWidth = renderer.getTextWidth(UI_10_FONT_ID, line);
    if (realmWidth + ageWidth + 16 > width) {  // "Qi Condensation, Great Perfection": age on its own line
      y += renderer.getLineHeight(UI_10_FONT_ID);
      renderer.drawText(UI_10_FONT_ID, x, y, line);
    } else {
      renderer.drawText(UI_10_FONT_ID, x + width - ageWidth, y, line);
    }
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

    char gained[24];
    visit_.qiGained.format(gained, sizeof(gained));
    snprintf(line, sizeof(line), ctr(CULT_VISIT_FORMAT), static_cast<unsigned long>(visitSeconds_ / 60), gained,
             static_cast<unsigned long>(visit_.stonesGained));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);

    // The stage line normally comes from the life log; without generated
    // text (out of memory) the plain notice stands in for it.
    bool stageLine = false;
    for (uint8_t i = 0; lifeLog_ && i < lifeLog_->count; ++i) {
      if (lifeLog_->lines[i].kind == lifelog::LineKind::Stage) stageLine = true;
    }
    if (visit_.stagesGained > 0 && !stageLine) {
      snprintf(line, sizeof(line), ctr(CULT_STAGES_GAINED_FORMAT), static_cast<unsigned>(visit_.stagesGained));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    // v0.2.1: depth.
    if (visit_.depthGained > 0) {
      snprintf(line, sizeof(line), ctr(CULT_DEPTH_GAINED_FORMAT), static_cast<unsigned>(visit_.depthGained),
               static_cast<unsigned>(breakthrough::depth(s)), static_cast<unsigned>(breakthrough::kDepthMax));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 3);
    }
    // v0.2.0: alchemy news.
    if (visit_.qiPillSeconds > 0) {
      snprintf(line, sizeof(line), ctr(CULT_QI_PILL_SUMMARY_FORMAT),
               static_cast<unsigned>((visit_.qiPillSeconds + 59u) / 60u));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    if (alchemy::ready(s)) {
      snprintf(line, sizeof(line), ctr(CULT_CAULDRON_READY_SUMMARY_FORMAT),
               ui::pillName(alchemy::recipesOf(s).recipes[alchemy::brewingRecipe(s)].pill));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    // v0.2.6: Daos found or grown, and daily tasks waiting.
    y = drawDaoNews(x, y + 4, width);
    if (daily::waiting(s) == 1) {
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, ctr(CULT_DAILY_WAITING_ONE), 2);
    } else if (daily::waiting(s) > 1) {
      snprintf(line, sizeof(line), ctr(CULT_DAILY_WAITING_SUMMARY_FORMAT), static_cast<unsigned>(daily::waiting(s)));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    y += metrics.verticalSpacing * 2;
  }

  summaryNext_ = first;
  if (!lifeLog_) return;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (uint8_t i = first; i < lifeLog_->count; ++i) {
    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, lifeLog_->lines[i].text, width, 6);
    const int height = static_cast<int>(wrapped.size()) * lineHeight;
    if (y + height > bottom && i != first) break;  // the rest goes on the next page
    for (const auto& w : wrapped) {
      renderer.drawText(UI_10_FONT_ID, x, y, w.c_str());
      y += lineHeight;
    }
    y += metrics.verticalSpacing;
    summaryNext_ = static_cast<uint8_t>(i + 1);
  }
}

// The Chronicle, one page at a time, read straight from the card: each
// paragraph is read into a 256-byte stack buffer, wrapped and drawn. Headings
// ("Age 17", "Life 1: ...") are bold. Reading it records no reading stats:
// it is the game's own screen, not CrossInk's reader.
void CultivationActivity::renderChronicle(const int x, int y, const int width, int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!chronicle_) return;
  if (notice_[0] != '\0') {  // after an export: where the copy went, under the page
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, notice_, width, 3);
    const int noticeTop = bottom - static_cast<int>(lines.size()) * renderer.getLineHeight(SMALL_FONT_ID);
    drawWrapped(SMALL_FONT_ID, x, noticeTop, width, notice_, 3);
    bottom = noticeTop - metrics.verticalSpacing;
  }
  ChronicleView& v = *chronicle_;
  v.more = false;
  v.next = v.starts[v.cur];

  FsFile f;
  if (!Storage.openFileForRead("CULT", chroniclePath(), f)) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_CHRONICLE_EMPTY), 3);
    return;
  }
  bool drewAny = false;
  // A page whose next page is already known (found going back) stops there,
  // so the same lines never show on both.
  const uint32_t end = v.cur + 1 < v.count ? v.starts[v.cur + 1] : v.size;
  const uint32_t next = chronicleLayout(f, end, v.starts[v.cur], x, y, width, bottom, true, &drewAny);
  f.close();
  v.next = next;
  v.more = next < v.size;
  if (!drewAny) drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_CHRONICLE_EMPTY), 3);
}

// Lays out (and with `draw`, draws) one page from `start`; returns where the
// next page starts. The same code measures pages for Previous and Newest, so
// the breaks it finds are the ones drawn.
uint32_t CultivationActivity::chronicleLayout(FsFile& f, const uint32_t size, const uint32_t start, const int x, int y,
                                              const int width, const int bottom, const bool draw, bool* drewAny) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  uint32_t offset = start;
  *drewAny = false;
  char buf[256];
  while (offset < size) {
    if (!f.seek(offset)) break;
    const int got = f.read(buf, sizeof(buf) - 1);
    if (got <= 0) break;
    size_t len = static_cast<size_t>(got);
    const auto* newline = static_cast<const char*>(memchr(buf, '\n', len));
    size_t consumed = len;
    if (newline != nullptr) {
      len = static_cast<size_t>(newline - buf);
      consumed = len + 1;
    }
    if (len > 0 && buf[len - 1] == '\r') --len;
    buf[len] = '\0';

    if (len == 0) {  // a blank line: a little space, but never at the top of a page
      if (*drewAny) y += metrics.verticalSpacing;
      offset += consumed;
      continue;
    }
    const bool heading =
        strncmp(buf, "Age ", 4) == 0 || strncmp(buf, "Life ", 5) == 0 || strncmp(buf, "The Chronicle of ", 17) == 0;
    const int font = heading ? UI_12_FONT_ID : UI_10_FONT_ID;
    const EpdFontFamily::Style style = heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int lineHeight = renderer.getLineHeight(font);
    const auto wrapped = renderer.wrappedText(font, buf, width, 8, style);
    const int height = static_cast<int>(wrapped.size()) * lineHeight;
    // Keep a heading with what follows it: room for a full entry (up to 3 lines) below it.
    const int needed = heading ? height + 3 * renderer.getLineHeight(UI_10_FONT_ID) : height;
    if (*drewAny && y + needed > bottom) break;
    for (const auto& w : wrapped) {
      if (draw) renderer.drawText(font, x, y, w.c_str(), true, style);
      y += lineHeight;
    }
    y += 4;
    *drewAny = true;
    offset += consumed;
  }
  return offset;
}

// v0.2.3: the jump menu; Select chooses, Back returns to the page.
void CultivationActivity::renderChronicleMenu(const int top, const int bottom) {
  static const GameStr kRows[kChronicleMenuRows] = {
      GameStr::CULT_CHRONICLE_NEWEST,        GameStr::CULT_CHRONICLE_BEGINNING, GameStr::CULT_CHRONICLE_YEARS_BACK,
      GameStr::CULT_CHRONICLE_YEARS_FORWARD, GameStr::CULT_CHRONICLE_EXPORT,    GameStr::CULT_CHRONICLE_ROW_PAST};
  const auto& metrics = UITheme::getInstance().getMetrics();
  int height = metrics.listRowHeight * kChronicleMenuRows;
  if (top + height > bottom) height = bottom - top;
  GUI.drawList(renderer, Rect{0, top, renderer.getScreenWidth(), height}, kChronicleMenuRows, selected_,
               [](const int i) -> std::string { return text(kRows[i]); });
}

#endif  // CROSSINK_CULTIVATION
