# DREAD — developer guide

How DREAD is put together, how to change it, and what this toolchain will
surprise you with. The [README](../README.md) covers installing, playing,
building and the level format.

## The frame

`main.c` runs a fixed 35 Hz game clock (from `clock()`, the C runtime's
32768 Hz hardware timer) independent of the frame rate: each frame reads the
keypad, runs as many game tics as real time demands (at most 4), renders
once and swaps buffers.

One game tic: `player_tic` (movement, collision, use) → `combat_tic` (the
weapon state machine) → `doors_tic` → `things_tic` (monsters, missiles,
effects, pickups) → `hud_tic` → `fx_tic` (screen flashes) → `automap_visit`.

One frame (`render_view` in `render.c`, the rest in `src/raycast.s`):

1. **`rc_cast`** casts one ray per logical column (160) with a grid DDA in
   Q12 fixed point and writes a draw descriptor per column plus the depth
   buffer (`zbuffer`). Door cells branch to a slower path that intersects
   the door slab. Every wall it hits is marked for the automap.
2. **`rc_draw`** fills ceiling/floor rows that no wall covers (3 bytes per
   `push`), then draws every wall column with an unrolled 11-byte-per-pixel
   loop (texture coordinates in the shadow registers).
3. **`sprites_draw`** (C) calls **`rc_gather`** (asm) to transform and cull
   things into camera space, projects and sorts them far to near, trims each
   to its opaque box and to the columns not hidden by walls, and draws them
   with **`rc_sprite`**.
4. **`weapon_draw`** → **`rc_weapon`**: the first-person weapon, RLE frames
   straight from the archive.
5. **`rc_dup`** copies each even screen row to the odd row below (the 3D view
   is rendered at 160×100 and shown as 2×2 blocks).

Then `hud_overlay` (message line, FPS), `hud_draw` (only the status bar parts
that changed) and `fx_apply` (palette tints for flashes).

## Conventions

- **Positions** are Q8: 256 units per map tile. Maps are copied into a
  fixed 64×64 grid so a cell index is `y << 6 | x`; everything outside a
  level is solid wall, so rays and movement never check bounds.
- **Angles**: 1024 per turn, 0 = east, increasing clockwise (map y points
  down). `sin_table` is Q14.
- **Tile bytes** in memory: 0 empty, 1..63 wall (texture + 1), 0x80 | index
  for every door and secret door (index into `doors[]`).
- **Fixed point only.** No floats anywhere in the game.
- `int` is 24 bits on the eZ80 (±8.3M); comments call out worst-case
  magnitudes wherever a product gets close.

## Memory

- **Program**: about 47 KB, copied into user RAM when it runs.
- **Static data (BSS)**: about 51 KB, placed by the toolchain in the OS's
  scratch RAM, not user RAM; about 8 KB of that area is still free.
- **`DREADTMP` arena** (`arena.c`, offsets in `dread.h`): a 25 KB temporary
  AppVar in free user RAM, 256-byte aligned: sprite texel cache (8 slots ×
  1 KB), tinted palettes for the flashes (4 KB), pickups per map cell,
  sound regions and the automap (4 KB each). TI-OS inserts new variables
  after existing ones, so the arena stays put unless a variable before it
  is deleted or resized; `arena_rebind()` re-finds it (saving calls it).
- **VRAM**: GraphX double buffering uses all of it.
- **Assets** stay in archived AppVars. Wall textures are copied to RAM once
  (every wall pixel reads them); sprites are copied into the cache when
  drawn; weapon frames and HUD art are read straight from flash.

## Assets and the build

`python build.py` runs `tools/build_assets.py`, then `make`, then
`tools/check_ix.py`, and copies the results to `release/`.

All art is procedural Python (numpy + Pillow) under `tools/`:

| Module | Makes |
|--------|-------|
| `palette.py` | 16 ramps × 16 shades, unique in 15-bit color |
| `textures.py` | 21 wall textures (32×32) |
| `sprites.py` | items and decorations |
| `enemies.py` | the four creatures, the Warden, projectiles and effects |
| `weapons.py` | first-person weapon frames |
| `hud_art.py` | status bar, digits, faces, key icons |
| `things.py` | the thing table (map chars, kinds, flags) → `src/gen/things.h`, `thingdefs.c` |
| `levels.py` | ASCII level → binary level, plus the reachability check |

convimg turns the PNGs into the `DREADTX/SP/EN/WP` AppVars (`src/gfx/`,
generated), convbin packs the levels, and the build verifies that every
texel survived conversion. `src/gen/` and `src/gfx/` are generated; don't
edit them.

## Adding things

- **An item or decoration**: draw it in `tools/sprites.py`, add a row to
  `THINGS` in `tools/things.py` (map char, sprite, kind, amount, flags,
  message). The C side picks it up from the generated table.
- **A monster**: add its 10 frames in `tools/enemies.py` (`creature_frames`
  or a custom figure like the Warden's), a `THINGS` row with kind
  `MONSTER` and `arg` = its class index, and a row in `classes[]` in
  `src/monsters.c` (hit points, speed, attack type, damage, radius…). Add a
  skill char in `SKILL_CHARS` (`tools/levels.py`) if needed.
- **A weapon**: frames in `tools/weapons.py` (`WEAPON_FRAMES`), a row in
  `weapon_info[]` in `src/combat.c` (ammo, frames, tics, damage, pellets,
  spread, missile), and HUD handling if it needs a new ammo type.
- **A texture**: add it to `tools/textures.py` and a map char in
  `DEFAULT_LEGEND` (`tools/levels.py`).
- **A level**: an ASCII file in `levels/`; `build.py` rejects it if a
  keycard, thing or the exit can't be reached. The game plays
  `DREADL1..DREADL5`; more levels need `NUM_LEVELS` in `dread.h` and room in
  the save format.

`DREADEN` is at 62 KB of the 64 KB AppVar limit; more enemy art needs a
second AppVar.

## Testing without a calculator

`tools/emu/` runs the real `bin/DREAD.obj` on a small eZ80 interpreter
(`ez80.py`). `ce.py` provides a minimal CE: timer, keypad and LCD
registers, VRAM, and Python stand-ins (HLE) for the GraphX, KeypadC and
FileIOC calls DREAD makes, plus the C library routines the toolchain links
to the OS ROM (`strlen`, `memcmp`). It counts cycles with a CE memory model
(RAM reads 4 cycles, writes 2), which is where the FPS estimates come from.

| Script | Checks |
|--------|--------|
| `test_render.py` | every column's depth against a float raycaster, 72 poses |
| `test_sprites.py` | `rc_gather` against float math, and that nothing visible is missed |
| `test_game.py` | doors, locks, pickups, secrets, monsters, combat, all weapons, dying, the exit (61 checks, ~15 min) |
| `test_campaign.py` | menus, automap, pause, tally, saves, Continue, the boss, difficulty (22 checks) |
| `perf_game.py` | frame cost of a live fight; `CALLERS=...` shows who calls a helper |
| `profile_pose.py` | cycles per function for one frame at a pose |
| `screenshots.py` | stages the README screenshots |

The tests walk through the title and difficulty menus like a player. They
load `levels/test_map.txt` as level 1 (one area per wall theme, every
weapon); set `DREAD_LEVEL=2`..`5` to profile a campaign map instead.
`test_game.py` freezes monsters (state 0xFF, which the AI ignores) and wakes
them one at a time so each check is deterministic.

A new GraphX/FileIOC call needs an `hle_*` method in `ce.py`; a new CPU
instruction needs support in `ez80.py` (the harness stops with
"Unimplemented" otherwise).

## Performance

Frame costs on the emulator's model (48 MHz):

| | cycles | FPS |
|-|--------|-----|
| exploring | 1.7–2.0M | 24–28 |
| 12-monster fight, map 4 | ~2.16M | ~22 |
| boss arena, map 5 | ~1.9M | ~25 |

Where it goes: `rc_cast` 700–890K (more in big open areas), `rc_draw`
220–540K (wall pixels), `rc_dup` 234K (fixed), `rc_gather` ~1.3K per thing
within 24 tiles, sprites vary (a view-filling close-up ~550K), game logic
100–400K in fights.

If a real calculator runs slower than 20 FPS, the next steps in order of
payoff: speed up `rc_cast`; move sprite projection (two C divisions per
visible thing) into `rc_gather`; move the hottest C game logic
(`monster_tic`, `thing_at`, `things_tic`, `line_clear`) to assembly. The
FPS counter itself costs ~20K a frame (Mode hides it).

## Things the toolchain will surprise you with

- **Stay at `-Oz`.** With `-O2`, CEdev v15's compiler reused IX while it
  was still the frame pointer, corrupting the stack. `tools/check_ix.py`
  scans every build's `obj/lto.s` for that pattern.
- **`-Oz` C is slow for per-pixel work**: shifts, odd-size multiplies and
  divisions are all library calls. That's why the hot paths are in
  `raycast.s`.
- **Indexing an array of odd-sized structs calls `__imulu`** every time.
  `thing_t`, `monclass_t` and `weapon_t` are padded to 32/16/16 bytes, and
  hot loops walk pointers instead of indexing.
- **Every shift is a library call**, even `x >> 8`. Casting to `uint8_t`
  after `>> 8` compiles to a register move on its own, but LLVM folds it
  back into shifts once the byte feeds more math.
- **`uint16_t` members are 2-byte aligned** and structs holding one are
  rounded up to an even size (`int` and pointers are 1-aligned). Every
  struct the assembly reads has a `_Static_assert` on its layout; keep it
  that way.
- **`.s`-suffixed 16-bit instructions zero the register's upper byte**; the
  compiler itself relies on that (`inc hl` / `dec.sis hl` zero-extends).
- **convimg turns names into C macros.** A convert called `things` became
  `#define things ...` and silently shadowed the game's `things[]` array.
  It also sorts images by file name, so `gen/weapons.h` binds frames by
  name (`WEAPON_FRAME_BIND`).
- **convimg matches colors after rounding to 15/16 bits**, so every palette
  entry is unique in 5-bit space (bit 15 of an entry is green's low bit).
- **`.8xv` layout**: variable entry header at offset 55, then a 2-byte
  length, then the AppVar data, which starts with its own 2-byte size.
- On Windows, call `CEdev\bin\make.exe` by full path from Python; `make
  clean` deletes `bin/`, so built AppVars live in `build/appvars`.
- Use `clock()` for timing; the C runtime owns timer 1.

## Saving

`save.c` keeps a 54-byte `save_t` in the `DREADSV` AppVar: magic `DSV1`,
the run in progress (difficulty, next map, health/armor/weapons/ammo at its
start), best times per map and difficulty, and a bit per difficulty won.
It is written to RAM whenever a map is finished and archived when DREAD
quits, after `gfx_End()` (archiving can make the OS ask to
garbage-collect). Its layout is pinned by a `_Static_assert` because the
tests build saves byte by byte.

## History

DREAD was built in five stages, each playable: the engine, then doors and
pickups, then enemies, then weapons, then the episode. See
[CHANGELOG.md](../CHANGELOG.md).
