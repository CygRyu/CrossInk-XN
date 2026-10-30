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

#include "CultivationActivity.h"
#include "MappedInputManager.h"
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
static const StrId kElementNames[static_cast<int>(Element::Count)] = {
    StrId::STR_CULT_ELEMENT_WOOD, StrId::STR_CULT_ELEMENT_FIRE, StrId::STR_CULT_ELEMENT_EARTH,
    StrId::STR_CULT_ELEMENT_METAL, StrId::STR_CULT_ELEMENT_WATER};
static const StrId kElementColors[static_cast<int>(Element::Count)] = {
    StrId::STR_CULT_COLOR_WOOD, StrId::STR_CULT_COLOR_FIRE, StrId::STR_CULT_COLOR_EARTH, StrId::STR_CULT_COLOR_METAL,
    StrId::STR_CULT_COLOR_WATER};
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
  state_.reset();  // frees the ~1 KB GameState
  renderer.setOrientation(previousOrientation_);
  Activity::onExit();
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
  visit_ = absorb(*state_, gains);
  visitSeconds_ = gains.seconds;
  visitReset_ = gains.resetDetected;
  LOG_INF("CULT", "Visit: %lu s read, %lu qualifying, %lu pages, reset=%d, stages+%u, stones+%lu, stamina+%u",
          static_cast<unsigned long>(gains.seconds), static_cast<unsigned long>(gains.tiers.qualifyingSeconds()),
          static_cast<unsigned long>(gains.pages), gains.resetDetected ? 1 : 0,
          static_cast<unsigned>(visit_.stagesGained), static_cast<unsigned long>(visit_.stonesGained),
          static_cast<unsigned>(visit_.staminaGained));
  persist();
  screen_ = Screen::Cultivator;
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
  persist();
  justCreated_ = true;
  visit_ = AbsorbResult{};
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
    case Screen::Cultivator:
      if (back || confirm) finish();
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
    case Screen::Cultivator:
      renderCultivator(x, top, width);
      confirmLabel = "";
      upLabel = "";
      downLabel = "";
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
  GUI.drawList(
      renderer, Rect{0, top, renderer.getScreenWidth(), bottom - top}, static_cast<int>(Origin::Count), selected_,
      [](const int i) -> std::string { return text(kOriginNames[i]); },
      [](const int i) -> std::string { return text(kBirthLines[i]); });
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

void CultivationActivity::renderCultivator(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const RealmInfo& realm = realmInfo(p.realm);
  char line[160];

  char name[kNameMaxBytes + 1];
  nameOf(s, name);
  renderer.drawText(UI_12_FONT_ID, x, y, name, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

  // Realm on the left, stage on the right.
  renderer.drawText(UI_10_FONT_ID, x, y, text(kRealmNames[p.realm < kMortalRealmCount ? p.realm : 0]), true,
                    EpdFontFamily::BOLD);
  if (realm.layered) {
    snprintf(line, sizeof(line), tr(STR_CULT_LAYER_FORMAT), static_cast<unsigned>(p.stage + 1));
  } else {
    snprintf(line, sizeof(line), "%s", text(kStageNames[p.stage < 4 ? p.stage : 3]));
  }
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
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

  // What this visit brought.
  if (justCreated_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_WELCOME), 3);
  } else if (visitReset_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_RESET), 3);
  } else if (visitSeconds_ == 0) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_VISIT_NONE), 3);
  } else {
    char gained[24];
    visit_.qiGained.format(gained, sizeof(gained));
    snprintf(line, sizeof(line), tr(STR_CULT_VISIT_FORMAT), static_cast<unsigned long>(visitSeconds_ / 60), gained,
             static_cast<unsigned long>(visit_.stonesGained));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);
  }
  if (visit_.stagesGained > 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_STAGES_GAINED_FORMAT), static_cast<unsigned>(visit_.stagesGained));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  }
  if (atBottleneck(s)) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_BOTTLENECK), 3);
  if (damagedSave_ && justCreated_) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_DAMAGED), 3);
  if (saveFailed_) drawWrapped(SMALL_FONT_ID, x, y + 4, width, tr(STR_CULT_SAVE_FAILED), 2);
}

#endif  // CROSSINK_CULTIVATION
