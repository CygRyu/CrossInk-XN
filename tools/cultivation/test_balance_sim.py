#!/usr/bin/env python3
"""Tests for balance_sim.py.  Run:  python3 tools/cultivation/test_balance_sim.py"""

import os
import statistics
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import balance_sim as b  # noqa: E402


class FormulaTests(unittest.TestCase):
    def test_target_hours_total_772(self):
        self.assertAlmostEqual(sum(b.TARGET_HOURS), 772.0)

    def test_doc_age_checkpoints(self):
        start = b.TUNING["start_age"]
        self.assertAlmostEqual(start + sum(b.TARGET_HOURS[:2]), 31)   # end of Qi Condensation
        self.assertAlmostEqual(start + sum(b.TARGET_HOURS[:4]), 113)  # end of Core Formation

    def test_stage_costs_sum_to_realm_qi(self):
        for r, (_, n, _, _) in enumerate(b.REALMS):
            total = sum(b.stage_cost(r, s) for s in range(n))
            self.assertAlmostEqual(total, b.realm_qi(r), places=6)

    def test_stage_cost_rises_12_percent_per_stage(self):
        self.assertAlmostEqual(b.stage_cost(1, 5) / b.stage_cost(1, 0), 1.6)

    def test_realm_qi_formula(self):
        self.assertAlmostEqual(b.realm_qi(0), 600 * 4.5)
        self.assertAlmostEqual(b.realm_qi(3), 600 * 52 * 1.6 ** 3)

    def test_weekly_allowance(self):
        # Decision 6A: 100% for the first 14 h of a calendar week, 50% for the next 14 h, 25% after.
        self.assertEqual(b.soft_cap_factor(0), (1.0, 840))
        self.assertEqual(b.soft_cap_factor(839)[0], 1.0)
        self.assertEqual(b.soft_cap_factor(840), (0.5, 840))
        self.assertEqual(b.soft_cap_factor(1680)[0], 0.25)

    def test_weekly_allowance_resets_on_monday(self):
        binge = b.Profile("binge", b.weekends(600))
        c = b.Character(b.random.Random(0), "modeled", binge).run(max_days=8)
        self.assertEqual(c.qual_minutes_week, 0.0)  # day 7 is the next Monday

    def test_stones_rule(self):
        # Decision 5B: 1 per minute + 1 per 2 pages, page part capped at half the minutes.
        self.assertEqual(b.stones_for_visit(30, 21), 30 + 10)
        self.assertEqual(b.stones_for_visit(30, 42), 30 + 15)   # capped
        self.assertEqual(b.stones_for_visit(30, 300), 45)       # auto page-turn farming gains nothing extra
        self.assertEqual(b.stones_for_visit(0.9, 10), 0)

    def test_dao_heart_from_hours(self):
        c = b.Character(b.random.Random(0), "modeled", b.PROFILES["30/day"])
        self.assertEqual(c.dao_heart, 0)
        c.hours = 7.5
        self.assertAlmostEqual(c.dao_heart, 50)
        c.hours = 40
        self.assertEqual(c.dao_heart, 100)

    def test_stamina_per_minutes_not_sessions(self):
        short = b.Character(b.random.Random(0), "modeled", b.Profile("3x10", b.daily(30, pieces=3))).run(max_days=14)
        long = b.Character(b.random.Random(0), "modeled", b.Profile("1x30", b.daily(30))).run(max_days=14)
        self.assertEqual(short.stamina_earned, long.stamina_earned)
        self.assertEqual(long.stamina_earned, 14 * 30 // 15)

    def test_night_reading_gives_no_extra_qi(self):
        night = b.Character(b.random.Random(0), "modeled", b.PROFILES["night30"])
        evening = b.Character(b.random.Random(0), "modeled", b.EXTRA_PROFILES["evening30"])
        night.hours = evening.hours = 60
        night.bucket_h[3] = evening.bucket_h[2] = 60
        night.omen_bucket = evening.omen_bucket = 0
        self.assertAlmostEqual(night.multiplier(3), evening.multiplier(2))

    def test_scroll_tiers(self):
        self.assertEqual([b.scroll_tier(h) for h in (0, 2.9, 3, 7.9, 8, 15, 24.9, 25, 90)],
                         [0, 0, 1, 1, 2, 3, 3, 4, 4])


class SimulationTests(unittest.TestCase):
    def test_nominal_reproduces_targets_exactly(self):
        c = b.Character(b.random.Random(0), "nominal", b.PROFILES["30/day"]).run()
        self.assertTrue(c.done)
        for r, rec in enumerate(c.records):
            self.assertAlmostEqual(rec.end_hours - rec.entered_hours, b.TARGET_HOURS[r], places=6)
        self.assertAlmostEqual(c.records[-1].end_day, 772 * 2, delta=1)   # 4.2 years

    def test_modeled_hits_mortal_plane_target(self):
        s = b.summarize(b.simulate(b.PROFILES["30/day"], "modeled", 120, 7))
        self.assertEqual(s["done"], 120)
        self.assertLess(abs(statistics.median(s["total_hours"]) / 772 - 1), 0.10)

    def test_each_realm_within_25_percent(self):
        s = b.summarize(b.simulate(b.PROFILES["30/day"], "modeled", 120, 7))
        for r in range(len(b.REALMS)):
            ratio = statistics.median(s["hours"][r]) / b.TARGET_HOURS[r]
            self.assertLess(abs(ratio - 1), 0.25, f"realm {r}: {ratio:.2f}")

    def test_fairness_spread_within_5_percent(self):
        # Same weekly minutes, different habits: Mortal Plane hours within 5% of evening-only.
        ref = statistics.median(b.summarize(b.simulate(b.EXTRA_PROFILES["evening30"], "modeled", 60, 7))["total_hours"])
        for prof in (b.PROFILES["night30"], b.PROFILES["weekend"], b.EXTRA_PROFILES["weekend-binge"]):
            h = statistics.median(b.summarize(b.simulate(prof, "modeled", 60, 7))["total_hours"])
            self.assertLess(abs(h / ref - 1), 0.05, prof.name)

    def test_deterministic(self):
        a = b.summarize(b.simulate(b.PROFILES["60/day"], "modeled", 10, 3))["total_hours"]
        c = b.summarize(b.simulate(b.PROFILES["60/day"], "modeled", 10, 3))["total_hours"]
        self.assertEqual(a, c)

    def test_no_reading_no_progress(self):
        idle = b.Profile("idle", lambda day: [])
        c = b.Character(b.random.Random(0), "modeled", idle).run(max_days=400)
        self.assertEqual((c.realm, c.stage, c.qi, c.hours), (0, 0, 0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
