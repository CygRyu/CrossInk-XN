#include "CultivationEngine.h"

#include <algorithm>
#include <cstdio>

namespace Cultivation {
namespace {

struct RealmDef {
  const char* name;
  uint32_t qiPerLayer;
};

// Pacing guide: a typical novel is ~300 pages / ~8h, i.e. roughly 3,500 Qi
// including the finish bonus. Cumulative Qi at the start of each realm:
// 0, 900, 4.5k, 18k, 63k, 207k, 657k.
constexpr RealmDef kRealms[kRealmCount] = {
    {"Qi Condensation", 100},   {"Foundation Establishment", 400}, {"Core Formation", 1500},
    {"Nascent Soul", 5000},     {"Spirit Severing", 16000},         {"Dao Seeking", 50000},
    {"True Immortal", 150000},
};

constexpr const char* kSurnames[] = {"Lin", "Chen", "Xiao", "Mo", "Yan", "Bai", "Shen", "Liu", "Ye", "Han", "Su", "Gu"};
constexpr const char* kGivenNames[] = {"Wuyan",  "Qingshan", "Tianxu", "Ruoxi", "Zhaoyang", "Hanyue",
                                       "Jingyu", "Mingche",  "Xuelan", "Fengyi", "Longwei", "Yunshu"};

constexpr const char* kEnemies[kRealmCount][4] = {
    {"starving mountain wolf", "bandit scout", "wandering ghost", "poisonous toad spirit"},
    {"rogue cultivator", "iron-hide boar", "corpse puppet", "river serpent"},
    {"demonic sect disciple", "thunder-eyed tiger", "spirit-eating vine", "reclusive cave elder"},
    {"blood-robed elder", "nine-tailed fox", "ancient guardian puppet", "storm drake"},
    {"patriarch's wrathful shadow", "void-walking beast", "tribulation wraith", "sword-saint remnant"},
    {"celestial general", "dao-eating demon", "thousand-year dragon", "fallen immortal"},
    {"heavenly emperor's avatar", "chaos devourer", "tribulation sovereign", "primordial phoenix"},
};

constexpr const char* kHerbs[kRealmCount][3] = {
    {"a bundle of moonlit grass", "three jade-dew mushrooms", "a handful of cinnabar seeds"},
    {"a ten-year spirit ginseng", "frost lotus petals", "a pouch of blood-qi berries"},
    {"a hundred-year spirit ginseng", "a nine-leaf immortal herb", "thunder-struck bamboo"},
    {"a thousand-year snow lotus", "dragon-vein moss", "a phoenix-ash blossom"},
    {"a heaven-grade pill herb", "a root of the world tree", "starfall dew"},
    {"a dao-flavoured lotus seed", "an essence of primordial chaos", "a fruit of the sunless tree"},
    {"an immortal's tear", "a seed of creation", "a shard of heavenly jade"},
};

constexpr const char* kPlaces[] = {"a misty ravine",       "a collapsed sect archive", "a moonlit bamboo grove",
                                   "an abandoned pagoda",  "a hidden spirit spring",   "a windswept cliff-top shrine",
                                   "a forgotten tomb",     "a lantern-lit mountain pass"};

constexpr const char* kTechniques[] = {"Nine Heavens Breathing Art",  "Silent Thunder Palm",   "Jade Mirror Mind Sutra",
                                       "Wandering Cloud Footwork",    "Crimson Lotus Sword",   "Heavenly Eye Scripture",
                                       "Mountain-Bearing Body Forge", "Starlit Dao Verses"};

constexpr const char* kInsights[] = {
    "The old texts you studied suddenly make sense, and your Qi settles like still water.",
    "A line from your reading echoes in your dantian, and your meridians widen a little.",
    "You realise the author's words were a cultivation method all along.",
    "Hours of quiet study sharpen your spirit sense.",
    "Your focus deepens; distractions fall away like autumn leaves.",
    "Knowledge turns into Qi, page by page.",
};

// Duel outcomes, per path. Index 0 is also used while the path is undecided.
constexpr const char* kDuelOutcomes[4][3] = {
    {"You prevail after a tense exchange.", "A single clean strike ends the fight.", "You drive the foe away."},
    {"Your sect's techniques prove decisive; you spare the foe.", "Righteous Qi cleaves through your enemy.",
     "You win honourably and bow to the fallen."},
    {"You devour the foe's essence and leave nothing behind.", "Blood Qi surges; the foe never stood a chance.",
     "You take their treasure and leave the corpse for the crows."},
    {"You slip away into the mist before they can recover.", "You win by cunning, then vanish without a name.",
     "A dirty trick, a clean escape, and an enemy who will never forget you."},
};

constexpr const char* kTribulations[] = {
    "Heavenly thunder gathers, yet you weather it with minor scorch marks.",
    "A lesser tribulation cloud passes overhead; you endure it.",
    "A heart-demon whispers during meditation, and you silence it.",
};

constexpr uint32_t kUint32Max = 0xFFFFFFFFu;

struct Rng {
  uint32_t state;
  explicit Rng(uint32_t s) : state(s ? s : 0x9E3779B9u) {
    for (int i = 0; i < 4; ++i) next();
  }
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  uint32_t pick(uint32_t n) { return n ? next() % n : 0; }
};

template <typename T, size_t N>
constexpr size_t countOf(const T (&)[N]) {
  return N;
}

void stageText(uint16_t stage, char* buf, size_t size) {
  const uint8_t realm = static_cast<uint8_t>(std::min<size_t>(stage / kLayersPerRealm, kRealmCount - 1));
  const unsigned layer = (stage % kLayersPerRealm) + 1;
  if (layer == 1) {
    snprintf(buf, size, "Breakthrough! Thunder rolls as you enter the %s realm.", kRealms[realm].name);
  } else {
    snprintf(buf, size, "Breakthrough! Your cultivation reaches %s, layer %u.", kRealms[realm].name, layer);
  }
}

void randomEvent(Rng& rng, uint8_t tier, Path path, JournalEntry& out) {
  const uint8_t pathIdx = static_cast<uint8_t>(path) <= 3 ? static_cast<uint8_t>(path) : 0;
  switch (rng.pick(6)) {
    case 0:
      snprintf(out.text, sizeof(out.text), "You meditate in %s. %s", kPlaces[rng.pick(countOf(kPlaces))],
               kInsights[rng.pick(countOf(kInsights))]);
      break;
    case 1:
      snprintf(out.text, sizeof(out.text), "A %s blocks your path. %s", kEnemies[tier][rng.pick(4)],
               kDuelOutcomes[pathIdx][rng.pick(3)]);
      break;
    case 2:
      snprintf(out.text, sizeof(out.text), "You gather %s from %s.", kHerbs[tier][rng.pick(3)],
               kPlaces[rng.pick(countOf(kPlaces))]);
      break;
    case 3:
      snprintf(out.text, sizeof(out.text), "In %s you uncover a fragment of the %s.", kPlaces[rng.pick(countOf(kPlaces))],
               kTechniques[rng.pick(countOf(kTechniques))]);
      break;
    case 4:
      snprintf(out.text, sizeof(out.text), "%s", kInsights[rng.pick(countOf(kInsights))]);
      break;
    default:
      snprintf(out.text, sizeof(out.text), "%s", kTribulations[rng.pick(countOf(kTribulations))]);
      break;
  }
}

}  // namespace

uint32_t qiFromReading(const ReadingInputs& in) {
  const uint64_t qi = static_cast<uint64_t>(in.pages) * 5u + static_cast<uint64_t>(in.seconds / 60u) * 3u +
                      static_cast<uint64_t>(in.books) * 600u + static_cast<uint64_t>(in.longestStreak) * 25u;
  return qi > kUint32Max ? kUint32Max : static_cast<uint32_t>(qi);
}

bool applyLedger(Ledger& ledger, const uint32_t derivedNow) {
  if (derivedNow > ledger.lastDerived) {
    const uint32_t gain = derivedNow - ledger.lastDerived;
    ledger.banked = (kUint32Max - ledger.banked < gain) ? kUint32Max : ledger.banked + gain;
    ledger.lastDerived = derivedNow;
    return true;
  }
  if (derivedNow < ledger.lastDerived) {
    ledger.lastDerived = derivedNow;  // stats were reset; rebase, never lose Qi
    return true;
  }
  return false;
}

Progress progressFor(uint32_t qi) {
  Progress p;
  for (uint8_t r = 0; r < kRealmCount; ++r) {
    const uint64_t realmTotal = static_cast<uint64_t>(kRealms[r].qiPerLayer) * kLayersPerRealm;
    if (qi < realmTotal) {
      p.realm = r;
      p.layer = static_cast<uint8_t>(qi / kRealms[r].qiPerLayer) + 1;
      p.qiIntoLayer = qi % kRealms[r].qiPerLayer;
      p.qiForLayer = kRealms[r].qiPerLayer;
      p.maxed = false;
      return p;
    }
    qi -= static_cast<uint32_t>(realmTotal);
  }
  p.realm = kRealmCount - 1;
  p.layer = kLayersPerRealm;
  p.qiForLayer = kRealms[kRealmCount - 1].qiPerLayer;
  p.qiIntoLayer = p.qiForLayer;
  p.maxed = true;
  return p;
}

uint16_t stageIndex(const Progress& p) { return static_cast<uint16_t>(p.realm * kLayersPerRealm + (p.layer - 1)); }

const char* realmName(const uint8_t realm) { return kRealms[std::min<size_t>(realm, kRealmCount - 1)].name; }

bool pathChoiceAvailable(const Progress& p) { return p.realm >= kPathChoiceRealm; }

const char* pathName(const Path path) {
  switch (path) {
    case Path::Orthodox:
      return "Orthodox";
    case Path::Demonic:
      return "Demonic";
    case Path::Rogue:
      return "Rogue";
    case Path::Undecided:
    default:
      return "Undecided";
  }
}

Attributes attributesFor(const ReadingInputs& in) {
  Attributes a;
  a.body = in.seconds / 3600u;
  a.insight = in.pages / 25u;
  a.resolve = in.longestStreak;
  a.fortune = in.books;
  return a;
}

uint32_t combatPower(const Progress& p, const Attributes& a) {
  const uint64_t stage = stageIndex(p);
  const uint64_t power =
      10u + stage * stage * 3u + static_cast<uint64_t>(a.body) * 2u + a.insight + static_cast<uint64_t>(a.resolve) * 3u +
      static_cast<uint64_t>(a.fortune) * 5u;
  return power > kUint32Max ? kUint32Max : static_cast<uint32_t>(power);
}

void characterName(const uint32_t seed, char* buf, const size_t bufSize) {
  if (!buf || bufSize == 0) return;
  Rng rng(seed ^ 0xA5A5A5A5u);
  snprintf(buf, bufSize, "%s %s", kSurnames[rng.pick(countOf(kSurnames))], kGivenNames[rng.pick(countOf(kGivenNames))]);
}

size_t generateJournal(const uint32_t seed, const Path path, const uint32_t qiFrom, const uint32_t qiTo,
                       JournalEntry* out, const size_t max) {
  const size_t cap = std::min(max, kMaxJournalEntries);
  if (!out || cap == 0 || qiTo <= qiFrom) return 0;

  const Progress before = progressFor(qiFrom);
  const Progress after = progressFor(qiTo);
  const uint16_t stageBefore = stageIndex(before);
  const uint16_t stageAfter = stageIndex(after);

  size_t n = 0;
  if (stageAfter > stageBefore) {
    const unsigned crossed = stageAfter - stageBefore;
    if (crossed > 2 && n < cap) {
      snprintf(out[n++].text, sizeof(out[0].text), "A long retreat: you push through %u layers in one go.", crossed);
    }
    if (crossed >= 2 && n < cap) {
      stageText(static_cast<uint16_t>(stageAfter - 1), out[n++].text, sizeof(out[0].text));
    }
    if (n < cap) {
      stageText(stageAfter, out[n++].text, sizeof(out[0].text));
    }
  }

  if (n < cap) {
    const uint32_t gain = qiTo - qiFrom;
    const uint32_t unit = std::max<uint32_t>(1, static_cast<uint32_t>(after.qiForLayer / 2));
    const size_t wanted = std::min<size_t>(1u + gain / unit, cap - n);
    Rng rng(seed ^ (qiFrom * 2654435761u) ^ (qiTo * 40503u));
    for (size_t i = 0; i < wanted; ++i) {
      randomEvent(rng, after.realm, path, out[n++]);
    }
  }
  return n;
}

}  // namespace Cultivation
