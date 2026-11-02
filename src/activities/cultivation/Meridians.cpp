#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "Meridians.h"
#include "Simulation.h"

namespace cultivation {
namespace meridians {
namespace {

// DOC: 0, 3, 3, 3, 4, 4, 5, 4, 5, 5 points in realms 0-9 (36 in all).
static const uint8_t kPerRealm[kMortalRealmCount] = {0, 3, 3, 3, 4, 4, 5, 4, 5, 5};

const uint8_t* bits(const GameState& s) { return s.daosAndMeridians.data() + kBitsOffset; }
uint8_t* bits(GameState& s) { return s.daosAndMeridians.data() + kBitsOffset; }

uint8_t limbIndex(const Channel c) { return static_cast<uint8_t>(static_cast<uint8_t>(c) - 2u); }

// Real acupoints in plain English (user, session 9), Ren and Du from the
// lowest point up, then two per limb channel.
static const char* const kPointNames[kPoints] = {
    "Sea of Qi",        "Origin Pass",      "Middle Cavity",   "Chest Centre",       "Heaven's Chimney",
    "Receiving Fluid",  "Long Strong",      "Gate of Life",    "Spirit Path",        "Great Hammer",
    "Wind Palace",      "Hundred Meetings", "Middle Palace",   "Great Abyss",        "Joining Valley",
    "Pool at the Bend", "Leg Three Miles",  "Inner Courtyard", "Three Yin Meeting",  "Sea of Blood",
    "Spirit Gate",      "Utmost Spring",    "Back Ravine",     "Heavenly Gathering", "Inner Pass",
    "Palace of Toil",   "Outer Pass",       "Wind Screen",     "Middle of the Bend", "Bright Eyes",
    "Bubbling Spring",  "Great Ravine",     "Great Surge",     "Gate of Hope",       "Yang Mound Spring",
    "Wind Pool"};
static const char* const kPointPlaces[kPoints] = {"two fingers below the navel",
                                                  "four fingers below the navel",
                                                  "halfway between the navel and the breastbone",
                                                  "between the nipples",
                                                  "in the hollow above the breastbone",
                                                  "below the lower lip",
                                                  "at the tip of the tailbone",
                                                  "on the spine, level with the navel",
                                                  "between the shoulder blades",
                                                  "on the big bone at the base of the neck",
                                                  "just under the skull at the back",
                                                  "at the crown of the head",
                                                  "below the collarbone",
                                                  "at the wrist crease, thumb side",
                                                  "in the web between thumb and forefinger",
                                                  "at the outer end of the elbow crease",
                                                  "four fingers below the knee",
                                                  "between the second and third toes",
                                                  "on the inner shin above the ankle",
                                                  "on the inner thigh above the knee",
                                                  "at the wrist crease, little-finger side",
                                                  "in the hollow of the armpit",
                                                  "on the edge of the hand below the little finger",
                                                  "in the middle of the shoulder blade",
                                                  "on the inner forearm above the wrist",
                                                  "in the centre of the palm",
                                                  "on the back of the forearm above the wrist",
                                                  "behind the earlobe",
                                                  "at the back of the knee",
                                                  "at the inner corner of the eye",
                                                  "on the sole of the foot",
                                                  "behind the inner ankle",
                                                  "on the top of the foot",
                                                  "below the ribs, under the nipple",
                                                  "below the knee on the outer side",
                                                  "under the skull at the side of the neck"};
static const char* const kChannelNames[kChannels] = {"Ren",         "Du",
                                                     "Lung",        "Large Intestine",
                                                     "Stomach",     "Spleen",
                                                     "Heart",       "Small Intestine",
                                                     "Pericardium", "Triple Burner",
                                                     "Bladder",     "Kidney",
                                                     "Liver",       "Gallbladder"};
static const char* const kChannelEffects[kChannels - 2] = {
    "+10 max HP",        "+1 level in fights",       "pills 10% stronger",
    "+5 brew quality",   "+5 on Heart Demon scenes", "10% softer lightning",
    "5% cheaper buying", "15% faster regrowth",      "5% less HP lost in fights",
    "+50% HP healing",   "+1 herb per herb tile",    "+10% to slip away"};
static const Element kChannelElements[kChannels - 2] = {Element::Metal, Element::Metal, Element::Earth, Element::Earth,
                                                        Element::Fire,  Element::Fire,  Element::Fire,  Element::Fire,
                                                        Element::Water, Element::Water, Element::Wood,  Element::Wood};

}  // namespace

Channel channelOf(const uint8_t point) {
  if (point < 6) return Channel::Ren;
  if (point < 12) return Channel::Du;
  const uint8_t limb = static_cast<uint8_t>((point < kPoints ? point - 12u : kPoints - 13u) / 2u);
  return static_cast<Channel>(2u + limb);
}

uint8_t firstPoint(const Channel c) {
  if (c == Channel::Ren) return 0;
  if (c == Channel::Du) return 6;
  return static_cast<uint8_t>(12u + 2u * limbIndex(c));
}

uint8_t pointsIn(const Channel c) { return c == Channel::Ren || c == Channel::Du ? 6 : 2; }

uint8_t prerequisite(const uint8_t point) {
  if (point == 0 || point >= kPoints) return kPoints;  // Sea of Qi comes first
  const Channel c = channelOf(point);
  return point == firstPoint(c) ? 0 : static_cast<uint8_t>(point - 1u);
}

bool isOpen(const GameState& s, const uint8_t point) {
  return point < kPoints && (bits(s)[point / 8u] & (1u << (point % 8u))) != 0;
}

uint8_t openCount(const GameState& s) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kPoints; ++i) n = static_cast<uint8_t>(n + (isOpen(s, i) ? 1 : 0));
  return n;
}

uint8_t openIn(const GameState& s, const Channel c) {
  uint8_t n = 0;
  const uint8_t first = firstPoint(c);
  for (uint8_t i = 0; i < pointsIn(c); ++i) n = static_cast<uint8_t>(n + (isOpen(s, first + i) ? 1 : 0));
  return n;
}

uint8_t nextIn(const GameState& s, const Channel c) {
  const uint8_t first = firstPoint(c);
  for (uint8_t i = 0; i < pointsIn(c); ++i) {
    if (!isOpen(s, first + i)) return static_cast<uint8_t>(first + i);
  }
  return kPoints;
}

uint8_t allowanceThrough(const uint8_t realm) {
  uint8_t n = 0;
  for (uint8_t r = 0; r < kMortalRealmCount && r <= realm; ++r) n = static_cast<uint8_t>(n + kPerRealm[r]);
  return n;
}

uint8_t allowance(const GameState& s) { return allowanceThrough(s.progress.realm); }

uint8_t ready(const GameState& s) {
  const uint8_t a = allowance(s);
  const uint8_t o = openCount(s);
  return a > o ? static_cast<uint8_t>(a - o) : 0;
}

uint8_t costRealm(const uint8_t n) {
  for (uint8_t r = 0; r < kMortalRealmCount; ++r) {
    if (n <= allowanceThrough(r)) return r;
  }
  return kMortalRealmCount - 1;
}

QiNumber costOfNext(const GameState& s) {
  const uint8_t n = static_cast<uint8_t>(openCount(s) + 1u);
  const uint64_t qi = static_cast<uint64_t>(realmInfo(costRealm(n)).totalQi) * kCostPermille / 1000u;
  return QiNumber::fromU64(qi);
}

Check check(const GameState& s, const uint8_t point) {
  if (point >= kPoints || isOpen(s, point)) return Check::AlreadyOpen;
  const uint8_t pre = prerequisite(point);
  if (pre < kPoints && !isOpen(s, pre)) return Check::Locked;
  if (ready(s) == 0) return Check::NoAllowance;
  if (s.progress.qi < costOfNext(s)) return Check::NotEnoughQi;
  return Check::Ok;
}

bool open(GameState& s, const uint8_t point) {
  if (check(s, point) != Check::Ok) return false;
  s.progress.qi.subtract(costOfNext(s));
  bits(s)[point / 8u] = static_cast<uint8_t>(bits(s)[point / 8u] | (1u << (point % 8u)));
  if (channelOf(point) == Channel::Lung) {
    s.progress.hpMax = static_cast<uint16_t>(s.progress.hpMax + kLungHp);
    s.progress.hp = static_cast<uint16_t>(s.progress.hp + kLungHp);
  }
  return true;
}

uint32_t qiPermille(const GameState& s) {
  // 1.1^n kept in millionths so 36 steps of rounding stay well under 0.1%.
  uint64_t m = 1000000u;
  const uint8_t n = openCount(s);
  for (uint8_t i = 0; i < n; ++i) m = m * kQiPerPointPermille / 1000u;
  return static_cast<uint32_t>((m + 500u) / 1000u);
}

uint32_t scaleUp(const uint32_t value, const uint32_t permille) {
  if (value == 0) return 0;
  const uint64_t v = (static_cast<uint64_t>(value) * permille + 999u) / 1000u;
  return v == 0 ? 1u : static_cast<uint32_t>(v);
}

int fightLevelBonus(const GameState& s) { return openIn(s, Channel::LargeIntestine); }

uint32_t pillBonus(const GameState& s, const uint32_t bonus) {
  return bonus * (10u + openIn(s, Channel::Stomach)) / 10u;
}

uint8_t brewQualityBonus(const GameState& s) { return static_cast<uint8_t>(5u * openIn(s, Channel::Spleen)); }

uint8_t heartDemonBonus(const GameState& s) { return static_cast<uint8_t>(5u * openIn(s, Channel::Heart)); }

uint16_t lightningPermille(const GameState& s) {
  return static_cast<uint16_t>(1000u - 100u * openIn(s, Channel::SmallIntestine));
}

uint16_t buyPermille(const GameState& s) {
  return static_cast<uint16_t>(1000u - 50u * openIn(s, Channel::Pericardium));
}

uint32_t regrowUnits(const GameState& s, const uint32_t base) {
  const uint32_t permille = 1000u - 150u * openIn(s, Channel::TripleBurner);
  const uint32_t units = (base * permille + 500u) / 1000u;
  return units == 0 ? 1u : units;
}

uint16_t fightDamagePermille(const GameState& s) {
  return static_cast<uint16_t>(1000u - 50u * openIn(s, Channel::Bladder));
}

uint8_t healHalvesPerMinute(const GameState& s) { return static_cast<uint8_t>(2u + openIn(s, Channel::Kidney)); }

uint8_t extraHerbs(const GameState& s) { return openIn(s, Channel::Liver); }

uint8_t slipBonus(const GameState& s) { return static_cast<uint8_t>(10u * openIn(s, Channel::Gallbladder)); }

const char* pointName(const uint8_t point) { return kPointNames[point < kPoints ? point : 0]; }
const char* pointPlace(const uint8_t point) { return kPointPlaces[point < kPoints ? point : 0]; }
const char* channelName(const Channel c) {
  const uint8_t i = static_cast<uint8_t>(c);
  return kChannelNames[i < kChannels ? i : 0];
}
const char* channelEffect(const Channel c) {
  if (c == Channel::Ren || c == Channel::Du) return nullptr;
  const uint8_t i = limbIndex(c);
  return i < kChannels - 2 ? kChannelEffects[i] : nullptr;
}
Element channelElement(const Channel c) {
  if (c == Channel::Ren || c == Channel::Du) return Element::Count;
  const uint8_t i = limbIndex(c);
  return i < kChannels - 2 ? kChannelElements[i] : Element::Count;
}

uint16_t damageHpBase(const GameState& s) {
  const uint16_t lung = static_cast<uint16_t>(kLungHp * openIn(s, Channel::Lung));
  return s.progress.hpMax > lung ? static_cast<uint16_t>(s.progress.hpMax - lung) : s.progress.hpMax;
}

}  // namespace meridians
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
