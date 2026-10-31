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
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "CultivatorSleepScreen.h"
#include "GameValues.h"
#include "MappedInputManager.h"
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

constexpr char kDamagedSavePath[] = "/.crosspoint/cultivation/save.damaged.bin";

// Text tables: static const, so they stay in flash.
static const StrId kRealmNames[kMortalRealmCount] = {
    StrId::STR_CULT_REALM_0, StrId::STR_CULT_REALM_1, StrId::STR_CULT_REALM_2, StrId::STR_CULT_REALM_3,
    StrId::STR_CULT_REALM_4, StrId::STR_CULT_REALM_5, StrId::STR_CULT_REALM_6, StrId::STR_CULT_REALM_7,
    StrId::STR_CULT_REALM_8, StrId::STR_CULT_REALM_9};
static const StrId kStageNames[4] = {StrId::STR_CULT_STAGE_EARLY, StrId::STR_CULT_STAGE_MIDDLE,
                                     StrId::STR_CULT_STAGE_LATE, StrId::STR_CULT_STAGE_PEAK};
static const StrId kGenderNames[static_cast<int>(Gender::Count)] = {
    StrId::STR_CULT_GENDER_MALE, StrId::STR_CULT_GENDER_FEMALE, StrId::STR_CULT_GENDER_NONBINARY,
    StrId::STR_CULT_GENDER_UNSPECIFIED};
static const StrId kOriginNames[static_cast<int>(Origin::Count)] = {
    StrId::STR_CULT_ORIGIN_FARMER, StrId::STR_CULT_ORIGIN_CLAN_HEIR, StrId::STR_CULT_ORIGIN_ORPHAN,
    StrId::STR_CULT_ORIGIN_SECT_SERVANT, StrId::STR_CULT_ORIGIN_HERBALIST};
static const StrId kBirthLines[static_cast<int>(Origin::Count)] = {
    StrId::STR_CULT_BIRTH_FARMER, StrId::STR_CULT_BIRTH_CLAN_HEIR, StrId::STR_CULT_BIRTH_ORPHAN,
    StrId::STR_CULT_BIRTH_SECT_SERVANT, StrId::STR_CULT_BIRTH_HERBALIST};
static const StrId kOriginGifts[static_cast<int>(Origin::Count)] = {
    StrId::STR_CULT_GIFT_FARMER, StrId::STR_CULT_GIFT_CLAN_HEIR, StrId::STR_CULT_GIFT_ORPHAN,
    StrId::STR_CULT_GIFT_SECT_SERVANT, StrId::STR_CULT_GIFT_HERBALIST};
static const StrId kElementNames[static_cast<int>(Element::Count)] = {
    StrId::STR_CULT_ELEMENT_WOOD, StrId::STR_CULT_ELEMENT_FIRE, StrId::STR_CULT_ELEMENT_EARTH,
    StrId::STR_CULT_ELEMENT_METAL, StrId::STR_CULT_ELEMENT_WATER};
static const StrId kElementColors[static_cast<int>(Element::Count)] = {
    StrId::STR_CULT_COLOR_WOOD, StrId::STR_CULT_COLOR_FIRE, StrId::STR_CULT_COLOR_EARTH, StrId::STR_CULT_COLOR_METAL,
    StrId::STR_CULT_COLOR_WATER};
static const StrId kFoundationGrades[5] = {StrId::STR_CULT_FOUNDATION_1, StrId::STR_CULT_FOUNDATION_2,
                                           StrId::STR_CULT_FOUNDATION_3, StrId::STR_CULT_FOUNDATION_4,
                                           StrId::STR_CULT_FOUNDATION_5};
static const StrId kCoreGrades[10] = {StrId::STR_CULT_CORE_1, StrId::STR_CULT_CORE_2, StrId::STR_CULT_CORE_3,
                                      StrId::STR_CULT_CORE_4, StrId::STR_CULT_CORE_5, StrId::STR_CULT_CORE_6,
                                      StrId::STR_CULT_CORE_7, StrId::STR_CULT_CORE_8, StrId::STR_CULT_CORE_9,
                                      StrId::STR_CULT_CORE_10};
static const StrId kSoulClarities[4] = {StrId::STR_CULT_SOUL_1, StrId::STR_CULT_SOUL_2, StrId::STR_CULT_SOUL_3,
                                        StrId::STR_CULT_SOUL_4};
constexpr uint32_t kKindOutcomeLine = 56;  // contentSeed kinds: 55 is the breakthrough roll
constexpr uint32_t kKindGreatPerfectionLine = 57;

static const StrId kTalentNames[kTalentCount] = {StrId::STR_CULT_TALENT_CHOKED, StrId::STR_CULT_TALENT_DULL,
                                                 StrId::STR_CULT_TALENT_ORDINARY, StrId::STR_CULT_TALENT_KEEN,
                                                 StrId::STR_CULT_TALENT_HEAVEN_BLESSED};

constexpr int kNameRows = 3;  // Name, Suggest, Continue
constexpr int kNameRowName = 0;
constexpr int kNameRowSuggest = 1;

const char* text(const StrId id) { return I18N.get(id); }

// "x0.60" style multiplier from permille.
void formatRate(char* buf, const size_t len, const uint32_t permille) {
  snprintf(buf, len, tr(STR_CULT_QI_RATE_FORMAT), static_cast<unsigned>(permille / 1000),
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
    snprintf(halfStep, sizeof(halfStep), tr(STR_CULT_HALF_STEP_FORMAT), text(kRealmNames[p.realm + 1]));
    add(halfStep);
  }
}
void stageName(const GameState& s, char* out, const size_t len) {
  const ProgressBlock& p = s.progress;
  if (breakthrough::inGreatPerfection(s)) {
    snprintf(out, len, "%s", tr(STR_CULT_GREAT_PERFECTION));
  } else if (realmInfo(p.realm).layered) {
    snprintf(out, len, tr(STR_CULT_LAYER_FORMAT), static_cast<unsigned>(p.stage + 1));
  } else {
    snprintf(out, len, "%s", text(kStageNames[p.stage < 4 ? p.stage : 3]));
  }
}
void versionLine(char* out, const size_t len) {
  snprintf(out, len, tr(STR_CULT_VERSION_FORMAT), CULTIVATION_GAME_VERSION);
}
void formatPermille(char* buf, const size_t len, const uint32_t permille) {
  snprintf(buf, len, "x%u.%02u", static_cast<unsigned>(permille / 1000), static_cast<unsigned>((permille % 1000) / 10));
}

const char* gradeWord(const inventory::Grade g) {
  switch (g) {
    case inventory::Grade::Spirit:
      return tr(STR_CULT_GRADE_SPIRIT);
    case inventory::Grade::Rare:
      return tr(STR_CULT_GRADE_RARE);
    default:
      return tr(STR_CULT_GRADE_COMMON);
  }
}

const char* pillName(const alchemy::Pill pill) {
  using alchemy::Pill;
  switch (pill) {
    case Pill::QiGathering:
      return tr(STR_CULT_PILL_QI);
    case Pill::TonicMinor:
      return tr(STR_CULT_ITEM_TONIC_MINOR);
    case Pill::BreakthroughCommon:
      return tr(STR_CULT_PILL_BT_COMMON);
    case Pill::FoundationBuilding:
      return tr(STR_CULT_PILL_FOUNDATION);
    case Pill::CoreForming:
      return tr(STR_CULT_PILL_CORE);
    case Pill::TonicGreater:
      return tr(STR_CULT_ITEM_TONIC_GREATER);
    case Pill::SoulClearing:
      return tr(STR_CULT_PILL_SOUL);
    case Pill::BreakthroughSpirit:
      return tr(STR_CULT_PILL_BT_SPIRIT);
    case Pill::TonicFull:
      return tr(STR_CULT_ITEM_TONIC_FULL);
    case Pill::BreakthroughRare:
      return tr(STR_CULT_PILL_BT_RARE);
    default:
      return "";
  }
}

namespace {
const char* qualityWord(const inventory::Quality q) {
  switch (q) {
    case inventory::Quality::High:
      return tr(STR_CULT_PILL_HIGH);
    case inventory::Quality::Medium:
      return tr(STR_CULT_PILL_MEDIUM);
    default:
      return tr(STR_CULT_PILL_LOW);
  }
}
// "Core-Forming pill · high", with the first letter capitalised.
void withQuality(const char* base, const inventory::Quality q, char* out, const size_t len) {
  snprintf(out, len, tr(STR_CULT_PILL_QUALITY_FORMAT), base, qualityWord(q));
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
    snprintf(out, len, tr(STR_CULT_ITEM_HERB_FALLBACK_FORMAT), static_cast<unsigned>(inventory::regionOf(item) + 1));
  } else if (inventory::isOre(item)) {
    snprintf(out, len, tr(STR_CULT_ITEM_ORE_FALLBACK_FORMAT), static_cast<unsigned>(inventory::regionOf(item) + 1));
  } else if (inventory::isBreakthroughPill(item)) {
    withQuality(pillName(kBreakthroughPills[inventory::pillGroup(item)]), inventory::pillQuality(item), out, len);
  } else if (inventory::isGradePill(item)) {
    withQuality(pillName(kGradePills[inventory::pillGroup(item)]), inventory::pillQuality(item), out, len);
  } else {
    const char* name = "";
    switch (item) {
      case Item::TonicMinor:
        name = tr(STR_CULT_ITEM_TONIC_MINOR);
        break;
      case Item::TonicGreater:
        name = tr(STR_CULT_ITEM_TONIC_GREATER);
        break;
      case Item::TonicFull:
        name = tr(STR_CULT_ITEM_TONIC_FULL);
        break;
      case Item::ThunderTalisman:
        name = tr(STR_CULT_ITEM_THUNDER);
        break;
      case Item::CalmIncense:
        name = tr(STR_CULT_ITEM_INCENSE);
        break;
      case Item::RegionTalisman:
        name = tr(STR_CULT_ITEM_REGION);
        break;
      case Item::QiPill:
        name = tr(STR_CULT_PILL_QI);
        break;
      case Item::RecipeSlip:
        name = tr(STR_CULT_ITEM_SLIP);
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
    snprintf(out, len, inventory::isHerb(item) ? tr(STR_CULT_ITEM_HERB_DESC_FORMAT) : tr(STR_CULT_ITEM_ORE_DESC_FORMAT),
             gradeWord(inventory::gradeOfRegion(inventory::regionOf(item))));
    return;
  }
  if (inventory::isBreakthroughPill(item)) {
    const unsigned bonus = alchemy::breakthroughBonus(item);
    const uint8_t group = inventory::pillGroup(item);
    if (group >= 2) {
      snprintf(out, len, tr(STR_CULT_ITEM_BT_DESC_ANY_FORMAT), bonus);
    } else {
      snprintf(out, len, tr(STR_CULT_ITEM_BT_DESC_FORMAT), bonus, realmName(group == 0 ? 4 : 7));
    }
    return;
  }
  if (inventory::isGradePill(item)) {
    static const StrId kKinds[3] = {StrId::STR_CULT_GRADE_KIND_FOUNDATION, StrId::STR_CULT_GRADE_KIND_CORE,
                                    StrId::STR_CULT_GRADE_KIND_SOUL};
    snprintf(out, len, tr(STR_CULT_ITEM_GRADE_DESC_FORMAT), static_cast<unsigned>(alchemy::gradePillPoints(item)),
             I18N.get(kKinds[inventory::pillGroup(item)]));
    return;
  }
  switch (item) {
    case Item::TonicMinor:
    case Item::TonicGreater:
    case Item::TonicFull:
      snprintf(out, len, tr(STR_CULT_ITEM_TONIC_DESC_FORMAT), static_cast<unsigned>(inventory::healPercent(item)));
      return;
    case Item::ThunderTalisman:
      snprintf(out, len, "%s", tr(STR_CULT_ITEM_THUNDER_DESC));
      return;
    case Item::CalmIncense:
      snprintf(out, len, "%s", tr(STR_CULT_ITEM_INCENSE_DESC));
      return;
    case Item::RegionTalisman:
      snprintf(out, len, "%s", tr(STR_CULT_ITEM_REGION_DESC));
      return;
    case Item::QiPill:
      snprintf(out, len, "%s", tr(STR_CULT_ITEM_QI_DESC));
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
// clock it changes with each reading hour instead.
void CultivationActivity::updateRumor() {
  rumor_[0] = '\0';
  if (!grammar_ || !state_ || !state_->isCreated()) return;
  constexpr uint32_t kKindRumor = 50;
  const uint32_t day = state_->snapshot.lastVisitDay;
  const uint32_t when = day != StatsSnapshot::kNoDay ? day : 0x80000000u + state_->progress.gameReadingSeconds / 3600u;
  GrammarContext ctx;
  ctx.plane = 0;  // the Mortal Plane until ascension exists
  ctx.values = gameValues(*state_);
  grammar_->beginScene();
  const char* text = grammar_->expand("{rumor}", contentSeed(worldSeedOf(*state_), kKindRumor, when), ctx);
  snprintf(rumor_, sizeof(rumor_), "%s", text);
  packs_->closeFile();
  if (grammar_->missing() > 0) {
    LOG_ERR("CULT", "Rumor had %u missing key(s)", grammar_->missing());
  }
  if (Storage.exists(sleepscreen::kOldRumorPath)) Storage.remove(sleepscreen::kOldRumorPath);  // v0.1.8 leftover
}

bool CultivationActivity::persist() {
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
  const StatsSample now = readLocalStats();
  const VisitGains gains = bridge::collect(state_->snapshot, now, s_revisionWatch);
  originGift_ = grantOriginGifts(*state_);  // only for characters made before origin gifts existed
  // v0.2.0: a v0.1.10 bag moves to the new one; an older Herbalist learns the Qi Gathering recipe.
  if (inventory::migrateLegacyBag(*state_)) LOG_INF("CULT", "Moved the v0.1.10 bag to the v0.2.0 one");
  alchemy::grantStartingRecipe(*state_);
  lifelog::VisitChange change;
  change.ageMonthsBefore = state_->progress.ageMonths;
  const bool bottleneckBefore = atBottleneck(*state_);
  visit_ = absorb(*state_, gains);
  change.ageMonthsAfter = state_->progress.ageMonths;
  change.stagesGained = visit_.stagesGained;
  // Only the first time the bar fills in a realm: after a failed breakthrough,
  // refilling it is not news (found in the simulator).
  change.reachedBottleneck = !bottleneckBefore && atBottleneck(*state_) && state_->progress.failures == 0;
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
  }
  // Books finished since the last visit become scrolls (Chronicle lines first,
  // like the life log). minted.bin and the pending list are only updated
  // once the save holds the new scrolls, so a failed save mints them again.
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
    return;
  }
  lifelog::build(*lifeLog_, change, visit_.monthsPassed, *state_, grammar_.get());
  appendUpheavalLines(upheavals_);  // Spatial Upheaval: after the visit's own lines
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  chronicle::appendVisit(*lifeLog_);
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
    RenderLock lock(*this);
    notice_[0] = '\0';
    chronicle_ = std::move(view);
    screen_ = Screen::Chronicle;
  }
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
    actions_[actionCount_++] = Action::Techniques;
    actions_[actionCount_++] = Action::Explore;  // v0.1.10; says when the first region opens
    actions_[actionCount_++] = Action::Alchemy;  // v0.2.0
  }
  if (chronicleExists_) actions_[actionCount_++] = Action::Chronicle;
  if (selected_ >= actionCount_) selected_ = 0;
}

void CultivationActivity::runAction() {
  if (selected_ < 0 || selected_ >= actionCount_) return;
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
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

void CultivationActivity::startCreation() {
  draft_ = CharacterDraft{};
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    LOG_ERR("CULT", "Could not read the device MAC; using zeros");
  }
  const StatsSample now = readLocalStats();
  draft_.worldSeed = worldSeed(mac, now.today, 1);
  draft_.rerollsAllowed = kBaseRerolls;  // + Tested Again ranks once legacy exists (milestone 10)
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
  scrollstore::clearForNewLife();                    // books finished before this life give nothing (baseline rule)
  chronicle::writeOpening(*state_, grammar_.get());  // a new chapter; the book's title too if it is new
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

  if (handleExplore(back, confirm, up, down)) return;  // exploration screens (CultivationExploreScreens.cpp)
  if (handleAlchemy(back, confirm, up, down)) return;  // alchemy screens (CultivationAlchemyScreens.cpp)

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
        alchemy::cycleChoice(*state_, c.target, c.needsKey && c.keyIndex < 0);
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
      } else if (confirm) {
        // Export (user request): a copy the player can see and copy off the card.
        // Under the render lock: the page being drawn reads the same file.
        RenderLock lock(*this);
        char path[96];
        const bool ok = chronicle::exportCopy(*state_, path, sizeof(path));
        if (ok) {
          snprintf(notice_, sizeof(notice_), tr(STR_CULT_EXPORTED_FORMAT), path);
        } else {
          snprintf(notice_, sizeof(notice_), "%s", tr(STR_CULT_EXPORT_FAILED));
        }
        requestUpdate();
      } else if (down && v.more) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        if (v.depth + 1 < ChronicleView::kHistory) {
          ++v.depth;
        } else {  // forget the oldest page
          memmove(v.starts, v.starts + 1, sizeof(v.starts) - sizeof(v.starts[0]));
        }
        v.starts[v.depth] = v.next;
        ++v.pageNumber;
        requestUpdate();
      } else if (up && v.depth > 0) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        --v.depth;
        --v.pageNumber;
        requestUpdate();
      }
      return;
    }

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
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CULT_NAME_PROMPT),
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

  const char* title = tr(STR_CULT_TITLE);
  if (screen_ == Screen::Name || screen_ == Screen::Gender || screen_ == Screen::Origin) title = tr(STR_CULT_NEW_LIFE);
  if (screen_ == Screen::Awakening) title = tr(STR_CULT_AWAKENING);
  if (screen_ == Screen::Breakthrough) title = tr(STR_CULT_BREAKTHROUGH);
  if (screen_ == Screen::Trial) {
    title = trials::kindFor(state_->progress.realm + 1) == trials::Kind::HeartDemon ? tr(STR_CULT_HEART_DEMON)
                                                                                    : tr(STR_CULT_TRIBULATION);
  }
  if (screen_ == Screen::GreatPerfection) title = tr(STR_CULT_GREAT_PERFECTION);
  if (screen_ == Screen::Outcome) {
    title =
        outcomeKind_ == OutcomeKind::EnteredGreatPerfection ? tr(STR_CULT_GREAT_PERFECTION) : tr(STR_CULT_BREAKTHROUGH);
    if (outcomeKind_ == OutcomeKind::TrialLost) {
      title = trials::kindFor(outcome_.toRealm) == trials::Kind::HeartDemon ? tr(STR_CULT_HEART_DEMON)
                                                                            : tr(STR_CULT_TRIBULATION);
    }
  }
  if (screen_ == Screen::ScrollNews) title = tr(STR_CULT_SCROLLS_TITLE);
  if (screen_ == Screen::BetterManual) title = tr(STR_CULT_BETTER_MANUAL);
  if (screen_ == Screen::Techniques || screen_ == Screen::ScrollDetail) title = tr(STR_CULT_TECHNIQUES);
  if (screen_ == Screen::SellConfirm) title = tr(STR_CULT_SELL);
  char exploreTitleBuf[ExploreView::kRegionNameSize];
  if (screen_ == Screen::Regions || screen_ == Screen::Map || screen_ == Screen::Fight || screen_ == Screen::Merchant ||
      screen_ == Screen::MerchantSell || screen_ == Screen::Bag || screen_ == Screen::Talisman) {
    title = exploreTitle(exploreTitleBuf, sizeof(exploreTitleBuf));
  }
  if (screen_ == Screen::Alchemy || screen_ == Screen::Recipes) title = tr(STR_CULT_ALCHEMY_TITLE);
  if (screen_ == Screen::Experiment) title = tr(STR_CULT_ALCHEMY_EXPERIMENT);
  char recipeTitle[64];
  if (screen_ == Screen::RecipeDetail && alchemy_) {
    snprintf(recipeTitle, sizeof(recipeTitle), "%s",
             pillLabel(alchemy::recipesOf(*state_).recipes[alchemy_->recipe].pill));
    if (recipeTitle[0] >= 'a' && recipeTitle[0] <= 'z') recipeTitle[0] = static_cast<char>(recipeTitle[0] - 'a' + 'A');
    title = recipeTitle;
  }
  char chronicleTitle[48];
  if (screen_ == Screen::Chronicle && chronicle_) {
    snprintf(chronicleTitle, sizeof(chronicleTitle), tr(STR_CULT_CHRONICLE_TITLE_FORMAT),
             static_cast<unsigned long>(chronicle_->pageNumber));
    title = chronicleTitle;
  }
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
      confirmLabel = tr(STR_CULT_KEEP);
      upLabel = rollView_ > 0 ? tr(STR_CULT_PREVIOUS) : "";
      const bool canReroll = static_cast<uint8_t>(rollCount_ - 1) < draft_.rerollsAllowed && rollCount_ < kMaxRolls;
      downLabel = rollView_ + 1 < rollCount_ ? tr(STR_CULT_NEXT) : (canReroll ? tr(STR_CULT_REROLL) : "");
      break;
    }
    case Screen::Summary:
      renderSummary(x, top, width, bottom);
      confirmLabel = tr(STR_CULT_CONTINUE);
      upLabel = summaryPage_ > 0 ? tr(STR_CULT_PREVIOUS) : "";
      downLabel = lifeLog_ && summaryNext_ < lifeLog_->count ? tr(STR_CULT_NEXT) : "";
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
          breakthrough::canAttempt(*state_) && !(lightning && state_->progress.hp == 0) ? tr(STR_CULT_ATTEMPT) : "";
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
        const bool onlyKey = c.needsKey && c.keyIndex < 0;
        if (fitting + (onlyKey ? 0 : 1) > 1) downLabel = tr(STR_CULT_CHANGE_PILL);
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
      confirmLabel = tr(STR_CULT_ENTER);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Outcome:
      renderOutcome(x, top, width);
      confirmLabel = tr(STR_CULT_CONTINUE);
      upLabel = "";
      downLabel = "";
      break;
    case Screen::Chronicle:
      renderChronicle(x, top, width, bottom);
      confirmLabel = tr(STR_CULT_EXPORT);
      upLabel = chronicle_ && chronicle_->depth > 0 ? tr(STR_CULT_PREVIOUS) : "";
      downLabel = chronicle_ && chronicle_->more ? tr(STR_CULT_NEXT) : "";
      break;
    case Screen::ScrollNews: {
      renderScrollNews(x, top, width, bottom);
      const bool more = news_ && news_->next < news_->count && news_->next > news_->pageStarts[news_->page];
      confirmLabel = more ? tr(STR_CULT_NEXT) : tr(STR_CULT_CONTINUE);
      upLabel = news_ && news_->page > 0 ? tr(STR_CULT_PREVIOUS) : "";
      downLabel = more ? tr(STR_CULT_NEXT) : "";
      break;
    }
    case Screen::BetterManual:
      renderBetterManual(x, top, width);
      confirmLabel = tr(STR_CULT_EQUIP);
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
      confirmLabel = tr(STR_CULT_SELL);
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
    case Screen::Alchemy:
      renderAlchemy(x, top, width, bottom);
      alchemyLabels(confirmLabel, upLabel, downLabel);
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
      drawWrapped(UI_10_FONT_ID, x, top, width, tr(STR_CULT_OUT_OF_MEMORY), 4);
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
    drawWrapped(SMALL_FONT_ID, x, listBottom, width, tr(STR_CULT_NAME_INVALID), 2);
  }
  GUI.drawList(
      renderer, Rect{0, top, renderer.getScreenWidth(), listBottom - top}, kNameRows, selected_,
      [](const int i) -> std::string {
        if (i == kNameRowName) return tr(STR_CULT_NAME);
        if (i == kNameRowSuggest) return tr(STR_CULT_NAME_SUGGEST);
        return tr(STR_CULT_CONTINUE);
      },
      nullptr, nullptr,
      [this](const int i) -> std::string {
        if (i != kNameRowName) return {};
        return draft_.name[0] != '\0' ? std::string(draft_.name) : std::string(tr(STR_CULT_NAME_NONE));
      });
}

void CultivationActivity::renderGender(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int noteTop = bottom - renderer.getLineHeight(SMALL_FONT_ID) * 2;
  drawWrapped(SMALL_FONT_ID, x, noteTop, width, tr(STR_CULT_GENDER_HINT), 2);
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

  snprintf(line, sizeof(line), tr(STR_CULT_ROLL_FORMAT), static_cast<unsigned>(rollView_ + 1),
           static_cast<unsigned>(rollCount_));
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  // DOC: "The elder's testing stone glowed a murky green."
  const StrId glow = roll.purity < 34   ? StrId::STR_CULT_GLOW_MURKY
                     : roll.purity < 67 ? StrId::STR_CULT_GLOW_STEADY
                                        : StrId::STR_CULT_GLOW_BRILLIANT;
  snprintf(line, sizeof(line), tr(STR_CULT_STONE_FORMAT), text(glow),
           text(kElementColors[static_cast<int>(roll.element)]));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;

  snprintf(line, sizeof(line), tr(STR_CULT_ROOT_FORMAT), text(kElementNames[static_cast<int>(roll.element)]),
           static_cast<unsigned>(roll.purity));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_TALENT_FORMAT), text(kTalentNames[roll.talent]));
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  formatRate(line, sizeof(line), destinyPermille(roll));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

  const unsigned rerollsLeft = draft_.rerollsAllowed - static_cast<unsigned>(rollCount_ - 1);
  snprintf(line, sizeof(line), tr(STR_CULT_REROLLS_LEFT_FORMAT), rerollsLeft);
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_AWAKENING_HINT), 3);
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
  snprintf(line, sizeof(line), tr(STR_CULT_QI_FORMAT), have, need);
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
    snprintf(line, sizeof(line), tr(STR_CULT_RUMOR_FORMAT), rumor_);
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
  snprintf(ageLine, sizeof(ageLine), tr(STR_CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
           static_cast<unsigned>(realm.lifespanYears));
  snprintf(daoLine, sizeof(daoLine), tr(STR_CULT_DAO_HEART_FORMAT), static_cast<unsigned>(p.daoHeart));
  snprintf(rootLine, sizeof(rootLine), tr(STR_CULT_ROOT_FORMAT),
           text(kElementNames[static_cast<int>(s.character.rootElement)]),
           static_cast<unsigned>(s.character.rootPurity));
  snprintf(talentLine, sizeof(talentLine), tr(STR_CULT_TALENT_FORMAT),
           text(kTalentNames[s.character.talent % kTalentCount]));
  const uint32_t rate = qiMultiplierPermille(s);
  if (scrolls::equippedManual(s) >= 0) {
    const uint32_t manual = scrolls::equippedManualPermille(s);
    snprintf(rateLine, sizeof(rateLine), tr(STR_CULT_QI_RATE_MANUAL_FORMAT), static_cast<unsigned>(rate / 1000),
             static_cast<unsigned>((rate % 1000) / 10), static_cast<unsigned>(manual / 1000),
             static_cast<unsigned>((manual % 1000) / 10));
  } else {
    formatRate(rateLine, sizeof(rateLine), rate);
  }
  snprintf(stonesLine, sizeof(stonesLine), tr(STR_CULT_STONES_FORMAT), static_cast<unsigned long>(p.stones));
  // HP on the Stamina line (v0.1.9: Tribulations spend it, reading heals it).
  snprintf(staminaLine, sizeof(staminaLine), tr(STR_CULT_STAMINA_FORMAT), static_cast<unsigned>(p.stamina),
           static_cast<unsigned>(kStaminaCap));
  snprintf(hpLine, sizeof(hpLine), tr(STR_CULT_HP_FORMAT), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));

  const bool gp = breakthrough::inGreatPerfection(s);
  const int fixedStats =
      rowStep * (pairRows(ageLine, daoLine) + pairRows(rateLine, stonesLine) + pairRows(staminaLine, hpLine)) +
      (gp ? textLine + 4 : 0) + metrics.verticalSpacing * 2 - 4;
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

  if (gp) {
    snprintf(line, sizeof(line), tr(STR_CULT_CONSOLIDATION_FORMAT), static_cast<unsigned>(p.consolidation),
             static_cast<unsigned>(breakthrough::kConsolidationMax));
    renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += textLine + 4;
  }
  y += metrics.verticalSpacing * 2 - 4;

  // What this visit brought.
  if (justCreated_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_WELCOME), fits(textLine, 3));
  } else if (visitReset_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_RESET), fits(textLine, 3));
  } else if (visitSeconds_ == 0) {
    const int needed =
        static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, tr(STR_CULT_VISIT_NONE), width, 3).size()) * textLine;
    if (y + needed + rumorReserve <= listTop) y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_NONE), 3);
  }  // otherwise the Return summary already showed what this visit brought
  if (originGift_.stones > 0 || originGift_.hp > 0) {
    if (originGift_.hp > 0) {
      snprintf(line, sizeof(line), tr(STR_CULT_ORIGIN_GIFT_HP_FORMAT), static_cast<unsigned long>(originGift_.stones),
               static_cast<unsigned>(originGift_.hp));
    } else {
      snprintf(line, sizeof(line), tr(STR_CULT_ORIGIN_GIFT_FORMAT), static_cast<unsigned long>(originGift_.stones));
    }
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  }
  // At a bottleneck the "Break through" row says what to do; only a missing
  // key needs a line of its own (the Breakthrough screen explains which keys).
  if (atBottleneck(s)) {
    const breakthrough::Chance c = breakthrough::chanceFor(s);
    if (c.needsKey && !c.hasKey && c.target < kMortalRealmCount) {
      snprintf(line, sizeof(line), tr(STR_CULT_BOTTLENECK_KEY_SHORT_FORMAT), text(kRealmNames[c.target]));
      y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, line, fits(smallLine, 1));
    }
  }
  if (rumor_[0] != '\0') {
    snprintf(line, sizeof(line), tr(STR_CULT_RUMOR_FORMAT), rumor_);
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
  if (packs_ && packs_->sdProblems() > 0)
    y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_PACK_PROBLEMS), 3);
  if (damagedSave_ && justCreated_) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_DAMAGED), 3);
  if (saveFailed_) drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_FAILED), 2);

  if (actionCount_ > 0) {
    // Two columns, reading order; Up/Down walk the cells.
    const int screenWidth = renderer.getScreenWidth();
    const int cellWidth = screenWidth / 2;
    const int pad = metrics.contentSidePadding;
    for (int i = 0; i < actionCount_; ++i) {
      const int cx = (i % 2) * cellWidth;
      const int cy = listTop + (i / 2) * cellHeight;
      const char* label = "";
      switch (actions_[i]) {
        case Action::Breakthrough:
          label = tr(STR_CULT_ACTION_BREAKTHROUGH);
          break;
        case Action::GreatPerfection:
          label = tr(STR_CULT_ACTION_GREAT_PERFECTION);
          break;
        case Action::Techniques:
          label = tr(STR_CULT_ACTION_TECHNIQUES);
          break;
        case Action::Explore:
          label = tr(STR_CULT_ACTION_EXPLORE);
          break;
        case Action::Alchemy:
          label = tr(STR_CULT_ACTION_ALCHEMY);
          break;
        case Action::Chronicle:
          label = tr(STR_CULT_ACTION_CHRONICLE);
          break;
        case Action::Trial:
          label = trials::kindFor(state_->progress.realm + 1) == trials::Kind::HeartDemon
                      ? tr(STR_CULT_ACTION_DEMON)
                      : tr(STR_CULT_ACTION_TRIBULATION);
          break;
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

  snprintf(line, sizeof(line), tr(STR_CULT_INTO_FORMAT), text(kRealmNames[c.target]));
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_FORMAT), static_cast<unsigned>(c.total));
  renderer.drawText(UI_12_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

  auto part = [&](const char* textLine) {
    renderer.drawText(UI_10_FONT_ID, x + 16, y, textLine);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  };
  snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_BASE_FORMAT), static_cast<unsigned>(c.base));
  part(line);
  snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_DAO_HEART_FORMAT), static_cast<unsigned>(c.daoHeart));
  part(line);
  if (breakthrough::inGreatPerfection(s) || c.consolidation > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_CONSOLIDATION_FORMAT), static_cast<unsigned>(c.consolidation));
    part(line);
  }
  if (c.element > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_ELEMENT_FORMAT), static_cast<unsigned>(c.element));
    part(line);
  }
  if (c.pill > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_PILL_FORMAT), static_cast<unsigned>(c.pill));
    part(line);
  }
  if (c.pity > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_PITY_FORMAT), static_cast<unsigned>(s.progress.failures),
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
    snprintf(line, sizeof(line), tr(STR_CULT_BT_TRIB_FORMAT), static_cast<unsigned>(strikes),
             static_cast<unsigned>(total), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4);
    if (p.hp == 0) {
      y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_BT_NO_HP), 3);
    } else if (p.hp < p.hpMax) {
      snprintf(line, sizeof(line), tr(STR_CULT_BT_HEAL_FORMAT), static_cast<unsigned>(p.hpMax - p.hp));
      y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
    }
    y += metrics.verticalSpacing;
  } else if (trial == trials::Kind::HeartDemon) {
    snprintf(line, sizeof(line), tr(STR_CULT_BT_DEMON_FORMAT), static_cast<unsigned>(trials::sceneChance(s)));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + metrics.verticalSpacing;
  }

  // v0.2.0: the breakthrough pill this attempt would take (Up/Down picks another or none).
  char pillName[64] = {};
  if (c.pillItem != inventory::Item::None) ui::itemName(c.pillItem, pillName, sizeof(pillName));
  if (c.needsKey && !c.hasKey) {
    const scrolls::Tier need = scrolls::keyTierFor(c.target);
    static const uint8_t kTierHours[] = {0, 3, 8, 15, 25};
    if (need == scrolls::Tier::Mortal) {
      snprintf(line, sizeof(line), tr(STR_CULT_BOTTLENECK_KEY_ANY_FORMAT), text(kRealmNames[c.target]));
    } else {
      snprintf(line, sizeof(line), tr(STR_CULT_BOTTLENECK_KEY_TIER_FORMAT), text(kRealmNames[c.target]),
               cultivation::ui::tierName(need), static_cast<unsigned>(kTierHours[static_cast<int>(need)]));
    }
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 5);
    const alchemy::Pill pill = c.target <= 4   ? alchemy::Pill::BreakthroughCommon
                               : c.target <= 7 ? alchemy::Pill::BreakthroughSpirit
                                               : alchemy::Pill::BreakthroughRare;
    snprintf(line, sizeof(line), tr(STR_CULT_BT_PILL_HINT_FORMAT), ui::pillName(pill));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  } else {
    if (c.needsKey && c.keyIndex >= 0 && nameBuf_[0] != '\0') {
      snprintf(line, sizeof(line), tr(STR_CULT_KEY_FORMAT), nameBuf_);
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + 4;
    }
    if (c.pillItem != inventory::Item::None) {
      snprintf(line, sizeof(line), c.pillIsKey ? tr(STR_CULT_BT_PILL_KEY_FORMAT) : tr(STR_CULT_BT_PILL_FORMAT),
               pillName);
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + 4;
    } else if (c.target >= 2 && alchemy::chosenPill(s, c.target) == inventory::Item::None &&
               state_->timers[9] == alchemy::kNoPillChoice) {
      y = drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_BT_PILL_NONE), 2) + 4;
    }
    y += metrics.verticalSpacing - 4;
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_BREAKTHROUGH_RISK), 4);
  }
  if (breakthrough::canEnterGreatPerfection(s)) {
    drawWrapped(SMALL_FONT_ID, x, y + metrics.verticalSpacing, width, tr(STR_CULT_GP_HINT), 3);
  }
}

void CultivationActivity::renderGreatPerfection(const int x, const int y, const int width) {
  char line[400];
  snprintf(line, sizeof(line), tr(STR_CULT_GP_EXPLAIN_FORMAT),
           text(kRealmNames[state_->progress.realm < kMortalRealmCount ? state_->progress.realm : 0]));
  drawWrapped(UI_10_FONT_ID, x, y, width, line, 12);
}

void CultivationActivity::renderOutcome(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];
  switch (outcomeKind_) {
    case OutcomeKind::BreakthroughSuccess:
      snprintf(line, sizeof(line), tr(STR_CULT_BREAKTHROUGH_SUCCESS_FORMAT),
               text(kRealmNames[outcome_.toRealm < kMortalRealmCount ? outcome_.toRealm : 0]));
      break;
    case OutcomeKind::BreakthroughFailure:
      snprintf(line, sizeof(line), "%s", tr(STR_CULT_BREAKTHROUGH_FAILED));
      break;
    case OutcomeKind::EnteredGreatPerfection:
      snprintf(line, sizeof(line), "%s", tr(STR_CULT_GP_ENTERED));
      break;
    case OutcomeKind::TrialLost:
      snprintf(line, sizeof(line), "%s",
               trials::kindFor(outcome_.toRealm) == trials::Kind::HeartDemon ? tr(STR_CULT_DEMON_LOST)
                                                                             : tr(STR_CULT_TRIB_LOST));
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
    snprintf(line, sizeof(line), tr(STR_CULT_BT_ROLL_FORMAT), static_cast<unsigned>(outcome_.roll),
             static_cast<unsigned>(outcome_.chance));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
  if (outcomeLine_.text[0] != '\0') {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, outcomeLine_.text, 6) + metrics.verticalSpacing;
  }
  const char* formed = ui::gradeName(outcome_.gradeKind, outcome_.grade);
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && formed[0] != '\0') {
    snprintf(line, sizeof(line),
             outcome_.gradeKind == breakthrough::GradeKind::Foundation ? tr(STR_CULT_FOUNDATION_FORMED_FORMAT)
                                                                       : tr(STR_CULT_GRADE_FORMED_FORMAT),
             formed, static_cast<unsigned>(outcome_.gradeScore));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  if ((outcomeKind_ == OutcomeKind::BreakthroughSuccess || outcomeKind_ == OutcomeKind::BreakthroughFailure) &&
      outcome_.pillUsed != inventory::Item::None) {
    char pill[64];
    ui::itemName(outcome_.pillUsed, pill, sizeof(pill));
    snprintf(line, sizeof(line), tr(STR_CULT_PILL_USED_FORMAT), pill);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && outcome_.keyUsed && nameBuf_[0] != '\0') {
    snprintf(line, sizeof(line), tr(STR_CULT_KEY_USED_FORMAT), nameBuf_);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  // v0.1.10: each realm opens the next region.
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && outcome_.toRealm >= 1 &&
      outcome_.toRealm <= explore::kRegionCount) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_REGION_OPENED), 3);
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughFailure || outcomeKind_ == OutcomeKind::TrialLost) {
    snprintf(line, sizeof(line), tr(STR_CULT_NEXT_CHANCE_FORMAT),
             static_cast<unsigned>(breakthrough::chanceFor(s).total));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::TrialLost && s.progress.hp < s.progress.hpMax) {
    snprintf(line, sizeof(line), tr(STR_CULT_HP_HEAL_FORMAT), static_cast<unsigned>(s.progress.hp),
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
      snprintf(line, sizeof(line), tr(STR_CULT_SEASONS_PASSED_FORMAT), static_cast<unsigned long>(seasons));
    } else if (seasons == 1) {
      snprintf(line, sizeof(line), "%s", tr(STR_CULT_SEASON_PASSED));
    } else if (months >= 2) {
      snprintf(line, sizeof(line), tr(STR_CULT_MONTHS_PASSED_FORMAT), static_cast<unsigned long>(months));
    } else if (months == 1) {
      snprintf(line, sizeof(line), "%s", tr(STR_CULT_MONTH_PASSED));
    } else {
      snprintf(line, sizeof(line), "%s", tr(STR_CULT_MOMENT_PASSED));
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
    snprintf(line, sizeof(line), tr(STR_CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
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
    snprintf(line, sizeof(line), tr(STR_CULT_VISIT_FORMAT), static_cast<unsigned long>(visitSeconds_ / 60), gained,
             static_cast<unsigned long>(visit_.stonesGained));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);

    // The stage line normally comes from the life log; without generated
    // text (out of memory) the plain notice stands in for it.
    bool stageLine = false;
    for (uint8_t i = 0; lifeLog_ && i < lifeLog_->count; ++i) {
      if (lifeLog_->lines[i].kind == lifelog::LineKind::Stage) stageLine = true;
    }
    if (visit_.stagesGained > 0 && !stageLine) {
      snprintf(line, sizeof(line), tr(STR_CULT_STAGES_GAINED_FORMAT), static_cast<unsigned>(visit_.stagesGained));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    // v0.2.0: alchemy news.
    if (visit_.qiPillSeconds > 0) {
      snprintf(line, sizeof(line), tr(STR_CULT_QI_PILL_SUMMARY_FORMAT),
               static_cast<unsigned>((visit_.qiPillSeconds + 59u) / 60u));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    }
    if (alchemy::ready(s)) {
      snprintf(line, sizeof(line), tr(STR_CULT_CAULDRON_READY_SUMMARY_FORMAT),
               ui::pillName(alchemy::recipesOf(s).recipes[alchemy::brewingRecipe(s)].pill));
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
  v.next = v.starts[v.depth];

  FsFile f;
  if (!Storage.openFileForRead("CULT", chronicle::kPath, f)) {
    drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_CHRONICLE_EMPTY), 3);
    return;
  }
  const size_t size = f.fileSize();
  size_t offset = v.starts[v.depth];
  bool drewAny = false;
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
      if (drewAny) y += metrics.verticalSpacing;
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
    if (drewAny && y + needed > bottom) break;
    for (const auto& w : wrapped) {
      renderer.drawText(font, x, y, w.c_str(), true, style);
      y += lineHeight;
    }
    y += 4;
    drewAny = true;
    offset += consumed;
  }
  f.close();
  v.next = static_cast<uint32_t>(offset);
  v.more = offset < size;
  if (!drewAny) drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_CHRONICLE_EMPTY), 3);
}

#endif  // CROSSINK_CULTIVATION
