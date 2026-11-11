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
    "",      "feud",   "succession",  "bandits", "tournament", "fair", "tide",
    "ruins", "blight", "inheritance", "demonic", "alliance",   "gate"};
const char* const kConditionKeys[static_cast<int>(sects::Condition::Count)] = {
    "", "war", "camp", "tournament", "fair", "tide", "ruins", "blight", "inheritance", "demonic"};

// The second sect a line names: a feud's or an alliance's other sect; for a
// demonic cultivator, the cult (exploretext names it from the story's sect).
int8_t secondOf(const sects::Event& e) {
  if (e.story == sects::Story::Demonic || e.condition == sects::Condition::Demonic) return sects::kCult;
  return e.other;
}

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

// v0.3.7: a story that ends through its region's condition (an inheritance taken, a war decided)
// ended twice in the news, once as the story and once as the condition, in the same visit.
// The condition's line says it; the story's is left out.
bool doubled(const sects::Steps& steps, const uint8_t i) {
  const sects::Event& e = steps.events[i];
  if (e.kind != sects::EventKind::Ended) return false;
  for (uint8_t j = 0; j < steps.count; ++j) {
    const sects::Event& c = steps.events[j];
    if (c.kind != sects::EventKind::ConditionEnded || c.region != e.region || c.sect != e.sect) continue;
    const int gap = static_cast<int>(c.year) - static_cast<int>(e.year);
    if (gap >= -1 && gap <= 1) return true;
  }
  return false;
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
    case sects::EventKind::WarDecided:
      snprintf(key, sizeof(key), "world.line.cond.war.decided");
      return exploretext::worldLine(line, *state_, key, eventSeed(*state_, e), e.region, e.sect, e.other, *grammar_);
    case sects::EventKind::CultBroken:
      snprintf(key, sizeof(key), "world.line.cond.demonic.broken");
      break;
    case sects::EventKind::CampBroken:
      snprintf(key, sizeof(key), "world.line.cond.camp.broken");
      break;
    case sects::EventKind::TideBroken:
      snprintf(key, sizeof(key), "world.line.cond.tide.broken");
      break;
    case sects::EventKind::YouTookInheritance:
      snprintf(key, sizeof(key), "world.line.cond.inheritance.won");
      break;
    case sects::EventKind::Dented:
      snprintf(key, sizeof(key), "world.line.dent");
      break;
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
  return exploretext::worldLine(line, *state_, key, eventSeed(*state_, e), e.region, e.sect, secondOf(e), *grammar_);
}

// v0.3.3: what a win did to the living world: a sentence for the result
// screen and the Chronicle lines (the decision first, then the story's end).
void CultivationActivity::worldInfluence(const sects::Influence& w, char* numbers, const size_t len) {
  if (w.count == 0) return;
  for (uint8_t i = 0; i < w.count; ++i) {
    const sects::Event& e = w.events[i];
    char sentence[160] = {};
    char name[exploretext::kNameSize] = {};
    switch (e.kind) {
      case sects::EventKind::WarDecided:
        if (grammar_ && exploretext::sectName(*state_, e.sect, *grammar_, name, sizeof(name))) {
          snprintf(sentence, sizeof(sentence), ctr(CULT_WORLD_WAR_DECIDED_FORMAT), name);
        }
        break;
      case sects::EventKind::CultBroken:
        snprintf(sentence, sizeof(sentence), "%s", ctr(CULT_WORLD_CULT_BROKEN));
        break;
      case sects::EventKind::CampBroken:
        snprintf(sentence, sizeof(sentence), "%s", ctr(CULT_CAMP_BROKEN));
        break;
      case sects::EventKind::TideBroken:
        snprintf(sentence, sizeof(sentence), "%s", ctr(CULT_WORLD_TIDE_BROKEN));
        break;
      case sects::EventKind::YouTookInheritance:
        snprintf(sentence, sizeof(sentence), "%s", ctr(CULT_WORLD_INHERITANCE_WON));
        break;
      case sects::EventKind::Dented:
        if (grammar_ && exploretext::sectName(*state_, e.sect, *grammar_, name, sizeof(name))) {
          snprintf(sentence, sizeof(sentence), ctr(CULT_WORLD_DENTED_FORMAT), name);
        }
        break;
      default:
        break;
    }
    if (sentence[0] >= 'a' && sentence[0] <= 'z') sentence[0] = static_cast<char>(sentence[0] - 'a' + 'A');
    if (sentence[0] != '\0' && numbers != nullptr) {
      const size_t used = strlen(numbers);
      if (used + 2 < len) snprintf(numbers + used, len - used, used > 0 ? " %s" : "%s", sentence);
    }
    lifelog::Line line;
    if (worldEventLine(e, line)) {
      if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
      chronicle::appendLines(&line, 1);
      chronicleExists_ = chronicle::exists();
    }
  }
  if (packs_) packs_->closeFile();
}

// The one line the Return summary gets (user: at most one). It is the log's
// last line; writeLifeLog() leaves it out of the Chronicle's visit lines, and
// appendWorldChronicleLines() writes every event in order, this one included.
void CultivationActivity::appendWorldSummaryLine() {
  worldSummary_ = -1;
  if (!lifeLog_ || worldSteps_.count == 0) return;
  int best = -1;
  for (uint8_t i = 0; i < worldSteps_.count; ++i) {
    if (doubled(worldSteps_, i)) continue;  // v0.3.7
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
    if (doubled(worldSteps_, i)) continue;  // v0.3.7: the condition's line tells it
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
                               k.region, static_cast<uint8_t>(index),
                               k.story == sects::Story::Demonic ? sects::kCult : k.other, *grammar_, line,
                               sizeof(line))) {
    return false;
  }
  snprintf(rumor_, sizeof(rumor_), "%s", line);
  return true;
}

// v0.3.3: one day in five, what a sect that thinks well or badly of you says
// (standing 3 or more either way). False when there is none or the dice say no.
bool CultivationActivity::standingRumorOfTheDay(const uint32_t when) {
  if (!grammar_ || !state_) return false;
  if (Rng(contentSeed(worldSeedOf(*state_), kKindWorldText, mix32(when, 0x594F5521u))).below(5) != 0) return false;
  const int index = sects::standingRumorSect(*state_, when);
  if (index < 0) return false;
  const sects::Sect k = sects::sect(*state_, static_cast<uint8_t>(index));
  char line[sizeof(rumor_)];
  if (!exploretext::worldRumor(*state_, k.standing > 0 ? "world.rumor.you.good" : "world.rumor.you.bad",
                               contentSeed(worldSeedOf(*state_), kKindWorldText, mix32(when, 2u)), k.home,
                               static_cast<uint8_t>(index), -1, *grammar_, line, sizeof(line))) {
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
    case sects::Condition::Inheritance:
      return ctr(CULT_COND_INHERITANCE);
    case sects::Condition::Demonic:
      return ctr(CULT_COND_DEMONIC);
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
    case sects::Condition::Inheritance:
      return ctr(CULT_COND_INHERITANCE_HERE);
    case sects::Condition::Demonic:
      return ctr(CULT_COND_DEMONIC_HERE);
    default:
      return nullptr;
  }
}

#endif  // CROSSINK_CULTIVATION
