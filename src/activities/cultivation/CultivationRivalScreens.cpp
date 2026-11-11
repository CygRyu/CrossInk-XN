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
#include "Days.h"
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

// v0.3.4: the agenda's goals in the pack's keys ("rival.goal.<key>.start", "rival.rumor.<key>").
const char* const kGoalKeys[agenda::index(agenda::Goal::Count)] = {"",     "tournament", "inheritance", "war",
                                                                   "rise", "duel",       "closed",      "hunt"};
const char* goalKey(const agenda::Goal g) {
  const uint8_t i = agenda::index(g);
  return i > 0 && i < agenda::index(agenda::Goal::Count) ? kGoalKeys[i] : nullptr;
}
const char* rankName(const agenda::Rank r) {  // on the card: "Inner disciple of ..."
  switch (r) {
    case agenda::Rank::Inner:
      return ctr(CULT_RANK_INNER);
    case agenda::Rank::Core:
      return ctr(CULT_RANK_CORE);
    case agenda::Rank::Elder:
      return ctr(CULT_RANK_ELDER);
    default:
      return ctr(CULT_RANK_OUTER);
  }
}
const char* rankWithArticle(const agenda::Rank r) {  // in lines: "made an inner disciple"
  switch (r) {
    case agenda::Rank::Inner:
      return ctr(CULT_RANK_INNER_A);
    case agenda::Rank::Core:
      return ctr(CULT_RANK_CORE_A);
    case agenda::Rank::Elder:
      return ctr(CULT_RANK_ELDER_A);
    default:
      return ctr(CULT_RANK_OUTER_A);
  }
}
const char* opinionWord(const int8_t v) {
  return v <= -4   ? ctr(CULT_OPINION_ENEMY)
         : v <= -2 ? ctr(CULT_OPINION_BITTER)
         : v == -1 ? ctr(CULT_OPINION_WARY)
         : v == 0  ? ctr(CULT_OPINION_EVEN)
         : v == 1  ? ctr(CULT_OPINION_WARM)
         : v <= 3  ? ctr(CULT_OPINION_FRIENDLY)
                   : ctr(CULT_OPINION_FRIEND);
}

// Menu rows of the Rivals screen.
enum MenuRow : int { kRowRival = 0, kRowLadder, kRowNemeses, kRowPeople, kMenuRows };  // People: v0.3.5

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
  agendaNowLine(v.now, sizeof(v.now));  // v0.3.4
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
  // v0.3.5: the cast. Names for all twelve (their links name each other), the living met ones first.
  for (uint8_t k = 0; k < sects::kSects; ++k) exploretext::sectName(s, k, *grammar_, v.sects[k], sizeof(v.sects[k]));
  v.peopleCount = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t i = 0; i < cast::kPeople; ++i) {
      cast::Person p;
      if (!cast::person(s, i, p)) continue;
      if (pass == 0) cast::name(s, i, v.people[i], sizeof(v.people[i]));
      if (p.met && p.alive == (pass == 0)) v.peopleOrder[v.peopleCount++] = i;
    }
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
  worldInfluence(res.world, notice_, sizeof(notice_));  // v0.3.3: a seat holder's sect
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
      screen_ != Screen::Nemeses && screen_ != Screen::People) {
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
      screen_ = selected_ == kRowRival    ? Screen::RivalCard
                : selected_ == kRowLadder ? Screen::Ladder
                : selected_ == kRowPeople ? Screen::People
                                          : Screen::Nemeses;
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
  } else if (screen_ == Screen::People && (up || down) && v.peopleCount > 0) {
    handleList(v.peopleCount, up, down);
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
        if (i == kRowPeople) return ctr(CULT_RIVALS_ROW_PEOPLE);
        if (i == kRowLadder) {
          snprintf(t, sizeof(t), ctr(CULT_RIVALS_ROW_LADDER_FORMAT), ui::realmName(state_->progress.realm));
          return t;
        }
        return ctr(CULT_RIVALS_ROW_NEMESES);
      },
      [this](const int i) -> std::string {
        const GameState& s = *state_;
        char t[96];
        if (i == kRowPeople) {  // v0.3.5
          if (!rivals_ || rivals_->peopleCount == 0) return ctr(CULT_PEOPLE_NONE_SUB);
          snprintf(t, sizeof(t), ctr(CULT_PEOPLE_COUNT_FORMAT), static_cast<unsigned>(rivals_->peopleCount));
          return t;
        }
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
  drawWrapped(SMALL_FONT_ID, x, noteTop, width,
              days::inUse(*state_) ? ctr(CULT_RIVALS_NOTE_READING_DAYS) : ctr(CULT_RIVALS_NOTE), 3);
}

void CultivationActivity::renderRivalCard(const int x, int y, const int width, const int bottom) {
  (void)bottom;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  const rivals::Rival r = rivals::rival(s);
  char line[224];
  if (!r.exists || !r.known || !rivals_) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RIVAL_UNMET), 8);
    if (r.exists && rivals_) {  // v0.3.7: their realm, by hearsay
      snprintf(line, sizeof(line), ctr(CULT_RIVAL_UNMET_REALM_FORMAT), ui::realmName(r.realm));
      drawWrapped(UI_10_FONT_ID, x, y + 8, width, line, 4);
    }
    return;
  }
  const RivalsView& v = *rivals_;
  const auto header = renderer.wrappedText(UI_12_FONT_ID, v.rival, width, 2, EpdFontFamily::BOLD);
  for (const auto& h : header) {
    renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  // v0.3.4: their place in the sect (outer disciple to elder).
  snprintf(line, sizeof(line), ctr(CULT_RIVAL_RANK_FORMAT), rankName(agenda::state(s).rank), v.sect);
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
  if (v.now[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, v.now, 3) + 4;  // v0.3.4: the agenda
  {
    const int8_t o = agenda::opinion(s);
    snprintf(line, sizeof(line), ctr(CULT_RIVAL_OPINION_FORMAT), opinionWord(o), static_cast<int>(o));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2) + metrics.verticalSpacing;
  }
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
              snprintf(t, sizeof(t),
                       b.extra && b.block != rivals::Block::DoneToday ? ctr(CULT_LADDER_AGAIN_FORMAT)
                                                                      : ctr(CULT_LADDER_CHALLENGE_FORMAT),
                       static_cast<unsigned>(b.rank));
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
                return b.extra ? ctr(CULT_LADDER_ALL_DONE) : ctr(CULT_LADDER_DONE);
              case rivals::Block::NoStamina:
                return ctr(CULT_LADDER_NO_STAMINA);
              case rivals::Block::AtTop:
                return "";
              case rivals::Block::NoHp:
                snprintf(t, sizeof(t), ctr(CULT_LADDER_NO_HP_FORMAT), static_cast<unsigned>(b.hpCost));
                return t;
              default:
                if (b.extra) {
                  snprintf(t, sizeof(t), ctr(CULT_LADDER_AGAIN_SUB_FORMAT), dangerWord(b.danger),
                           static_cast<unsigned>(b.chance), static_cast<unsigned>(b.hpCost),
                           static_cast<unsigned long>(b.stones), static_cast<unsigned>(b.extraLeft));
                  return t;
                }
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
void CultivationActivity::nameGoneNemeses(const uint8_t mask) {
  goneCount_ = 0;
  if (mask == 0 || !grammar_ || !state_) return;
  for (uint8_t i = 0; i < rivals::kNemesisMax; ++i) {
    rivals::Nemesis n;
    if ((mask & (1u << i)) == 0 || !rivals::nemesis(*state_, i, n)) continue;
    char name[exploretext::kNameSize];
    char region[exploretext::kNameSize];
    if (!exploretext::nemesisName(*state_, i, *grammar_, name, sizeof(name))) continue;
    if (!exploretext::regionName(*state_, n.region, *grammar_, region, sizeof(region))) region[0] = '\0';
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(n.seed, 0x474F4E45u));
    if (exploretext::personLine(goneLines_[goneCount_], *state_, "nemesis.line.gone", seed,
                                region[0] != '\0' ? region : nullptr, name, nullptr, *grammar_)) {
      ++goneCount_;
    }
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::appendGoneLines(const bool summary) {
  for (uint8_t i = 0; i < goneCount_; ++i) {
    if (summary && lifeLog_ && lifeLog_->count < lifelog::kMaxLines) {
      lifeLog_->lines[lifeLog_->count++] = goneLines_[i];
    } else {
      if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
      chronicle::appendLines(&goneLines_[i], 1);
    }
  }
  goneCount_ = 0;
}

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

// --- The rival's agenda (v0.3.4) ----------------------------------------------------------------

bool CultivationActivity::agendaRivalName(char* name, const size_t nameLen, char* sect, const size_t sectLen) {
  if (!grammar_ || !state_) return false;
  if (!exploretext::rivalName(*state_, *grammar_, name, nameLen, sect, sectLen)) return false;
  // Unmet, you hear of them only as a young disciple of their sect (agreed).
  if (!rivals::rival(*state_).known) snprintf(name, nameLen, ctr(CULT_RIVAL_UNMET_NAME_FORMAT), sect);
  return true;
}

bool CultivationActivity::agendaEventLine(const agenda::Event& e, lifelog::Line& line) {
  if (!grammar_) return false;
  const char* goal = goalKey(e.goal);
  if (goal == nullptr) return false;
  char key[48];
  switch (e.kind) {
    case agenda::EventKind::Started:
      snprintf(key, sizeof(key), "rival.goal.%s.start", goal);
      break;
    case agenda::EventKind::Won:
      snprintf(key, sizeof(key), "rival.goal.%s.won", goal);
      break;
    case agenda::EventKind::Lost:
      snprintf(key, sizeof(key), e.byYou ? "rival.goal.%s.yours" : "rival.goal.%s.lost", goal);
      break;
    case agenda::EventKind::Promoted:
      snprintf(key, sizeof(key), "rival.goal.rise.promoted");
      break;
    case agenda::EventKind::Served:
      snprintf(key, sizeof(key), "rival.goal.rise.served");
      break;
    case agenda::EventKind::CameOut:
      snprintf(key, sizeof(key), "rival.goal.closed.end");
      break;
    case agenda::EventKind::DuelIgnored:
      snprintf(key, sizeof(key), "rival.goal.duel.ignored");
      break;
    default:
      return false;
  }
  char name[96];
  char sect[64];
  if (!agendaRivalName(name, sizeof(name), sect, sizeof(sect))) return false;
  char region[48] = {};
  exploretext::regionName(*state_, e.region < explore::kRegionCount ? e.region : 0, *grammar_, region, sizeof(region));
  char what[96] = {};
  switch (e.goal) {
    case agenda::Goal::Tournament:
    case agenda::Goal::Inheritance:
    case agenda::Goal::War:
      if (e.sect >= 0) exploretext::sectName(*state_, static_cast<uint8_t>(e.sect), *grammar_, what, sizeof(what));
      break;
    case agenda::Goal::Hunt:
      exploretext::nemesisName(*state_, e.nemesis, *grammar_, what, sizeof(what));
      break;
    case agenda::Goal::Rise:
      snprintf(what, sizeof(what), "%s", rankWithArticle(e.rank));
      break;
    case agenda::Goal::ClosedDoor: {
      const rivals::Rival r = rivals::rival(*state_);
      char stage[32];
      ui::stageNameOf(r.realm, r.stage, stage, sizeof(stage));
      snprintf(what, sizeof(what), ctr(CULT_REALM_STAGE_FORMAT), ui::realmName(r.realm), stage);
      break;
    }
    default:
      break;
  }
  const uint32_t seed = contentSeed(
      worldSeedOf(*state_), kKindRivalText,
      mix32(e.year, 0x41474500u + (static_cast<uint32_t>(e.kind) << 4) + static_cast<uint32_t>(agenda::index(e.goal))));
  return exploretext::personLine(line, *state_, key, seed, region, name, what, *grammar_);
}

// summary: the Return summary's one line (an actionable duel letter first, then an ending, the
// latest); otherwise every other event of the visit, in order, into the Chronicle.
void CultivationActivity::appendAgendaLines(const bool summary) {
  if (!grammar_ || !state_ || agendaSteps_.count == 0) return;
  if (summary) {
    agendaSummary_ = -1;
    if (!lifeLog_ || lifeLog_->count >= lifelog::kMaxLines) return;
    auto priority = [](const agenda::Event& e) -> int {
      if (e.kind == agenda::EventKind::Started) return e.goal == agenda::Goal::Duel ? 3 : 1;
      return e.kind == agenda::EventKind::Served ? 1 : 2;
    };
    int best = -1;
    for (uint8_t i = 0; i < agendaSteps_.count; ++i) {
      if (best < 0 || priority(agendaSteps_.events[i]) >= priority(agendaSteps_.events[best])) best = i;
    }
    lifelog::Line line;
    if (best >= 0 && agendaEventLine(agendaSteps_.events[best], line)) {
      lifeLog_->lines[lifeLog_->count++] = line;
      agendaSummary_ = static_cast<int8_t>(best);
    }
  } else {
    bool opened = chronicle::exists();
    for (uint8_t i = 0; i < agendaSteps_.count; ++i) {
      if (i == agendaSummary_) continue;
      lifelog::Line line;
      if (!agendaEventLine(agendaSteps_.events[i], line)) continue;
      if (!opened) {
        chronicle::writeOpening(*state_, grammar_.get());
        opened = true;
      }
      chronicle::appendLines(&line, 1);
    }
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

// One day in four while a goal is under way, the rumor of the day is about it.
bool CultivationActivity::agendaRumorOfTheDay(const uint32_t when) {
  if (!grammar_ || !state_) return false;
  const agenda::State st = agenda::state(*state_);
  const char* goal = goalKey(st.goal);
  if (goal == nullptr) return false;
  if (Rng(contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(when, 0x41474E44u))).below(4) != 0) return false;
  char name[96];
  char sect[64];
  if (!agendaRivalName(name, sizeof(name), sect, sizeof(sect))) return false;
  char region[48] = {};
  exploretext::regionName(*state_, st.region < explore::kRegionCount ? st.region : 0, *grammar_, region,
                          sizeof(region));
  char what[96] = {};
  if (st.goal == agenda::Goal::Hunt) {
    exploretext::nemesisName(*state_, st.nemesis, *grammar_, what, sizeof(what));
  } else if (st.nemesis < sects::kSects && (st.goal == agenda::Goal::Tournament ||
                                            st.goal == agenda::Goal::Inheritance || st.goal == agenda::Goal::War)) {
    exploretext::sectName(*state_, st.nemesis, *grammar_, what, sizeof(what));
  } else {
    snprintf(what, sizeof(what), "%s", sect);
  }
  char key[40];
  snprintf(key, sizeof(key), "rival.rumor.%s", goal);
  char line[sizeof(rumor_)];
  if (!exploretext::personFlavor(*state_, key, contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(when, 7u)),
                                 region, name, what, *grammar_, line, sizeof(line))) {
    return false;
  }
  capitalize(line);
  snprintf(rumor_, sizeof(rumor_), "%s.", line);
  return true;
}

// The rival card's "Now:" line. Reads the packs (names): call while loading, never while drawing.
void CultivationActivity::agendaNowLine(char* out, const size_t len) {
  if (len == 0) return;
  out[0] = '\0';
  if (!grammar_ || !state_) return;
  const GameState& s = *state_;
  const agenda::State st = agenda::state(s);
  char region[48] = {};
  exploretext::regionName(s, st.region < explore::kRegionCount ? st.region : 0, *grammar_, region, sizeof(region));
  char other[64] = {};
  if (st.nemesis < sects::kSects) exploretext::sectName(s, st.nemesis, *grammar_, other, sizeof(other));
  const unsigned hours = agenda::hoursLeft(s);
  switch (st.goal) {
    case agenda::Goal::Tournament:
      snprintf(out, len, ctr(CULT_AGENDA_NOW_TOURNAMENT_FORMAT), other, region);
      break;
    case agenda::Goal::Inheritance:
      snprintf(out, len, ctr(CULT_AGENDA_NOW_INHERITANCE_FORMAT), region);
      break;
    case agenda::Goal::War:
      snprintf(out, len, ctr(CULT_AGENDA_NOW_WAR_FORMAT), other, region);
      break;
    case agenda::Goal::Rise: {
      const uint8_t next = static_cast<uint8_t>(agenda::index(st.rank) + 1);
      if (next < agenda::index(agenda::Rank::Count)) {
        snprintf(out, len, ctr(CULT_AGENDA_NOW_RISE_FORMAT), rankWithArticle(static_cast<agenda::Rank>(next)));
      } else {
        snprintf(out, len, "%s", ctr(CULT_AGENDA_NOW_SERVE));
      }
      break;
    }
    case agenda::Goal::Duel:
      snprintf(out, len, ctr(CULT_AGENDA_NOW_DUEL_FORMAT), region, hours,
               static_cast<unsigned long>(agenda::duelStakes(s)));
      break;
    case agenda::Goal::ClosedDoor:
      snprintf(out, len, ctr(CULT_AGENDA_NOW_CLOSED_FORMAT), hours);
      break;
    case agenda::Goal::Hunt: {
      char name[exploretext::kNameSize] = {};
      exploretext::nemesisName(s, st.nemesis, *grammar_, name, sizeof(name));
      snprintf(out, len, ctr(CULT_AGENDA_NOW_HUNT_FORMAT), name);
      break;
    }
    default:
      snprintf(out, len, "%s", ctr(CULT_AGENDA_NOW_NONE));
      break;
  }
}

// --- The cast (v0.3.5) ---------------------------------------------------------------------------

void CultivationActivity::renderPeople(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  if (!rivals_ || rivals_->peopleCount == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_PEOPLE_NONE), 6);
    return;
  }
  const RivalsView& v = *rivals_;
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int shown = v.peopleCount < 5 ? v.peopleCount : 5;
  const int listHeight = rowHeight * shown;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), listHeight}, v.peopleCount, selected_,
      [this](const int i) -> std::string {
        char t[64];
        snprintf(t, sizeof(t), "%s", rivals_->people[rivals_->peopleOrder[i]]);
        capitalize(t);
        return t;
      },
      [this](const int i) -> std::string {
        cast::Person p;
        if (!cast::person(*state_, rivals_->peopleOrder[i], p)) return "";
        char t[96];
        if (!p.alive) {
          snprintf(t, sizeof(t),
                   p.cause == cast::Cause::War ? ctr(CULT_PEOPLE_DIED_WAR_FORMAT) : ctr(CULT_PEOPLE_DIED_OLD_FORMAT),
                   static_cast<unsigned>(p.age), ui::realmName(p.realm));
          return t;
        }
        char stage[32];
        ui::stageNameOf(p.realm, p.stage, stage, sizeof(stage));
        snprintf(t, sizeof(t), ctr(CULT_PEOPLE_SUB_FORMAT), ui::realmName(p.realm), stage,
                 static_cast<unsigned>(p.age));
        return t;
      },
      nullptr,
      [this](const int i) -> std::string {
        cast::Person p;
        if (!cast::person(*state_, rivals_->peopleOrder[i], p) || !p.alive || p.opinion == 0) return "";
        return opinionWord(p.opinion);  // the long "neither warm nor cold" would squeeze the row
      });
  y += listHeight + metrics.verticalSpacing;

  const int sel = selected_ < v.peopleCount ? selected_ : 0;
  cast::Person p;
  if (!cast::person(s, v.peopleOrder[sel], p)) return;
  char line[224];
  if (p.sect >= 0) {
    snprintf(line, sizeof(line), ctr(CULT_RIVAL_RANK_FORMAT), rankName(static_cast<agenda::Rank>(p.rank)),
             v.sects[p.sect]);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  } else if (p.alive) {
    y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_PEOPLE_NO_SECT), 2);
  }
  if (p.link != cast::Link::None && y < bottom) {
    char other[64];
    if (p.linkTo < cast::kPeople) {
      cast::Person o;
      const bool dead = cast::person(s, p.linkTo, o) && !o.alive;
      snprintf(other, sizeof(other), dead ? ctr(CULT_PEOPLE_LATE_FORMAT) : "%s", v.people[p.linkTo]);
    } else if (p.linkTo == cast::kLinkRival) {
      snprintf(other, sizeof(other), ctr(CULT_PEOPLE_YOUR_RIVAL_FORMAT), v.rival);
    } else {
      snprintf(other, sizeof(other), "%s", ctr(CULT_PEOPLE_YOU));
    }
    const GameStr formats[] = {GameStr::CULT_PEOPLE_MASTER_FORMAT,   GameStr::CULT_PEOPLE_MASTER_FORMAT,
                               GameStr::CULT_PEOPLE_DISCIPLE_FORMAT, GameStr::CULT_PEOPLE_SIBLING_FORMAT,
                               GameStr::CULT_PEOPLE_SPOUSE_FORMAT,   GameStr::CULT_PEOPLE_ENEMY_FORMAT};
    snprintf(line, sizeof(line), gameText(formats[static_cast<uint8_t>(p.link)]), other);
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
  if (p.alive && y < bottom) {
    const bool known = p.region < explore::unlockedCount(s);
    if (known) {
      snprintf(line, sizeof(line), ctr(CULT_PEOPLE_FOUND_FORMAT), v.regions[p.region]);
    } else {
      snprintf(line, sizeof(line), "%s", ctr(CULT_PEOPLE_FAR));
    }
    if (p.secluded) snprintf(line, sizeof(line), "%s", ctr(CULT_PEOPLE_SECLUDED));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
  if (!p.alive) {  // the dead: who they were tied to is enough; the Chronicle has the rest
    if (y < bottom) drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_PEOPLE_REMEMBERED), 2);
    return;
  }
  if (y < bottom) {
    const GameStr tempers[] = {GameStr::CULT_PEOPLE_PROUD, GameStr::CULT_PEOPLE_CAUTIOUS, GameStr::CULT_PEOPLE_GREEDY};
    const GameStr ambitions[] = {GameStr::CULT_PEOPLE_WANTS_POWER, GameStr::CULT_PEOPLE_WANTS_STANDING,
                                 GameStr::CULT_PEOPLE_WANTS_WEALTH, GameStr::CULT_PEOPLE_WANTS_FAMILY};
    snprintf(line, sizeof(line), "%s %s", gameText(tempers[static_cast<uint8_t>(p.temper) % 3]),
             gameText(ambitions[static_cast<uint8_t>(p.ambition) % 4]));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  }
  if (p.alive && y < bottom) {
    const GameStr partings[] = {GameStr::CULT_PEOPLE_PARTED_NONE, GameStr::CULT_PEOPLE_PARTED_WON,
                                GameStr::CULT_PEOPLE_PARTED_LOST, GameStr::CULT_PEOPLE_PARTED_FRIENDLY};
    snprintf(line, sizeof(line), ctr(CULT_PEOPLE_OPINION_FORMAT), opinionWord(p.opinion), static_cast<int>(p.opinion),
             gameText(partings[static_cast<uint8_t>(p.parting) % 4]));
    drawWrapped(UI_10_FONT_ID, x, y, width, line, 3);
  }
}

bool CultivationActivity::castPersonName(const uint8_t i, char* out, const size_t len) {
  if (!grammar_ || !state_ || len == 0) return false;
  out[0] = '\0';
  if (i == cast::kLinkRival) {
    char sect[64];
    if (!exploretext::rivalName(*state_, *grammar_, out, len, sect, sizeof(sect))) return false;
    if (!rivals::rival(*state_).known) snprintf(out, len, ctr(CULT_RIVAL_UNMET_NAME_FORMAT), sect);
    return true;
  }
  cast::Person p;
  if (!cast::person(*state_, i, p)) return false;
  cast::name(*state_, i, out, len);
  return out[0] != '\0';
}

bool CultivationActivity::castEventLine(const cast::Event& e, lifelog::Line& line) {
  if (!grammar_ || !state_) return false;
  static const char* const kKeys[] = {
      "cast.line.arrived", "cast.line.breakthrough", "cast.line.failed",     "cast.line.seclusion", "cast.line.joined",
      "cast.line.left",    "cast.line.rose",         "cast.line.apprentice", "cast.line.married",   "cast.line.sworn",
      "cast.line.feud",    "cast.line.fellout",      "cast.line.died.old",   "cast.line.died.war"};
  static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == static_cast<size_t>(cast::EventKind::Count), "cast lines");
  const auto k = static_cast<uint8_t>(e.kind);
  if (k >= static_cast<uint8_t>(cast::EventKind::Count)) return false;
  const char* key = kKeys[k];
  if (e.kind == cast::EventKind::Arrived && e.other != cast::kLinkNone) key = "cast.line.arrived.disciple";
  char name[exploretext::kNameSize];
  if (!castPersonName(e.who, name, sizeof(name))) return false;
  char region[48] = {};
  exploretext::regionName(*state_, e.region < explore::kRegionCount ? e.region : 0, *grammar_, region, sizeof(region));
  char what[exploretext::kNameSize] = {};
  char sect[exploretext::kNameSize] = {};
  if (e.sect >= 0) exploretext::sectName(*state_, static_cast<uint8_t>(e.sect), *grammar_, sect, sizeof(sect));
  switch (e.kind) {
    case cast::EventKind::Breakthrough:
    case cast::EventKind::Failed:
      snprintf(what, sizeof(what), "%s", ui::realmName(e.realm));
      break;
    case cast::EventKind::JoinedSect:
    case cast::EventKind::LeftSect:
    case cast::EventKind::DiedWar:
      snprintf(what, sizeof(what), "%s", sect);
      break;
    case cast::EventKind::Rose:
      snprintf(what, sizeof(what), ctr(CULT_RIVAL_RANK_FORMAT), rankWithArticle(static_cast<agenda::Rank>(e.rank)),
               sect);
      break;
    case cast::EventKind::DiedOld:
      snprintf(what, sizeof(what), ctr(CULT_CAST_AGE_FORMAT), static_cast<unsigned>(e.age));
      break;
    default:
      if (e.other != cast::kLinkNone) castPersonName(e.other, what, sizeof(what));
      break;
  }
  const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(e.year, 0x43535400u + e.who * 32u + k));
  return exploretext::personLine(line, *state_, key, seed, region, name, what, *grammar_);
}

// summary: the Return summary's one line (a death first, then a bond, a rank or a breakthrough, the
// latest); otherwise every other newsworthy event of the visit, in order, into the Chronicle.
void CultivationActivity::appendCastLines(const bool summary) {
  if (!grammar_ || !state_ || castSteps_.count == 0) return;
  if (summary) {
    castSummary_ = -1;
    if (!lifeLog_ || lifeLog_->count >= lifelog::kMaxLines) return;
    auto priority = [](const cast::Event& e) -> int {
      switch (e.kind) {
        case cast::EventKind::DiedOld:
        case cast::EventKind::DiedWar:
          return 3;
        case cast::EventKind::Seclusion:
        case cast::EventKind::Failed:
        case cast::EventKind::JoinedSect:
        case cast::EventKind::LeftSect:
          return 1;
        default:
          return 2;
      }
    };
    int best = -1;
    for (uint8_t i = 0; i < castSteps_.count; ++i) {
      if (!cast::newsworthy(*state_, castSteps_.events[i])) continue;
      if (best < 0 || priority(castSteps_.events[i]) >= priority(castSteps_.events[best])) best = i;
    }
    lifelog::Line line;
    if (best >= 0 && castEventLine(castSteps_.events[best], line)) {
      lifeLog_->lines[lifeLog_->count++] = line;
      castSummary_ = static_cast<int8_t>(best);
    }
  } else {
    bool opened = chronicle::exists();
    for (uint8_t i = 0; i < castSteps_.count; ++i) {
      if (i == castSummary_ || !cast::newsworthy(*state_, castSteps_.events[i])) continue;
      lifelog::Line line;
      if (!castEventLine(castSteps_.events[i], line)) continue;
      if (!opened) {
        chronicle::writeOpening(*state_, grammar_.get());
        opened = true;
      }
      chronicle::appendLines(&line, 1);
    }
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

// One day in four, the rumor of the day is about someone of the cast (by name once you have met).
bool CultivationActivity::castRumorOfTheDay(const uint32_t when) {
  if (!grammar_ || !state_ || !cast::founded(*state_)) return false;
  Rng rng(contentSeed(worldSeedOf(*state_), kKindRivalText, mix32(when, 0x43415354u)));
  if (rng.below(4) != 0) return false;
  const auto i = static_cast<uint8_t>(rng.below(cast::kPeople));
  cast::Person p;
  if (!cast::person(*state_, i, p) || !p.alive) return false;
  char name[exploretext::kNameSize];
  if (p.met) {
    if (!castPersonName(i, name, sizeof(name))) return false;
  } else if (p.sect >= 0) {
    char sect[exploretext::kNameSize];
    exploretext::sectName(*state_, static_cast<uint8_t>(p.sect), *grammar_, sect, sizeof(sect));
    snprintf(name, sizeof(name), ctr(CULT_CAST_UNMET_SECT_FORMAT), sect);
  } else {
    snprintf(name, sizeof(name), "%s", ctr(CULT_CAST_UNMET_NONE));
  }
  char region[48] = {};
  exploretext::regionName(*state_, p.region, *grammar_, region, sizeof(region));
  char line[sizeof(rumor_)];
  if (!exploretext::personFlavor(*state_, p.met ? "cast.rumor.met" : "cast.rumor.unmet", rng.next(), region, name,
                                 ui::realmName(p.realm), *grammar_, line, sizeof(line))) {
    return false;
  }
  capitalize(line);
  snprintf(rumor_, sizeof(rumor_), "%s.", line);
  return true;
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
