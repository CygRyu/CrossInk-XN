#include "CultivationActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CultivationStore.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/InputReleaseGuard.h"

using namespace Cultivation;

namespace {
constexpr const char* kPathTitles[3] = {"Orthodox", "Demonic", "Rogue"};
constexpr const char* kPathBlurbs[3] = {"Honour, a sect at your back, and steady growth.",
                                        "Power at any price. Devour, conquer, ascend.",
                                        "Cunning and freedom. Bow to no master."};
constexpr Path kPathValues[3] = {Path::Orthodox, Path::Demonic, Path::Rogue};
}  // namespace

void CultivationActivity::refreshGameState() {
  // loadAggregated() includes stats synced from other CrossInk readers, if any.
  const GlobalReadingStats stats = GlobalReadingStats::loadAggregated();
  inputs.pages = stats.totalPagesTurned;
  inputs.seconds = stats.totalReadingSeconds;
  inputs.books = stats.completedBooks;
  inputs.longestStreak = stats.displayLongestReadingStreak();

  CULTIVATION.ensureLoaded();
  const uint32_t derived = qiFromReading(inputs);
  bool dirty = false;

  if (CULTIVATION.seed() == 0) {
    uint32_t seed = static_cast<uint32_t>(millis()) ^ (derived * 2654435761u) ^ (inputs.seconds * 40503u);
    CULTIVATION.setSeed(seed ? seed : 0xC0FFEEu);
    dirty = true;
  }

  Ledger ledger = CULTIVATION.ledger();
  dirty |= applyLedger(ledger, derived);
  CULTIVATION.setLedger(ledger);

  if (ledger.banked > CULTIVATION.seenQi()) {
    CULTIVATION.advanceJournal(CULTIVATION.seenQi(), ledger.banked);
    dirty = true;
  }
  if (dirty && !CULTIVATION.saveToFile()) {
    LOG_ERR("CULT", "Could not save cultivation state");
  }

  banked = ledger.banked;
  progress = progressFor(banked);
  attributes = attributesFor(inputs);
  characterName(CULTIVATION.seed(), name, sizeof(name));
  journalCount = generateJournal(CULTIVATION.seed(), CULTIVATION.path(), CULTIVATION.journalFrom(),
                                 CULTIVATION.journalTo(), journal.data(), journal.size());
}

bool CultivationActivity::pathChoicePending() const {
  return CULTIVATION.path() == Path::Undecided && pathChoiceAvailable(progress);
}

void CultivationActivity::onEnter() {
  Activity::onEnter();
  ignoreInitialBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialPowerRelease = mappedInput.isPressed(MappedInputManager::Button::Power);
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  refreshGameState();
  requestUpdate();
}

void CultivationActivity::onExit() {
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void CultivationActivity::showPage(const Page next) {
  page = next;
  requestUpdate();
}

void CultivationActivity::exitGame() { finish(); }

void CultivationActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Back,
                                               ignoreInitialBackRelease) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Power,
                                               ignoreInitialPowerRelease) ||
      InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm,
                                               ignoreInitialConfirmRelease)) {
    return;
  }

  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    exitGame();
    return;
  }

  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                  mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                    mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    if (page == Page::PathChoice) {
      showPage(Page::Status);  // cancel the choice
    } else {
      exitGame();
    }
    return;
  }

  if (page == Page::PathChoice) {
    if (up) {
      pathSelection = (pathSelection + 2) % 3;
      requestUpdate();
    } else if (down) {
      pathSelection = (pathSelection + 1) % 3;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      mappedInput.suppressNextConfirmRelease();
      CULTIVATION.setPath(kPathValues[pathSelection]);
      if (!CULTIVATION.saveToFile()) {
        LOG_ERR("CULT", "Could not save chosen path");
      }
      // Re-derive the journal text so it uses the new path's flavour.
      refreshGameState();
      showPage(Page::Status);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    if (page == Page::Status) {
      showPage(pathChoicePending() ? Page::PathChoice : Page::Journal);
    } else {
      showPage(Page::Status);
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (up || swipe == MappedInputManager::SwipeDir::Right) {
    showPage(page == Page::Journal ? Page::Status : Page::Journal);
  } else if (down || swipe == MappedInputManager::SwipeDir::Left) {
    showPage(page == Page::Status ? Page::Journal : Page::Status);
  }
}

void CultivationActivity::renderStatus(int y, const int x, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  char line[96];

  renderer.drawText(UI_12_FONT_ID, x, y, name, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 2;

  snprintf(line, sizeof(line), "%s, layer %u%s", realmName(progress.realm), static_cast<unsigned>(progress.layer),
           progress.maxed ? " (peak)" : "");
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  GUI.drawProgressBar(renderer, Rect{x, y, width, metrics.progressBarHeight}, progress.qiIntoLayer,
                      progress.qiForLayer);
  y += metrics.progressBarHeight + 4;
  snprintf(line, sizeof(line), "Qi %lu / %lu to next layer", static_cast<unsigned long>(progress.qiIntoLayer),
           static_cast<unsigned long>(progress.qiForLayer));
  renderer.drawText(SMALL_FONT_ID, x, y, line);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  const Path path = CULTIVATION.path();
  snprintf(line, sizeof(line), "Dao path: %s", pathName(path));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  struct Row {
    const char* label;
    uint32_t value;
  };
  const Row rows[] = {{"Body (hours read)", attributes.body},
                      {"Insight (pages / 25)", attributes.insight},
                      {"Resolve (best streak)", attributes.resolve},
                      {"Fortune (books done)", attributes.fortune},
                      {"Combat power", combatPower(progress, attributes)}};
  for (const Row& row : rows) {
    char value[16];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(row.value));
    renderer.drawText(UI_10_FONT_ID, x, y, row.label);
    renderer.drawText(UI_10_FONT_ID, x + width - renderer.getTextWidth(UI_10_FONT_ID, value), y, value);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }

  y += metrics.verticalSpacing;
  if (pathChoicePending()) {
    renderer.drawText(SMALL_FONT_ID, x, y, "Your Dao path awaits. Press Path.");
  } else if (progress.maxed) {
    renderer.drawText(SMALL_FONT_ID, x, y, "You have reached the peak. Keep reading.");
  } else {
    renderer.drawText(SMALL_FONT_ID, x, y, "Read more to grow stronger.");
  }
}

void CultivationActivity::renderJournal(int y, const int x, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(UI_10_FONT_ID, x, y, "Journal", true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  if (journalCount == 0) {
    renderer.drawText(UI_10_FONT_ID, x, y, "Nothing yet. Read a few pages!");
    return;
  }

  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  for (size_t i = 0; i < journalCount; ++i) {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, journal[i].text, width - 14, 4);
    const int entryHeight = static_cast<int>(lines.size()) * lineH;
    if (y + entryHeight > bottom) break;
    renderer.fillRect(x, y + lineH / 2 - 2, 4, 4, true);
    for (const auto& text : lines) {
      renderer.drawText(UI_10_FONT_ID, x + 14, y, text.c_str());
      y += lineH;
    }
    y += metrics.verticalSpacing;
  }
}

void CultivationActivity::renderPathChoice(int y, const int x, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(UI_10_FONT_ID, x, y, "Choose your Dao path", true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  renderer.drawText(SMALL_FONT_ID, x, y, "This choice is permanent.");
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  const int rowH = renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 16;
  for (int i = 0; i < 3; ++i) {
    const bool selected = (i == pathSelection);
    if (selected) {
      renderer.fillRect(x, y, width, rowH, true);
    } else {
      renderer.drawRect(x, y, width, rowH, true);
    }
    renderer.drawText(UI_10_FONT_ID, x + 10, y + 6, kPathTitles[i], !selected, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, x + 10, y + 6 + renderer.getLineHeight(UI_10_FONT_ID), kPathBlurbs[i],
                      !selected);
    y += rowH + metrics.verticalSpacing;
  }
}

void CultivationActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();

  const char* title = page == Page::Journal ? "Journal" : "Cultivation";
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, title);
  } else {
    CompactHeader::drawTitle(renderer, title);
  }

  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int top = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;

  switch (page) {
    case Page::Status:
      renderStatus(top, x, width);
      break;
    case Page::Journal:
      renderJournal(top, x, width);
      break;
    case Page::PathChoice:
      renderPathChoice(top, x, width);
      break;
  }

  const char* confirmLabel = page == Page::PathChoice ? "Choose" : page == Page::Journal ? "Status"
                             : pathChoicePending() ? "Path" : "Journal";
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), confirmLabel, tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
