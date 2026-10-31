#pragma once

#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <array>
#include <cstdint>
#include <memory>

#include "Breakthrough.h"
#include "GameState.h"
#include "Grammar.h"
#include "LifeLog.h"
#include "PackStore.h"
#include "ScrollNames.h"
#include "ScrollStore.h"
#include "Scrolls.h"
#include "Simulation.h"
#include "activities/Activity.h"

// Path of Immortal Reading: character creation, the Return summary, the
// Cultivator screen with its actions (breakthrough, Great Perfection,
// techniques, Chronicle), the breakthrough screens, the Technique Scroll
// screens (CultivationScrollScreens.cpp) and the Chronicle viewer.
//
// Memory: the ~1 KB GameState is allocated in onEnter() with
// makeUniqueNoThrow and freed in onExit(), so the game costs nothing while it
// is closed. It is too large for the stack, and a static would hold DRAM for
// the whole session. Two more blocks live only while their screen is open:
// the visit's life log (~1.6 KB, Return summary) and the Chronicle viewer's
// page history (~2 KB). Technique Scrolls add, only while their screen is
// open: the visit's scroll news (~0.8 KB), the minting work list (~0.5 KB,
// during the visit's start) and the library list with one page of names
// (~1 KB). Everything else is a few hundred bytes of members.
class CultivationActivity final : public Activity {
 public:
  CultivationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Cultivation", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t {
    Name,
    Gender,
    Origin,
    Awakening,
    Summary,
    Cultivator,
    Breakthrough,     // the chance and its parts; Confirm attempts
    GreatPerfection,  // what staying at the peak does; Confirm enters it
    Outcome,          // the result of either, with its Chronicle line
    Chronicle,
    ScrollNews,    // scrolls minted, improved or sold this visit
    BetterManual,  // a new manual beats the equipped one: offer to equip it
    Techniques,    // the library
    ScrollDetail,  // one scroll: equip, unequip, sell
    SellConfirm,   // selling cannot be undone
    Error
  };
  // What the Cultivator screen's list offers right now.
  enum class Action : uint8_t { Breakthrough, GreatPerfection, Techniques, Chronicle };
  enum class OutcomeKind : uint8_t { BreakthroughSuccess, BreakthroughFailure, EnteredGreatPerfection };

  // Visit
  void startVisit();
  void startCreation();
  void finishCreation();
  bool persist();
  void writeLifeLog(const cultivation::lifelog::VisitChange& change);
  void leaveSummary();
  void openChronicle();
  void closeChronicle();
  void refreshActions();
  void runAction();
  void attemptBreakthrough();
  void enterGreatPerfection();
  void recordOutcome(const char* listKey, uint32_t seed);

  // Technique Scrolls (CultivationScrollScreens.cpp)
  // One visit's finished books (~0.5 KB, only while the visit starts): the
  // pending records, then the title hashes minted, for minted.bin once saved.
  struct MintWork {
    cultivation::scrolls::BookRecord books[cultivation::scrollstore::kMaxPending];
    uint32_t minted[cultivation::scrollstore::kMaxPending] = {};
    uint8_t mintedCount = 0;
  };
  std::unique_ptr<MintWork> mintScrolls();
  void finishMinting(MintWork& work);
  void appendScrollLine(const char* listKey, const cultivation::scrolls::Scroll& s);
  void scrollName(const cultivation::scrolls::Scroll& s, char* out, size_t len);
  void afterSummary();
  void leaveScrollNews();
  void openTechniques();
  void closeTechniques();
  void sortLibrary();
  void loadLibraryNames(bool force);
  int libraryRows() const;
  int selectedSlot() const;
  void handleTechniques(bool back, bool confirm, bool up, bool down);
  void handleScrollDetail(bool back, bool confirm, bool up, bool down);
  void refreshDetailActions();
  void sellSelected();
  void equipBetterManual();
  void prepareKeyName();
  void scrollAttributes(const cultivation::scrolls::Scroll& s, char* out, size_t len) const;

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
  void renderCultivator(int x, int y, int width, int bottom);
  void renderBreakthrough(int x, int y, int width);
  void renderGreatPerfection(int x, int y, int width);
  void renderOutcome(int x, int y, int width);
  void stageText(char* out, size_t len) const;
  void renderChronicle(int x, int y, int width, int bottom);
  void renderScrollNews(int x, int y, int width, int bottom);
  void renderBetterManual(int x, int y, int width);
  void renderTechniques(int x, int y, int width, int bottom);
  void renderScrollDetail(int x, int y, int width, int bottom);
  void renderSellConfirm(int x, int y, int width);

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

  // Technique Scrolls. The visit's news, freed when its screen is left.
  struct ScrollNews {
    static constexpr uint8_t kMax = 8;  // shown; any more are still in the Chronicle
    cultivation::scrolls::Event events[kMax];
    char names[kMax][cultivation::scrollnames::kNameSize] = {};
    uint8_t count = 0;
    uint8_t hidden = 0;
    uint8_t soldHidden = 0;  // auto-sales past kMax: never only "more in the Chronicle"
    uint32_t soldHiddenStones = 0;
    bool overflow = false;  // more books were waiting than one visit takes; the rest wait
    uint8_t pageStarts[kMax + 1] = {};
    uint8_t page = 0;
    uint8_t next = 0;  // first event of the next page, set while drawing
  };
  std::unique_ptr<ScrollNews> news_;
  uint32_t betterManualHash_ = 0;  // a manual minted this visit that beats the equipped one

  // The library screen: slots in display order and the names of one page.
  struct LibraryView {
    static constexpr uint8_t kMaxRows = 12;
    uint8_t order[cultivation::scrolls::kCapacity] = {};
    uint8_t count = 0;
    int pageLoaded = -1;
    char names[kMaxRows][cultivation::scrollnames::kNameSize] = {};
    char manualName[cultivation::scrollnames::kNameSize] = {};  // the equipped manual's
  };
  std::unique_ptr<LibraryView> library_;
  int librarySelected_ = 0;  // position in library_->order
  enum class DetailAction : uint8_t { Equip, Unequip, Sell };
  DetailAction detailActions_[2] = {};
  uint8_t detailActionCount_ = 0;
  char nameBuf_[cultivation::scrollnames::kNameSize] = {};  // key, better manual or selected scroll
  char notice_[200] = {};                                   // one line after selling, equipping ...

  // Cultivator actions and the last breakthrough or Great Perfection outcome.
  Action actions_[4] = {};
  uint8_t actionCount_ = 0;
  OutcomeKind outcomeKind_ = OutcomeKind::BreakthroughSuccess;
  cultivation::breakthrough::Result outcome_;
  cultivation::lifelog::Line outcomeLine_;  // also written to the Chronicle; ~200 B
  bool justCreated_ = false;
  bool saveFailed_ = false;

  bool ignoreInitialBackRelease_ = false;
  bool ignoreInitialConfirmRelease_ = false;
  bool ignoreInitialPowerRelease_ = false;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;
};

#endif  // CROSSINK_CULTIVATION
