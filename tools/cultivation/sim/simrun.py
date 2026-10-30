#!/usr/bin/env python3
"""Play a button script on the simulator built by simbuild.py and save screenshots.

    python3 tools/cultivation/sim/simrun.py NAME "STEPS" [READ_MINUTES]

STEPS is space-separated: a button (UP, DOWN, LEFT, RIGHT, CONFIRM, BACK),
WAIT (one more pause), or SHOT:label for a screenshot, saved as
$CULTSIM_WORK/shots/NAME_label.png. READ_MINUTES adds that much fake reading
to the fake SD card's stats before the game opens (the scratch-only hook).

The fake SD card ($CULTSIM_WORK/sd) persists between runs, like a real card:
delete $CULTSIM_WORK/sd/.crosspoint/cultivation to start a new character, or
all of $CULTSIM_WORK/sd for a device that has never been used.

Home menu: "Cultivation" is the 3rd entry, or the 4th once reading stats exist
("Reading Stats" appears above it). Example, a fresh card:

    simrun.py create "DOWN DOWN CONFIRM DOWN CONFIRM DOWN CONFIRM CONFIRM CONFIRM SHOT:awakening CONFIRM SHOT:home"
    simrun.py visit "DOWN DOWN DOWN CONFIRM SHOT:summary CONFIRM SHOT:cultivator" 20

Needs xvfb-run (Debian/Ubuntu: xvfb) and Pillow.
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

WORK = Path(os.environ.get("CULTSIM_WORK", "/tmp/cultsim"))
BINARY = WORK / "obj" / "crossink-sim"
SD = WORK / "sd"
SHOTS = WORK / "shots"
BOOT_MS = 4500
GAP_MS = 1400  # long enough for the simulated e-ink refresh


def main():
    name, steps = sys.argv[1], sys.argv[2].split()
    read_minutes = sys.argv[3] if len(sys.argv) > 3 else None
    SD.mkdir(parents=True, exist_ok=True)
    SHOTS.mkdir(parents=True, exist_ok=True)

    t = BOOT_MS
    events, shots = [], []
    for step in steps:
        if step == "WAIT":
            t += GAP_MS
        elif step.startswith("SHOT:"):
            t += 300
            shots.append((t, step[5:]))
        else:
            t += GAP_MS
            events.append(f"{t}:{step}")
    t += GAP_MS
    events.append(f"{t}:QUIT")

    env = dict(os.environ)
    env.update({
        "CROSSPOINT_SIM_SD": str(SD),
        "CROSSPOINT_SIM_INPUT_SCRIPT": ";".join(events),
        # The simulator saves BMP whatever the extension; converted below.
        "CROSSPOINT_SIM_SCREENSHOTS": ";".join(f"{at}:{SHOTS}/{name}_{label}.bmp" for at, label in shots),
        "SDL_AUDIODRIVER": "dummy",
    })
    if read_minutes:
        env["CULT_SIM_READ_MINUTES"] = read_minutes
    log = SHOTS / f"{name}.log"
    with open(log, "w") as f:
        try:
            subprocess.run(["xvfb-run", "-a", str(BINARY)], cwd=SD, env=env, stdout=f, stderr=subprocess.STDOUT,
                           timeout=t / 1000 + 60)
        except subprocess.TimeoutExpired:
            # A screen that blocks the loop (File Transfer's network setup) keeps QUIT from arriving.
            print("TIMEOUT: the script probably opened a screen that blocks (check the Home menu position)")
    for _, label in shots:
        bmp = SHOTS / f"{name}_{label}.bmp"
        if bmp.exists():
            Image.open(bmp).convert("L").save(SHOTS / f"{name}_{label}.png")
            bmp.unlink()
            print("shot", SHOTS / f"{name}_{label}.png")
        else:
            print("missing shot", label)
    for line in log.read_text(errors="replace").splitlines():
        if "[CULT]" in line:
            print(line)


if __name__ == "__main__":
    main()
