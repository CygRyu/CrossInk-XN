#pragma once

#include <cstddef>
#include <cstdint>

#include "CultivationConfig.h"

class GfxRenderer;

// The "Cultivator" sleep screen (DOC: Entry points): name, grade titles,
// realm and stage, the Qi bar and the rumor of the day, drawn when CrossInk
// goes to sleep and Settings > Display > Sleep Screen is "Cultivator".
//
// It shows the character as of the last visit: reading since then is only
// converted when the game is opened. The rumor comes from rumor.txt, written
// by the game when a visit generates the day's rumor, so the sleep screen
// never loads the content packs or the grammar engine.
//
// Memory: the ~1 KB GameState on the heap for the duration of the draw, and a
// 256-byte stack buffer for the rumor.
namespace cultivation {
namespace sleepscreen {

constexpr char kRumorPath[] = "/.crosspoint/cultivation/rumor.txt";

#if CROSSINK_CULTIVATION
// Line 1: the day index the rumor belongs to; line 2: the rumor. Rewritten
// only when the day or the text changes (about once a day).
void storeRumor(uint32_t day, const char* rumor);

// Draws the screen black on white, then inverts it when `invert` is set (the
// dark look CrossInk's generated sleep screens use by default). Returns false,
// drawing nothing, when there is no character; the caller then falls back to
// CrossInk's default sleep screen. The caller pushes the buffer to the display.
bool render(GfxRenderer& renderer, bool invert);
#endif

}  // namespace sleepscreen
}  // namespace cultivation
