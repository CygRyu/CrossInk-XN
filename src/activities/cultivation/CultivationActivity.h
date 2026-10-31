#pragma once

#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "Breakthrough.h"
#include "Exploration.h"
#include "ExploreText.h"
#include "GameState.h"
#include "Grammar.h"
#include "Inventory.h"
#include "LifeLog.h"
#include "PackStore.h"
#include "ScrollNames.h"
#include "ScrollStore.h"
#include "Scrolls.h"
#include "Simulation.h"
#include "Trials.h"
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
// (~1 KB). Exploration (v0.1.10) adds ~1.9 KB while its screens are open: the
// open regions' names, the open map's layout and the last event's text.
// Everything else is a few hundred bytes of members.
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
    Trial,         // a Tribulation strike or a Heart Demon scene (CultivationTrialScreens.cpp)
    Regions,       // exploration (CultivationExploreScreens.cpp): the open regions and the bag
    Map,           // one region's 6x6 map
    Fight,         // a random encounter, a lair or the guardian
    Merchant,      // a wandering merchant's stock
    MerchantSell,  // selling items back at a third
    Bag,           // items; tonics are drunk here
    Talisman,      // which cleared region a Region Talisman reshapes
    Error
  };
  // What the Cultivator screen's list offers right now.
  enum class Action : uint8_t { Breakthrough, GreatPerfection, Techniques, Explore, Chronicle, Trial };
  enum class OutcomeKind : uint8_t { BreakthroughSuccess, BreakthroughFailure, EnteredGreatPerfection, TrialLost };

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

  // Tribulations and Heart Demons (CultivationTrialScreens.cpp)
  void startTrial();
  void openTrial();
  void prepareTrialStep();
  void answerTrial();
  void trialLost();
  void renderTrial(int x, int y, int width, int bottom);
  std::string trialRowTitle(int row) const;
  std::string trialRowSubtitle(int row) const;
  void trialFlavor(const char* listKey, uint32_t seed, char* out, size_t len);
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

  // Exploration (CultivationExploreScreens.cpp)
  void openRegions();
  void closeExplore();
  void openMap(uint8_t region);
  void refreshCursor();
  void mapConfirm();
  void showStep(const cultivation::explore::StepResult& r);
  void openFight();
  void answerFight();
  void openMerchant(uint8_t tile);
  void openBag(Screen returnTo);
  void refreshBag();
  void useBagItem();
  void openTalisman();
  void exploreChronicle(const char* listKey, uint8_t region, const char* beast, uint32_t extra);
  void formatReward(const cultivation::explore::Reward& r, uint16_t hpLost, uint16_t hpHealed, char* out,
                    size_t len) const;
  bool handleExplore(bool back, bool confirm, bool up, bool down);  // true when the screen is an exploration one
  void renderRegions(int x, int y, int width, int bottom);
  void renderMap(int x, int y, int width, int bottom);
  void renderFight(int x, int y, int width, int bottom);
  void renderMerchant(int x, int y, int width, int bottom);
  void renderSell(int x, int y, int width, int bottom);
  void renderBag(int x, int y, int width, int bottom);
  void renderTalisman(int x, int y, int width, int bottom);
  void exploreLabels(const char*& confirmLabel, const char*& upLabel, const char*& downLabel) const;
  void appendUpheavalLines(uint16_t reshaped);
  int regionRows() const;  // regions, then the Bag, then the Region Talisman when one can be used
  int fightRows() const;   // Fight, Slip away, and a tonic when one would help
  bool canResolveHere() const;
  void loadRegionNames();
  const char* exploreTitle(char* buf, size_t len) const;  // the header's title on exploration screens
  void drawMapTile(int x, int y, int cell, uint8_t tile) const;
  void renderMapInfo(int x, int y, int width, int bottom);

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
  void renderChronicle(int x, int y, int width, int bottom);  // bottom shrinks for an export notice
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
  char notice_[200] = {};  // one line after selling, equipping, exporting the Chronicle ...

  // Cultivator actions and the last breakthrough or Great Perfection outcome.
  Action actions_[5] = {};
  uint8_t actionCount_ = 0;
  OutcomeKind outcomeKind_ = OutcomeKind::BreakthroughSuccess;
  cultivation::breakthrough::Result outcome_;
  cultivation::lifelog::Line outcomeLine_;  // also written to the Chronicle; ~200 B

  // The trial step on screen (~0.7 KB of members): its text, the result of
  // the previous step, the answers and the scroll names they mention.
  char trialText_[200] = {};
  char trialLast_[160] = {};
  char trialSummary_[120] = {};  // on the Outcome screen after a trial was won
  cultivation::trials::Strike strike_;
  char trialNames_[cultivation::trials::kMaxStrikeOptions][cultivation::scrollnames::kNameSize] = {};
  // v0.1.10: the rows on the trial screen. Strike answers (row i = strike
  // option i), then a tonic between strikes; for a Heart Demon: face it,
  // burn incense, burn a Mind scroll.
  enum class TrialRow : uint8_t { Answer, Tonic, Face, Incense, BurnMind };
  TrialRow trialRows_[cultivation::trials::kMaxStrikeOptions + 1] = {};
  uint8_t trialChoices_ = 0;  // answers on screen

  // Exploration, only while its screens are open (~1.5 KB): region names
  // for the list, the open map's layout (regenerated from the seed), the
  // cursor, the last event's text, the merchant's stock and the bag's rows.
  struct ExploreView {
    static constexpr uint8_t kBagMax = static_cast<uint8_t>(cultivation::inventory::Item::Count);
    static constexpr size_t kRegionNameSize = 64;
    char regionNames[cultivation::explore::kRegionCount][kRegionNameSize] = {};
    uint8_t walkable[cultivation::explore::kRegionCount] = {};  // charting goal per region, for the list
    uint8_t regionCount = 0;                                    // regions open
    cultivation::explore::Layout layout;
    uint8_t region = 0;
    bool mapOpen = false;
    uint8_t cursorTiles[5] = {};
    uint8_t cursorCount = 0;
    uint8_t cursor = 0;
    char beast[cultivation::exploretext::kNameSize] = {};
    char fightText[200] = {};
    char fightResult[160] = {};
    char text[200] = {};     // the last thing that happened on the map
    char numbers[200] = {};  // what it gave or cost
    cultivation::explore::Stock stock;
    uint8_t merchantTile = 0;
    cultivation::inventory::Item bag[kBagMax] = {};
    uint8_t bagCount = 0;
    Screen bagReturn = Screen::Regions;
    uint8_t talismanRegions[cultivation::explore::kRegionCount] = {};
    uint8_t talismanCount = 0;
    int regionsSelected = 0;
  };
  std::unique_ptr<ExploreView> explore_;
  uint16_t upheavals_ = 0;  // regions reshaped this visit, for the life log
  bool justCreated_ = false;
  bool saveFailed_ = false;

  bool ignoreInitialBackRelease_ = false;
  bool ignoreInitialConfirmRelease_ = false;
  bool ignoreInitialPowerRelease_ = false;
  GfxRenderer::Orientation previousOrientation_ = GfxRenderer::Orientation::Portrait;
};

#endif  // CROSSINK_CULTIVATION
