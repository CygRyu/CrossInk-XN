#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "CultivationUi.h"
#include "CultivatorSleepScreen.h"
#include "GameState.h"
#include "QiNumber.h"
#include "Simulation.h"
#include "StatsSource.h"
#include "fontIds.h"

namespace cultivation {
namespace sleepscreen {
namespace {

constexpr size_t kRumorBuffer = 256;  // a day index line + a rumor (under 200 bytes)

// Reads rumor.txt into `rumor`; returns the day it belongs to (kNoDay if unknown or missing).
uint32_t loadRumor(char* rumor, const size_t len) {
  rumor[0] = '\0';
  FsFile f;
  if (!Storage.openFileForRead("CULT", kRumorPath, f)) return StatsSnapshot::kNoDay;
  char buf[kRumorBuffer];
  const int got = f.read(buf, sizeof(buf) - 1);
  f.close();
  if (got <= 0) return StatsSnapshot::kNoDay;
  buf[got] = '\0';
  char* newline = strchr(buf, '\n');
  if (newline == nullptr) return StatsSnapshot::kNoDay;
  *newline = '\0';
  const uint32_t day = static_cast<uint32_t>(strtoul(buf, nullptr, 10));
  char* text = newline + 1;
  const size_t textLen = strcspn(text, "\r\n");
  text[textLen] = '\0';
  snprintf(rumor, len, "%s", text);
  return day;
}

// The taiji, drawn pixel by pixel (DOC: icons are simple 1-bit figures; this
// one needs no bitmap in flash). Black half on the right, with the two small
// circles and their dots, and an outline so the white half shows.
void drawTaiji(const GfxRenderer& renderer, const int cx, const int cy, const int r) {
  const int r2 = r * r;
  const int half = r / 2;
  const int dot = r / 7 > 1 ? r / 7 : 2;
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > r2) continue;
      const int upper = dx * dx + (dy + half) * (dy + half);  // the small circle above the centre
      const int lower = dx * dx + (dy - half) * (dy - half);  // the small circle below
      bool black = dx > 0;
      if (upper <= half * half) black = false;
      if (lower <= half * half) black = true;
      if (upper <= dot * dot) black = true;      // the dark dot in the light head
      if (lower <= dot * dot) black = false;     // the light dot in the dark head
      if (d2 > (r - 2) * (r - 2)) black = true;  // outline
      if (black) renderer.drawPixel(cx + dx, cy + dy);
    }
  }
}

int drawCenteredWrapped(const GfxRenderer& renderer, const int fontId, const int y, const int width, const char* text,
                        const int maxLines, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const auto lines = renderer.wrappedText(fontId, text, width, maxLines, style);
  int at = y;
  for (const auto& line : lines) {
    renderer.drawCenteredText(fontId, at, line.c_str(), true, style);
    at += renderer.getLineHeight(fontId);
  }
  return at;
}

}  // namespace

void storeRumor(const uint32_t day, const char* rumor) {
  if (rumor == nullptr || rumor[0] == '\0') return;
  char current[kRumorBuffer];
  if (loadRumor(current, sizeof(current)) == day && strcmp(current, rumor) == 0) return;
  Storage.mkdir(save::kDirectory);
  FsFile f;
  if (!Storage.openFileForWrite("CULT", kRumorPath, f)) {
    LOG_ERR("CULT", "Could not write %s", kRumorPath);
    return;
  }
  char line[24];
  const int n = snprintf(line, sizeof(line), "%lu\n", static_cast<unsigned long>(day));
  f.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
  f.write(reinterpret_cast<const uint8_t*>(rumor), strlen(rumor));
  f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  f.close();
}

bool render(GfxRenderer& renderer, const bool invert) {
  auto state = makeUniqueNoThrow<GameState>();
  if (!state) {
    LOG_ERR("CULT", "Out of memory for the Cultivator sleep screen");
    return false;
  }
  if (save::load(*state) != save::LoadResult::Loaded || !state->isCreated()) return false;
  const GameState& s = *state;
  const ProgressBlock& p = s.progress;

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  constexpr int kMargin = 48;
  const int textWidth = width - kMargin * 2;
  char line[224];

  renderer.clearScreen();

  // The figure and the character, a little above the middle.
  const int taijiRadius = 34;
  int y = height * 2 / 9;
  drawTaiji(renderer, width / 2, y, taijiRadius);
  y += taijiRadius + 36;

  char name[kNameMaxBytes + 1];
  memcpy(name, s.character.name.data(), kNameMaxBytes);
  name[kNameMaxBytes] = '\0';
  renderer.drawCenteredText(UI_12_FONT_ID, y, name, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 6;

  char titles[160];
  ui::formatTitles(s, titles, sizeof(titles));
  if (titles[0] != '\0') y = drawCenteredWrapped(renderer, SMALL_FONT_ID, y, textWidth, titles, 2) + 6;

  char stage[32];
  ui::stageName(s, stage, sizeof(stage));
  snprintf(line, sizeof(line), "%s, %s", ui::realmName(p.realm), stage);
  y = drawCenteredWrapped(renderer, UI_10_FONT_ID, y, textWidth, line, 2, EpdFontFamily::BOLD) + 14;

  // The Qi bar, as on the Cultivator screen, with the percentage under it.
  const QiNumber cost = stageCost(p.realm, p.stage);
  const uint32_t fill = QiNumber::fillPermille(p.qi, cost);
  const int barWidth = textWidth * 3 / 4;
  const int barX = (width - barWidth) / 2;
  constexpr int kBarHeight = 14;
  renderer.drawRect(barX, y, barWidth, kBarHeight);
  const int fillWidth = static_cast<int>(static_cast<int64_t>(barWidth - 4) * fill / 1000);
  if (fillWidth > 0) renderer.fillRect(barX + 2, y + 2, fillWidth, kBarHeight - 4);
  y += kBarHeight + 6;
  snprintf(line, sizeof(line), tr(STR_CULT_SLEEP_QI_FORMAT), static_cast<unsigned>(fill / 10));
  renderer.drawCenteredText(SMALL_FONT_ID, y, line);

  // The rumor near the bottom, above the version.
  const int footer = height - renderer.getLineHeight(SMALL_FONT_ID) - 24;
  char rumor[kRumorBuffer];
  const uint32_t rumorDay = loadRumor(rumor, sizeof(rumor));
  if (rumor[0] != '\0') {
    const uint32_t today = readLocalStats().today;
    const bool fresh = today != StatsSnapshot::kNoDay && today == rumorDay;
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, rumor, textWidth, 5);
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    int ry = footer - 20 - static_cast<int>(lines.size()) * lineHeight;
    renderer.drawCenteredText(SMALL_FONT_ID, ry - renderer.getLineHeight(SMALL_FONT_ID) - 4,
                              fresh ? tr(STR_CULT_SLEEP_RUMOR_TODAY) : tr(STR_CULT_SLEEP_RUMOR_LAST), true,
                              EpdFontFamily::BOLD);
    for (const auto& l : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, ry, l.c_str());
      ry += lineHeight;
    }
  }

  ui::versionLine(line, sizeof(line));
  const std::string version = renderer.truncatedText(SMALL_FONT_ID, line, textWidth);
  renderer.drawCenteredText(SMALL_FONT_ID, footer, version.c_str());

  if (invert) renderer.invertScreen();
  return true;
}

}  // namespace sleepscreen
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
