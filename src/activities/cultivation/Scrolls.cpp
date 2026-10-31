#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstring>

#include "Rng.h"
#include "Scrolls.h"
#include "Simulation.h"

namespace cultivation {
namespace scrolls {
namespace {

constexpr uint32_t kKindKind = 60;  // contentSeed kinds 50-57 are rumor, life log, breakthrough lines

// DOC: tier thresholds in hours read in that book.
static const uint8_t kTierHours[static_cast<int>(Tier::Count)] = {0, 3, 8, 15, 25};
// DOC: Mortal x1.15, Yellow x1.30, Earth x1.50, Heaven x1.75, Saint x2.00.
static const uint16_t kManualTierPermille[static_cast<int>(Tier::Count)] = {1150, 1300, 1500, 1750, 2000};
// Proposed sale prices, Spirit Stones by tier (about a fifth of what reading the book earned).
static const uint16_t kSellPrice[static_cast<int>(Tier::Count)] = {25, 60, 150, 300, 500};
// Bucket order in CrossInk's stats: Morning, Afternoon, Evening, Night.
static const Element kBucketElement[4] = {Element::Wood, Element::Fire, Element::Metal, Element::Water};

uint8_t* slotBytes(GameState& state, const uint8_t index) {
  return state.techniqueScrolls.data() + 1 + index * kSlotSize;
}
const uint8_t* slotBytes(const GameState& state, const uint8_t index) {
  return state.techniqueScrolls.data() + 1 + index * kSlotSize;
}

uint8_t& sinceManual(GameState& state) { return state.techniqueMeta[0]; }

}  // namespace

Tier tierForHours(const uint32_t hours) {
  uint8_t tier = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(Tier::Count); ++i) {
    if (hours >= kTierHours[i]) tier = i;
  }
  return static_cast<Tier>(tier);
}

Quality qualityFor(const uint8_t c) {
  if (c >= 98) return Quality::Perfected;
  if (c >= 90) return Quality::Refined;
  if (c >= 75) return Quality::Complete;
  if (c >= 50) return Quality::Incomplete;
  return Quality::Fragmentary;
}

Element elementForBuckets(const uint32_t seconds[4]) {
  uint64_t total = 0;
  uint8_t top = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    total += seconds[i];
    if (seconds[i] > seconds[top]) top = i;
  }
  // Strictly more than 40%: seconds * 100 > total * 40.
  if (total == 0 || static_cast<uint64_t>(seconds[top]) * 100u <= total * 40u) return Element::Earth;
  return kBucketElement[top];
}

Kind kindFor(const uint32_t titleHash, const uint32_t worldSeed, const uint8_t since) {
  if (since >= kManualPity) return Kind::Manual;
  const uint32_t roll = contentSeed(worldSeed, kKindKind, titleHash) % static_cast<uint32_t>(Kind::Count);
  return static_cast<Kind>(roll);
}

uint32_t manualPermille(const Scroll& s) {
  // balance_sim.py: quality 0.90 .. 1.10 over comprehension 75 .. 100%; lower comprehension stays at 0.90.
  const uint32_t c = s.comprehension < 75 ? 75 : (s.comprehension > 100 ? 100 : s.comprehension);
  const uint32_t quality = 900u + (c - 75u) * 8u;  // 900 .. 1100
  return static_cast<uint32_t>(kManualTierPermille[static_cast<int>(tierOf(s))]) * quality / 1000u;
}

uint32_t sellPrice(const Scroll& s) { return kSellPrice[static_cast<int>(tierOf(s))]; }

uint32_t strength(const Scroll& s) { return static_cast<uint32_t>(tierOf(s)) * 1000u + s.comprehension; }

uint8_t count(const GameState& state) {
  const uint8_t n = state.techniqueScrolls[0];
  return n < kCapacity ? n : kCapacity;
}

Scroll get(const GameState& state, const uint8_t index) {
  Scroll s;
  if (index >= count(state)) return s;
  const uint8_t* b = slotBytes(state, index);
  s.titleHash = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
                (static_cast<uint32_t>(b[3]) << 24);
  s.hours = b[4];
  const uint8_t element = b[5] & 0x07;
  const uint8_t kind = (b[5] >> 3) & 0x07;
  s.element = element < static_cast<uint8_t>(Element::Count) ? static_cast<Element>(element) : Element::Earth;
  s.kind = kind < static_cast<uint8_t>(Kind::Count) ? static_cast<Kind>(kind) : Kind::Sword;
  s.comprehension = b[6] <= 100 ? b[6] : 100;
  s.flags = b[7];
  return s;
}

void put(GameState& state, const uint8_t index, const Scroll& s) {
  if (index >= kCapacity) return;
  uint8_t* b = slotBytes(state, index);
  b[0] = static_cast<uint8_t>(s.titleHash);
  b[1] = static_cast<uint8_t>(s.titleHash >> 8);
  b[2] = static_cast<uint8_t>(s.titleHash >> 16);
  b[3] = static_cast<uint8_t>(s.titleHash >> 24);
  b[4] = s.hours;
  b[5] = static_cast<uint8_t>((static_cast<uint8_t>(s.element) & 0x07) | ((static_cast<uint8_t>(s.kind) & 0x07) << 3));
  b[6] = s.comprehension;
  b[7] = s.flags;
}

Scroll removeAt(GameState& state, const uint8_t index) {
  const uint8_t n = count(state);
  if (index >= n) return Scroll{};
  const Scroll removed = get(state, index);
  uint8_t* at = slotBytes(state, index);
  memmove(at, at + kSlotSize, static_cast<size_t>(n - index - 1) * kSlotSize);
  memset(slotBytes(state, static_cast<uint8_t>(n - 1)), 0, kSlotSize);  // all-zero = empty
  state.techniqueScrolls[0] = static_cast<uint8_t>(n - 1);
  return removed;
}

int find(const GameState& state, const uint32_t titleHash) {
  const uint8_t n = count(state);
  for (uint8_t i = 0; i < n; ++i) {
    if (get(state, i).titleHash == titleHash) return i;
  }
  return -1;
}

int equippedManual(const GameState& state) {
  const uint8_t n = count(state);
  for (uint8_t i = 0; i < n; ++i) {
    if ((get(state, i).flags & Scroll::kEquippedManual) != 0) return i;
  }
  return -1;
}

uint8_t equippedTechniques(const GameState& state) {
  uint8_t equipped = 0;
  const uint8_t n = count(state);
  for (uint8_t i = 0; i < n; ++i) {
    if ((get(state, i).flags & Scroll::kEquippedTechnique) != 0) ++equipped;
  }
  return equipped;
}

int bestManual(const GameState& state) {
  int best = -1;
  uint32_t bestPermille = 0;
  const uint8_t n = count(state);
  for (uint8_t i = 0; i < n; ++i) {
    const Scroll s = get(state, i);
    if (s.kind != Kind::Manual) continue;
    const uint32_t p = manualPermille(s);
    if (p > bestPermille) {
      best = i;
      bestPermille = p;
    }
  }
  return best;
}

bool equip(GameState& state, const uint8_t index) {
  if (index >= count(state)) return false;
  Scroll s = get(state, index);
  if (s.equipped()) return true;
  if (s.kind == Kind::Manual) {
    const int old = equippedManual(state);
    if (old >= 0) unequip(state, static_cast<uint8_t>(old));
    s.flags |= Scroll::kEquippedManual;
  } else {
    if (equippedTechniques(state) >= kMaxEquippedTechniques) return false;
    s.flags |= Scroll::kEquippedTechnique;
  }
  put(state, index, s);
  return true;
}

void unequip(GameState& state, const uint8_t index) {
  if (index >= count(state)) return;
  Scroll s = get(state, index);
  s.flags &= static_cast<uint8_t>(~(Scroll::kEquippedManual | Scroll::kEquippedTechnique));
  put(state, index, s);
}

uint32_t sell(GameState& state, const uint8_t index) {
  if (index >= count(state) || get(state, index).equipped()) return 0;
  const uint32_t price = sellPrice(get(state, index));
  removeAt(state, index);
  const uint32_t room = UINT32_MAX - state.progress.stones;
  state.progress.stones += price < room ? price : room;
  return price;
}

uint32_t equippedManualPermille(const GameState& state) {
  const int m = equippedManual(state);
  return m >= 0 ? manualPermille(get(state, static_cast<uint8_t>(m))) : 1000u;
}

bool manualMatchesRoot(const GameState& state) {
  const int m = equippedManual(state);
  return m >= 0 && get(state, static_cast<uint8_t>(m)).element == state.character.rootElement;
}

Tier keyTierFor(const uint8_t targetRealm) {
  if (targetRealm <= 1) return Tier::Count;  // the small bottleneck: no key
  if (targetRealm == 2) return Tier::Mortal;
  if (targetRealm == 3) return Tier::Yellow;
  if (targetRealm == 4) return Tier::Earth;
  return Tier::Heaven;
}

int keyFor(const GameState& state, const uint8_t targetRealm) {
  const Tier need = keyTierFor(targetRealm);
  if (need == Tier::Count) return -1;
  int best = -1;
  uint32_t bestStrength = 0;
  const uint8_t n = count(state);
  for (uint8_t i = 0; i < n; ++i) {
    const Scroll s = get(state, i);
    if (s.equipped() || tierOf(s) < need) continue;
    const uint32_t str = strength(s);
    if (best < 0 || str < bestStrength) {  // strictly weaker: the oldest wins a tie
      best = i;
      bestStrength = str;
    }
  }
  return best;
}

uint8_t mint(GameState& state, const BookRecord& book, const bool alreadyMinted, Event out[2]) {
  const uint32_t hash = book.titleHash != 0 ? book.titleHash : 1u;
  const uint8_t share = book.shareRead <= 100 ? book.shareRead : 100;

  // A reread: the scroll already in the library gains quality, never a second copy.
  const int existing = find(state, hash);
  if (existing >= 0) {
    Scroll s = get(state, static_cast<uint8_t>(existing));
    out[0].scroll = s;
    out[0].oldComprehension = s.comprehension;
    if (share > s.comprehension) {
      s.comprehension = share;
      put(state, static_cast<uint8_t>(existing), s);
      out[0].kind = EventKind::Improved;
      out[0].scroll = s;
    } else {
      out[0].kind = EventKind::Unchanged;
    }
    return 1;
  }

  Scroll s;
  s.titleHash = hash;
  const uint32_t hours = book.seconds / 3600u;
  s.hours = static_cast<uint8_t>(hours < 255 ? hours : 255);
  if (book.bucket < 4) {
    s.element = kBucketElement[book.bucket];
  } else {
    s.element = Element::Earth;
  }
  s.comprehension = share;

  if (alreadyMinted) {  // spent as a key or sold earlier in this life
    out[0].kind = EventKind::AlreadyUsed;
    out[0].scroll = s;
    return 1;
  }
  if (book.seconds < kMinBookSeconds) {
    out[0].kind = EventKind::TooShort;
    out[0].scroll = s;
    out[0].minutes = static_cast<uint16_t>(book.seconds / 60u);
    return 1;
  }

  s.kind = kindFor(hash, worldSeedOf(state), sinceManual(state));
  sinceManual(state) = s.kind == Kind::Manual ? 0 : static_cast<uint8_t>(sinceManual(state) + 1);

  uint8_t events = 0;
  uint8_t n = count(state);
  if (n >= kCapacity) {
    // Full: the weakest unequipped scroll goes, which may be the new one.
    int weakest = -1;
    uint32_t weakestStrength = 0;
    for (uint8_t i = 0; i < n; ++i) {
      const Scroll held = get(state, i);
      if (held.equipped()) continue;
      if (weakest < 0 || strength(held) < weakestStrength) {
        weakest = i;
        weakestStrength = strength(held);
      }
    }
    if (weakest < 0 || strength(s) <= weakestStrength) {
      out[0].kind = EventKind::Minted;
      out[0].scroll = s;
      out[1].kind = EventKind::AutoSold;
      out[1].scroll = s;
      out[1].stones = sellPrice(s);
      const uint32_t room = UINT32_MAX - state.progress.stones;
      state.progress.stones += out[1].stones < room ? out[1].stones : room;
      return 2;
    }
    out[1].kind = EventKind::AutoSold;
    out[1].scroll = get(state, static_cast<uint8_t>(weakest));
    out[1].stones = sell(state, static_cast<uint8_t>(weakest));
    events = 1;
    n = count(state);
  }
  put(state, n, s);
  state.techniqueScrolls[0] = static_cast<uint8_t>(n + 1);
  out[0].kind = EventKind::Minted;
  out[0].scroll = s;
  return static_cast<uint8_t>(events + 1);
}

}  // namespace scrolls
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
