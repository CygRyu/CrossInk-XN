#pragma once

#include <CultivationEngine.h>

#include <array>

#include "../Activity.h"
#include "activities/reader/GlobalReadingStats.h"

// Reading-driven zero-player cultivation game. Qi comes from reading stats;
// this screen only displays the character, the journal of recent events, and
// lets the player pick a Dao path once. See docs/cultivation.md.
class CultivationActivity final : public Activity {
  enum class Page : uint8_t { Status, Journal, PathChoice };

  Page page = Page::Status;
  int pathSelection = 0;  // 0..2 -> Orthodox, Demonic, Rogue
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  bool ignoreInitialBackRelease = false;
  bool ignoreInitialConfirmRelease = false;
  bool ignoreInitialPowerRelease = false;

  Cultivation::ReadingInputs inputs;
  Cultivation::Progress progress;
  Cultivation::Attributes attributes;
  uint32_t banked = 0;
  char name[32] = {};
  std::array<Cultivation::JournalEntry, Cultivation::kMaxJournalEntries> journal{};
  size_t journalCount = 0;

  void refreshGameState();
  bool pathChoicePending() const;
  void showPage(Page next);
  void exitGame();
  void renderStatus(int top, int x, int width);
  void renderJournal(int top, int x, int width);
  void renderPathChoice(int top, int x, int width);

 public:
  CultivationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Cultivation", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
