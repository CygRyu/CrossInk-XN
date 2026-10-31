# Simulator tests for the game

Runs the real firmware (X3 screen, game on) on a computer, presses buttons from a script, and saves screenshots. Used to check screens and flows that would take hours of reading on the device: reaching a bottleneck, failing and passing a breakthrough, Great Perfection, a long Chronicle.

It uses the [CrossInk simulator](https://github.com/uxjulia/crossink-simulator) but builds it with plain `gcc`/`g++`, so PlatformIO and its registry are not needed. Linux or WSL only.

## Setup (once)

```sh
sudo apt install libsdl2-dev libssl-dev xvfb   # Debian/Ubuntu/WSL
pip install pillow
python3 tools/cultivation/sim/simbuild.py      # from the repo root; about 5 minutes the first time
```

`simbuild.py` copies the working tree to a scratch folder (`/tmp/cultsim/repo`, or `$CULTSIM_WORK/repo`), applies `sim-reading-hook.patch`, fetches the simulator and the libraries `platformio.ini` pins, and builds `/tmp/cultsim/obj/crossink-sim`. Run it again after changing code; only changed files are recompiled, plus every file that uses the game's headers when one of them changed, and every file that uses strings when `english.yaml` changed (a stale object once crashed the simulator in a way that looked like a game bug).

## Running a scenario

```sh
python3 tools/cultivation/sim/simrun.py NAME "STEPS" [READ_MINUTES]
```

- `STEPS`: buttons (`UP`, `DOWN`, `LEFT`, `RIGHT`, `CONFIRM`, `BACK`), `WAIT`, and `SHOT:label` for a screenshot (`/tmp/cultsim/shots/NAME_label.png`).
- `READ_MINUTES`: fake reading added to CrossInk's stats on the fake SD card just before the game opens.
- `CULT_SIM_FINISH_BOOKS="Title:minutes:bucket:percent;..."` (environment): books marked as finished just before the game opens, through the real `CultivationHooks` call. `bucket` is 0-3 (Morning, Afternoon, Evening, Night) or 4 for evenly spread reading; `percent` is where the reader was in the book. A title starting with `-` marks the book unfinished instead.
- The fake SD card (`/tmp/cultsim/sd`) is kept between runs. Delete `sd/.crosspoint/cultivation` for a new character, or `sd` for a never-used device.
- On Home, Cultivation is the 3rd entry until reading stats exist, then the 4th. Opening File Transfer by mistake blocks the run until it times out.

```sh
simrun.py create "DOWN DOWN CONFIRM DOWN CONFIRM DOWN CONFIRM CONFIRM CONFIRM CONFIRM SHOT:cultivator"
simrun.py peak "DOWN DOWN DOWN CONFIRM SHOT:summary CONFIRM SHOT:cultivator CONFIRM SHOT:breakthrough" 240
```

## The reading hook

`sim-reading-hook.patch` adds `simAddReading()` to `CultivationActivity::onEnter()` under `#ifdef SIMULATOR`: it reads `CULT_SIM_READ_MINUTES` and adds that time, a page per 40 seconds and one session to `GlobalReadingStats` (today's weekday, the Evening bucket), then saves; and it marks the books in `CULT_SIM_FINISH_BOOKS` as finished. The game itself never writes CrossInk's stats, so the hook is only ever applied to the scratch copy, never committed to the firmware. If `CultivationActivity.cpp` changes near `onEnter()`, regenerate the patch.

## What the simulator does not show

- Timing and memory of the ESP32-C3 (the simulator has 1 MB of fake heap).
- Real e-ink ghosting and refresh behaviour.
- The device's own clock: the simulator uses the computer's date and time.
