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

    def test_soft_cap(self):
        self.assertEqual(b.soft_cap_factor(0), (1.0, 120))
        self.assertEqual(b.soft_cap_factor(119)[0], 1.0)
        self.assertEqual(b.soft_cap_factor(120), (0.5, 120))
        self.assertEqual(b.soft_cap_factor(240)[0], 0.25)

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
