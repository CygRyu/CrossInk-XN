# Path of Immortal Reading: desktop tools

Host-side tools for the cultivation game. Nothing in this folder is compiled into the firmware.

## Balance simulator

```sh
python3 tools/cultivation/balance_sim.py                 # nominal + modeled, 6 standard profiles
python3 tools/cultivation/balance_sim.py --extra         # + evening-only, rotating, 3x10 min, weekend binge
python3 tools/cultivation/balance_sim.py --linger 10     # stay 10 reading hours in each Great Perfection
python3 tools/cultivation/balance_sim.py --runs 500 --seed 2
python3 tools/cultivation/test_balance_sim.py            # unit tests
```

Python 3.9+, standard library only. Output is deterministic for a given `--seed`. `report.txt` is a saved run (`--runs 300 --extra`).

### Modes

- **nominal**: the multiplier is exactly the design curve M̂ᵣ = 1.6ʳ and every breakthrough succeeds. This checks the formulas: it reproduces the document's 772 h, 4.2 y at 30 min/day, 2.1 y at 60 and 17 months at 90.
- **modeled**: multipliers come from the game's systems (destiny roll with 3 rerolls keeping the best, root purity, Cultivation manuals from finished books, Foundation and Core grades, meridians, daily omen), with breakthrough rolls, pity, and a 25% Peak-bar loss on failure. Results are Monte Carlo medians and p10–p90 ranges over N characters. All profiles use the same random draws so they compare cleanly.

### What is DOC vs PROPOSED

`TUNING` in `balance_sim.py` marks each value. **DOC** values come from the design document. **PROPOSED** values fill gaps the document leaves open:

| Gap in the document | Proposed value |
| --- | --- |
| Per-realm hours chart (did not survive export) | 4.5, 10.5, 30, 52, 75, 90, 105, 120, 135, 150 h. These meet every checkpoint in the text: 772 h total, age 31 / 113, 5–10 months per major realm |
| Talent tiers between ×0.6 and ×1.5 | Choked ×0.6 (35%), Dull ×0.8 (30%), Ordinary ×1.0 (20%), Keen ×1.2 (11%), Heaven-Blessed ×1.5 (4%) |
| Size of the manual, purity and study bonuses | manual ×1.15 / 1.30 / 1.50 / 1.75 / 2.00 by tier; purity ×0.85–1.15; study up to +10% |
| **What makes M grow by 1.6× per realm** | Meridians: 36 acupoints at +10% Qi rate each, opened 0,3,3,3,4,4,5,4,5,5 per realm, each costing 2% of that realm's Qi |
| Consolidation speed in Great Perfection | 2 points per reading hour (20 h to reach 40) |
| Dao Heart scale | 0–100, full after 15 reading hours (decision 2B) |
| Dao of the Myriad Paths unlock (doc: 10,000 pages) | 150 reading hours (decision 5B): about 10,000 pages at a typical 1.1 pages per minute |

The meridian row matters most. With the doc's own bonus sizes (grades at most ×1.5 and ×1.4, manuals capped by book length), nothing else comes close to M̂₉ ≈ 69. Something has to compound per realm. Meridians are realm-gated already, so they are the natural carrier. An alternative would be a "cultivation base" bonus per realm, like plane Qi density.

### Fairness decisions (session 1, implemented here and in the firmware)

| # | Problem found | Decision |
| --- | --- | --- |
| 1A | Night readers got ~12% more Qi (Dao of Flowing Water "+Qi at night") | No time-of-day Dao gives Qi. Flowing Water keeps +Water techniques and a non-power night perk (better event luck at night) |
| 2B | Weekend-only readers' Dao Heart stuck near 4/100 (calendar streak) | Dao Heart grows with reading hours: full after 15 h, never decays. The streak stays as flavor and the Dao of Time unlock; streak decay and the Stillness Charm no longer touch Dao Heart |
| 3A | Heavy readers reached bottlenecks with a weak Dao Heart | Fixed by 2B |
| 4A | Stamina favoured many short sessions (+1 per session) | +1 per 15 minutes read, nothing per session; bank cap 12 |
| 5B | Stones at 1 per page gave fast readers 2× | 1 per minute + 1 per 2 pages, the page part at most half the minutes of the visit. Dao of the Myriad Paths unlocks at 150 h instead of 10,000 pages |
| 6A | The daily soft cap halved weekend binges (1,236 h vs 825 h) | Weekly allowance: 100% for the first 14 h of a calendar week (Mon–Sun), 50% for the next 14 h, 25% after |

### Results (modeled, 300 characters per profile, `report.txt`)

Reading hours to finish the Mortal Plane, median:

| Profile | Before (doc rules) | After (decisions) | Mortal Plane, after |
| --- | --- | --- | --- |
| 15 min/day | 795 h | 795 h | 8.7 y |
| 30 min/day | 791 h | 791 h | 4.3 y |
| 60 min/day | 825 h | 800 h | 2.2 y |
| 90 min/day | 896 h | 803 h | 17.6 mo |
| Evening only, 30/day | 791 h | 791 h | 4.3 y |
| Rotating buckets, 30/day | 771 h | 789 h | 4.3 y |
| Night only, 30/day | 694 h | 803 h | 4.4 y |
| 3 × 10 min/day | 798 h | 798 h | 4.4 y |
| Weekends only, 105 min Sat+Sun | 960 h | 812 h | 4.5 y |
| Weekends only, 210 min Sat+Sun | 1,236 h | 800 h | 2.2 y |

Every habit now lands within 3% of evening-only reading (before: −12% to +56%). 30 min/day stays at 791 h, 2.5% above the 772 h design target, the same as before the decisions, so nothing was re-tuned: the gap comes from Foundation Establishment (34 h vs 30), where the typical multiplier still lags the design curve (×1.99 vs ×2.56).

Other checks after the decisions: Dao Heart ends at 100 for daily and weekend readers alike; stamina is 14 a week for 30 min/day whether read as 1×30 min, 3×10 min or at weekends (before: 17.5, 31.5 and 12.5); stones are 40 vs 45 per 30 minutes for a 0.7 vs 1.4 pages-per-minute reader (before 21 vs 42).

### Earlier findings (doc rules, before the decisions)

These came straight from the document's rules and led to the decisions above:

1. **Night readers get ~12% more Qi.** Dao of Flowing Water gives "+Qi at night", and no other time-of-day Dao gives Qi. This breaks "a night reader and a morning reader with equal minutes get equal Qi."
2. **Weekend-only readers need ~21% more hours.** Dao Heart comes from the calendar streak, so a weekend reader sits near 4/100 forever: worse grades, worse breakthrough odds. The Stillness Charm covers one missed day a week, but a weekend reader misses five. This breaks pillar 4 ("no resource depends on a particular reading habit").
3. **Heavy readers are penalised too.** Realms advance on hours and Dao Heart on days, so a 90-minute reader reaches the Foundation bottleneck on day 9 with Dao Heart around 30. The result is a worse Foundation grade (×0.88 average vs ×0.98) and about 13% more hours overall. With Dao Heart always full, the spread mostly closes.
4. **The soft cap halves a weekend binge.** 210 min on Sat and Sun needs 1236 h vs 825 h for the same weekly minutes spread daily.
5. **Stamina rewards many short sessions.** +1 per session gives 3×10 min/day 31.5 stamina a week, vs 17.5 for one 30-min sitting and 12.5 for weekends-only at the same minutes. The doc says long and short sessions should pay equally.
6. **Spirit Stones at 1 per page break "pace is neutral".** A fast reader earns about 2× the stones of a slow one in the same minutes. Page counts also change with font size, which the doc already avoids for scroll tiers.

Not modelled yet: Tribulation strikes, Heart Demon scenes, alchemy and stamina spending, later planes, legacy.
