#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "GameText.h"

namespace cultivation {
namespace {

static const char* const kGameTexts[static_cast<int>(GameStr::Count)] = {
#define CULT_STR(id, text) text,
#include "GameStrings.inc"
#undef CULT_STR
};

}  // namespace

const char* gameText(const GameStr id) {
  const auto i = static_cast<uint16_t>(id);
  return i < static_cast<uint16_t>(GameStr::Count) ? kGameTexts[i] : "";
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
