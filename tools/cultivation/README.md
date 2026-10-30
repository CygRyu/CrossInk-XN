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
- **modeled**: multipliers come from the game's systems (destiny roll with 3 rerolls keeping the best, root purity, Cultivation manuals from finished books, Foundation and Core grades, meridians, Dao of Flowing Water, daily omen), with breakthrough rolls, pity, and a 25% Peak-bar loss on failure. Results are Monte Carlo medians and p10–p90 ranges over N characters. All profiles use the same random draws so they compare cleanly.

### What is DOC vs PROPOSED

`TUNING` in `balance_sim.py` marks each value. **DOC** values come from the design document. **PROPOSED** values fill gaps the document leaves open:

| Gap in the document | Proposed value |
| --- | --- |
| Per-realm hours chart (did not survive export) | 4.5, 10.5, 30, 52, 75, 90, 105, 120, 135, 150 h. These meet every checkpoint in the text: 772 h total, age 31 / 113, 5–10 months per major realm |
| Talent tiers between ×0.6 and ×1.5 | Choked ×0.6 (35%), Dull ×0.8 (30%), Ordinary ×1.0 (20%), Keen ×1.2 (11%), Heaven-Blessed ×1.5 (4%) |
| Size of the manual, purity and study bonuses | manual ×1.15 / 1.30 / 1.50 / 1.75 / 2.00 by tier; purity ×0.85–1.15; study up to +10% |
| **What makes M grow by 1.6× per realm** | Meridians: 36 acupoints at +10% Qi rate each, opened 0,3,3,3,4,4,5,4,5,5 per realm, each costing 2% of that realm's Qi |
| Consolidation speed in Great Perfection | 2 points per reading hour (20 h to reach 40) |
| Dao Heart scale | 0–100, full at a 30-day streak |

The meridian row matters most. With the doc's own bonus sizes (grades at most ×1.5 and ×1.4, manuals capped by book length), nothing else comes close to M̂₉ ≈ 69. Something has to compound per realm. Meridians are realm-gated already, so they are the natural carrier. An alternative would be a "cultivation base" bonus per realm, like plane Qi density.

### Results (modeled, 300 characters per profile)

| Profile | Mortal Plane, median | p10–p90 | Reading hours |
| --- | --- | --- | --- |
| 15 min/day | 8.7 y | 6.5–11.5 y | 795 h |
| 30 min/day | 4.3 y | 3.2–5.6 y | 791 h |
| 60 min/day | 2.3 y | 1.7–2.9 y | 825 h |
| 90 min/day | 19.6 mo | 15–25 mo | 896 h |
| Night only, 30/day | 3.8 y | 2.9–4.9 y | 694 h |
| Weekends only, 105 min Sat+Sun | 5.3 y | 4.1–7.1 y | 960 h |

At 30 min/day every realm lands within ±16% of its target hours (Body Tempering runs fastest at 3.8 h vs 4.5; see `report.txt`).

### Fairness findings: the design conflicts with itself

These come straight from the document's rules, not from the proposed numbers:

1. **Night readers get ~12% more Qi.** Dao of Flowing Water gives "+Qi at night", and no other time-of-day Dao gives Qi. This breaks "a night reader and a morning reader with equal minutes get equal Qi."
2. **Weekend-only readers need ~21% more hours.** Dao Heart comes from the calendar streak, so a weekend reader sits near 4/100 forever: worse grades, worse breakthrough odds. The Stillness Charm covers one missed day a week, but a weekend reader misses five. This breaks pillar 4 ("no resource depends on a particular reading habit").
3. **Heavy readers are penalised too.** Realms advance on hours and Dao Heart on days, so a 90-minute reader reaches the Foundation bottleneck on day 9 with Dao Heart around 30. The result is a worse Foundation grade (×0.88 average vs ×0.98) and about 13% more hours overall. With Dao Heart always full, the spread mostly closes.
4. **The soft cap halves a weekend binge.** 210 min on Sat and Sun needs 1236 h vs 825 h for the same weekly minutes spread daily.
5. **Stamina rewards many short sessions.** +1 per session gives 3×10 min/day 31.5 stamina a week, vs 17.5 for one 30-min sitting and 12.5 for weekends-only at the same minutes. The doc says long and short sessions should pay equally.
6. **Spirit Stones at 1 per page break "pace is neutral".** A fast reader earns about 2× the stones of a slow one in the same minutes. Page counts also change with font size, which the doc already avoids for scroll tiers.

Not modelled yet: Tribulation strikes, Heart Demon scenes, alchemy and stamina spending, later planes, legacy.
