# Path of Immortal Reading

**A personal fork of CrossInk that adds a Xianxia cultivation game for your e-reader that grows when you read.** 

<table>
  <tr>
    <td align="center"><img src="./docs/images/game/cultivator.png" alt="The Cultivator screen" width="200" /><br/><em>Your cultivator</em></td>
    <td align="center"><img src="./docs/images/game/map.png" alt="A region's map under fog" width="200" /><br/><em>Exploring a region</em></td>
    <td align="center"><img src="./docs/images/game/rival.png" alt="Meeting your rival on the road" width="200" /><br/><em>Meeting your rival</em></td>
    <td align="center"><img src="./docs/images/game/chronicle.png" alt="A page of the Chronicle" width="200" /><br/><em>The Chronicle</em></td>
  </tr>
</table>

Path of Immortal Reading is a small cultivation game built into the firmware of your e-reader. Every minute you spend reading becomes Qi, Spirit Stones and stamina. Open the game for a minute or two between chapters, collect what your reading earned, make a few choices, and go back to your book.

Nothing happens while the book is closed, and nothing is lost either: no decay, no raids, no penalty for a week away. Time in the game passes only as you read, one in-game year for every hour.

## What's in it

* **Realms and breakthroughs:** rise from Body Tempering through Qi Condensation, Foundation Establishment, Core Formation and beyond, with bottlenecks, grades, Great Perfection, tribulations and Heart Demons.
* **Techniques from your books:** every book you finish becomes a technique scroll, named and graded by how long and how you read it.
* **Exploration:** a region opens with each realm, a 6×6 map under fog with herbs, ruins, caves, merchants, beasts, bandits and a guardian. Regions regrow and reshape over time.
* **Alchemy:** gather ingredients, discover recipes and brew pills while you read.
* **Meridians and Daos:** open acupoints for a faster Qi rate, and find Daos that grow from your reading habits.
* **Daily omens and tasks**, scaled to how much you usually read.
* **Your rival:** a cultivator of your own age who trains every day, plus a ranking of twenty to climb in each realm.
* **Nemeses:** enemies who beat you may come back, stronger and with habits of their own to discover.
* **The Chronicle:** your character's whole life, written as you go, readable as a book in the reader.
* **Fair to every reader:** night or morning, daily or weekends only, fast or slow pages, all earn the same power. Only time spent reading counts, and only on this device.

## Install

1. Download the `.bin` for your device from the [latest release](https://github.com/CygRyu/CrossInk-XN/releases/latest):

|Device|File|
|-|-|
|Xteink X3 or X4|`firmware-x3-x4-cultivation-v0.2.8.bin`|
|Xteink X4 Pro|`firmware-x4-pro-cultivation-v0.2.8.bin`|
|Xteink X4 Classic|`firmware-x4-classic-cultivation-v0.2.8.bin`|
|Seeed Studio Sticky|`firmware-sticky-cultivation-v0.2.8.bin`|

2. Flash it. If your reader already runs CrossInk, copy the file to the SD card and choose **Settings > System > SD Card Firmware Update**. Otherwise see the [command line instructions](./docs/installation.md#command-line).
3. Set the date and time in the settings. The daily systems, your rival and the ranking need them.
4. Open **Cultivation** from the Home menu and create your character. Reading done before that moment does not count; everyone starts at zero.

To see your cultivator whenever the reader sleeps, choose **Cultivator** under **Settings > Display > Sleep Screen**.

Your save lives on the SD card (`/.crosspoint/cultivation/`) and carries over to new versions. To go back to plain CrossInk, flash its firmware again; the save stays on the card.

## Status

A personal project in active development. Only tested on a real **Xteink X3**; the other builds compile and should work, but have not been tried on a device. On readers whose clock drifts in sleep (the X4 family), date-based features may be less reliable. The game's screens are in English only.

## Credits

Built on [CrossInk](https://github.com/uxjulia/CrossInk), itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Both are MIT-licensed; see [LICENSE](./LICENSE). This is an unofficial fork: please report game problems here, not to CrossInk or CrossPoint.

## Support this project

If the game keeps you reading, you can support its development on Ko-fi:

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/cygryu)
