#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Achievements.h"
#include "Chronicle.h"
#include "CultivationActivity.h"
#include "GameText.h"
#include "components/UITheme.h"
#include "fontIds.h"

// v0.3.8: achievements (agreed with the user in session 18), browsed from
// Lineage: the groups with the totals by tier, one group's list, one
// achievement. Uses the LineageView (no allocation of its own). What is earned
// is announced by announceAchievements(): one line in the Return summary, or
// a Chronicle line and a notice on the Cultivator screen.

using namespace cultivation;

namespace {

GameStr offset(const GameStr first, const int i) {
  return static_cast<GameStr>(static_cast<uint16_t>(first) + static_cast<uint16_t>(i));
}

const char* achName(const uint8_t id) { return gameText(offset(GameStr::CULT_ACH_000, 2 * id)); }
const char* achDescription(const uint8_t id) { return gameText(offset(GameStr::CULT_ACH_000, 2 * id + 1)); }
const char* achHint(const uint8_t id) {
  const int h = achievements::hiddenIndex(id);
  return h >= 0 ? gameText(offset(GameStr::CULT_ACH_HINT_00, h)) : "";
}
const char* groupName(const uint8_t g) { return gameText(offset(GameStr::CULT_ACH_GROUP_0, g)); }
const char* tierName(const achievements::Tier t) {
  return gameText(offset(GameStr::CULT_ACH_TIER_0, static_cast<int>(t)));
}

// Shown by name: earned, or not hidden.
bool named(const GameState& s, const uint8_t id) {
  return achievements::earned(s, id) || !achievements::info(id).hidden;
}

// The ones whose count runs over all lives (said on the detail screen).
bool overLives(const uint8_t id) {
  using achievements::Id;
  const auto i = static_cast<Id>(id);
  return (i >= Id::Hours1 && i <= Id::Hours1000) || i == Id::LongDays10 || i == Id::LongDays50 ||
         (i >= Id::Books1 && i <= Id::Books100) || (i >= Id::Win1 && i <= Id::Guardian10) || i == Id::Brew1 ||
         i == Id::Brew50 || i == Id::LadderFirst5;
}

// "Earned at reading hour 312." or "Recognised from your past."
void earnedText(const GameState& s, const uint8_t id, char* out, const size_t len) {
  const uint16_t h = achievements::earnedHour(s, id);
  if (h == achievements::kRecognised) {
    snprintf(out, len, "%s", ctr(CULT_ACH_RECOGNISED_ROW));
  } else {
    snprintf(out, len, ctr(CULT_ACH_EARNED_AT_FORMAT), static_cast<unsigned>(h));
  }
}

}  // namespace

const char* CultivationActivity::achievementTitle() const {
  switch (screen_) {
    case Screen::Achievements:
      return ctr(CULT_ACH_TITLE);
    case Screen::AchievementList:
    case Screen::AchievementDetail:
      return lineage_ ? groupName(lineage_->achGroup) : ctr(CULT_ACH_TITLE);
    default:
      return nullptr;
  }
}

// Called under the render lock (from lineageChoose).
void CultivationActivity::openAchievements() {
  if (!lineage_) return;
  lineage_->lineageSelected = selected_;
  achievements::markSeen(*state_);  // the Lineage cell's "(New!)" goes
  persist();
  notice_[0] = '\0';
  selected_ = lineage_->achGroupSelected;
  screen_ = Screen::Achievements;
}

bool CultivationActivity::handleAchievements(const bool back, const bool confirm, const bool up, const bool down) {
  if (screen_ != Screen::Achievements && screen_ != Screen::AchievementList && screen_ != Screen::AchievementDetail) {
    return false;
  }
  if (!lineage_) {  // out of memory earlier: nothing to show
    if (back || confirm) closeLineage();
    return true;
  }
  LineageView& v = *lineage_;
  const auto group = static_cast<achievements::Group>(v.achGroup);
  if (up || down) {
    if (screen_ == Screen::Achievements) handleList(achievements::kGroupCount, up, down);
    if (screen_ == Screen::AchievementList) handleList(achievements::countIn(group), up, down);
    if (screen_ == Screen::AchievementDetail) {  // Up/Down walk the group's achievements
      const uint8_t first = achievements::firstOf(group);
      const uint8_t n = achievements::countIn(group);
      RenderLock lock(*this);
      const uint8_t at = static_cast<uint8_t>(v.achId - first);
      v.achId = static_cast<uint8_t>(first + (down ? (at + 1) % n : (at + n - 1) % n));
      v.achItemSelected = v.achId - first;
      requestUpdate();
    }
    return true;
  }
  if (!back && !confirm) return true;
  {
    RenderLock lock(*this);
    switch (screen_) {
      case Screen::Achievements:
        v.achGroupSelected = selected_;
        if (back) {
          selected_ = v.lineageSelected;
          screen_ = Screen::Lineage;
        } else {
          v.achGroup = static_cast<uint8_t>(selected_);
          v.achItemSelected = 0;
          selected_ = 0;
          screen_ = Screen::AchievementList;
        }
        break;
      case Screen::AchievementList:
        v.achItemSelected = selected_;
        if (back) {
          selected_ = v.achGroupSelected;
          screen_ = Screen::Achievements;
        } else {
          v.achId = static_cast<uint8_t>(achievements::firstOf(group) + selected_);
          screen_ = Screen::AchievementDetail;
        }
        break;
      default:  // the detail: either button goes back to the list, on this achievement
        selected_ = v.achItemSelected;
        screen_ = Screen::AchievementList;
        break;
    }
  }
  requestUpdate();
  return true;
}

void CultivationActivity::achievementLabels(const char*& confirmLabel, const char*& upLabel,
                                            const char*& downLabel) const {
  upLabel = tr(STR_DIR_UP);
  downLabel = tr(STR_DIR_DOWN);
  confirmLabel = screen_ == Screen::AchievementDetail ? "" : ctr(CULT_COLLECTION_OPEN);
}

void CultivationActivity::renderAchievements(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[200];
  snprintf(line, sizeof(line), ctr(CULT_ACH_SUMMARY_FORMAT), static_cast<unsigned>(achievements::earnedCount(s)),
           static_cast<unsigned>(achievements::kCount), static_cast<unsigned>(achievements::percent(s)));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  using achievements::Tier;
  snprintf(line, sizeof(line), ctr(CULT_ACH_TIERS_FORMAT), tierName(Tier::Mortal),
           static_cast<unsigned>(achievements::earnedOfTier(s, Tier::Mortal)),
           static_cast<unsigned>(achievements::countOfTier(Tier::Mortal)), tierName(Tier::Earth),
           static_cast<unsigned>(achievements::earnedOfTier(s, Tier::Earth)),
           static_cast<unsigned>(achievements::countOfTier(Tier::Earth)), tierName(Tier::Heaven),
           static_cast<unsigned>(achievements::earnedOfTier(s, Tier::Heaven)),
           static_cast<unsigned>(achievements::countOfTier(Tier::Heaven)), tierName(Tier::Immortal),
           static_cast<unsigned>(achievements::earnedOfTier(s, Tier::Immortal)),
           static_cast<unsigned>(achievements::countOfTier(Tier::Immortal)));
  y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_ACH_INTRO), 3) + metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), bottom - y}, achievements::kGroupCount, selected_,
      [](const int i) -> std::string { return groupName(static_cast<uint8_t>(i)); }, nullptr, nullptr,
      [this](const int i) -> std::string {
        const auto g = static_cast<achievements::Group>(i);
        char value[16];
        snprintf(value, sizeof(value), "%u/%u", static_cast<unsigned>(achievements::earnedIn(*state_, g)),
                 static_cast<unsigned>(achievements::countIn(g)));
        return value;
      });
}

void CultivationActivity::renderAchievementList(const int x, const int y, const int width, const int bottom) {
  if (!lineage_) return;
  (void)x;
  (void)width;
  const auto group = static_cast<achievements::Group>(lineage_->achGroup);
  const uint8_t first = achievements::firstOf(group);
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), bottom - y}, achievements::countIn(group), selected_,
      [this, first](const int i) -> std::string {
        const auto id = static_cast<uint8_t>(first + i);
        return named(*state_, id) ? achName(id) : ctr(CULT_ACH_HIDDEN);
      },
      [this, first](const int i) -> std::string {
        const GameState& s = *state_;
        const auto id = static_cast<uint8_t>(first + i);
        char sub[200];
        if (achievements::earned(s, id)) {
          earnedText(s, id, sub, sizeof(sub));
          return sub;
        }
        const char* what = named(s, id) ? achDescription(id) : achHint(id);
        const achievements::Progress p = achievements::progress(s, id);
        if (p.need > 0) {
          snprintf(sub, sizeof(sub), ctr(CULT_ACH_ROW_PROGRESS_FORMAT), static_cast<unsigned long>(p.have),
                   static_cast<unsigned long>(p.need), what);
        } else {
          snprintf(sub, sizeof(sub), "%s", what);
        }
        return sub;
      },
      nullptr,
      [first](const int i) -> std::string {
        return tierName(achievements::info(static_cast<uint8_t>(first + i)).tier);
      },
      false, [this, first](const int i) { return !achievements::earned(*state_, static_cast<uint8_t>(first + i)); });
}

void CultivationActivity::renderAchievementDetail(const int x, int y, const int width, const int bottom) {
  if (!lineage_) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const uint8_t id = lineage_->achId;
  const achievements::Info& in = achievements::info(id);
  const bool isEarned = achievements::earned(s, id);
  char line[240];
  const int bigLine = renderer.getLineHeight(UI_12_FONT_ID);
  const auto title = renderer.wrappedText(UI_12_FONT_ID, named(s, id) ? achName(id) : ctr(CULT_ACH_HIDDEN_NAME), width,
                                          2, EpdFontFamily::BOLD);
  for (const auto& t : title) {
    renderer.drawText(UI_12_FONT_ID, x, y, t.c_str(), true, EpdFontFamily::BOLD);
    y += bigLine;
  }
  snprintf(line, sizeof(line), ctr(CULT_ACH_DETAIL_TIER_FORMAT), tierName(in.tier),
           groupName(static_cast<uint8_t>(in.group)));
  y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  if (named(s, id)) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, achDescription(id), 4);
  } else {
    snprintf(line, sizeof(line), ctr(CULT_ACH_DETAIL_HINT_FORMAT), achHint(id));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4);
  }
  const achievements::Progress p = achievements::progress(s, id);
  if (p.need > 0) {
    snprintf(line, sizeof(line), ctr(CULT_ACH_DETAIL_PROGRESS_FORMAT), static_cast<unsigned long>(p.have),
             static_cast<unsigned long>(p.need), static_cast<unsigned>(p.have * 100u / p.need));
    y = drawWrapped(UI_10_FONT_ID, x, y + 6, width, line, 2);
  }
  if (overLives(id)) y = drawWrapped(SMALL_FONT_ID, x, y + 6, width, ctr(CULT_ACH_ALL_LIVES), 2);
  if (isEarned) {
    earnedText(s, id, line, sizeof(line));
  } else {
    snprintf(line, sizeof(line), "%s", ctr(CULT_ACH_LOCKED));
  }
  y = drawWrapped(UI_10_FONT_ID, x, y + metrics.verticalSpacing, width, line, 2);
  snprintf(line, sizeof(line), ctr(CULT_ACH_DETAIL_POSITION_FORMAT),
           static_cast<unsigned>(id - achievements::firstOf(in.group) + 1),
           static_cast<unsigned>(achievements::countIn(in.group)));
  drawWrapped(SMALL_FONT_ID, x, bottom - renderer.getLineHeight(SMALL_FONT_ID), width, line, 1);
  (void)y;
}

// --- Announcing --------------------------------------------------------------------------------

void CultivationActivity::announceAchievements(const bool summary) {
  if (!state_) return;
  const uint8_t* bits = state_->achievements.data() + achievements::kBitsOffset;
  uint8_t fresh[achievements::kCount];
  uint8_t freshCount = 0;
  uint8_t recognised = 0;
  for (uint8_t id = 0; id < achievements::kCount; ++id) {
    const bool now = (bits[id / 8] & (1u << (id % 8))) != 0;
    const bool before = (achSeenBits_[id / 8] & (1u << (id % 8))) != 0;
    if (!now || before) continue;
    if (achievements::earnedHour(*state_, id) == achievements::kRecognised) {
      ++recognised;
    } else {
      fresh[freshCount++] = id;
    }
  }
  memcpy(achSeenBits_, bits, achievements::kBitBytes);
  if (freshCount == 0 && recognised == 0) return;

  // One line: "Achievement earned: A." / "Achievements earned: A, B and 3 more." Recognised ones
  // (an older save's, set up once) are counted, not named.
  char text[lifelog::kLineSize];
  text[0] = '\0';
  if (freshCount > 0) {
    char names[160];
    names[0] = '\0';
    constexpr uint8_t kNamed = 3;
    const uint8_t shown = freshCount < kNamed ? freshCount : kNamed;
    for (uint8_t i = 0; i < shown; ++i) {
      const size_t used = strlen(names);
      const char* sep = i == 0 ? "" : (i + 1 == shown && freshCount <= kNamed ? ctr(CULT_ACH_AND) : ", ");
      snprintf(names + used, sizeof(names) - used, "%s%s", sep, achName(fresh[i]));
    }
    if (freshCount > kNamed) {
      const size_t used = strlen(names);
      snprintf(names + used, sizeof(names) - used, ctr(CULT_ACH_AND_MORE_FORMAT),
               static_cast<unsigned>(freshCount - kNamed));
    }
    snprintf(text, sizeof(text), freshCount == 1 ? ctr(CULT_ACH_EARNED_ONE_FORMAT) : ctr(CULT_ACH_EARNED_MANY_FORMAT),
             names);
  }
  if (recognised > 0) {
    const size_t used = strlen(text);
    snprintf(text + used, sizeof(text) - used, ctr(CULT_ACH_RECOGNISED_FORMAT), used > 0 ? " " : "",
             static_cast<unsigned>(recognised), recognised == 1 ? "" : "s");
  }
  LOG_INF("CULT", "Achievements: %u new, %u recognised", static_cast<unsigned>(freshCount),
          static_cast<unsigned>(recognised));

  lifelog::Line line;
  line.kind = lifelog::LineKind::Event;
  line.season = lifelog::seasonOf(state_->progress.ageMonths);
  // Labelled like the other event lines ("Spring, age 27. "): the Chronicle reads the age from it.
  char label[32];
  lifelog::formatLabel(line.season, label, sizeof(label));
  snprintf(line.text, sizeof(line.text), "%s%s", label, text);
  if (summary && lifeLog_ && lifeLog_->count < lifelog::kMaxLines) {
    lifeLog_->lines[lifeLog_->count++] = line;  // the visit's lines go to the Chronicle together
    return;
  }
  if (state_->isCreated()) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
    if (packs_) packs_->closeFile();
  }
  snprintf(achNotice_, sizeof(achNotice_), "%s", text);  // the Cultivator screen: without the label
}

#endif  // CROSSINK_CULTIVATION
