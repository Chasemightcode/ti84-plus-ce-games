"""Stage 5 checks on the real binary: menus, automap, pause, level exit and
tally, saving, Continue, difficulty, and the boss fight.

    python tools/emu/test_campaign.py

1. The engine test map as level 1 on Normal: automap (Y=), pause menu
   (ENTER: restart map, resume), the exit switch -> tally -> DREADSV saved
   -> level 2 of the campaign -> Clear quits and archives the save.
2. That save, rewritten to continue at level 5 with a full arsenal:
   Continue -> the Warden's pit -> the boss is drawn two tiles tall -> a
   rocket finishes it -> victory page -> the save records the win.
3. Campaign level 1 on Easy, Normal and Hard: monster counts per skill.

Screenshots go to build/shots/camp_*.png.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from ce import Exit  # noqa: E402
from test_game import (_ID, L_EXITING, L_KILLS_TOTAL, MS_DYING, P_AMMO, P_HEALTH,  # noqa: E402
                       P_WEAPON, T_HP, T_STATE, T_TYPE, T_FLAGS, T_SPRITE, Done, Game,
                       press, start_game)
import levels as LV  # noqa: E402

L_NUMBER, L_TICS, L_BOSS = 30, 37, 41
SV_LEVEL, SV_SKILL, SV_HEALTH, SV_ARMOR, SV_CLASS = 5, 4, 6, 9, 12
SV_WEAPONS, SV_WEAPON, SV_AMMO, SV_BEST, SV_WON = 13, 14, 16, 22, 52
BLACK = 15                               # PAL(GRAY, 15)
TH_BOSS = _ID["TH_BOSS"]
MONSTER_TYPES = {_ID[n] for n in ("TH_GRUNT", "TH_HEAVY", "TH_IMP", "TH_BRUTE", "TH_BOSS")}


def run(g, scenario):
    """Drives main with the scenario generator (one key state per frame)."""
    steps = scenario(g)

    def advance(mach):
        try:
            mach.set_keys(next(steps))
        except StopIteration:
            raise Done()
    g.m.on_swap = advance
    g.m.set_keys({})
    try:
        g.m.call("_main")
        g.exited = True
    except Done:
        g.exited = False
    except Exit:
        g.exited = True


def shot(g, name):
    g.m.screenshot().save(os.path.join(ROOT, "build", "shots", "camp_%s.png" % name))


def black_share(g):
    """Share of black pixels in the 3D view area of the screen shown."""
    base = g.m.screen_base()
    view = g.mem[base:base + 320 * 200]
    return view.count(BLACK) / len(view)


def wait_mode(g, playing, limit=400):
    for _ in range(limit):
        if g.m.in_game() == playing:
            return
        yield {}


def first_run(g):
    yield from start_game(g)
    yield {}
    g.freeze_monsters()
    print("automap")
    before = black_share(g)
    yield press("yequ")
    yield {}
    yield {}
    after = black_share(g)
    g.check(after > 0.3 and after > before + 0.2, "Y= shows the automap (black %.0f%% -> %.0f%%)"
            % (before * 100, after * 100))
    shot(g, "automap")
    for _ in range(12):                  # walk with the map open
        yield press("up")
    yield press("yequ")
    yield {}
    yield {}
    g.check(black_share(g) < 0.1, "Y= again returns to the view")

    print("pause menu")
    for _ in range(20):
        yield press("up")
    t0 = g.u24("_level", L_TICS)
    yield press("enter")
    for _ in range(8):
        yield {}
    shot(g, "pause")
    g.check(g.u24("_level", L_TICS) - t0 <= 2, "the game stops while paused")
    yield press("down")
    yield {}
    yield press("2nd")                   # RESTART MAP
    yield {}
    yield {}
    g.check(g.u24("_level", L_TICS) < 5, "restart map starts the level over")
    g.freeze_monsters()
    yield press("enter")
    yield {}
    yield press("2nd")                   # RESUME
    for _ in range(4):
        yield {}
    t1 = g.u24("_level", L_TICS)
    for _ in range(4):
        yield {}
    g.check(g.u24("_level", L_TICS) > t1, "resume continues the game")

    print("exit -> tally -> save -> level 2")
    g.place(18.6, 34.5, 180)             # the test map's exit switch at 17,34
    yield {}
    yield press("alpha")
    for _ in range(3):
        yield {}
    g.check(g.u8("_level", L_EXITING) == 1, "exit switch pressed")
    yield from wait_mode(g, False)
    for _ in range(30):                  # let the tally count up
        yield {}
    shot(g, "tally")
    sv = g.m.saved_var("DREADSV")
    g.check(sv is not None and len(sv) == 54, "DREADSV written after the level")
    if sv:
        g.save = bytearray(sv)
        g.check(sv[SV_LEVEL] == 2 and sv[SV_SKILL] == 1, "the save continues at map 2 on Normal")
        best = sv[SV_BEST + 2 * 5] | sv[SV_BEST + 2 * 5 + 1] << 8
        g.check(best > 0, "best time for map 1 on Normal recorded (%d s)" % best)
    yield press("2nd")
    yield {}
    yield press("2nd")
    yield from wait_mode(g, True)
    yield {}
    yield {}
    g.check(g.u8("_level", L_NUMBER) == 2, "map 2 follows")
    g.check(bytes(g.mem[g.sym["_level"]:g.sym["_level"] + 15]).startswith(b"Coolant Tunnels"),
            "map 2 is Coolant Tunnels")
    shot(g, "map2")
    yield press("clear")
    for _ in range(4):
        yield {}


def boss_run(g):
    for _ in range(2):
        yield {}
    yield {}
    yield press("2nd")                   # CONTINUE is selected when there is a save
    yield from wait_mode(g, True)
    yield {}
    print("continue into map 5")
    g.check(g.u8("_level", L_NUMBER) == 5, "Continue starts map 5")
    g.check(g.u8("_level", L_BOSS) == 1, "map 5 is a boss level")
    g.check(g.u8("_player", P_WEAPON) == 4 and g.u24("_player", P_AMMO + 6) == 50,
            "the saved loadout came along")
    bosses = [i for i in g.things() if g.thing(i, T_TYPE) == TH_BOSS]
    g.check(len(bosses) == 1, "the Warden is here")
    if not bosses:
        return
    boss = bosses[0]
    print("the Warden")
    for _ in range(3):
        g.place(29.5, 14.5, 270)
        g.set24("_player", P_HEALTH, 200)
        yield {}
    shot(g, "boss")
    g.set_thing24(boss, T_HP, 40)
    for n in range(80):
        g.place(29.5, 14.5, 270)
        g.set24("_player", P_HEALTH, 200)
        g.aim_at(boss)
        if g.thing(boss, T_STATE) >= MS_DYING:
            break
        yield press("2nd") if n < 3 else {}
    g.check(g.thing(boss, T_STATE) >= MS_DYING, "a rocket finishes the Warden")
    for _ in range(12):
        g.set24("_player", P_HEALTH, 200)
        yield {}
    shot(g, "boss_dying")
    for _ in range(200):
        g.set24("_player", P_HEALTH, 200)
        if not g.m.in_game():
            break
        yield {}
    g.check(not g.m.in_game(), "the episode ends after the Warden's death")
    for _ in range(4):
        yield {}
    shot(g, "victory")
    sv = g.m.saved_var("DREADSV")
    g.check(sv is not None and sv[SV_WON] & 2 and sv[SV_LEVEL] == 0,
            "the save records the win on Normal and ends the run")
    yield press("2nd")
    for _ in range(3):
        yield {}
    yield press("clear")
    for _ in range(4):
        yield {}


def skill_counts(g, skill):
    moves = {0: ("up",), 1: (), 2: ("down",)}[skill]
    yield {}
    yield press("2nd")                   # NEW GAME
    yield {}
    for k in moves:
        yield press(k)
        yield {}
    yield press("2nd")
    yield from wait_mode(g, True)
    yield {}
    n = sum(1 for i in g.things() if g.thing(i, T_TYPE) in MONSTER_TYPES and g.thing(i, T_FLAGS) & 1)
    g.counts = (n, g.u8("_level", L_KILLS_TOTAL))
    raise Done()


def main():
    os.makedirs(os.path.join(ROOT, "build", "shots"), exist_ok=True)
    failures = []
    checks = 0

    print("-- map 1 (engine test map) on Normal")
    g = Game()
    g.save = None
    run(g, first_run)
    g.check(g.exited, "Clear quits DREAD")
    checks += g.checks
    failures += g.failures

    print("-- continue at map 5")
    b = Game(test_level=False)
    if g.save:
        sv = g.save
        sv[SV_LEVEL] = 5
        sv[SV_SKILL] = 1
        sv[SV_HEALTH:SV_HEALTH + 3] = (200).to_bytes(3, "little")
        sv[SV_ARMOR:SV_ARMOR + 3] = (200).to_bytes(3, "little")
        sv[SV_CLASS] = 2
        sv[SV_WEAPONS] = 0x1F
        sv[SV_WEAPON] = 4
        for k, v in enumerate((200, 50, 50)):
            sv[SV_AMMO + 2 * k:SV_AMMO + 2 * k + 2] = v.to_bytes(2, "little")
        b.m.add_appvar("DREADSV", bytes(sv))
    run(b, boss_run)
    checks += b.checks
    failures += b.failures

    print("-- difficulty: monsters on campaign map 1")
    info, _ = LV.compile_level(os.path.join(ROOT, "levels", "e1m1.txt"))
    for skill, name in enumerate(("easy", "normal", "hard")):
        c = Game(test_level=False)
        run(c, lambda gg, s=skill: skill_counts(gg, s))
        n, total = c.counts
        c.check(n == info["stats"][name] and total == n,
                "%s: %d monsters (expected %d), kill total %d" % (name, n, info["stats"][name], total))
        checks += c.checks
        failures += c.failures

    print("%d checks, %d failed" % (checks, len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
