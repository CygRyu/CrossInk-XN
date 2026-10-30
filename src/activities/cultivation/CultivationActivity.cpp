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
  lifelog::VisitChange change;
  change.ageMonthsBefore = state_->progress.ageMonths;
  const bool bottleneckBefore = atBottleneck(*state_);
  visit_ = absorb(*state_, gains);
  change.ageMonthsAfter = state_->progress.ageMonths;
  change.stagesGained = visit_.stagesGained;
  change.reachedBottleneck = !bottleneckBefore && atBottleneck(*state_);
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
  if (summary) writeLifeLog(change);
  persist();
  chronicleExists_ = chronicle::exists();
  refreshActions();
  summaryPage_ = 0;
  summaryPages_[0] = 0;
  screen_ = summary ? Screen::Summary : Screen::Cultivator;
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
    screen_ = Screen::Cultivator;
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
    chronicle_ = std::move(view);
    screen_ = Screen::Chronicle;
  }
  requestUpdate();
}

// The Cultivator screen's list: what can be done right now.
void CultivationActivity::refreshActions() {
  actionCount_ = 0;
  if (state_ && state_->isCreated()) {
    // Shown even when a key is missing: the Breakthrough screen says which.
    if (atBottleneck(*state_)) actions_[actionCount_++] = Action::Breakthrough;
    if (breakthrough::canEnterGreatPerfection(*state_)) actions_[actionCount_++] = Action::GreatPerfection;
  }
  if (chronicleExists_) actions_[actionCount_++] = Action::Chronicle;
  if (selected_ >= actionCount_) selected_ = 0;
}

void CultivationActivity::runAction() {
  if (selected_ < 0 || selected_ >= actionCount_) return;
  switch (actions_[selected_]) {
    case Action::Breakthrough:
      screen_ = Screen::Breakthrough;
      requestUpdate();
      return;
    case Action::GreatPerfection:
      screen_ = Screen::GreatPerfection;
      requestUpdate();
      return;
    case Action::Chronicle:
      openChronicle();
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
      } else if (confirm) {
        attemptBreakthrough();
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
      } else if ((down || confirm) && v.more) {
        RenderLock lock(*this);
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
        --v.depth;
        --v.pageNumber;
        requestUpdate();
      }
      return;
    }

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
  if (screen_ == Screen::GreatPerfection) title = tr(STR_CULT_GREAT_PERFECTION);
  if (screen_ == Screen::Outcome) {
    title =
        outcomeKind_ == OutcomeKind::EnteredGreatPerfection ? tr(STR_CULT_GREAT_PERFECTION) : tr(STR_CULT_BREAKTHROUGH);
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
    case Screen::Breakthrough:
      renderBreakthrough(x, top, width);
      confirmLabel = breakthrough::canAttempt(*state_) ? tr(STR_CULT_ATTEMPT) : "";
      upLabel = "";
      downLabel = "";
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
      confirmLabel = "";
      upLabel = chronicle_ && chronicle_->depth > 0 ? tr(STR_CULT_PREVIOUS) : "";
      downLabel = chronicle_ && chronicle_->more ? tr(STR_CULT_NEXT) : "";
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
void CultivationActivity::stageText(char* out, const size_t len) const {
  const ProgressBlock& p = state_->progress;
  if (breakthrough::inGreatPerfection(*state_)) {
    snprintf(out, len, "%s", tr(STR_CULT_GREAT_PERFECTION));
  } else if (realmInfo(p.realm).layered) {
    snprintf(out, len, tr(STR_CULT_LAYER_FORMAT), static_cast<unsigned>(p.stage + 1));
  } else {
    snprintf(out, len, "%s", text(kStageNames[p.stage < 4 ? p.stage : 3]));
  }
}

void CultivationActivity::renderCultivator(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const RealmInfo& realm = realmInfo(p.realm);
  char line[224];  // fits "Rumor of the day: " plus a rumor (under 200 bytes)

  char name[kNameMaxBytes + 1];
  nameOf(s, name);
  renderer.drawText(UI_12_FONT_ID, x, y, name, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

  // Titles: the Foundation grade once formed, and "Half-step <next realm>"
  // after a failed breakthrough (DOC: grades are shown as titles).
  char titles[96] = {};
  if (p.foundationGrade >= 1 && p.foundationGrade <= 5) {
    snprintf(titles, sizeof(titles), "%s", text(kFoundationGrades[p.foundationGrade - 1]));
  }
  if (breakthrough::isHalfStep(s) && p.realm + 1 < kMortalRealmCount) {
    char halfStep[64];
    snprintf(halfStep, sizeof(halfStep), tr(STR_CULT_HALF_STEP_FORMAT), text(kRealmNames[p.realm + 1]));
    const size_t used = strlen(titles);
    snprintf(titles + used, sizeof(titles) - used, "%s%s", used > 0 ? ", " : "", halfStep);
  }
  if (titles[0] != '\0') {
    renderer.drawText(SMALL_FONT_ID, x, y, titles);
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

  snprintf(line, sizeof(line), tr(STR_CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
           static_cast<unsigned>(realm.lifespanYears));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_ROOT_FORMAT), text(kElementNames[static_cast<int>(s.character.rootElement)]),
           static_cast<unsigned>(s.character.rootPurity));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_TALENT_FORMAT), text(kTalentNames[s.character.talent % kTalentCount]));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  formatRate(line, sizeof(line), qiMultiplierPermille(s));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_STONES_FORMAT), static_cast<unsigned long>(p.stones));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_STAMINA_FORMAT), static_cast<unsigned>(p.stamina),
           static_cast<unsigned>(kStaminaCap));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  snprintf(line, sizeof(line), tr(STR_CULT_DAO_HEART_FORMAT), static_cast<unsigned>(p.daoHeart));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  if (breakthrough::inGreatPerfection(s)) {
    snprintf(line, sizeof(line), tr(STR_CULT_CONSOLIDATION_FORMAT), static_cast<unsigned>(p.consolidation),
             static_cast<unsigned>(breakthrough::kConsolidationMax));
    renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }
  y += metrics.verticalSpacing * 2 - 4;

  // The action list sits at the bottom; text above it is cut to fit.
  const int rowHeight = metrics.listRowHeight;
  const int listTop = bottom - rowHeight * actionCount_;
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int textLine = renderer.getLineHeight(UI_10_FONT_ID);
  auto fits = [&](const int lineHeight, const int wanted) {
    const int room = (listTop - y) / lineHeight;
    return room < wanted ? (room > 0 ? room : 0) : wanted;
  };

  // What this visit brought.
  if (justCreated_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_WELCOME), 3);
  } else if (visitReset_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_RESET), 3);
  } else if (visitSeconds_ == 0) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_NONE), 3);
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
  if (atBottleneck(s)) {
    const breakthrough::Chance c = breakthrough::chanceFor(s);
    if (c.needsKey && !c.hasKey && c.target < kMortalRealmCount) {
      snprintf(line, sizeof(line), tr(STR_CULT_BOTTLENECK_KEY_FORMAT), text(kRealmNames[c.target]));
      y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, line, fits(smallLine, 4));
    } else {
      y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_BOTTLENECK), fits(smallLine, 2));
    }
  }
  if (rumor_[0] != '\0') {
    snprintf(line, sizeof(line), tr(STR_CULT_RUMOR_FORMAT), rumor_);
    y += metrics.verticalSpacing * 2;
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, fits(textLine, 4));
  }
  if (packs_ && packs_->sdProblems() > 0)
    y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_PACK_PROBLEMS), 3);
  if (damagedSave_ && justCreated_) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_DAMAGED), 3);
  if (saveFailed_) drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_FAILED), 2);

  if (actionCount_ > 0) {
    GUI.drawList(renderer, Rect{0, listTop, renderer.getScreenWidth(), rowHeight * actionCount_}, actionCount_,
                 selected_, [this](const int i) -> std::string {
                   switch (actions_[i]) {
                     case Action::Breakthrough:
                       return tr(STR_CULT_ACTION_BREAKTHROUGH);
                     case Action::GreatPerfection:
                       return tr(STR_CULT_ACTION_GREAT_PERFECTION);
                     case Action::Chronicle:
                       return tr(STR_CULT_ACTION_CHRONICLE);
                   }
                   return {};
                 });
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
  if (c.pity > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_CHANCE_PITY_FORMAT), static_cast<unsigned>(s.progress.failures),
             static_cast<unsigned>(c.pity));
    part(line);
  }
  y += metrics.verticalSpacing;

  if (c.needsKey && !c.hasKey) {
    snprintf(line, sizeof(line), tr(STR_CULT_BOTTLENECK_KEY_FORMAT), text(kRealmNames[c.target]));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 5);
  } else {
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
  }
  const auto header = renderer.wrappedText(UI_12_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : header) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += metrics.verticalSpacing;

  if (outcomeKind_ != OutcomeKind::EnteredGreatPerfection) {
    snprintf(line, sizeof(line), tr(STR_CULT_BT_ROLL_FORMAT), static_cast<unsigned>(outcome_.roll),
             static_cast<unsigned>(outcome_.chance));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
  if (outcomeLine_.text[0] != '\0') {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, outcomeLine_.text, 6) + metrics.verticalSpacing;
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughSuccess && outcome_.gradeKind == breakthrough::GradeKind::Foundation &&
      outcome_.grade >= 1 && outcome_.grade <= 5) {
    snprintf(line, sizeof(line), tr(STR_CULT_FOUNDATION_FORMED_FORMAT), text(kFoundationGrades[outcome_.grade - 1]),
             static_cast<unsigned>(outcome_.gradeScore));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);
  }
  if (outcomeKind_ == OutcomeKind::BreakthroughFailure) {
    snprintf(line, sizeof(line), tr(STR_CULT_NEXT_CHANCE_FORMAT),
             static_cast<unsigned>(breakthrough::chanceFor(s).total));
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
    snprintf(line, sizeof(line), tr(STR_CULT_AGE_FORMAT), static_cast<unsigned>(ageYears(s)),
             static_cast<unsigned>(realm.lifespanYears));
    renderer.drawText(UI_10_FONT_ID, x + width - renderer.getTextWidth(UI_10_FONT_ID, line), y, line);
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
void CultivationActivity::renderChronicle(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!chronicle_) return;
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
    // Keep a heading with at least one line of what follows it.
    const int needed = heading ? height + renderer.getLineHeight(UI_10_FONT_ID) : height;
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
