#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Chronicle.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "Daily.h"
#include "ExploreText.h"
#include "GameText.h"
#include "LifeLog.h"
#include "Rivals.h"
#include "Rng.h"
#include "Simulation.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Rivals (v0.2.8, milestone 9b; agreed with the user in session 11, HANDOFF
// 14u): a menu of three, then the rival's card, the realm's ranking and the
// nemeses. The rules are in Rivals.{h,cpp}. Names come from the packs once, on
// opening (RivalsView, ~2.7 KB: the rival, 20 seats, three nemeses and their
// kinds, the open regions), and are freed when the screens are left. The
// rival's news on a visit and the nemesis rumor are written here too.

using namespace cultivation;

namespace {

constexpr uint32_t kKindRivalText = 128;  // contentSeed kind: rival, ladder and nemesis lines (122-127: Rivals.cpp)

const char* text(const GameStr id) { return gameText(id); }

static const GameStr kDangerNames[static_cast<int>(explore::Danger::Count)] = {
    GameStr::CULT_DANGER_TRIVIAL,   GameStr::CULT_DANGER_WEAK,    GameStr::CULT_DANGER_DANGEROUS,
    GameStr::CULT_DANGER_HAZARDOUS, GameStr::CULT_DANGER_EXTREME, GameStr::CULT_DANGER_IMPOSSIBLE};

const char* dangerWord(const explore::Danger d) {
  const int i = static_cast<int>(d);
  return text(kDangerNames[i < static_cast<int>(explore::Danger::Count) ? i : 0]);
}

void capitalize(char* s) {
  if (s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
}

static const char* const kSayLists[static_cast<int>(rivals::Temper::Count)] = {
    "rival.say.idle", "rival.say.steady", "rival.say.driven", "rival.say.erratic", "rival.say.late"};
static const char* const kReactLists[static_cast<int>(rivals::Temper::Count)] = {
    "rival.react.idle", "rival.react.steady", "rival.react.driven", "rival.react.erratic", "rival.react.late"};

// Menu rows of the Rivals screen.
enum MenuRow : int { kRowRival = 0, kRowLadder, kRowNemeses, kMenuRows };

}  // namespace

namespace cultivation {
namespace ui {

const char* traitName(const rivals::Trait t) {
  using rivals::Trait;
  switch (t) {
    case Trait::Overcoming:
      return ctr(CULT_TRAIT_OVERCOMING);
    case Trait::IronHide:
      return ctr(CULT_TRAIT_IRON_HIDE);
    case Trait::Hunter:
      return ctr(CULT_TRAIT_HUNTER);
    case Trait::Venomous:
      return ctr(CULT_TRAIT_VENOMOUS);
    case Trait::Ambusher:
      return ctr(CULT_TRAIT_AMBUSHER);
    case Trait::QuickToHeal:
      return ctr(CULT_TRAIT_QUICK_TO_HEAL);
    case Trait::PackLeader:
      return ctr(CULT_TRAIT_PACK_LEADER);
    case Trait::Overcome:
      return ctr(CULT_TRAIT_OVERCOME);
    case Trait::Proud:
      return ctr(CULT_TRAIT_PROUD);
    case Trait::Coward:
      return ctr(CULT_TRAIT_COWARD);
    case Trait::Slow:
      return ctr(CULT_TRAIT_SLOW);
    case Trait::Greedy:
      return ctr(CULT_TRAIT_GREEDY);
    case Trait::FearsDawn:
      return ctr(CULT_TRAIT_FEARS_DAWN);
    case Trait::OldWound:
      return ctr(CULT_TRAIT_OLD_WOUND);
    default:
      return "";
  }
}

const char* traitEffect(const rivals::Trait t) {
  using rivals::Trait;
  switch (t) {
    case Trait::Overcoming:
      return ctr(CULT_TRAIT_OVERCOMING_EFFECT);
    case Trait::IronHide:
      return ctr(CULT_TRAIT_IRON_HIDE_EFFECT);
    case Trait::Hunter:
      return ctr(CULT_TRAIT_HUNTER_EFFECT);
    case Trait::Venomous:
      return ctr(CULT_TRAIT_VENOMOUS_EFFECT);
    case Trait::Ambusher:
      return ctr(CULT_TRAIT_AMBUSHER_EFFECT);
    case Trait::QuickToHeal:
      return ctr(CULT_TRAIT_QUICK_TO_HEAL_EFFECT);
    case Trait::PackLeader:
      return ctr(CULT_TRAIT_PACK_LEADER_EFFECT);
    case Trait::Overcome:
      return ctr(CULT_TRAIT_OVERCOME_EFFECT);
    case Trait::Proud:
      return ctr(CULT_TRAIT_PROUD_EFFECT);
    case Trait::Coward:
      return ctr(CULT_TRAIT_COWARD_EFFECT);
    case Trait::Slow:
      return ctr(CULT_TRAIT_SLOW_EFFECT);
    case Trait::Greedy:
      return ctr(CULT_TRAIT_GREEDY_EFFECT);
    case Trait::FearsDawn:
      return ctr(CULT_TRAIT_FEARS_DAWN_EFFECT);
    case Trait::OldWound:
      return ctr(CULT_TRAIT_OLD_WOUND_EFFECT);
    default:
      return "";
  }
}

const char* traitRumor(const rivals::Trait t) {
  using rivals::Trait;
  switch (t) {
    case Trait::Overcoming:
      return ctr(CULT_TRAIT_OVERCOMING_RUMOR);
    case Trait::IronHide:
      return ctr(CULT_TRAIT_IRON_HIDE_RUMOR);
    case Trait::Hunter:
      return ctr(CULT_TRAIT_HUNTER_RUMOR);
    case Trait::Venomous:
      return ctr(CULT_TRAIT_VENOMOUS_RUMOR);
    case Trait::Ambusher:
      return ctr(CULT_TRAIT_AMBUSHER_RUMOR);
    case Trait::QuickToHeal:
      return ctr(CULT_TRAIT_QUICK_TO_HEAL_RUMOR);
    case Trait::PackLeader:
      return ctr(CULT_TRAIT_PACK_LEADER_RUMOR);
    case Trait::Overcome:
      return ctr(CULT_TRAIT_OVERCOME_RUMOR);
    case Trait::Proud:
      return ctr(CULT_TRAIT_PROUD_RUMOR);
    case Trait::Coward:
      return ctr(CULT_TRAIT_COWARD_RUMOR);
    case Trait::Slow:
      return ctr(CULT_TRAIT_SLOW_RUMOR);
    case Trait::Greedy:
      return ctr(CULT_TRAIT_GREEDY_RUMOR);
    case Trait::FearsDawn:
      return ctr(CULT_TRAIT_FEARS_DAWN_RUMOR);
    case Trait::OldWound:
      return ctr(CULT_TRAIT_OLD_WOUND_RUMOR);
    default:
      return "";
  }
}

void knownTraits(const rivals::Nemesis& n, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if ((n.revealed & (1u << i)) == 0) continue;
    const size_t used = strlen(out);
    snprintf(out + used, len - used, "%s%s", used > 0 ? ", " : "", traitName(n.traits[i]));
  }
}

namespace {
void traitShort(const rivals::Nemesis& n, const rivals::Trait t, char* out, const size_t len) {
  using rivals::Trait;
  switch (t) {
    case Trait::Overcoming:
    case Trait::IronHide:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_PLUS3));
      return;
    case Trait::PackLeader:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_PLUS2));
      return;
    case Trait::Hunter:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_HUNTER));
      return;
    case Trait::Venomous:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_VENOMOUS));
      return;
    case Trait::Ambusher:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_AMBUSHER));
      return;
    case Trait::QuickToHeal:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_QUICK_TO_HEAL));
      return;
    case Trait::Overcome:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_MINUS3));
      return;
    case Trait::Proud:
      snprintf(out, len, "%s", n.wonLast ? ctr(CULT_TRAIT_SHORT_PROUD_NOW) : ctr(CULT_TRAIT_SHORT_PROUD));
      return;
    case Trait::Coward:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_COWARD));
      return;
    case Trait::Slow:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_SLOW));
      return;
    case Trait::Greedy:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_GREEDY));
      return;
    case Trait::FearsDawn:
      snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_FEARS_DAWN));
      return;
    case Trait::OldWound:
      if (n.wins == 0) {
        snprintf(out, len, "%s", ctr(CULT_TRAIT_SHORT_OLD_WOUND_NONE));
      } else {
        snprintf(out, len, ctr(CULT_TRAIT_SHORT_OLD_WOUND_FORMAT), static_cast<unsigned>(n.wins < 4 ? n.wins : 4));
      }
      return;
    default:
      if (len > 0) out[0] = '\0';
      return;
  }
}
}  // namespace

void knownTraitsWithEffects(const rivals::Nemesis& n, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  for (uint8_t i = 0; i < n.traitCount; ++i) {
    if ((n.revealed & (1u << i)) == 0) continue;
    char effect[48];
    traitShort(n, n.traits[i], effect, sizeof(effect));
    const size_t used = strlen(out);
    snprintf(out + used, len - used, "%s%s (%s)", used > 0 ? ", " : "", traitName(n.traits[i]), effect);
  }
}

void woundLine(const GameState& s, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  const explore::Wound w = explore::woundOf(s);
  if (w == explore::Wound::None) return;
  const bool badly = w == explore::Wound::Badly;
  snprintf(out, len, ctr(CULT_WOUND_INFO_FORMAT), badly ? ctr(CULT_WOUND_BADLY) : ctr(CULT_WOUND_WOUNDED),
           static_cast<unsigned>(badly ? explore::kBadlyWoundedPercent : explore::kWoundedPercent),
           static_cast<unsigned>(explore::woundLevels(w)), static_cast<unsigned>(explore::woundSlipCut(w)));
}

void ladderTitle(const GameState& s, char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  const uint8_t rank = rivals::titleRank(s);
  if (rank == 0) return;
  const char* fmt = rank == 1   ? ctr(CULT_LADDER_TITLE_1_FORMAT)
                    : rank == 2 ? ctr(CULT_LADDER_TITLE_2_FORMAT)
                                : ctr(CULT_LADDER_TITLE_3_FORMAT);
  snprintf(out, len, fmt, realmName(s.progress.realm));
}

}  // namespace ui
}  // namespace cultivation

// --- Opening and closing ----------------------------------------------------------------------

void CultivationActivity::openRivals() {
  auto view = makeUniqueNoThrow<RivalsView>();
  if (!view) {
    LOG_ERR("CULT", "Out of memory for the Rivals screens (%u bytes)", static_cast<unsigned>(sizeof(RivalsView)));
    return;
  }
  {
    RenderLock lock(*this);
    rivals_ = std::move(view);
    rivalsReturn_ = selected_;
    selected_ = 0;
    notice_[0] = '\0';
    loadRivalNames();
    screen_ = Screen::Rivals;
  }
  requestUpdate();
}

void CultivationActivity::closeRivals() {
  RenderLock lock(*this);
  rivals_.reset();
  notice_[0] = '\0';
  refreshActions();
  selected_ = rivalsReturn_ < actionCount_ ? rivalsReturn_ : 0;
  screen_ = Screen::Cultivator;
  requestUpdate();
}

// Every name the screens show, once: the packs are read here, never while drawing.
void CultivationActivity::loadRivalNames() {
  if (!rivals_ || !grammar_) return;
  RivalsView& v = *rivals_;
  const GameState& s = *state_;
  exploretext::rivalName(s, *grammar_, v.rival, sizeof(v.rival), v.sect, sizeof(v.sect));
  const rivals::Rival r = rivals::rival(s);
  v.say[0] = '\0';
  if (r.exists && r.known) {
    const uint32_t day = daily::live(s) ? daily::today(s) : s.progress.gameReadingSeconds / 3600u;
    exploretext::personFlavor(s, kSayLists[static_cast<int>(r.temper)],
                              contentSeed(worldSeedOf(s), kKindRivalText, day), nullptr, v.rival, v.sect, *grammar_,
                              v.say, sizeof(v.say));
  }
  if (rivals::ladderOpen(s)) {
    // Twenty people need twenty names: a seat whose name (before "of the ...")
    // is taken by a seat above tries the next seed. Stable: the same every time.
    auto baseLength = [](const char* name) -> size_t {
      const char* cut = strstr(name, " of the ");
      return cut != nullptr ? static_cast<size_t>(cut - name) : strlen(name);
    };
    for (uint8_t seat = 1; seat <= rivals::kLadderSize; ++seat) {
      char* out = v.seats[seat - 1];
      for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        const uint32_t seed = attempt == 0 ? rivals::seatSeed(s, seat) : mix32(rivals::seatSeed(s, seat), attempt);
        exploretext::seatName(s, seed, *grammar_, out, RivalsView::kNameSize);
        const size_t len = baseLength(out);
        bool taken = false;
        for (uint8_t k = 0; k + 1 < seat && !taken; ++k) {
          taken = baseLength(v.seats[k]) == len && strncmp(v.seats[k], out, len) == 0;
        }
        if (!taken) break;
      }
      capitalize(out);  // "The one-armed swordsman Ren ..." at the start of a row
    }
  }
  for (uint8_t i = 0; i < rivals::kNemesisMax; ++i) {
    exploretext::nemesisName(s, i, *grammar_, v.nemeses[i], sizeof(v.nemeses[i]));
    exploretext::nemesisKind(s, i, *grammar_, v.kinds[i], sizeof(v.kinds[i]));
  }
  const uint8_t open = explore::unlockedCount(s);
  for (uint8_t region = 0; region < open; ++region) {
    exploretext::regionName(s, region, *grammar_, v.regions[region], sizeof(v.regions[region]));
  }
  if (packs_) packs_->closeFile();
}

uint8_t CultivationActivity::ladderActions(LadderRow* out) const {
  uint8_t n = 0;
  if (!rivals::ladderOpen(*state_)) return 0;
  out[n++] = LadderRow::Challenge;
  if (rivals::defenceBout(*state_).block == rivals::Block::None) {
    out[n++] = LadderRow::Accept;
    out[n++] = LadderRow::Decline;
  }
  return n;
}

void CultivationActivity::rivalLabels(const char*& confirmLabel) const {
  confirmLabel = "";
  if (screen_ == Screen::Rivals) {
    confirmLabel = ctr(CULT_OPEN);
  } else if (screen_ == Screen::Ladder) {
    LadderRow rows[3];
    const uint8_t n = ladderActions(rows);
    if (selected_ < n) {
      if (rows[selected_] == LadderRow::Decline) {
        confirmLabel = ctr(CULT_LADDER_DECLINE);
      } else if (rows[selected_] == LadderRow::Accept || rivals::challengeBout(*state_).block == rivals::Block::None) {
        confirmLabel = ctr(CULT_LADDER_FIGHT);
      }
    }
  }
}

// One Chronicle line about the rival, a ladder bout or a nemesis.
void CultivationActivity::rivalChronicle(const char* listKey, const char* region, const char* name, const char* what,
                                         const uint32_t extra, const char* note) {
  if (!grammar_) return;
  lifelog::Line line;
  const uint32_t seed =
      contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(state_->progress.gameReadingSeconds, extra));
  if (exploretext::personLine(line, *state_, listKey, seed, region, name, what, *grammar_)) {
    if (note != nullptr && note[0] != '\0') {  // v0.2.9: what you learnt goes into the Chronicle too
      const size_t used = strlen(line.text);
      if (used + 1 < sizeof(line.text)) snprintf(line.text + used, sizeof(line.text) - used, " %s", note);
    }
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::ladderChoose() {
  LadderRow rows[3];
  const uint8_t n = ladderActions(rows);
  if (selected_ >= n) return;
  RenderLock lock(*this);
  const RivalsView* v = rivals_.get();
  auto who = [&](const rivals::Rung& r) -> const char* {
    if (r.rival) return v ? v->rival : "";
    return v && r.seat >= 1 && r.seat <= rivals::kLadderSize ? v->seats[r.seat - 1] : "";
  };
  char what[24];
  rivals::BoutResult res;
  const LadderRow row = rows[selected_];
  if (row == LadderRow::Decline) {
    if (rivals::declineDefence(*state_)) {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_LADDER_DECLINED));
      persist();
    }
    selected_ = 0;
    requestUpdate();
    return;
  }
  const rivals::Bout bout = row == LadderRow::Accept ? rivals::defenceBout(*state_) : rivals::challengeBout(*state_);
  res = row == LadderRow::Accept ? rivals::acceptDefence(*state_) : rivals::challenge(*state_);
  if (!res.done) {  // blocked: the row's subtitle says why
    requestUpdate();
    return;
  }
  const char* name = who(res.who);
  if (row == LadderRow::Challenge) {
    if (res.won) {
      snprintf(notice_, sizeof(notice_), ctr(CULT_LADDER_WON_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(bout.chance), static_cast<unsigned>(res.rankAfter),
               static_cast<unsigned long>(res.stones));
      snprintf(what, sizeof(what), ctr(CULT_LADDER_RANK_WHAT_FORMAT), static_cast<unsigned>(res.rankAfter));
      rivalChronicle("ladder.line.won", nullptr, name, what, 1);
    } else {
      snprintf(notice_, sizeof(notice_), ctr(CULT_LADDER_LOST_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(bout.chance), static_cast<unsigned>(res.hpLost));
      snprintf(what, sizeof(what), ctr(CULT_LADDER_RANK_WHAT_FORMAT), static_cast<unsigned>(bout.rank));
      rivalChronicle("ladder.line.lost", nullptr, name, what, 2);
    }
  } else {
    if (res.won) {
      snprintf(notice_, sizeof(notice_), ctr(CULT_LADDER_DEFENDED_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(bout.chance), static_cast<unsigned>(res.rankAfter),
               static_cast<unsigned long>(res.stones));
      snprintf(what, sizeof(what), ctr(CULT_LADDER_RANK_WHAT_FORMAT), static_cast<unsigned>(res.rankAfter));
      rivalChronicle("ladder.line.defended", nullptr, name, what, 3);
    } else {
      snprintf(notice_, sizeof(notice_), ctr(CULT_LADDER_FELL_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(bout.chance), static_cast<unsigned>(res.rankAfter),
               static_cast<unsigned>(res.hpLost));
      snprintf(what, sizeof(what), ctr(CULT_LADDER_RANK_WHAT_FORMAT), static_cast<unsigned>(res.rankAfter));
      rivalChronicle("ladder.line.fell", nullptr, name, what, 4);
    }
  }
  // Beating the rival on the ranking stings them as on the road.
  if (res.rival && res.won && grammar_ && v && rivals::rival(*state_).known) {
    const rivals::Rival r = rivals::rival(*state_);
    rivalChronicle(kReactLists[static_cast<int>(r.temper)], nullptr, v->rival, v->sect, 5);
  }
  persist();
  LOG_INF("CULT", "Ladder %s vs rank %u: roll %u, %s, rank %u -> %u",
          row == LadderRow::Accept ? "defence" : "challenge", static_cast<unsigned>(bout.rank),
          static_cast<unsigned>(res.roll), res.won ? "won" : "lost", static_cast<unsigned>(res.rankBefore),
          static_cast<unsigned>(res.rankAfter));
  selected_ = 0;
  requestUpdate();
}

bool CultivationActivity::handleRivals(const bool back, const bool confirm, const bool up, const bool down) {
  if (screen_ != Screen::Rivals && screen_ != Screen::RivalCard && screen_ != Screen::Ladder &&
      screen_ != Screen::Nemeses) {
    return false;
  }
  if (!rivals_) {
    closeRivals();
    return true;
  }
  RivalsView& v = *rivals_;
  if (screen_ == Screen::Rivals) {
    if (back) {
      closeRivals();
    } else if (confirm) {
      RenderLock lock(*this);
      v.menuSelected = selected_;
      notice_[0] = '\0';
      screen_ = selected_ == kRowRival ? Screen::RivalCard : selected_ == kRowLadder ? Screen::Ladder : Screen::Nemeses;
      selected_ = 0;
      requestUpdate();
    } else if (up || down) {
      handleList(kMenuRows, up, down);
    }
    return true;
  }
  if (back) {
    RenderLock lock(*this);
    notice_[0] = '\0';
    selected_ = v.menuSelected;
    screen_ = Screen::Rivals;
    requestUpdate();
    return true;
  }
  if (screen_ == Screen::Ladder) {
    LadderRow rows[3];
    const int count = ladderActions(rows) + (rivals::ladderOpen(*state_) ? rivals::kLadderSize : 0);
    if (confirm) {
      ladderChoose();
    } else if (up || down) {
      notice_[0] = '\0';
      handleList(count, up, down);
    }
  } else if (screen_ == Screen::Nemeses && (up || down)) {
    handleList(rivals::nemesisCount(*state_), up, down);
  }
  return true;
}

// --- Drawing --------------------------------------------------------------------------------------

void CultivationActivity::renderRivals(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int noteTop = bottom - renderer.getLineHeight(SMALL_FONT_ID) * 3;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), noteTop - metrics.verticalSpacing - y}, kMenuRows, selected_,
      [this](const int i) -> std::string {
        char t[64];
        if (i == kRowRival) return ctr(CULT_RIVALS_ROW_RIVAL);
        if (i == kRowLadder) {
          snprintf(t, sizeof(t), ctr(CULT_RIVALS_ROW_LADDER_FORMAT), ui::realmName(state_->progress.realm));
          return t;
        }
        return ctr(CULT_RIVALS_ROW_NEMESES);
      },
      [this](const int i) -> std::string {
        const GameState& s = *state_;
        char t[96];
        if (i == kRowRival) {
          const rivals::Rival r = rivals::rival(s);
          if (!r.known || !rivals_) return ctr(CULT_RIVALS_UNMET);
          return rivals_->rival;
        }
        if (i == kRowLadder) {
          if (rivals::defenceBout(s).block == rivals::Block::None) return ctr(CULT_RIVALS_CHALLENGED_SUB);
          const uint8_t rank = rivals::playerRank(s);
          if (rank == 0) return ctr(CULT_RIVALS_UNRANKED);
          snprintf(t, sizeof(t), ctr(CULT_RIVALS_RANK_FORMAT), static_cast<unsigned>(rank));
          return t;
        }
        const uint8_t n = rivals::nemesisCount(s);
        if (n == 0) return ctr(CULT_RIVALS_NEMESES_NONE);
        bool hunted = false;
        for (uint8_t k = 0; k < rivals::kNemesisMax; ++k) {
          rivals::Nemesis nm;
          if (rivals::nemesis(s, k, nm) && rivals::hunting(s, nm)) hunted = true;
        }
        snprintf(t, sizeof(t), hunted ? ctr(CULT_RIVALS_NEMESES_HUNTING_FORMAT) : ctr(CULT_RIVALS_NEMESES_FORMAT),
                 static_cast<unsigned>(n));
        return t;
      });
  drawWrapped(SMALL_FONT_ID, x, noteTop, width, ctr(CULT_RIVALS_NOTE), 3);
}

void CultivationActivity::renderRivalCard(const int x, int y, const int width, const int bottom) {
  (void)bottom;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const rivals::Rival r = rivals::rival(s);
  char line[224];
  if (!r.exists || !r.known || !rivals_) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RIVAL_UNMET), 8);
    return;
  }
  const RivalsView& v = *rivals_;
  const auto header = renderer.wrappedText(UI_12_FONT_ID, v.rival, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : header) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  snprintf(line, sizeof(line), ctr(CULT_RIVAL_OF_FORMAT), v.sect);
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;

  // Where they are, against you.
  char stage[32];
  ui::stageNameOf(r.realm, r.stage, stage, sizeof(stage));
  const int gap = rivals::stageGap(s);
  const unsigned g = static_cast<unsigned>(gap < 0 ? -gap : gap);
  const char* fmt = gap == 0    ? ctr(CULT_RIVAL_LEVEL_FORMAT)
                    : gap == 1  ? ctr(CULT_RIVAL_AHEAD_ONE_FORMAT)
                    : gap > 0   ? ctr(CULT_RIVAL_AHEAD_FORMAT)
                    : gap == -1 ? ctr(CULT_RIVAL_BEHIND_ONE_FORMAT)
                                : ctr(CULT_RIVAL_BEHIND_FORMAT);
  snprintf(line, sizeof(line), fmt, ui::realmName(r.realm), stage, g);
  const auto pos = renderer.wrappedText(UI_10_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : pos) {
    renderer.drawText(UI_10_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y += metrics.verticalSpacing;
  if (!daily::live(s)) y = drawWrapped(SMALL_FONT_ID, x, y, width, ctr(CULT_RIVAL_NO_DATE), 2) + 4;
  if (r.hiatus) y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RIVAL_SECLUSION), 2) + 4;
  if (v.say[0] != '\0') {
    snprintf(line, sizeof(line), ctr(CULT_RIVAL_SAY_FORMAT), v.say);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 5) + metrics.verticalSpacing;
  }
  const int d = explore::playerLevel(s, true) - rivals::rivalLevel(s) -
                explore::woundLevels(explore::woundOf(s));  // v0.2.9: as in a spar on the road
  snprintf(line, sizeof(line), ctr(CULT_RIVAL_BOUT_FORMAT), dangerWord(explore::dangerFor(d)),
           static_cast<unsigned>(explore::winChance(d)));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  snprintf(line, sizeof(line), ctr(CULT_RIVAL_RECORD_FORMAT), static_cast<unsigned>(r.wins),
           static_cast<unsigned>(r.losses));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  const uint8_t rank = rivals::rivalRank(s);
  if (rank != 0) {
    snprintf(line, sizeof(line), ctr(CULT_RIVAL_ON_LADDER_FORMAT), static_cast<unsigned>(rank));
    drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
}

void CultivationActivity::renderLadder(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];
  if (!rivals::ladderOpen(s)) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_LADDER_NO_DATE), 4);
    return;
  }
  const uint8_t rank = rivals::playerRank(s);
  if (rank == 0) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_LADDER_UNRANKED), 2);
  } else {
    snprintf(line, sizeof(line), ctr(CULT_LADDER_YOU_FORMAT), static_cast<unsigned>(rank),
             static_cast<unsigned>(s.rivals[22] != 0 ? s.rivals[22] : rank));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
  if (explore::woundOf(s) != explore::Wound::None) {  // v0.2.9: why the chances are lower
    ui::woundLine(s, line, sizeof(line));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
  }
  y += metrics.verticalSpacing;

  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int noteTop = bottom - smallLine * 3;
  LadderRow actions[3];
  const uint8_t n = ladderActions(actions);
  const int rows = n + rivals::kLadderSize;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), noteTop - metrics.verticalSpacing - y}, rows, selected_,
      [this](const int i) -> std::string {
        LadderRow acts[3];
        const uint8_t na = ladderActions(acts);
        char t[128];
        if (i < na) {
          switch (acts[i]) {
            case LadderRow::Challenge: {
              const rivals::Bout b = rivals::challengeBout(*state_);
              if (b.block == rivals::Block::AtTop) return ctr(CULT_LADDER_TOP);
              snprintf(t, sizeof(t), ctr(CULT_LADDER_CHALLENGE_FORMAT), static_cast<unsigned>(b.rank));
              return t;
            }
            case LadderRow::Accept:
              snprintf(t, sizeof(t), ctr(CULT_LADDER_INCOMING_FORMAT),
                       static_cast<unsigned>(rivals::defenceBout(*state_).rank));
              return t;
            default:
              return ctr(CULT_LADDER_DECLINE);
          }
        }
        const auto rk = static_cast<uint8_t>(i - na + 1);
        const rivals::Rung r = rivals::rung(*state_, rk);
        if (r.player) {
          snprintf(t, sizeof(t), ctr(CULT_LADDER_ROW_FORMAT), static_cast<unsigned>(rk), ctr(CULT_LADDER_YOU_ROW));
        } else if (r.rival) {  // named as your rival once you have met them
          snprintf(t, sizeof(t),
                   rivals::rival(*state_).known ? ctr(CULT_LADDER_RIVAL_ROW_FORMAT) : ctr(CULT_LADDER_ROW_FORMAT),
                   static_cast<unsigned>(rk), rivals_ ? rivals_->rival : "");
        } else {
          snprintf(t, sizeof(t), ctr(CULT_LADDER_ROW_FORMAT), static_cast<unsigned>(rk),
                   rivals_ && r.seat >= 1 ? rivals_->seats[r.seat - 1] : "");
        }
        return t;
      },
      [this](const int i) -> std::string {
        LadderRow acts[3];
        const uint8_t na = ladderActions(acts);
        char t[160];
        if (i < na) {
          if (acts[i] == LadderRow::Challenge) {
            const rivals::Bout b = rivals::challengeBout(*state_);
            switch (b.block) {
              case rivals::Block::NoDate:
                return ctr(CULT_LADDER_NO_DATE);
              case rivals::Block::DoneToday:
                return ctr(CULT_LADDER_DONE);
              case rivals::Block::AtTop:
                return "";
              case rivals::Block::NoHp:
                snprintf(t, sizeof(t), ctr(CULT_LADDER_NO_HP_FORMAT), static_cast<unsigned>(b.hpCost));
                return t;
              default:
                snprintf(t, sizeof(t), ctr(CULT_LADDER_CHALLENGE_SUB_FORMAT), dangerWord(b.danger),
                         static_cast<unsigned>(b.chance), static_cast<unsigned>(b.hpCost),
                         static_cast<unsigned long>(b.stones));
                return t;
            }
          }
          if (acts[i] == LadderRow::Accept) {
            const rivals::Bout b = rivals::defenceBout(*state_);
            if (state_->progress.hp < b.hpCost) {
              snprintf(t, sizeof(t), ctr(CULT_LADDER_ACCEPT_NO_HP_FORMAT), static_cast<unsigned>(b.hpCost));
            } else {
              snprintf(t, sizeof(t), ctr(CULT_LADDER_ACCEPT_SUB_FORMAT), dangerWord(b.danger),
                       static_cast<unsigned>(b.chance), static_cast<unsigned>(b.rank), static_cast<unsigned>(b.hpCost),
                       static_cast<unsigned long>(b.stones));
            }
            return t;
          }
          return ctr(CULT_LADDER_DECLINE_SUB);
        }
        const auto rk = static_cast<uint8_t>(i - na + 1);
        const rivals::Rung r = rivals::rung(*state_, rk);
        if (r.player) return "";
        return dangerWord(explore::dangerFor(explore::playerLevel(*state_, true) - r.level));
      },
      nullptr, nullptr, false,
      [this](const int i) -> bool {
        LadderRow acts[3];
        const uint8_t na = ladderActions(acts);
        if (i < na) {
          return acts[i] == LadderRow::Challenge && rivals::challengeBout(*state_).block != rivals::Block::None;
        }
        return false;
      });
  drawWrapped(SMALL_FONT_ID, x, noteTop, width, notice_[0] != '\0' ? notice_ : ctr(CULT_LADDER_NOTE), 3);
}

void CultivationActivity::renderNemeses(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[224];
  const uint8_t count = rivals::nemesisCount(s);
  if (count == 0 || !rivals_) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_NEMESES_NONE), 6);
    return;
  }
  // Nemeses fill their places in order and never leave, so row i is place i.
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int listHeight = rowHeight * count;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), listHeight}, count, selected_,
      [this](const int i) -> std::string {
        char t[exploretext::kNameSize];
        snprintf(t, sizeof(t), "%s", rivals_->nemeses[i]);
        capitalize(t);
        return t;
      },
      [this](const int i) -> std::string {
        rivals::Nemesis n;
        if (!rivals::nemesis(*state_, static_cast<uint8_t>(i), n)) return "";
        char t[96];
        if (rivals::hunting(*state_, n)) {
          snprintf(t, sizeof(t), "%s", ctr(CULT_NEMESIS_HUNTING));
        } else {
          snprintf(t, sizeof(t), ctr(CULT_NEMESIS_SEEN_FORMAT), rivals_->regions[n.region]);
        }
        if (rivals::resting(*state_, n)) {
          const size_t used = strlen(t);
          snprintf(t + used, sizeof(t) - used, "%s", ctr(CULT_NEMESIS_RESTING_SUFFIX));
        }
        return t;
      },
      nullptr,
      [this](const int i) -> std::string {
        rivals::Nemesis n;
        if (!rivals::nemesis(*state_, static_cast<uint8_t>(i), n)) return "";
        const int d = explore::playerLevel(*state_, n.kind == explore::Enemy::Cultivator) -
                      rivals::fightLevel(*state_, n, rivals::clockHour());
        return dangerWord(explore::dangerFor(d));
      });
  y += listHeight + metrics.verticalSpacing;

  rivals::Nemesis n;
  if (!rivals::nemesis(s, static_cast<uint8_t>(selected_), n)) return;
  char kind[exploretext::kNameSize];
  snprintf(kind, sizeof(kind), "%s", rivals_->kinds[selected_]);
  capitalize(kind);
  if (kind[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, kind, 2);
  if (n.elementKnown) {
    snprintf(line, sizeof(line), ctr(CULT_NEMESIS_ELEMENT_FORMAT), ui::elementName(n.element));
  } else {
    snprintf(line, sizeof(line), "%s", ctr(CULT_NEMESIS_ELEMENT_UNKNOWN));
  }
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + 4;
  uint8_t hidden = 0;
  bool anyKnown = false;
  for (uint8_t i = 0; i < n.traitCount && y < bottom; ++i) {
    if ((n.revealed & (1u << i)) == 0) {
      ++hidden;
      continue;
    }
    anyKnown = true;
    char name[48];
    snprintf(name, sizeof(name), "%s", ui::traitName(n.traits[i]));
    capitalize(name);
    snprintf(line, sizeof(line), "%s: %s.", name, ui::traitEffect(n.traits[i]));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
  if (!anyKnown) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_NEMESIS_NONE_KNOWN), 3);
  } else if (hidden > 0) {
    snprintf(line, sizeof(line), ctr(CULT_NEMESIS_HIDDEN_FORMAT), static_cast<unsigned>(hidden));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 1);
  }
  snprintf(line, sizeof(line), ctr(CULT_NEMESIS_HISTORY_FORMAT), static_cast<unsigned>(n.wins),
           static_cast<unsigned>(n.losses), static_cast<unsigned>(n.escapes), static_cast<unsigned>(n.paid),
           static_cast<unsigned>(n.fled));
  drawWrapped(SMALL_FONT_ID, x, y + 4, width, line, 2);
}

// --- A visit's news ----------------------------------------------------------------------------

// The rival's news since the last visit (two lines at most): into the Return
// summary's log when there is one (the Chronicle gets it with the visit),
// otherwise straight into the Chronicle.
void CultivationActivity::appendRivalLines(const rivals::News& news) {
  if (!grammar_ || !state_) return;
  const rivals::Rival r = rivals::rival(*state_);
  if (!r.exists || !r.known) return;
  char name[64];
  char sect[64];
  if (!exploretext::rivalName(*state_, *grammar_, name, sizeof(name), sect, sizeof(sect))) {
    if (packs_) packs_->closeFile();
    return;
  }
  struct Pick {
    const char* list;
    const char* what;
  };
  Pick picks[lifelog::kMaxRivalLines];
  uint8_t count = 0;
  auto add = [&](const char* list, const char* what) {
    if (count < lifelog::kMaxRivalLines) picks[count++] = {list, what};
  };
  if (news.realmReached) add("rival.line.realm", ui::realmName(news.realm));
  if (news.youOvertook) add("rival.line.overtook", sect);
  if (news.theyOvertook) add("rival.line.overtaken", sect);
  if (news.hiatusBegan) add("rival.line.seclusion", sect);
  if (news.hiatusEnded) add("rival.line.back", sect);
  // Now and then, what people say (one summary in three, by the season).
  const uint32_t season = lifelog::seasonOf(state_->progress.ageMonths);
  if (lifeLog_ && Rng(contentSeed(worldSeedOf(*state_), kKindRivalText, season)).below(3) == 0) {
    add(kSayLists[static_cast<int>(r.temper)], sect);
  }
  for (uint8_t i = 0; i < count; ++i) {
    lifelog::Line line;
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(season, 0x100u + i));
    if (!exploretext::personLine(line, *state_, picks[i].list, seed, nullptr, name, picks[i].what, *grammar_)) continue;
    if (lifeLog_ && lifeLog_->count < lifelog::kMaxLines) {
      lifeLog_->lines[lifeLog_->count++] = line;
    } else {
      if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
      chronicle::appendLines(&line, 1);
    }
  }
  if (packs_) packs_->closeFile();
}

// One day in four the rumor of the day names a nemesis's trait (and you know it from then on).
bool CultivationActivity::nemesisRumor(const uint32_t day) {
  if (!grammar_) return false;
  rivals::Trait trait = rivals::Trait::None;
  const int index = rivals::rumorPick(*state_, day, trait);
  if (index < 0 || trait == rivals::Trait::None) return false;
  char name[exploretext::kNameSize];
  if (!exploretext::nemesisName(*state_, static_cast<uint8_t>(index), *grammar_, name, sizeof(name))) return false;
  char line[sizeof(rumor_)];
  if (!exploretext::personFlavor(*state_, "rumor.nemesis", contentSeed(worldSeedOf(*state_), kKindRivalText, day),
                                 nullptr, name, ui::traitRumor(trait), *grammar_, line, sizeof(line))) {
    return false;
  }
  capitalize(line);
  snprintf(rumor_, sizeof(rumor_), "%s.", line);
  rivals::revealTrait(*state_, static_cast<uint8_t>(index), trait);
  return true;
}

#endif  // CROSSINK_CULTIVATION
