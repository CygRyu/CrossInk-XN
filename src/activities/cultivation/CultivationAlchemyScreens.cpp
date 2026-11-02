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
#include "GameText.h"
#include "Meridians.h"
#include "Rng.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Alchemy (v0.2.0): the Alchemy screen (the cauldron, Recipes, Experiment,
// the Bag), a recipe's page and the experiment's picker. The rules are in
// Alchemy.{h,cpp}; this file shows them, writes their Chronicle lines and
// saves after every action. Text is generated in the input loop, under the
// render lock, never while drawing.

using namespace cultivation;

namespace {

constexpr uint32_t kKindAlchemyText = 104;  // contentSeed kinds 100-119: alchemy
constexpr uint32_t kKindAlchemyChronicle = 103;

enum class MenuRow : uint8_t { Collect, Recipes, Experiment, Bag };

uint8_t menuRows(const GameState& s, MenuRow* out) {
  uint8_t n = 0;
  if (alchemy::ready(s)) out[n++] = MenuRow::Collect;
  out[n++] = MenuRow::Recipes;
  out[n++] = MenuRow::Experiment;
  out[n++] = MenuRow::Bag;
  return n;
}

// Appends " sentence" to out.
void appendSentence(char* out, const size_t len, const char* sentence) {
  if (sentence == nullptr || sentence[0] == '\0') return;
  const size_t used = strlen(out);
  if (used + 1 >= len) return;
  snprintf(out + used, len - used, "%s%s", used > 0 ? " " : "", sentence);
}

void capitalise(char* s) {
  if (s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
}

// Rows of the experiment's picker: Mix (2-3 parts chosen), Start over (any), then the ingredients.
int experimentHeadRows(const uint8_t chosen) { return (chosen >= 2 ? 1 : 0) + (chosen >= 1 ? 1 : 0); }

}  // namespace

// --- Names ------------------------------------------------------------------------------

void CultivationActivity::loadIngredientNames() {
  if (ingredients_) return;
  ingredients_ = makeUniqueNoThrow<IngredientNames>();
  if (!ingredients_) {
    LOG_ERR("CULT", "Out of memory for ingredient names (%u bytes)", static_cast<unsigned>(sizeof(IngredientNames)));
    return;
  }
  if (!grammar_) return;  // the fallback names ("Herb of region 3") stand in
  for (uint8_t i = 0; i < 20; ++i) {
    const inventory::Item item = i < 10 ? inventory::herbFor(i) : inventory::oreFor(static_cast<uint8_t>(i - 10));
    char* out = ingredients_->names[i];
    if (exploretext::ingredientName(*state_, item, *grammar_, out, sizeof(ingredients_->names[i]))) {
      capitalise(out);
    } else {
      out[0] = '\0';
    }
  }
  if (packs_) packs_->closeFile();
}

const char* CultivationActivity::itemLabel(const inventory::Item item, char* buf, const size_t len) const {
  if (inventory::isIngredient(item) && ingredients_) {
    const uint8_t i = static_cast<uint8_t>(inventory::regionOf(item) + (inventory::isOre(item) ? 10 : 0));
    if (ingredients_->names[i][0] != '\0') {
      snprintf(buf, len, "%s", ingredients_->names[i]);
      return buf;
    }
  }
  ui::itemName(item, buf, len);
  return buf;
}

const char* CultivationActivity::pillLabel(const alchemy::Pill pill) const { return ui::pillName(pill); }

// One line at the bottom of the Chronicle; seeded by the recipe, so a retried
// save writes the same line (appendLines skips it then).
void CultivationActivity::alchemyChronicle(const char* listKey, const uint8_t recipe) {
  if (!grammar_ || recipe >= alchemy::kRecipeCount) return;
  lifelog::Line line;
  const bool slip = strcmp(listKey, "alchemy.line.slip") == 0;
  const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindAlchemyChronicle, (slip ? 0x100u : 0u) | recipe);
  if (exploretext::alchemyLine(line, *state_, listKey, seed,
                               pillLabel(alchemy::recipesOf(*state_).recipes[recipe].pill), *grammar_)) {
    if (!chronicle::exists()) chronicle::writeOpening(*state_, grammar_.get());
    chronicle::appendLines(&line, 1);
    chronicleExists_ = chronicle::exists();
  }
  if (packs_) packs_->closeFile();
}

// Grade and Qi Gathering pills, taken from the Bag. Caller holds the render lock.
void CultivationActivity::useBagPill(const inventory::Item item) {
  char name[64];
  itemLabel(item, name, sizeof(name));
  switch (alchemy::use(*state_, item)) {
    case alchemy::UseError::None:
      if (item == inventory::Item::QiPill) {
        snprintf(notice_, sizeof(notice_), ctr(CULT_TOOK_QI_FORMAT),
                 static_cast<unsigned>(meridians::pillBonus(*state_, alchemy::kQiPillBonusPermille) / 10u),
                 static_cast<unsigned>((alchemy::qiPillSecondsLeft(*state_) + 59u) / 60u));
      } else {
        snprintf(notice_, sizeof(notice_), ctr(CULT_TOOK_GRADE_FORMAT), name,
                 static_cast<unsigned>(alchemy::gradePillPoints(*state_, item)),
                 static_cast<unsigned>(alchemy::gradePillsTaken(*state_)));
      }
      persist();
      break;
    case alchemy::UseError::WrongRealm:
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_USE_WRONG_REALM));
      break;
    case alchemy::UseError::Enough:
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_USE_ENOUGH));
      break;
    case alchemy::UseError::QiFull:
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_USE_QI_FULL));
      break;
    default:
      break;
  }
}

// --- Opening and closing --------------------------------------------------------------------

void CultivationActivity::openAlchemy() {
  auto view = makeUniqueNoThrow<AlchemyView>();
  if (!view) {
    LOG_ERR("CULT", "Out of memory for alchemy (%u bytes)", static_cast<unsigned>(sizeof(AlchemyView)));
    return;
  }
  {
    RenderLock lock(*this);
    alchemy_ = std::move(view);
    loadIngredientNames();
    notice_[0] = '\0';
    refreshAlchemy();
    selected_ = 0;
    screen_ = Screen::Alchemy;
  }
  requestUpdate();
}

void CultivationActivity::closeAlchemy() {
  {
    RenderLock lock(*this);
    alchemy_.reset();
    explore_.reset();  // only ever the Bag's, from here
    ingredients_.reset();
    notice_[0] = '\0';
    refreshActions();
    selected_ = 0;
    for (uint8_t i = 0; i < actionCount_; ++i) {
      if (actions_[i] == Action::Alchemy) selected_ = i;
    }
    screen_ = Screen::Cultivator;
  }
  requestUpdate();
}

// Known recipes and held ingredients. Caller holds the render lock.
void CultivationActivity::refreshAlchemy() {
  if (!alchemy_) return;
  AlchemyView& v = *alchemy_;
  v.recipeCount = 0;
  for (uint8_t i = 0; i < alchemy::kRecipeCount; ++i) {
    if (alchemy::known(*state_, i)) v.recipes[v.recipeCount++] = i;
  }
  v.ingredientCount = 0;
  for (uint8_t region = 0; region < inventory::kRegions; ++region) {
    for (const inventory::Item item : {inventory::herbFor(region), inventory::oreFor(region)}) {
      if (inventory::count(*state_, item) > 0 && v.ingredientCount < sizeof(v.ingredients) / sizeof(v.ingredients[0])) {
        v.ingredients[v.ingredientCount++] = item;
      }
    }
  }
}

int CultivationActivity::alchemyRows() const {
  if (!alchemy_) return 0;
  const AlchemyView& v = *alchemy_;
  switch (screen_) {
    case Screen::Alchemy: {
      MenuRow rows[4];
      return menuRows(*state_, rows);
    }
    case Screen::Recipes:
      return v.recipeCount;
    case Screen::Experiment:
      return experimentHeadRows(v.chosenCount) + v.ingredientCount;
    default:
      return 0;
  }
}

// --- Actions ------------------------------------------------------------------------------------

void CultivationActivity::collectPills() {
  {
    RenderLock lock(*this);
    AlchemyView& v = *alchemy_;
    const alchemy::Collected c = alchemy::collect(*state_);
    if (!c.ok) return;
    char name[64];
    itemLabel(c.item, name, sizeof(name));
    // v0.2.1 (user): taking the pills out is a small event.
    if (c.failed) {
      snprintf(notice_, sizeof(notice_), ctr(CULT_COLLECTED_FAILED_FORMAT), static_cast<unsigned>(c.count));
    } else {
      snprintf(notice_, sizeof(notice_), ctr(CULT_COLLECTED_FORMAT), static_cast<unsigned>(c.count), name,
               static_cast<unsigned>(c.roll));
      if (c.improved) appendSentence(notice_, sizeof(notice_), ctr(CULT_COLLECTED_IMPROVED));
      if (c.extra) appendSentence(notice_, sizeof(notice_), ctr(CULT_COLLECTED_EXTRA));
    }
    char extra[120];
    if (c.added.sold > 0) {
      snprintf(extra, sizeof(extra), ctr(CULT_BAG_FULL_SOLD_FORMAT), static_cast<unsigned>(c.added.sold),
               static_cast<unsigned long>(c.added.soldStones));
      appendSentence(notice_, sizeof(notice_), extra);
    }
    if (c.added.lost > 0) {
      snprintf(extra, sizeof(extra), ctr(CULT_BAG_FULL_KEPT_FORMAT), static_cast<unsigned>(c.added.lost));
      appendSentence(notice_, sizeof(notice_), extra);
    }
    v.flavor[0] = '\0';
    if (grammar_) {
      const uint16_t brews = static_cast<uint16_t>(state_->timers[6] | (state_->timers[7] << 8));
      const char* list = c.failed     ? "alchemy.failed"
                         : c.extra    ? "alchemy.extra"
                         : c.improved ? "alchemy.improved"
                                      : "alchemy.collect";
      exploretext::alchemyFlavor(*state_, list, contentSeed(worldSeedOf(*state_), kKindAlchemyText, brews),
                                 pillLabel(c.pill), *grammar_, v.flavor, sizeof(v.flavor));
      if (packs_) packs_->closeFile();
    }
    LOG_INF("CULT", "Collected recipe %u: %u x item %u (roll %u)", static_cast<unsigned>(c.recipe),
            static_cast<unsigned>(c.count), static_cast<unsigned>(c.item), static_cast<unsigned>(c.roll));
    persist();
    refreshAlchemy();
    selected_ = 0;
  }
  requestUpdate();
}

void CultivationActivity::brewSelected() {
  {
    RenderLock lock(*this);
    AlchemyView& v = *alchemy_;
    if (alchemy::brewing(*state_)) {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_RECIPE_BUSY));
    } else if (!alchemy::brew(*state_, v.recipe, v.batch)) {
      snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_RECIPE_MISSING));
    } else {
      notice_[0] = '\0';  // the cauldron line says what is brewing and for how long
      v.flavor[0] = '\0';
      persist();
      refreshAlchemy();
      selected_ = v.menuSelected = 0;
      screen_ = Screen::Alchemy;
    }
  }
  requestUpdate();
}

void CultivationActivity::mixExperiment() {
  {
    RenderLock lock(*this);
    AlchemyView& v = *alchemy_;
    if (v.chosenCount < 2) return;
    const alchemy::Experiment e = alchemy::experiment(*state_, v.chosen, v.chosenCount);
    if (!e.valid) {
      snprintf(notice_, sizeof(notice_), "%s",
               alchemy::brewing(*state_) ? ctr(CULT_RECIPE_BUSY) : ctr(CULT_RECIPE_MISSING));
      requestUpdate();
      return;
    }
    v.flavor[0] = '\0';
    const char* list = "alchemy.miss";
    const char* pill = "";
    if (e.recipe >= 0) {
      const alchemy::Recipe r = alchemy::recipesOf(*state_).recipes[e.recipe];
      pill = pillLabel(r.pill);
      snprintf(notice_, sizeof(notice_),
               e.learned ? ctr(CULT_EXPERIMENT_LEARNED_FORMAT) : ctr(CULT_EXPERIMENT_KNOWN_FORMAT), pill,
               static_cast<unsigned>(r.brewMinutes));
      list = "alchemy.learned";
      if (e.learned) alchemyChronicle("alchemy.line.learned", static_cast<uint8_t>(e.recipe));
    } else if (e.noneUnknown) {
      snprintf(notice_, sizeof(notice_), ctr(CULT_EXPERIMENT_MISS_NONE_FORMAT), static_cast<unsigned>(e.parts));
    } else {
      snprintf(notice_, sizeof(notice_), ctr(CULT_EXPERIMENT_MISS_FORMAT), static_cast<unsigned>(e.matches),
               static_cast<unsigned>(e.parts));
    }
    if (grammar_) {
      const uint32_t seed = contentSeed(worldSeedOf(*state_), kKindAlchemyText,
                                        0x10000u | (static_cast<uint32_t>(v.chosen[0]) << 8) | v.chosenCount);
      exploretext::alchemyFlavor(*state_, list, seed, pill, *grammar_, v.flavor, sizeof(v.flavor));
      if (packs_) packs_->closeFile();
    }
    LOG_INF("CULT", "Experiment with %u parts: recipe %d, %u matched", static_cast<unsigned>(e.parts), e.recipe,
            static_cast<unsigned>(e.matches));
    persist();
    v.chosenCount = 0;
    refreshAlchemy();
    selected_ = v.menuSelected = 0;
    screen_ = Screen::Alchemy;
  }
  requestUpdate();
}

// --- Input -------------------------------------------------------------------------------------

bool CultivationActivity::handleAlchemy(const bool back, const bool confirm, const bool up, const bool down) {
  switch (screen_) {
    case Screen::Alchemy:
    case Screen::Recipes:
    case Screen::RecipeDetail:
    case Screen::Experiment:
      break;
    default:
      return false;
  }
  if (!alchemy_) {
    if (back || confirm) closeAlchemy();
    return true;
  }
  AlchemyView& v = *alchemy_;

  switch (screen_) {
    case Screen::Alchemy: {
      if (back) {
        closeAlchemy();
        return true;
      }
      if (!confirm) {
        handleList(alchemyRows(), up, down);
        return true;
      }
      MenuRow rows[4];
      const uint8_t n = menuRows(*state_, rows);
      if (selected_ < 0 || selected_ >= n) return true;
      v.menuSelected = selected_;
      switch (rows[selected_]) {
        case MenuRow::Collect:
          collectPills();
          break;
        case MenuRow::Recipes: {
          RenderLock lock(*this);
          notice_[0] = '\0';
          refreshAlchemy();
          selected_ = 0;
          screen_ = Screen::Recipes;
          requestUpdate();
          break;
        }
        case MenuRow::Experiment: {
          RenderLock lock(*this);
          if (alchemy::brewing(*state_)) {
            snprintf(notice_, sizeof(notice_), "%s", ctr(CULT_RECIPE_BUSY));
          } else {
            notice_[0] = '\0';
            v.flavor[0] = '\0';
            v.chosenCount = 0;
            refreshAlchemy();
            selected_ = 0;
            screen_ = Screen::Experiment;
          }
          requestUpdate();
          break;
        }
        case MenuRow::Bag: {
          // The Bag lives in the exploration view; one is made just for it.
          auto bag = explore_ ? nullptr : makeUniqueNoThrow<ExploreView>();
          if (!explore_ && !bag) {
            LOG_ERR("CULT", "Out of memory for the bag (%u bytes)", static_cast<unsigned>(sizeof(ExploreView)));
            return true;
          }
          {
            RenderLock lock(*this);
            if (bag) {
              explore_ = std::move(bag);
              explore_->forAlchemy = true;
            }
          }
          openBag(Screen::Alchemy);
          break;
        }
      }
      return true;
    }

    case Screen::Recipes:
      if (back) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        selected_ = v.menuSelected;
        screen_ = Screen::Alchemy;
        requestUpdate();
      } else if (confirm && selected_ >= 0 && selected_ < v.recipeCount) {
        RenderLock lock(*this);
        v.recipe = v.recipes[selected_];
        v.batch = 1;
        notice_[0] = '\0';
        screen_ = Screen::RecipeDetail;
        requestUpdate();
      } else {
        handleList(v.recipeCount, up, down);
      }
      return true;

    case Screen::RecipeDetail:
      if (back) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        selected_ = 0;
        for (uint8_t i = 0; i < v.recipeCount; ++i) {
          if (v.recipes[i] == v.recipe) selected_ = i;
        }
        screen_ = Screen::Recipes;
        requestUpdate();
      } else if (confirm) {
        brewSelected();
      } else if (up || down) {  // v0.2.1: how many brews in one go
        const uint8_t most = alchemy::maxBatch(*state_, v.recipe);
        if (most > 1) {
          RenderLock lock(*this);
          v.batch = static_cast<uint8_t>(down ? (v.batch % most) + 1 : (v.batch + most - 2) % most + 1);
          requestUpdate();
        }
      }
      return true;

    case Screen::Experiment: {
      if (back) {
        RenderLock lock(*this);
        notice_[0] = '\0';
        v.chosenCount = 0;
        selected_ = v.menuSelected;
        screen_ = Screen::Alchemy;
        requestUpdate();
        return true;
      }
      if (!confirm) {
        handleList(alchemyRows(), up, down);
        return true;
      }
      const int head = experimentHeadRows(v.chosenCount);
      if (v.chosenCount >= 2 && selected_ == 0) {
        mixExperiment();
        return true;
      }
      RenderLock lock(*this);
      if (v.chosenCount >= 1 && selected_ == head - 1) {  // Start over
        v.chosenCount = 0;
        selected_ = 0;
      } else if (selected_ >= head && selected_ - head < v.ingredientCount && v.chosenCount < alchemy::kMaxParts) {
        const inventory::Item item = v.ingredients[selected_ - head];
        uint8_t inCauldron = 0;
        for (uint8_t i = 0; i < v.chosenCount; ++i)
          inCauldron = static_cast<uint8_t>(inCauldron + (v.chosen[i] == item));
        if (inventory::count(*state_, item) > inCauldron) {
          v.chosen[v.chosenCount++] = item;
          // Keep the cursor on the same ingredient as the rows above it change.
          selected_ = experimentHeadRows(v.chosenCount) + (selected_ - head);
        }
      }
      requestUpdate();
      return true;
    }

    default:
      return true;
  }
}

void CultivationActivity::alchemyLabels(const char*& confirmLabel, const char*& upLabel, const char*& downLabel) const {
  confirmLabel = tr(STR_SELECT);
  upLabel = tr(STR_DIR_UP);
  downLabel = tr(STR_DIR_DOWN);
  if (!alchemy_) {
    confirmLabel = upLabel = downLabel = "";
    return;
  }
  const AlchemyView& v = *alchemy_;
  const int rows = alchemyRows();
  switch (screen_) {
    case Screen::Recipes:
      if (v.recipeCount == 0) confirmLabel = "";
      break;
    case Screen::RecipeDetail:
      confirmLabel = alchemy::canBrew(*state_, v.recipe) ? ctr(CULT_BREW) : "";
      upLabel = downLabel = "";
      if (alchemy::maxBatch(*state_, v.recipe) > 1) {
        upLabel = ctr(CULT_FEWER);
        downLabel = ctr(CULT_MORE);
      }
      return;
    case Screen::Experiment: {
      const int head = experimentHeadRows(v.chosenCount);
      if (selected_ >= head) {
        confirmLabel = v.chosenCount < alchemy::kMaxParts ? ctr(CULT_ADD) : "";
      }
      break;
    }
    default:
      break;
  }
  if (rows < 2) upLabel = downLabel = "";
}

// --- Drawing ------------------------------------------------------------------------------------

void CultivationActivity::renderAlchemy(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!alchemy_) return;
  const AlchemyView& v = *alchemy_;
  const GameState& s = *state_;
  char line[224];
  const int recipe = alchemy::brewingRecipe(s);
  if (recipe < 0) {
    snprintf(line, sizeof(line), "%s", ctr(CULT_CAULDRON_EMPTY));
  } else {
    const char* pill = pillLabel(alchemy::recipesOf(s).recipes[recipe].pill);
    if (alchemy::ready(s)) {
      snprintf(line, sizeof(line), ctr(CULT_CAULDRON_READY_FORMAT), pill);
    } else {
      snprintf(line, sizeof(line), ctr(CULT_CAULDRON_BREWING_FORMAT), pill,
               static_cast<unsigned>(alchemy::brewMinutesLeft(s)));
    }
  }
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 2);
  y += 4;
  if (notice_[0] != '\0') y = drawWrapped(UI_10_FONT_ID, x, y, width, notice_, 3);
  if (v.flavor[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y + 2, width, v.flavor, 3);
  y += metrics.verticalSpacing;

  MenuRow rows[4];
  const uint8_t n = menuRows(s, rows);
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * n;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, n, selected_,
      [this](const int i) -> std::string {
        MenuRow r[4];
        menuRows(*state_, r);
        switch (r[i]) {
          case MenuRow::Collect:
            return ctr(CULT_ALCHEMY_COLLECT);
          case MenuRow::Recipes:
            return ctr(CULT_ALCHEMY_RECIPES);
          case MenuRow::Experiment:
            return ctr(CULT_ALCHEMY_EXPERIMENT);
          case MenuRow::Bag:
            return ctr(CULT_BAG);
        }
        return {};
      },
      [this](const int i) -> std::string {
        MenuRow r[4];
        menuRows(*state_, r);
        char sub[160];
        switch (r[i]) {
          case MenuRow::Collect:
            return ctr(CULT_ALCHEMY_COLLECT_SUB);
          case MenuRow::Recipes:
            snprintf(sub, sizeof(sub), ctr(CULT_ALCHEMY_RECIPES_SUB_FORMAT),
                     static_cast<unsigned>(alchemy::knownCount(*state_)));
            return sub;
          case MenuRow::Experiment:
            return alchemy::brewing(*state_) ? ctr(CULT_ALCHEMY_BUSY_SUB) : ctr(CULT_ALCHEMY_EXPERIMENT_SUB);
          case MenuRow::Bag:
            snprintf(sub, sizeof(sub), ctr(CULT_BAG_SUB_FORMAT), static_cast<unsigned long>(state_->progress.stones),
                     static_cast<unsigned>(inventory::kinds(*state_)));
            return sub;
        }
        return {};
      });
}

void CultivationActivity::renderRecipes(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!alchemy_) return;
  const AlchemyView& v = *alchemy_;
  if (v.recipeCount == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RECIPES_NONE), 8);
    return;
  }
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * v.recipeCount;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, v.recipeCount, selected_,
      [this](const int i) -> std::string {
        const alchemy::Recipe r = alchemy::recipesOf(*state_).recipes[alchemy_->recipes[i]];
        char title[64];
        snprintf(title, sizeof(title), "%s", pillLabel(r.pill));
        capitalise(title);
        return title;
      },
      [this](const int i) -> std::string {
        const alchemy::Recipe r = alchemy::recipesOf(*state_).recipes[alchemy_->recipes[i]];
        char sub[96];
        snprintf(sub, sizeof(sub), ctr(CULT_RECIPE_SUB_FORMAT), static_cast<unsigned>(r.brewMinutes),
                 static_cast<unsigned>(alchemy::batchesHeld(*state_, r)));
        return sub;
      });
}

void CultivationActivity::renderRecipeDetail(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!alchemy_) return;
  const AlchemyView& v = *alchemy_;
  const alchemy::Recipe r = alchemy::recipesOf(*state_).recipes[v.recipe];
  char line[224];
  char name[64];
  // What it makes: the pill's own description (its quality is rolled when it is taken out).
  const inventory::Item made = alchemy::pillItem(r.pill, inventory::Quality::Medium);
  char desc[128];
  ui::itemDescription(made, desc, sizeof(desc));
  snprintf(name, sizeof(name), "%s", pillLabel(r.pill));
  capitalise(name);
  snprintf(line, sizeof(line), ctr(CULT_RECIPE_MAKES_FORMAT), name);
  renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  if (desc[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, desc, 2);
  y += metrics.verticalSpacing;

  renderer.drawText(UI_10_FONT_ID, x, y, ctr(CULT_RECIPE_PARTS), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  for (uint8_t p = 0; p < r.parts;) {  // parts are sorted: repeats are next to each other
    uint8_t same = 1;
    while (p + same < r.parts && r.part[p + same] == r.part[p]) ++same;
    snprintf(line, sizeof(line), ctr(CULT_RECIPE_PART_FORMAT), static_cast<unsigned>(same),
             itemLabel(r.part[p], name, sizeof(name)), static_cast<unsigned>(inventory::count(*state_, r.part[p])));
    renderer.drawText(UI_10_FONT_ID, x + 16, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    p = static_cast<uint8_t>(p + same);
  }
  y += metrics.verticalSpacing;
  snprintf(line, sizeof(line), ctr(CULT_RECIPE_TIME_FORMAT), static_cast<unsigned>(r.brewMinutes),
           static_cast<unsigned>(alchemy::qualityBonus(*state_, r)));
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 4) + metrics.verticalSpacing;
  // v0.2.1: the batch (Up/Down when the bag holds parts for more than one).
  if (alchemy::maxBatch(*state_, v.recipe) > 1) {
    const uint8_t batch = v.batch <= alchemy::maxBatch(*state_, v.recipe) ? v.batch : 1;
    snprintf(line, sizeof(line), ctr(CULT_BATCH_FORMAT), static_cast<unsigned>(batch),
             static_cast<unsigned>(alchemy::batchMinutes(r, batch)));
    renderer.drawText(UI_10_FONT_ID, x, y, line, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  }
  if (notice_[0] != '\0') {
    drawWrapped(UI_10_FONT_ID, x, y, width, notice_, 3);
  } else if (alchemy::brewing(*state_)) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RECIPE_BUSY), 2);
  } else if (alchemy::batchesHeld(*state_, r) == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_RECIPE_MISSING), 2);
  }
  (void)bottom;
}

void CultivationActivity::renderExperiment(const int x, int y, const int width, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!alchemy_) return;
  const AlchemyView& v = *alchemy_;
  char line[224];
  if (v.chosenCount == 0) {
    snprintf(line, sizeof(line), "%s", ctr(CULT_EXPERIMENT_NOTHING));
  } else {
    char parts[200] = {};
    char name[64];
    for (uint8_t i = 0; i < v.chosenCount; ++i) {
      const size_t used = strlen(parts);
      snprintf(parts + used, sizeof(parts) - used, "%s%s", i > 0 ? " + " : "",
               itemLabel(v.chosen[i], name, sizeof(name)));
    }
    snprintf(line, sizeof(line), ctr(CULT_EXPERIMENT_CHOSEN_FORMAT), parts);
  }
  y = drawWrapped(UI_10_FONT_ID, x, y, width, line, 3) + metrics.verticalSpacing;
  if (notice_[0] != '\0') y = drawWrapped(SMALL_FONT_ID, x, y, width, notice_, 2) + 4;
  if (v.ingredientCount == 0 && v.chosenCount == 0) {
    drawWrapped(UI_10_FONT_ID, x, y, width, ctr(CULT_EXPERIMENT_NONE), 4);
    return;
  }
  const int rows = alchemyRows();
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  int height = rowHeight * rows;
  if (y + height > bottom) height = bottom - y;
  GUI.drawList(
      renderer, Rect{0, y, renderer.getScreenWidth(), height}, rows, selected_,
      [this](const int i) -> std::string {
        const AlchemyView& av = *alchemy_;
        const int head = experimentHeadRows(av.chosenCount);
        if (i < head) {
          return av.chosenCount >= 2 && i == 0 ? ctr(CULT_EXPERIMENT_MIX) : ctr(CULT_EXPERIMENT_CLEAR);
        }
        char name[64];
        return itemLabel(av.ingredients[i - head], name, sizeof(name));
      },
      [this](const int i) -> std::string {
        const AlchemyView& av = *alchemy_;
        const int head = experimentHeadRows(av.chosenCount);
        if (i < head) return av.chosenCount >= 2 && i == 0 ? ctr(CULT_EXPERIMENT_MIX_SUB) : std::string();
        const inventory::Item item = av.ingredients[i - head];
        uint8_t inCauldron = 0;
        for (uint8_t k = 0; k < av.chosenCount; ++k)
          inCauldron = static_cast<uint8_t>(inCauldron + (av.chosen[k] == item));
        char desc[96];
        ui::itemDescription(item, desc, sizeof(desc));
        char sub[160];
        const unsigned held = static_cast<unsigned>(inventory::count(*state_, item) - inCauldron);
        snprintf(sub, sizeof(sub), ctr(CULT_BAG_HELD_FORMAT), held, desc);
        return sub;
      });
}

#endif  // CROSSINK_CULTIVATION
