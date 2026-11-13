#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Achievements.h"
#include "Chronicle.h"
#include "Codex.h"
#include "CultivationActivity.h"
#include "CultivationUi.h"
#include "GameText.h"
#include "Lineage.h"
#include "Scrolls.h"
#include "components/UITheme.h"
#include "fontIds.h"

// v0.3.0 "A New Life" (agreed with the user in session 14): the Lineage cell
// (lives, the Qi bonus, imprints, reincarnating or abandoning a life), the end
// of a life (export, two confirmations, Past Life Echo), the legacy perks
// before a new life, the Collection (codex and mementos) and past lives'
// Chronicles. One LineageView (~1.3 KB) while these screens are open.

using namespace cultivation;

namespace {

GameStr offset(const GameStr first, const int i) {
  return static_cast<GameStr>(static_cast<uint16_t>(first) + static_cast<uint16_t>(i));
}

const char* mementoName(const uint8_t m) { return gameText(offset(GameStr::CULT_MEM_00, 2 * m)); }
const char* mementoDescription(const uint8_t m) { return gameText(offset(GameStr::CULT_MEM_00, 2 * m + 1)); }

const char* rarityWord(const codex::Rarity r) {
  return r == codex::Rarity::Legendary ? ctr(CULT_RARITY_LEGENDARY)
         : r == codex::Rarity::Rare    ? ctr(CULT_RARITY_RARE)
                                       : ctr(CULT_RARITY_COMMON);
}

void nameOf(const GameState& state, char out[kNameMaxBytes + 1]) {
  memcpy(out, state.character.name.data(), kNameMaxBytes);
  out[kNameMaxBytes] = '\0';
}

const char* plural(const unsigned n) { return n == 1 ? "" : "s"; }

// The perks, then (with Familiar Face) the rival choice, then "Begin the new life".
bool rivalRow(const GameState& s) { return lineage::rank(s, lineage::Perk::FamiliarFace) > 0; }
int perkRows(const GameState& s) { return lineage::kPerkCount + (rivalRow(s) ? 1 : 0) + 1; }
bool isRivalRow(const GameState& s, const int row) { return rivalRow(s) && row == lineage::kPerkCount; }
constexpr int kCollectionRows = 1 + codex::kSetCount;

}  // namespace

const char* CultivationActivity::chroniclePath() const {
  return chronicleLife_ != 0 ? chroniclePath_ : chronicle::kPath;
}

const char* CultivationActivity::lineageTitle() const {
  switch (screen_) {
    case Screen::Lineage:
      return lineage_ && lineage_->oldAge ? ctr(CULT_LINEAGE_OLD_AGE_TITLE) : ctr(CULT_LINEAGE_TITLE);
    case Screen::LineageExport:
    case Screen::LineageConfirm:
      return ctr(CULT_LINEAGE_EXPORT_TITLE);
    case Screen::Echo:
      return ctr(CULT_ECHO_TITLE);
    case Screen::Perks:
      return ctr(CULT_PERKS_TITLE);
    case Screen::Collection:
      return ctr(CULT_COLLECTION_TITLE);
    case Screen::CollectionPage:
      if (lineage_ && lineage_->page >= 1 && lineage_->page <= codex::kSetCount) {
        return gameText(offset(GameStr::CULT_SET_0, lineage_->page - 1));
      }
      return ctr(CULT_CODEX_TITLE);
    case Screen::PastLives:
      return ctr(CULT_PAST_LIVES_TITLE);
    default:
      return nullptr;
  }
}

// --- Opening and closing ----------------------------------------------------------------------

void CultivationActivity::prepareLineage(const bool oldAge) {
  if (!lineage_) lineage_ = makeUniqueNoThrow<LineageView>();
  if (!lineage_) {
    LOG_ERR("CULT", "Out of memory for the lineage screens (%u bytes)", static_cast<unsigned>(sizeof(LineageView)));
    screen_ = Screen::Cultivator;
    return;
  }
  *lineage_ = LineageView{};
  lineage_->oldAge = oldAge;
  if (!oldAge) lineageReturn_ = selected_;
  notice_[0] = '\0';
  selected_ = 0;
  screen_ = Screen::Lineage;
}

void CultivationActivity::openLineage(const bool oldAge) {
  {
    RenderLock lock(*this);
    prepareLineage(oldAge);
  }
  requestUpdate();
}

void CultivationActivity::closeLineage() {
  {
    RenderLock lock(*this);
    lineage_.reset();
    notice_[0] = '\0';
    refreshActions();
    selected_ = lineageReturn_ < actionCount_ ? lineageReturn_ : 0;
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

uint8_t CultivationActivity::lineageRowList(LineageRow* out) const {
  uint8_t n = 0;
  if (lineage_ && lineage_->oldAge) {
    out[n++] = LineageRow::NewLife;
    out[n++] = LineageRow::Collection;
    out[n++] = LineageRow::Achievements;  // v0.3.8
    return n;
  }
  out[n++] = LineageRow::Collection;
  out[n++] = LineageRow::Achievements;  // v0.3.8
  if (lineage::canReincarnate(*state_)) out[n++] = LineageRow::Reincarnate;
  if (lineage::canAbandon(*state_)) out[n++] = LineageRow::Abandon;
  return n;
}

int CultivationActivity::lineageRows() const {
  switch (screen_) {
    case Screen::Lineage: {
      LineageRow rows[4];
      return lineageRowList(rows);
    }
    case Screen::LineageExport:
      return 2;
    case Screen::Echo:
      return lineage_ ? lineage_->candidateCount + 1 : 1;
    case Screen::Perks:
      return perkRows(*state_);
    case Screen::Collection:
      return kCollectionRows;
    case Screen::PastLives:
      return lineage_ ? lineage_->pastCount : 0;
    default:
      return 0;
  }
}

// --- Input -------------------------------------------------------------------------------------

void CultivationActivity::lineageChoose() {
  LineageRow rows[4];
  const uint8_t n = lineageRowList(rows);
  if (!lineage_ || selected_ < 0 || selected_ >= n) return;
  LineageView& v = *lineage_;
  v.lineageSelected = selected_;
  switch (rows[selected_]) {
    case LineageRow::Collection:
      selected_ = v.collectionSelected;
      screen_ = Screen::Collection;
      return;
    case LineageRow::Achievements:  // v0.3.8
      openAchievements();
      return;
    case LineageRow::Reincarnate:
      v.kind = lineage::EndKind::Reincarnated;
      break;
    case LineageRow::Abandon:
      v.kind = lineage::EndKind::Abandoned;
      break;
    case LineageRow::NewLife:
      v.kind = lineage::EndKind::OldAge;
      break;
  }
  notice_[0] = '\0';
  selected_ = 0;
  screen_ = Screen::LineageExport;
}

// After the confirmations: Past Life Echo when there is a scroll to carry, else the end itself.
void CultivationActivity::afterConfirm() {
  LineageView& v = *lineage_;
  const uint8_t n = scrolls::count(*state_);
  if (v.kind == lineage::EndKind::Abandoned || lineage::rank(*state_, lineage::Perk::Echo) == 0 || n == 0) {
    endThisLife();
    return;
  }
  // The strongest scrolls first (tier, then quality), at most kCandidates.
  v.candidateCount = 0;
  bool taken[scrolls::kCapacity] = {};
  while (v.candidateCount < LineageView::kCandidates) {
    int best = -1;
    uint32_t bestStrength = 0;
    for (uint8_t i = 0; i < n; ++i) {
      if (taken[i]) continue;
      const scrolls::Scroll sc = scrolls::get(*state_, i);
      if (sc.titleHash == 0) continue;
      const uint32_t st = scrolls::strength(sc);
      if (best < 0 || st > bestStrength) {
        best = i;
        bestStrength = st;
      }
    }
    if (best < 0) break;
    taken[best] = true;
    scrollName(scrolls::get(*state_, static_cast<uint8_t>(best)), v.names[v.candidateCount],
               sizeof(v.names[v.candidateCount]));
    v.candidates[v.candidateCount++] = static_cast<uint8_t>(best);
  }
  if (packs_) packs_->closeFile();
  v.chosenCount = 0;
  selected_ = 0;
  screen_ = Screen::Echo;
}

// The life ends: its last Chronicle line, the lineage updated, the save, the
// Chronicle moved aside; then the legacy perks. Called under the render lock.
void CultivationActivity::endThisLife() {
  LineageView& v = *lineage_;
  char name[kNameMaxBytes + 1];
  nameOf(*state_, name);
  const unsigned age = ageYears(*state_);
  lifelog::Line line;
  line.kind = lifelog::LineKind::Event;
  line.season = lifelog::seasonOf(state_->progress.ageMonths);
  switch (v.kind) {
    case lineage::EndKind::OldAge:
      snprintf(line.text, sizeof(line.text), ctr(CULT_END_OLD_AGE_FORMAT), age, name,
               ui::realmName(state_->progress.realm));
      break;
    case lineage::EndKind::Abandoned:
      snprintf(line.text, sizeof(line.text), ctr(CULT_END_ABANDONED_FORMAT), age, name);
      break;
    default:
      snprintf(line.text, sizeof(line.text), ctr(CULT_END_REINCARNATED_FORMAT), age, name);
      break;
  }
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  chronicle::appendLines(&line, 1);
  if (packs_) packs_->closeFile();
  // v0.3.8: old age or abandoning earns its achievement in this life's Chronicle (endLife repeats it).
  achievements::lifeEnding(*state_, v.kind == lineage::EndKind::OldAge, v.kind == lineage::EndKind::Abandoned);
  announceAchievements(false);

  const lineage::EndResult r = lineage::endLife(*state_, v.kind, v.chosen, v.chosenCount);
  LOG_INF("CULT", "Life %u ended (%u): %u imprints, best realm %d -> %d, %u scrolls carried",
          static_cast<unsigned>(r.lifeNumber), static_cast<unsigned>(v.kind), static_cast<unsigned>(r.imprints),
          r.bestBefore, r.bestAfter, static_cast<unsigned>(r.echoKept));
  // Saved first: a power cut before the move leaves the flag, and creation finishes the move.
  if (persist() && chronicle::moveToLives(r.lifeNumber)) {
    lineage::setChronicleMovePending(*state_, false);
    persist();
  }
  if (v.kind == lineage::EndKind::Abandoned) {
    snprintf(notice_, sizeof(notice_), ctr(CULT_PERKS_ABANDONED_FORMAT), name);
  } else {
    snprintf(notice_, sizeof(notice_), ctr(CULT_PERKS_ENDED_FORMAT), name, static_cast<unsigned>(r.imprints),
             plural(r.imprints));
  }
  chronicleExists_ = false;
  justCreated_ = false;
  visit_ = AbsorbResult{};
  visitSeconds_ = 0;
  rumor_[0] = '\0';
  selected_ = 0;
  screen_ = Screen::Perks;
}

bool CultivationActivity::handleLineage(const bool back, const bool confirm, const bool up, const bool down) {
  switch (screen_) {
    case Screen::Lineage:
    case Screen::LineageExport:
    case Screen::LineageConfirm:
    case Screen::Echo:
    case Screen::Perks:
    case Screen::Collection:
    case Screen::CollectionPage:
    case Screen::PastLives:
      break;
    default:
      return false;
  }
  if (!lineage_ && screen_ != Screen::Perks) {  // out of memory earlier: nothing to show
    if (back || confirm) closeLineage();
    return true;
  }
  const int rows = lineageRows();
  if ((up || down) && rows > 0 && screen_ != Screen::LineageConfirm && screen_ != Screen::CollectionPage) {
    handleList(rows, up, down);
    return true;
  }
  if (!back && !confirm) return true;

  switch (screen_) {
    case Screen::Lineage:
      if (back) {
        if (lineage_->oldAge) {
          finish();  // the life is over either way; the next visit comes back here
        } else {
          closeLineage();
        }
        return true;
      }
      {
        RenderLock lock(*this);
        lineageChoose();
      }
      break;

    case Screen::LineageExport: {
      RenderLock lock(*this);
      if (back) {
        selected_ = lineage_->lineageSelected;
        screen_ = Screen::Lineage;
        break;
      }
      notice_[0] = '\0';
      if (selected_ == 0) {  // Export
        char path[96];
        if (chronicle::exportCopy(*state_, path, sizeof(path))) {
          snprintf(notice_, sizeof(notice_), ctr(CULT_EXPORTED_FORMAT), path);
        } else {
          snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_EXPORT_FAILED));
        }
      }
      lineage_->confirmStep = 0;
      if (lineage_->kind == lineage::EndKind::OldAge) {  // no choice to confirm (user, session 14)
        afterConfirm();
      } else {
        screen_ = Screen::LineageConfirm;
      }
      break;
    }

    case Screen::LineageConfirm: {
      RenderLock lock(*this);
      if (back) {
        notice_[0] = '\0';
        selected_ = lineage_->lineageSelected;
        screen_ = Screen::Lineage;
      } else if (lineage_->confirmStep == 0) {
        lineage_->confirmStep = 1;
        notice_[0] = '\0';
      } else {
        afterConfirm();
      }
      break;
    }

    case Screen::Echo: {
      RenderLock lock(*this);
      LineageView& v = *lineage_;
      if (back) {
        selected_ = v.lineageSelected;
        screen_ = Screen::Lineage;
        break;
      }
      if (selected_ >= v.candidateCount) {  // Done
        endThisLife();
        break;
      }
      const uint8_t slot = v.candidates[selected_];
      int at = -1;
      for (uint8_t i = 0; i < v.chosenCount; ++i) {
        if (v.chosen[i] == slot) at = i;
      }
      if (at >= 0) {
        for (uint8_t i = static_cast<uint8_t>(at); i + 1 < v.chosenCount; ++i) v.chosen[i] = v.chosen[i + 1];
        --v.chosenCount;
      } else if (v.chosenCount < lineage::rank(*state_, lineage::Perk::Echo) && v.chosenCount < lineage::kEchoSlots) {
        v.chosen[v.chosenCount++] = slot;
      }
      break;
    }

    case Screen::Perks: {
      if (back) {
        finish();  // saved; the next visit comes back to the perks
        return true;
      }
      RenderLock lock(*this);
      if (selected_ < lineage::kPerkCount) {
        const auto perk = static_cast<lineage::Perk>(selected_);
        if (lineage::buy(*state_, perk)) {
          snprintf(notice_, sizeof(notice_), ctr(CULT_PERKS_BOUGHT_FORMAT),
                   gameText(offset(GameStr::CULT_PERK_0, selected_)),
                   static_cast<unsigned>(lineage::rank(*state_, perk)));
          persist();
        }
      } else if (isRivalRow(*state_, selected_)) {  // Familiar Face: keep the rival or meet a new one
        lineage::setKeepsRival(*state_, !lineage::keepsRival(*state_));
        notice_[0] = '\0';
        persist();
      } else {
        lineage_.reset();
        notice_[0] = '\0';
        beginCreation();
      }
      break;
    }

    case Screen::Collection: {
      RenderLock lock(*this);
      if (back) {
        lineage_->collectionSelected = selected_;
        selected_ = lineage_->lineageSelected;
        screen_ = Screen::Lineage;
      } else {
        lineage_->collectionSelected = selected_;
        lineage_->page = static_cast<uint8_t>(selected_);
        screen_ = Screen::CollectionPage;
      }
      break;
    }

    case Screen::CollectionPage: {
      RenderLock lock(*this);
      selected_ = lineage_->collectionSelected;
      screen_ = Screen::Collection;
      break;
    }

    case Screen::PastLives:
      if (back) {
        RenderLock lock(*this);
        lineage_.reset();
        selected_ = chronicle_ ? chronicle_->savedSelected : 0;
        screen_ = chronicle_ ? Screen::Chronicle : Screen::Cultivator;
      } else if (selected_ < lineage_->pastCount) {
        openPastLife(lineage_->pastLives[selected_]);
        return true;
      }
      break;

    default:
      break;
  }
  requestUpdate();
  return true;
}

// --- Past lives -----------------------------------------------------------------------------------

void CultivationActivity::openPastLives() {
  {
    RenderLock lock(*this);
    if (!lineage_) lineage_ = makeUniqueNoThrow<LineageView>();
    if (!lineage_) return;
    LineageView& v = *lineage_;
    v.pastCount = 0;
    const uint16_t ended = lineage::livesEnded(*state_);
    for (uint16_t life = ended; life >= 1 && v.pastCount < LineageView::kPastShown; --life) {
      char path[64];
      chronicle::lifePath(life, path, sizeof(path));
      if (!Storage.exists(path)) continue;
      if (!chronicle::lifeName(path, v.pastNames[v.pastCount], sizeof(v.pastNames[v.pastCount]))) {
        v.pastNames[v.pastCount][0] = '\0';
      }
      v.pastLives[v.pastCount++] = life;
    }
    notice_[0] = '\0';
    selected_ = 0;
    screen_ = Screen::PastLives;
  }
  requestUpdate();
}

void CultivationActivity::openPastLife(const uint16_t life) {
  {
    RenderLock lock(*this);
    lineage_.reset();
    if (!chronicle_) chronicle_ = makeUniqueNoThrow<ChronicleView>();
    if (!chronicle_) {
      screen_ = Screen::Cultivator;
      return;
    }
    chronicle::lifePath(life, chroniclePath_, sizeof(chroniclePath_));
    chronicleLife_ = life;
    ChronicleView& v = *chronicle_;
    const int savedSelected = v.savedSelected;
    v = ChronicleView{};
    v.savedSelected = savedSelected;
    notice_[0] = '\0';
    FsFile f;
    if (Storage.openFileForRead("CULT", chroniclePath_, f)) {
      v.size = static_cast<uint32_t>(f.fileSize());
      v.starts[0] = chronicleNewestStart(f, v.size);
      v.age = chronicle::pageAge(f, v.size, v.starts[0]);
      f.close();
    }
    screen_ = Screen::Chronicle;
  }
  requestUpdate();
}

// The new Chronicle's paragraph about the life before ("" for a first life).
void CultivationActivity::pastLifeLine(char* out, const size_t len) const {
  out[0] = '\0';
  const int realm = lineage::previousRealm(*state_);
  if (realm < 0) return;
  char name[kNameMaxBytes + 1];
  lineage::previousName(*state_, name, sizeof(name));
  const int kind = static_cast<int>(lineage::lastEnd(*state_));
  snprintf(out, len, ctr(CULT_PAST_LIFE_LINE_FORMAT), name, ui::realmName(static_cast<uint8_t>(realm)),
           gameText(offset(GameStr::CULT_PAST_VERB_0, kind < 3 ? kind : 0)),
           static_cast<unsigned>(lineage::previousAge(*state_)));
}

// --- Mementos ------------------------------------------------------------------------------------

void CultivationActivity::appendMemento(const uint8_t memento, const bool duplicate, const uint16_t stones, char* out,
                                        const size_t len) const {
  if (memento >= codex::kMementoCount || len == 0) return;
  char part[160];
  if (duplicate) {
    snprintf(part, sizeof(part), ctr(CULT_MEMENTO_AGAIN_FORMAT), mementoName(memento), static_cast<unsigned>(stones));
  } else {
    snprintf(part, sizeof(part), ctr(CULT_MEMENTO_FOUND_FORMAT), mementoName(memento),
             rarityWord(codex::rarityOf(memento)));
  }
  const size_t used = strlen(out);
  if (used + 1 >= len) return;
  snprintf(out + used, len - used, "%s%s", used > 0 ? ", " : "", part);
}

void CultivationActivity::mementoChronicle(const uint8_t memento) {
  if (memento >= codex::kMementoCount) return;
  lifelog::Line line;
  line.kind = lifelog::LineKind::Event;
  line.season = lifelog::seasonOf(state_->progress.ageMonths);
  snprintf(line.text, sizeof(line.text), ctr(CULT_MEMENTO_CHRONICLE_FORMAT), mementoName(memento));
  if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
  chronicle::appendLines(&line, 1);
  chronicleExists_ = chronicle::exists();
  if (packs_) packs_->closeFile();
}

void CultivationActivity::noteMementos(const explore::StepResult& r) {
  for (const explore::Reward* reward : {&r.reward, &r.clearReward}) {
    if (reward->memento != codex::kNone && !reward->mementoDuplicate) mementoChronicle(reward->memento);
  }
}

// --- Drawing -------------------------------------------------------------------------------------

void CultivationActivity::lineageLabels(const char*& confirmLabel, const char*& upLabel, const char*& downLabel) const {
  const int rows = lineageRows();
  upLabel = rows > 1 ? tr(STR_DIR_UP) : "";
  downLabel = rows > 1 ? tr(STR_DIR_DOWN) : "";
  switch (screen_) {
    case Screen::LineageConfirm:
      confirmLabel = lineage_ && lineage_->confirmStep > 0 ? ctr(CULT_LINEAGE_YES) : ctr(CULT_LINEAGE_END);
      break;
    case Screen::Echo:
      confirmLabel = lineage_ && selected_ < lineage_->candidateCount ? ctr(CULT_ECHO_PICK) : ctr(CULT_ECHO_DONE);
      break;
    case Screen::Perks:
      confirmLabel = isRivalRow(*state_, selected_)                                    ? ctr(CULT_PERKS_CHANGE)
                     : selected_ >= lineage::kPerkCount                                ? ctr(CULT_CONTINUE)
                     : lineage::canBuy(*state_, static_cast<lineage::Perk>(selected_)) ? ctr(CULT_PERKS_BUY)
                                                                                       : "";
      break;
    case Screen::Collection:
      confirmLabel = ctr(CULT_COLLECTION_OPEN);
      break;
    case Screen::CollectionPage:
      confirmLabel = "";
      break;
    case Screen::PastLives:
      confirmLabel = rows > 0 ? ctr(CULT_GUIDE_READ) : "";
      break;
    default:
      confirmLabel = rows > 0 ? tr(STR_SELECT) : "";
      break;
  }
}

void CultivationActivity::renderLineage(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[320];
  const int gap = metrics.verticalSpacing;
  if (lineage_ && lineage_->oldAge) {
    char name[kNameMaxBytes + 1];
    nameOf(s, name);
    snprintf(line, sizeof(line), ctr(CULT_LINEAGE_OLD_AGE_FORMAT), name, static_cast<unsigned>(ageYears(s)),
             ui::realmName(s.progress.realm), static_cast<unsigned>(lineage::imprintsFor(s)),
             plural(lineage::imprintsFor(s)));
    y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 6) + gap;
  } else {
    if (lineage::inUse(s)) {
      const int best = lineage::bestRealm(s);
      snprintf(line, sizeof(line), ctr(CULT_LINEAGE_LIVES_FORMAT), static_cast<unsigned>(lineage::livesEnded(s)),
               best >= 0 ? ui::realmName(static_cast<uint8_t>(best)) : "-");
      y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
    } else {
      y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_LINEAGE_FIRST), 2);
    }
    const uint16_t bonus = lineage::qiBonusPermille(s);
    if (bonus > 0) {
      snprintf(line, sizeof(line), ctr(CULT_LINEAGE_BONUS_FORMAT), static_cast<unsigned>(bonus / 10));
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 3);
    } else {
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, ctr(CULT_LINEAGE_NO_BONUS), 2);
    }
    snprintf(line, sizeof(line), ctr(CULT_LINEAGE_IMPRINTS_FORMAT), static_cast<unsigned>(lineage::imprints(s)),
             static_cast<unsigned>(lineage::canReincarnate(s) ? lineage::imprintsFor(s) : 0u));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    snprintf(line, sizeof(line), ctr(CULT_LINEAGE_YEARS_FORMAT), static_cast<unsigned>(lineage::lifespanYears(s)),
             static_cast<unsigned>(lineage::yearsLeft(s)));
    y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 2);
    // Perks owned: "Fated 2, Hidden Purse 1".
    char perks[200] = {};
    for (uint8_t p = 0; p < lineage::kPerkCount; ++p) {
      const uint8_t r = lineage::rank(s, static_cast<lineage::Perk>(p));
      if (r == 0) continue;
      const size_t used = strlen(perks);
      snprintf(perks + used, sizeof(perks) - used, "%s%s %u", used > 0 ? ", " : "",
               gameText(offset(GameStr::CULT_PERK_0, p)), static_cast<unsigned>(r));
    }
    if (perks[0] != '\0') {
      snprintf(line, sizeof(line), ctr(CULT_LINEAGE_PERKS_FORMAT), perks);
      y = drawWrapped(UI_10_FONT_ID, x, y + 4, width, line, 3);
    }
    if (!lineage::canReincarnate(s))
      y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_LINEAGE_REINCARNATE_LATER), 2);
    y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_LINEAGE_KEEP), 3) + gap;
  }
  LineageRow rows[4];
  const uint8_t n = lineageRowList(rows);
  const int height = metrics.listRowHeight * n;
  const int top = bottom - height > y ? bottom - height : y;
  GUI.drawList(renderer, Rect{0, top, renderer.getScreenWidth(), bottom - top}, n, selected_,
               [this](const int i) -> std::string {
                 LineageRow r[4];
                 const uint8_t count = lineageRowList(r);
                 if (i >= count) return "";
                 switch (r[i]) {
                   case LineageRow::Collection:
                     return ctr(CULT_LINEAGE_ROW_COLLECTION);
                   case LineageRow::Achievements: {  // v0.3.8
                     char row[80];
                     snprintf(row, sizeof(row),
                              achievements::unseen(*state_) ? ctr(CULT_LINEAGE_ROW_ACH_NEW_FORMAT)
                                                            : ctr(CULT_LINEAGE_ROW_ACH_FORMAT),
                              static_cast<unsigned>(achievements::earnedCount(*state_)),
                              static_cast<unsigned>(achievements::kCount));
                     return row;
                   }
                   case LineageRow::Reincarnate:
                     return ctr(CULT_LINEAGE_ROW_REINCARNATE);
                   case LineageRow::Abandon:
                     return ctr(CULT_LINEAGE_ROW_ABANDON);
                   default:
                     return ctr(CULT_LINEAGE_ROW_NEW_LIFE);
                 }
               });
}

void CultivationActivity::renderLineageExport(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_LINEAGE_EXPORT_ASK), 5) + metrics.verticalSpacing * 2;
  const int height = metrics.listRowHeight * 2;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), y + height < bottom ? height : bottom - y}, 2, selected_,
      [](const int i) -> std::string { return i == 0 ? ctr(CULT_LINEAGE_ROW_EXPORT) : ctr(CULT_LINEAGE_ROW_SKIP); });
}

void CultivationActivity::renderLineageConfirm(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!lineage_) return;
  char line[240];
  if (lineage_->confirmStep > 0) {
    snprintf(line, sizeof(line), "%s", ctr(CULT_LINEAGE_CONFIRM_SURE));
  } else if (lineage_->kind == lineage::EndKind::Abandoned) {
    snprintf(line, sizeof(line), "%s", ctr(CULT_LINEAGE_CONFIRM_ABANDON));
  } else {
    const unsigned n = lineage::imprintsFor(*state_);
    snprintf(line, sizeof(line), ctr(CULT_LINEAGE_CONFIRM_REINCARNATE_FORMAT), n, plural(n));
  }
  y = drawWrapped(UI_12_FONT_ID, x, y, width, line, 6) + metrics.verticalSpacing * 2;
  if (notice_[0] != '\0') drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 4);
  (void)bottom;
}

void CultivationActivity::renderEcho(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!lineage_) return;
  char line[200];
  snprintf(line, sizeof(line), ctr(CULT_ECHO_HINT_FORMAT),
           static_cast<unsigned>(lineage::rank(*state_, lineage::Perk::Echo)),
           static_cast<unsigned>(lineage_->chosenCount));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, y, renderer.getScreenWidth(), bottom - y}, lineage_->candidateCount + 1, selected_,
               [this](const int i) -> std::string {
                 const LineageView& v = *lineage_;
                 if (i >= v.candidateCount) return ctr(CULT_ECHO_DONE);
                 bool chosen = false;
                 for (uint8_t k = 0; k < v.chosenCount; ++k) chosen = chosen || v.chosen[k] == v.candidates[i];
                 char row[96];
                 snprintf(row, sizeof(row), chosen ? ctr(CULT_ECHO_CHOSEN_FORMAT) : ctr(CULT_ECHO_OPEN_FORMAT),
                          v.names[i]);
                 return row;
               });
}

void CultivationActivity::renderPerks(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  char line[200];
  if (notice_[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, notice_, 3) + 4;
  snprintf(line, sizeof(line), ctr(CULT_PERKS_HEADER_FORMAT), static_cast<unsigned>(lineage::imprints(*state_)));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  if (saveFailed_) y = drawWrapped(SMALL_FONT_ID, x, y + 4, width, ctr(CULT_SAVE_FAILED), 2);
  y += metrics.verticalSpacing;
  // The selected perk's text under the list.
  const int detailLines = 3;
  const int detailTop = bottom - renderer.getLineHeight(SMALL_FONT_ID) * detailLines;
  GUI.drawList(renderer, Rect{0, y, renderer.getScreenWidth(), detailTop - metrics.verticalSpacing - y},
               perkRows(*state_), selected_, [this](const int i) -> std::string {
                 if (isRivalRow(*state_, i)) {
                   return lineage::keepsRival(*state_) ? ctr(CULT_PERKS_RIVAL_KEEP) : ctr(CULT_PERKS_RIVAL_NEW);
                 }
                 if (i >= lineage::kPerkCount) return ctr(CULT_PERKS_BEGIN);
                 const auto perk = static_cast<lineage::Perk>(i);
                 char row[96];
                 snprintf(row, sizeof(row), ctr(CULT_PERKS_ROW_FORMAT), gameText(offset(GameStr::CULT_PERK_0, i)),
                          static_cast<unsigned>(lineage::rank(*state_, perk)),
                          static_cast<unsigned>(lineage::perkInfo(perk).maxRank));
                 return row;
               });
  if (selected_ < lineage::kPerkCount) {
    const auto perk = static_cast<lineage::Perk>(selected_);
    char cost[64];
    if (lineage::rank(*state_, perk) >= lineage::perkInfo(perk).maxRank) {
      snprintf(cost, sizeof(cost), "%s", ctr(CULT_PERKS_MAXED));
    } else {
      const unsigned c = lineage::perkInfo(perk).cost;
      snprintf(cost, sizeof(cost), ctr(CULT_PERKS_COST_FORMAT), c, plural(c));
    }
    snprintf(line, sizeof(line), "%s %s", gameText(offset(GameStr::CULT_PERK_0_DESC, selected_)), cost);
    drawWrapped(SMALL_FONT_ID, x, detailTop, width, line, detailLines);
  } else if (isRivalRow(*state_, selected_)) {
    drawWrapped(SMALL_FONT_ID, x, detailTop, width, ctr(CULT_PERKS_RIVAL_DESC), detailLines);
  }
}

void CultivationActivity::renderCollection(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const GameState& s = *state_;
  char line[200];
  uint16_t codexFound = 0;
  for (uint8_t g = 0; g < static_cast<uint8_t>(codex::Group::Count); ++g) {
    codexFound = static_cast<uint16_t>(codexFound + codex::groupFound(s, static_cast<codex::Group>(g)));
  }
  snprintf(line, sizeof(line), ctr(CULT_COLLECTION_SUMMARY_FORMAT), static_cast<unsigned>(codex::mementosFound(s)),
           static_cast<unsigned>(codex::mementosOfRarity(s, codex::Rarity::Common)),
           static_cast<unsigned>(codex::mementosOfRarity(s, codex::Rarity::Rare)),
           static_cast<unsigned>(codex::mementosOfRarity(s, codex::Rarity::Legendary)),
           static_cast<unsigned>(codexFound), static_cast<unsigned>(codex::kCodexEntries));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, y, renderer.getScreenWidth(), bottom - y}, kCollectionRows, selected_,
               [this, codexFound](const int i) -> std::string {
                 char row[96];
                 if (i == 0) {
                   snprintf(row, sizeof(row), ctr(CULT_COLLECTION_CODEX_FORMAT), static_cast<unsigned>(codexFound),
                            static_cast<unsigned>(codex::kCodexEntries));
                 } else {
                   snprintf(row, sizeof(row), ctr(CULT_COLLECTION_SET_FORMAT),
                            gameText(offset(GameStr::CULT_SET_0, i - 1)),
                            static_cast<unsigned>(codex::mementosInSet(*state_, static_cast<uint8_t>(i - 1))));
                 }
                 return row;
               });
}

void CultivationActivity::renderCollectionPage(const int x, int y, const int width, const int bottom) {
  if (!lineage_) return;
  const GameState& s = *state_;
  char line[200];
  const int textLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  if (lineage_->page == 0) {
    y = drawWrapped(SMALL_FONT_ID, x, y, width, ctr(CULT_CODEX_HINT), 4) + 8;
    for (uint8_t g = 0; g < static_cast<uint8_t>(codex::Group::Count) && y + textLine <= bottom; ++g) {
      const auto group = static_cast<codex::Group>(g);
      snprintf(line, sizeof(line), ctr(CULT_CODEX_ROW_FORMAT), gameText(offset(GameStr::CULT_CODEX_G0, g)),
               static_cast<unsigned>(codex::groupFound(s, group)), static_cast<unsigned>(codex::groupInfo(group).size));
      renderer.drawText(UI_10_FONT_ID, x, y, line);
      y += textLine + 6;
    }
    return;
  }
  const uint8_t set = static_cast<uint8_t>(lineage_->page - 1);
  for (uint8_t i = 0; i < codex::kSetSize && y + textLine <= bottom; ++i) {
    const auto m = static_cast<uint8_t>(set * codex::kSetSize + i);
    if (codex::hasMemento(s, m)) {
      snprintf(line, sizeof(line), ctr(CULT_MEMENTO_ROW_FORMAT), mementoName(m), rarityWord(codex::rarityOf(m)));
      renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
      y += textLine;
      const int room = (bottom - y) / smallLine;
      y = drawWrapped(SMALL_FONT_ID, x, y, width, mementoDescription(m), room < 2 ? room : 2) + 6;
    } else {
      renderer.drawText(UI_10_FONT_ID, x, y, ctr(CULT_MEMENTO_MISSING));
      y += textLine + 6;
    }
  }
}

void CultivationActivity::renderPastLives(const int x, const int y, const int width, const int bottom) {
  if (!lineage_ || lineage_->pastCount == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_PAST_NONE), 4);
    return;
  }
  GUI.drawList(renderer, Rect{0, y, renderer.getScreenWidth(), bottom - y}, lineage_->pastCount, selected_,
               [this](const int i) -> std::string {
                 const LineageView& v = *lineage_;
                 char row[64];
                 if (v.pastNames[i][0] != '\0') {
                   snprintf(row, sizeof(row), ctr(CULT_PAST_LIFE_FORMAT), static_cast<unsigned>(v.pastLives[i]),
                            v.pastNames[i]);
                 } else {
                   snprintf(row, sizeof(row), ctr(CULT_PAST_LIFE_TITLE_FORMAT), static_cast<unsigned>(v.pastLives[i]));
                 }
                 return row;
               });
}

#endif  // CROSSINK_CULTIVATION
