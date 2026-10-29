---
title: Cultivation (reading game)
nav_order: 8
---

# Cultivation: a reading-driven xianxia game

A zero-player cultivation game built into the reader. Your character gets
stronger the more you read. There is nothing to grind: you read, and when you
open **Cultivation** on the Home menu you see how far they have come.

The entry appears on the Home menu whenever reading stats are enabled. Build
with `-DCROSSINK_CULTIVATION=0` to remove it.

## How progress works

Qi is derived from your all-time reading stats (this reader plus any readers
synced through Stats Sync):

| Source | Qi |
| --- | --- |
| each page turned | 5 |
| each minute read | 3 |
| each finished book | 600 |
| each day of your longest reading streak | 25 |

Seven realms (Qi Condensation, Foundation Establishment, Core Formation,
Nascent Soul, Spirit Severing, Dao Seeking, True Immortal) with nine layers each.
A typical novel (about 300 pages, 8 hours) is roughly 3,500 Qi.

### The ledger (why resets cannot erase your character)

Reading stats can be reset or merged, so the game keeps a tiny ledger in
`/.crosspoint/cultivation.json`: `banked` Qi plus the last derived value it saw.
Each time you open the game, any increase is banked. A decrease (stats reset)
only rebases the ledger. Qi never goes down.

### The journal

The "zero-player" part. Each visit shows events for the Qi gained since you last
opened the game: breakthroughs first, then encounters, herb finds, and
tribulations. The journal is a pure function of `(seed, path, qiFrom, qiTo)`, so
it is never stored and always reproducible.

### Choices

On reaching Foundation Establishment you can choose a permanent Dao path
(Orthodox, Demonic, or Rogue). It currently changes the flavour of duel events.
It is the first "persistent choice", and the save format is ready for more.

## Code map

| Piece | File |
| --- | --- |
| Pure game logic (testable natively) | `lib/Cultivation/CultivationEngine.{h,cpp}` |
| Save file | `src/CultivationStore.{h,cpp}` |
| Screen | `src/activities/home/CultivationActivity.{h,cpp}` |
| Home menu entry | `src/activities/home/HomeActivity.cpp` |
| Tests | `test/cultivation/` |

## Optional heavier layers (not built yet)

The core deliberately has no inventory, techniques, or sect management. They
should attach as layers, each behind its own compile-time flag, so the core
stays small and stable on the ESP32-C3:

1. **Techniques**: add a `uint32_t techniqueMask` to `CultivationStore`, bump
   `kSaveVersion`, unlock techniques at stage thresholds from `stageIndex()`,
   and let a technique modify journal text or attributes.
2. **Sect**: add a sect id plus a rank derived from `stageIndex()`; sect events
   become another journal category.
3. **Inventory**: keep it as a small fixed-size array of item ids; only add it
   if it can be edited with the existing Up/Down/Confirm buttons.

Guidelines for any layer: keep derived values derived, persist only the
player's choices, save atomically, and keep all RAM use fixed-size.
