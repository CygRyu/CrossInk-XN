# Path of Immortal Reading: game module

Personal-fork game built to `CrossInk_Cultivation_Game___Design_Document.md`. Everything here compiles only with `-DCROSSINK_CULTIVATION=1` (the `cultivation` PlatformIO environment). In the `default` build every `.cpp` here is empty (0 symbols).

| File | Role |
| --- | --- |
| `CultivationConfig.h` | The build flag; defaults to off |
| `QiNumber.{h,cpp}` | 6-byte mantissa/exponent number for Qi, integer-only |
| `GameState.{h,cpp}` | The save: layout, CRC, `.tmp`/`.bak` publish and recovery |
| `StatsBridge.{h,cpp}` | Pure logic: baseline, differences since the last visit, reset detection, weekly allowance |
| `StatsSource.{h,cpp}` | Device-only adapter: reads `GlobalReadingStats::load()` and today's date. Read-only |
| `Rng.h` | Seed mixing and xorshift32 (DOC: deterministic generation) |
| `Simulation.{h,cpp}` | Pure rules: realm table, stage costs, talents, Awakening rolls, name suggestions, character creation, converting a visit into Qi, age, stones, stamina and Dao Heart |
| `CultivationActivity.{h,cpp}` | The screens: name, gender, origin, Awakening Test, Cultivator |

Hooks outside this folder (all inside `#if CROSSINK_CULTIVATION`):

- `src/activities/home/HomeActivity.{h,cpp}`: the "Cultivation" menu entry, shown whenever reading stats are tracked.
- `lib/I18n/translations/english.yaml`: the `STR_CULT_*` block at the end. Other languages fall back to English. The strings are compiled into every build, game or not: about 6 KB of flash (1.7 KB of text plus a 2-byte index per language), and no RAM.

Host tests: `test/cultivation/` (standalone CMake; see its `CMakeLists.txt`).

## Save file

`/.crosspoint/cultivation/save.bin`: 955 bytes = 10-byte header + 945-byte payload, little-endian.

| Offset | Size | Block |
| --- | --- | --- |
| 0 | 10 | magic `PI`, version u16, payload size u16, CRC-32 of payload |
| 10 | 68 | Stats snapshot: seconds, pages, sessions, completed books, 4 time-of-day buckets, 7 weekday buckets, last visit day, seconds credited in that day's week |
| 78 | 16 | Baseline totals at creation (Past Life prologue) |
| 94 | 42 | Character: name (24 B UTF-8), gender, origin, root element and secondary, purity, talent, hidden trait, temperament, 2 paths, karma, rerolls allowed/used, flags, creation day |
| 136 | 38 | Progress: realm, stage, Qi (6 B), stones, stamina, HP, max HP, age in months, age carry, Qi carry, failures, 3 grades, consolidation, Dao Heart, flags (bit 0x04: odd page toward the next stone), reading seconds |
| 174 | 781 | Reserved at full design size: Daos and meridians 23, scrolls 513, inventory 96, world 37, timers 16, daily 25, rival 11, flags and codex 48, legacy 12 |

Rules: append-only. A new field goes at the end, `kSaveVersion` and `kPayloadSize` go up, and older saves load with the new field at its default. All-zero must mean "empty" for every reserved byte, so later milestones can give meaning to reserved bytes without a format change. A newer save loads its known prefix in older firmware (and that firmware writes back only what it knows).

Differences from the design document's budget: the stats snapshot is 68 bytes instead of 24, because it also keeps the time-of-day and weekday buckets (needed for affinity and for attributing reading to calendar weeks), and there is a 16-byte baseline block. The total is still under 1 KB.

## Memory

- `CultivationActivity` allocates `GameState` (~1 KB) with `makeUniqueNoThrow` in `onEnter()` and frees it in `onExit()`. If the allocation fails, the game shows an out-of-memory message instead of aborting.
- The keyboard child activity is allocated by the same pattern and owned by `ActivityManager` (existing CrossInk code).
- `renderer.wrappedText` returns a few short strings per render for wrapped notes (render-time only, as CrossInk's other screens do).
- The creation draft (name, up to 9 destiny rolls) is ~60 bytes of members, gone once the activity closes.
- Saving streams the payload twice, once to compute the CRC and once to the file, so there is no 945-byte buffer.
- Largest stack users: `readLocalStats()` holds a `GlobalReadingStats` (~160 B) for one call; `save::store`/`load` use two 64-byte path buffers and a 32-byte scratch.

## Stats rules (StatsBridge)

- **Local stats only:** `GlobalReadingStats::load()`, never `loadAggregated()`. CrossInk stats files are never written.
- **Reset detection:** any total going down (seconds, pages, sessions, any time-of-day or weekday bucket) rebases the snapshot and credits nothing. `completedBooks` going down alone is not a reset, because unmarking a book does that. `localResetRevision()` is a RAM counter that restarts at 0 on boot, so it is compared only within one boot (`RevisionWatch`).
- **Weekly allowance** (decision 6A, replaces the doc's daily soft cap): 100% Qi for the first 14 h read in a calendar week, Monday to Sunday, 50% for the next 14 h, 25% after. A calendar week rather than a rolling 7 days, because a rolling window would need 7 per-day totals in the save; the calendar week needs none, and it matches the simulator.
  - The snapshot's `lastVisitDay` stays a day index. Its partner field, renamed `secondsCountedInLastVisitWeek` (same offset and size), now holds the reading already credited in the week containing that day. Save v1 stored "seconds credited on that day" there; read as a week total it can only under-count the week, so old saves need no conversion.
  - Same week as the last visit: the new reading uses what is left of that week's allowance.
  - Later week: CrossInk's per-weekday totals say which weekdays the new reading fell on. Each weekday's seconds are spread evenly over that weekday's dates in the gap; dates in the last visit's week use what was left of it, dates in full weeks in between get a fresh allowance each, and dates in the current week start the new week's count. Rounding remainders and reading CrossInk could not date go to the current week.
  - Without a clock, the whole gap counts as one fresh week; X4 "reading days" come later.
- **Sessions are not used.** Stamina is per 15 minutes read (decision 4A), so CrossInk not storing session lengths no longer matters.

## Resource rules (Simulation)

- **Qi:** 10 per qualifying minute × talent × purity, kept exact with a carry.
- **Age:** 1 month per 5 reading minutes, all minutes (allowance or not).
- **Spirit Stones** (decision 5B): 1 per minute read plus 1 per 2 pages; the page part of one visit is at most half that visit's minutes. One leftover page carries to the next visit (flag bit `kOddPageCarry`); pages over the limit are dropped, so auto page-turns gain nothing. A 0.7 and a 1.4 pages-per-minute reader earn 40 and 45 stones per 30 minutes.
- **Stamina** (decision 4A): +1 per 15 minutes read, nothing per session, bank cap 12. Nothing spends it yet (exploration, milestone 8).
- **Dao Heart** (decision 2B): `100 × reading seconds / 15 h`, capped at 100, never decays. The reading streak becomes flavor and the Dao of Time unlock only.
- Minutes for stones, steps for stamina and Dao Heart all come from `gameReadingSeconds` (this character's total reading time), so none of them needs its own carry field.

## Screens (milestone 3)

1. **Name:** type it with the CrossInk keyboard, or Suggest (generated from the world seed). Names are trimmed and must be 1–20 characters the UI font can draw.
2. **Gender:** Male, Female, Non-binary, Prefer not to say. Flavor only.
3. **Origin:** five origins with their birth line.
4. **Awakening Test:** one destiny per screen. Previous and Next move between destinies seen so far, Next past the last one spends a reroll (3 per life), and Keep takes the destiny on screen. Destinies are a pure function of the world seed (MAC, creation day, life number), so leaving and re-entering shows the same ones.
5. **Cultivator:** name, realm and stage, Qi bar with the Qi amount and percentage on one line, age and lifespan, root, talent, Qi rate, Spirit Stones, Stamina, Dao Heart, and what the visit brought (minutes, Qi, stones).

Nothing is saved until a destiny is kept. If a damaged save is found, the new character's first save first moves it to `save.damaged.bin`.

Not yet (later milestones): breakthroughs (the Peak bar stays full and reading there does not carry over yet), the return summary and life log, renaming from the Cultivator screen, origin start bonuses, and anything that spends stamina.
