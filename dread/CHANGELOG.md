# Changelog

DREAD was built in five stages; each one was a playable build.

## 1.0 — Stage 5: the episode

- Five levels: Outpost Gate, Coolant Tunnels, Stone Keep, Flesh Works and
  The Warden's Pit, from 40×32 to 60×60 cells.
- The Warden, the final boss: 1200 hp, two tiles tall, three-rocket volleys
  with splash damage, a stomp up close. The episode ends when it dies.
- Title page (New Game, Continue, Best times, Quit), difficulty select
  (Easy, Normal, Hard: monster count, damage taken, ammo), pause menu,
  end-of-map tally (kills, items, secrets, time, best time), victory page.
- Automap on Y=: walls you've seen, floor you've walked, doors in their
  keycard colors, your position and heading.
- Saves in the `DREADSV` AppVar: the run in progress, best times per map and
  difficulty, and which difficulties you've won.
- Dying restarts the map with what you carried into it.
- Monsters line up with doorways they need to open.
- Faster: sprite culling and transforms rewritten, monsters think every
  other tic, game data laid out to avoid slow library calls.

## Stage 4: weapons

- Scattergun, rotary cannon and rocket launcher join the fists and pistol,
  on keys 1–5, each with its own ammo, fire rate and damage.
- Rockets are visible projectiles with splash damage that can hurt you.
- First-person art for every weapon, muzzle flashes, a brief light flash
  when a gun fires, weapons lowering and raising on switch.

## Stage 3: enemies and combat

- Grunt, heavy trooper, imp and brute, with walk, attack, pain, death
  frames and corpses.
- AI: wake on sight or gunfire, chase, open doors, attack at range with
  line-of-sight checks, flinch, die.
- Fists and pistol; hitscan with blood and wall puffs; fireballs you can
  sidestep; armor; red damage flash; death and restart.

## Stage 2: doors, pickups, status bar

- Sliding doors, red/blue/yellow keycard doors, secret walls.
- Billboard sprites for items and decorations; classic pickup rules.
- Full status bar with an animated face; yellow pickup flash; exit switch.

## Stage 1: engine

- Raycaster at 160×100 logical pixels (2×2 blocks) in eZ80 assembly,
  procedural textures, distance shading, collision with wall sliding, fixed
  35 Hz game tics, double buffering.
