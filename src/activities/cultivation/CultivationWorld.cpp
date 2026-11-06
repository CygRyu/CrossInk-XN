#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "Chronicle.h"
#include "CultivationActivity.h"
#include "ExploreText.h"
#include "GameText.h"
#include "LifeLog.h"
#include "Rng.h"
#include "Sects.h"
#include "Simulation.h"

// The living world (v0.3.2; agreed with the user in sessions 14 and 15): the
// visit's news of the five sects, the rumor of the day about a live story, and
// the word a region condition shows. The rules are in Sects.{h,cpp}. Text only:
// no screen of its own (user).

using namespace cultivation;

namespace {

constexpr uint32_t kKindWorldText = 134;  // contentSeed kind: the living world's lines and rumors

const char* const kStoryKeys[static_cast<int>(sects::Story::Count)] = {
    "", "feud", "succession", "bandits", "tournament", "fair", "tide", "ruins", "blight"};
const char* const kConditionKeys[static_cast<int>(sects::Condition::Count)] = {"",     "war",  "camp",  "tournament",
                                                                               "fair", "tide", "ruins", "blight"};

const char* storyKey(const sects::Story s) {
  const int i = static_cast<int>(s);
  return i > 0 && i < static_cast<int>(sects::Story::Count) ? kStoryKeys[i] : nullptr;
}
const char* conditionKey(const sects::Condition c) {
  const int i = static_cast<int>(c);
  return i > 0 && i < static_cast<int>(sects::Condition::Count) ? kConditionKeys[i] : nullptr;
}

// Which event of the visit the Return summary shows: a condition that is on
// now, then one that ended, then a story's end, then a story's start (the
// latest of them). A condition that began and ended within the visit counts
// as a story's end.
int priority(const GameState& s, const sects::Event& e) {
  switch (e.kind) {
    case sects::EventKind::ConditionBegan: {
      uint8_t region = 0;
      const sects::Condition now = sects::condition(s, &region);
      return now == e.condition && region == e.region ? 4 : 1;
    }
    case sects::EventKind::ConditionEnded:
      return 2;
    case sects::EventKind::Ended:
      return 1;
    default:
      return 0;
  }
}

uint32_t eventSeed(const GameState& s, const sects::Event& e) {
  return contentSeed(worldSeedOf(s), kKindWorldText,
                     (static_cast<uint32_t>(e.year) << 8) | (static_cast<uint32_t>(e.sect) << 4) |
                         (static_cast<uint32_t>(e.kind) << 2));
}

}  // namespace

bool CultivationActivity::worldEventLine(const sects::Event& e, lifelog::Line& line) {
  if (!grammar_) return false;
  char key[48];
  switch (e.kind) {
    case sects::EventKind::Started:
    case sects::EventKind::Ended: {
      const char* k = storyKey(e.story);
      if (k == nullptr) return false;
      snprintf(key, sizeof(key), "world.line.%s.%s", k,
               e.kind == sects::EventKind::Started ? "start" : (e.good ? "good" : "bad"));
      break;
    }
    default: {
      const char* k = conditionKey(e.condition);
      if (k == nullptr) return false;
      snprintf(key, sizeof(key), "world.line.cond.%s.%s", k,
               e.kind == sects::EventKind::ConditionBegan ? "start" : "end");
      break;
    }
  }
  return exploretext::worldLine(line, *state_, key, eventSeed(*state_, e), e.region, e.sect, e.other, *grammar_);
}

// The one line the Return summary gets (user: at most one). It is the log's
// last line; writeLifeLog() leaves it out of the Chronicle's visit lines, and
// appendWorldChronicleLines() writes every event in order, this one included.
void CultivationActivity::appendWorldSummaryLine() {
  worldSummary_ = -1;
  if (!lifeLog_ || worldSteps_.count == 0) return;
  int best = -1;
  for (uint8_t i = 0; i < worldSteps_.count; ++i) {
    if (best < 0 || priority(*state_, worldSteps_.events[i]) >= priority(*state_, worldSteps_.events[best])) best = i;
  }
  if (best < 0 || lifeLog_->count >= lifelog::kMaxLines) return;
  lifelog::Line line;
  if (!worldEventLine(worldSteps_.events[best], line)) return;
  lifeLog_->lines[lifeLog_->count++] = line;
  worldSummary_ = static_cast<int8_t>(best);
}

// Every event of the visit, in order, straight into the Chronicle (after the
// visit's own lines, labelled with the current season like them).
void CultivationActivity::appendWorldChronicleLines() {
  if (!grammar_ || worldSteps_.count == 0) return;
  bool opened = chronicle::exists();
  for (uint8_t i = 0; i < worldSteps_.count; ++i) {
    lifelog::Line line;
    if (!worldEventLine(worldSteps_.events[i], line)) continue;
    if (!opened) {
      chronicle::writeOpening(*state_, grammar_.get());
      opened = true;
    }
    chronicle::appendLines(&line, 1);
  }
  if (worldSteps_.dropped > 0) {
    LOG_INF("CULT", "World: %u more events this visit not written", static_cast<unsigned>(worldSteps_.dropped));
  }
  chronicleExists_ = chronicle::exists();
  if (packs_) packs_->closeFile();
}

// One line straight into the Chronicle (the bandit camp you broke).
void CultivationActivity::worldChronicle(const char* listKey, const uint8_t region, const uint8_t sect,
                                         const int8_t sect2, const uint32_t extra) {
  if (!grammar_) return;
  lifelog::Line line;
  const uint32_t seed =
      contentSeed(worldSeedOf(*state_), kKindWorldText, mix32(state_->progress.gameReadingSeconds, extra));
  if (exploretext::worldLine(line, *state_, listKey, seed, region, sect, sect2, *grammar_)) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

// One day in three, the rumor of the day follows a live story (the region's
// condition first). False when there is none or the dice say no.
bool CultivationActivity::worldRumorOfTheDay(const uint32_t when) {
  if (!grammar_ || !state_) return false;
  if (Rng(contentSeed(worldSeedOf(*state_), kKindWorldText, mix32(when, 0x52554D52u))).below(3) != 0) return false;
  const int index = sects::rumorSect(*state_, when);
  if (index < 0) return false;
  const sects::Sect k = sects::sect(*state_, static_cast<uint8_t>(index));
  const char* story = storyKey(k.story);
  if (story == nullptr) return false;
  char key[48];
  snprintf(key, sizeof(key), "world.rumor.%s.%s", story, k.stage == 0 ? "early" : "late");
  char line[sizeof(rumor_)];
  if (!exploretext::worldRumor(*state_, key, contentSeed(worldSeedOf(*state_), kKindWorldText, mix32(when, 1u)),
                               k.region, static_cast<uint8_t>(index), k.other, *grammar_, line, sizeof(line))) {
    return false;
  }
  snprintf(rumor_, sizeof(rumor_), "%s", line);
  return true;
}

const char* CultivationActivity::conditionWord(const sects::Condition c) const {
  switch (c) {
    case sects::Condition::SectWar:
      return ctr(CULT_COND_WAR);
    case sects::Condition::BanditCamp:
      return ctr(CULT_COND_CAMP);
    case sects::Condition::Tournament:
      return ctr(CULT_COND_TOURNAMENT);
    case sects::Condition::TempleFair:
      return ctr(CULT_COND_FAIR);
    case sects::Condition::BeastTide:
      return ctr(CULT_COND_TIDE);
    case sects::Condition::Ruins:
      return ctr(CULT_COND_RUINS);
    case sects::Condition::Blight:
      return ctr(CULT_COND_BLIGHT);
    default:
      return nullptr;
  }
}

const char* CultivationActivity::conditionHere(const sects::Condition c) const {
  switch (c) {
    case sects::Condition::SectWar:
      return ctr(CULT_COND_WAR_HERE);
    case sects::Condition::BanditCamp:
      return ctr(CULT_COND_CAMP_HERE);
    case sects::Condition::Tournament:
      return ctr(CULT_COND_TOURNAMENT_HERE);
    case sects::Condition::TempleFair:
      return ctr(CULT_COND_FAIR_HERE);
    case sects::Condition::BeastTide:
      return ctr(CULT_COND_TIDE_HERE);
    case sects::Condition::Ruins:
      return ctr(CULT_COND_RUINS_HERE);
    case sects::Condition::Blight:
      return ctr(CULT_COND_BLIGHT_HERE);
    default:
      return nullptr;
  }
}

#endif  // CROSSINK_CULTIVATION
