"""Build everything DREAD needs and collect it in release/.

    python build.py            assets + program + checks
    python build.py --run      also run a short emulator smoke test

Requires the CE C toolchain (CEdev) and Python 3 with Pillow and numpy.
"""

import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
RELEASE = os.path.join(ROOT, "release")


def cedev():
    for d in (os.environ.get("CEDEV"), os.path.join(os.path.expanduser("~"), "CEdev")):
        if d and os.path.isdir(os.path.join(d, "bin")):
            return d
    sys.exit("CEdev not found: set the CEDEV environment variable")


def run(cmd, **kw):
    print(">", " ".join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode != 0:
        sys.exit("failed: " + " ".join(cmd))


def main():
    env = dict(os.environ)
    env["PATH"] = os.path.join(cedev(), "bin") + os.pathsep + env["PATH"]
    run([sys.executable, "tools/build_assets.py"])
    make = os.path.join(cedev(), "bin", "make.exe" if os.name == "nt" else "make")
    run([make, "-s"], env=env)
    run([sys.executable, "tools/check_ix.py", "obj/lto.s"])

    os.makedirs(RELEASE, exist_ok=True)
    for f in glob.glob(os.path.join(RELEASE, "*.8x*")):
        os.remove(f)
    shutil.copy(os.path.join(ROOT, "bin", "DREAD.8xp"), RELEASE)
    for f in sorted(glob.glob(os.path.join(ROOT, "build", "appvars", "*.8xv"))):
        shutil.copy(f, RELEASE)
    print("\nrelease/:")
    for f in sorted(os.listdir(RELEASE)):
        print("  %-14s %6d bytes" % (f, os.path.getsize(os.path.join(RELEASE, f))))

    if "--run" in sys.argv:
        run([sys.executable, "tools/emu/run.py", "3 none, 6 up, 4 right"])


if __name__ == "__main__":
    main()
