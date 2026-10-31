# Writing content packs

All text in Path of Immortal Reading comes from content packs: plain `.txt` files of word lists and events that the game's grammar engine combines into sentences. The firmware has a built-in pack (`src/activities/cultivation/BuiltinPack.cpp`); packs on the SD card are layered on top of it, so new content never needs a reflash.

## Where packs go

```
/.crosspoint/cultivation/lists/*.txt     word and phrase lists
/.crosspoint/cultivation/events/*.txt    events (exploration arrives in a later update)
```

Any number of files up to 15, any names ending in `.txt`, with names under about 40 characters. Files are read in name order, so `b.txt` comes after `a.txt`. Other files in the game folder (the save, `chronicle.txt`) are never read as packs.

The game notices added, removed or edited files the next time it opens: it re-reads the packs, checks them, and saves an index (`index.bin`) so later opens are instant. If it finds problems it writes `pack_report.txt` next to the save and shows a one-line notice on the Cultivator screen. Fix the file and reopen the game; the report disappears once everything is fine. Text from a broken line is skipped, never shown half-expanded.

## Lists

```
# A comment. Blank lines are fine.
[rumor.body]
the ferry at {place.town} stopped running after the boatman was found with a sword wound in his back
{beast.a} took [two|three|four] mules off the road near {place.town}, and the magistrate has posted a reward
```

- A `[header]` starts a list. Keys use lowercase `a-z`, digits and `. _ - @ ~`, up to 47 characters.
- Every other non-blank, non-comment line is one entry. Leading and trailing spaces are trimmed.
- Keep each line under 191 bytes; longer lines are cut on the device and reported.
- A list with the same key in an SD pack **adds** its entries to the built-in list.
- `[!key]` **replaces** every earlier list with that key (built-in or from an earlier file).

## Grammar

| Write | Gives |
| --- | --- |
| `{key}` | one entry of list `key`, itself expanded |
| `[a\|b\|c]` | one of the options; options can contain `{...}` and nested `[...]` |
| `{key.cap}` | first letter upper case. Also `lower`, `title` (Every Word), `a` (a/an), `the`, `s` (plural). Chain them: `{beast.a.cap}` |
| `{key>name}` | the pick, also remembered as `name` for the rest of the scene |
| `{$name}` | a remembered pick, or a game value (below) |
| `{name.[m\|f]}` | a key built from parts: here `name.m` or `name.f` |
| `\{`, `\[`, `\|` | the character itself |

Game values: `$player.name`, `$player.realm` (or `$realm`), `$player.stage`, `$player.age`, `$player.element`, `$season` ("spring"), `$root.glow` and `$root.color` (the Awakening Test stone: "murky", "green"), `$past.pages`, `$past.books` and `$past.hours` (reading from before this life, with the noun: "48,210 pages", "1 book"); pronouns `$pro.they`, `$pro.them`, `$pro.their`, `$pro.theirs`, `$pro.themself` with matching verbs `$pro.is`, `$pro.was`, `$pro.has` ("{$pro.they.cap} {$pro.is} ready" gives "She is ready" or "They are ready"); titles `$title.senior` (Senior Brother / Sister / Sibling / Senior), `$title.sect` (Sect Master / Mistress / Head) and `$title.god` (Godking / Godqueen / Godsovereign); `$scroll.name`, a Technique Scroll's name, only in the `scroll.line.*` lists; `$region.name` and `$beast.name` only in the exploration lists.

After expanding, the engine tidies spacing ("word ," becomes "word,") and doubled articles ("the the river" becomes "the river"). Lists remember their recent picks (60% of the list, at most 16) and avoid repeating them.

### Layers

- `[key@1]` is used instead of `key` on plane 1 (the Immortal Plane), `@2` on the Divine Plane. Without such a list, plain `key` is used.
- `[key~theme]` is used instead of `key` inside an event with that theme (for example `weather~demonic`).

## The life log and the Chronicle

The Return summary shows one line per in-game season (15 reading minutes) that passed since the last visit, up to 6, and every line is also added to the bottom of `/.crosspoint/cultivation/chronicle.txt`. The game puts "Spring, age 17." in front of each line, so write one full sentence in the second person ("You ..."), starting with a capital and ending with a full stop, under 170 bytes. In these lines `{$season}` and `{$player.age}` are the season the line describes.

Lines never grant anything, so they never mention Spirit Stones, which are a real resource the player counts: use mortal money (taels, silver, copper cash). The host tests check this for the built-in pack.

Within a realm, each list's sentences are used once each, in an order shuffled per world, before any comes back, even across visits. Adding sentences to a list makes the cycle longer.

| List | Used for |
| --- | --- |
| `log.body` | Body Tempering, any origin |
| `log.body.farmer`, `log.body.clan`, `log.body.orphan`, `log.body.servant`, `log.body.herbalist` | Body Tempering, that origin only |
| `log.qi` | Qi Condensation |
| `log.foundation` | Foundation Establishment |
| `log.core` | Core Formation |
| `log.any` | every realm; the only list after Core Formation for now |
| `log.gp` | seasons spent in Great Perfection (mixed with the realm's lists) |
| `log.halfstep` | seasons after a failed breakthrough, until the next success |
| `log.stage` | the visit gained stages; use `{$player.realm}, {$player.stage}` ("Body Tempering, Layer 4") |
| `log.bottleneck` | the Peak bar just filled |
| `log.breakthrough` | a breakthrough succeeded; `{$player.realm}` is the new realm |
| `log.failure` | a breakthrough failed; `{$player.realm}` is the realm still held |
| `log.gp.enter` | the player chose Great Perfection |
| `chronicle.birth.farmer` ... `chronicle.birth.herbalist` | first paragraph of each life in the Chronicle |
| `chronicle.awakening` | the Awakening Test, with `$root.glow`, `$root.color` and `$player.element` |
| `chronicle.past` | only when there was reading before the character: `$past.pages`, `$past.books`, `$past.hours` |

```
[log.body.farmer]
You dig an irrigation ditch for the village headman, and he pays you in rice.
```

## Technique Scrolls

Every book marked as finished becomes a Technique Scroll. Its name is built as

```
{scroll.name} {scroll.kind.<kind>} {scroll.tier.<tier>}
```

with the scroll's element as the theme, so `[scroll.image~water]` replaces `scroll.image` for Water scrolls (themes: `wood`, `fire`, `earth`, `metal`, `water`). A themed list can still mix in the common words with an entry like `{scroll.image.common}` (not `{scroll.image}`, which would pick the themed list again). Picks are stable: the same book always gets the same name in a life, and the book's own title never contributes a word. Changing these lists in an SD pack renames existing scrolls.

| List | Used for |
| --- | --- |
| `scroll.name` | the pattern before the kind word; uses the lists below |
| `scroll.adj`, `scroll.number`, `scroll.heavens`, `scroll.verb` | words in the patterns |
| `scroll.image`, `scroll.color`, `scroll.beast`, `scroll.place` | words in the patterns, each with `~wood` ... `~water` versions |
| `scroll.kind.sword`, `.palm`, `.body`, `.movement`, `.mind`, `.manual` | the kind word: "Saber", "Fist", "Iron Skin", "Light Body", "Heart", "Breathing" |
| `scroll.tier.mortal`, `.yellow`, `.earth`, `.heaven`, `.saint` | the suffix: Method, Art, Scripture, Canon, True Classic |
| `scroll.line.new` | Chronicle: a new scroll; `{$scroll.name}` is its name |
| `scroll.line.reread` | Chronicle: a reread raised its quality |
| `scroll.line.sold` | Chronicle: sold, by the player or because the library was full |
| `scroll.line.key` | Chronicle: spent as the key of a breakthrough; `{$player.realm}` is the new realm |

Names should read like wuxia and xianxia manuals ("Moon-Severing Sword Canon", "Azure Dragon Body Refining Scripture"); keep a whole name under about 60 bytes. Chronicle lines follow the life log's rules: one sentence, second person, a full stop at the end.

## Tribulations and Heart Demons

| List | Used for |
| --- | --- |
| `trib.gather`, `demon.gather` | Chronicle: the trial begins |
| `trib.strike` | the Trial screen: one strike (no season label) |
| `demon.origin.farmer` ... `demon.origin.herbalist` | the Trial screen: the first scene, by origin |
| `demon.family` | the second scene |
| `demon.failure`, `demon.years` | the third scene: after a failed attempt at this barrier, or otherwise |
| `trib.survive`, `trib.fail`, `demon.pass`, `demon.fail` | Chronicle: how it ended |
| `trib.burn` | Chronicle: a scroll burned to stop a strike or pass a scene; `{$scroll.name}` is its name |

Scenes are what the cultivator sees in seclusion: concrete people and places from their past, one sentence, present tense.

## Exploration (v0.1.10)

Region slots have a theme each, in order: `marsh`, `wastes`, `forest`, `gorge`, `snow`, `delta`, `volcano`, `battlefield`, `salt`, `ridge`. The themed lists use the theme layer, so `[region.name~marsh]` is the marsh slot's list and plain `[region.name]` is the fallback. Names are picked once per world and never change; a region keeps its name when it reshapes.

| List | Used for |
| --- | --- |
| `region.name` (`~theme`) | a region's name, used inside sentences ("the Reedwater Marshes"; the screen capitalises it for titles) |
| `beast.region`, `bandit.region`, `cultivator.region`, `spirit.region` (`~theme`) | v0.2.0: the enemies met on the region's paths, by kind, with an article ("a marsh crocodile", "two smugglers poling a flat boat of salt"); lairs use the beast or bandit list |
| `guardian.beast`, `guardian.bandit`, `guardian.cultivator`, `guardian.spirit` (`~theme`) | v0.2.0: the region's guardian, by its kind (replaces `guardian.region`); a new one after each reshape |
| `ingredient.herb`, `ingredient.ore` (`~theme`) | v0.2.0: each region's own herb and ore, lower case, no article ("bitter reed-root", "bog iron"); one of each per region is picked by the world seed, so keep each theme's names distinct |
| `explore.path`, `explore.herb`, `explore.ore`, `explore.ruin.won`, `explore.ruin.trap`, `explore.cave`, `explore.quiet`, `explore.quiet.full`, `explore.merchant` | the map: one line for what a step found (no season label) |
| `explore.fight.beast`, `explore.fight.bandit`, `explore.fight.cultivator`, `explore.fight.spirit`, `explore.fight.lair`, `explore.fight.hideout`, `explore.fight.guardian` | the Fight screen's line, by kind (a hideout is a bandits' lair) |
| `explore.won` (beasts), `explore.won.bandit`, `explore.won.cultivator`, `explore.won.spirit`, `explore.lost`, `explore.slip`, `explore.paid`, `explore.drivenoff` | after a fight, on the map |
| `alchemy.learned`, `alchemy.miss`, `alchemy.collect` | v0.2.0: the Alchemy screen's line after an experiment or taking pills out; `{$pill.name}` is set ("Core-Forming pill") |
| `alchemy.line.learned`, `alchemy.line.slip` | v0.2.0: Chronicle lines for a recipe worked out and a recipe slip; `{$pill.name}` is set |
| `explore.line.guardian`, `explore.line.clear`, `explore.line.upheaval`, `explore.line.talisman` | Chronicle lines (the upheaval line also shows in the Return summary) |

`{$region.name}` and `{$beast.name}` (the beast or guardian) are set in these lists only. Write beasts and guardians with their article and in lower case unless they are a name ("a white bear", "the Snow Bear of Frozen Bell"); use `{$beast.name.cap}` at the start of a sentence. Since v0.2.0 each enemy kind has its own lines; write bandit and cultivator lines so they fit one person or a group ("they"), and keep `explore.lost` and `explore.slip` neutral, since they follow every kind.

## Events

Events are written now and come alive with exploration in a later update. The checker already validates them.

```
[event.ruins]
@weight 6
@req realm>=1 stamina>=1
While crossing {place.region>where}, you find {ruin.desc}.
> Search the ruins carefully
  @check luck 40
  @win stones+[20-60] text={ruin.loot}
  @lose hp-10 text={ruin.trap}
> Burn incense for the dead and move on
  @effect karma+3 text=You burn a stick of incense for the dead of {$where} and go on your way.
```

- `@weight N`: relative chance among events that qualify.
- `@req`: conditions such as `realm>=2`, `karma<0`, `element=water`, `plane=1`, `flag.met_master=1`, `dao.time>=3`. Fields: realm, stage, plane, element, karma, path, tier, stamina, stones, hp, origin, gender, night, luck, `flag.*`, `dao.*`.
- `> text`: a choice. The lines under it say what happens.
- `@check luck|heart|karma|power N`, then `@win` and `@lose`; or `@effect` for a choice without a check.
- Effects: `name+N`, `name-N`, `name+[a-b]`, `flag=name`, `unflag=name`, `codex=name`, and `text=` with the Chronicle line (the rest of the line).

## Checking packs on a computer

The host test build also builds `cultivation_pack_check`, which uses the device's own parser, checker and engine:

```sh
cmake -S test/cultivation -B build/cultivation-tests && cmake --build build/cultivation-tests -j
build/cultivation-tests/cultivation_pack_check my-pack.txt                         # check
build/cultivation-tests/cultivation_pack_check --expand rumor --count 20 my-pack.txt  # preview
```

Without a computer, put the file on the card and open the game: the same checks run there, and `pack_report.txt` lists what they found.
