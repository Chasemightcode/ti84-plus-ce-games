"""Scripted gameplay checks on the real binary (stages 2-4).

Drives the actual main loop frame by frame: places the player, presses keys,
and checks doors, locks, pickups, secrets, monsters, combat and the exit
through game state read from memory. Monsters are frozen (state 0xFF, which
the AI ignores) while the stage 2 checks run and woken one at a time for
the combat checks. Screenshots of each check go to build/shots/game_*.png.

    python tools/emu/test_game.py
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from ce import Exit, Machine  # noqa: E402
from run import KEYS  # noqa: E402

# struct offsets (eZ80: int/pointers 3 bytes, no padding)
P_X, P_Y, P_ANGLE, P_HEALTH, P_ARMOR = 0, 3, 6, 10, 13
P_AMMO, P_WEAPONS, P_WEAPON, P_KEYS = 17, 26, 27, 28
L_ITEMS, L_ITEMS_TOTAL, L_SECRETS, L_SECRETS_TOTAL, L_EXITING = 31, 32, 33, 34, 40
L_KILLS, L_KILLS_TOTAL = 35, 36
D_OPEN, D_FLAGS, D_STATE, D_X, D_Y, D_LOCK = 0, 1, 2, 4, 5, 6
# thing_t
T_SIZE = 32
T_X, T_Y, T_TYPE, T_FLAGS, T_SPRITE, T_STATE, T_TICS, T_HP = 0, 3, 6, 7, 8, 9, 10, 11
L_TICS = 37
# generated ids, read from src/gen/things.h so they never drift
def _gen_ids():
    ids = {}
    with open(os.path.join(ROOT, "src", "gen", "things.h")) as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0] == "#define" and p[2].isdigit():
                ids[p[1]] = int(p[2])
    return ids


_ID = _gen_ids()
TH_GRUNT, TH_HEAVY, TH_IMP, TH_BRUTE = _ID["TH_GRUNT"], _ID["TH_HEAVY"], _ID["TH_IMP"], _ID["TH_BRUTE"]
TH_FIREBALL, TH_BOOM, TH_ROCKETFLY = _ID["TH_FIREBALL"], _ID["TH_BOOM"], _ID["TH_ROCKETFLY"]
SPR_GRUNT_DIE0, SPR_GRUNT_CORPSE = _ID["SPR_GRUNT_DIE0"], _ID["SPR_GRUNT_CORPSE"]
SPR_BRUTE_ATTACK = _ID["SPR_BRUTE_ATTACK"]
MS_IDLE, MS_CHASE, MS_DYING, MS_DEAD, FROZEN = 0, 1, 5, 6, 0xFF
MONSTERS = (TH_GRUNT, TH_HEAVY, TH_IMP, TH_BRUTE)


class Done(Exception):
    pass


class Game:
    def __init__(self, test_level=True):
        self.m = Machine(os.path.join(ROOT, "bin", "DREAD.obj"), os.path.join(ROOT, "bin", "DREAD.map"))
        self.m.load_game_vars(ROOT, test_level)
        self.sym = self.m.symbols
        self.mem = self.m.bus.mem
        self.failures = []
        self.checks = 0

    # memory helpers
    def u8(self, sym, off=0):
        return self.mem[self.sym[sym] + off]

    def u24(self, sym, off=0):
        a = self.sym[sym] + off
        return int.from_bytes(self.mem[a:a + 3], "little")

    def s24(self, sym, off=0):
        v = self.u24(sym, off)
        return v - (1 << 24) if v & 0x800000 else v

    def set24(self, sym, off, v):
        a = self.sym[sym] + off
        self.mem[a:a + 3] = (v & 0xFFFFFF).to_bytes(3, "little")

    def place(self, x, y, deg):
        self.set24("_player", P_X, int(x * 256))
        self.set24("_player", P_Y, int(y * 256))
        self.set24("_player", P_ANGLE, int(round(deg / 360 * 1024)) & 1023)

    def door_at(self, x, y):
        n = self.u8("_num_doors")
        for i in range(n):
            if self.u8("_doors", 8 * i + D_X) == x and self.u8("_doors", 8 * i + D_Y) == y:
                return i
        raise KeyError("no door at %d,%d" % (x, y))

    def door(self, i, field):
        return self.u8("_doors", 8 * i + field)

    def check(self, cond, what):
        self.checks += 1
        mark = "ok  " if cond else "FAIL"
        print("  %s %s" % (mark, what))
        if not cond:
            self.failures.append(what)

    # things
    def thing(self, i, off, size=1):
        a = self.sym["_things"] + i * T_SIZE + off
        v = int.from_bytes(self.mem[a:a + size], "little")
        if size == 3 and v & 0x800000:
            v -= 1 << 24
        return v

    def set_thing(self, i, off, v):
        self.mem[self.sym["_things"] + i * T_SIZE + off] = v

    def things(self):
        return range(self.u8("_num_things"))

    def thing_at(self, typ, cx, cy):
        for i in self.things():
            if (self.thing(i, T_TYPE) == typ and self.thing(i, T_X, 3) >> 8 == cx
                    and self.thing(i, T_Y, 3) >> 8 == cy):
                return i
        raise KeyError("no thing %d at %d,%d" % (typ, cx, cy))

    def freeze_monsters(self):
        for i in self.things():
            if self.thing(i, T_TYPE) in MONSTERS and self.thing(i, T_STATE) < MS_DYING:
                self.set_thing(i, T_STATE, FROZEN)

    def aim_at(self, i):
        dx = self.thing(i, T_X, 3) - self.s24("_player", P_X)
        dy = self.thing(i, T_Y, 3) - self.s24("_player", P_Y)
        ang = int(round(math.atan2(dy, dx) / (2 * math.pi) * 1024)) & 1023
        self.set24("_player", P_ANGLE, ang)

    def missiles(self):
        return [i for i in self.things() if self.thing(i, T_FLAGS) & 1
                and self.thing(i, T_TYPE) == TH_FIREBALL]

    def set_thing24(self, i, off, v):
        a = self.sym["_things"] + i * T_SIZE + off
        self.mem[a:a + 3] = (int(v) & 0xFFFFFF).to_bytes(3, "little")

    def switch_to(self, key, weapon):
        """Press a number key and wait for the lower/raise to finish."""
        yield press(key)
        for _ in range(40):              # a shot in progress finishes first
            if self.u8("_player", P_WEAPON) == weapon:
                break
            yield {}
        for _ in range(8):               # then the new weapon comes up
            yield {}
        self.check(self.u8("_player", P_WEAPON) == weapon, "key %s selects weapon %d" % (key, weapon))

    def shot(self, name):
        self.m.screenshot().save(os.path.join(ROOT, "build", "shots", "game_%s.png" % name))


def combat(g):
    """Stage 3: monsters and combat. The player has the combat vest (100
    armor) from the pickup checks."""
    print("monsters")
    g.check(g.u8("_level", L_KILLS_TOTAL) == 20, "level has 20 monsters to kill (%d)"
            % g.u8("_level", L_KILLS_TOTAL))

    print("a grunt wakes on sight (vent corridor, 22,4)")
    gr = g.thing_at(TH_GRUNT, 22, 4)
    g.set_thing(gr, T_STATE, MS_IDLE)
    g.place(17.5, 4.5, 0)
    for _ in range(12):
        yield {}
    g.check(g.thing(gr, T_STATE) not in (MS_IDLE, FROZEN), "grunt woke up")
    g.shot("grunt_awake")

    print("the grunt shoots")
    h0, a0 = g.s24("_player", P_HEALTH), g.s24("_player", P_ARMOR)
    for _ in range(150):
        g.place(17.5, 4.5, 0)       # stand still
        if g.s24("_player", P_HEALTH) < h0:
            break
        yield {}
    h1, a1 = g.s24("_player", P_HEALTH), g.s24("_player", P_ARMOR)
    g.check(h1 < h0, "grunt fire lowers health (%d -> %d)" % (h0, h1))
    g.check(a1 < a0, "the combat vest absorbs part of it (%d -> %d)" % (a0, a1))
    g.check((h0 - h1) >= 2 * (a0 - a1), "vest takes a third at most (%d health, %d armor)"
            % (h0 - h1, a0 - a1))
    g.shot("grunt_hit_player")

    print("killing the grunt with the pistol")
    yield from g.switch_to("2", 1)
    g.place(17.5, 4.5, 0)
    ammo0 = g.u24("_player", P_AMMO)
    kills0 = g.u8("_level", L_KILLS)
    frames_seen = set()
    for n in range(200):
        g.set24("_player", P_HEALTH, 100)
        g.aim_at(gr)
        frames_seen.add(g.thing(gr, T_SPRITE))
        if g.thing(gr, T_STATE) == MS_DEAD:
            break
        if n == 3:
            g.shot("pistol_fire")
        yield press("2nd")
    g.check(g.u24("_player", P_AMMO) < ammo0, "pistol used bullets (%d -> %d)"
            % (ammo0, g.u24("_player", P_AMMO)))
    g.check(g.thing(gr, T_STATE) == MS_DEAD, "grunt died")
    g.check(all(s in frames_seen for s in range(SPR_GRUNT_DIE0, SPR_GRUNT_CORPSE)),
            "death frames played (%s)" % sorted(s for s in frames_seen if s >= SPR_GRUNT_DIE0))
    g.check(g.thing(gr, T_SPRITE) == SPR_GRUNT_CORPSE, "corpse left behind")
    g.check(not g.thing(gr, T_FLAGS) & 2, "corpse is not solid")
    g.check(g.u8("_level", L_KILLS) == kills0 + 1, "kill counted")
    for _ in range(3):
        yield {}
    g.shot("grunt_corpse")

    print("an imp's fireball can be dodged (stone hall, imp at 35,8)")
    imp = g.hall_imp = g.thing_at(TH_IMP, 35, 8)
    g.set24("_player", P_HEALTH, 100)
    g.set_thing(imp, T_STATE, MS_IDLE)
    fb = None
    for _ in range(200):
        g.place(35.5, 2.5, 90)
        m = g.missiles()
        if m:
            fb = m[0]
            break
        yield {}
    g.check(fb is not None, "imp threw a fireball")
    if fb is not None:
        y0 = g.thing(fb, T_Y, 3)
        yield {}
        g.shot("fireball")
        g.check(g.thing(fb, T_Y, 3) < y0, "fireball flies toward the player")
        g.set_thing(imp, T_STATE, FROZEN)
        h0 = g.s24("_player", P_HEALTH)
        for _ in range(14):              # strafe east out of its path
            yield press("alpha", "left")
        g.check(g.s24("_player", P_X) > 36.3 * 256, "player strafed aside (x=%.2f)"
                % (g.s24("_player", P_X) / 256))
        for _ in range(60):
            if not (g.thing(fb, T_FLAGS) & 1 and g.thing(fb, T_TYPE) == TH_FIREBALL):
                break
            yield {}
        g.check(g.thing(fb, T_TYPE) != TH_FIREBALL or not g.thing(fb, T_FLAGS) & 1,
                "fireball hit the wall behind")
        g.check(g.s24("_player", P_HEALTH) == h0, "dodged: no damage")

    print("a grunt opens a door to get at the player (grunt at 38,3, door at 32,5)")
    d = g.door_at(32, 5)
    gr2 = g.thing_at(TH_GRUNT, 38, 3)
    g.set_thing(gr2, T_STATE, MS_CHASE)
    opened = False
    for _ in range(500):
        # in the door's row: the secret door at 32,2 found earlier is open
        # too, and the room behind it is a dead end
        g.place(28.5, 5.5, 0)
        g.set24("_player", P_HEALTH, 100)
        if g.door(d, D_STATE) in (1, 2):
            opened = True
            break
        yield {}
    g.check(opened, "grunt opened the door")
    g.set_thing(gr2, T_STATE, FROZEN)

    print("weapon switch and fists (heavy trooper at 40,11)")
    yield press("1")
    for _ in range(16):
        yield {}
    g.check(g.u8("_player", P_WEAPON) == 0, "key 1 switches to the fists")
    hv = g.thing_at(TH_HEAVY, 40, 11)
    g.set_thing(hv, T_STATE, MS_CHASE)
    hp0 = g.thing(hv, T_HP, 3)
    ammo0 = g.u24("_player", P_AMMO)
    for n in range(120):
        g.place(40.5, 12.45, 270)
        g.set24("_player", P_HEALTH, 100)
        g.aim_at(hv)
        if g.thing(hv, T_HP, 3) < hp0:
            break
        if n == 6:
            g.shot("punch")
        yield press("2nd")
    g.check(g.thing(hv, T_HP, 3) < hp0, "a punch hurts (%d -> %d)" % (hp0, g.thing(hv, T_HP, 3)))
    g.check(g.u24("_player", P_AMMO) == ammo0, "fists use no ammo")
    g.set_thing(hv, T_STATE, FROZEN)
    yield press("2")
    for _ in range(16):
        yield {}
    g.check(g.u8("_player", P_WEAPON) == 1, "key 2 switches back to the pistol")

    yield from weapons(g)

    print("brute melee (hell cave, brute at 30,34)")
    br = g.thing_at(TH_BRUTE, 30, 34)
    g.set24("_player", P_HEALTH, 100)
    g.set24("_player", P_ARMOR, 0)
    g.set_thing(br, T_STATE, MS_IDLE)
    attacked = False
    for _ in range(200):
        g.place(30.5, 36.8, 270)
        if g.thing(br, T_SPRITE) == SPR_BRUTE_ATTACK:
            if not attacked:
                g.shot("brute_attack")
            attacked = True
        if g.s24("_player", P_HEALTH) < 100:
            break
        yield {}
    h = g.s24("_player", P_HEALTH)
    g.check(attacked, "brute swung")
    g.check(15 <= 100 - h <= 35, "brute hit hard up close (-%d)" % (100 - h))

    print("dying and trying again")
    g.set24("_player", P_HEALTH, 1)
    for _ in range(100):
        g.place(30.5, 36.8, 270)
        if g.u8("_player_dead"):
            break
        yield {}
    g.check(g.u8("_player_dead") == 1, "player died")
    g.check(g.s24("_player", P_HEALTH) == 0, "health shows 0")
    for _ in range(4):
        yield {}
    g.shot("dead")
    for _ in range(40):                  # 2nd is ignored for a second
        yield {}
    yield press("2nd")
    yield {}
    yield {}
    g.check(g.u8("_player_dead") == 0 and g.s24("_player", P_HEALTH) == 100,
            "2nd restarts the level")
    g.check(g.u8("_level", L_KILLS) == 0, "kills reset")
    g.freeze_monsters()


def weapons(g):
    """Stage 4: scattergun, rotary cannon, rocket launcher."""
    print("scattergun up close (grunt at 29,6)")
    yield from g.switch_to("3", 2)
    gr = g.thing_at(TH_GRUNT, 29, 6)
    g.set_thing(gr, T_STATE, MS_IDLE)
    shells0 = g.u24("_player", P_AMMO + 3)
    for n in range(60):
        g.place(27.4, 6.5, 0)
        g.set24("_player", P_HEALTH, 100)
        if g.thing(gr, T_STATE) >= MS_DYING:
            break
        if n == 1:
            g.shot("scattergun_fire")
        yield press("2nd")
    used = shells0 - g.u24("_player", P_AMMO + 3)
    g.check(g.thing(gr, T_STATE) >= MS_DYING, "grunt dropped")
    g.check(1 <= used <= 2, "it took %d shell(s)" % used)
    for _ in range(4):
        yield {}

    print("rotary cannon fire rate")
    g.mem[g.sym["_player"] + P_WEAPONS] |= 1 << 3
    g.set24("_player", P_AMMO, 150)
    yield from g.switch_to("4", 3)
    g.place(20.5, 5.5, 180)              # at the corridor's west end wall
    yield {}
    b0, t0 = g.u24("_player", P_AMMO), g.u24("_level", L_TICS)
    frames = set()
    for n in range(40):
        g.place(20.5, 5.5, 180)
        if n == 5:
            g.shot("cannon_fire")
        yield press("2nd")
    used, tics = b0 - g.u24("_player", P_AMMO), g.u24("_level", L_TICS) - t0
    g.check(abs(used - tics / 4) <= 2, "a bullet every 4 tics (%d in %d tics)" % (used, tics))
    for _ in range(4):
        yield {}

    print("rocket launcher (heavy trooper at 40,11, imp beside it)")
    g.mem[g.sym["_player"] + P_WEAPONS] |= 1 << 4
    g.set24("_player", P_AMMO + 6, 10)
    yield from g.switch_to("5", 4)
    hv = g.thing_at(TH_HEAVY, 40, 11)
    imp = g.hall_imp                      # from the fireball check
    # move the imp next to the heavy; clearing its solid flag keeps the
    # blockmap consistent (it only affects collision, not blast damage)
    g.set_thing(imp, T_FLAGS, g.thing(imp, T_FLAGS) & ~2)
    g.set_thing24(imp, T_X, 41.4 * 256)
    g.set_thing24(imp, T_Y, 11.7 * 256)
    g.set_thing(imp, T_STATE, FROZEN)
    hp_imp = g.thing(imp, T_HP, 3)
    g.set_thing(hv, T_STATE, MS_IDLE)
    g.place(40.5, 7.5, 90)
    yield {}
    yield {}
    rockets0 = g.u24("_player", P_AMMO + 6)
    g.set_thing(imp, T_STATE, MS_IDLE)
    yield press("2nd")
    flew = False
    for n in range(40):
        g.place(40.5, 7.5, 90)
        g.set24("_player", P_HEALTH, 100)
        live = [i for i in g.things() if g.thing(i, T_FLAGS) & 1 and g.thing(i, T_TYPE) == TH_ROCKETFLY]
        if live and not flew:
            flew = True
            g.shot("rocket")
        if g.thing(hv, T_STATE) >= MS_DYING:
            break
        yield {}
    g.check(g.u24("_player", P_AMMO + 6) == rockets0 - 1, "one rocket used")
    g.check(flew, "the rocket is a visible projectile")
    g.check(g.thing(hv, T_STATE) >= MS_DYING, "direct hit kills the heavy trooper")
    g.check(g.thing(imp, T_HP, 3) < hp_imp, "the blast hurts the imp beside it (%d -> %d)"
            % (hp_imp, g.thing(imp, T_HP, 3)))
    yield {}
    g.shot("rocket_boom")
    for _ in range(10):
        yield {}

    print("rocket blast hurts the player too")
    g.set24("_player", P_HEALTH, 100)
    g.set24("_player", P_ARMOR, 0)
    g.place(45.4, 2.6, 0)                # facing the stone hall's east wall
    for _ in range(3):
        g.place(45.4, 2.6, 0)
        yield {}
    yield press("2nd")
    for _ in range(6):
        g.place(45.4, 2.6, 0)
        yield {}
    h = g.s24("_player", P_HEALTH)
    g.check(h < 100, "point-blank rocket hurts (-%d)" % (100 - h))
    g.set24("_player", P_HEALTH, 100)
    yield from g.switch_to("2", 1)
    g.freeze_monsters()


def press(*names):
    groups = {}
    for n in names:
        g, bit = KEYS[n]
        groups[g] = groups.get(g, 0) | bit
    return groups


def start_game(g):
    """Title: New Game; difficulty: Normal; then wait for the first frame."""
    n = 0
    while not g.m.in_game():
        yield g.m.menu_keys(n)
        n += 1


def scenario(g):
    """Generator: yields the key state for each frame; runs checks between."""
    yield from start_game(g)
    yield {}
    g.freeze_monsters()
    yield {}

    print("sliding door (start room east door at 14,5)")
    d = g.door_at(14, 5)
    g.check(g.door(d, D_STATE) == 0 and g.door(d, D_OPEN) == 0, "door starts closed")
    g.check(g.door(d, D_FLAGS) & 1, "door between N/S walls is vertical")
    g.place(13.7, 5.5, 0)            # as close as collision allows
    yield press("alpha")
    for _ in range(4):
        yield {}
    g.check(g.door(d, D_STATE) == 1 and 0 < g.door(d, D_OPEN) < 255, "use starts opening it")
    g.shot("door_opening")
    for _ in range(12):
        yield {}
    g.check(g.door(d, D_OPEN) == 255, "door fully open")
    g.shot("door_open")
    for _ in range(30):             # walk until clear of the doorway cell
        if g.s24("_player", P_X) > 15.4 * 256:
            break
        yield press("up")
    g.check(g.s24("_player", P_X) > 15.4 * 256, "walked through the doorway (x=%.2f)"
            % (g.s24("_player", P_X) / 256))
    for _ in range(70):
        yield {}
    g.check(g.door(d, D_STATE) in (3, 0), "door closes again after a few seconds")

    print("locked door without the key (red door at 16,19)")
    d = g.door_at(16, 19)
    g.place(15.7, 19.5, 0)
    yield press("alpha")
    for _ in range(4):
        yield {}
    g.check(g.door(d, D_STATE) == 0, "red door stays closed")
    g.shot("locked")

    print("pickups")
    items0 = g.u8("_level", L_ITEMS)
    g.place(45.3, 12.5, 0)          # on the red keycard at 45,12
    for _ in range(3):
        yield {}
    g.check(g.u8("_player", P_KEYS) & 1, "red keycard picked up")
    g.check(g.u8("_level", L_ITEMS) == items0 + 1, "item counted")
    g.shot("pickup_key")
    g.place(24.5, 19.5, 0)          # scattergun at 24,19
    for _ in range(3):
        yield {}
    g.check(g.u8("_player", P_WEAPONS) & 4, "scattergun added to arms")
    for _ in range(12):             # lower the pistol, raise the new gun
        yield {}
    g.check(g.u8("_player", P_WEAPON) == 2, "switched to the scattergun")
    g.check(g.u24("_player", P_AMMO + 3) == 8, "scattergun came with 8 shells")
    g.place(7.5, 7.0, 90)           # combat vest at 7,7
    for _ in range(3):
        yield {}
    g.check(g.s24("_player", P_ARMOR) == 100, "combat vest gives 100 armor")
    g.place(12.5, 3.5, 0)           # stim at 12,3 with full health: stays
    for _ in range(3):
        yield {}
    g.check(g.s24("_player", P_HEALTH) == 100, "stim ignored at full health")
    g.shot("pickups")

    print("red door with the key")
    d = g.door_at(16, 19)
    g.place(15.7, 19.5, 0)
    yield press("alpha")
    for _ in range(16):
        yield {}
    g.check(g.door(d, D_OPEN) == 255, "red door opens with the red keycard")
    g.place(15.2, 19.5, 0)
    for _ in range(2):
        yield {}
    g.shot("door_red_open")

    print("secret door (flush stone wall at 32,2)")
    d = g.door_at(32, 2)
    s0 = g.u8("_level", L_SECRETS)
    g.check(g.door(d, D_FLAGS) & 2, "secret door is flush")
    g.check(g.u8("_level", L_SECRETS_TOTAL) == 2, "level has 2 secrets")
    g.place(33.6, 2.5, 180)
    for _ in range(2):
        yield {}
    g.shot("secret_closed")
    yield press("alpha")
    for _ in range(8):
        yield {}
    g.shot("secret_opening")
    for _ in range(12):
        yield {}
    g.check(g.u8("_level", L_SECRETS) == s0 + 1, "secret counted")
    g.check(g.door(d, D_OPEN) == 255, "secret door open")
    for _ in range(80):
        yield {}
    g.check(g.door(d, D_OPEN) == 255, "secret door stays open")

    yield from combat(g)

    print("exit switch (hell cave west wall at 17,34)")
    g.place(18.6, 34.5, 180)
    yield {}
    g.shot("exit_before")
    yield press("alpha")
    for _ in range(3):
        yield {}
    g.check(g.u8("_level", L_EXITING) == 1, "exit switch starts the level exit")
    g.in_exit = True
    for _ in range(40):
        yield {}
    raise Done()


def main():
    os.makedirs(os.path.join(ROOT, "build", "shots"), exist_ok=True)
    g = Game()
    steps = scenario(g)
    state = {"keys": {}}

    def advance(mach):
        try:
            state["keys"] = next(steps)
        except StopIteration:
            raise Done()
        mach.set_keys(state["keys"])

    g.m.on_swap = advance
    # the intermission waits on kb_Scan in a loop: stop there
    polls = {"n": 0}
    orig_scan = g.m.hle_kb_Scan

    def scan_hook():
        # the tally screen polls the keypad without swapping frames
        if getattr(g, "in_exit", False):
            polls["n"] += 1
            if polls["n"] == 50:
                g.shot("intermission")
                g.check(True, "tally screen shown")
                raise Done()
        return orig_scan()
    g.m.cpu.traps[g.sym["_kb_Scan"]] = g.m._wrap(scan_hook, "_kb_Scan")
    g.m.set_keys({})
    try:
        g.m.call("_main")
    except Done:
        pass
    except Exit:
        pass
    print("%d checks, %d failed" % (g.checks, len(g.failures)))
    sys.exit(1 if g.failures else 0)


if __name__ == "__main__":
    main()
