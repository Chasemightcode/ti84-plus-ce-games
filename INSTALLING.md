# Installing the games

These games run on the **TI-84 Plus CE**, **TI-84 Plus CE Python** and
**TI-83 Premium CE**. They do *not* run on the older black-and-white TI-84
Plus or the TI-84 Plus C Silver Edition.

## What you need

| | What | Where |
|-|------|-------|
| 1 | **TI Connect CE** on your computer, to send files | [education.ti.com](https://education.ti.com/en/products/computer-software/ti-connect-ce-sw) |
| 2 | **Cesium**, a program launcher on the calculator | [Cesium releases](https://github.com/mateoconlechuga/cesium/releases) |
| 3 | **arTIfiCE**, only on OS 5.5 or newer (see below) | [arTIfiCE](https://yvantt.github.io/arTIfiCE/) |
| 4 | **clibs.8xg**, the CE C libraries | [`libraries/`](libraries) in this repository, and in every game's zip |
| 5 | the game's files | [`downloads/`](downloads), or the game's `release/` folder |

To see your OS version, press **2nd**, then **+** (MEM), then **1: About**.

## Step by step

1. **Install TI Connect CE** and connect the calculator with its USB cable.
2. **Put Cesium on the calculator** (once): send the Cesium `.8xp` file.
   - On **OS 5.4 or older** you can start it straight from the **PRGM**
     menu.
   - On **OS 5.5 or newer** TI blocked starting assembly programs, so also
     send **arTIfiCE** and start Cesium through it, following the
     arTIfiCE instructions (it uses the CabriJr app that comes with the
     calculator). After a RAM reset or a battery change you may need to do
     this again.
3. **Send the game.** Unzip the game's download and drag **every file** into
   TI Connect CE's calculator explorer. Include `clibs.8xg` the first time;
   every C game shares it.
   The files are marked to go into the **archive** (flash memory), where
   they don't use RAM and survive resets.
4. **Play.** Open Cesium and pick the game: **DREAD** or **SLICE**.

## Troubleshooting

**"Memory insufficient", "ERR: MEMORY" or "DREAD cannot start: needs 25 KB
free RAM".** The calculator has two kinds of memory: the archive (large)
and RAM (about 150 KB). A running game has to fit in RAM: DREAD needs
about **75 KB free**, Neon Slice a little more than its own size (38 KB).
To check, press **2nd**, **+**, **2: Mem Management/Delete**; the top shows
**RAM FREE** and **ARC FREE**.

- Choose **1: All...** in that menu. Anything *without* a **\*** next to it
  is sitting in RAM. Move the cursor to the big ones and press **ENTER** to
  archive them (a **\*** appears). Archived programs still run from Cesium.
- Taking a program *out* of the archive puts it back in RAM, so leave
  games archived.
- If RAM FREE stays low even though everything shows a **\***, something
  hidden is using RAM. A **RAM reset** clears it: **2nd**, **+**, **7:
  Reset...**, **1: All RAM...**, **2: Reset**. It erases everything that is
  *not* archived and resets settings (for example Degree/Radian goes back to
  the default, so check **MODE** before a math test). Archived files, apps
  and the OS are not touched. On OS 5.5+, start Cesium through arTIfiCE
  again afterwards.

**"DREAD cannot start: DREAD… appvar missing" or "…is the wrong version".**
Send all of the game's files again, from the same download. DREAD needs
`DREAD.8xp` plus nine `.8xv` files.

**A message about missing libraries (LibLoad).** Send `clibs.8xg`.

**The game won't show up in Cesium.** Make sure it's a `.8xp` program that
was sent (the `.8xv` files are data, not programs), and that the calculator
is a CE model.

**It feels slow.** DREAD shows its frame rate in the top-right corner
(**Mode** hides it); please report what you see in an issue, with the map
and what was on screen.

## Removing a game

**2nd**, **+**, **2: Mem Management/Delete**, then **Prgm...** for the
program and **AppVars...** for its data: select an item and press **DEL**.
DREAD's files all start with `DREAD` (its save is `DREADSV`); Neon Slice's
save is `NSLICESV`.
