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
#include "CultivatorSleepScreen.h"
#include "GameText.h"
#include "GameValues.h"
#include "Rng.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Exploration (v0.1.10): the Regions list, a region's 6x6 map under fog,
// fights, wandering merchants, the bag and the Region Talisman. The rules are
// in Exploration.{h,cpp} and Inventory.{h,cpp}; this file shows them, writes
// their Chronicle lines and saves after every action. Text is generated in
// the input loop, under the render lock, never while drawing.

using namespace cultivation;

namespace {

constexpr uint32_t kKindMapText = 91;  // contentSeed kinds 80-99: exploration
constexpr uint32_t kKindFightText = 92;
constexpr uint32_t kKindExploreChronicle = 93;
constexpr uint32_t kKindAlchemyChronicle = 103;  // 100-119: alchemy

constexpr int kMaxCell = 72;  // the agreed mockup drew 76 px; 72 leaves room for three lines of news

const char* text(const GameStr id) { return gameText(id); }

static const GameStr kDangerNames[static_cast<int>(explore::Danger::Count)] = {
    GameStr::CULT_DANGER_TRIVIAL,   GameStr::CULT_DANGER_WEAK,    GameStr::CULT_DANGER_DANGEROUS,
    GameStr::CULT_DANGER_HAZARDOUS, GameStr::CULT_DANGER_EXTREME, GameStr::CULT_DANGER_IMPOSSIBLE};

static const GameStr kTileNames[static_cast<int>(explore::Tile::Count)] = {
    GameStr::CULT_TILE_CRAG,  GameStr::CULT_TILE_PATH,    GameStr::CULT_TILE_HERB, GameStr::CULT_TILE_ORE,
    GameStr::CULT_TILE_RUIN,  GameStr::CULT_TILE_CAVE,    GameStr::CULT_TILE_LAIR, GameStr::CULT_TILE_MERCHANT,
    GameStr::CULT_TILE_QUIET, GameStr::CULT_TILE_GUARDIAN};

const char* dangerName(const explore::Danger d) {
  const int i = static_cast<int>(d);
  return text(kDangerNames[i < static_cast<int>(explore::Danger::Count) ? i : 0]);
}

// Appends ", part" (or "part" first) to out.
void appendPart(char* out, const size_t len, const char* part) {
  if (part == nullptr || part[0] == '\0') return;
  const size_t used = strlen(out);
  if (used + 1 >= len) return;
  snprintf(out + used, len - used, "%s%s", used > 0 ? ", " : "", part);
}

// Appends " sentence" to out.
void appendSentence(char* out, const size_t len, const char* sentence) {
  if (sentence == nullptr || sentence[0] == '\0') return;
  const size_t used = strlen(out);
  if (used + 1 >= len) return;
  snprintf(out + used, len - used, "%s%s", used > 0 ? " " : "", sentence);
}

// "The Mistveil Marshes" from "the Mistveil Marshes", for titles.
void capitalized(const char* in, char* out, const size_t len) {
  snprintf(out, len, "%s", in);
  if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
}

// --- 1-bit drawing for the map: no bitmaps in flash, a few pixels each ---------

void fillDisc(const GfxRenderer& r, const int cx, const int cy, const int radius, const bool black = true) {
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= radius * radius) r.drawPixel(cx + dx, cy + dy, black);
    }
  }
}

void ring(const GfxRenderer& r, const int cx, const int cy, const int outer, const int inner) {
  for (int dy = -outer; dy <= outer; ++dy) {
    for (int dx = -outer; dx <= outer; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 <= outer * outer && d2 > inner * inner) r.drawPixel(cx + dx, cy + dy);
    }
  }
}

// Fog: diagonal hatching, as in the mockup.
void hatch(const GfxRenderer& r, const int x, const int y, const int c) {
  for (int k = -(c - 1); k < c; k += 7) {
    const int u0 = k > 0 ? k : 0;
    const int u1 = k < 0 ? c - 1 + k : c - 1;
    r.drawLine(x + u0, y + u0 - k, x + u1, y + u1 - k);
  }
}

// Crags: a dotted background and two peaks.
void crag(const GfxRenderer& r, const int x, const int y, const int c) {
  for (int v = 3; v < c - 1; v += 5) {
    for (int u = 3 + (v / 5 % 2) * 2; u < c - 1; u += 5) r.drawPixel(x + u, y + v);
  }
  const int cx = x + c / 2;
  const int cy = y + c / 2;
  for (int i = 0; i < 2; ++i) {  // white under each peak, then its outline
    const int ax = i == 0 ? cx - 8 : cx + 9;
    const int ay = i == 0 ? cy - 18 : cy - 9;
    const int left = i == 0 ? cx - 24 : cx - 5;
    const int right = i == 0 ? cx + 8 : cx + 24;
    const int base = cy + 16;
    const int xs[3] = {ax, left, right};
    const int ys[3] = {ay, base, base};
    r.fillPolygon(xs, ys, 3, false);
    r.drawLine(ax, ay, left, base, 2, true);
    r.drawLine(ax, ay, right, base, 2, true);
    r.drawLine(left, base, right, base, 2, true);
  }
}

void dottedFrame(const GfxRenderer& r, const int x, const int y, const int w, const int h) {
  for (int u = 0; u < w; u += 4) {
    r.drawPixel(x + u, y);
    r.drawPixel(x + u, y + h - 1);
  }
  for (int v = 0; v < h; v += 4) {
    r.drawPixel(x, y + v);
    r.drawPixel(x + w - 1, y + v);
  }
}

// The content icons, about 28 px, centred on (cx, cy).
void drawIcon(const GfxRenderer& r, const explore::Tile t, const int cx, const int cy) {
  using explore::Tile;
  switch (t) {
    case Tile::Path:  // footprints: two ovals 6 x 10 px
      for (int i = 0; i < 2; ++i) {
        const int fx = i == 0 ? cx - 6 : cx + 6;
        const int fy = i == 0 ? cy + 5 : cy - 5;
        for (int dy = -5; dy <= 5; ++dy) {
          for (int dx = -3; dx <= 3; ++dx) {
            if (dx * dx * 25 + dy * dy * 9 <= 225) r.drawPixel(fx + dx, fy + dy);
          }
        }
      }
      return;
    case Tile::Herb:  // a sprout: stem and two leaves
      r.drawLine(cx - 1, cy - 4, cx - 1, cy + 13, 2, true);
      r.drawLine(cx - 8, cy + 13, cx + 6, cy + 13, 2, true);
      ring(r, cx - 7, cy - 8, 6, 3);
      ring(r, cx + 7, cy - 4, 6, 3);
      return;
    case Tile::Ore: {  // a crystal
      const int xs[5] = {cx - 8, cx + 8, cx + 13, cx, cx - 13};
      const int ys[5] = {cy - 12, cy - 12, cy - 3, cy + 13, cy - 3};
      for (int i = 0; i < 5; ++i) r.drawLine(xs[i], ys[i], xs[(i + 1) % 5], ys[(i + 1) % 5], 2, true);
      r.drawLine(cx - 13, cy - 3, cx + 13, cy - 3, 2, true);
      r.drawLine(cx - 4, cy - 12, cx, cy + 13, 1, true);
      r.drawLine(cx + 4, cy - 12, cx, cy + 13, 1, true);
      return;
    }
    case Tile::Ruin:  // broken pillars on a base
      r.fillRect(cx - 15, cy + 9, 30, 4);
      r.fillRect(cx - 13, cy - 11, 7, 20);
      r.fillRect(cx - 3, cy + 1, 7, 8);
      r.fillRect(cx + 7, cy - 5, 7, 14);
      return;
    case Tile::Cave:  // an arch
      for (int dy = -14; dy <= 0; ++dy) {
        for (int dx = -14; dx <= 14; ++dx) {
          const int d2 = dx * dx + dy * dy;
          if (d2 <= 14 * 14 && d2 > 7 * 7) r.drawPixel(cx + dx, cy + 2 + dy);
        }
      }
      r.fillRect(cx - 14, cy + 2, 7, 11);
      r.fillRect(cx + 8, cy + 2, 7, 11);
      return;
    case Tile::Lair:  // claw marks
      for (int i = 0; i < 3; ++i) r.drawLine(cx - 11 + 7 * i, cy - 12, cx - 3 + 7 * i, cy + 12, 3, true);
      return;
    case Tile::Merchant:  // a coin with a square hole
      fillDisc(r, cx, cy, 13);
      r.fillRect(cx - 4, cy - 4, 9, 9, false);
      return;
    case Tile::Quiet:  // ripples: a dot and two rings, 2.5 times wider than tall
      for (int dy = -7; dy <= 7; ++dy) {
        for (int dx = -16; dx <= 16; ++dx) {
          const int e = 4 * dx * dx + 25 * dy * dy;  // = 4 R^2 on an ellipse of half-width R
          const bool dot = e <= 4 * 3 * 3;
          const bool middle = e <= 4 * 9 * 9 && e > 4 * 7 * 7;
          const bool outer = e <= 4 * 16 * 16 && e > 4 * 14 * 14;
          if (dot || middle || outer) r.drawPixel(cx + dx, cy + dy);
        }
      }
      return;
    case Tile::Guardian: {  // a horned mask
      ring(r, cx, cy + 3, 11, 8);
      const int lx[3] = {cx - 9, cx - 15, cx - 3};
      const int ly[3] = {cy - 3, cy - 15, cy - 7};
      r.fillPolygon(lx, ly, 3);
      const int rx[3] = {cx + 9, cx + 15, cx + 3};
      r.fillPolygon(rx, ly, 3);
      r.fillRect(cx - 6, cy + 1, 4, 3);
      r.fillRect(cx + 3, cy + 1, 4, 3);
      r.fillRect(cx - 3, cy + 8, 7, 2);
      return;
    }
    default:
      return;
  }
}

bool revealed(const GameState& s, const uint8_t region, const uint8_t tile) {
  if (explore::charted(s, region, tile)) return true;
  uint8_t n[4];
  const uint8_t c = explore::neighbours(tile, n);
  for (uint8_t i = 0; i < c; ++i) {
    if (explore::charted(s, region, n[i])) return true;
  }
  return false;
}

}  // namespace

// --- Opening and closing --------------------------------------------------------

void CultivationActivity::openRegions() {
  auto view = makeUniqueNoThrow<ExploreView>();
  if (!view) {
    LOG_ERR("CULT", "Out of memory for exploration (%u bytes)", static_cast<unsigned>(sizeof(ExploreView)));
    return;
  }
  {
    RenderLock lock(*this);
    explore_ = std::move(view);
    notice_[0] = '\0';
    loadRegionNames();
    loadIngredientNames();
    selected_ = 0;
    screen_ = Screen::Regions;
  }
  requestUpdate();
}

// Names and charting goals of the open regions. Caller holds the render lock.
void CultivationActivity::loadRegionNames() {
  ExploreView& v = *explore_;
  v.regionCount = explore::unlockedCount(*state_);
  for (uint8_t i = 0; i < v.regionCount; ++i) {
    v.walkable[i] = explore::layoutOf(*state_, i).walkable;
    if (!grammar_ || !exploretext::regionName(*state_, i, *grammar_, v.regionNames[i], sizeof(v.regionNames[i]))) {
      snprintf(v.regionNames[i], sizeof(v.regionNames[i]), ctr(CULT_REGION_FALLBACK_FORMAT),
               static_cast<unsigned>(i + 1));
    }
  }
  if (packs_) packs_->closeFile();
}

void CultivationActivity::closeExplore() {
  {
    RenderLock lock(*this);
    explore_.reset();
    if (!alchemy_) ingredients_.reset();
    notice_[0] = '\0';
    refreshActions();
    selected_ = 0;
    for (uint8_t i = 0; i < actionCount_; ++i) {
      if (actions_[i] == Action::Explore) selected_ = i;
    }
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

int CultivationActivity::regionRows() const {
  if (!explore_) return 0;
  int rows = explore_->regionCount + 1;  // + the Bag
  for (uint8_t i = 0; i < explore_->regionCount; ++i) {
    if (explore::canUseRegionTalisman(*state_, i)) return rows + 1;
  }
  return rows;
}

void CultivationActivity::openMap(uint8_t region) {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    // A fight left waiting on another map comes first.
    const int pending = explore::waitingRegion(*state_);  // v0.2.2: a meeting waits like a fight
    if (pending >= 0 && pending < v.regionCount) region = static_cast<uint8_t>(pending);
    v.region = region;
    v.layout = explore::layoutOf(*state_, region);
    const bool first = explore::chartedCount(*state_, region) == 0;
    explore::enter(*state_, v.layout, region);
    if (first) persist();
    v.mapOpen = true;
    v.cursor = 0;
    v.text[0] = '\0';
    v.numbers[0] = '\0';
    notice_[0] = '\0';
    refreshCursor();
    screen_ = Screen::Map;
  }
  if (explore::pendingRegion(*state_) == explore_->region) {
    openFight();
  } else if (explore::meetingRegion(*state_) == explore_->region) {
    openMeeting();
  } else {
    requestUpdate();
  }
}

// The cursor moves between the player's tile and its walkable neighbours
// (north, east, south, west). Caller holds the render lock.
void CultivationActivity::refreshCursor() {
  ExploreView& v = *explore_;
  const uint8_t here = explore::position(*state_, v.layout, v.region);
  const uint8_t previous = v.cursorCount > 0 && v.cursor < v.cursorCount ? v.cursorTiles[v.cursor] : here;
  v.cursorCount = 0;
  v.cursorTiles[v.cursorCount++] = here;
  if (explore::waitingRegion(*state_) < 0) {
    uint8_t n[4];
    const uint8_t c = explore::neighbours(here, n);
    for (uint8_t i = 0; i < c; ++i) {
      if (explore::walkable(v.layout, n[i])) v.cursorTiles[v.cursorCount++] = n[i];
    }
  }
  v.cursor = 0;
  for (uint8_t i = 0; i < v.cursorCount; ++i) {
    if (v.cursorTiles[i] == previous) v.cursor = i;
  }
}

bool CultivationActivity::canResolveHere() const {
  const ExploreView& v = *explore_;
  const uint8_t here = explore::position(*state_, v.layout, v.region);
  const explore::Tile t = v.layout.tiles[here];
  using explore::Tile;
  if (t == Tile::Merchant) return true;
  const bool content = t == Tile::Herb || t == Tile::Ore || t == Tile::Ruin || t == Tile::Cave || t == Tile::Quiet;
  return content && !explore::used(*state_, v.region, here);
}

// Confirm on the map: step to the selected neighbour, trade or take what is
// here, face a waiting fight, or open the bag.
void CultivationActivity::mapConfirm() {
  ExploreView& v = *explore_;
  if (explore::pendingRegion(*state_) == v.region) {
    openFight();
    return;
  }
  if (explore::meetingRegion(*state_) == v.region) {
    openMeeting();
    return;
  }
  const uint8_t here = explore::position(*state_, v.layout, v.region);
  const uint8_t target = v.cursor < v.cursorCount ? v.cursorTiles[v.cursor] : here;
  if (target == here) {
    if (v.layout.tiles[here] == explore::Tile::Merchant) {
      openMerchant(here);
    } else if (canResolveHere()) {
      explore::StepResult r;
      {
        RenderLock lock(*this);
        r = explore::resolveHere(*state_, v.layout, v.region);
        showStep(r);
        persist();
      }
      requestUpdate();
    } else {
      openBag(Screen::Map);
    }
    return;
  }
  if (explore::canStep(*state_, v.layout, v.region, target) != explore::StepError::None) {
    requestUpdate();  // the info lines say why (no stamina, no HP)
    return;
  }
  bool fight = false;
  bool meeting = false;
  {
    RenderLock lock(*this);
    const explore::StepResult r = explore::step(*state_, v.layout, v.region, target);
    fight = r.fight;
    meeting = r.meeting;
    if (!fight && !meeting) showStep(r);
    persist();
    refreshCursor();
    // Follow the player: the cursor stays on the tile just entered.
    for (uint8_t i = 0; i < v.cursorCount; ++i) {
      if (v.cursorTiles[i] == explore::position(*state_, v.layout, v.region)) v.cursor = i;
    }
  }
  if (fight) {
    openFight();
  } else if (meeting) {
    openMeeting();
  } else {
    requestUpdate();
  }
}

// The text for what a step or a tile gave, and its Chronicle line when the
// region was cleared. Caller holds the render lock.
void CultivationActivity::showStep(const explore::StepResult& r) {
  ExploreView& v = *explore_;
  const uint8_t here = explore::position(*state_, v.layout, v.region);
  v.text[0] = '\0';
  v.numbers[0] = '\0';
  const char* list = nullptr;
  switch (r.outcome) {
    case explore::TileOutcome::Nothing:
      if (v.layout.tiles[here] == explore::Tile::Path) list = "explore.path";
      break;
    case explore::TileOutcome::Gathered:
      list = v.layout.tiles[here] == explore::Tile::Ore ? "explore.ore" : "explore.herb";
      break;
    case explore::TileOutcome::RuinWon:
      list = "explore.ruin.won";
      break;
    case explore::TileOutcome::RuinTrapped:
      list = "explore.ruin.trap";
      break;
    case explore::TileOutcome::CaveFound:
      list = "explore.cave";
      break;
    case explore::TileOutcome::Rested:
      list = "explore.quiet";
      break;
    case explore::TileOutcome::RestedFull:
      list = "explore.quiet.full";
      break;
    case explore::TileOutcome::Merchant:
      list = "explore.merchant";
      break;
  }
  if (list != nullptr && grammar_) {
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindMapText, mix32(explore::stepCounter(*state_), here));
    exploretext::flavor(*state_, list, seed, v.regionNames[v.region], nullptr, *grammar_, v.text, sizeof(v.text));
    if (packs_) packs_->closeFile();
  }
  formatReward(r.reward, r.hpLost, r.hpHealed, v.numbers, sizeof(v.numbers));
  if (r.reward.recipe >= 0) alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(r.reward.recipe));
  if (r.clearedNow) {
    char reward[120] = {};
    formatReward(r.clearReward, 0, 0, reward, sizeof(reward));
    char line[200];
    snprintf(line, sizeof(line), ctr(CULT_CLEARED_FORMAT), reward);
    appendSentence(v.numbers, sizeof(v.numbers), line);
    exploreChronicle("explore.line.clear", v.region, nullptr, 1);
  }
}

// "+45 Spirit Stones, +2 Common herb, -10 HP".
void CultivationActivity::formatReward(const explore::Reward& r, const uint16_t hpLost, const uint16_t hpHealed,
                                       char* out, const size_t len) const {
  char part[96];
  char name[64];
  if (r.stones > 0) {
    snprintf(part, sizeof(part), ctr(CULT_GAINED_STONES_FORMAT), static_cast<unsigned long>(r.stones));
    appendPart(out, len, part);
  }
  if (r.item != inventory::Item::None && r.itemCount > 0) {
    snprintf(part, sizeof(part), ctr(CULT_GAINED_ITEM_FORMAT), static_cast<unsigned>(r.itemCount),
             itemLabel(r.item, name, sizeof(name)));
    appendPart(out, len, part);
  }
  if (r.item2 != inventory::Item::None && r.item2Count > 0) {
    snprintf(part, sizeof(part), ctr(CULT_GAINED_ITEM_FORMAT), static_cast<unsigned>(r.item2Count),
             itemLabel(r.item2, name, sizeof(name)));
    appendPart(out, len, part);
  }
  if (r.item3 != inventory::Item::None && r.item3Count > 0) {
    snprintf(part, sizeof(part), ctr(CULT_GAINED_ITEM_FORMAT), static_cast<unsigned>(r.item3Count),
             itemLabel(r.item3, name, sizeof(name)));
    appendPart(out, len, part);
  }
  if (r.soldStones > 0) {  // v0.2.1: the bag held 9,999 already
    snprintf(part, sizeof(part), ctr(CULT_REWARD_SOLD_FORMAT), static_cast<unsigned long>(r.soldStones));
    appendPart(out, len, part);
  }
  if (r.lost > 0) {
    snprintf(part, sizeof(part), ctr(CULT_REWARD_KEPT_FORMAT), static_cast<unsigned>(r.lost));
    appendPart(out, len, part);
  }
  if (r.recipe >= 0 && r.recipe < alchemy::kRecipeCount) {
    snprintf(part, sizeof(part), ctr(CULT_GAINED_RECIPE_FORMAT),
             pillLabel(alchemy::recipesOf(*state_).recipes[r.recipe].pill));
    appendPart(out, len, part);
  }
  if (hpLost > 0) {
    snprintf(part, sizeof(part), ctr(CULT_LOST_HP_FORMAT), static_cast<unsigned>(hpLost));
    appendPart(out, len, part);
  }
  if (hpHealed > 0) {
    snprintf(part, sizeof(part), ctr(CULT_HEALED_FORMAT), static_cast<unsigned>(hpHealed));
    appendPart(out, len, part);
  }
}

// One line at the bottom of the Chronicle, seeded by the region's layout so a
// retried save writes the same line (appendLines skips it then).
void CultivationActivity::exploreChronicle(const char* listKey, const uint8_t region, const char* beast,
                                           const uint32_t extra, const char* ago) {
  if (!grammar_ || !explore_) return;
  lifelog::Line line;
  const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindExploreChronicle,
                                    (static_cast<uint32_t>(region) << 16) |
                                        (static_cast<uint32_t>(explore::reshapeCount(*state_, region)) << 8) | extra);
  if (exploretext::chronicleLine(line, *state_, listKey, seed, explore_->regionNames[region], beast, *grammar_, ago)) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

// Regions reshaped by Spatial Upheaval during this visit's reading: a line in
// the Return summary's life log, or straight into the Chronicle when there is
// no summary to show it in.
void CultivationActivity::appendUpheavalLines(const uint16_t reshaped) {
  if (reshaped == 0 || !grammar_) return;
  uint8_t added = 0;
  for (uint8_t region = 0; region < explore::kRegionCount && added < lifelog::kMaxUpheavalLines; ++region) {
    if ((reshaped & (1u << region)) == 0) continue;
    char name[ExploreView::kRegionNameSize];
    if (!exploretext::regionName(*state_, region, *grammar_, name, sizeof(name))) continue;
    lifelog::Line line;
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindExploreChronicle,
                                      (static_cast<uint32_t>(region) << 16) |
                                          (static_cast<uint32_t>(explore::reshapeCount(*state_, region)) << 8) | 3u);
    if (!exploretext::chronicleLine(line, *state_, "explore.line.upheaval", seed, name, nullptr, *grammar_)) continue;
    ++added;
    if (lifeLog_ && lifeLog_->count < lifelog::kMaxLines) {
      lifeLog_->lines[lifeLog_->count++] = line;  // written to the Chronicle with the rest of the visit
    } else {
      if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
      chronicle::appendLines(&line, 1);
    }
  }
  if (packs_) packs_->closeFile();
}

// --- Fights ----------------------------------------------------------------------------

void CultivationActivity::openFight() {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    const explore::Fight f = explore::pendingFight(*state_, v.layout);
    if (f.kind == explore::FightKind::None) return;
    v.beast[0] = '\0';
    v.fightText[0] = '\0';
    v.fightResult[0] = '\0';
    const char* list = "explore.fight.beast";
    if (grammar_) {
      bool named = false;
      // v0.2.0: the enemy's kind picks its name list and its opening line.
      static const char* const kKindLines[static_cast<int>(explore::Enemy::Count)] = {
          "explore.fight.beast", "explore.fight.bandit", "explore.fight.cultivator", "explore.fight.spirit"};
      if (f.kind == explore::FightKind::Beast) {
        list = f.returns > 0 ? "explore.fight.return" : kKindLines[static_cast<int>(f.enemy)];
        named = exploretext::enemyName(*state_, v.region, f.enemy, explore::beastSeed(*state_), *grammar_, v.beast,
                                       sizeof(v.beast));
      } else if (f.kind == explore::FightKind::Lair) {
        list = f.enemy == explore::Enemy::Bandit ? "explore.fight.hideout" : "explore.fight.lair";
        named = exploretext::enemyName(*state_, v.region, f.enemy, exploretext::lairSeed(*state_, v.region, f.tile),
                                       *grammar_, v.beast, sizeof(v.beast));
      } else {
        list = "explore.fight.guardian";
        named = exploretext::guardianName(*state_, v.region, *grammar_, v.beast, sizeof(v.beast));
      }
      if (named) {
        const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindFightText, explore::stepCounter(*state_));
        exploretext::flavor(*state_, list, seed, v.regionNames[v.region], v.beast, *grammar_, v.fightText,
                            sizeof(v.fightText));
      }
      if (packs_) packs_->closeFile();
    }
    selected_ = 0;
    screen_ = Screen::Fight;
  }
  requestUpdate();
}

// The answers on the fight screen (v0.2.0: by the enemy's kind).
uint8_t CultivationActivity::fightRowList(FightRow* out) const {
  uint8_t n = 0;
  if (!explore_) return 0;
  const ProgressBlock& p = state_->progress;
  const explore::Fight f = explore::pendingFight(*state_, explore_->layout);
  out[n++] = FightRow::Fight;
  if (f.canSlip) out[n++] = FightRow::Slip;
  if (f.payCost > 0) out[n++] = FightRow::Pay;
  if (f.canIncense) out[n++] = FightRow::Incense;
  if (inventory::tonicFor(*state_, 1) != inventory::Item::None && p.hp < p.hpMax) out[n++] = FightRow::Tonic;
  return n;
}

int CultivationActivity::fightRows() const {
  FightRow rows[5];
  return fightRowList(rows);
}

void CultivationActivity::answerFight() {
  ExploreView& v = *explore_;
  {
    RenderLock lock(*this);
    const explore::Fight f = explore::pendingFight(*state_, v.layout);
    if (f.kind == explore::FightKind::None) return;
    FightRow rows[5];
    const uint8_t rowCount = fightRowList(rows);
    if (selected_ < 0 || selected_ >= rowCount) return;
    char name[64];
    if (rows[selected_] == FightRow::Tonic) {  // a tonic first; the fight waits
      const inventory::Item tonic = inventory::tonicFor(*state_, f.damage);
      const uint16_t healed = inventory::useTonic(*state_, tonic);
      if (healed > 0) {
        snprintf(v.fightResult, sizeof(v.fightResult), ctr(CULT_DRANK_FORMAT), itemLabel(tonic, name, sizeof(name)),
                 static_cast<unsigned>(healed));
        persist();
      }
      if (selected_ >= fightRows()) selected_ = 0;
      requestUpdate();
      return;
    }
    const explore::FightChoice choice = rows[selected_] == FightRow::Slip      ? explore::FightChoice::Slip
                                        : rows[selected_] == FightRow::Pay     ? explore::FightChoice::Pay
                                        : rows[selected_] == FightRow::Incense ? explore::FightChoice::Incense
                                                                               : explore::FightChoice::Fight;
    const explore::FightResult res = explore::fight(*state_, v.layout, choice);
    if (res.refused) {  // short of stones: the row said how many
      snprintf(v.fightResult, sizeof(v.fightResult), "%s", ctr(CULT_TOO_POOR));
      requestUpdate();
      return;
    }
    LOG_INF("CULT", "Fight (kind %u, level %d vs %d): roll %u, %s", static_cast<unsigned>(f.kind),
            explore::playerLevel(*state_), f.level, static_cast<unsigned>(res.roll),
            res.won ? "won" : (res.slipped ? "slipped away" : "lost"));

    // The map shows what happened: the fight's line, then the numbers.
    v.text[0] = '\0';
    v.numbers[0] = '\0';
    static const char* const kWonLines[static_cast<int>(explore::Enemy::Count)] = {
        "explore.won", "explore.won.bandit", "explore.won.cultivator", "explore.won.spirit"};
    const char* list = res.paid        ? "explore.paid"
                       : res.drivenOff ? "explore.drivenoff"
                       : res.won       ? kWonLines[static_cast<int>(f.enemy)]
                       : res.slipped   ? "explore.slip"
                                       : "explore.lost";
    if (grammar_) {
      const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindFightText, explore::stepCounter(*state_) | 1u);
      if (!exploretext::flavor(*state_, list, seed, v.regionNames[v.region], v.beast, *grammar_, v.text,
                               sizeof(v.text)) &&
          res.won) {  // an SD pack may have replaced a kind's list away
        exploretext::flavor(*state_, "explore.won", seed, v.regionNames[v.region], v.beast, *grammar_, v.text,
                            sizeof(v.text));
      }
      if (packs_) packs_->closeFile();
    }
    char line[160];
    if (res.paid) {
      snprintf(line, sizeof(line), ctr(CULT_PAID_FORMAT), static_cast<unsigned long>(res.paidStones));
    } else if (res.drivenOff) {
      snprintf(line, sizeof(line), "%s", ctr(CULT_DRIVEN_OFF));
    } else if (res.won) {
      snprintf(line, sizeof(line), ctr(CULT_FIGHT_WON_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(f.chance));
    } else if (res.slipped) {
      snprintf(line, sizeof(line), "%s", ctr(CULT_SLIPPED));
    } else if (choice == explore::FightChoice::Slip) {
      snprintf(line, sizeof(line), ctr(CULT_SLIP_FAILED_FORMAT), static_cast<unsigned>(res.roll));
    } else {
      snprintf(line, sizeof(line), ctr(CULT_FIGHT_LOST_FORMAT), static_cast<unsigned>(res.roll),
               static_cast<unsigned>(f.chance));
    }
    char parts[160] = {};
    formatReward(res.reward, res.hpLost, 0, parts, sizeof(parts));
    formatReward(res.tile.reward, res.tile.hpLost, res.tile.hpHealed, parts, sizeof(parts));
    snprintf(v.numbers, sizeof(v.numbers), "%s", line);
    if (parts[0] != '\0') {
      appendSentence(v.numbers, sizeof(v.numbers), parts);
      const size_t used = strlen(v.numbers);
      if (used + 1 < sizeof(v.numbers)) snprintf(v.numbers + used, sizeof(v.numbers) - used, ".");
    }
    // v0.2.1: enemies that come back.
    if (res.remembered) appendSentence(v.numbers, sizeof(v.numbers), ctr(CULT_FOE_REMEMBERED));
    if (res.foeBeaten) {
      appendSentence(v.numbers, sizeof(v.numbers), ctr(CULT_FOE_GONE));
      exploreChronicle("explore.line.return.won", v.region, v.beast, 5);
    } else if (res.returning && (res.slipped || res.paid)) {
      exploreChronicle("explore.line.return.left", v.region, v.beast, 6);
    }
    if (res.reward.recipe >= 0) alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(res.reward.recipe));
    if (res.tile.reward.recipe >= 0) {
      alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(res.tile.reward.recipe));
    }
    if (res.guardianBeaten) {
      appendSentence(v.numbers, sizeof(v.numbers), ctr(CULT_GUARDIAN_BEATEN));
      exploreChronicle("explore.line.guardian", v.region, v.beast, 2);
    }
    if (res.tile.clearedNow) {
      char reward[120] = {};
      formatReward(res.tile.clearReward, 0, 0, reward, sizeof(reward));
      snprintf(line, sizeof(line), ctr(CULT_CLEARED_FORMAT), reward);
      appendSentence(v.numbers, sizeof(v.numbers), line);
      exploreChronicle("explore.line.clear", v.region, nullptr, 1);
    }
    persist();
    refreshCursor();
    for (uint8_t i = 0; i < v.cursorCount; ++i) {
      if (v.cursorTiles[i] == explore::position(*state_, v.layout, v.region)) v.cursor = i;
    }
    selected_ = 0;
    screen_ = Screen::Map;
  }
  requestUpdate();
}

// --- Meetings (v0.2.2) -----------------------------------------------------------------------

void CultivationActivity::openMeeting() {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    const explore::Meeting m = explore::pendingMeeting(*state_, v.layout);
    if (!m.active) return;
    v.beast[0] = '\0';
    v.fightText[0] = '\0';
    v.fightResult[0] = '\0';
    if (grammar_) {
      if (exploretext::cultivatorName(*state_, m.region, m.nameSeed, *grammar_, v.beast, sizeof(v.beast))) {
        const char* list = m.ring >= 0                        ? "explore.meet.return"
                           : m.offer == explore::Offer::Spar  ? "explore.meet.spar"
                           : m.offer == explore::Offer::Trade ? "explore.meet.trade"
                                                              : "explore.meet.challenge";
        const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindFightText, explore::stepCounter(*state_) | 2u);
        exploretext::flavor(*state_, list, seed, v.regionNames[v.region], v.beast, *grammar_, v.fightText,
                            sizeof(v.fightText));
      }
      if (packs_) packs_->closeFile();
    }
    selected_ = 0;
    screen_ = Screen::Meeting;
  }
  requestUpdate();
}

// The answers on the meeting screen: what they offer, Talk (once), a tonic
// before a challenge, and Walk on.
uint8_t CultivationActivity::meetRowList(MeetRow* out) const {
  uint8_t n = 0;
  if (!explore_) return 0;
  const explore::Meeting m = explore::pendingMeeting(*state_, explore_->layout);
  if (!m.active) return 0;
  out[n++] = MeetRow::Offer;
  if (!m.talked) out[n++] = MeetRow::Talk;
  if (m.offer == explore::Offer::Challenge && inventory::tonicFor(*state_, 1) != inventory::Item::None &&
      state_->progress.hp < state_->progress.hpMax) {
    out[n++] = MeetRow::Tonic;
  }
  out[n++] = MeetRow::WalkOn;
  return n;
}

int CultivationActivity::meetRows() const {
  MeetRow rows[4];
  return meetRowList(rows);
}

namespace {
// "not long ago", "a year ago", "7 years ago" into `out`.
const char* agoText(const uint16_t years, char* out, const size_t len) {
  if (years == 0) {
    snprintf(out, len, "%s", ctr(CULT_AGO_NOW));
  } else if (years == 1) {
    snprintf(out, len, "%s", ctr(CULT_AGO_ONE));
  } else {
    snprintf(out, len, ctr(CULT_AGO_FORMAT), static_cast<unsigned>(years));
  }
  return out;
}
}  // namespace

void CultivationActivity::answerMeeting() {
  ExploreView& v = *explore_;
  bool shop = false;
  {
    RenderLock lock(*this);
    const explore::Meeting m = explore::pendingMeeting(*state_, v.layout);
    if (!m.active) return;
    MeetRow rows[4];
    const uint8_t rowCount = meetRowList(rows);
    if (selected_ < 0 || selected_ >= rowCount) return;
    const MeetRow row = rows[selected_];
    char name[64];

    if (row == MeetRow::Tonic) {  // a tonic first; the challenge waits
      const inventory::Item tonic = inventory::tonicFor(*state_, m.damage);
      const uint16_t healed = inventory::useTonic(*state_, tonic);
      if (healed > 0) {
        snprintf(v.fightResult, sizeof(v.fightResult), ctr(CULT_DRANK_FORMAT), itemLabel(tonic, name, sizeof(name)),
                 static_cast<unsigned>(healed));
        persist();
      }
      if (selected_ >= meetRows()) selected_ = 0;
      requestUpdate();
      return;
    }
    if (row == MeetRow::Offer && m.offer == explore::Offer::Trade) {
      shop = true;  // the shop is its own screen
    } else if (row == MeetRow::Talk) {
      const explore::MeetResult r = explore::meet(*state_, v.layout, explore::MeetChoice::Talk);
      if (r.refused) {
        requestUpdate();
        return;
      }
      static const char* const kTalkLists[3] = {"explore.meet.talk.proud", "explore.meet.talk.open",
                                                "explore.meet.talk.greedy"};
      const char* list = r.talk == explore::TalkResult::Hint      ? kTalkLists[static_cast<int>(m.temperament)]
                         : r.talk == explore::TalkResult::TooPoor ? "explore.meet.talk.greedy"
                         : (m.temperament == explore::Temperament::Cautious && m.ring < 0)
                             ? "explore.meet.talk.shy"
                             : "explore.meet.talk.nothing";
      v.fightResult[0] = '\0';
      if (grammar_) {
        const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindFightText, explore::stepCounter(*state_) | 3u);
        exploretext::flavor(*state_, list, seed, v.regionNames[v.region], v.beast, *grammar_, v.fightText,
                            sizeof(v.fightText));
        if (packs_) packs_->closeFile();
      }
      if (r.talk == explore::TalkResult::Hint) {
        static const GameStr kEnemyNames[static_cast<int>(explore::Enemy::Count)] = {
            GameStr::CULT_ENEMY_BEAST, GameStr::CULT_ENEMY_BANDIT, GameStr::CULT_ENEMY_CULTIVATOR,
            GameStr::CULT_ENEMY_SPIRIT};
        snprintf(v.fightResult, sizeof(v.fightResult), ctr(CULT_MEET_HINT_FORMAT),
                 text(kEnemyNames[static_cast<int>(r.hintEnemy)]), dangerName(r.hintDanger));
        if (r.talkPaid > 0) {
          char paid[80];
          snprintf(paid, sizeof(paid), ctr(CULT_MEET_PAID_FORMAT), static_cast<unsigned long>(r.talkPaid));
          appendSentence(v.fightResult, sizeof(v.fightResult), paid);
        }
        persist();
      } else if (r.talk == explore::TalkResult::TooPoor) {
        snprintf(v.fightResult, sizeof(v.fightResult), ctr(CULT_MEET_POOR_FORMAT),
                 static_cast<unsigned long>(r.talkCost));
      } else {
        persist();
      }
      selected_ = 0;
      requestUpdate();
      return;
    } else {
      const explore::MeetResult r = explore::meet(
          *state_, v.layout, row == MeetRow::WalkOn ? explore::MeetChoice::WalkOn : explore::MeetChoice::Offer);
      if (r.refused) {
        requestUpdate();
        return;
      }
      LOG_INF("CULT", "Meeting (%s, level %d vs %d): %s", row == MeetRow::WalkOn ? "walk on" : "offer", m.level,
              explore::playerLevel(*state_), r.fought ? (r.won ? "won" : "lost") : "-");
      showMeetingResult(m, r);
    }
  }
  if (shop) {
    openMerchant(explore::kTraderTile);
  } else {
    requestUpdate();
  }
}

// The map after a meeting: its line, the numbers, the Chronicle. Caller holds the render lock.
void CultivationActivity::showMeetingResult(const explore::Meeting& m, const explore::MeetResult& r) {
  ExploreView& v = *explore_;
  v.text[0] = '\0';
  v.numbers[0] = '\0';
  const bool spar = m.offer == explore::Offer::Spar;
  if (r.fought && grammar_) {
    const char* list = spar ? (r.won ? "explore.meet.sparwon" : "explore.meet.sparlost")
                            : (r.won ? "explore.won.cultivator" : "explore.lost");
    const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindFightText, explore::stepCounter(*state_) | 5u);
    exploretext::flavor(*state_, list, seed, v.regionNames[v.region], v.beast, *grammar_, v.text, sizeof(v.text));
    if (packs_) packs_->closeFile();
  }
  char line[160];
  if (r.fought) {
    if (spar) {
      snprintf(line, sizeof(line), r.won ? ctr(CULT_SPAR_WON_FORMAT) : ctr(CULT_SPAR_LOST_FORMAT),
               static_cast<unsigned>(r.roll), static_cast<unsigned>(m.chance));
    } else {
      snprintf(line, sizeof(line), r.won ? ctr(CULT_FIGHT_WON_FORMAT) : ctr(CULT_FIGHT_LOST_FORMAT),
               static_cast<unsigned>(r.roll), static_cast<unsigned>(m.chance));
    }
  } else {
    snprintf(line, sizeof(line), "%s", ctr(CULT_WALKED_ON));
  }
  char parts[160] = {};
  formatReward(r.reward, r.hpLost, 0, parts, sizeof(parts));
  formatReward(r.tile.reward, r.tile.hpLost, r.tile.hpHealed, parts, sizeof(parts));
  snprintf(v.numbers, sizeof(v.numbers), "%s", line);
  if (parts[0] != '\0') {
    appendSentence(v.numbers, sizeof(v.numbers), parts);
    const size_t used = strlen(v.numbers);
    if (used + 1 < sizeof(v.numbers)) snprintf(v.numbers + used, sizeof(v.numbers) - used, ".");
  }
  if (r.reward.recipe >= 0) alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(r.reward.recipe));
  if (r.tile.reward.recipe >= 0) alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(r.tile.reward.recipe));
  if (r.tile.clearedNow) {
    char reward[120] = {};
    formatReward(r.tile.clearReward, 0, 0, reward, sizeof(reward));
    snprintf(line, sizeof(line), ctr(CULT_CLEARED_FORMAT), reward);
    appendSentence(v.numbers, sizeof(v.numbers), line);
    exploreChronicle("explore.line.clear", v.region, nullptr, 1);
  }

  // The Chronicle: who they were to you before, then what happened now.
  if (m.ring >= 0) {
    static const char* const kAgain[] = {nullptr,
                                         "explore.line.meet.again.won",
                                         "explore.line.meet.again.lost",
                                         "explore.line.meet.again.traded",
                                         "explore.line.meet.again.talked",
                                         "explore.line.meet.again.passed"};
    const int last = static_cast<int>(m.last);
    char ago[40];
    agoText(m.yearsAgo, ago, sizeof(ago));
    if (last > 0 && last < 6) exploreChronicle(kAgain[last], v.region, v.beast, 20, ago);
  }
  const char* result = nullptr;
  switch (r.outcome) {
    case explore::MeetOutcome::Won:
      result = spar ? "explore.line.meet.sparwon" : "explore.line.meet.challengewon";
      break;
    case explore::MeetOutcome::Lost:
      result = spar ? "explore.line.meet.sparlost" : "explore.line.meet.challengelost";
      break;
    case explore::MeetOutcome::Traded:
      result = "explore.line.meet.trade";
      break;
    case explore::MeetOutcome::Talked:
      result = "explore.line.meet.talk";
      break;
    default:
      break;
  }
  if (result != nullptr) exploreChronicle(result, v.region, v.beast, 21);

  persist();
  refreshCursor();
  for (uint8_t i = 0; i < v.cursorCount; ++i) {
    if (v.cursorTiles[i] == explore::position(*state_, v.layout, v.region)) v.cursor = i;
  }
  selected_ = 0;
  screen_ = Screen::Map;
}

// --- Merchants, the bag and the Region Talisman ------------------------------------------------

void CultivationActivity::openMerchant(const uint8_t tile) {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    v.merchantTile = tile;
    v.stock = explore::stockFor(*state_, v.layout, v.region, tile);
    notice_[0] = '\0';
    selected_ = 0;
    screen_ = Screen::Merchant;
  }
  requestUpdate();
}

// v0.2.1: how many herbs or ores to buy: 1, 5, 10 (as far as the stones and
// the bag allow) and as many as can be afforded.
void CultivationActivity::openMerchantQty(const uint8_t line) {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    const uint16_t most = explore::affordable(*state_, v.stock.lines[line]);
    v.qtyLine = line;
    v.qtyRows = 0;
    for (const uint16_t n : {static_cast<uint16_t>(1), static_cast<uint16_t>(5), static_cast<uint16_t>(10)}) {
      if (n < most) v.qtyCounts[v.qtyRows++] = n;
    }
    v.qtyCounts[v.qtyRows++] = most;
    notice_[0] = '\0';
    selected_ = 0;
    screen_ = Screen::MerchantQty;
  }
  requestUpdate();
}

void CultivationActivity::buyFromMerchant(const uint8_t line, const uint16_t count) {
  {
    RenderLock lock(*this);
    ExploreView& v = *explore_;
    const explore::StockLine l = v.stock.lines[line];
    const uint16_t cost = explore::linePrice(*state_, l);
    const int slip = alchemy::slipRecipe(*state_);
    char name[64];
    if (explore::buy(*state_, v.layout, v.region, v.merchantTile, line, count)) {
      if (l.item == inventory::Item::RecipeSlip && slip >= 0) {
        snprintf(notice_, sizeof(notice_), ctr(CULT_SLIP_BOUGHT_FORMAT),
                 pillLabel(alchemy::recipesOf(*state_).recipes[slip].pill), static_cast<unsigned>(cost));
        alchemyChronicle("alchemy.line.slip", static_cast<uint8_t>(slip));
      } else if (count > 1) {
        snprintf(notice_, sizeof(notice_), ctr(CULT_BOUGHT_MANY_FORMAT), static_cast<unsigned>(count),
                 itemLabel(l.item, name, sizeof(name)),
                 static_cast<unsigned long>(static_cast<uint32_t>(cost) * count));
      } else {
        snprintf(notice_, sizeof(notice_), ctr(CULT_BOUGHT_FORMAT), itemLabel(l.item, name, sizeof(name)),
                 static_cast<unsigned>(cost));
      }
      persist();
    } else {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_TOO_POOR));
    }
    v.stock = explore::stockFor(*state_, v.layout, v.region, v.merchantTile);
    selected_ = line;
    screen_ = Screen::Merchant;
  }
  requestUpdate();
}

void CultivationActivity::openBag(const Screen returnTo) {
  {
    RenderLock lock(*this);
    explore_->bagReturn = returnTo;
    notice_[0] = '\0';
    refreshBag();
    selected_ = 0;
    screen_ = returnTo == Screen::Merchant ? Screen::MerchantSell : Screen::Bag;
  }
  requestUpdate();
}

// The bag's rows: every item kind held, in item order. Caller holds the render lock.
void CultivationActivity::refreshBag() {
  ExploreView& v = *explore_;
  v.bagCount = 0;
  for (uint8_t i = 1; i < static_cast<uint8_t>(inventory::Item::Count); ++i) {
    const auto item = static_cast<inventory::Item>(i);
    if (inventory::count(*state_, item) > 0 && v.bagCount < ExploreView::kBagMax) v.bag[v.bagCount++] = item;
  }
  if (selected_ >= v.bagCount) selected_ = v.bagCount > 0 ? v.bagCount - 1 : 0;
}

// Bag: drink a tonic. Sell screen: sell one of the item.
void CultivationActivity::useBagItem() {
  ExploreView& v = *explore_;
  if (selected_ < 0 || selected_ >= v.bagCount) return;
  {
    RenderLock lock(*this);
    const inventory::Item item = v.bag[selected_];
    char name[64];
    itemLabel(item, name, sizeof(name));
    if (screen_ == Screen::MerchantSell) {
      const uint16_t price = inventory::sellPrice(item);
      if (explore::sell(*state_, item)) {
        snprintf(notice_, sizeof(notice_), ctr(CULT_SOLD_ITEM_FORMAT), name, static_cast<unsigned>(price));
        persist();
      }
    } else if (inventory::isTonic(item)) {
      if (state_->progress.hp >= state_->progress.hpMax) {
        snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_HP_FULL));
      } else {
        const uint16_t healed = inventory::useTonic(*state_, item);
        snprintf(notice_, sizeof(notice_), ctr(CULT_DRANK_FORMAT), name, static_cast<unsigned>(healed));
        persist();
      }
    } else if (inventory::isGradePill(item) || item == inventory::Item::QiPill) {
      useBagPill(item);
    } else if (item == inventory::Item::RegionTalisman && !v.forAlchemy) {
      openTalisman();
      return;
    }
    refreshBag();
  }
  requestUpdate();
}

void CultivationActivity::openTalisman() {
  // Caller may hold the render lock (from the bag); the members are only set here.
  ExploreView& v = *explore_;
  v.talismanCount = 0;
  for (uint8_t i = 0; i < v.regionCount; ++i) {
    if (explore::canUseRegionTalisman(*state_, i)) v.talismanRegions[v.talismanCount++] = i;
  }
  selected_ = 0;
  screen_ = Screen::Talisman;
  requestUpdate();
}

// --- Input ---------------------------------------------------------------------------------

bool CultivationActivity::handleExplore(const bool back, const bool confirm, const bool up, const bool down) {
  switch (screen_) {
    case Screen::Regions:
    case Screen::Map:
    case Screen::Fight:
    case Screen::Meeting:
    case Screen::Merchant:
    case Screen::MerchantSell:
    case Screen::Bag:
    case Screen::Talisman:
    case Screen::MerchantQty:
      break;
    default:
      return false;
  }
  if (!explore_) {  // out of memory earlier: nothing to show
    if (back || confirm) closeExplore();
    return true;
  }
  ExploreView& v = *explore_;

  switch (screen_) {
    case Screen::Regions: {
      if (back) {
        closeExplore();
      } else if (confirm) {
        v.regionsSelected = selected_;
        if (selected_ < v.regionCount) {
          openMap(static_cast<uint8_t>(selected_));
        } else if (selected_ == v.regionCount) {
          openBag(Screen::Regions);
        } else {
          RenderLock lock(*this);
          openTalisman();
        }
      } else {
        handleList(regionRows(), up, down);
      }
      return true;
    }

    case Screen::Map:
      if (back) {
        RenderLock lock(*this);
        v.mapOpen = false;
        notice_[0] = '\0';
        loadRegionNames();  // charting and clearing may have changed the list's lines
        selected_ = v.regionsSelected;
        screen_ = Screen::Regions;
        requestUpdate();
      } else if (confirm) {
        mapConfirm();
      } else if ((up || down) && v.cursorCount > 1) {
        RenderLock lock(*this);
        v.cursor =
            static_cast<uint8_t>(up ? (v.cursor + v.cursorCount - 1) % v.cursorCount : (v.cursor + 1) % v.cursorCount);
        v.text[0] = '\0';  // the last event was about the tile left behind
        v.numbers[0] = '\0';
        requestUpdate();
      }
      return true;

    case Screen::Fight:
      if (back) {  // the fight waits in the save; the map offers it again
        RenderLock lock(*this);
        refreshCursor();
        screen_ = Screen::Map;
        requestUpdate();
      } else if (confirm) {
        answerFight();
      } else {
        handleList(fightRows(), up, down);
      }
      return true;

    case Screen::Meeting:
      if (back) {  // the meeting waits in the save; the map offers it again
        RenderLock lock(*this);
        refreshCursor();
        screen_ = Screen::Map;
        requestUpdate();
      } else if (confirm) {
        answerMeeting();
      } else {
        handleList(meetRows(), up, down);
      }
      return true;

    case Screen::Merchant:
      if (back) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        if (v.merchantTile == explore::kTraderTile) {  // v0.2.2: back to the cultivator
          selected_ = 0;
          screen_ = Screen::Meeting;
        } else {
          screen_ = Screen::Map;
        }
        requestUpdate();
      } else if (confirm) {
        if (selected_ < v.stock.count) {
          const explore::StockLine& l = v.stock.lines[selected_];
          if (!l.limited && explore::affordable(*state_, l) > 1) {
            openMerchantQty(static_cast<uint8_t>(selected_));  // v0.2.1: herbs and ores in bulk
          } else if (!l.soldOut) {
            buyFromMerchant(static_cast<uint8_t>(selected_), 1);
          }
        } else {
          openBag(Screen::Merchant);
        }
      } else {
        handleList(v.stock.count + 1, up, down);
      }
      return true;

    case Screen::MerchantSell:
    case Screen::Bag:
      if (back) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        if (screen_ == Screen::MerchantSell) {
          v.stock = explore::stockFor(*state_, v.layout, v.region, v.merchantTile);
          selected_ = v.stock.count;  // back on "Sell items"
          screen_ = Screen::Merchant;
        } else if (v.bagReturn == Screen::Alchemy) {
          if (v.forAlchemy) explore_.reset();  // made only for the Bag
          refreshAlchemy();
          selected_ = alchemy_ ? alchemy_->menuSelected : 0;
          screen_ = Screen::Alchemy;
        } else {
          selected_ = v.bagReturn == Screen::Regions ? v.regionCount : 0;
          if (v.bagReturn == Screen::Regions) loadRegionNames();
          screen_ = v.bagReturn;
        }
        requestUpdate();
      } else if (confirm) {
        useBagItem();
      } else {
        handleList(v.bagCount, up, down);
      }
      return true;

    case Screen::MerchantQty:
      if (back) {
        RenderLock lock(*this);
        selected_ = v.qtyLine;
        screen_ = Screen::Merchant;
        requestUpdate();
      } else if (confirm && selected_ < v.qtyRows) {
        const uint16_t count = v.qtyCounts[selected_];
        selected_ = v.qtyLine;
        screen_ = Screen::Merchant;
        buyFromMerchant(v.qtyLine, count);
      } else {
        handleList(v.qtyRows, up, down);
      }
      return true;

    case Screen::Talisman:
      if (back) {
        RenderLock lock(*this);
        loadRegionNames();
        selected_ = v.regionsSelected;
        if (selected_ >= regionRows()) selected_ = 0;
        screen_ = Screen::Regions;
        requestUpdate();
      } else if (confirm && selected_ < v.talismanCount) {
        RenderLock lock(*this);
        const uint8_t region = v.talismanRegions[selected_];
        if (explore::useRegionTalisman(*state_, region)) {
          exploreChronicle("explore.line.talisman", region, nullptr, 4);
          char title[ExploreView::kRegionNameSize];
          capitalized(v.regionNames[region], title, sizeof(title));
          snprintf(notice_, sizeof(notice_), ctr(CULT_RESHAPED_FORMAT), title);
          persist();
        }
        loadRegionNames();
        selected_ = 0;
        screen_ = Screen::Regions;
        requestUpdate();
      } else {
        handleList(v.talismanCount, up, down);
      }
      return true;

    default:
      return true;
  }
}

// --- Drawing -------------------------------------------------------------------------------

const char* CultivationActivity::exploreTitle(char* buf, const size_t len) const {
  switch (screen_) {
    case Screen::Map:
      if (explore_) {
        capitalized(explore_->regionNames[explore_->region], buf, len);
        return buf;
      }
      return ctr(CULT_REGIONS);
    case Screen::Fight:
      return ctr(CULT_FIGHT_TITLE);
    case Screen::Meeting:
      return ctr(CULT_MEET_TITLE);
    case Screen::Merchant:
    case Screen::MerchantSell:
      return ctr(CULT_MERCHANT);
    case Screen::Bag:
      return ctr(CULT_BAG);
    case Screen::Talisman:
      return ctr(CULT_TALISMAN_TITLE);
    case Screen::MerchantQty:
      return ctr(CULT_MERCHANT);
    default:
      return ctr(CULT_REGIONS);
  }
}

void CultivationActivity::exploreLabels(const char*& confirmLabel, const char*& upLabel, const char*& downLabel) const {
  confirmLabel = tr(STR_SELECT);
  upLabel = tr(STR_DIR_UP);
  downLabel = tr(STR_DIR_DOWN);
  if (!explore_) {
    confirmLabel = upLabel = downLabel = "";
    return;
  }
  const ExploreView& v = *explore_;
  switch (screen_) {
    case Screen::Map: {
      upLabel = v.cursorCount > 1 ? tr(STR_DIR_LEFT) : "";
      downLabel = v.cursorCount > 1 ? tr(STR_DIR_RIGHT) : "";
      const uint8_t here = explore::position(*state_, v.layout, v.region);
      const uint8_t target = v.cursor < v.cursorCount ? v.cursorTiles[v.cursor] : here;
      if (explore::pendingRegion(*state_) == v.region) {
        confirmLabel = ctr(CULT_FIGHT_ROW);
      } else if (explore::meetingRegion(*state_) == v.region) {
        confirmLabel = ctr(CULT_MEET_ROW);
      } else if (target != here) {
        confirmLabel =
            explore::canStep(*state_, v.layout, v.region, target) == explore::StepError::None ? ctr(CULT_STEP_IN) : "";
      } else if (v.layout.tiles[here] == explore::Tile::Merchant) {
        confirmLabel = ctr(CULT_TRADE);
      } else if (canResolveHere()) {
        confirmLabel = ctr(CULT_TAKE);
      } else {
        confirmLabel = ctr(CULT_BAG);
      }
      return;
    }
    case Screen::Fight:
      if (fightRows() < 2) upLabel = downLabel = "";
      return;
    case Screen::Meeting:
      if (meetRows() < 2) upLabel = downLabel = "";
      return;
    case Screen::Merchant:
      return;
    case Screen::MerchantSell:
    case Screen::Bag:
      if (v.bagCount == 0) confirmLabel = "";
      if (screen_ == Screen::Bag && selected_ >= 0 && selected_ < v.bagCount) {
        // Only tonics (when hurt), the Region Talisman and pills that would help now do anything from the bag.
        const inventory::Item item = v.bag[selected_];
        const bool usable = (inventory::isTonic(item) && state_->progress.hp < state_->progress.hpMax) ||
                            (item == inventory::Item::RegionTalisman && !v.forAlchemy) ||
                            alchemy::canUse(*state_, item) == alchemy::UseError::None;
        if (!usable) confirmLabel = "";
      }
      if (v.bagCount < 2) upLabel = downLabel = "";
      return;
    case Screen::Talisman:
      if (v.talismanCount == 0) confirmLabel = "";
      if (v.talismanCount < 2) upLabel = downLabel = "";
      return;
    case Screen::MerchantQty:
      if (v.qtyRows < 2) upLabel = downLabel = "";
      return;
    default:
      if (regionRows() < 2) upLabel = downLabel = "";
      return;
  }
}

void CultivationActivity::renderRegions(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  if (notice_[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 2) + metrics.verticalSpacing;
  if (v.regionCount == 0) y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_REGIONS_NONE), 3) + 4;
  const int rows = regionRows();
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * rows;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, rows, selected_,
      [this](const int i) -> std::string {
        const ExploreView& ev = *explore_;
        if (i < ev.regionCount) {
          char title[ExploreView::kRegionNameSize];
          capitalized(ev.regionNames[i], title, sizeof(title));
          return title;
        }
        if (i == ev.regionCount) return ctr(CULT_BAG);
        return ctr(CULT_TALISMAN_ROW);
      },
      [this](const int i) -> std::string {
        const ExploreView& ev = *explore_;
        char sub[160];
        if (i < ev.regionCount) {
          const auto region = static_cast<uint8_t>(i);
          const char* danger = dangerName(explore::regionDanger(*state_, region));
          if (explore::cleared(*state_, region)) {
            const uint32_t hours = (explore::upheavalMinutesLeft(*state_, region) + 59u) / 60u;
            snprintf(sub, sizeof(sub), ctr(CULT_REGION_CLEARED_SUB_FORMAT), danger, static_cast<unsigned>(hours));
          } else {
            snprintf(sub, sizeof(sub), ctr(CULT_REGION_SUB_FORMAT), danger,
                     static_cast<unsigned>(explore::chartedCount(*state_, region)),
                     static_cast<unsigned>(ev.walkable[region]));
          }
        } else if (i == ev.regionCount) {
          snprintf(sub, sizeof(sub), ctr(CULT_BAG_SUB_FORMAT), static_cast<unsigned long>(state_->progress.stones),
                   static_cast<unsigned>(inventory::kinds(*state_)));
        } else {
          snprintf(sub, sizeof(sub), ctr(CULT_TALISMAN_ROW_SUB_FORMAT),
                   static_cast<unsigned>(inventory::count(*state_, inventory::Item::RegionTalisman)));
        }
        return sub;
      });
}

void CultivationActivity::drawMapTile(const int x, const int y, const int cell, const uint8_t tile) const {
  const ExploreView& v = *explore_;
  const GameState& s = *state_;
  const int cx = x + cell / 2;
  const int cy = y + cell / 2;
  if (!revealed(s, v.region, tile)) {
    hatch(renderer, x, y, cell);
    return;
  }
  if (!explore::walkable(v.layout, tile)) {
    crag(renderer, x, y, cell);
    return;
  }
  if (!explore::charted(s, v.region, tile)) return;  // walkable, not yet stepped on: blank
  const explore::Tile t = v.layout.tiles[tile];
  drawIcon(renderer, t, cx, cy);
  if (explore::used(s, v.region, tile) && t != explore::Tile::Path) {  // a small x: used up
    const int ux = x + cell - 15;
    const int uy = y + 6;
    renderer.drawLine(ux, uy, ux + 9, uy + 9, 2, true);
    renderer.drawLine(ux + 9, uy, ux, uy + 9, 2, true);
  }
}

void CultivationActivity::renderMap(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  char line[200];

  if (explore::cleared(s, v.region)) {
    snprintf(line, sizeof(line), ctr(CULT_MAP_STATUS_CLEARED_FORMAT), static_cast<unsigned>(p.stamina),
             static_cast<unsigned>(kStaminaCap), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
  } else {
    snprintf(line, sizeof(line), ctr(CULT_MAP_STATUS_FORMAT), static_cast<unsigned>(p.stamina),
             static_cast<unsigned>(kStaminaCap), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax),
             static_cast<unsigned>(explore::chartedCount(s, v.region)), static_cast<unsigned>(v.layout.walkable));
  }
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

  int cell = width / explore::kGrid;
  if (cell > kMaxCell) cell = kMaxCell;
  const int grid = cell * explore::kGrid;
  const int gx = x + (width - grid) / 2;
  const int gy = y;
  const uint8_t here = explore::position(s, v.layout, v.region);
  const uint8_t target = v.cursor < v.cursorCount ? v.cursorTiles[v.cursor] : here;

  for (uint8_t t = 0; t < explore::kTiles; ++t) {
    const int tx = gx + explore::colOf(t) * cell;
    const int ty = gy + explore::rowOf(t) * cell;
    drawMapTile(tx, ty, cell, t);
    renderer.drawRect(tx, ty, cell + 1, cell + 1);
  }
  // Where a step can go: a dotted inner frame.
  for (uint8_t i = 0; i < v.cursorCount; ++i) {
    const uint8_t t = v.cursorTiles[i];
    if (t == here) continue;
    dottedFrame(renderer, gx + explore::colOf(t) * cell + 6, gy + explore::rowOf(t) * cell + 6, cell - 11, cell - 11);
  }
  // The player: a small taiji in the corner of their tile, on white.
  {
    const int px = gx + explore::colOf(here) * cell + cell - 16;
    const int py = gy + explore::rowOf(here) * cell + cell - 16;
    fillDisc(renderer, px, py, 13, false);
    sleepscreen::drawTaiji(renderer, px, py, 11);
  }
  renderer.drawRect(gx - 1, gy - 1, grid + 3, grid + 3, 2, true);
  // The cursor: a thick frame.
  renderer.drawRect(gx + explore::colOf(target) * cell, gy + explore::rowOf(target) * cell, cell + 1, cell + 1, 4,
                    true);
  y = gy + grid + metrics.verticalSpacing * 2;
  renderMapInfo(x, y, width, bottom);
}

// Under the grid: the selected tile, what stepping there costs, and the last
// thing that happened.
void CultivationActivity::renderMapInfo(const int x, int y, const int width, const int bottom) {
  const ExploreView& v = *explore_;
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const uint8_t here = explore::position(s, v.layout, v.region);
  const uint8_t target = v.cursor < v.cursorCount ? v.cursorTiles[v.cursor] : here;
  const explore::Tile t = v.layout.tiles[target];
  const bool known = explore::charted(s, v.region, target);
  char line[224];
  char name[64];
  snprintf(name, sizeof(name), "%s", known ? text(kTileNames[static_cast<int>(t)]) : ctr(CULT_TILE_FOG));
  snprintf(line, sizeof(line), target == here ? ctr(CULT_TILE_HERE_FORMAT) : ctr(CULT_TILE_STEP_FORMAT), name);
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 2;

  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int textLine = renderer.getLineHeight(UI_10_FONT_ID);
  auto info = [&](const char* message) {
    if (y + smallLine > bottom) return;
    y = drawWrapped(SMALL_FONT_ID, x, y, width, message, (bottom - y) / smallLine < 2 ? 1 : 2);
  };

  // What matters most first: no HP, then the last event's numbers and line,
  // then what the selected tile is.
  if (p.hp == 0) {
    snprintf(line, sizeof(line), ctr(CULT_NO_HP_FORMAT), static_cast<unsigned>(p.hpMax));
    info(line);
  }
  if (v.numbers[0] != '\0' && y + textLine <= bottom) {
    const int room = (bottom - y) / textLine;
    y = drawWrapped(UI_10_FONT_ID, x, y, width, v.numbers, room < 3 ? room : 3);
  }
  if (v.text[0] != '\0') info(v.text);

  if (explore::pendingRegion(s) == v.region) {
    info(ctr(CULT_FIGHT_WAITS));
  } else if (explore::meetingRegion(s) == v.region) {
    info(ctr(CULT_MEET_WAITS));
  } else if (target != here && p.hp > 0) {
    info(ctr(CULT_TILE_STEP_COST));
    if (p.stamina == 0) info(ctr(CULT_NO_STAMINA));
  }
  if (!known) return;
  using explore::Tile;
  const bool used = explore::used(s, v.region, target);
  if (used && explore::regrows(t)) {
    const uint32_t minutes = explore::regrowMinutesLeft(s, v.region);
    if (minutes > 0) {
      snprintf(line, sizeof(line), ctr(CULT_TILE_REGROW_FORMAT), static_cast<unsigned>(minutes));
      info(line);
    } else {
      info(ctr(CULT_TILE_REGROW_SOON));
    }
    return;
  }
  if (used && explore::unique(t)) {
    info(ctr(CULT_TILE_DONE));
    return;
  }
  switch (t) {
    case Tile::Path:
      info(ctr(CULT_TILE_PATH_INFO));
      break;
    case Tile::Herb:
      info(ctr(CULT_TILE_HERB_INFO));
      break;
    case Tile::Ore:
      info(ctr(CULT_TILE_ORE_INFO));
      break;
    case Tile::Ruin:
      info(ctr(CULT_TILE_RUIN_INFO));
      break;
    case Tile::Cave:
      info(ctr(CULT_TILE_CAVE_INFO));
      break;
    case Tile::Quiet:
      info(ctr(CULT_TILE_QUIET_INFO));
      break;
    case Tile::Merchant:
      if (target == here) info(ctr(CULT_TILE_MERCHANT_INFO));
      break;
    case Tile::Lair: {
      const int level = explore::regionBase(v.region) + 3 + v.layout.lairBonus[target];
      snprintf(line, sizeof(line), ctr(CULT_TILE_LAIR_FORMAT),
               dangerName(explore::dangerFor(explore::playerLevel(s) - level)));
      info(line);
      break;
    }
    case Tile::Guardian:
      snprintf(line, sizeof(line), ctr(CULT_TILE_GUARDIAN_FORMAT),
               dangerName(explore::dangerFor(explore::playerLevel(s) - v.layout.guardianLevel)));
      info(line);
      break;
    default:
      break;
  }
}

void CultivationActivity::renderFight(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  const GameState& s = *state_;
  const ProgressBlock& p = s.progress;
  const explore::Fight f = explore::pendingFight(s, v.layout);
  char line[224];

  if (v.beast[0] != '\0') {
    capitalized(v.beast, line, sizeof(line));
    const auto header = renderer.wrappedText(UI_12_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
    for (const auto& h : header) {
      renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    y += 4;
  }
  static const GameStr kEnemyNames[static_cast<int>(explore::Enemy::Count)] = {
      GameStr::CULT_ENEMY_BEAST, GameStr::CULT_ENEMY_BANDIT, GameStr::CULT_ENEMY_CULTIVATOR,
      GameStr::CULT_ENEMY_SPIRIT};
  snprintf(line, sizeof(line), ctr(CULT_FIGHT_KIND_FORMAT), dangerName(f.danger),
           text(kEnemyNames[static_cast<int>(f.enemy)]));
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  if (v.fightText[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, v.fightText, 5) + metrics.verticalSpacing;

  snprintf(line, sizeof(line), ctr(CULT_HP_FORMAT), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
  renderer.drawText(UI_10_FONT_ID, x, y, line);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  const int barHeight = metrics.progressBarHeight;
  renderer.drawRect(x, y, width, barHeight);
  const int fill = p.hpMax > 0 ? static_cast<int>(static_cast<int64_t>(width - 4) * p.hp / p.hpMax) : 0;
  if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, barHeight - 4);
  y += barHeight + metrics.verticalSpacing;
  if (v.fightResult[0] != '\0') {
    y = drawWrapped(SMALL_FONT_ID, x, y, width, v.fightResult, 2);
  } else if (f.returns > 0) {  // v0.2.1
    snprintf(line, sizeof(line), ctr(CULT_FOE_BACK_FORMAT), static_cast<unsigned>(explore::kFoeLevelStep * f.returns));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
  } else if (f.enemy == explore::Enemy::Spirit) {
    y = drawWrapped(SMALL_FONT_ID, x, y, width, ctr(CULT_SPIRIT_NO_SLIP), 2);
  }

  const int rows = fightRows();
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int listTop = bottom - rowHeight * rows;
  GUI.drawList(
      renderer, Rect{0, listTop, renderer.getScreenWidth(), rowHeight * rows}, rows, selected_,
      [this](const int i) -> std::string {
        FightRow list[5];
        const uint8_t n = fightRowList(list);
        if (i >= n) return {};
        char row[96];
        char name[64];
        switch (list[i]) {
          case FightRow::Fight:
            return ctr(CULT_FIGHT_ROW);
          case FightRow::Slip:
            return ctr(CULT_SLIP_ROW);
          case FightRow::Pay:
            return ctr(CULT_PAY_ROW);
          case FightRow::Incense:
            return ctr(CULT_INCENSE_ROW);
          case FightRow::Tonic: {
            const explore::Fight fight = explore::pendingFight(*state_, explore_->layout);
            snprintf(row, sizeof(row), ctr(CULT_TONIC_ROW_FORMAT),
                     itemLabel(inventory::tonicFor(*state_, fight.damage), name, sizeof(name)));
            return row;
          }
        }
        return {};
      },
      [this](const int i) -> std::string {
        FightRow list[5];
        const uint8_t n = fightRowList(list);
        if (i >= n) return {};
        char sub[160];
        const explore::Fight fight = explore::pendingFight(*state_, explore_->layout);
        switch (list[i]) {
          case FightRow::Fight:
            snprintf(sub, sizeof(sub),
                     fight.enemy == explore::Enemy::Cultivator ? ctr(CULT_FIGHT_SUB_CULT_FORMAT)
                                                               : ctr(CULT_FIGHT_SUB_FORMAT),
                     static_cast<unsigned>(fight.chance), static_cast<unsigned>(fight.damage));
            break;
          case FightRow::Slip:
            if (fight.slipChance >= 100) {
              snprintf(sub, sizeof(sub), "%s", ctr(CULT_SLIP_SURE_SUB));
            } else {
              snprintf(sub, sizeof(sub), ctr(CULT_SLIP_SUB_FORMAT), static_cast<unsigned>(fight.slipChance),
                       static_cast<unsigned>(fight.slipDamage));
            }
            break;
          case FightRow::Pay:
            if (state_->progress.stones >= fight.payCost) {
              snprintf(sub, sizeof(sub), ctr(CULT_PAY_SUB_FORMAT), static_cast<unsigned long>(fight.payCost));
            } else {
              snprintf(sub, sizeof(sub), ctr(CULT_PAY_SHORT_FORMAT), static_cast<unsigned long>(fight.payCost),
                       static_cast<unsigned long>(state_->progress.stones));
            }
            break;
          case FightRow::Incense:
            snprintf(sub, sizeof(sub), ctr(CULT_INCENSE_SUB_FORMAT),
                     static_cast<unsigned>(inventory::count(*state_, inventory::Item::CalmIncense)));
            break;
          case FightRow::Tonic: {
            const inventory::Item tonic = inventory::tonicFor(*state_, fight.damage);
            snprintf(sub, sizeof(sub), ctr(CULT_TONIC_SUB_FORMAT),
                     static_cast<unsigned>(inventory::healAmount(*state_, tonic)),
                     static_cast<unsigned>(inventory::count(*state_, tonic)));
            break;
          }
        }
        return sub;
      });
}

void CultivationActivity::renderMeeting(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  const ProgressBlock& p = state_->progress;
  const explore::Meeting m = explore::pendingMeeting(*state_, v.layout);
  if (!m.active) return;
  char line[224];

  if (v.beast[0] != '\0') {
    capitalized(v.beast, line, sizeof(line));
    const auto header = renderer.wrappedText(UI_12_FONT_ID, line, width, 2, EpdFontFamily::BOLD);
    for (const auto& h : header) {
      renderer.drawText(UI_12_FONT_ID, x, y, h.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    y += 4;
  }
  if (m.offer != explore::Offer::Trade) {  // how you would rate them, as in a fight
    snprintf(line, sizeof(line), ctr(CULT_FIGHT_KIND_FORMAT), dangerName(m.danger), ctr(CULT_ENEMY_CULTIVATOR));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  }
  if (v.fightText[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, v.fightText, 6) + metrics.verticalSpacing;

  if (m.offer == explore::Offer::Challenge) {  // a real fight: show the HP at stake
    snprintf(line, sizeof(line), ctr(CULT_HP_FORMAT), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
    renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    const int barHeight = metrics.progressBarHeight;
    renderer.drawRect(x, y, width, barHeight);
    const int fill = p.hpMax > 0 ? static_cast<int>(static_cast<int64_t>(width - 4) * p.hp / p.hpMax) : 0;
    if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, barHeight - 4);
    y += barHeight + metrics.verticalSpacing;
  }
  if (v.fightResult[0] != '\0') {
    y = drawWrapped(SMALL_FONT_ID, x, y, width, v.fightResult, 3);
  } else if (m.levelsAdded > 0) {
    snprintf(line, sizeof(line), ctr(CULT_FOE_BACK_FORMAT), static_cast<unsigned>(m.levelsAdded));
    y = drawWrapped(SMALL_FONT_ID, x, y, width, line, 2);
  }

  const int rows = meetRows();
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int listTop = bottom - rowHeight * rows;
  GUI.drawList(
      renderer, Rect{0, listTop, renderer.getScreenWidth(), rowHeight * rows}, rows, selected_,
      [this](const int i) -> std::string {
        MeetRow list[4];
        const uint8_t n = meetRowList(list);
        if (i >= n) return {};
        const explore::Meeting mm = explore::pendingMeeting(*state_, explore_->layout);
        char row[96];
        char name[64];
        switch (list[i]) {
          case MeetRow::Offer:
            return mm.offer == explore::Offer::Spar    ? ctr(CULT_SPAR_ROW)
                   : mm.offer == explore::Offer::Trade ? ctr(CULT_TRADE)
                                                       : ctr(CULT_CHALLENGE_ROW);
          case MeetRow::Talk:
            return ctr(CULT_TALK_ROW);
          case MeetRow::Tonic:
            snprintf(row, sizeof(row), ctr(CULT_TONIC_ROW_FORMAT),
                     itemLabel(inventory::tonicFor(*state_, mm.damage), name, sizeof(name)));
            return row;
          case MeetRow::WalkOn:
            return ctr(CULT_WALK_ON_ROW);
        }
        return {};
      },
      [this](const int i) -> std::string {
        MeetRow list[4];
        const uint8_t n = meetRowList(list);
        if (i >= n) return {};
        const explore::Meeting mm = explore::pendingMeeting(*state_, explore_->layout);
        char sub[160];
        switch (list[i]) {
          case MeetRow::Offer:
            if (mm.offer == explore::Offer::Spar) {
              snprintf(sub, sizeof(sub), ctr(CULT_SPAR_SUB_FORMAT), static_cast<unsigned>(mm.chance));
            } else if (mm.offer == explore::Offer::Trade) {
              snprintf(sub, sizeof(sub), "%s", ctr(CULT_TRADE_SUB));
            } else {
              snprintf(sub, sizeof(sub), ctr(CULT_FIGHT_SUB_CULT_FORMAT), static_cast<unsigned>(mm.chance),
                       static_cast<unsigned>(mm.damage));
            }
            break;
          case MeetRow::Talk:
            snprintf(sub, sizeof(sub), "%s", ctr(CULT_TALK_SUB));
            break;
          case MeetRow::Tonic: {
            const inventory::Item tonic = inventory::tonicFor(*state_, mm.damage);
            snprintf(sub, sizeof(sub), ctr(CULT_TONIC_SUB_FORMAT),
                     static_cast<unsigned>(inventory::healAmount(*state_, tonic)),
                     static_cast<unsigned>(inventory::count(*state_, tonic)));
            break;
          }
          case MeetRow::WalkOn:
            snprintf(sub, sizeof(sub), "%s", ctr(CULT_WALK_ON_SUB));
            break;
        }
        return sub;
      });
}

void CultivationActivity::renderMerchant(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  char line[160];
  snprintf(line, sizeof(line), ctr(CULT_MERCHANT_STONES_FORMAT), static_cast<unsigned long>(state_->progress.stones));
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  if (notice_[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 2);
  y += metrics.verticalSpacing;
  const int rows = v.stock.count + 1;
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * rows;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, rows, selected_,
      [this](const int i) -> std::string {
        char name[64];
        if (i < explore_->stock.count) return itemLabel(explore_->stock.lines[i].item, name, sizeof(name));
        return ctr(CULT_SELL_ITEMS);
      },
      [this](const int i) -> std::string {
        const ExploreView& ev = *explore_;
        char sub[160];
        if (i >= ev.stock.count) return ctr(CULT_SELL_ITEMS_SUB);
        const explore::StockLine& l = ev.stock.lines[i];
        if (l.soldOut) {
          if (ev.merchantTile == explore::kTraderTile) {
            snprintf(sub, sizeof(sub), "%s", ctr(CULT_TRADER_SOLD));
          } else {
            snprintf(sub, sizeof(sub), ctr(CULT_SOLD_OUT_FORMAT), ev.regionNames[ev.region]);
          }
        } else {
          char desc[96];
          ui::itemDescription(l.item, desc, sizeof(desc));
          const int slip = alchemy::slipRecipe(*state_);
          if (l.item == inventory::Item::RecipeSlip && slip >= 0) {
            snprintf(desc, sizeof(desc), ctr(CULT_ITEM_SLIP_DESC_FORMAT),
                     pillLabel(alchemy::recipesOf(*state_).recipes[slip].pill));
          }
          snprintf(sub, sizeof(sub), ctr(CULT_BUY_SUB_FORMAT), static_cast<unsigned>(explore::linePrice(*state_, l)),
                   desc);
        }
        return sub;
      });
}

void CultivationActivity::renderSell(const int x, const int y, const int width, const int bottom) {
  renderBag(x, y, width, bottom);  // the same rows; the subtitles show the price
}

void CultivationActivity::renderBag(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  const ProgressBlock& p = state_->progress;
  char line[160];
  if (screen_ == Screen::MerchantSell) {
    snprintf(line, sizeof(line), ctr(CULT_MERCHANT_STONES_FORMAT), static_cast<unsigned long>(p.stones));
  } else {
    snprintf(line, sizeof(line), ctr(CULT_HP_FORMAT), static_cast<unsigned>(p.hp), static_cast<unsigned>(p.hpMax));
  }
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  if (notice_[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 2);
  y += metrics.verticalSpacing;
  if (v.bagCount == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_BAG_EMPTY), 4);
    return;
  }
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * v.bagCount;
  if (y + height > bottom) height = bottom - y;
  const bool selling = screen_ == Screen::MerchantSell;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, v.bagCount, selected_,
      [this](const int i) -> std::string {
        char name[64];
        return itemLabel(explore_->bag[i], name, sizeof(name));
      },
      [this, selling](const int i) -> std::string {
        const inventory::Item item = explore_->bag[i];
        char sub[160];
        if (selling) {
          snprintf(sub, sizeof(sub), ctr(CULT_SELL_SUB_FORMAT), static_cast<unsigned>(inventory::count(*state_, item)),
                   static_cast<unsigned>(inventory::sellPrice(item)));
        } else {
          char desc[96];
          ui::itemDescription(item, desc, sizeof(desc));
          snprintf(sub, sizeof(sub), ctr(CULT_BAG_HELD_FORMAT), static_cast<unsigned>(inventory::count(*state_, item)),
                   desc);
        }
        return sub;
      });
}

void CultivationActivity::renderMerchantQty(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  char line[160];
  char name[64];
  const inventory::Item item = v.stock.lines[v.qtyLine].item;
  snprintf(line, sizeof(line), ctr(CULT_QTY_HEADER_FORMAT), itemLabel(item, name, sizeof(name)),
           static_cast<unsigned>(explore::linePrice(*state_, v.stock.lines[v.qtyLine])),
           static_cast<unsigned long>(state_->progress.stones));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;
  int height = rowHeight * v.qtyRows;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, v.qtyRows, selected_,
      [this](const int i) -> std::string {
        const ExploreView& ev = *explore_;
        const uint16_t n = ev.qtyCounts[i];
        const uint32_t cost = static_cast<uint32_t>(explore::linePrice(*state_, ev.stock.lines[ev.qtyLine])) * n;
        char row[96];
        snprintf(row, sizeof(row), i + 1 == ev.qtyRows && i > 0 ? ctr(CULT_QTY_MAX_FORMAT) : ctr(CULT_QTY_FORMAT),
                 static_cast<unsigned>(n), static_cast<unsigned long>(cost));
        return row;
      });
}

void CultivationActivity::renderTalisman(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!explore_) return;
  const ExploreView& v = *explore_;
  y = drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_TALISMAN_HINT), 4) + metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight;
  int height = rowHeight * v.talismanCount;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(renderer, Rect{0, y, renderer.getScreenWidth(), height}, v.talismanCount, selected_,
               [this](const int i) -> std::string {
                 char title[ExploreView::kRegionNameSize];
                 capitalized(explore_->regionNames[explore_->talismanRegions[i]], title, sizeof(title));
                 return title;
               });
}

#endif  // CROSSINK_CULTIVATION
