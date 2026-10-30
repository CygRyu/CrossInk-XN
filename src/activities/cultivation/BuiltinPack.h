#pragma once

#include <cstddef>

namespace cultivation {

// The content pack compiled into the firmware (flash, not RAM). Source 0 of
// the pack index; SD packs are layered on top of it.
const char* builtinPackText();
size_t builtinPackSize();

}  // namespace cultivation
