"""Levels 4 and 5; imported by design_levels.py."""
import os

from mapkit import Map

LEVELS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                      "levels")


def e1m4():
    m = Map(58, 54)
    # blood-pool chamber (start)
    m.room(25, 46, 32, 51, "K")
    m.put(28, 50, "^")
    for x, y, c in ((25, 46, "i"), (32, 46, "i"), (26, 51, "s"), (31, 51, "n"), (28, 47, "d")):
        m.put(x, y, c)
    # great cavern
    m.room(16, 24, 41, 41, "L")
    m.carve(28, 42, 28, 44)
    m.paint(27, 43, 29, 44, "K")
    m.door(28, 45)
    for x0, y0, x1, y1 in ((20, 28, 21, 29), (35, 28, 36, 30), (22, 35, 23, 36), (33, 36, 34, 37),
                           (28, 31, 29, 33)):
        m.fill(x0, y0, x1, y1, "L")
    for x, y, c in ((18, 26, "3"), (39, 26, "3"), (18, 39, "3"), (39, 39, "3"), (28, 27, "4"),
                    (24, 30, "2"), (32, 34, "2"), (26, 39, "1"), (31, 25, "1"), (20, 33, "7"),
                    (37, 33, "7"), (25, 26, "8"), (38, 36, "&"), (17, 30, "&"), (30, 38, "*"),
                    (40, 24, "q"), (16, 41, "m"), (41, 41, "f"), (17, 24, "n"), (27, 36, "z"),
                    (34, 26, "d"), (16, 24, "i"), (41, 24, "i")):
        m.put(x, y, c)
    # west flesh maze: red keycard at its far end
    m.room(2, 24, 12, 41, "F")
    m.carve(13, 32, 14, 32)
    m.door(15, 32)
    m.fill(5, 24, 5, 36, "F")
    m.fill(9, 29, 9, 41, "F")
    for x, y, c in ((3, 25, "k"), (11, 26, "3"), (7, 25, "3"), (7, 39, "4"), (3, 40, "3"),
                    (11, 38, "7"), (3, 30, "&"), (7, 32, "5"), (12, 41, "m"), (2, 38, "e"),
                    (4, 24, "s")):
        m.put(x, y, c)
    # sigil temple behind the red door: blue keycard on the altar
    m.room(18, 4, 39, 17, "G")
    m.paint(27, 19, 29, 22, "K")
    m.carve(28, 18, 28, 22)
    m.door(28, 23, "R")
    for x0, y0, x1, y1 in ((21, 7, 22, 8), (34, 7, 35, 8), (21, 13, 22, 14), (34, 13, 35, 14),
                           (26, 6, 30, 6)):
        m.fill(x0, y0, x1, y1, "K")
    for x, y, c in ((28, 5, "j"), (24, 10, "4"), (32, 10, "4"), (19, 5, "3"), (38, 5, "3"),
                    (19, 16, "3"), (38, 16, "3"), (28, 12, "2"), (25, 15, "8"), (31, 15, "6"),
                    (28, 9, "*"), (20, 11, "&"), (36, 11, "@"), (18, 4, "i"), (39, 4, "i"),
                    (27, 16, "m"), (29, 16, "q"), (38, 10, "f")):
        m.put(x, y, c)
    # bone pits behind the blue door: yellow keycard
    m.room(46, 24, 55, 45, "K")
    m.carve(43, 30, 45, 30)
    m.door(42, 30, "U")
    for x0, y0, x1, y1 in ((48, 28, 49, 29), (52, 33, 53, 34), (48, 38, 49, 39)):
        m.fill(x0, y0, x1, y1, "L")
    for x, y, c in ((54, 44, "y"), (50, 26, "4"), (47, 42, "4"), (54, 27, "3"), (51, 31, "3"),
                    (47, 35, "2"), (54, 38, "2"), (50, 44, "3"), (46, 25, "7"), (53, 41, "8"),
                    (51, 36, "*"), (55, 30, "@"), (46, 45, "l"), (47, 45, "q"), (55, 24, "m"),
                    (50, 34, "z")):
        m.put(x, y, c)
    # gore chamber behind the yellow door: the exit
    m.room(3, 4, 13, 15, "F")
    m.carve(14, 10, 16, 10)
    m.door(17, 10, "Y")
    m.door(8, 3, "X")
    for x, y, c in ((8, 8, "4"), (4, 13, "3"), (12, 13, "3"), (5, 6, "7"), (11, 6, "&"),
                    (3, 15, "m"), (13, 15, "q"), (3, 4, "i"), (13, 4, "i")):
        m.put(x, y, c)
    # secrets: start chamber east wall, cavern south wall, temple east wall
    m.carve(34, 47, 36, 49)
    m.door(33, 48, "$")
    for x, y, c in ((35, 48, "b"), (34, 47, "q"), (36, 49, "e")):
        m.put(x, y, c)
    m.carve(19, 43, 21, 44)
    m.door(20, 42, "%")
    m.put(20, 43, "o")
    m.put(19, 44, "f")
    m.carve(41, 9, 43, 11)
    m.door(40, 10, "+")
    m.put(42, 10, "q")
    m.put(41, 9, "m")
    m.write(os.path.join(LEVELS, "e1m4.txt"), """; DREAD episode 1, map 4: flesh works. Blood pool (start) -> great cavern
; -> west flesh maze (red keycard) -> red door north -> sigil temple (blue
; keycard on the altar) -> blue door east of the cavern -> bone pits (yellow
; keycard) -> yellow door west of the temple -> gore chamber and the exit.
; Secrets in the start chamber's east wall, the cavern's south wall and the
; temple's east wall.
name: Flesh Works
ceiling: rust 9
floor: red 10
light: 1 13
legend:
  $ = secret hell_bone
  % = secret hell_rock
  + = secret hell_sigil""")


def e1m5():
    m = Map(60, 60)
    # ledge (start)
    m.room(26, 53, 33, 57, "L")
    m.put(29, 56, "^")
    for x, y, c in ((26, 53, "i"), (33, 53, "i"), (27, 57, "s"), (32, 57, "c")):
        m.put(x, y, c)
    # supply cave
    m.room(22, 40, 37, 50, "L")
    m.door(29, 52)
    m.carve(29, 51, 29, 51)
    for x0, y0, x1, y1 in ((25, 44, 26, 45), (33, 44, 34, 45)):
        m.fill(x0, y0, x1, y1, "L")
    for x, y, c in ((23, 42, "3"), (36, 42, "3"), (29, 46, "2"), (24, 49, "1"), (35, 49, "1"),
                    (29, 41, "7"), (27, 48, "&"), (31, 43, "6"), (22, 50, "q"), (37, 50, "q"),
                    (37, 40, "n"), (22, 40, "m"), (30, 48, "l"), (29, 44, "a")):
        m.put(x, y, c)
    # west chapel: red keycard
    m.room(4, 36, 15, 48, "F")
    m.paint(16, 43, 20, 45, "K")
    m.carve(16, 44, 20, 44)
    m.door(21, 44)
    for x0, y0, x1, y1 in ((7, 39, 8, 40), (11, 39, 12, 40), (7, 44, 8, 45), (11, 44, 12, 45)):
        m.fill(x0, y0, x1, y1, "K")
    for x, y, c in ((5, 37, "k"), (10, 42, "4"), (5, 47, "4"), (14, 37, "3"), (14, 47, "3"),
                    (9, 37, "8"), (4, 42, "&"), (13, 42, "*"), (4, 48, "m"), (15, 48, "q"),
                    (4, 36, "i"), (15, 36, "i")):
        m.put(x, y, c)
    # east chapel: armor and supplies
    m.room(44, 36, 55, 48, "F")
    m.paint(39, 43, 43, 45, "K")
    m.carve(39, 44, 43, 44)
    m.door(38, 44)
    for x, y, c in ((50, 42, "b"), (54, 37, "o"), (45, 47, "q"), (54, 47, "q"), (46, 38, "2"),
                    (53, 45, "2"), (49, 46, "3"), (51, 38, "4"), (45, 42, "7"), (54, 42, "6"),
                    (48, 44, "@"), (44, 36, "i"), (55, 48, "i")):
        m.put(x, y, c)
    # passage north to the arena gate
    m.door(29, 39)
    m.paint(28, 32, 30, 38, "K")
    m.carve(29, 32, 29, 38)
    m.door(29, 31, "R")
    # the arena
    m.room(6, 3, 53, 30, "G")
    for x0, y0, x1, y1 in ((13, 9, 15, 11), (44, 9, 46, 11), (13, 21, 15, 23), (44, 21, 46, 23),
                           (22, 15, 24, 17), (35, 15, 37, 17)):
        m.fill(x0, y0, x1, y1, "K")
    for x, y, c in ((29, 6, "9"), (8, 5, "3"), (51, 5, "3"), (18, 13, "7"), (41, 13, "7"),
                    (8, 28, "&"), (51, 28, "&"), (29, 12, "&"),
                    (8, 29, "q"), (51, 29, "q"), (20, 29, "m"), (39, 29, "m"), (6, 3, "b"),
                    (53, 3, "q"), (29, 28, "f"), (6, 16, "n"), (53, 16, "n"), (6, 30, "o"),
                    (53, 30, "m"), (19, 4, "i"), (40, 4, "i"), (19, 27, "i"), (40, 27, "i"),
                    (6, 10, "q"), (53, 22, "q")):
        m.put(x, y, c)
    # secrets: the ledge's east wall, the supply cave's west wall
    m.carve(35, 54, 37, 56)
    m.door(34, 55, "$")
    m.put(36, 55, "q")
    m.put(35, 54, "m")
    m.put(37, 56, "e")
    m.carve(18, 40, 20, 42)
    m.door(21, 41, "%")
    m.put(19, 41, "o")
    m.put(18, 40, "f")
    m.write(os.path.join(LEVELS, "e1m5.txt"), """; DREAD episode 1, map 5: the Warden's pit. Ledge (start) -> supply cave
; (rocket launcher, armor) -> west chapel (red keycard) and east chapel
; (heavy plate, orb, rockets) -> north passage -> red gate -> the arena,
; where the Warden waits. Killing the Warden ends the episode. Secrets in
; the ledge's east wall and the supply cave's west wall.
name: The Warden's Pit
ceiling: red 11
floor: rust 10
light: 1 12
boss: yes
legend:
  $ = secret hell_rock
  % = secret hell_rock""")
