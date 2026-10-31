# Path of Immortal Reading: desktop tools

Host-side tools for the cultivation game. Nothing in this folder is compiled into the firmware.

- `balance_sim.py`: the balance simulator (below).
- `PACKS.md`: how to write content packs for the grammar engine, and how to check them.
- `example-pack/`: an SD pack to copy to `/.crosspoint/cultivation/` for trying out packs.
- `sim/`: builds the CrossInk simulator with the game on (no PlatformIO needed) and plays button scripts with screenshots, with fake reading time; see `sim/README.md`.

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

### Milestone 7: Technique Scroll keys and manual pity

The simulator now follows the firmware's scroll rules (user decisions, session 3): a bottleneck from Foundation Establishment on needs an unequipped scroll of at least Mortal / Yellow / Earth / Heaven tier (Foundation / Core / Nascent Soul / later), **from any realm**, spent only on success; a book read under 30 minutes gives no scroll; if none of the last 4 scrolls was a Cultivation manual the next one is; a better manual is equipped and the old one becomes a spare scroll. Before, the simulator only counted a scroll finished in the current realm. Breakthrough pills stay modelled as the third key (3 reading hours in the realm), as before.

| 30 min/day (300 characters) | Before | After |
| --- | --- | --- |
| Mortal Plane, reading hours (median) | 791 h | 754 h (2.3% under the 772 h target) |
| Mortal Plane, years | 4.3 y | 4.1 y |
| Spread of habits vs evening-only | within 3% | within 3% (largest +2.4%, 60 and 90 min/day) |

Most of the change comes from manual pity (more manuals, so a higher Qi multiplier earlier), not from the key rule. Nothing was re-tuned.

Keys without breakthrough pills (`--extra` prints this in the fairness section): with typical books (median 9 h) 30 min/day needs 796 h and everyone finishes; with **short books (median 4 h) only 183 of 300 finish within 40 years and the rest stop around realm 7**, because Heaven tier needs 15 hours in one book. This is the fairness caveat agreed with the user: breakthrough pills (alchemy) must arrive before players reach the Soul Transformation bottleneck.

### v0.1.9: Tribulations and Heart Demons

The simulator now plays the trial before each roll that has one (Core Formation, Soul Transformation, Body Integration and Tribulation Transcendence: 3/5/7/9 strikes; Nascent Soul, Void Refinement and Mahayana: a Heart Demon of 3 scenes at 40 + Dao Heart x 30 / 100 + Golden Core grade 0-20). The model is cautious: it endures every strike from full HP and burns a spare scroll (never the last key) only when the next strike would kill; it never turns strikes with techniques or uses a Body technique or Mind scroll. A lost trial is a failed attempt.

| 30 min/day (300 characters) | Before | After |
| --- | --- | --- |
| Mortal Plane, reading hours (median) | 754 h | 762 h |
| Trials lost per life (mean) | - | 0.49 |
| Scrolls burned in tribulations per life (mean) | - | 11.3 |
| Spread of habits vs evening-only | within 2.4% | within 1.7% |

Nothing was re-tuned. Real players who turn strikes with techniques burn fewer scrolls.

### v0.1.10: Exploration, Spirit Stones and merchant items

Exploration gives no Qi, so it changes no reading-hour targets by itself. What it adds is a use for Spirit Stones: wandering merchants sell Thunder-Warding Talismans (150 stones, take one whole strike), Calm-Heart Incense (120, +25 to one Heart Demon scene) and tonics (Greater: 80, heals 50% of max HP). The simulator now counts stones from reading (decision 5B, 1 page a minute) and, before each trial, passes 3 merchants whose stock has each item at the agreed rate (30% / 30% / 25%); it buys what the coming trial needs, holding at most 2 talismans, 2 incense and 1 tonic. In the trial it uses a talisman before burning a scroll, then a tonic, and burns incense on Heart Demon scenes. Stones and items found while exploring are left out, so the model is cautious.

| 30 min/day (300 characters) | Without items (v0.1.9) | With items (v0.1.10) |
| --- | --- | --- |
| Trials lost per life (mean) | 0.49 | 0.30 |
| Scrolls burned in tribulations per life (mean) | 11.3 | 3.4 |
| Mortal Plane, reading hours (median) | 762 h | 774 h (within run-to-run noise: the item rolls shift the random sequence) |
| Stones spent on items / earned | - | about 1,400 of 70,000 |
| Spread of habits vs evening-only | within 1.7% | within 1.9% |

Nothing was re-tuned. Stones pile up: the merchants' items are cheap next to a life's income, so alchemy (v0.2.0) is where most stones should go.

### v0.2.0: Alchemy

The simulator always modelled breakthrough pills (a key after 3 reading hours in the realm, +15 chance from Foundation on, grade pills worth 10); the firmware had none until v0.2.0. The model now matches what was built: one breakthrough pill per attempt, of the best grade the open regions allow and medium quality on average (+17), a key when its grade fits (common up to Nascent Soul, spirit up to Body Integration, rare any), used up on every attempt; two medium grade pills (+8 each) before the Foundation and Core grades. Brewing one pill takes 40-60 reading minutes plus gathering, still modelled as 3 reading hours in the realm. Qi Gathering pills are left out (+25% for an hour of reading at a time), so the model is cautious.

| 30 min/day (300 characters) | v0.1.10 model | v0.2.0 model |
| --- | --- | --- |
| Mortal Plane, reading hours (median) | 774 h | 751 h |
| Spread of habits vs evening-only | within 1.9% | within 1.0% |
| Short books (median 4 h), with pills | - | 849 h, 300/300 finish |
| Short books (median 4 h), scrolls only | 214/300 finish, the rest stop in realm 7 | (unchanged) |

The Soul Transformation bottleneck no longer stalls short-book readers: a spirit breakthrough pill (region 4's ingredients, open from Nascent Soul) opens it. Nothing was re-tuned.

### v0.2.1: Depth

Depth (user, v0.2.1): reading at a full Peak bar outside Great Perfection adds 2 points an hour, up to 20; it counts half as much as consolidation toward the grade score and the breakthrough chance, within consolidation's caps. The model adds it while a character waits at a full bar (for a pill in pill mode, for a scroll in scrolls-only mode).

| 30 min/day (300 characters) | without depth | with depth |
| --- | --- | --- |
| Mortal Plane, reading hours (median) | 751 h | 751 h |
| Short books (4 h), scrolls only: finish within 40 years | 214/300 | 209/300 (run-to-run noise) |

Depth changes almost nothing in the model: with breakthrough pills a character rarely sits at a full bar for long, and depth is no key, so readers stuck for a Heaven scroll stay stuck. What it changes is what waiting feels like on the device: Qi past the bar is no longer simply thrown away. Nothing was re-tuned.

### Earlier findings (doc rules, before the decisions)

These came straight from the document's rules and led to the decisions above:

1. **Night readers get ~12% more Qi.** Dao of Flowing Water gives "+Qi at night", and no other time-of-day Dao gives Qi. This breaks "a night reader and a morning reader with equal minutes get equal Qi."
2. **Weekend-only readers need ~21% more hours.** Dao Heart comes from the calendar streak, so a weekend reader sits near 4/100 forever: worse grades, worse breakthrough odds. The Stillness Charm covers one missed day a week, but a weekend reader misses five. This breaks pillar 4 ("no resource depends on a particular reading habit").
3. **Heavy readers are penalised too.** Realms advance on hours and Dao Heart on days, so a 90-minute reader reaches the Foundation bottleneck on day 9 with Dao Heart around 30. The result is a worse Foundation grade (×0.88 average vs ×0.98) and about 13% more hours overall. With Dao Heart always full, the spread mostly closes.
4. **The soft cap halves a weekend binge.** 210 min on Sat and Sun needs 1236 h vs 825 h for the same weekly minutes spread daily.
5. **Stamina rewards many short sessions.** +1 per session gives 3×10 min/day 31.5 stamina a week, vs 17.5 for one 30-min sitting and 12.5 for weekends-only at the same minutes. The doc says long and short sessions should pay equally.
6. **Spirit Stones at 1 per page break "pace is neutral".** A fast reader earns about 2× the stones of a slow one in the same minutes. Page counts also change with font size, which the doc already avoids for scroll tiers.

Not modelled yet: Tribulation strikes, Heart Demon scenes, alchemy and stamina spending, later planes, legacy.
