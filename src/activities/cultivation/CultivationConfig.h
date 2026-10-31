#pragma once

// Path of Immortal Reading (personal fork).
//
// The whole game compiles only when the build sets -DCROSSINK_CULTIVATION=1
// (see [env:cultivation] in platformio.ini). Every .cpp in this folder wraps
// its body in `#if CROSSINK_CULTIVATION`, so the stock `default` build
// compiles these files to nothing and links no game code.
#ifndef CROSSINK_CULTIVATION
#define CROSSINK_CULTIVATION 0
#endif

// The game's own version, shown with CrossInk's version and the commit on the
// Cultivator screen and the sleep screen. Bump it with each milestone.
#define CULTIVATION_GAME_VERSION "M8"
