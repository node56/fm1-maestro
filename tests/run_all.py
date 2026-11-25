#!/usr/bin/env python3
"""Run the full Maestro regression -- from this repo.

The repo is the source of truth for the tests. This runner:

  1. syncs the repo into the Dinghy build tree (the overlay of
     BUILDING.md): firmware/src/*, firmware/overlay/{platform.c,
     dinghy.c} over the template's copies, lessons_data.c
     regenerated from lessons/, and tests/* over the build tree's
     tests -- so what runs is always the repo's code, never a stale
     build-tree copy;
  2. builds the host binary and the MIDI unit test in the build
     tree;
  3. runs, in order: tests/test_core (the template-free unit
     tests), verify_cleanup, the lessons2c compiler suite (run
     from the repo: it tests tools/lessons2c.py against its
     fixtures, plus a host build of a song-less book), and
     v022..v034 (v032 is the cold-path matrix: every book entry
     booted from a persisted selection; v033 is the accidentals
     suite: a black-key pitch wears its sharp in every plane;
     v034 is the v0.31 coverage: the title band through SELECT,
     HOME-then-kick-off, free KNOB2 stepping, per-score takes,
     the assembly as ordinary playback, and the tempo overlay);
  4. prints a per-suite summary and exits non-zero on any failure.

The build tree is the Dinghy template checkout (SETUP.md):
MAESTRO_BUILD_TREE overrides its location; the default is
~/workspace/maestro-build.

Usage: python3 tests/run_all.py [substring ...]
With arguments, only suites whose name contains one of them run
(the sync and builds still happen).
"""
import glob
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BT = os.environ.get("MAESTRO_BUILD_TREE",
                    os.path.expanduser("~/workspace/maestro-build"))


def die(msg):
    print(f"run_all: {msg}")
    sys.exit(2)


def sync():
    if not (os.path.isfile(os.path.join(BT, "build.sh"))
            and os.path.isfile(os.path.join(BT, "firmware/src/platform.c"))):
        die(f"{BT} does not look like the Dinghy build tree "
            "(set MAESTRO_BUILD_TREE; see SETUP.md)")
    pairs = []
    for src in sorted(glob.glob(os.path.join(REPO, "firmware/src/*"))):
        if os.path.isfile(src):
            pairs.append((src, os.path.join(BT, "firmware/src",
                                            os.path.basename(src))))
    for name in ("platform.c", "dinghy.c"):
        pairs.append((os.path.join(REPO, "firmware/overlay", name),
                      os.path.join(BT, "firmware/src", name)))
    for src in sorted(glob.glob(os.path.join(REPO, "tests/*_verify.py"))
                      + glob.glob(os.path.join(REPO, "tests/verify_cleanup.py"))):
        pairs.append((src, os.path.join(BT, "tests", os.path.basename(src))))
    for name in ("host.c", "midi_test.c", "run_host.sh"):
        pairs.append((os.path.join(REPO, "tests", name),
                      os.path.join(BT, "tests", name)))
    for src in sorted(glob.glob(os.path.join(REPO, "tests/scripts/*.txt"))):
        pairs.append((src, os.path.join(BT, "tests/scripts",
                                        os.path.basename(src))))
    for src, dst in pairs:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools/lessons2c.py"),
                        os.path.join(REPO, "lessons"), "-o",
                        os.path.join(BT, "firmware/src/lessons_data.c")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die(f"lessons2c failed: {r.stderr[-400:]}")
    print(f"run_all: synced repo -> {BT} "
          f"({len(pairs)} files + lessons_data.c regenerated)")


def build():
    os.makedirs(os.path.join(BT, "build/host"), exist_ok=True)
    cmds = [
        ["cc", "-O2", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="dev"',
         "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen",
         "-o", "build/host/dinghy", "firmware/src/dinghy.c"],
        ["cc", "-O2", "-Wall", "-Wextra", "-Ifirmware/src",
         "-o", "build/host/midi_test", "tests/midi_test.c"],
        ["gcc", "-Wall", "-Wextra", "-std=c99",
         "-I", os.path.join(REPO, "firmware/src"),
         "-o", "build/host/test_core", os.path.join(REPO, "tests/test_core.c")],
    ]
    for cmd in cmds:
        r = subprocess.run(cmd, cwd=BT, capture_output=True, text=True)
        if r.returncode != 0:
            die(f"build failed: {' '.join(cmd)}\n{r.stderr[-800:]}")
    print("run_all: host builds ok (dinghy, midi_test, test_core)")


def main():
    filters = sys.argv[1:]
    os.environ["MAESTRO_BUILD_TREE"] = BT   # suites (lessons2c) read it
    sync()
    build()
    suites = [("test_core", [os.path.join(BT, "build/host/test_core")])]
    suites.append(("verify_cleanup",
                   ["python3", os.path.join(BT, "tests/verify_cleanup.py")]))
    suites.append(("lessons2c",
                   [sys.executable, os.path.join(REPO, "tests/lessons2c_test.py")]))
    for n in range(22, 35):
        name = f"v{n:03d}"
        suites.append((name, ["python3", os.path.join(BT, "tests",
                                                      f"{name}_verify.py")]))
    results = []
    for name, cmd in suites:
        if filters and not any(f in name for f in filters):
            continue
        print(f"\n===== {name} =====", flush=True)
        r = subprocess.run(cmd, cwd=BT)
        results.append((name, r.returncode == 0))
    print("\n===== summary =====")
    for name, ok in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
    bad = [n for n, ok in results if not ok]
    if bad:
        print(f"run_all: {len(bad)} suite(s) failed: {', '.join(bad)}")
        sys.exit(1)
    print(f"run_all: all {len(results)} suites passed")


if __name__ == "__main__":
    main()
