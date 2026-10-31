#!/usr/bin/env python3
"""Path of Immortal Reading - desktop balance simulator.

Runs simulated characters through the Mortal Plane (realms 0-9) under
different reading profiles and reports days per realm, reading hours, ages and
a few fairness checks.

Two modes:
  nominal   The player's multiplier is exactly the design curve M_hat_r = 1.6^r
            and every breakthrough succeeds. This must reproduce the design
            document's hour targets exactly (772 h for the Mortal Plane); it
            checks the cost formulas themselves.
  modeled   (default) Multipliers come from the systems in the design document
            (talent, root purity, manuals from finished books, grades,
            meridians, Daos, omens), breakthroughs are rolled with pity, and
            failures cost 25% of the Peak bar. This is the real balance check.

Every tuning number lives in TUNING below. Values marked DOC come from the
design document; values marked PROPOSED are not specified by the document and
were chosen here, so they are the ones to argue about.

Pure Python 3 standard library. Deterministic for a given --seed.
"""

from __future__ import annotations

import argparse
import math
import random
import statistics
import sys
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Design data
# ---------------------------------------------------------------------------

# (name, minor stages excl. Great Perfection, lifespan years, has Great Perfection)  # DOC: realm table
REALMS = [
    ("Body Tempering", 9, 80, False),
    ("Qi Condensation", 9, 120, True),
    ("Foundation Establishment", 4, 200, True),
    ("Core Formation", 4, 320, True),
    ("Nascent Soul", 4, 500, True),
    ("Soul Transformation", 4, 800, True),
    ("Void Refinement", 4, 1200, True),
    ("Body Integration", 4, 2000, True),
    ("Mahayana", 4, 3000, True),
    ("Tribulation Transcendence", 4, 5000, True),
]

# Target reading hours per realm, H_r.  The document's chart did not survive
# export; these values are reconstructed from every checkpoint the text gives:
#   - sum = 772 h                                  ("about 772 reading hours")
#   - realms 0-1 end at 15 h  -> age 31            (start age 16, 1 year/hour)
#   - realms 0-3 end at 97 h  -> age 113
#   - realms 4-9 at 75-150 h = 5-10 months at 30 min/day
TARGET_HOURS = [4.5, 10.5, 30.0, 52.0, 75.0, 90.0, 105.0, 120.0, 135.0, 150.0]

# Base breakthrough chance INTO realm r (index r).  DOC: 90/70/45/35, then -5 per realm, floor 20.
BASE_CHANCE = [None, 90, 70, 45, 35, 30, 25, 20, 20, 20]

# Scroll tiers by hours read in that book. DOC: <3 Mortal, 3-8 Yellow, 8-15 Earth, 15-25 Heaven, 25+ Saint
TIER_NAMES = ["Mortal", "Yellow", "Earth", "Heaven", "Saint"]
TIER_HOURS = [0, 3, 8, 15, 25]

TECHNIQUE_KINDS = ["Sword", "Palm/Fist", "Body", "Movement", "Mind", "Cultivation"]  # DOC

BUCKETS = ["Morning", "Afternoon", "Evening", "Night"]

TUNING = {
    # --- DOC -------------------------------------------------------------
    "qi_per_minute": 10,
    "mhat_base": 1.6,                  # M_hat_r = 1.6^r
    "stage_cost_step": 0.12,           # +12% per minor stage
    # Weekly allowance (decision 6A, replaces the doc's daily soft cap): 100% for
    # the first 14 h read in a calendar week (Mon-Sun), 50% for the next 14 h,
    # 25% after. Minutes into the week -> factor.
    "soft_cap": [(14 * 60, 1.0), (28 * 60, 0.5), (None, 0.25)],
    "pity_per_failure": 15,
    "pity_cap": 90,
    "failure_bar_loss": 0.25,
    "start_age": 16,
    "rerolls": 3,
    "stamina_cap": 12,
    "stamina_minutes": 15,             # decision 4A: +1 stamina per 15 min read, nothing per session
    "stones_per_minute": 1,            # decision 5B: 1 stone per minute read ...
    "pages_per_stone": 2,              # ... + 1 per 2 pages, page part <= half the minutes of the visit
    "consolidation_max": 40,
    "dao_heart_breakthrough_max": 15,
    "dao_heart_grade_max": 25,
    "consolidation_breakthrough_max": 15,
    "element_match_breakthrough": 5,
    "element_match_grade": 15,
    "pill_grade_max": 20,
    "omen_bucket_bonus": 0.20,         # "Water Qi surges at night: +20%"
    # --- PROPOSED (not specified in the design document) -----------------
    # Talent table: the doc names only Choked Meridians x0.6 (common) and
    # Heaven-Blessed Root x1.5 (rare). Middle tiers and weights are proposed.
    "talents": [("Choked Meridians", 0.6, 35), ("Dull Root", 0.8, 30), ("Ordinary Root", 1.0, 20),
                ("Keen Root", 1.2, 11), ("Heaven-Blessed Root", 1.5, 4)],
    "purity_mult_range": (0.85, 1.15),  # purity 1..100 maps linearly onto this
    "manual_tier_mult": [1.15, 1.30, 1.50, 1.75, 2.00],  # Mortal..Saint cultivation manual
    "manual_quality_range": (0.9, 1.1),  # scaled by comprehension (share of book read in tracked time)
    "study_bonus_max": 0.10,            # current book progress 0..100% -> +0..10% Qi
    "book_hours_median": 9.0,          # lognormal book length in reading hours
    "book_hours_sigma": 0.5,
    "comprehension_range": (0.75, 1.0),
    # Meridians carry the realm-to-realm growth of M (see report). Points
    # opened at entry to each realm (realm 0..9) and the Qi bonus per point.
    "meridian_points_per_realm": [0, 3, 3, 3, 4, 4, 5, 4, 5, 5],  # 36 acupoints (DOC count),
    "meridian_qi_per_point": 0.10,
    "meridian_cost_frac": 0.02,         # each point costs 2% of that realm's Q_r
    "consolidation_per_hour": 2.0,      # Great Perfection: points per reading hour
    # v0.2.0 alchemy, as built: two grade pills of medium quality (+8 each) before the Foundation and
    # Core grades; one breakthrough pill per attempt, of the best grade the open regions allow, medium
    # quality on average (+17; low/medium/high = +10/+17/+25). A pill is a key when its grade fits
    # (common up to Nascent Soul, spirit up to Body Integration, rare any) and is used up on every
    # attempt; brewing one takes 40-60 reading minutes plus gathering, modelled as 3 reading hours.
    "pill_grade_points": 16,
    "breakthrough_pill_bonus": 17,
    "pill_key_hours": 3.0,             # reading hours in-realm to gather for and brew a breakthrough pill
    "pill_keys": True,                 # breakthrough pills exist (alchemy, v0.2.0)
    # Milestone 7 (user, session 3): a bottleneck needs a Technique Scroll of at least this tier, from any
    # realm, spent on success. Index = the realm being entered; None = no key (the small bottleneck).
    "key_tier_by_target": [None, None, 0, 1, 2, 3, 3, 3, 3, 3],
    "manual_pity": 4,                  # if none of the last 4 minted scrolls was a manual, the next one is
    "min_book_hours": 0.5,             # books read under 30 minutes give no scroll
    # v0.1.9 trials before the roll (user-approved mechanics). Strike damage in % of max HP by
    # strike count; the model endures every strike and burns a spare scroll (never the last
    # qualifying key) only when the next strike would kill. HP is full at every attempt: it heals
    # 1 per minute read, and refilling the bar after any failure takes far longer than 100 minutes.
    # Turning strikes with equipped techniques and Body cuts are left out (a cautious model).
    "trials": True,
    "tribulation_strikes": {3: [25, 30, 35], 5: [15, 18, 21, 24, 27], 7: [12, 15, 18, 21, 24, 27, 30],
                            9: [10, 13, 16, 19, 22, 25, 28, 31, 34]},
    "heart_demon_realms": (4, 6, 8),
    "heart_demon_base": 40,            # + Dao Heart x 30 / 100 + Golden Core grade 0..20 (+10 Mind technique: left out)
    # v0.1.10 exploration (user-agreed): Spirit Stones buy trial items from wandering merchants. Exploration
    # gives no Qi; its own stone and item finds are left out (a cautious model: reading income only). Before
    # each trial the model passes a few merchants; each has an item in stock at the agreed rate.
    "shop": True,
    "pages_per_minute": 1.0,           # for the stone income (page part of decision 5B)
    "item_price": {"thunder": 150, "incense": 120, "tonic_greater": 80},
    "item_stock": {"thunder": 0.30, "incense": 0.30, "tonic_greater": 0.25},
    "merchant_chances": 3,             # merchants passed (fresh stock) before each trial
    "thunder_held_max": 2,
    "incense_held_max": 2,
    "tonic_held_max": 1,
    "incense_bonus": 25,
    "tonic_greater_heal": 50,          # % of max HP
    "dao_heart_full_hours": 15,        # decision 2B: Dao Heart 100 after 15 h read, never decays
    "flowing_water_unlock_night_h": 5,  # DOC
    "dao_time_of_day_unlock_any_h": 40,  # DOC
    "dao_level_hours": 10,             # hours in bucket per extra Dao level
    "myriad_paths_unlock_h": 150,      # decision 5B: Dao of the Myriad Paths unlocks at 150 h read (was 10,000 pages)
}


def mhat(r: int) -> float:
    return TUNING["mhat_base"] ** r


def realm_qi(r: int) -> float:
    """Q_r = 600 * H_r * M_hat_r  (DOC)."""
    return 600.0 * TARGET_HOURS[r] * mhat(r)


def stage_cost(r: int, s: int) -> float:
    """cost(r, s) = Q_r * (1 + 0.12 s) / sum_k (1 + 0.12 k)  (DOC)."""
    n = REALMS[r][1]
    step = TUNING["stage_cost_step"]
    denom = sum(1 + step * k for k in range(n))
    return realm_qi(r) * (1 + step * s) / denom


def soft_cap_factor(minutes_already_this_week: float) -> tuple[float, float]:
    """Return (Qi factor, minutes until the factor changes) for the next reading minute.

    The allowance is weekly (decision 6A): minutes count from Monday."""
    lower = 0.0
    for limit, factor in TUNING["soft_cap"]:
        if limit is None or minutes_already_this_week < limit:
            return factor, (math.inf if limit is None else limit - minutes_already_this_week)
        lower = limit
    raise AssertionError(lower)


def scroll_tier(hours: float) -> int:
    tier = 0
    for i, h in enumerate(TIER_HOURS):
        if hours >= h:
            tier = i
    return tier


# ---------------------------------------------------------------------------
# Reading profiles
# ---------------------------------------------------------------------------

@dataclass
class Profile:
    name: str
    # sessions(day_index) -> list of (bucket index, minutes)
    sessions: callable
    note: str = ""


def daily(minutes: float, bucket: int = 2, pieces: int = 1):
    def f(day: int):
        return [(bucket, minutes / pieces)] * pieces
    return f


def weekends(minutes_per_weekend_day: float, bucket: int = 1):
    def f(day: int):
        return [(bucket, minutes_per_weekend_day)] if day % 7 in (5, 6) else []
    return f


def stones_for_visit(minutes: float, pages: float) -> int:
    """Spirit Stones for one visit (decision 5B): 1 per whole minute read plus 1 per
    2 pages, where the page part can't exceed half the minutes of the same visit."""
    m = int(minutes)
    page_part = min(int(pages) // TUNING["pages_per_stone"], m // 2)
    return TUNING["stones_per_minute"] * m + page_part


def mixed_buckets(minutes: float):
    def f(day: int):
        return [(day % 4, minutes)]
    return f


PROFILES = {
    "15/day": Profile("15 min/day", daily(15)),
    "30/day": Profile("30 min/day", daily(30)),
    "60/day": Profile("60 min/day", daily(60)),
    "90/day": Profile("90 min/day", daily(90)),
    "night30": Profile("Night only, 30 min/day", daily(30, bucket=3)),
    "weekend": Profile("Weekends only, 105 min Sat+Sun", weekends(105),
                       "same 210 min/week as 30 min/day"),
}

EXTRA_PROFILES = {
    "evening30": Profile("Evening only, 30 min/day", daily(30, bucket=2)),
    "mixed30": Profile("Rotating buckets, 30 min/day", mixed_buckets(30)),
    "3x10": Profile("3 x 10 min/day", daily(30, pieces=3)),
    "weekend-binge": Profile("Weekends only, 210 min Sat+Sun", weekends(210),
                             "same 420 min/week as 60 min/day"),
}


# ---------------------------------------------------------------------------
# Character simulation
# ---------------------------------------------------------------------------

@dataclass
class Book:
    hours: float
    kind: int
    comprehension: float
    read_h: float = 0.0
    bucket_h: list = field(default_factory=lambda: [0.0] * 4)


@dataclass
class RealmRecord:
    entered_day: float
    entered_hours: float
    multiplier_at_entry: float
    attempts: int = 0
    end_day: float = 0.0
    end_hours: float = 0.0


class Character:
    def __init__(self, rng: random.Random, mode: str, profile: Profile, gp_linger_hours: float = 0.0):
        self.rng = rng
        self.mode = mode
        self.profile = profile
        self.gp_linger_hours = gp_linger_hours
        t = TUNING

        # Destiny: one roll per reroll, keep the best seen (doc: any roll can be kept).
        rolls = [self._roll_destiny() for _ in range(1 + t["rerolls"])]
        self.talent_name, self.talent, self.purity = max(rolls, key=lambda x: x[1] * self._purity_mult(x[2]))

        self.realm = 0
        self.stage = 0
        self.qi = 0.0
        self.extra_cost = 0.0          # meridian costs owed in this realm
        self.meridian_points = 0
        self.foundation_mult = 1.0
        self.core_mult = 1.0
        self.consolidation = 0.0
        self.failures = 0
        self.manual_mult = 1.0          # best equipped Cultivation manual
        self.manual_tier = None         # its tier, so it can become a spare scroll when replaced
        self.spare_scrolls = []         # tiers of unequipped scrolls (keys); techniques are kept unequipped
        self.since_manual = 0
        self.waiting_key = False        # full Peak bar, no key (only without pill keys)
        self.scrolls_burned = 0         # v0.1.9: spare scrolls burned in tribulations
        self.trials_lost = 0
        self.stones = 0                 # v0.1.10: Spirit Stones (reading income only, see TUNING["shop"])
        self.stones_spent = 0
        self.thunder = 0                # Thunder-Warding Talismans held
        self.incense = 0                # Calm-Heart Incense held
        self.tonics = 0                 # Greater tonics held
        self.items_used = 0
        self.hours = 0.0               # all reading hours (drives age)
        self.qual_minutes_week = 0.0
        self.bucket_h = [0.0] * 4
        self.streak = 0                # kept as flavor and for the Dao of Time unlock only
        self.longest = 0
        self.read_today = False
        self.omen_bucket = 0
        self.stamina_earned = 0
        self.stamina_lost_to_cap = 0
        self.stamina_bank = 0
        self.stamina_minutes = 0.0
        self.stamina_sessions = 0
        self.books_finished = 0
        self.book = self._new_book()
        self.records: list[RealmRecord] = []
        self.day = 0
        self.done = False
        self._enter_realm(0, day=0.0)

    # -- rolls ------------------------------------------------------------
    def _roll_destiny(self):
        names = TUNING["talents"]
        pick = self.rng.choices(names, weights=[w for _, _, w in names])[0]
        return pick[0], pick[1], self.rng.randint(1, 100)

    @staticmethod
    def _purity_mult(purity: int) -> float:
        lo, hi = TUNING["purity_mult_range"]
        return lo + (hi - lo) * (purity - 1) / 99

    def _new_book(self) -> Book:
        t = TUNING
        hours = self.rng.lognormvariate(math.log(t["book_hours_median"]), t["book_hours_sigma"])
        return Book(hours=hours, kind=self.rng.randrange(len(TECHNIQUE_KINDS)),
                    comprehension=self.rng.uniform(*t["comprehension_range"]))

    @property
    def dao_heart(self) -> float:
        """Decision 2B: grows with reading hours, full (100) after 15 h, never decays."""
        return min(100.0, 100.0 * self.hours / TUNING["dao_heart_full_hours"])

    # -- multipliers ------------------------------------------------------
    def dao_level(self, bucket: int) -> int:
        t = TUNING
        h = self.bucket_h[bucket]
        if h < t["flowing_water_unlock_night_h"] and self.hours < t["dao_time_of_day_unlock_any_h"]:
            return 0
        return min(9, 1 + int(max(0.0, h - t["flowing_water_unlock_night_h"]) // t["dao_level_hours"]))

    def multiplier(self, bucket: int) -> float:
        if self.mode == "nominal":
            return mhat(self.realm)
        t = TUNING
        m = self.talent * self._purity_mult(self.purity)
        m *= self.manual_mult
        m *= 1 + t["study_bonus_max"] * min(1.0, self.book.read_h / self.book.hours)
        m *= self.foundation_mult * self.core_mult
        m *= (1 + t["meridian_qi_per_point"]) ** self.meridian_points
        # Decision 1A: no time-of-day Dao gives Qi (Flowing Water's night perk is event luck).
        if bucket == self.omen_bucket:
            m *= 1 + t["omen_bucket_bonus"]
        return m

    def base_multiplier(self) -> float:
        """Multiplier without bucket-specific parts, for reporting."""
        if self.mode == "nominal":
            return mhat(self.realm)
        t = TUNING
        return (self.talent * self._purity_mult(self.purity) * self.manual_mult * self.foundation_mult
                * self.core_mult * (1 + t["meridian_qi_per_point"]) ** self.meridian_points)

    # -- realm transitions --------------------------------------------------
    def _enter_realm(self, r: int, day: float):
        t = TUNING
        self.realm, self.stage, self.qi = r, 0, 0.0
        self.extra_cost = 0.0
        if self.mode == "modeled":
            pts = t["meridian_points_per_realm"][r]
            self.meridian_points += pts
            self.extra_cost = pts * t["meridian_cost_frac"] * realm_qi(r)
        self.records.append(RealmRecord(entered_day=day, entered_hours=self.hours,
                                        multiplier_at_entry=self.base_multiplier()))

    def grade_score(self) -> float:
        t = TUNING
        s = min(t["consolidation_max"], self.consolidation)
        s += t["dao_heart_grade_max"] * self.dao_heart / 100
        s += min(t["pill_grade_max"], t["pill_grade_points"])
        if self.rng.random() < 0.5:   # main technique element matches the root
            s += t["element_match_grade"]
        return s

    def _set_grade(self, target_realm: int):
        score = self.grade_score()
        if target_realm == 2:   # Foundation grade (DOC)
            if score >= 90:
                self.foundation_mult = 1.5
            elif score >= 70:
                self.foundation_mult = 1.25
            elif score >= 40:
                self.foundation_mult = 1.0
            elif score >= 20:
                self.foundation_mult = 0.95
            else:
                self.foundation_mult = 0.8
        elif target_realm == 3:  # Golden Core grade (DOC)
            if score >= 95:
                self.core_mult = 1.40
            else:
                g = min(8, int(score // 10))  # 0 = 9th grade ... 8 = 1st grade
                self.core_mult = 1 + 0.30 * g / 8

    def breakthrough_chance(self, target: int) -> float:
        t = TUNING
        c = BASE_CHANCE[target]
        c += t["dao_heart_breakthrough_max"] * self.dao_heart / 100
        c += t["consolidation_breakthrough_max"] * min(t["consolidation_max"], self.consolidation) / t["consolidation_max"]
        if target >= 2:
            c += t["breakthrough_pill_bonus"]
        if self.rng.random() < 0.5:
            c += t["element_match_breakthrough"]
        c = min(99.0, c)
        if self.failures:
            c = max(c, min(t["pity_cap"], c + t["pity_per_failure"] * self.failures))
        return c

    def scroll_key(self, target: int):
        """Index of the spare scroll the firmware would spend (lowest qualifying tier), or None."""
        need = TUNING["key_tier_by_target"][target]
        if need is None:
            return None
        fits = [i for i, t in enumerate(self.spare_scrolls) if t >= need]
        return min(fits, key=lambda i: self.spare_scrolls[i]) if fits else None

    def shop(self, strikes, heart_demon: bool):
        """v0.1.10: before a trial, buy what the coming trial needs from the merchants passed."""
        t = TUNING
        if not t["shop"]:
            return
        price = t["item_price"]
        for _ in range(t["merchant_chances"]):
            wants = []
            if strikes:
                wants += [("thunder", "thunder_held_max"), ("tonic_greater", "tonic_held_max")]
            if heart_demon:
                wants += [("incense", "incense_held_max")]
            for item, cap in wants:
                held = {"thunder": self.thunder, "incense": self.incense, "tonic_greater": self.tonics}[item]
                in_stock = self.rng.random() < t["item_stock"][item]
                if not in_stock or held >= t[cap] or self.stones < price[item]:
                    continue
                self.stones -= price[item]
                self.stones_spent += price[item]
                if item == "thunder":
                    self.thunder += 1
                elif item == "incense":
                    self.incense += 1
                else:
                    self.tonics += 1

    def pass_trial(self, target: int) -> bool:
        """The Tribulation or Heart Demon before the roll (firmware Trials.cpp), with the cautious model."""
        t = TUNING
        if not t["trials"]:
            return True
        strikes = t["tribulation_strikes"].get(target)
        self.shop(strikes, target in t["heart_demon_realms"])
        if strikes:
            hp = 100
            for pct in strikes:
                if pct < hp:
                    hp -= pct
                    continue
                if self.thunder:   # v0.1.10: a Thunder-Warding Talisman takes the whole strike
                    self.thunder -= 1
                    self.items_used += 1
                    continue
                if self.tonics and hp + t["tonic_greater_heal"] > pct:   # a Greater tonic between strikes
                    self.tonics -= 1
                    self.items_used += 1
                    hp = min(100, hp + t["tonic_greater_heal"]) - pct
                    continue
                key = self.scroll_key(target)
                spare = [i for i in range(len(self.spare_scrolls)) if i != key]
                if not spare:
                    return False
                weakest = min(spare, key=lambda i: self.spare_scrolls[i])
                self.spare_scrolls.pop(weakest)
                self.scrolls_burned += 1
            return True
        if target in t["heart_demon_realms"]:
            core_bonus = min(20.0, (self.core_mult - 1.0) / 0.30 * 20.0)
            chance = min(95.0, t["heart_demon_base"] + 30 * self.dao_heart / 100 + core_bonus)
            won = lost = 0
            while won < 2 and lost < 2:
                scene = chance
                if self.incense:   # v0.1.10: Calm-Heart Incense, +25 to one scene (at most 99)
                    self.incense -= 1
                    self.items_used += 1
                    scene = min(99.0, chance + t["incense_bonus"])
                if self.rng.random() * 100 < scene:
                    won += 1
                else:
                    lost += 1
            return won == 2
        return True

    def has_key(self, target: int, hours_in_realm: float) -> bool:
        if TUNING["key_tier_by_target"][target] is None:   # small bottleneck into Qi Condensation needs no key
            return True
        if self.scroll_key(target) is not None:
            return True
        return TUNING["pill_keys"] and hours_in_realm >= TUNING["pill_key_hours"]

    # -- the reading loop ----------------------------------------------------
    def new_day(self, day: int):
        self.day = day
        # The streak is flavor now (and the Dao of Time unlock); Dao Heart comes from hours (2B).
        if day > 0:
            self.streak = self.streak + 1 if self.read_today else 0
            self.longest = max(self.longest, self.streak)
        self.read_today = False
        if day % 7 == 0:   # day 0 is a Monday: calendar weeks, Mon-Sun (decision 6A)
            self.qual_minutes_week = 0.0
        self.omen_bucket = self.rng.randrange(4)

    def read_session(self, bucket: int, minutes: float, day: int):
        t = TUNING
        self.read_today = True
        self.stamina_sessions += 1
        # Stamina (decision 4A): +1 per 15 min read, accumulated across sessions; nothing per session. Cap 12.
        step_min = t["stamina_minutes"]
        before = int(self.stamina_minutes // step_min)
        self.stamina_minutes += minutes
        gained = int(self.stamina_minutes // step_min) - before
        self.stamina_earned += gained
        room = t["stamina_cap"] - self.stamina_bank
        self.stamina_lost_to_cap += max(0, gained - room)
        self.stamina_bank = min(t["stamina_cap"], self.stamina_bank + gained)
        self.stones += stones_for_visit(minutes, minutes * t["pages_per_minute"])

        left = minutes
        while left > 1e-9 and not self.done:
            cap_factor, until_cap_change = soft_cap_factor(self.qual_minutes_week)
            rate = t["qi_per_minute"] * self.multiplier(bucket) * cap_factor   # Qi per minute
            if self.waiting_key:   # a full bar waiting for a scroll: reading only finishes books
                rate = 0.0
            book_left_min = (self.book.hours - self.book.read_h) * 60
            need_qi = self.current_need() - self.qi
            to_event = need_qi / rate if rate > 0 else math.inf
            step = min(left, until_cap_change, book_left_min, max(0.0, to_event))
            self._advance(bucket, step, rate)
            left -= step
            if self.book.read_h >= self.book.hours - 1e-9:
                self._finish_book()
                if self.waiting_key and self.has_key(self.realm + 1, 0.0):
                    self.waiting_key = False
            if self.qi >= self.current_need() - 1e-6 and not self.waiting_key:
                self._complete_stage(day + (minutes - left) / (24 * 60))

    def _advance(self, bucket: int, minutes: float, rate: float):
        self.qi += rate * minutes
        self.qual_minutes_week += minutes
        h = minutes / 60
        self.hours += h
        self.bucket_h[bucket] += h
        self.book.read_h += h
        self.book.bucket_h[bucket] += h

    def current_need(self) -> float:
        need = stage_cost(self.realm, self.stage)
        if self.stage == 0:
            need += self.extra_cost
        return need

    def _finish_book(self):
        t = TUNING
        b = self.book
        self.books_finished += 1
        if b.hours >= t["min_book_hours"]:
            tier = scroll_tier(b.hours)
            kind = TECHNIQUE_KINDS.index("Cultivation") if self.since_manual >= t["manual_pity"] else b.kind
            self.since_manual = 0 if TECHNIQUE_KINDS[kind] == "Cultivation" else self.since_manual + 1
            if TECHNIQUE_KINDS[kind] == "Cultivation":
                lo, hi = t["manual_quality_range"]
                q = lo + (hi - lo) * (b.comprehension - t["comprehension_range"][0]) / (
                    t["comprehension_range"][1] - t["comprehension_range"][0])
                mult = t["manual_tier_mult"][tier] * q
                if mult > self.manual_mult:   # equip the better manual; the old one becomes a spare scroll
                    if self.manual_tier is not None:
                        self.spare_scrolls.append(self.manual_tier)
                    self.manual_mult, self.manual_tier = mult, tier
                else:
                    self.spare_scrolls.append(tier)
            else:
                self.spare_scrolls.append(tier)
        self.book = self._new_book()

    def _complete_stage(self, when_day: float):
        r = self.realm
        n = REALMS[r][1]
        need = self.current_need()
        if self.stage < n - 1:            # minor stage: automatic, excess carries over
            self.qi -= need
            self.extra_cost = 0.0 if self.stage == 0 else self.extra_cost
            self.stage += 1
            return
        # Full Peak bar.
        self.qi = need
        rec = self.records[-1]
        if r == len(REALMS) - 1:          # Mortal Plane complete (ready for the Great Tribulation)
            rec.end_day, rec.end_hours = when_day, self.hours
            self.done = True
            return
        hours_in_realm = self.hours - rec.entered_hours
        if self.mode == "modeled":
            if REALMS[r][3] and self.gp_linger_hours > 0:
                # Model the linger as extra reading time spent at Peak, earning consolidation.
                extra = self.gp_linger_hours
                self.hours += extra
                self.consolidation = min(TUNING["consolidation_max"],
                                         self.consolidation + TUNING["consolidation_per_hour"] * extra)
                when_day += extra * 60 / max(1e-9, self._minutes_per_day())
                hours_in_realm += extra
            if not self.has_key(r + 1, hours_in_realm):
                if not TUNING["pill_keys"]:   # wait, reading, until a book gives a scroll of the tier
                    self.waiting_key = True
                    return
                wait = TUNING["pill_key_hours"] - hours_in_realm
                self.hours += wait
                when_day += wait * 60 / max(1e-9, self._minutes_per_day())
            rec.attempts += 1
            if not self.pass_trial(r + 1):   # v0.1.9: a lost trial is a failed attempt
                self.trials_lost += 1
                self.failures += 1
                self.qi = need * (1 - TUNING["failure_bar_loss"])
                return
            if self.rng.random() * 100 >= self.breakthrough_chance(r + 1):
                self.failures += 1
                self.qi = need * (1 - TUNING["failure_bar_loss"])
                return
            key = self.scroll_key(r + 1)
            if key is not None:   # a scroll is spent only on success; otherwise a pill was the key
                self.spare_scrolls.pop(key)
            self._set_grade(r + 1)
        else:
            rec.attempts += 1
        rec.end_day, rec.end_hours = when_day, self.hours
        self.failures = 0
        self.consolidation = 0.0
        self._enter_realm(r + 1, day=when_day)

    def _minutes_per_day(self) -> float:
        week = sum(m for d in range(7) for _, m in self.profile.sessions(d))
        return week / 7

    def run(self, max_days: int = 40 * 365):
        for day in range(max_days):
            self.new_day(day)
            for bucket, minutes in self.profile.sessions(day):
                self.read_session(bucket, minutes, day)
                if self.done:
                    return self
        return self


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def pct(values, q):
    v = sorted(values)
    if not v:
        return float("nan")
    k = (len(v) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def simulate(profile: Profile, mode: str, runs: int, seed: int, gp_linger: float = 0.0):
    chars = []
    for i in range(runs):
        rng = random.Random(f"{seed}:{i}")  # common random numbers across profiles
        chars.append(Character(rng, mode, profile, gp_linger).run())
    return chars


def summarize(chars):
    n = len(REALMS)
    days = [[c.records[r].end_day - c.records[r].entered_day for c in chars if len(c.records) > r and c.records[r].end_day]
            for r in range(n)]
    hours = [[c.records[r].end_hours - c.records[r].entered_hours for c in chars if len(c.records) > r and c.records[r].end_day]
             for r in range(n)]
    mult = [[c.records[r].multiplier_at_entry for c in chars if len(c.records) > r] for r in range(n)]
    attempts = [[c.records[r].attempts for c in chars if len(c.records) > r and c.records[r].end_day] for r in range(n - 1)]
    total_days = [c.records[-1].end_day for c in chars if c.done]
    total_hours = [c.records[-1].end_hours for c in chars if c.done]
    return {"days": days, "hours": hours, "mult": mult, "attempts": attempts,
            "total_days": total_days, "total_hours": total_hours, "done": sum(c.done for c in chars),
            "chars": chars}


def fmt_days(d: float) -> str:
    if d != d:
        return "-"
    if d < 60:
        return f"{d:.0f} d"
    if d < 730:
        return f"{d / 30.44:.1f} mo"
    return f"{d / 365.25:.1f} y"


def report(mode: str, runs: int, seed: int, profiles: dict, gp_linger: float, out=sys.stdout):
    w = out.write
    w(f"Path of Immortal Reading - balance simulator  (mode={mode}, runs={runs}, seed={seed}"
      f"{', Great Perfection linger ' + str(gp_linger) + ' h' if gp_linger else ''})\n\n")

    w("Design targets (DOC formulas):\n")
    w(f"  {'#':>2} {'Realm':<26}{'H_r':>7}{'cum h':>8}{'age at end':>12}{'M_hat':>8}{'Q_r Qi':>14}\n")
    cum = 0.0
    for r, (name, _, life, _) in enumerate(REALMS):
        cum += TARGET_HOURS[r]
        w(f"  {r:>2} {name:<26}{TARGET_HOURS[r]:>7.1f}{cum:>8.1f}{TUNING['start_age'] + cum:>9.0f}/{life:<4}"
          f"{mhat(r):>7.2f}{realm_qi(r):>14,.0f}\n")
    w(f"  Mortal Plane total: {sum(TARGET_HOURS):.0f} h  -> {sum(TARGET_HOURS) / 0.5 / 365.25:.1f} y at 30 min/day, "
      f"{sum(TARGET_HOURS) / 1 / 365.25:.1f} y at 60, {sum(TARGET_HOURS) / 1.5 / 30.44:.0f} mo at 90\n\n")

    summaries = {}
    for key, prof in profiles.items():
        summaries[key] = summarize(simulate(prof, mode, runs, seed, gp_linger))

    # Days per realm (median) table.
    keys = list(profiles)
    w("Median days per realm (time from entering the realm to entering the next):\n")
    w(f"  {'#':>2} {'Realm':<26}" + "".join(f"{profiles[k].name.split(',')[0][:12]:>13}" for k in keys) + "\n")
    for r, (name, *_rest) in enumerate(REALMS):
        row = "".join(f"{fmt_days(statistics.median(summaries[k]['days'][r]) if summaries[k]['days'][r] else float('nan')):>13}"
                      for k in keys)
        w(f"  {r:>2} {name:<26}{row}\n")
    w(f"     {'Mortal Plane (median)':<26}" + "".join(
        f"{fmt_days(statistics.median(summaries[k]['total_days']) if summaries[k]['total_days'] else float('nan')):>13}"
        for k in keys) + "\n")
    w(f"     {'  p10 - p90':<26}" + "".join(
        f"{fmt_days(pct(summaries[k]['total_days'], .1)) + '-' + fmt_days(pct(summaries[k]['total_days'], .9)).replace(' ', ''):>13}"
        for k in keys) + "\n")
    w(f"     {'Reading hours (median)':<26}" + "".join(
        f"{statistics.median(summaries[k]['total_hours']) if summaries[k]['total_hours'] else float('nan'):>12.0f}h"
        for k in keys) + "\n")
    w(f"     {'Finished in 40 y':<26}" + "".join(f"{summaries[k]['done']:>9}/{runs:<3}" for k in keys) + "\n\n")
    for k in keys:
        if profiles[k].note:
            w(f"  {profiles[k].name}: {profiles[k].note}\n")
    w("\n")

    base = summaries.get("30/day") or next(iter(summaries.values()))
    w("Per realm, 30 min/day profile: reading hours vs target, multiplier vs M_hat, attempts, age vs lifespan:\n")
    w(f"  {'#':>2} {'Realm':<26}{'target h':>9}{'median h':>9}{'p10-p90 h':>14}{'M entry':>9}{'M_hat':>7}"
      f"{'attempts':>9}{'age':>6}{'life':>6}\n")
    cum_h = []
    for c in base["chars"]:
        cum_h.append([c.records[r].end_hours for r in range(len(c.records)) if c.records[r].end_day])
    for r, (name, _, life, _) in enumerate(REALMS):
        hs = base["hours"][r]
        att = statistics.mean(base["attempts"][r]) if r < len(base["attempts"]) and base["attempts"][r] else float("nan")
        ages = [TUNING["start_age"] + ch[r] for ch in cum_h if len(ch) > r]
        w(f"  {r:>2} {name:<26}{TARGET_HOURS[r]:>9.1f}{statistics.median(hs):>9.1f}"
          f"{pct(hs, .1):>7.1f}-{pct(hs, .9):<6.1f}{statistics.median(base['mult'][r]):>9.2f}{mhat(r):>7.2f}"
          f"{att:>9.2f}{statistics.median(ages):>6.0f}{life:>6}\n")
    w("\n")

    if mode == "modeled":
        fairness(runs, seed, gp_linger, w)


def fairness(runs: int, seed: int, gp_linger: float, w):
    w("Fairness checks (DOC fairness rules, with decisions 1A 2B 4A 5B 6A):\n")
    cmp = {**{k: PROFILES[k] for k in ("30/day", "60/day", "90/day", "night30", "weekend")}, **EXTRA_PROFILES}
    res = {k: summarize(simulate(p, "modeled", runs, seed, gp_linger)) for k, p in cmp.items()}

    def med_h(k):
        return statistics.median(res[k]["total_hours"]) if res[k]["total_hours"] else float("nan")

    ref = med_h("evening30")
    w("  Reading hours to finish the Mortal Plane (lower = stronger):\n")
    for k in ("evening30", "mixed30", "night30", "3x10", "30/day", "weekend", "60/day", "90/day", "weekend-binge"):
        w(f"    {cmp[k].name:<38}{med_h(k):>7.0f} h  ({(med_h(k) / ref - 1) * 100:+.1f}% vs evening)\n")

    dh = {k: statistics.median([c.dao_heart for c in res[k]["chars"]]) for k in ("30/day", "weekend")}
    w(f"  Dao Heart at the end (0-100, full after {TUNING['dao_heart_full_hours']} h read): "
      f"daily {dh['30/day']:.0f}, weekends-only {dh['weekend']:.0f}\n")

    w(f"  Stamina per week (+1 per {TUNING['stamina_minutes']} min read, nothing per session, bank cap "
      f"{TUNING['stamina_cap']}):\n")
    for k in ("30/day", "3x10", "weekend", "weekend-binge"):
        cs = res[k]["chars"]
        weeks = statistics.median([max(1, c.day) / 7 for c in cs])
        earned = statistics.median([c.stamina_earned for c in cs]) / weeks
        w(f"    {cmp[k].name:<38}{earned:>6.1f}/week earned\n")

    cs = res["30/day"]["chars"]
    saved_shop = TUNING["shop"]
    TUNING["shop"] = False
    try:
        bare = summarize(simulate(PROFILES["30/day"], "modeled", runs, seed, gp_linger))["chars"]
    finally:
        TUNING["shop"] = saved_shop
    w(f"  Trials (cautious model: endure, burn a spare scroll only to survive), 30 min/day, over the Mortal "
      f"Plane (mean):\n")
    w(f"    without merchant items (v0.1.9): {statistics.mean(c.trials_lost for c in bare):.2f} trials lost, "
      f"{statistics.mean(c.scrolls_burned for c in bare):.1f} scrolls burned, "
      f"{statistics.median(c.records[-1].end_hours for c in bare if c.done):.0f} h\n")
    w(f"    with merchant items (v0.1.10):   {statistics.mean(c.trials_lost for c in cs):.2f} trials lost, "
      f"{statistics.mean(c.scrolls_burned for c in cs):.1f} scrolls burned, "
      f"{statistics.median(c.records[-1].end_hours for c in cs if c.done):.0f} h; "
      f"{statistics.mean(c.items_used for c in cs):.1f} items used, "
      f"{statistics.mean(c.stones_spent for c in cs):,.0f} of "
      f"{statistics.mean(c.stones_spent + c.stones for c in cs):,.0f} stones spent\n")
    w("  Keys (milestone 7: a scroll of the tier, from any realm, spent on success; v0.2.0: or a breakthrough pill):\n")
    for label, pills, median in (("with pill keys, typical books", True, 9.0),
                                 ("with pill keys, short books (4 h)", True, 4.0),
                                 ("scrolls only, typical books (9 h)", False, 9.0),
                                 ("scrolls only, short books (4 h)", False, 4.0)):
        saved = (TUNING["pill_keys"], TUNING["book_hours_median"])
        TUNING["pill_keys"], TUNING["book_hours_median"] = pills, median
        try:
            r30 = summarize(simulate(PROFILES["30/day"], "modeled", runs, seed, gp_linger))
        finally:
            TUNING["pill_keys"], TUNING["book_hours_median"] = saved
        hours = statistics.median(r30["total_hours"]) if r30["total_hours"] else float("nan")
        stuck = [len(c.records) - 1 for c in r30["chars"] if not c.done]
        where = f", the rest stop in realm {statistics.median(stuck):.0f}" if stuck else ""
        w(f"    30 min/day, {label:<34}{hours:>6.0f} h, {r30['done']}/{runs} finish in 40 y{where}\n")

    ppm_slow, ppm_fast = 0.7, 1.4
    slow = stones_for_visit(30, 30 * ppm_slow)
    fast = stones_for_visit(30, 30 * ppm_fast)
    w(f"  Spirit Stones (1/min + 1 per 2 pages, page part <= half the minutes): at 30 min/day a slow reader "
      f"({ppm_slow} pages/min) earns {slow}/day, a fast one ({ppm_fast}) {fast}/day -> {fast / slow:.2f}x.\n\n")


# ---------------------------------------------------------------------------

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--mode", choices=["modeled", "nominal", "both"], default="both")
    p.add_argument("--runs", type=int, default=200, help="characters per profile (modeled mode)")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--linger", type=float, default=0.0,
                   help="reading hours to stay in Great Perfection before each breakthrough")
    p.add_argument("--extra", action="store_true", help="also show the extra comparison profiles")
    a = p.parse_args(argv)
    profiles = dict(PROFILES)
    if a.extra:
        profiles.update(EXTRA_PROFILES)
    modes = ["nominal", "modeled"] if a.mode == "both" else [a.mode]
    for m in modes:
        report(m, 1 if m == "nominal" else a.runs, a.seed, profiles, a.linger)


if __name__ == "__main__":
    main()
