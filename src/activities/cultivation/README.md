# Path of Immortal Reading: game module

Personal-fork game built to `CrossInk_Cultivation_Game___Design_Document.md`. Everything here compiles only with `-DCROSSINK_CULTIVATION=1` (the `cultivation` PlatformIO environment). In the `default` build every `.cpp` here is empty (0 symbols).

| File | Role |
| --- | --- |
| `CultivationConfig.h` | The build flag; defaults to off |
| `QiNumber.{h,cpp}` | 6-byte mantissa/exponent number for Qi, integer-only |
| `GameState.{h,cpp}` | The save: layout, CRC, `.tmp`/`.bak` publish and recovery |
| `StatsBridge.{h,cpp}` | Pure logic: baseline, differences since the last visit, reset detection, daily soft cap |
| `StatsSource.{h,cpp}` | Device-only adapter: reads `GlobalReadingStats::load()` and today's date. Read-only |

Host tests: `test/cultivation/` (standalone CMake; see its `CMakeLists.txt`).

## Save file

`/.crosspoint/cultivation/save.bin`: 955 bytes = 10-byte header + 945-byte payload, little-endian.

| Offset | Size | Block |
| --- | --- | --- |
| 0 | 10 | magic `PI`, version u16, payload size u16, CRC-32 of payload |
| 10 | 68 | Stats snapshot: seconds, pages, sessions, completed books, 4 time-of-day buckets, 7 weekday buckets, last visit day, seconds credited that day |
| 78 | 16 | Baseline totals at creation (Past Life prologue) |
| 94 | 42 | Character: name (24 B UTF-8), gender, origin, root element and secondary, purity, talent, hidden trait, temperament, 2 paths, karma, rerolls allowed/used, flags, creation day |
| 136 | 38 | Progress: realm, stage, Qi (6 B), stones, stamina, HP, max HP, age in months, age carry, Qi carry, failures, 3 grades, consolidation, Dao Heart, flags, reading seconds |
| 174 | 781 | Reserved at full design size: Daos and meridians 23, scrolls 513, inventory 96, world 37, timers 16, daily 25, rival 11, flags and codex 48, legacy 12 |

Rules: append-only. A new field goes at the end, `kSaveVersion` and `kPayloadSize` go up, and older saves load with the new field at its default. All-zero must mean "empty" for every reserved byte, so later milestones can give meaning to reserved bytes without a format change. A newer save loads its known prefix in older firmware (and that firmware writes back only what it knows).

Differences from the design document's budget: the stats snapshot is 68 bytes instead of 24, because it also keeps the time-of-day and weekday buckets (needed for affinity and for applying the soft cap per day), and there is a 16-byte baseline block. The total is still under 1 KB.

## Memory

- Milestone 2 makes no heap allocations.
- Saving streams the payload twice, once to compute the CRC and once to the file, so there is no 945-byte buffer.
- Largest stack users: `readLocalStats()` holds a `GlobalReadingStats` (~160 B) for one call; `save::store`/`load` use two 64-byte path buffers and a 32-byte scratch.
- `GameState` itself (~1 KB in RAM) will be allocated with `makeUniqueNoThrow` in the activity's `onEnter()` and freed in `onExit()` (milestone 3), so it costs nothing while the game is closed.

## Stats rules (StatsBridge)

- **Local stats only:** `GlobalReadingStats::load()`, never `loadAggregated()`. CrossInk stats files are never written.
- **Reset detection:** any total going down (seconds, pages, sessions, any time-of-day or weekday bucket) rebases the snapshot and credits nothing. `completedBooks` going down alone is not a reset, because unmarking a book does that. `localResetRevision()` is a RAM counter that restarts at 0 on boot, so it is compared only within one boot (`RevisionWatch`).
- **Daily soft cap:** CrossInk's per-weekday totals show which days the new reading happened on, so a visit after three days caps each day separately. For gaps of a week or more, each weekday's allowance is multiplied by how often it occurred. Without a clock, the whole gap counts as one day; X4 "reading days" come later.
- **Sessions of 5+ minutes:** CrossInk does not store session lengths, so the count is approximated as `min(new sessions, new minutes / 5)`.
