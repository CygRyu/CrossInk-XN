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
| `GrammarPack.{h,cpp}` | Pure: pack format, parser, the 12-byte-per-section index, extend/replace layering, in-memory reader |
| `Grammar.{h,cpp}` | Pure: the grammar engine (`{key}`, `[a\|b]`, modifiers, variables, plane/theme layers, anti-repetition, depth 14, tidying) |
| `GameValues.{h,cpp}` | Pure: `{$player.name}`, pronouns, verb forms and gendered titles for the grammar |
| `PackCheck.{h,cpp}` | Pure: the pack checker (missing keys, brackets, values, event syntax, line length) |
| `BuiltinPack.{h,cpp}` | The built-in content pack, in flash (~4.3 KB) |
| `PackStore.{h,cpp}` | Device: SD packs, the `index.bin` cache, rebuild + check + `pack_report.txt` |
| `LifeLog.{h,cpp}` | Pure: seasons passed in a visit, which seasons get a line, and the lines themselves (DOC: Time passage) |
| `Chronicle.{h,cpp}` | `chronicle.txt`: each life's opening chapter, and each visit's life-log lines appended once |
| `CultivationActivity.{h,cpp}` | The screens: name, gender, origin, Awakening Test, Return summary, Cultivator, Chronicle |

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
- Content packs and the grammar engine, allocated in `onEnter()` after the save loads and freed in `onExit()`: `PackStore` (~1.3 KB: file table, read buffer), its index (384 × 12 B = 4.6 KB, one allocation sized once) and `Grammar` (~3.4 KB: 1 KB output, 1 KB line arena, ~1 KB anti-repeat memory, ~0.4 KB variables). Heap rather than members of the activity so the game still runs (without generated text) if they cannot be allocated. With the ~1 KB save that is about 10.5 KB, inside the design document's 12 KB. On the Return summary the life log adds ~1.6 KB for a peak of about 12 KB; the Chronicle viewer (~2 KB) is never open at the same time, and both are freed when their screen closes.
- A pack rebuild (only after pack files change) borrows ~4.3 KB more for its duration: a 256-byte line buffer, the findings, the first 20 report messages (3.2 KB) and the checker (~0.7 KB). It happens before `Grammar` is allocated, so the freed space is reused right away.
- Return summary: the visit's `LifeLog` (~1.6 KB: 8 lines of 200 bytes) is allocated when a visit with reading starts and freed when the summary is left (or in `onExit()`). If it cannot be allocated, the summary shows the numbers without lines and nothing is added to the Chronicle.
- Chronicle viewer: `ChronicleView` (~2 KB: the byte offsets of the last 512 pages, for Previous) is allocated when the Chronicle is opened and freed when it is closed. Each page is read straight from the card, one paragraph at a time, through a 256-byte stack buffer.
- Writing the Chronicle uses a 256-byte stack buffer for the file's tail; lines are appended, never rewritten.
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
- **Origin gifts** (agreed in session 2): starting stones and max HP are granted at creation; a character made before gifts existed gets them once on its next visit (character flag `kOriginGiftsGiven`, bit 0x04 of a byte that already exists, so no format change). No origin changes the Qi rate. The other gifts are recorded in `kOriginBonuses` for the systems that will use them.

  | Origin | Stones | Now | Later |
  | --- | --- | --- | --- |
  | Farmer's child | 30 | +20 max HP (120) | |
  | Fallen clan heir | 100 | | +5 Foundation grade score (milestone 5); rival known from day one |
  | Street orphan | 10 | | +5 luck on event checks (milestone 8) |
  | Sect servant | 50 | | Sect gate open from Qi Condensation instead of Foundation (milestone 8) |
  | Herbalist's apprentice | 40 | | Knows one Qi pill recipe; +10% brew quality (milestone 8) |
- Minutes for stones, steps for stamina and Dao Heart all come from `gameReadingSeconds` (this character's total reading time), so none of them needs its own carry field.

## Content packs and the grammar engine (milestone 4, the doc's build step 3)

Pack format and grammar syntax for authors: `tools/cultivation/PACKS.md`.

- **Sources:** the built-in pack (source 0, flash) and up to 15 SD files from `/.crosspoint/cultivation/lists/*.txt` and `events/*.txt` (sources 1..15, in name order). An SD list with an existing key adds entries; `[!key]` replaces earlier ones.
- **Index:** one `PackSection` (12 B: key hash, byte offset, entry count, source, flags) per `[section]`, not per line. Reading entry i walks the section's lines from its start; lists are short, and reading forward continues from the last entry, so the checker never rewinds.
- **`index.bin`:** the SD sections, plus each file's name, size and modification time and a hash of the built-in pack, with a CRC. Any difference (file added, removed, edited, new firmware pack, damaged cache) triggers a rescan on the device. The built-in pack is re-parsed from flash on every open (a few ms).
- **Checking:** a rescan runs `PackChecker` over the SD packs (built-in lists count as known keys) and writes `pack_report.txt`, or deletes it when all is well. The count is kept in `index.bin`, so the Cultivator's notice survives later opens. The host tests run the same checker on the built-in pack in CI, and `test/cultivation` also builds `cultivation_pack_check` for checking packs on a computer.
- **Engine determinism:** output is a pure function of seed, packs and the anti-repeat history of the open session. Seeds are mixed before use, so consecutive days give unrelated picks.
- **Rumor of the day:** `{rumor}` seeded by `contentSeed(world seed, 50, day index)`; without a clock, by reading hour. Shown under the visit summary.

## Life log and the Chronicle (milestone 5, the doc's build step 4)

- **Seasons:** a season is 3 in-game months, 15 reading minutes. Seasons are counted from `ageMonths` (season k = months 3k..3k+2), so short visits add up without a save field.
- **Lines:** one per completed season, at most 6 per visit; more than 6 are spread evenly, first and last included. Plus one line when stages were gained (`log.stage`) and one when the Peak bar fills (`log.bottleneck`). Each line starts "Spring, age 17." and is seeded by `contentSeed(world seed, 51, season)` (52 stage, 53 bottleneck), so the same season always gives the same line.
- **Lists:** Body Tempering mixes the origin's own list (`log.body.farmer` ... `log.body.herbalist`, weight 3), `log.body` (4) and `log.any` (3); Qi Condensation mixes `log.qi` (6) and `log.any` (3); later realms use `log.any` until they get lists of their own. Lines never grant anything.
- **Chronicle file:** `/.crosspoint/cultivation/chronicle.txt`. A new character (or the first visit of one made before this update) appends an opening chapter: the book's title if the file is new, "Life N: name", then `chronicle.birth.<origin>`, `chronicle.awakening` and, if there was earlier reading, `chronicle.past`. Each visit appends its lines at the bottom, with an "Age N" heading when the age changes.
- **No double lines:** the Chronicle is written before the save. If the save fails, the next visit re-counts the same reading, builds the same lines, finds the file already ends with them, and skips them. A line cut short by a power loss is closed with a newline before anything new is written.
- **Reading it:** the Cultivator's Confirm button opens the game's own page viewer. It is not CrossInk's TXT reader, which would record the file as the current book (sleep screen, resume on boot) and return Home on Back. No reading stats are recorded by the viewer. (CrossInk's TXT reader records none either; if a future CrossInk update changes that, `.txt` reading would start earning Qi, which would also matter for the Chronicle only if it were opened there.)

## Screens (milestone 3)

1. **Name:** type it with the CrossInk keyboard, or Suggest (generated from the world seed). Names are trimmed and must be 1–20 characters the UI font can draw.
2. **Gender:** Male, Female, Non-binary, Prefer not to say. Flavor only.
3. **Origin:** five origins with their birth line; the selected origin's gifts are shown under the list.
4. **Awakening Test:** one destiny per screen. Previous and Next move between destinies seen so far, Next past the last one spends a reroll (3 per life), and Keep takes the destiny on screen. Destinies are a pure function of the world seed (MAC, creation day, life number), so leaving and re-entering shows the same ones.
5. **Return summary** (milestone 5, after any visit with reading): how many seasons or months passed, realm, stage and age, the minutes, Qi and stones the visit brought, and the life-log lines. Next and Previous page through long logs; Continue or Back goes to the Cultivator screen.
6. **Cultivator:** name, realm and stage, Qi bar with the Qi amount and percentage on one line, age and lifespan, root, talent, Qi rate, Spirit Stones, Stamina, Dao Heart, origin gifts the one time they are granted, the rumor of the day, and a notice when an SD pack has problems. Confirm opens the Chronicle.
7. **Chronicle:** the Chronicle one page at a time, from the beginning; Next and Previous turn pages, Back returns.

Nothing is saved until a destiny is kept. If a damaged save is found, the new character's first save first moves it to `save.damaged.bin`.

Not yet (later milestones): breakthroughs (the Peak bar stays full and reading there does not carry over yet), renaming from the Cultivator screen, origin gifts other than stones and HP, and anything that spends stamina.
