"""Lays out DREAD's five campaign levels and writes levels/e1m1..e1m5.txt.

    python tools/level_design/design_levels.py

This is how the shipped maps were first drawn: rooms are carved as
rectangles (mapkit.py), walls painted per area, then doors, secrets, things
and the start placed on top. The ASCII files in levels/ are the real source
now; running this again OVERWRITES them, so only do that if you'd rather
edit the layouts here. `python build.py` checks every level either way.

Monster chars: 1-4 every skill (grunt, heavy, imp, brute), 5-8 Normal and
Hard, ! @ & * Hard only, 9 the boss. Secret doors use legend symbols.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mapkit import Map  # noqa: E402
from levels45 import e1m4, e1m5  # noqa: E402

LEVELS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                      "levels")


def e1m1():
    m = Map(40, 32)
    # loading bay (start)
    m.room(2, 22, 11, 29, "M")
    m.put(6, 28, "^")
    m.fill(2, 22, 3, 23, "Q")                       # crates
    m.fill(10, 22, 11, 22, "Q")
    for x, y, c in ((9, 25, "c"), (10, 28, "s"), (3, 28, "w"), (2, 27, "w")):
        m.put(x, y, c)
    # vent corridor north of the bay
    m.room(5, 12, 7, 20, "V")
    m.door(6, 21)
    m.put(5, 19, "1")
    m.put(7, 13, "5")
    # control room
    m.room(2, 3, 12, 10, "C")
    m.door(6, 11)
    for x, y, c in ((2, 3, "t"), (12, 3, "t"), (4, 5, "1"), (10, 5, "1"), (7, 4, "5"),
                    (11, 9, "c"), (3, 9, "s"), (7, 8, "d")):
        m.put(x, y, c)
    # brick hall with stone pillars
    m.room(14, 3, 26, 14, "B")
    m.door(13, 6)
    for x0, y0 in ((17, 6), (22, 6), (17, 10), (22, 10)):
        m.fill(x0, y0, x0 + 1, y0 + 1, "O")
    for x, y, c in ((20, 4, "3"), (25, 13, "3"), (15, 13, "1"), (25, 4, "7"), (20, 13, "!"),
                    (25, 8, "n"), (15, 4, "m"), (20, 9, "z"), (16, 8, "d"), (14, 14, "t"),
                    (26, 14, "t")):
        m.put(x, y, c)
    # red keycard room (lit tech), reached through a short passage
    m.room(29, 2, 37, 9, "I")
    m.carve(27, 8, 27, 8)
    m.door(28, 8)
    for x, y, c in ((35, 3, "k"), (36, 4, "g"), (31, 3, "1"), (36, 8, "5"), (33, 6, "3"),
                    (30, 8, "m"), (31, 8, "e"), (32, 8, "e")):
        m.put(x, y, c)
    # exit room behind the red door
    m.room(20, 18, 30, 27, "M")
    m.carve(24, 15, 24, 16)
    m.door(24, 17, "R")
    m.door(31, 22, "X")
    for x, y, c in ((22, 25, "3"), (28, 20, "3"), (21, 20, "1"), (29, 26, "&"), (26, 23, "5"),
                    (29, 19, "m"), (21, 26, "c"), (20, 18, "w"), (30, 27, "w")):
        m.put(x, y, c)
    # secret 1: closet behind a vent panel in the corridor
    m.carve(1, 15, 3, 17)
    m.door(4, 16, "$")
    m.put(2, 16, "a")
    m.put(1, 15, "e")
    m.put(3, 17, "s")
    # secret 2: under the exit room
    m.carve(24, 29, 26, 30)
    m.door(25, 28, "%")
    m.put(25, 30, "f")
    m.put(24, 29, "m")
    m.put(26, 29, "o")
    m.write(os.path.join(LEVELS, "e1m1.txt"), """; DREAD episode 1, map 1: a small tech outpost. Loading bay -> vent
; corridor -> control room -> brick hall -> red keycard room; the red door
; south of the hall leads to the exit. Two secrets: a vent panel in the
; corridor and a plate in the exit room's south wall.
name: Outpost Gate
ceiling: steel 8
floor: brown 7
light: 0 12
legend:
  $ = secret tech_vent
  % = secret metal_plate""")


def e1m2():
    m = Map(46, 40)
    # lift room (start)
    m.room(20, 32, 25, 37, "M")
    m.put(22, 36, "^")
    for x, y, c in ((20, 32, "w"), (25, 37, "w"), (24, 33, "c"), (21, 37, "s")):
        m.put(x, y, c)
    # east-west service corridor
    m.room(4, 28, 41, 29, "Q")
    m.carve(22, 30, 22, 30)
    m.door(22, 31)
    for x, y, c in ((12, 28, "1"), (33, 29, "1"), (6, 29, "5"), (40, 28, "!"), (17, 29, "c"),
                    (29, 28, "d")):
        m.put(x, y, c)
    # west pump room with hazard columns
    m.room(2, 12, 14, 25, "Q")
    m.carve(8, 26, 8, 26)
    m.door(8, 27)
    for x0, y0 in ((5, 15), (10, 15), (5, 20), (10, 20)):
        m.fill(x0, y0, x0 + 1, y0 + 1, "Z")
    for x, y, c in ((8, 14, "2"), (3, 23, "1"), (13, 23, "1"), (8, 18, "3"), (3, 13, "6"),
                    (13, 13, "5"), (8, 24, "@"), (2, 18, "n"), (14, 18, "m"), (8, 22, "z")):
        m.put(x, y, c)
    # blue keycard alcove north of the pump room
    m.room(2, 6, 6, 9, "V")
    m.carve(4, 10, 4, 10)
    m.door(4, 11)
    for x, y, c in ((4, 7, "j"), (3, 8, "1"), (6, 6, "s"), (2, 6, "t")):
        m.put(x, y, c)
    # coolant hall: tanks in hazard-striped walls
    m.room(17, 12, 30, 24, "Z")
    m.carve(23, 25, 23, 26)
    m.door(23, 27)
    for x0, y0 in ((20, 15), (26, 15), (20, 20), (26, 20)):
        m.fill(x0, y0, x0 + 1, y0 + 1, "M")
    for x, y, c in ((18, 13, "3"), (29, 13, "3"), (23, 18, "2"), (18, 23, "1"), (29, 23, "1"),
                    (23, 13, "7"), (29, 18, "6"), (18, 18, "&"), (28, 24, "n"), (17, 24, "s"),
                    (23, 22, "d")):
        m.put(x, y, c)
    # control deck behind the red door: the exit
    m.room(16, 2, 31, 8, "C")
    m.carve(23, 9, 23, 10)
    m.door(23, 11, "R")
    m.door(23, 1, "X")
    for x, y, c in ((18, 4, "2"), (29, 4, "2"), (23, 6, "3"), (26, 3, "@"), (20, 7, "7"),
                    (16, 2, "t"), (31, 2, "t"), (17, 8, "m"), (30, 8, "c")):
        m.put(x, y, c)
    # east storage behind the blue door: rotary cannon, red keycard
    m.room(34, 10, 43, 24, "M")
    m.carve(38, 25, 38, 26)
    m.door(38, 27, "U")
    for x0, y0, x1, y1 in ((36, 13, 37, 14), (40, 17, 41, 18), (36, 20, 37, 21)):
        m.fill(x0, y0, x1, y1, "Q")
    for x, y, c in ((35, 11, "k"), (42, 11, "u"), (39, 12, "2"), (35, 22, "2"), (42, 22, "1"),
                    (40, 15, "3"), (35, 16, "5"), (42, 12, "6"), (39, 23, "!"), (43, 24, "n"),
                    (34, 24, "m"), (43, 16, "w")):
        m.put(x, y, c)
    # secrets: a plate in the lift room, a pipe panel in the corridor, a
    # hazard panel in the coolant hall
    m.carve(16, 33, 18, 35)
    m.door(19, 34, "$")
    for x, y, c in ((17, 34, "a"), (16, 33, "e"), (18, 35, "e")):
        m.put(x, y, c)
    m.carve(9, 31, 11, 33)
    m.door(10, 30, "%")
    for x, y, c in ((10, 32, "m"), (9, 33, "f"), (11, 31, "c")):
        m.put(x, y, c)
    m.carve(32, 19, 32, 21)
    m.door(31, 20, "+")
    m.put(32, 19, "o")
    m.put(32, 21, "n")
    m.write(os.path.join(LEVELS, "e1m2.txt"), """; DREAD episode 1, map 2: coolant plant. A service corridor links the lift
; (start), the west pump room (blue keycard in the alcove north of it), the
; coolant hall (red door north to the control deck and the exit) and the
; east storage behind the blue door (red keycard, rotary cannon). Three
; secrets: a plate in the lift room, a pipe panel in the corridor's south
; wall, a hazard panel in the coolant hall's east wall.
name: Coolant Tunnels
ceiling: steel 9
floor: gray 9
light: 0 13
legend:
  $ = secret metal_plate
  % = secret metal_pipes
  + = secret metal_hazard""")


def e1m3():
    m = Map(52, 48)
    # gatehouse (start)
    m.room(22, 40, 29, 45, "O")
    m.put(25, 44, "^")
    for x, y, c in ((22, 40, "i"), (29, 40, "i"), (23, 45, "c"), (28, 45, "s")):
        m.put(x, y, c)
    # courtyard around a fountain
    m.room(12, 20, 39, 36, "S")
    m.carve(25, 37, 25, 38)
    m.door(25, 39)
    m.fill(24, 26, 27, 30, "O")
    for x, y, c in ((25, 23, "4"), (14, 22, "3"), (37, 22, "3"), (14, 34, "1"), (37, 34, "1"),
                    (25, 33, "2"), (20, 21, "7"), (31, 35, "5"), (32, 23, "8"), (18, 30, "*"),
                    (33, 30, "@"), (13, 21, "m"), (38, 21, "f"), (13, 35, "n"), (38, 35, "e"),
                    (22, 24, "i"), (29, 24, "i"), (22, 32, "i"), (29, 32, "i"), (17, 26, "z"),
                    (34, 27, "d")):
        m.put(x, y, c)
    # west tower: red keycard
    m.room(2, 22, 8, 34, "B")
    m.carve(9, 28, 10, 28)
    m.door(11, 28)
    for x, y, c in ((3, 23, "k"), (5, 25, "2"), (5, 32, "2"), (7, 23, "3"), (3, 33, "6"),
                    (7, 29, "&"), (7, 33, "f"), (2, 28, "m"), (2, 22, "t"), (8, 34, "t")):
        m.put(x, y, c)
    # east tower behind the red door: blue keycard, rocket launcher
    m.room(43, 22, 49, 34, "B")
    m.carve(41, 28, 42, 28)
    m.door(40, 28, "R")
    for x, y, c in ((48, 23, "j"), (44, 33, "l"), (45, 33, "r"), (46, 33, "r"), (46, 28, "4"),
                    (44, 23, "1"), (48, 33, "1"), (47, 31, "6"), (44, 26, "*"), (49, 34, "m"),
                    (49, 22, "t")):
        m.put(x, y, c)
    # north keep behind the blue door
    m.room(14, 4, 37, 14, "S")
    m.paint(24, 15, 26, 18, "S")
    m.carve(25, 15, 25, 18)
    m.door(25, 19, "U")
    for x0, y0, x1, y1 in ((18, 7, 19, 8), (24, 7, 27, 8), (32, 7, 33, 8), (18, 11, 19, 12),
                           (32, 11, 33, 12)):
        m.fill(x0, y0, x1, y1, "O")
    for x, y, c in ((21, 10, "4"), (30, 10, "4"), (16, 5, "2"), (35, 5, "2"), (25, 5, "3"),
                    (20, 13, "3"), (31, 13, "3"), (25, 11, "8"), (16, 13, "7"), (35, 13, "7"),
                    (22, 5, "@"), (29, 5, "*"), (15, 9, "n"), (36, 9, "f"), (26, 13, "r"),
                    (14, 4, "i"), (37, 4, "i")):
        m.put(x, y, c)
    # keep's back room: yellow keycard
    m.room(22, 1, 29, 2, "O")
    m.door(25, 3)
    for x, y, c in ((23, 1, "y"), (28, 2, "b"), (22, 2, "s")):
        m.put(x, y, c)
    # exit chamber behind the yellow door
    m.room(8, 40, 14, 45, "O")
    m.carve(13, 38, 13, 39)
    m.door(13, 37, "Y")
    m.door(7, 43, "X")
    for x, y, c in ((9, 41, "2"), (12, 44, "3"), (10, 44, "8"), (14, 45, "m"), (8, 40, "i"),
                    (8, 45, "i")):
        m.put(x, y, c)
    # secrets: gatehouse east wall, courtyard north wall, keep west wall
    m.carve(31, 41, 33, 43)
    m.door(30, 42, "$")
    for x, y, c in ((32, 42, "a"), (31, 41, "q"), (33, 43, "e")):
        m.put(x, y, c)
    m.carve(13, 16, 15, 18)
    m.door(14, 19, "%")
    m.put(14, 17, "o")
    m.put(13, 16, "f")
    m.carve(10, 9, 12, 11)
    m.door(13, 10, "+")
    m.put(11, 10, "q")
    m.put(10, 9, "m")
    m.write(os.path.join(LEVELS, "e1m3.txt"), """; DREAD episode 1, map 3: a stone keep. Gatehouse (start) -> courtyard ->
; west tower (red keycard) -> red door -> east tower (blue keycard, rocket
; launcher) -> blue door north -> the keep and its back room (yellow
; keycard) -> yellow door in the courtyard's south-west corner -> exit
; chamber. Secrets in the gatehouse's east wall, the courtyard's north wall
; and the keep's west wall.
name: Stone Keep
ceiling: gray 11
floor: brown 8
light: 1 14
legend:
  $ = secret stone_brown
  % = secret stone_gray
  + = secret stone_gray""")


def main():
    e1m1()
    e1m2()
    e1m3()
    e1m4()
    e1m5()


if __name__ == "__main__":
    main()
