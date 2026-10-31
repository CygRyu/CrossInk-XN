#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CultivationUi.h"
#include "CultivatorSleepScreen.h"
#include "GameState.h"
#include "Simulation.h"
#include "fontIds.h"

namespace cultivation {
namespace sleepscreen {
namespace {

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
  int y = height * 3 / 10;
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
  drawCenteredWrapped(renderer, UI_10_FONT_ID, y, textWidth, line, 2, EpdFontFamily::BOLD);

  // Below: only the game's version (user, v0.1.9: no Qi bar, no rumor, no commit).
  const int footer = height - renderer.getLineHeight(SMALL_FONT_ID) - 24;
  ui::versionLine(line, sizeof(line));
  const std::string version = renderer.truncatedText(SMALL_FONT_ID, line, textWidth);
  renderer.drawCenteredText(SMALL_FONT_ID, footer, version.c_str());

  if (invert) renderer.invertScreen();
  return true;
}

}  // namespace sleepscreen
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
