#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Breakthrough.h"
#include "Chronicle.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "GameValues.h"
#include "Rng.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Tribulations and Heart Demons (v0.1.9): the Trial screen, one strike or
// scene at a time, between the Breakthrough screen's Attempt and the roll.
// The rules are in Trials.{h,cpp}; this file only shows them and writes
// their Chronicle lines. Text is generated in the input loop, under the
// render lock, never while drawing.

using namespace cultivation;

namespace {

constexpr uint32_t kKindTrialText = 72;  // contentSeed kinds 70-79: trials (71 is the scene roll)
constexpr uint32_t kKindTrialChronicle = 73;

static const char* const kOriginDemons[static_cast<int>(Origin::Count)] = {
    "demon.origin.farmer", "demon.origin.clan", "demon.origin.orphan", "demon.origin.servant",
    "demon.origin.herbalist"};

uint32_t trialSeed(const GameState& s, const uint32_t kind, const uint32_t extra) {
  const ProgressBlock& p = s.progress;
  return contentSeed(worldSeedOf(s), kind,
                     (static_cast<uint32_t>(p.realm) << 16) | (static_cast<uint32_t>(p.failures) << 8) | extra);
}

}  // namespace

// A line from `listKey` for the screen (no season label), seeded so that
// reopening the game shows the same strike text.
void CultivationActivity::trialFlavor(const char* listKey, const uint32_t seed, char* out, const size_t len) {
  out[0] = '\0';
  if (!grammar_) return;
  GrammarContext ctx;
  ctx.values = gameValues(*state_);
  ctx.stable = true;  // the same strike reads the same after reopening the game (found in the simulator)
  if (!grammar_->hasKey(listKey, ctx)) return;
  char tmpl[48];
  snprintf(tmpl, sizeof(tmpl), "{%s}", listKey);
  grammar_->beginScene();
  const char* text = grammar_->expand(tmpl, seed, ctx);
  if (grammar_->missing() == 0 && text != nullptr) snprintf(out, len, "%s", text);
  if (packs_) packs_->closeFile();
}

// The Breakthrough screen's Attempt, for a realm with a trial.
void CultivationActivity::startTrial() {
  {
    RenderLock lock(*this);
    if (!trials::begin(*state_)) return;  // no full bar, no key, or no HP for lightning
    const bool tribulation = trials::kindFor(state_->progress.realm + 1) == trials::Kind::Tribulation;
    recordOutcome(tribulation ? "trib.gather" : "demon.gather", trialSeed(*state_, kKindTrialChronicle, 0));
    persist();
    trialLast_[0] = '\0';
    refreshActions();
  }
  openTrial();
}

void CultivationActivity::openTrial() {
  {
    RenderLock lock(*this);
    prepareTrialStep();
    selected_ = 0;
    screen_ = Screen::Trial;
  }
  requestUpdate();
}

// The text and answers of the current strike or scene. Caller holds the render lock.
void CultivationActivity::prepareTrialStep() {
  const GameState& s = *state_;
  const uint8_t target = static_cast<uint8_t>(s.progress.realm + 1);
  const uint8_t index = trials::step(s);
  for (auto& n : trialNames_) n[0] = '\0';
  if (trials::kindFor(target) == trials::Kind::Tribulation) {
    strike_ = trials::currentStrike(s);
    trialChoices_ = strike_.optionCount;
    for (uint8_t i = 0; i < strike_.optionCount; ++i) {
      const auto& o = strike_.options[i];
      if (o.slot >= 0)
        scrollName(scrolls::get(s, static_cast<uint8_t>(o.slot)), trialNames_[i], sizeof(trialNames_[0]));
    }
    trialFlavor("trib.strike", trialSeed(s, kKindTrialText, index), trialText_, sizeof(trialText_));
  } else {
    // Scenes: the origin, then family, then the last failure (or the years).
    const char* list = "demon.family";
    if (index == 0) {
      const int origin = static_cast<int>(s.character.origin);
      list = kOriginDemons[origin < static_cast<int>(Origin::Count) ? origin : 0];
    } else if (index >= 2) {
      list = s.progress.failures > 0 ? "demon.failure" : "demon.years";
    }
    trialFlavor(list, trialSeed(s, kKindTrialText, index), trialText_, sizeof(trialText_));
    trialChoices_ = 1;
    const int burn = trials::burnableMindScroll(s);
    if (burn >= 0) {
      scrollName(scrolls::get(s, static_cast<uint8_t>(burn)), trialNames_[1], sizeof(trialNames_[0]));
      trialChoices_ = 2;
    }
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::answerTrial() {
  bool won = false;
  {
    RenderLock lock(*this);
    if (!trials::inTrial(*state_) || selected_ < 0 || selected_ >= trialChoices_) return;
    const uint8_t target = static_cast<uint8_t>(state_->progress.realm + 1);
    trials::Progress progress = trials::Progress::Continue;
    if (trials::kindFor(target) == trials::Kind::Tribulation) {
      const trials::StrikeResult r = trials::answerStrike(*state_, static_cast<uint8_t>(selected_));
      progress = r.progress;
      if (r.didBurn) {
        appendScrollLine("trib.burn", r.burned);
        snprintf(trialLast_, sizeof(trialLast_), tr(STR_CULT_TRIB_BURNED_FORMAT), trialNames_[selected_]);
      } else {
        snprintf(trialLast_, sizeof(trialLast_), tr(STR_CULT_TRIB_TOOK_FORMAT), static_cast<unsigned>(r.damage));
      }
      LOG_INF("CULT", "Strike answered: %u damage, HP %u", static_cast<unsigned>(r.damage),
              static_cast<unsigned>(state_->progress.hp));
    } else {
      const bool burn = selected_ == 1;
      const trials::SceneResult r = trials::answerScene(*state_, burn);
      progress = r.progress;
      if (r.didBurn) {
        appendScrollLine("trib.burn", r.burned);
        snprintf(trialLast_, sizeof(trialLast_), tr(STR_CULT_DEMON_BURNED_FORMAT), trialNames_[1]);
      } else {
        snprintf(trialLast_, sizeof(trialLast_),
                 r.passed ? tr(STR_CULT_DEMON_PASSED_FORMAT) : tr(STR_CULT_DEMON_FAILED_FORMAT),
                 static_cast<unsigned>(r.roll), static_cast<unsigned>(r.chance));
      }
    }
    if (progress == trials::Progress::Lost) {
      trialLost();
    } else if (progress == trials::Progress::Won) {
      const bool tribulation = trials::kindFor(target) == trials::Kind::Tribulation;
      recordOutcome(tribulation ? "trib.survive" : "demon.pass", trialSeed(*state_, kKindTrialChronicle, 1));
      if (tribulation) {
        snprintf(trialSummary_, sizeof(trialSummary_), tr(STR_CULT_TRIB_SURVIVED_FORMAT),
                 static_cast<unsigned>(state_->progress.hp), static_cast<unsigned>(state_->progress.hpMax));
      } else {
        snprintf(trialSummary_, sizeof(trialSummary_), "%s", tr(STR_CULT_DEMON_SURVIVED));
      }
      won = true;
    } else {
      persist();
      prepareTrialStep();
      selected_ = 0;
    }
  }
  if (won) {
    attemptBreakthrough();  // the usual roll, saved with the trial's end (takes the render lock itself)
  } else {
    requestUpdate();
  }
}

// A lost trial is a failed attempt. Caller holds the render lock.
void CultivationActivity::trialLost() {
  const uint8_t target = static_cast<uint8_t>(state_->progress.realm + 1);
  const bool tribulation = trials::kindFor(target) == trials::Kind::Tribulation;
  breakthrough::failAttempt(*state_);
  outcome_ = breakthrough::Result{};
  outcome_.toRealm = target;
  outcomeKind_ = OutcomeKind::TrialLost;
  recordOutcome(tribulation ? "trib.fail" : "demon.fail", trialSeed(*state_, kKindTrialChronicle, 2));
  persist();
  refreshActions();
  selected_ = 0;
  screen_ = Screen::Outcome;
  LOG_INF("CULT", "%s lost", tribulation ? "Tribulation" : "Heart Demon");
}

// --- Drawing ----------------------------------------------------------------------------

void CultivationActivity::renderTrial(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const uint8_t target = static_cast<uint8_t>(p.realm + 1);
  const bool tribulation = trials::kindFor(target) == trials::Kind::Tribulation;
  char line[224];

  if (tribulation) {
    snprintf(line, sizeof(line), tr(STR_CULT_TRIB_STRIKE_FORMAT), static_cast<unsigned>(strike_.index + 1),
             static_cast<unsigned>(strike_.count));
  } else {
    snprintf(line, sizeof(line), tr(STR_CULT_DEMON_SCENE_FORMAT), static_cast<unsigned>(trials::step(s) + 1),
             static_cast<unsigned>(trials::kHeartDemonScenes));
  }
  renderer.drawText(UI_12_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  if (trialLast_[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, trialLast_, 2) + metrics.verticalSpacing;
  if (trialText_[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, trialText_, 5) + metrics.verticalSpacing;

  if (tribulation) {
    // HP and the bolt to come.
    snprintf(line, sizeof(line), tr(STR_CULT_HP_FORMAT), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
    renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    const int barHeight = metrics.progressBarHeight;
    renderer.drawRect(x, y, width, barHeight);
    const int fill = p.hpMax > 0 ? static_cast<int>(static_cast<int64_t>(width - 4) * p.hp / p.hpMax) : 0;
    if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, barHeight - 4);
    y += barHeight + metrics.verticalSpacing;
    snprintf(line, sizeof(line), tr(STR_CULT_TRIB_BOLT_FORMAT), static_cast<unsigned>(strike_.base),
             static_cast<unsigned>(strike_.percent));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  } else {
    snprintf(line, sizeof(line), tr(STR_CULT_DEMON_TALLY_FORMAT), static_cast<unsigned>(trials::scenesWon(s)),
             static_cast<unsigned>(trials::scenesLost(s)));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
    drawWrapped(SMALL_FONT_ID, x, y + 2, width, tr(STR_CULT_DEMON_RULE), 2);
  }

  // The answers at the bottom.
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int listTop = bottom - rowHeight * trialChoices_;
  GUI.drawList(
      renderer, Rect{0, listTop, renderer.getScreenWidth(), rowHeight * trialChoices_}, trialChoices_, selected_,
      [this, tribulation](const int i) -> std::string {
        if (!tribulation) return i == 0 ? tr(STR_CULT_DEMON_FACE) : tr(STR_CULT_DEMON_BURN);
        switch (strike_.options[i].answer) {
          case trials::Answer::Endure:
            return tr(STR_CULT_TRIB_ENDURE);
          case trials::Answer::Turn:
            return tr(STR_CULT_TRIB_TURN);
          case trials::Answer::Burn:
            return tr(STR_CULT_TRIB_BURN);
        }
        return {};
      },
      [this, tribulation](const int i) -> std::string {
        char sub[160];
        if (!tribulation) {
          if (i == 0) {
            snprintf(sub, sizeof(sub), tr(STR_CULT_DEMON_FACE_SUB_FORMAT),
                     static_cast<unsigned>(trials::sceneChance(*state_)));
          } else {
            snprintf(sub, sizeof(sub), tr(STR_CULT_DEMON_BURN_SUB_FORMAT), trialNames_[1]);
          }
          return sub;
        }
        const auto& o = strike_.options[i];
        switch (o.answer) {
          case trials::Answer::Endure:
            snprintf(sub, sizeof(sub), tr(STR_CULT_TRIB_ENDURE_SUB_FORMAT), static_cast<unsigned>(o.damage));
            break;
          case trials::Answer::Turn:
            snprintf(sub, sizeof(sub), tr(STR_CULT_TRIB_TURN_SUB_FORMAT), static_cast<unsigned>(o.damage),
                     trialNames_[i]);
            break;
          case trials::Answer::Burn:
            snprintf(sub, sizeof(sub), tr(STR_CULT_TRIB_BURN_SUB_FORMAT), trialNames_[i]);
            break;
        }
        return sub;
      });
}

#endif  // CROSSINK_CULTIVATION
