"""Scan compiled assembly for functions that clobber the IX frame pointer.

The CE toolchain's compiler was seen (at -O2) emitting code that sets up IX
as the frame pointer and then reuses IX as a general register while still
addressing stack slots through it, which corrupts memory and the stack.
This check fails the build if any function writes IX between its frame
setup ("add ix, sp") and its epilogue.

    python tools/check_ix.py obj/lto.s
"""

import re
import sys

WRITES_IX = re.compile(
    r"^\s+(ld\s+ix[lh]?\s*,|lea\s+ix\s*,|add\s+ix\s*,|adc\s+ix|sbc\s+ix|pop\s+ix\b|"
    r"inc\s+ix[lh]?\b|dec\s+ix[lh]?\b|ex\s+\(sp\)\s*,\s*ix)")


def scan(path):
    bad = []
    func = None
    has_frame = False
    lines = open(path).read().splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^(_\w+):", line)
        if m:
            func, has_frame = m.group(1), False
            continue
        if func is None:
            continue
        s = line.split(";", 1)[0]
        if re.match(r"^\s+add\s+ix\s*,\s*sp", s):
            has_frame = True
            continue
        if not has_frame:
            continue
        if re.match(r"^\s+ld\s+sp\s*,\s*ix", s):
            # Epilogue: frame is torn down; IX pops that follow are fine.
            has_frame = False
            continue
        if re.match(r"^\s+pop\s+ix\b", s):
            nxt = [l.split(";", 1)[0].strip() for l in lines[i + 1:i + 3]]
            if any(n.startswith("ret") or n.startswith("pop\tix") or n.startswith("pop ix") for n in nxt):
                has_frame = False
                continue
        if WRITES_IX.match(s):
            bad.append((func, i + 1, s.strip()))
    return bad


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "obj/lto.s"
    bad = scan(path)
    for func, ln, ins in bad:
        print("%s:%d: %s writes IX while it is the frame pointer: %s" % (path, ln, func, ins))
    if bad:
        sys.exit(1)
    print("IX frame pointer check: OK")


if __name__ == "__main__":
    main()
