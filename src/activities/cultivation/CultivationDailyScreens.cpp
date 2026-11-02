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
#include "Daily.h"
#include "Daos.h"
#include "GameText.h"
#include "LifeLog.h"
#include "Simulation.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Daily systems and Daos (v0.2.6, milestone 9a). Daily: the omen of the day,
// the three tasks (Select collects a done one, or rerolls an unfinished one
// once a day) and the streak. Daos: five rows, the Daos found and the slots
// still empty, never the Daos not found or their conditions (the user wanted
// them hidden). The rules are in Daily.{h,cpp} and Daos.{h,cpp}. No heap but
// the ingredient names (~0.7 KB) while Daily is open, for its rewards.

using namespace cultivation;

namespace cultivation {
namespace ui {

const char* daoName(const daos::Dao dao) {
  switch (dao) {
    case daos::Dao::Time:
      return ctr(CULT_DAO_TIME);
    case daos::Dao::Stillness:
      return ctr(CULT_DAO_STILLNESS);
    case daos::Dao::Tide:
      return ctr(CULT_DAO_TIDE);
    case daos::Dao::Water:
      return ctr(CULT_DAO_WATER);
    case daos::Dao::Sun:
      return ctr(CULT_DAO_SUN);
    case daos::Dao::Fire:
      return ctr(CULT_DAO_FIRE);
    case daos::Dao::Metal:
      return ctr(CULT_DAO_METAL);
    case daos::Dao::Myriad:
      return ctr(CULT_DAO_MYRIAD);
    case daos::Dao::Fate:
      return ctr(CULT_DAO_FATE);
    default:
      return "";
  }
}

}  // namespace ui
}  // namespace cultivation

namespace {

const char* omenName(const daily::Omen o) {
  switch (o) {
    case daily::Omen::HerbsThick:
      return ctr(CULT_OMEN_HERBS);
    case daily::Omen::VeinsShallow:
      return ctr(CULT_OMEN_VEINS);
    case daily::Omen::SteadyFurnace:
      return ctr(CULT_OMEN_FURNACE);
    case daily::Omen::GenerousRoad:
      return ctr(CULT_OMEN_ROAD);
    case daily::Omen::SharpBlade:
      return ctr(CULT_OMEN_BLADE);
    case daily::Omen::LightFeet:
      return ctr(CULT_OMEN_FEET);
    case daily::Omen::ClearHeart:
      return ctr(CULT_OMEN_HEART);
    case daily::Omen::CalmSky:
      return ctr(CULT_OMEN_SKY);
    case daily::Omen::OldRuins:
      return ctr(CULT_OMEN_RUINS);
    case daily::Omen::QuietMind:
      return ctr(CULT_OMEN_MIND);
    default:
      return "";
  }
}

const char* omenEffect(const daily::Omen o) {
  switch (o) {
    case daily::Omen::HerbsThick:
      return ctr(CULT_OMEN_HERBS_EFFECT);
    case daily::Omen::VeinsShallow:
      return ctr(CULT_OMEN_VEINS_EFFECT);
    case daily::Omen::SteadyFurnace:
      return ctr(CULT_OMEN_FURNACE_EFFECT);
    case daily::Omen::GenerousRoad:
      return ctr(CULT_OMEN_ROAD_EFFECT);
    case daily::Omen::SharpBlade:
      return ctr(CULT_OMEN_BLADE_EFFECT);
    case daily::Omen::LightFeet:
      return ctr(CULT_OMEN_FEET_EFFECT);
    case daily::Omen::ClearHeart:
      return ctr(CULT_OMEN_HEART_EFFECT);
    case daily::Omen::CalmSky:
      return ctr(CULT_OMEN_SKY_EFFECT);
    case daily::Omen::OldRuins:
      return ctr(CULT_OMEN_RUINS_EFFECT);
    case daily::Omen::QuietMind:
      return ctr(CULT_OMEN_MIND_EFFECT);
    default:
      return "";
  }
}

void taskTitle(const GameState& s, const uint8_t slot, char* out, const size_t len) {
  const unsigned t = daily::target(s, slot);
  switch (daily::task(s, slot)) {
    case daily::Task::ReadMinutes:
      snprintf(out, len, ctr(CULT_TASK_MINUTES_FORMAT), t);
      return;
    case daily::Task::TurnPages:
      snprintf(out, len, ctr(CULT_TASK_PAGES_FORMAT), t);
      return;
    case daily::Task::TwoSessions:
      snprintf(out, len, "%s", ctr(CULT_TASK_SESSIONS));
      return;
    case daily::Task::Chart:
      snprintf(out, len, "%s", ctr(CULT_TASK_CHART));
      return;
    case daily::Task::Gather:
      snprintf(out, len, "%s", ctr(CULT_TASK_GATHER));
      return;
    case daily::Task::WinFight:
      snprintf(out, len, "%s", ctr(CULT_TASK_FIGHT));
      return;
    case daily::Task::Brew:
      snprintf(out, len, "%s", ctr(CULT_TASK_BREW));
      return;
    case daily::Task::Merchant:
      snprintf(out, len, "%s", ctr(CULT_TASK_MERCHANT));
      return;
    case daily::Task::Meridian:
      snprintf(out, len, "%s", ctr(CULT_TASK_MERIDIAN));
      return;
    default:
      out[0] = '\0';
      return;
  }
}

const char* foundLine(const daos::Dao d) {
  switch (d) {
    case daos::Dao::Time:
      return ctr(CULT_DAO_FOUND_TIME);
    case daos::Dao::Stillness:
      return ctr(CULT_DAO_FOUND_STILLNESS);
    case daos::Dao::Tide:
      return ctr(CULT_DAO_FOUND_TIDE);
    case daos::Dao::Water:
      return ctr(CULT_DAO_FOUND_WATER);
    case daos::Dao::Sun:
      return ctr(CULT_DAO_FOUND_SUN);
    case daos::Dao::Fire:
      return ctr(CULT_DAO_FOUND_FIRE);
    case daos::Dao::Metal:
      return ctr(CULT_DAO_FOUND_METAL);
    case daos::Dao::Myriad:
      return ctr(CULT_DAO_FOUND_MYRIAD);
    case daos::Dao::Fate:
      return ctr(CULT_DAO_FOUND_FATE);
    default:
      return "";
  }
}

const char* unitOf(const daos::Dao d) {
  switch (d) {
    case daos::Dao::Time:
      return ctr(CULT_DAO_UNIT_DAYS_STREAK);
    case daos::Dao::Stillness:
      return ctr(CULT_DAO_UNIT_DAYS_HOUR);
    case daos::Dao::Tide:
      return ctr(CULT_DAO_UNIT_DAYS_3H);
    case daos::Dao::Water:
      return ctr(CULT_DAO_UNIT_HOURS_NIGHT);
    case daos::Dao::Sun:
      return ctr(CULT_DAO_UNIT_HOURS_MORNING);
    case daos::Dao::Fire:
      return ctr(CULT_DAO_UNIT_HOURS_AFTERNOON);
    case daos::Dao::Metal:
      return ctr(CULT_DAO_UNIT_HOURS_EVENING);
    case daos::Dao::Myriad:
      return ctr(CULT_DAO_UNIT_HOURS);
    default:
      return ctr(CULT_DAO_UNIT_BOOKS);
  }
}

void effectLine(const GameState& s, const daos::Dao d, char* out, const size_t len) {
  switch (d) {
    case daos::Dao::Time:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_TIME_FORMAT), static_cast<unsigned>(daos::ruinBonus(s)));
      return;
    case daos::Dao::Stillness:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_STILLNESS_FORMAT),
               static_cast<unsigned>((daos::consolidationPermille(s) - 1000u) / 10u));
      return;
    case daos::Dao::Tide:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_TIDE_FORMAT), static_cast<unsigned>(daos::pityPerFailureBonus(s)));
      return;
    case daos::Dao::Water:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_WATER_FORMAT), static_cast<unsigned>(daos::heartDemonBonus(s)));
      return;
    case daos::Dao::Sun:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_SUN_FORMAT), static_cast<unsigned>((daos::healPermille(s) - 1000u) / 10u));
      return;
    case daos::Dao::Fire:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_FIRE_FORMAT), static_cast<unsigned>(daos::brewQualityBonus(s)));
      return;
    case daos::Dao::Metal:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_METAL_FORMAT),
               static_cast<unsigned>((1000u - daos::fightDamagePermille(s)) / 10u));
      return;
    case daos::Dao::Myriad:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_MYRIAD_FORMAT), static_cast<unsigned>(daos::stonesPercentBonus(s)));
      return;
    case daos::Dao::Fate:
      snprintf(out, len, ctr(CULT_DAO_EFFECT_FATE_FORMAT), static_cast<unsigned>(daos::prizeChanceBonus(s)));
      return;
    default:
      out[0] = '\0';
      return;
  }
}

// The found Daos in table order, then the empty slots: row -> Dao, or Dao::Count for a slot.
daos::Dao daoAtRow(const GameState& s, const int row) {
  int n = 0;
  for (uint8_t i = 0; i < daos::kCount; ++i) {
    if (!daos::found(s, static_cast<daos::Dao>(i))) continue;
    if (n == row) return static_cast<daos::Dao>(i);
    ++n;
  }
  return daos::Dao::Count;
}

// The realm whose entry opens slot i (0-based): 2 slots at Qi Condensation, then one a realm.
uint8_t slotRealm(const int slot) { return static_cast<uint8_t>(slot < 2 ? 1 : slot); }

}  // namespace

// --- Opening and closing ----------------------------------------------------------------------

void CultivationActivity::openDaily() {
  RenderLock lock(*this);
  dailyReturn_ = selected_;
  selected_ = 0;
  notice_[0] = '\0';
  loadIngredientNames();  // the rewards name the newest region's herb and ore
  screen_ = Screen::Daily;
  requestUpdate();
}

void CultivationActivity::openDaos() {
  RenderLock lock(*this);
  dailyReturn_ = selected_;
  selected_ = 0;
  notice_[0] = '\0';
  screen_ = Screen::Daos;
  requestUpdate();
}

void CultivationActivity::closeDailyOrDaos() {
  RenderLock lock(*this);
  notice_[0] = '\0';
  if (!alchemy_ && !explore_) ingredients_.reset();
  refreshActions();
  selected_ = dailyReturn_ < actionCount_ ? dailyReturn_ : 0;
  screen_ = Screen::Cultivator;
  requestUpdate();
}

int CultivationActivity::dailyRows() const {
  int n = 0;
  for (uint8_t i = 0; i < daily::kTaskSlots; ++i) n += daily::task(*state_, i) != daily::Task::None ? 1 : 0;
  return n;
}

uint8_t CultivationActivity::dailySlotAt(const int row) const {
  int n = 0;
  for (uint8_t i = 0; i < daily::kTaskSlots; ++i) {
    if (daily::task(*state_, i) == daily::Task::None) continue;
    if (n == row) return i;
    ++n;
  }
  return daily::kTaskSlots;
}

void CultivationActivity::dailyLabels(const char*& confirmLabel) const {
  confirmLabel = "";
  if (!daily::live(*state_)) return;
  const uint8_t slot = dailySlotAt(selected_);
  if (slot >= daily::kTaskSlots) return;
  if (daily::done(*state_, slot) && !daily::collected(*state_, slot)) {
    confirmLabel = ctr(CULT_COLLECT);
  } else if (!daily::done(*state_, slot) && daily::canReroll(*state_, slot)) {
    confirmLabel = ctr(CULT_REROLL);
  }
}

void CultivationActivity::formatDailyReward(const daily::Reward& r, char* out, const size_t len) const {
  if (r.item != inventory::Item::None && r.count > 0) {
    char name[64];
    itemLabel(r.item, name, sizeof(name));
    snprintf(out, len, ctr(CULT_TASK_REWARD_ITEM_FORMAT), static_cast<unsigned>(r.count), name);
  } else {
    snprintf(out, len, ctr(CULT_TASK_REWARD_STONES_FORMAT), static_cast<unsigned long>(r.stones));
  }
}

bool CultivationActivity::handleDaily(const bool back, const bool confirm, const bool up, const bool down) {
  if (screen_ != Screen::Daily && screen_ != Screen::Daos) return false;
  if (back) {
    closeDailyOrDaos();
    return true;
  }
  if (screen_ == Screen::Daos) {
    if (up || down) handleList(daos::kPerLife, up, down);
    return true;
  }
  if (confirm && daily::live(*state_)) {
    RenderLock lock(*this);
    const uint8_t slot = dailySlotAt(selected_);
    daily::Reward r;
    if (slot < daily::kTaskSlots && daily::collect(*state_, slot, &r)) {
      char what[96];
      formatDailyReward(r, what, sizeof(what));
      snprintf(notice_, sizeof(notice_), ctr(CULT_TASK_COLLECTED_FORMAT), what);
      persist();
      LOG_INF("CULT", "Daily task %u collected", static_cast<unsigned>(slot));
    } else if (slot < daily::kTaskSlots && !daily::done(*state_, slot) && daily::reroll(*state_, slot)) {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_TASK_REROLLED));
      persist();
      LOG_INF("CULT", "Daily task %u rerolled", static_cast<unsigned>(slot));
    }
    requestUpdate();
  } else if (up || down) {
    notice_[0] = '\0';
    handleList(dailyRows(), up, down);
  }
  return true;
}

// --- Daily ------------------------------------------------------------------------------------

void CultivationActivity::renderDaily(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];

  if (!daily::live(s)) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_DAILY_NO_DATE), 6);
    return;
  }

  // The omen: its name in bold, what it does under it.
  const daily::Omen o = daily::omen(s);
  snprintf(line, sizeof(line), ctr(CULT_OMEN_FORMAT), omenName(o));
  const auto header = renderer.wrappedText(UI_10_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : header) {
    renderer.drawText(UI_10_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y = drawWrapped(UI_10_FONT_ID, x, y, width, omenEffect(o), 2) + metrics.verticalSpacing;

  // The details of the selected task, then the streak, take the bottom.
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int detailTop = bottom - lineHeight * 4 - smallLine * 4 - metrics.verticalSpacing;
  const int rows = dailyRows();
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), detailTop - metrics.verticalSpacing - y}, rows, selected_,
      [this](const int i) -> std::string {
        char t[96];
        taskTitle(*state_, dailySlotAt(i), t, sizeof(t));
        return t;
      },
      nullptr, nullptr,
      [this](const int i) -> std::string {
        const uint8_t slot = dailySlotAt(i);
        if (daily::collected(*state_, slot)) return ctr(CULT_TASK_COLLECTED);
        if (daily::done(*state_, slot)) return ctr(CULT_TASK_DONE);
        char v[24];
        snprintf(v, sizeof(v), ctr(CULT_TASK_PROGRESS_FORMAT), static_cast<unsigned>(daily::progress(*state_, slot)),
                 static_cast<unsigned>(daily::target(*state_, slot)));
        return v;
      },
      false, [this](const int i) -> bool { return daily::collected(*state_, dailySlotAt(i)); });

  int dy = detailTop;
  const uint8_t slot = dailySlotAt(selected_);
  if (notice_[0] != '\0') {
    dy = drawWrapped(UI_10_FONT_ID, x, dy, width, notice_, 3);
  } else if (slot < daily::kTaskSlots) {
    char what[96];
    formatDailyReward(daily::rewardFor(s, slot), what, sizeof(what));
    snprintf(line, sizeof(line), ctr(CULT_TASK_REWARD_FORMAT), what);
    dy = drawWrapped(UI_10_FONT_ID, x, dy, width, line, 2);
    if (daily::task(s, slot) == daily::Task::TwoSessions && !daily::done(s, slot)) {
      dy = drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_TASK_SESSIONS_HINT), 2);
    } else if (!daily::done(s, slot) && daily::canReroll(s, slot)) {
      dy = drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_TASK_REROLL_HINT), 2);
    }
  }
  dy += metrics.verticalSpacing;
  if (daily::currentStreak(s) == 1) {
    snprintf(line, sizeof(line), ctr(CULT_STREAK_ONE_FORMAT), static_cast<unsigned>(daily::longestStreak(s)));
  } else {
    snprintf(line, sizeof(line), ctr(CULT_STREAK_FORMAT), static_cast<unsigned>(daily::currentStreak(s)),
             static_cast<unsigned>(daily::longestStreak(s)));
  }
  dy = drawWrapped(SMALL_FONT_ID, x, dy, width, line, 2);
  drawWrapped(SMALL_FONT_ID, x, dy, width, ctr(CULT_TASK_NOTE), 2);
}

// --- Daos -------------------------------------------------------------------------------------

void CultivationActivity::renderDaos(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];

  snprintf(line, sizeof(line), ctr(CULT_DAOS_HEADER_FORMAT), static_cast<unsigned>(daos::foundCount(s)));
  if (daos::settled(s)) {
    const size_t used = strlen(line);
    snprintf(line + used, sizeof(line) - used, " %s", ctr(CULT_DAOS_SETTLED));
  }
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int detailTop = bottom - lineHeight * 8;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), detailTop - metrics.verticalSpacing - y}, daos::kPerLife,
      selected_,
      [this](const int i) -> std::string {
        const daos::Dao d = daoAtRow(*state_, i);
        if (d != daos::Dao::Count) return ui::daoName(d);
        return i < daos::slots(*state_) && !daos::settled(*state_) ? ctr(CULT_DAO_SLOT_OPEN)
                                                                   : ctr(CULT_DAO_SLOT_CLOSED);
      },
      nullptr, nullptr,
      [this](const int i) -> std::string {
        const daos::Dao d = daoAtRow(*state_, i);
        if (d == daos::Dao::Count) return "";
        char v[16];
        snprintf(v, sizeof(v), ctr(CULT_DAO_LEVEL_FORMAT), static_cast<unsigned>(daos::level(*state_, d)));
        return v;
      },
      false, [this](const int i) -> bool { return daoAtRow(*state_, i) == daos::Dao::Count; });

  int dy = detailTop;
  const daos::Dao d = daoAtRow(s, selected_);
  if (d == daos::Dao::Count) {
    const uint8_t realm = slotRealm(selected_);
    if (daos::settled(s)) {
      drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_DAO_SLOT_SETTLED), 3);
    } else if (selected_ < daos::slots(s)) {
      drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_DAO_SLOT_OPEN_HINT), 4);
    } else if (s.progress.realm >= realm) {
      drawWrapped(UI_10_FONT_ID, x, dy, width, ctr(CULT_DAO_SLOT_WAIT_HINT), 4);
    } else {
      snprintf(line, sizeof(line), ctr(CULT_DAO_SLOT_REALM_FORMAT), ui::realmName(realm));
      drawWrapped(UI_10_FONT_ID, x, dy, width, line, 3);
    }
    return;
  }

  effectLine(s, d, line, sizeof(line));
  dy = drawWrapped(UI_10_FONT_ID, x, dy, width, line, 3);
  const Element e = daos::elementOf(d);
  if (e != Element::Count) {
    snprintf(line, sizeof(line), ctr(CULT_DAO_TIER_FORMAT), ui::elementName(e),
             static_cast<unsigned>(daos::tierBoost(s, e)));
    dy = drawWrapped(UI_10_FONT_ID, x, dy, width, line, 3);
  }
  const uint8_t lv = daos::level(s, d);
  if (lv >= daos::kMaxLevel) {
    snprintf(line, sizeof(line), ctr(CULT_DAO_MAX_FORMAT), static_cast<unsigned>(lv));
  } else {
    snprintf(line, sizeof(line), ctr(CULT_DAO_NEXT_FORMAT), static_cast<unsigned>(lv), static_cast<unsigned>(lv + 1u),
             static_cast<unsigned long>(daos::nextLevelAt(s, d)), unitOf(d),
             static_cast<unsigned long>(daos::measure(s, d)));
  }
  dy = drawWrapped(UI_10_FONT_ID, x, dy + 4, width, line, 3);
  // As a key: level >= target + 2, at most 9.
  if (lv >= daos::kMaxLevel) {
    snprintf(line, sizeof(line), "%s", ctr(CULT_DAO_KEY_ALL));
  } else if (lv >= daos::keyLevelFor(2)) {
    snprintf(line, sizeof(line), ctr(CULT_DAO_KEY_FORMAT), ui::realmName(static_cast<uint8_t>(lv - 2u)));
  } else {
    snprintf(line, sizeof(line), "%s", ctr(CULT_DAO_KEY_NONE));
  }
  drawWrapped(UI_10_FONT_ID, x, dy + 4, width, line, 3);
}

// --- News ----------------------------------------------------------------------------------------

void CultivationActivity::writeDaoNews(const daos::News& n) {
  if (n.foundCount == 0 && n.levelCount == 0 && !n.settledNow) return;
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  // One line at a time (~200 B on the stack), labelled with today's season like
  // the other event lines, so the Chronicle files it under the right age.
  lifelog::Line line;
  line.kind = lifelog::LineKind::Event;
  line.season = lifelog::seasonOf(state_->progress.ageMonths);
  auto append = [&](const char* text) {
    lifelog::formatLabel(line.season, line.text, sizeof(line.text));
    const size_t used = strlen(line.text);
    snprintf(line.text + used, sizeof(line.text) - used, "%s", text);
    chronicle::appendLines(&line, 1);
  };
  char text[160];
  for (uint8_t i = 0; i < n.foundCount; ++i) append(foundLine(n.foundDaos[i]));
  for (uint8_t i = 0; i < n.levelCount; ++i) {
    snprintf(text, sizeof(text), ctr(CULT_DAO_LEVEL_UP_FORMAT), ui::daoName(n.levelDaos[i]),
             static_cast<unsigned>(n.levels[i]));
    append(text);
  }
  if (n.settledNow) append(ctr(CULT_DAO_SETTLED_LINE));
  chronicleExists_ = chronicle::exists();
  if (packs_) packs_->closeFile();
  LOG_INF("CULT", "Daos: %u found, %u level(s) up, settled=%d", static_cast<unsigned>(n.foundCount),
          static_cast<unsigned>(n.levelCount), n.settledNow ? 1 : 0);
}

int CultivationActivity::drawDaoNews(const int x, int y, const int width) const {
  const daos::News& n = daoNews_;
  char line[224];
  for (uint8_t i = 0; i < n.foundCount; ++i) {
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, foundLine(n.foundDaos[i]), 3);
  }
  for (uint8_t i = 0; i < n.levelCount; ++i) {
    snprintf(line, sizeof(line), ctr(CULT_DAO_LEVEL_UP_FORMAT), ui::daoName(n.levelDaos[i]),
             static_cast<unsigned>(n.levels[i]));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
  }
  if (n.settledNow) y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, ctr(CULT_DAO_SETTLED_LINE), 2);
  return y;
}

#endif  // CROSSINK_CULTIVATION
