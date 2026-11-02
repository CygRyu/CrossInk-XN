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

// The game's own version (user, v0.1.9): 0.1.x per milestone, 0.2.0 when
// exploration and alchemy land, 1.0 when the doc's whole Mortal Plane is
// playable. Shown on the Cultivator screen and the sleep screen. Milestone 8
// was v0.1.8.
#define CULTIVATION_GAME_VERSION "0.2.4"
