#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "AppVersion.h"
#include "CultivationBootScreen.h"
#include "CultivationLogo.h"
#include "GameText.h"
#include "fontIds.h"

namespace cultivation {
namespace boot {
namespace {

// Drawn 1:1 from the bitmap; the shared helper below draws it for the sleep screen too.
}  // namespace

void drawLogo(const GfxRenderer& renderer, const int left, const int top) {
  for (int y = 0; y < logo::kHeight; ++y) {
    const uint8_t* row = logo::kBits + y * logo::kStride;
    for (int x = 0; x < logo::kWidth; ++x) {
      if ((row[x / 8] & (0x80u >> (x % 8))) != 0) renderer.drawPixel(left + x, top + y, true);
    }
  }
}

void drawBootScreen(const GfxRenderer& renderer) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  // v0.2.7: the user's logo in place of the yin-yang, a little above the middle.
  const int logoTop = pageHeight / 2 - logo::kHeight + 50;
  drawLogo(renderer, (pageWidth - logo::kWidth) / 2, logoTop);
  const int below = logoTop + logo::kHeight;
  renderer.drawCenteredText(UI_10_FONT_ID, below + 28, ctr(CULT_BOOT_TITLE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, below + 53, tr(STR_BOOTING));
  char version[64];
  snprintf(version, sizeof(version), ctr(CULT_BOOT_VERSION_FORMAT), CULTIVATION_GAME_VERSION, AppVersion::version());
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, version);
}

}  // namespace boot
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
