#pragma once

class GfxRenderer;

// The game build's boot screen (v0.2.5, design agreed with the user in
// session 3): laid out like CrossInk's own (BootActivity's default logo), with
// the game's logo in place of the ink drop (v0.2.7, the user's artwork; it was
// a yin-yang drawn in code), "Immortal" in bold, CrossInk's "BOOTING", and the
// game and firmware versions at the bottom. Only replaces the default logo: a
// custom boot image chosen in CrossInk's settings still wins. No heap: the
// logo is a 5.5 KB 1-bit bitmap in flash (CultivationLogo.h).
namespace cultivation {
namespace boot {

void drawBootScreen(const GfxRenderer& renderer);
// The logo (logo::kWidth x logo::kHeight, 240 x 185) with its top-left corner at (left, top).
// Also used by the Cultivator sleep screen.
void drawLogo(const GfxRenderer& renderer, int left, int top);

}  // namespace boot
}  // namespace cultivation
