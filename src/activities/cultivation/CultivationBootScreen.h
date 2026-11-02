#pragma once

class GfxRenderer;

// The game build's boot screen (v0.2.5, design agreed with the user in
// session 3): laid out like CrossInk's own (BootActivity's default logo), with
// a yin-yang drawn in code in place of the ink drop, "Immortal" in bold,
// CrossInk's "BOOTING", and the game and firmware versions at the bottom.
// Only replaces the default logo: a custom boot image chosen in CrossInk's
// settings still wins. No heap, no bitmap: the symbol is drawn pixel by pixel.
namespace cultivation {
namespace boot {

void drawBootScreen(const GfxRenderer& renderer);

}  // namespace boot
}  // namespace cultivation
