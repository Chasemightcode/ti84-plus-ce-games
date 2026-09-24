"""Tiny helper for laying out DREAD levels as rectangles.

The map starts as solid wall; rooms are carved out, walls around them are
painted with a theme character, and doors, secrets, things and the start
are placed on top. write() emits the ASCII level format of tools/levels.py.
"""


class Map:
    def __init__(self, w, h, fill="#"):
        self.w, self.h = w, h
        self.g = [[fill] * w for _ in range(h)]

    def carve(self, x0, y0, x1, y1):
        """Open floor over the inclusive rectangle."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                assert 0 < x < self.w - 1 and 0 < y < self.h - 1, (x, y)
                self.g[y][x] = "."

    def fill(self, x0, y0, x1, y1, ch):
        """Solid block (pillars, crates) inside a room."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.g[y][x] = ch

    def paint(self, x0, y0, x1, y1, ch, only="#"):
        """Retexture the walls in a rectangle (cells that are still `only`)."""
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                if self.g[y][x] == only:
                    self.g[y][x] = ch

    def room(self, x0, y0, x1, y1, wall=None):
        """Carve a room and paint its surrounding wall ring."""
        if wall:
            self.paint(x0 - 1, y0 - 1, x1 + 1, y1 + 1, wall)
        self.carve(x0, y0, x1, y1)

    def put(self, x, y, ch):
        cur = self.g[y][x]
        assert cur == ".", "put %r at %d,%d over %r" % (ch, x, y, cur)
        self.g[y][x] = ch

    def door(self, x, y, ch="D"):
        self.g[y][x] = ch

    def text(self):
        return "\n".join("".join(r) for r in self.g)

    def write(self, path, header):
        with open(path, "w", newline="\n") as f:
            f.write(header.rstrip("\n") + "\nmap:\n" + self.text() + "\n")
