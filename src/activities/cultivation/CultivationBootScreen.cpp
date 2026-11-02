#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "AppVersion.h"
#include "CultivationBootScreen.h"
#include "GameText.h"
#include "fontIds.h"

namespace cultivation {
namespace boot {
namespace {

constexpr int kSize = 120;   // the same box as CrossInk's Logo120
constexpr int kOutline = 3;  // so the white half shows against the white page

// True where the yin-yang is black, for a point (x, y) from its centre
// (y grows downward). Black on the right, the white head at the top with a
// black eye, the black head at the bottom with a white eye.
bool blackAt(const int x, const int y, const int r) {
  const int d2 = x * x + y * y;
  if (d2 > r * r) return false;
  if (d2 >= (r - kOutline) * (r - kOutline)) return true;  // the outline
  const int half = r / 2;
  const int eye = r / 7;
  const int top = x * x + (y + half) * (y + half);
  const int bottom = x * x + (y - half) * (y - half);
  if (top <= eye * eye) return true;       // the eye in the white head
  if (bottom <= eye * eye) return false;   // the eye in the black head
  if (top <= half * half) return false;    // the white head
  if (bottom <= half * half) return true;  // the black head
  return x > 0;
}

void drawYinYang(const GfxRenderer& renderer, const int left, const int top) {
  const int r = kSize / 2;
  for (int y = -r; y < r; ++y) {
    for (int x = -r; x < r; ++x) {
      // Pixel centres, so the shape is symmetric.
      if (blackAt(x, y, r)) renderer.drawPixel(left + r + x, top + r + y, true);
    }
  }
}

}  // namespace

void drawBootScreen(const GfxRenderer& renderer) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  drawYinYang(renderer, (pageWidth - kSize) / 2, (pageHeight - kSize) / 2);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, ctr(CULT_BOOT_TITLE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  char version[64];
  snprintf(version, sizeof(version), ctr(CULT_BOOT_VERSION_FORMAT), CULTIVATION_GAME_VERSION, AppVersion::version());
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, version);
}

}  // namespace boot
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
