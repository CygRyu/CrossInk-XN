#pragma once

#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <array>
#include <cstdint>
#include <memory>

#include "GameState.h"
#include "Grammar.h"
#include "LifeLog.h"
#include "PackStore.h"
#include "Simulation.h"
#include "activities/Activity.h"

// Path of Immortal Reading: character creation, the Return summary, the
// Cultivator screen and the Chronicle viewer.
//
// Memory: the ~1 KB GameState is allocated in onEnter() with
// makeUniqueNoThrow and freed in onExit(), so the game costs nothing while it
// is closed. It is too large for the stack, and a static would hold DRAM for
// the whole session. Two more blocks live only while their screen is open:
// the visit's life log (~1.6 KB, Return summary) and the Chronicle viewer's
// page history (~2 KB). Everything else is a few dozen bytes of members.
class CultivationActivity final : public Activity {
 public:
  CultivationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Cultivation", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t { Name, Gender, Origin, Awakening, Summary, Cultivator, Chronicle, Error };

  // Visit
  void startVisit();
  void startCreation();
  void finishCreation();
  bool persist();
  void writeLifeLog(const cultivation::lifelog::VisitChange& change);
  void leaveSummary();
  void openChronicle();
  void closeChronicle();

  // Input per screen
  void handleList(int count, bool up, bool down);
  void onNameConfirm();
  void openKeyboard();
  void showAwakening(uint8_t view);
  void rerollOrNext();
  void loadPacks();
  void updateRumor();

  // Drawing
  int drawWrapped(int fontId, int x, int y, int width, const char* text, int maxLines) const;
  void renderName(int top, int bottom);
  void renderGender(int top, int bottom);
  void renderOrigin(int top, int bottom);
  void renderAwakening(int x, int y, int width);
  void renderSummary(int x, int y, int width, int bottom);
  void renderCultivator(int x, int y, int width);
  void renderChronicle(int x, int y, int width, int bottom);

  Screen screen_ = Screen::Error;
  int selected_ = 0;
  std::unique_ptr<cultivation::GameState> state_;
  // Content packs and the grammar engine (~4.6 KB index + ~3.4 KB engine),
  // allocated in onEnter() and freed in onExit(). If either allocation fails
  // the game still runs, without generated text.
  std::unique_ptr<cultivation::PackStore> packs_;
  std::unique_ptr<cultivation::Grammar> grammar_;
  char rumor_[200] = {};  // rumor of the day, expanded once per visit

  // Creation draft (RAM only until the player keeps a destiny).
  cultivation::CharacterDraft draft_;
  std::array<cultivation::DestinyRoll, cultivation::kMaxRolls> rolls_{};
  uint8_t rollCount_ = 0;
  uint8_t rollView_ = 0;
  uint8_t nameSuggestions_ = 0;
  bool nameInvalid_ = false;
  bool damagedSave_ = false;  // a corrupt save exists and will be kept aside

  // What the last visit produced, for the Cultivator screen.
  cultivation::AbsorbResult visit_;
  cultivation::OriginGift originGift_;  // shown once: at creation, or on an older character's next visit
  uint32_t visitSeconds_ = 0;
  bool visitReset_ = false;

  // Return summary: the visit's life log, freed when the summary is left.
  // Pages are runs of log lines; summaryPages_ holds where each page starts.
  std::unique_ptr<cultivation::lifelog::LifeLog> lifeLog_;
  uint8_t summaryPages_[cultivation::lifelog::kMaxLines + 1] = {};
  uint8_t summaryPage_ = 0;
  uint8_t summaryNext_ = 0;  // first line of the next page, set while drawing

  // Chronicle viewer: byte offsets of the pages read so far, so Previous can
  // go back. 512 pages of history; beyond that the oldest are forgotten.
  struct ChronicleView {
    static constexpr uint16_t kHistory = 512;
    uint32_t starts[kHistory] = {};
    uint16_t depth = 0;       // starts[depth] is the page on screen
    uint32_t pageNumber = 1;  // 1-based, for the title
    uint32_t next = 0;        // where the next page starts, set while drawing
    bool more = false;        // the file goes on past this page
  };
  std::unique_ptr<ChronicleView> chronicle_;
  bool chronicleExists_ = false;
  bool justCreated_ = false;
  bool saveFailed_ = false;

  bool ignoreInitialBackRelease_ = false;
  bool ignoreInitialConfirmRelease_ = false;
  bool ignoreInitialPowerRelease_ = false;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;
};

#endif  // CROSSINK_CULTIVATION
