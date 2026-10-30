#!/usr/bin/env python3
"""Build the CrossInk X3 simulator with the cultivation game on, without PlatformIO.

For checking the game's screens on a computer (Linux, or WSL on Windows) when
PlatformIO's registry is not reachable: it mirrors [simulator-base] and
[env:simulator-X3] from platformio.ini with plain gcc/g++.

    python3 tools/cultivation/sim/simbuild.py            # from the repo root

Needs: git, gcc/g++, python3, SDL2 and OpenSSL headers (Debian/Ubuntu:
`sudo apt install libsdl2-dev libssl-dev`). Everything else is fetched from
GitHub into the work folder (default /tmp/cultsim, or $CULTSIM_WORK):

    work/repo      a scratch copy of this repo with sim-reading-hook.patch applied
    work/sim       uxjulia/crossink-simulator
    work/libs      ArduinoJson, QRCode, PNGdec, JPEGDEC at the versions platformio.ini pins
    work/obj       objects, failures.txt, and the binary: work/obj/crossink-sim

The hook lets simrun.py add fake reading time (CULT_SIM_READ_MINUTES). It
writes CrossInk's stats on the simulator's fake SD card, so it only ever goes
into the scratch copy, never into the firmware.
"""
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2]  # the repo root
WORK = Path(os.environ.get("CULTSIM_WORK", "/tmp/cultsim"))
REPO = WORK / "repo"
SIM = WORK / "sim"
LIBS = WORK / "libs"
OUT = WORK / "obj"
I18N = WORK / "i18n"

LIB_PINS = [  # name, git URL, ref (platformio.ini [simulator-base] lib_deps)
    ("ArduinoJson", "https://github.com/bblanchon/ArduinoJson.git", "v7.4.2"),
    ("QRCode", "https://github.com/ricmoo/QRCode.git", None),
    ("PNGdec", "https://github.com/bitbank2/PNGdec.git", "1.1.6"),
    ("JPEGDEC", "https://github.com/bitbank2/JPEGDEC.git", "86282979224c8a32fd51e091ed5a35b0c699a52b"),
]
SDK_LIBS = ["ui/FreeInkUI", "assets/Icons", "network/NearbyTransfer"]  # the simulator env's lib_deps
EXCLUDED_SRC = {"network/FirmwareBoardTag.cpp", "network/FirmwareFlasher.cpp", "network/OtaBootSwitch.cpp",
                "network/OtaUpdater.cpp", "platform/skip_efuse_blk_check.c"}
DEFINES = [
    "SIMULATOR", "CROSSPOINT_SIMULATOR_PROJECT_WEBSERVER", 'CROSSINK_VERSION="sim"', "ENABLE_SERIAL_LOG",
    "LOG_LEVEL=2", "EINK_DISPLAY_SINGLE_BUFFER_MODE=1", "XML_GE=0", "XML_CONTEXT_BYTES=1024",
    "MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1", "USE_UTF8_LONG_NAMES=1", "PNG_MAX_BUFFERED_PIXELS=16416",
    "DISABLE_FS_H_WARNING=1", "DESTRUCTOR_CLOSES_FILE=1", "CROSSPOINT_SIM_USE_NATIVE_DECODERS", "memcpy_P=memcpy",
    "CROSSINK_APP_CAP_TOUCH=0", "CROSSINK_APP_CAP_USB_DRIVE=0", 'CROSSINK_FIRMWARE_DEVICE_TYPE="x3-x4"',
    "SIMULATOR_DEVICE_X3", "CROSSINK_CULTIVATION=1",
    # run_simulator.py sets this when the firmware's Logging.cpp defines the serial wrapper.
    "CROSSPOINT_SIMULATOR_PROJECT_SERIAL_IMPL",
]


def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def prepare():
    WORK.mkdir(parents=True, exist_ok=True)
    if not SIM.exists():
        run(["git", "clone", "-q", "--depth", "1", "https://github.com/uxjulia/crossink-simulator.git", str(SIM)])
    LIBS.mkdir(exist_ok=True)
    for name, url, ref in LIB_PINS:
        dest = LIBS / name
        if dest.exists():
            continue
        run(["git", "clone", "-q", url, str(dest)])
        if ref:
            run(["git", "-C", str(dest), "checkout", "-q", ref])

    # Fresh scratch copy of the working tree (sources only), then the hook.
    if REPO.exists():
        shutil.rmtree(REPO)
    ignore = shutil.ignore_patterns(".git", ".pio", "build", "node_modules")
    shutil.copytree(SOURCE, REPO, ignore=ignore, symlinks=True)
    run(["git", "apply", str(HERE / "sim-reading-hook.patch")], cwd=REPO)

    # The generators PlatformIO runs before a simulator build.
    I18N.mkdir(exist_ok=True)
    run([sys.executable, "scripts/gen_i18n.py", "lib/I18n/translations", str(I18N)], cwd=REPO,
        stdout=subprocess.DEVNULL)
    run([sys.executable, "scripts/build_web.py"], cwd=REPO, stdout=subprocess.DEVNULL)
    # scripts/build_scalable_font_assets.py only runs with CROSSINK_SCALABLE_FONTS=1 (not on the X3).


def lib_sources(d: Path):
    """A library's sources the way PlatformIO picks them: build.srcDir, else src/, else the root."""
    src_dir = None
    meta = d / "library.json"
    if meta.exists():
        try:
            src_dir = json.loads(meta.read_text()).get("build", {}).get("srcDir")
        except ValueError:
            pass
    root = d / src_dir if src_dir else (d / "src" if (d / "src").is_dir() else d)
    return [p for p in root.rglob("*") if p.suffix in (".cpp", ".c")
            and not any(part in ("test", "tests", "examples", "tools") for part in p.parts)]


def build():
    sim_src = SIM / "src"
    inc = [sim_src, REPO / "include", REPO / "src"]
    lib_dirs = [d for d in sorted((REPO / "lib").iterdir()) if d.is_dir()
                # HalClockSim reports "no clock"; the simulator's own HalClock gives host time, like an X3's RTC.
                and d.name not in ("hal", "HalClockSim")]
    sdk_dirs = [REPO / "freeink-sdk/libs" / n for n in SDK_LIBS]
    for d in lib_dirs + sdk_dirs:
        inc.append(d)
        inc += [d / sub for sub in ("src", "include") if (d / sub).is_dir()]
    inc += [LIBS / "ArduinoJson/src", LIBS / "QRCode/src", LIBS / "PNGdec/src", LIBS / "JPEGDEC/src", I18N]

    groups = {"firmware": [], "libs": [], "sim": []}
    for p in (REPO / "src").rglob("*"):
        if p.suffix in (".cpp", ".c") and str(p.relative_to(REPO / "src")) not in EXCLUDED_SRC:
            groups["firmware"].append(p)
    groups["firmware"].append(I18N / "I18nStrings.cpp")
    for d in lib_dirs + sdk_dirs + [LIBS / "QRCode", LIBS / "PNGdec", LIBS / "JPEGDEC"]:
        groups["libs"] += lib_sources(d)
    groups["sim"] = [p for p in sim_src.rglob("*") if p.suffix in (".cpp", ".c")]
    sources = groups["firmware"] + groups["libs"] + groups["sim"]

    sdl_cflags = subprocess.run(["sdl2-config", "--cflags"], capture_output=True, text=True, check=True).stdout.split()
    sdl_libs = subprocess.run(["sdl2-config", "--libs"], capture_output=True, text=True, check=True).stdout.split()
    common = [f"-D{d}" for d in DEFINES] + [f"-I{d}" for d in inc] + sdl_cflags + ["-O1", "-g0", "-w"]
    OUT.mkdir(parents=True, exist_ok=True)

    def compile_one(src: Path):
        obj = OUT / (str(src.relative_to(WORK)).replace("/", "_") + ".o")
        if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime:
            return obj, None
        if src.suffix == ".c":
            cmd = ["gcc", "-std=gnu11"] + common
        else:
            cmd = ["g++", "-std=gnu++2a", "-fpermissive", "-Wno-c++11-narrowing"] + common
        r = subprocess.run(cmd + ["-c", str(src), "-o", str(obj)], capture_output=True, text=True)
        return (obj, None) if r.returncode == 0 else (None, (src, r.stderr))

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(compile_one, sources))
    failures = [e for _, e in results if e is not None]
    print(f"compiled {len(sources) - len(failures)} / {len(sources)}")
    if failures:
        with open(OUT / "failures.txt", "w") as f:
            for src, err in failures:
                f.write(f"===== {src}\n{err[:3000]}\n")
        for src, err in failures[:20]:
            print("FAIL", src, next((l for l in err.splitlines() if "error" in l), "")[:200])
        sys.exit(1)

    # Like PlatformIO: the firmware's objects directly, libraries as static
    # archives, so a member is only pulled in for symbols still missing (the
    # simulator stubs some symbols a library also defines).
    obj_of = {src: o for src, (o, _) in zip(sources, results)}
    archives = []
    for name in ("libs", "sim"):
        archive = OUT / f"lib{name}.a"
        archive.unlink(missing_ok=True)
        run(["ar", "rcs", str(archive)] + [str(obj_of[s]) for s in groups[name]])
        archives.append(str(archive))
    link = (["g++", "-o", str(OUT / "crossink-sim")] + [str(obj_of[s]) for s in groups["firmware"]] +
            ["-Wl,--start-group"] + archives + ["-Wl,--end-group"] + sdl_libs + ["-lssl", "-lcrypto", "-lpthread"])
    r = subprocess.run(link, capture_output=True, text=True)
    if r.returncode != 0:
        (OUT / "link.txt").write_text(r.stderr)
        print("link FAILED; see", OUT / "link.txt")
        sys.exit(1)
    print("built", OUT / "crossink-sim")


if __name__ == "__main__":
    prepare()
    build()
