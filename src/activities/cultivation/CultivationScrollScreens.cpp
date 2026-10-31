#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Chronicle.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Technique Scroll screens of CultivationActivity (milestone 7): minting at
// the start of a visit, the scroll news after the Return summary, the offer
// to equip a better manual, the library, one scroll's page and selling.
//
// Grammar rule for this file: names are generated in the input loop (under
// the render lock when a screen is already showing) and kept in small
// buffers; render() only reads them, so the engine is never used by two
// tasks at once.

using namespace cultivation;

namespace {

static const StrId kTierNames[static_cast<int>(scrolls::Tier::Count)] = {
    StrId::STR_CULT_TIER_MORTAL, StrId::STR_CULT_TIER_YELLOW, StrId::STR_CULT_TIER_EARTH, StrId::STR_CULT_TIER_HEAVEN,
    StrId::STR_CULT_TIER_SAINT};
static const StrId kKindNames[static_cast<int>(scrolls::Kind::Count)] = {
    StrId::STR_CULT_KIND_SWORD,    StrId::STR_CULT_KIND_PALM, StrId::STR_CULT_KIND_BODY,
    StrId::STR_CULT_KIND_MOVEMENT, StrId::STR_CULT_KIND_MIND, StrId::STR_CULT_KIND_MANUAL};
static const StrId kQualityNames[static_cast<int>(scrolls::Quality::Count)] = {
    StrId::STR_CULT_QUALITY_FRAGMENTARY, StrId::STR_CULT_QUALITY_INCOMPLETE, StrId::STR_CULT_QUALITY_COMPLETE,
    StrId::STR_CULT_QUALITY_REFINED, StrId::STR_CULT_QUALITY_PERFECTED};

int clampIndex(const int v, const int count) { return v < 0 ? 0 : (v >= count ? count - 1 : v); }

}  // namespace

namespace cultivation {
namespace ui {
const char* tierName(const scrolls::Tier t) {
  return I18N.get(kTierNames[clampIndex(static_cast<int>(t), static_cast<int>(scrolls::Tier::Count))]);
}
const char* kindName(const scrolls::Kind k) {
  return I18N.get(kKindNames[clampIndex(static_cast<int>(k), static_cast<int>(scrolls::Kind::Count))]);
}
const char* qualityName(const scrolls::Quality q) {
  return I18N.get(kQualityNames[clampIndex(static_cast<int>(q), static_cast<int>(scrolls::Quality::Count))]);
}
}  // namespace ui
}  // namespace cultivation

// --- Minting -------------------------------------------------------------------------

// Mints the books finished since the last visit into the library, writes
// their Chronicle lines and collects the news. Returns the work list (for
// finishMinting() once the save holds the result), or nullptr if no book was
// waiting. Without memory for the list, the books simply wait for the next visit.
std::unique_ptr<CultivationActivity::MintWork> CultivationActivity::mintScrolls() {
  if (!scrollstore::hasPending()) return nullptr;
  auto work = makeUniqueNoThrow<MintWork>();
  if (!work) {
    LOG_ERR("CULT", "Out of memory for finished books (%u bytes); they wait for the next visit",
            static_cast<unsigned>(sizeof(MintWork)));
    return nullptr;
  }
  bool overflow = false;
  const uint8_t books = scrollstore::readPending(work->books, scrollstore::kMaxPending, overflow);
  news_ = makeUniqueNoThrow<ScrollNews>();  // without it the scrolls are minted, just not announced
  if (news_) news_->overflow = overflow;

  const uint32_t manualBefore = scrolls::equippedManual(*state_) >= 0 ? scrolls::equippedManualPermille(*state_) : 0;
  uint32_t bestNewHash = 0;
  uint32_t bestNewPermille = 0;
  for (uint8_t i = 0; i < books; ++i) {
    const scrolls::BookRecord& book = work->books[i];
    scrolls::Event events[2];
    const uint8_t n = scrolls::mint(*state_, book, scrollstore::isMinted(book.titleHash), events);
    for (uint8_t j = 0; j < n; ++j) {
      const scrolls::Event& e = events[j];
      switch (e.kind) {
        case scrolls::EventKind::Minted:
          appendScrollLine("scroll.line.new", e.scroll);
          work->minted[work->mintedCount++] = e.scroll.titleHash;
          if (e.scroll.kind == scrolls::Kind::Manual && scrolls::manualPermille(e.scroll) > bestNewPermille) {
            bestNewPermille = scrolls::manualPermille(e.scroll);
            bestNewHash = e.scroll.titleHash;
          }
          break;
        case scrolls::EventKind::Improved:
          appendScrollLine("scroll.line.reread", e.scroll);
          break;
        case scrolls::EventKind::AutoSold:
          appendScrollLine("scroll.line.sold", e.scroll);
          break;
        default:
          break;
      }
      LOG_INF("CULT", "Scroll event %u: hours %u, kind %u, comprehension %u", static_cast<unsigned>(e.kind),
              static_cast<unsigned>(e.scroll.hours), static_cast<unsigned>(e.scroll.kind),
              static_cast<unsigned>(e.scroll.comprehension));
      if (!news_) continue;
      if (news_->count < ScrollNews::kMax) {
        news_->events[news_->count] = e;
        const bool named = e.kind != scrolls::EventKind::TooShort && e.kind != scrolls::EventKind::AlreadyUsed;
        if (named) scrollName(e.scroll, news_->names[news_->count], sizeof(news_->names[0]));
        ++news_->count;
      } else if (e.kind == scrolls::EventKind::AutoSold) {
        ++news_->soldHidden;
        news_->soldHiddenStones += e.stones;
      } else {
        ++news_->hidden;
      }
    }
  }
  if (packs_) packs_->closeFile();

  // A new manual that beats the equipped one (or the first manual) is offered after the news.
  if (bestNewHash != 0 && bestNewPermille > manualBefore && scrolls::find(*state_, bestNewHash) >= 0) {
    betterManualHash_ = bestNewHash;
  }
  if (news_ && news_->count == 0 && !news_->overflow) news_.reset();
  return work;
}

// After a successful save: remember what this life minted, then empty the list.
void CultivationActivity::finishMinting(MintWork& work) {
  if (!scrollstore::addMinted(work.minted, work.mintedCount)) {
    LOG_ERR("CULT", "Could not update the minted list");
  }
  if (!scrollstore::finishPending()) {
    LOG_ERR("CULT", "Could not update the finished-books list");
  }
}

void CultivationActivity::appendScrollLine(const char* listKey, const scrolls::Scroll& s) {
  if (!grammar_) return;
  lifelog::Line line;  // ~0.2 KB on the stack, as recordOutcome's
  if (!scrollnames::line(line, listKey, s, *state_, *grammar_)) return;
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  chronicle::appendLines(&line, 1);
  chronicleExists_ = chronicle::exists();
}

void CultivationActivity::scrollName(const scrolls::Scroll& s, char* out, const size_t len) {
  if (grammar_ && scrollnames::name(s, worldSeedOf(*state_), *grammar_, out, len)) return;
  // Without generated text: "Earth tier Sword scroll".
  snprintf(out, len, tr(STR_CULT_SCROLL_FALLBACK_FORMAT), ui::tierName(scrolls::tierOf(s)), ui::kindName(s.kind));
}

// "Earth tier, Water, Sword"
void CultivationActivity::scrollAttributes(const scrolls::Scroll& s, char* out, const size_t len) const {
  snprintf(out, len, tr(STR_CULT_SCROLL_ATTRS_FORMAT), ui::tierName(scrolls::tierOf(s)), ui::elementName(s.element),
           ui::kindName(s.kind));
}

// What follows the Return summary: the scroll news, then the better manual.
// Callers that may be drawing hold the render lock.
void CultivationActivity::afterSummary() {
  selected_ = 0;
  if (news_) {
    news_->page = 0;
    news_->pageStarts[0] = 0;
    screen_ = Screen::ScrollNews;
    return;
  }
  const int better = betterManualHash_ != 0 ? scrolls::find(*state_, betterManualHash_) : -1;
  if (better >= 0) {
    scrollName(scrolls::get(*state_, static_cast<uint8_t>(better)), nameBuf_, sizeof(nameBuf_));
    if (packs_) packs_->closeFile();
    screen_ = Screen::BetterManual;
    return;
  }
  betterManualHash_ = 0;
  screen_ = Screen::Cultivator;
}

void CultivationActivity::leaveScrollNews() {
  RenderLock lock(*this);
  news_.reset();
  afterSummary();
}

void CultivationActivity::equipBetterManual() {
  {
    RenderLock lock(*this);
    const int index = scrolls::find(*state_, betterManualHash_);
    if (index >= 0 && scrolls::equip(*state_, static_cast<uint8_t>(index))) persist();
    betterManualHash_ = 0;
    selected_ = 0;
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

// The key the Breakthrough screen shows (and the attempt would spend).
void CultivationActivity::prepareKeyName() {
  nameBuf_[0] = '\0';
  const breakthrough::Chance c = breakthrough::chanceFor(*state_);
  if (c.keyIndex < 0) return;
  scrollName(scrolls::get(*state_, static_cast<uint8_t>(c.keyIndex)), nameBuf_, sizeof(nameBuf_));
  if (packs_) packs_->closeFile();
}

// --- The library ------------------------------------------------------------------------

namespace {

// The library screen's layout, shared by input (paging) and drawing.
struct LibraryLayout {
  int listTop;
  int pageLineTop;
  int rows;
};

LibraryLayout libraryLayout(const GfxRenderer& renderer) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int top = CompactHeader::contentTop(m) + m.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - m.buttonHintsHeight - m.verticalSpacing;
  LibraryLayout l;
  // Manual name, Qi rate (UI 10), two small lines (counts or a notice).
  l.listTop = top + 2 * (renderer.getLineHeight(UI_10_FONT_ID) + 4) + 2 * renderer.getLineHeight(SMALL_FONT_ID) +
              m.verticalSpacing * 2;
  l.pageLineTop = bottom - renderer.getLineHeight(SMALL_FONT_ID);
  l.rows = GUI.getListPageItems(l.pageLineTop - 4 - l.listTop, true);
  if (l.rows < 1) l.rows = 1;
  if (l.rows > 12) l.rows = 12;  // LibraryView::kMaxRows
  return l;
}

}  // namespace

int CultivationActivity::libraryRows() const { return libraryLayout(renderer).rows; }

int CultivationActivity::selectedSlot() const {
  if (!library_ || library_->count == 0) return -1;
  return library_->order[clampIndex(librarySelected_, library_->count)];
}

void CultivationActivity::openTechniques() {
  auto view = makeUniqueNoThrow<LibraryView>();
  if (!view) {
    LOG_ERR("CULT", "Out of memory for the library (%u bytes)", static_cast<unsigned>(sizeof(LibraryView)));
    return;
  }
  {
    RenderLock lock(*this);
    library_ = std::move(view);
    librarySelected_ = 0;
    notice_[0] = '\0';
    sortLibrary();
    loadLibraryNames(true);
    screen_ = Screen::Techniques;
  }
  requestUpdate();
}

void CultivationActivity::closeTechniques() {
  {
    RenderLock lock(*this);
    library_.reset();
    notice_[0] = '\0';
    refreshActions();
    selected_ = 0;
    for (uint8_t i = 0; i < actionCount_; ++i) {
      if (actions_[i] == Action::Techniques) selected_ = i;
    }
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

// Display order: the equipped manual, equipped techniques, then the rest
// strongest first (tier, then comprehension), oldest first on ties.
void CultivationActivity::sortLibrary() {
  if (!library_) return;
  const uint8_t n = scrolls::count(*state_);
  auto rank = [&](const uint8_t slot) -> uint32_t {
    const scrolls::Scroll s = scrolls::get(*state_, slot);
    const uint32_t group = (s.flags & scrolls::Scroll::kEquippedManual) != 0      ? 0u
                           : (s.flags & scrolls::Scroll::kEquippedTechnique) != 0 ? 1u
                                                                                  : 2u;
    return group * 100000u + (99999u - scrolls::strength(s));
  };
  library_->count = n;
  for (uint8_t i = 0; i < n; ++i) {
    uint8_t j = i;
    const uint32_t r = rank(i);
    while (j > 0 && rank(library_->order[j - 1]) > r) {
      library_->order[j] = library_->order[j - 1];
      --j;
    }
    library_->order[j] = i;
  }
  library_->pageLoaded = -1;
}

// Names of the page holding the selection; on `force` also the equipped manual's.
void CultivationActivity::loadLibraryNames(const bool force) {
  if (!library_) return;
  if (force) {
    const int manual = scrolls::equippedManual(*state_);
    library_->manualName[0] = '\0';
    if (manual >= 0) {
      scrollName(scrolls::get(*state_, static_cast<uint8_t>(manual)), library_->manualName,
                 sizeof(library_->manualName));
    }
  }
  const int rows = libraryRows();
  const int page = librarySelected_ / rows;
  if (!force && page == library_->pageLoaded) return;
  for (int r = 0; r < rows && r < LibraryView::kMaxRows; ++r) {
    const int pos = page * rows + r;
    library_->names[r][0] = '\0';
    if (pos < library_->count) {
      scrollName(scrolls::get(*state_, library_->order[pos]), library_->names[r], sizeof(library_->names[0]));
    }
  }
  library_->pageLoaded = page;
  if (packs_) packs_->closeFile();
}

void CultivationActivity::handleTechniques(const bool back, const bool confirm, const bool up, const bool down) {
  if (!library_) {
    closeTechniques();
    return;
  }
  const int count = library_->count;
  if (back || (confirm && count == 0)) {
    closeTechniques();
    return;
  }
  if (count == 0) return;
  if (up || down) {
    {
      RenderLock lock(*this);
      librarySelected_ = up ? (librarySelected_ + count - 1) % count : (librarySelected_ + 1) % count;
      notice_[0] = '\0';
      loadLibraryNames(false);
    }
    requestUpdate();
    return;
  }
  if (confirm) {
    {
      RenderLock lock(*this);
      const int rows = libraryRows();
      snprintf(nameBuf_, sizeof(nameBuf_), "%s", library_->names[librarySelected_ % rows]);
      selected_ = 0;
      notice_[0] = '\0';
      refreshDetailActions();
      screen_ = Screen::ScrollDetail;
    }
    requestUpdate();
  }
}

void CultivationActivity::refreshDetailActions() {
  detailActionCount_ = 0;
  const int slot = selectedSlot();
  if (slot < 0) return;
  const scrolls::Scroll s = scrolls::get(*state_, static_cast<uint8_t>(slot));
  if (s.equipped()) {
    detailActions_[detailActionCount_++] = DetailAction::Unequip;
  } else {
    detailActions_[detailActionCount_++] = DetailAction::Equip;
    detailActions_[detailActionCount_++] = DetailAction::Sell;
  }
  if (selected_ >= detailActionCount_) selected_ = 0;
}

void CultivationActivity::handleScrollDetail(const bool back, const bool confirm, const bool up, const bool down) {
  const int slot = selectedSlot();
  if (back || slot < 0) {
    {
      RenderLock lock(*this);
      // Back to the list, with the same scroll selected wherever equipping moved it.
      sortLibrary();
      if (library_) {
        for (uint8_t i = 0; i < library_->count; ++i) {
          if (library_->order[i] == slot) librarySelected_ = i;
        }
      }
      loadLibraryNames(true);
      screen_ = Screen::Techniques;
    }
    requestUpdate();
    return;
  }
  if (up || down) {
    handleList(detailActionCount_, up, down);
    return;
  }
  if (!confirm || selected_ >= detailActionCount_) return;
  const uint8_t index = static_cast<uint8_t>(slot);
  const scrolls::Scroll s = scrolls::get(*state_, index);
  {
    RenderLock lock(*this);
    switch (detailActions_[selected_]) {
      case DetailAction::Equip:
        if (scrolls::equip(*state_, index)) {
          persist();
          snprintf(
              notice_, sizeof(notice_), "%s",
              s.kind == scrolls::Kind::Manual ? tr(STR_CULT_EQUIPPED_MANUAL_NOTICE) : tr(STR_CULT_EQUIPPED_NOTICE));
        } else {
          snprintf(notice_, sizeof(notice_), "%s", tr(STR_CULT_EQUIP_FULL_NOTICE));
        }
        break;
      case DetailAction::Unequip:
        scrolls::unequip(*state_, index);
        persist();
        snprintf(notice_, sizeof(notice_), "%s", tr(STR_CULT_UNEQUIPPED_NOTICE));
        break;
      case DetailAction::Sell:
        screen_ = Screen::SellConfirm;
        break;
    }
    refreshDetailActions();
  }
  requestUpdate();
}

void CultivationActivity::sellSelected() {
  const int slot = selectedSlot();
  {
    RenderLock lock(*this);
    if (slot >= 0) {
      const scrolls::Scroll s = scrolls::get(*state_, static_cast<uint8_t>(slot));
      if (!s.equipped()) {
        appendScrollLine("scroll.line.sold", s);  // the Chronicle first, as every event
        const uint32_t price = scrolls::sell(*state_, static_cast<uint8_t>(slot));
        persist();
        snprintf(notice_, sizeof(notice_), tr(STR_CULT_SOLD_FORMAT), nameBuf_, static_cast<unsigned long>(price));
        LOG_INF("CULT", "Sold a scroll for %lu stones", static_cast<unsigned long>(price));
      }
    }
    sortLibrary();
    if (library_ && library_->count > 0) {
      librarySelected_ = clampIndex(librarySelected_, library_->count);
    } else {
      librarySelected_ = 0;
    }
    loadLibraryNames(true);
    selected_ = 0;
    screen_ = Screen::Techniques;
  }
  requestUpdate();
}

// --- Drawing ----------------------------------------------------------------------------

void CultivationActivity::renderScrollNews(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!news_) return;
  ScrollNews& n = *news_;
  const uint8_t first = n.pageStarts[n.page];
  n.next = first;
  char line[256];
  char attrs[96];
  const int nameLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int textLine = renderer.getLineHeight(UI_10_FONT_ID);

  for (uint8_t i = first; i < n.count; ++i) {
    const scrolls::Event& e = n.events[i];
    const scrolls::Scroll& s = e.scroll;
    const bool named = e.kind != scrolls::EventKind::TooShort && e.kind != scrolls::EventKind::AlreadyUsed;
    const char* heading = named ? n.names[i] : tr(STR_CULT_NEWS_BOOK);

    line[0] = '\0';
    switch (e.kind) {
      case scrolls::EventKind::Minted:
        scrollAttributes(s, attrs, sizeof(attrs));
        snprintf(line, sizeof(line), tr(STR_CULT_NEWS_NEW_FORMAT), attrs,
                 ui::qualityName(scrolls::qualityFor(s.comprehension)));
        break;
      case scrolls::EventKind::Improved:
        snprintf(line, sizeof(line), tr(STR_CULT_NEWS_IMPROVED_FORMAT),
                 ui::qualityName(scrolls::qualityFor(e.oldComprehension)),
                 ui::qualityName(scrolls::qualityFor(s.comprehension)), static_cast<unsigned>(e.oldComprehension),
                 static_cast<unsigned>(s.comprehension));
        break;
      case scrolls::EventKind::Unchanged:
        snprintf(line, sizeof(line), "%s", tr(STR_CULT_NEWS_UNCHANGED));
        break;
      case scrolls::EventKind::AlreadyUsed:
        snprintf(line, sizeof(line), "%s", tr(STR_CULT_NEWS_ALREADY_USED));
        break;
      case scrolls::EventKind::TooShort:
        snprintf(line, sizeof(line), tr(STR_CULT_NEWS_TOO_SHORT_FORMAT), static_cast<unsigned>(e.minutes));
        break;
      case scrolls::EventKind::AutoSold:
        snprintf(line, sizeof(line), tr(STR_CULT_NEWS_AUTOSOLD_FORMAT), static_cast<unsigned>(scrolls::kCapacity),
                 static_cast<unsigned long>(e.stones));
        break;
    }
    const auto head = renderer.wrappedText(UI_12_FONT_ID, heading, width, 2, EpdFontFamily::BOLD);
    const auto body = renderer.wrappedText(UI_10_FONT_ID, line, width, 4);
    const int height = static_cast<int>(head.size()) * nameLine + static_cast<int>(body.size()) * textLine;
    if (i != first && y + height > bottom) break;  // the rest goes on the next page
    for (const auto& h : head) {
      renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
      y += nameLine;
    }
    for (const auto& b : body) {
      renderer.drawText(UI_10_FONT_ID, x, y, b.c_str());
      y += textLine;
    }
    y += metrics.verticalSpacing * 2;
    n.next = static_cast<uint8_t>(i + 1);
  }
  if (n.next >= n.count) {  // the last page: what did not fit in the news
    if (n.soldHidden > 0) {
      snprintf(line, sizeof(line), tr(STR_CULT_NEWS_SOLD_MORE_FORMAT), static_cast<unsigned>(n.soldHidden),
               static_cast<unsigned long>(n.soldHiddenStones));
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + 4;
    }
    if (n.hidden > 0) {
      snprintf(line, sizeof(line), tr(STR_CULT_NEWS_MORE_FORMAT), static_cast<unsigned>(n.hidden));
      y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + 4;
    }
    if (n.overflow) y = drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_NEWS_OVERFLOW), 3) + 4;
    if (saveFailed_) drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_SAVE_FAILED), 2);
  }
}

void CultivationActivity::renderBetterManual(const int x, int y, const int width) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const int index = scrolls::find(s, betterManualHash_);
  if (index < 0) return;
  const scrolls::Scroll m = scrolls::get(s, static_cast<uint8_t>(index));
  char line[256];
  char attrs[96];

  const auto head = renderer.wrappedText(UI_12_FONT_ID, nameBuf_, width, 3, EpdFontFamily::BOLD);
  for (const auto& h : head) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  scrollAttributes(m, attrs, sizeof(attrs));
  snprintf(line, sizeof(line), "%s, %s", attrs, ui::qualityName(scrolls::qualityFor(m.comprehension)));
  y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2) + metrics.verticalSpacing;

  // The rate without any manual, then with this one.
  const uint32_t now = qiMultiplierPermille(s);
  const uint32_t bare = static_cast<uint32_t>(static_cast<uint64_t>(now) * 1000u / scrolls::equippedManualPermille(s));
  const uint32_t with = static_cast<uint32_t>(static_cast<uint64_t>(bare) * scrolls::manualPermille(m) / 1000u);
  char from[16];
  char to[16];
  ui::formatPermille(from, sizeof(from), now);
  ui::formatPermille(to, sizeof(to), with);
  snprintf(line, sizeof(line), tr(STR_CULT_BETTER_MANUAL_FORMAT), from, to);
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  if (m.element == s.character.rootElement) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_SCROLL_ELEMENT_MATCH), 3) + metrics.verticalSpacing;
  }
  if (scrolls::equippedManual(s) >= 0) {
    drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_BETTER_MANUAL_KEEPS_OLD), 3);
  } else {
    drawWrapped(SMALL_FONT_ID, x, y, width, tr(STR_CULT_BETTER_MANUAL_FIRST), 3);
  }
}

void CultivationActivity::renderTechniques(const int x, int y, const int width, const int bottom) {
  (void)bottom;
  const GameState& s = *state_;
  if (!library_) return;
  const LibraryLayout layout = libraryLayout(renderer);
  char line[224];

  // The equipped manual and what it does to the Qi rate.
  const int manual = scrolls::equippedManual(s);
  if (manual >= 0) {
    snprintf(line, sizeof(line), tr(STR_CULT_LIBRARY_MANUAL_FORMAT), library_->manualName);
  } else {
    snprintf(line, sizeof(line), "%s", tr(STR_CULT_LIBRARY_NO_MANUAL));
  }
  const std::string manualLine = renderer.truncatedText(UI_10_FONT_ID, line, width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y, manualLine.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  const uint32_t rate = qiMultiplierPermille(s);
  char rateText[16];
  char manualText[16];
  ui::formatPermille(rateText, sizeof(rateText), rate);
  ui::formatPermille(manualText, sizeof(manualText), scrolls::equippedManualPermille(s));
  snprintf(line, sizeof(line), tr(STR_CULT_LIBRARY_RATE_FORMAT), rateText, manualText);
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  if (notice_[0] != '\0') {
    drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 2);
  } else {
    snprintf(line, sizeof(line), tr(STR_CULT_LIBRARY_COUNTS_FORMAT),
             static_cast<unsigned>(scrolls::equippedTechniques(s)),
             static_cast<unsigned>(scrolls::kMaxEquippedTechniques), static_cast<unsigned>(library_->count),
             static_cast<unsigned>(scrolls::kCapacity));
    drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
  }

  if (library_->count == 0) {
    drawWrapped(UI_10_FONT_ID, x, layout.listTop, width, tr(STR_CULT_LIBRARY_EMPTY), 8);
    return;
  }

  const int rows = layout.rows;
  const int page = librarySelected_ / rows;
  const int pages = (library_->count + rows - 1) / rows;
  const int first = page * rows;
  const int onPage = library_->count - first < rows ? library_->count - first : rows;
  const int best = scrolls::bestManual(s);
  GUI.drawList(
      renderer, Rect{0, layout.listTop, renderer.getScreenWidth(), layout.pageLineTop - 4 - layout.listTop}, onPage,
      librarySelected_ - first,
      [this](const int i) -> std::string {
        return i < LibraryView::kMaxRows && library_->pageLoaded >= 0 ? std::string(library_->names[i]) : std::string();
      },
      [this, first, best](const int i) -> std::string {
        const uint8_t slot = library_->order[first + i];
        const scrolls::Scroll sc = scrolls::get(*state_, slot);
        char attrs[96];
        scrollAttributes(sc, attrs, sizeof(attrs));
        char sub[160];
        snprintf(sub, sizeof(sub), "%s, %s", attrs, ui::qualityName(scrolls::qualityFor(sc.comprehension)));
        std::string out = sub;
        if ((sc.flags & scrolls::Scroll::kEquippedManual) != 0) {
          out += tr(STR_CULT_TAG_MANUAL);
        } else if ((sc.flags & scrolls::Scroll::kEquippedTechnique) != 0) {
          out += tr(STR_CULT_TAG_EQUIPPED);
        } else if (slot == best) {
          out += tr(STR_CULT_TAG_BEST_MANUAL);
        }
        return out;
      });
  if (pages > 1) {
    snprintf(line, sizeof(line), tr(STR_CULT_PAGE_FORMAT), static_cast<unsigned>(page + 1),
             static_cast<unsigned>(pages));
    renderer.drawText(SMALL_FONT_ID, x + width - renderer.getTextWidth(SMALL_FONT_ID, line), layout.pageLineTop, line);
  }
}

void CultivationActivity::renderScrollDetail(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const int slot = selectedSlot();
  if (slot < 0) return;
  const scrolls::Scroll sc = scrolls::get(s, static_cast<uint8_t>(slot));
  char line[256];
  char attrs[96];

  const auto head = renderer.wrappedText(UI_12_FONT_ID, nameBuf_, width, 3, EpdFontFamily::BOLD);
  for (const auto& h : head) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += 4;
  scrollAttributes(sc, attrs, sizeof(attrs));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, attrs, 2);
  snprintf(line, sizeof(line), tr(STR_CULT_SCROLL_QUALITY_FORMAT),
           ui::qualityName(scrolls::qualityFor(sc.comprehension)), static_cast<unsigned>(sc.comprehension));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  snprintf(line, sizeof(line), tr(STR_CULT_SCROLL_HOURS_FORMAT), static_cast<unsigned>(sc.hours));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;

  // What it is for.
  if (sc.kind == scrolls::Kind::Manual) {
    char rate[16];
    ui::formatPermille(rate, sizeof(rate), scrolls::manualPermille(sc));
    snprintf(line, sizeof(line), tr(STR_CULT_SCROLL_MANUAL_RATE_FORMAT), rate);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
    if (sc.element == s.character.rootElement) {
      y = drawWrapped(SMALL_FONT_ID, x, y + 2, width, tr(STR_CULT_SCROLL_ELEMENT_MATCH), 3);
    }
  } else {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, tr(STR_CULT_SCROLL_TECHNIQUE_USE), 3);
  }
  const scrolls::Tier tier = scrolls::tierOf(sc);
  if (tier >= scrolls::Tier::Heaven) {
    snprintf(line, sizeof(line), "%s", tr(STR_CULT_SCROLL_KEY_ANY));
  } else {
    // The highest realm this tier opens: Mortal Foundation (2), Yellow Core (3), Earth Nascent Soul (4).
    const uint8_t upTo = static_cast<uint8_t>(2 + static_cast<int>(tier));
    snprintf(line, sizeof(line), tr(STR_CULT_SCROLL_KEY_UPTO_FORMAT), ui::realmName(upTo));
  }
  y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  if (sc.equipped()) {
    y = drawWrapped(
        UI_10_FONT_ID, x, y + 4, width,
        sc.kind == scrolls::Kind::Manual ? tr(STR_CULT_SCROLL_EQUIPPED_MANUAL) : tr(STR_CULT_SCROLL_EQUIPPED_TECHNIQUE),
        2);
  }

  // Actions at the bottom; a notice just above them.
  const int rowHeight = metrics.listRowHeight;
  const int listTop = bottom - rowHeight * detailActionCount_;
  if (notice_[0] != '\0') {
    const int noticeTop = listTop - renderer.getLineHeight(SMALL_FONT_ID) * 2 - 4;
    if (noticeTop > y) drawWrapped(SMALL_FONT_ID, x, noticeTop, width, notice_, 2);
  }
  GUI.drawList(renderer, Rect{0, listTop, renderer.getScreenWidth(), rowHeight * detailActionCount_},
               detailActionCount_, selected_, [this, sc](const int i) -> std::string {
                 char label[96];
                 switch (detailActions_[i]) {
                   case DetailAction::Equip:
                     if (sc.kind != scrolls::Kind::Manual &&
                         scrolls::equippedTechniques(*state_) >= scrolls::kMaxEquippedTechniques) {
                       return tr(STR_CULT_EQUIP_FULL);
                     }
                     return sc.kind == scrolls::Kind::Manual ? tr(STR_CULT_EQUIP_MANUAL) : tr(STR_CULT_EQUIP);
                   case DetailAction::Unequip:
                     return tr(STR_CULT_UNEQUIP);
                   case DetailAction::Sell:
                     snprintf(label, sizeof(label), tr(STR_CULT_SELL_FORMAT),
                              static_cast<unsigned long>(scrolls::sellPrice(sc)));
                     return label;
                 }
                 return {};
               });
}

void CultivationActivity::renderSellConfirm(const int x, const int y, const int width) {
  const int slot = selectedSlot();
  if (slot < 0) return;
  const scrolls::Scroll sc = scrolls::get(*state_, static_cast<uint8_t>(slot));
  char line[320];
  snprintf(line, sizeof(line), tr(STR_CULT_SELL_CONFIRM_FORMAT), nameBuf_,
           static_cast<unsigned long>(scrolls::sellPrice(sc)));
  drawWrapped(UI_10_FONT_ID, x, y, width, line, 10);
}

#endif  // CROSSINK_CULTIVATION
