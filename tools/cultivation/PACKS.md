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

Game values: `$player.name`, `$player.realm` (or `$realm`), `$player.stage`, `$player.age`, `$player.element`, `$season` ("spring"), `$root.glow` and `$root.color` (the Awakening Test stone: "murky", "green"), `$past.pages`, `$past.books` and `$past.hours` (reading from before this life, with the noun: "48,210 pages", "1 book"); pronouns `$pro.they`, `$pro.them`, `$pro.their`, `$pro.theirs`, `$pro.themself` with matching verbs `$pro.is`, `$pro.was`, `$pro.has` ("{$pro.they.cap} {$pro.is} ready" gives "She is ready" or "They are ready"); titles `$title.senior` (Senior Brother / Sister / Sibling / Senior), `$title.sect` (Sect Master / Mistress / Head) and `$title.god` (Godking / Godqueen / Godsovereign).

After expanding, the engine tidies spacing ("word ," becomes "word,") and doubled articles ("the the river" becomes "the river"). Lists remember their recent picks (60% of the list, at most 16) and avoid repeating them.

### Layers

- `[key@1]` is used instead of `key` on plane 1 (the Immortal Plane), `@2` on the Divine Plane. Without such a list, plain `key` is used.
- `[key~theme]` is used instead of `key` inside an event with that theme (for example `weather~demonic`).

## The life log and the Chronicle

The Return summary shows one line per in-game season (15 reading minutes) that passed since the last visit, up to 6, and every line is also added to the bottom of `/.crosspoint/cultivation/chronicle.txt`. The game puts "Spring, age 17." in front of each line, so write one full sentence in the second person ("You ..."), starting with a capital and ending with a full stop, under 170 bytes. In these lines `{$season}` and `{$player.age}` are the season the line describes. Lines never grant anything.

| List | Used for |
| --- | --- |
| `log.body` | Body Tempering, any origin |
| `log.body.farmer`, `log.body.clan`, `log.body.orphan`, `log.body.servant`, `log.body.herbalist` | Body Tempering, that origin only |
| `log.qi` | Qi Condensation |
| `log.any` | every realm; the only list after Qi Condensation for now |
| `log.stage` | the visit gained stages; use `{$player.realm}, {$player.stage}` ("Body Tempering, Layer 4") |
| `log.bottleneck` | the Peak bar just filled |
| `chronicle.birth.farmer` ... `chronicle.birth.herbalist` | first paragraph of each life in the Chronicle |
| `chronicle.awakening` | the Awakening Test, with `$root.glow`, `$root.color` and `$player.element` |
| `chronicle.past` | only when there was reading before the character: `$past.pages`, `$past.books`, `$past.hours` |

```
[log.body.farmer]
You dig an irrigation ditch for the village headman, and he pays you in rice.
```

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
