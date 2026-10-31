#pragma once

#include <cstddef>
#include <cstdint>

#include "CultivationConfig.h"

class GfxRenderer;

// The "Cultivator" sleep screen (DOC: Entry points): the taiji, name, grade
// titles, realm and stage, and the game's version, drawn when CrossInk goes
// to sleep and Settings > Display > Sleep Screen > Wallpaper is "Cultivator".
// The user trimmed it in v0.1.9: no Qi bar, no rumor, no commit.
//
// It shows the character as of the last visit. It never loads the content
// packs or the grammar engine. Memory: the ~1 KB GameState on the heap for
// the duration of the draw.
namespace cultivation {
namespace sleepscreen {

// v0.1.8 kept the rumor here for the sleep screen; v0.1.9 removes the file.
constexpr char kOldRumorPath[] = "/.crosspoint/cultivation/rumor.txt";

#if CROSSINK_CULTIVATION
// Draws the screen black on white, then inverts it when `invert` is set (the
// dark look CrossInk's generated sleep screens use by default). Returns false,
// drawing nothing, when there is no character; the caller then falls back to
// CrossInk's default sleep screen. The caller pushes the buffer to the display.
bool render(GfxRenderer& renderer, bool invert);
// The taiji of radius r centred on (cx, cy), pixel by pixel, outlined so the
// white half shows. Also the player's marker on the exploration map (v0.1.10).
void drawTaiji(const GfxRenderer& renderer, int cx, int cy, int r);
#endif

}  // namespace sleepscreen
}  // namespace cultivation
