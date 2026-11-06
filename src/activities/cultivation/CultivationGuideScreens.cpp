#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CultivationActivity.h"
#include "GameText.h"
#include "components/UITheme.h"
#include "fontIds.h"

// The Guide (v0.2.9, agreed with the user in session 13): a cell on the
// Cultivator screen, "(New!)" once for every character, a list of topics,
// each one to three pages of text. Topics and pages are flash strings
// (GameStrings.inc); nothing is allocated. Topics 6 and 7 wait until their
// features open (the user's choice): Alchemy, meridians and Daos once any of
// those cells is shown, the rival and the ranking once the Rivals cell is.

using namespace cultivation;

namespace {

constexpr uint8_t kMaxGuidePages = 3;
enum class GuideGate : uint8_t { Always, Crafts, Rivals };

struct GuideTopic {
  GameStr title;
  GameStr sub;
  GameStr pages[kMaxGuidePages];
  uint8_t pageCount;
  GuideGate gate;
};

constexpr GuideTopic kTopics[] = {
    {GameStr::CULT_GUIDE_T1,
     GameStr::CULT_GUIDE_T1_SUB,
     {GameStr::CULT_GUIDE_T1_P1, GameStr::CULT_GUIDE_T1_P2, GameStr::CULT_GUIDE_T1_P2},
     2,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T2,
     GameStr::CULT_GUIDE_T2_SUB,
     {GameStr::CULT_GUIDE_T2_P1, GameStr::CULT_GUIDE_T2_P1, GameStr::CULT_GUIDE_T2_P1},
     1,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T3,
     GameStr::CULT_GUIDE_T3_SUB,
     {GameStr::CULT_GUIDE_T3_P1, GameStr::CULT_GUIDE_T3_P2, GameStr::CULT_GUIDE_T3_P2},
     2,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T4,
     GameStr::CULT_GUIDE_T4_SUB,
     {GameStr::CULT_GUIDE_T4_P1, GameStr::CULT_GUIDE_T4_P2, GameStr::CULT_GUIDE_T4_P2},
     2,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T5,
     GameStr::CULT_GUIDE_T5_SUB,
     {GameStr::CULT_GUIDE_T5_P1, GameStr::CULT_GUIDE_T5_P2, GameStr::CULT_GUIDE_T5_P2},
     2,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T6,
     GameStr::CULT_GUIDE_T6_SUB,
     {GameStr::CULT_GUIDE_T6_P1, GameStr::CULT_GUIDE_T6_P2, GameStr::CULT_GUIDE_T6_P2},
     2,
     GuideGate::Crafts},
    {GameStr::CULT_GUIDE_T7,
     GameStr::CULT_GUIDE_T7_SUB,
     {GameStr::CULT_GUIDE_T7_P1, GameStr::CULT_GUIDE_T7_P2, GameStr::CULT_GUIDE_T7_P2},
     2,
     GuideGate::Rivals},
    {GameStr::CULT_GUIDE_T8,
     GameStr::CULT_GUIDE_T8_SUB,
     {GameStr::CULT_GUIDE_T8_P1, GameStr::CULT_GUIDE_T8_P1, GameStr::CULT_GUIDE_T8_P1},
     1,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T9,  // v0.3.0
     GameStr::CULT_GUIDE_T9_SUB,
     {GameStr::CULT_GUIDE_T9_P1, GameStr::CULT_GUIDE_T9_P2, GameStr::CULT_GUIDE_T9_P2},
     2,
     GuideGate::Always},
    {GameStr::CULT_GUIDE_T10,  // v0.3.1
     GameStr::CULT_GUIDE_T10_SUB,
     {GameStr::CULT_GUIDE_T10_P1, GameStr::CULT_GUIDE_T10_P1, GameStr::CULT_GUIDE_T10_P1},
     1,
     GuideGate::Always},
};
constexpr uint8_t kTopicCount = sizeof(kTopics) / sizeof(kTopics[0]);

}  // namespace

uint8_t CultivationActivity::guideTopics(uint8_t* out) const {
  uint8_t n = 0;
  const bool crafts = actionShown(Action::Alchemy) || actionShown(Action::Meridians) || actionShown(Action::Daos);
  const bool rivalsOpen = actionShown(Action::Rivals);
  for (uint8_t i = 0; i < kTopicCount; ++i) {
    const GuideGate g = kTopics[i].gate;
    if ((g == GuideGate::Crafts && !crafts) || (g == GuideGate::Rivals && !rivalsOpen)) continue;
    out[n++] = i;
  }
  return n;
}

uint8_t CultivationActivity::guidePageCount(const uint8_t topic) const {
  return topic < kTopicCount ? kTopics[topic].pageCount : 0;
}

const char* CultivationActivity::guideTitle(char* buf, const size_t len) const {
  if (guideTopic_ >= kTopicCount) return ctr(CULT_GUIDE_TITLE);
  const GuideTopic& t = kTopics[guideTopic_];
  if (t.pageCount <= 1) return gameText(t.title);
  snprintf(buf, len, "%s (%u/%u)", gameText(t.title), static_cast<unsigned>(guidePage_ + 1),
           static_cast<unsigned>(t.pageCount));
  return buf;
}

void CultivationActivity::openGuide() {
  RenderLock lock(*this);
  guideReturn_ = selected_;
  selected_ = guideListSelected_;
  uint8_t topics[kTopicCount];
  if (selected_ >= guideTopics(topics)) selected_ = 0;
  screen_ = Screen::Guide;
  requestUpdate();
}

void CultivationActivity::closeGuide() {
  RenderLock lock(*this);
  guideListSelected_ = selected_;
  refreshActions();
  selected_ = guideReturn_ < actionCount_ ? guideReturn_ : 0;
  screen_ = Screen::Cultivator;
  requestUpdate();
}

bool CultivationActivity::handleGuide(const bool back, const bool confirm, const bool up, const bool down) {
  if (screen_ != Screen::Guide && screen_ != Screen::GuidePage) return false;
  uint8_t topics[kTopicCount];
  const uint8_t count = guideTopics(topics);
  if (screen_ == Screen::Guide) {
    if (back) {
      closeGuide();
    } else if (confirm && selected_ < count) {
      RenderLock lock(*this);
      guideListSelected_ = selected_;
      guideTopic_ = topics[selected_];
      guidePage_ = 0;
      screen_ = Screen::GuidePage;
      requestUpdate();
    } else if (up || down) {
      handleList(count, up, down);
    }
    return true;
  }
  const uint8_t pages = guidePageCount(guideTopic_);
  if (back || (confirm && guidePage_ + 1 >= pages)) {
    RenderLock lock(*this);
    selected_ = guideListSelected_;
    screen_ = Screen::Guide;
    requestUpdate();
  } else if ((confirm || down) && guidePage_ + 1 < pages) {
    ++guidePage_;
    requestUpdate();
  } else if (up && guidePage_ > 0) {
    --guidePage_;
    requestUpdate();
  }
  return true;
}

void CultivationActivity::renderGuide(const int x, const int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int noteTop = bottom - renderer.getLineHeight(SMALL_FONT_ID) * 2;
  uint8_t topics[kTopicCount];
  const uint8_t count = guideTopics(topics);
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), noteTop - metrics.verticalSpacing - y}, count, selected_,
      [this](const int i) -> std::string {
        uint8_t t[kTopicCount];
        const uint8_t n = guideTopics(t);
        return i < n ? gameText(kTopics[t[i]].title) : "";
      },
      [this](const int i) -> std::string {
        uint8_t t[kTopicCount];
        const uint8_t n = guideTopics(t);
        return i < n ? gameText(kTopics[t[i]].sub) : "";
      });
  if (count < kTopicCount) drawWrapped(SMALL_FONT_ID, x, noteTop, width, ctr(CULT_GUIDE_MORE_LATER), 2);
}

void CultivationActivity::renderGuidePage(const int x, int y, const int width, const int bottom) {
  if (guideTopic_ >= kTopicCount) return;
  const GuideTopic& t = kTopics[guideTopic_];
  const char* text = gameText(t.pages[guidePage_ < t.pageCount ? guidePage_ : 0]);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  // Paragraphs are separated by '\n' in the string; each is wrapped on its own
  // through a 400-byte stack buffer (no paragraph is longer).
  char para[400];
  const char* p = text;
  while (*p != '\0' && y + lineHeight <= bottom) {
    const char* end = strchr(p, '\n');
    const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
    const size_t copy = n < sizeof(para) - 1 ? n : sizeof(para) - 1;
    memcpy(para, p, copy);
    para[copy] = '\0';
    const int room = (bottom - y) / lineHeight;
    y = drawWrapped(UI_10_FONT_ID, x, y, width, para, room) + lineHeight / 3;
    if (!end) break;
    p = end + 1;
  }
}

#endif  // CROSSINK_CULTIVATION
