#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Chronicle.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "ExploreText.h"
#include "GameText.h"
#include "Meridians.h"
#include "Rng.h"
#include "Simulation.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Meridians (v0.2.4): one list row per channel ("Lung · Metal", "1 / 2"), and
// under it the selected channel's next point: where it is, what it costs and
// what it gives, or why it cannot be opened yet. Select opens it, pays from
// the Qi bar, writes a Chronicle line and saves. The rules are in Meridians.{h,cpp}.
// No heap: rows and details are worked out while drawing from the save.

using namespace cultivation;

namespace {

constexpr uint32_t kKindMeridianChronicle = 120;  // contentSeed kind (105-119 stay alchemy's)

const char* elementWord(const Element e) {
  switch (e) {
    case Element::Wood:
      return ctr(CULT_ELEMENT_WOOD);
    case Element::Fire:
      return ctr(CULT_ELEMENT_FIRE);
    case Element::Earth:
      return ctr(CULT_ELEMENT_EARTH);
    case Element::Metal:
      return ctr(CULT_ELEMENT_METAL);
    case Element::Water:
      return ctr(CULT_ELEMENT_WATER);
    default:
      return "";
  }
}

meridians::Channel channelAt(const int row) {
  return static_cast<meridians::Channel>(row >= 0 && row < meridians::kChannels ? row : 0);
}

}  // namespace

void CultivationActivity::openMeridians() {
  RenderLock lock(*this);
  meridianReturn_ = selected_;
  selected_ = 0;
  notice_[0] = '\0';
  screen_ = Screen::Meridians;
  requestUpdate();
}

void CultivationActivity::closeMeridians() {
  RenderLock lock(*this);
  notice_[0] = '\0';
  refreshActions();
  selected_ = meridianReturn_ < actionCount_ ? meridianReturn_ : 0;
  screen_ = Screen::Cultivator;
  requestUpdate();
}

bool CultivationActivity::handleMeridians(const bool back, const bool confirm, const bool up, const bool down) {
  if (screen_ != Screen::Meridians) return false;
  if (back) {
    closeMeridians();
  } else if (confirm) {
    RenderLock lock(*this);
    const uint8_t point = meridians::nextIn(*state_, channelAt(selected_));
    if (point < meridians::kPoints && meridians::open(*state_, point)) {
      meridianChronicle(point);
      persist();
      char rate[16];
      ui::formatPermille(rate, sizeof(rate), qiMultiplierPermille(*state_));
      snprintf(notice_, sizeof(notice_), ctr(CULT_MERIDIAN_OPENED_FORMAT), meridians::pointName(point), rate);
      LOG_INF("CULT", "Meridian opened: point %u, %u of %u", static_cast<unsigned>(point),
              static_cast<unsigned>(meridians::openCount(*state_)),
              static_cast<unsigned>(meridians::allowance(*state_)));
    } else {
      notice_[0] = '\0';  // show why it cannot be opened
    }
    requestUpdate();
  } else if (up || down) {
    notice_[0] = '\0';
    handleList(meridians::kChannels, up, down);
  }
  return true;
}

// One Chronicle line per point opened, seeded by the point so each reads differently.
void CultivationActivity::meridianChronicle(const uint8_t point) {
  if (!grammar_) return;
  lifelog::Line line;
  const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindMeridianChronicle, point);
  if (exploretext::meridianLine(line, *state_, seed, meridians::pointName(point), meridians::pointPlace(point),
                                *grammar_)) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::renderMeridians(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[200];

  char rate[16];
  ui::formatPermille(rate, sizeof(rate), meridians::qiPermille(s));
  snprintf(line, sizeof(line), ctr(CULT_MERIDIANS_HEADER_FORMAT), static_cast<unsigned>(meridians::openCount(s)),
           static_cast<unsigned>(meridians::allowance(s)), rate);
  y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;

  // The details take the bottom: up to 6 lines of the regular font.
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int detailTop = bottom - lineHeight * 6;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), detailTop - metrics.verticalSpacing - y}, meridians::kChannels,
      selected_,
      [](const int i) -> std::string {
        const meridians::Channel c = channelAt(i);
        const Element e = meridians::channelElement(c);
        if (e == Element::Count) return meridians::channelName(c);
        return std::string(meridians::channelName(c)) + " · " + elementWord(e);
      },
      nullptr, nullptr,
      [this](const int i) -> std::string {
        const meridians::Channel c = channelAt(i);
        char v[16];
        snprintf(v, sizeof(v), "%u / %u", static_cast<unsigned>(meridians::openIn(*state_, c)),
                 static_cast<unsigned>(meridians::pointsIn(c)));
        return v;
      },
      false,
      [this](const int i) -> bool {  // dimmed: nothing to open in this channel now
        const meridians::Channel c = channelAt(i);
        const uint8_t next = meridians::nextIn(*state_, c);
        return next >= meridians::kPoints || (meridians::prerequisite(next) < meridians::kPoints &&
                                              !meridians::isOpen(*state_, meridians::prerequisite(next)));
      });

  // The selected channel's next point.
  int dy = detailTop;
  const meridians::Channel c = channelAt(selected_);
  const uint8_t point = meridians::nextIn(s, c);
  if (notice_[0] != '\0') {
    drawWrapped(UI_10_FONT_ID, x, dy, width, notice_, 6);
    return;
  }
  if (point >= meridians::kPoints) {
    drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_MERIDIAN_FULL), 2);
    return;
  }
  snprintf(line, sizeof(line), ctr(CULT_MERIDIAN_POINT_FORMAT), meridians::pointName(point),
           meridians::pointPlace(point));
  dy = drawWrapped(UI_10_FONT_ID, x, dy, width, line, 2);
  const char* effect = meridians::channelEffect(c);
  if (effect != nullptr) {
    snprintf(line, sizeof(line), ctr(CULT_MERIDIAN_GIVES_FORMAT), effect);
    dy = drawWrapped(UI_10_FONT_ID, x, dy, width, line, 2);
  } else {
    dy = drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_MERIDIAN_GIVES_QI), 2);
  }
  switch (meridians::check(s, point)) {
    case meridians::Check::Locked:
      snprintf(line, sizeof(line), ctr(CULT_MERIDIAN_LOCKED_FORMAT),
               meridians::pointName(meridians::prerequisite(point)));
      break;
    case meridians::Check::NoAllowance:
      snprintf(line, sizeof(line), "%s", ctr(CULT_MERIDIAN_NO_ALLOWANCE));
      break;
    default: {
      char cost[24];
      char have[24];
      meridians::costOfNext(s).format(cost, sizeof(cost));
      s.progress.qi.format(have, sizeof(have));
      snprintf(line, sizeof(line), ctr(CULT_MERIDIAN_COST_FORMAT), cost, have);
      if (meridians::check(s, point) == meridians::Check::NotEnoughQi) {
        const size_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " %s", ctr(CULT_MERIDIAN_NO_QI));
      }
      break;
    }
  }
  drawWrapped(UI_10_FONT_ID, x, dy, width, line, 3);
}

#endif  // CROSSINK_CULTIVATION
