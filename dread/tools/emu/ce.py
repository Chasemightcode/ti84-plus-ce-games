"""Minimal TI-84 Plus CE machine for running DREAD builds on a PC.

Loads the linked ELF (bin/DREAD.obj), runs _main directly on the eZ80
interpreter, and replaces LibLoad library calls (graphx, keypadc, fileioc)
with Python implementations. Hardware used by compiled code directly is
emulated: timer 1 (0xF20000), keypad data (0xF50010) and the LCD
registers/palette (0xE30000). VRAM lives at 0xD40000 like the real thing.

This is a test harness, not an emulator of the OS: no ROM is needed and
nothing outside what DREAD touches is modeled.
"""

import os
import re
import struct
import subprocess

from PIL import Image

import gfx_font
from ez80 import CPU, M24

CPU_HZ = 48_000_000
VRAM = 0xD40000
VRAM_B = VRAM + 320 * 240
LCD_UPBASE = 0xE30010
LCD_LPBASE = 0xE30014      # graphx keeps the draw-buffer pointer here
LCD_PALETTE = 0xE30200
TIMER_BASE = 0xF20000
KEYPAD_DATA = 0xF50010
APPVAR_BASE = 0x0C0000     # "archive": appvar data is placed from here
STACK_TOP = 0xD1A87E
EXIT_TRAP = 0xFFFFF0


class Exit(Exception):
    def __init__(self, value):
        self.value = value


class Bus:
    def __init__(self, machine):
        self.m = machine
        self.mem = bytearray(0x1000000)

    def read(self, addr):
        if addr >= 0xF20000:
            if TIMER_BASE <= addr < TIMER_BASE + 4:
                return (self.m.timer_count() >> (8 * (addr - TIMER_BASE))) & 0xFF
            if KEYPAD_DATA <= addr < KEYPAD_DATA + 16:
                off = addr - KEYPAD_DATA
                return 0 if off & 1 else self.m.keys.get(off >> 1, 0)
        return self.mem[addr]

    def write(self, addr, v):
        if TIMER_BASE <= addr < TIMER_BASE + 4:
            self.m.timer_write(addr - TIMER_BASE, v)
            return
        if addr < 0xD00000:
            raise RuntimeError("write to flash/unmapped %06X (pc %06X)" % (addr, self.m.cpu.pc))
        self.mem[addr] = v


class Machine:
    def __init__(self, elf_path, map_path):
        self.bus = Bus(self)
        self.cpu = CPU(self.bus)
        self.symbols = {}
        self.keys = {}
        self.appvars = {}
        self.handles = {}
        self.timer_base_cycles = 0
        self.timer_base_value = 0
        self.timer_latch = bytearray(4)
        self.frames = 0
        self.frame_cycles = []
        self._last_swap_cycles = 0
        self.on_swap = None          # callback(machine) after each gfx_SwapDraw
        self.on_getcsc = None        # callback(machine) on each os_GetCSC
        self.gfx = {"color": 0, "fg": 0, "bg": 255, "transp": 255, "tx": 0, "ty": 0,
                    "sw": 1, "sh": 1}
        self._font = self._build_font()
        self.load_elf(elf_path)
        self.load_map(map_path)
        self.install_hle()

    # ----------------------------------------------------------- loading
    def load_elf(self, path):
        d = open(path, "rb").read()
        assert d[:4] == b"\x7fELF", "not an ELF file"
        shoff, = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
        for i in range(shnum):
            (name, stype, flags, addr, off, size) = struct.unpack_from("<IIIIII", d, shoff + i * shentsize)
            if flags & 2 and size and addr:      # SHF_ALLOC
                if stype == 8:                   # NOBITS (.bss)
                    self.bus.mem[addr:addr + size] = bytes(size)
                else:
                    self.bus.mem[addr:addr + size] = d[off:off + size]

    def load_map(self, path):
        pat = re.compile(r"^\s+0x([0-9a-f]{8})\s+([_A-Za-z][\w.]*)\s*$")
        for line in open(path):
            m = pat.match(line)
            if m:
                self.symbols[m.group(2)] = int(m.group(1), 16)
        # Absolute symbols (OS entry points such as _os_GetCSC) only show up
        # in the ELF symbol table.
        objdump = os.path.join(os.path.expanduser("~"), "CEdev", "binutils", "bin", "z80-none-elf-objdump.exe")
        elf = os.path.splitext(path)[0] + ".obj"
        if os.path.exists(objdump) and os.path.exists(elf):
            out = subprocess.run([objdump, "-t", elf], capture_output=True, text=True).stdout
            for line in out.splitlines():
                parts = line.split()
                if len(parts) >= 4 and parts[-2] == "00000000" and "*ABS*" in parts:
                    self.symbols.setdefault(parts[-1], int(parts[0], 16))

    def add_8xv(self, path, as_name=None):
        d = open(path, "rb").read()
        pos = 55
        hdr_len = d[pos] | d[pos + 1] << 8
        name = as_name or d[pos + 5:pos + 13].rstrip(b"\0").decode()
        var = d[pos + 2 + hdr_len + 2:]
        size = var[0] | var[1] << 8
        self.add_appvar(name, var[2:2 + size])

    def load_game_vars(self, root, test_level=True):
        """Every release AppVar from build/appvars; with test_level, the
        engine test map from build/testvars replaces level 1. The DREAD_LEVEL
        environment variable (2..5) puts that campaign level in level 1's
        place instead, for profiling the big maps."""
        dirs = [os.path.join(root, "build", "appvars")]
        swap = os.environ.get("DREAD_LEVEL")
        if test_level and not swap:
            dirs.append(os.path.join(root, "build", "testvars"))
        for d in dirs:
            if os.path.isdir(d):
                for name in sorted(os.listdir(d)):
                    if name.endswith(".8xv"):
                        self.add_8xv(os.path.join(d, name))
        if test_level and swap:
            self.add_8xv(os.path.join(root, "build", "appvars", "DREADL%s.8xv" % swap), "DREADL1")

    def add_appvar(self, name, data):
        """Places a variable in the "archive" (below 0xD00000: read-only)."""
        addr = getattr(self, "archive_next", APPVAR_BASE)
        self.archive_next = addr + len(data) + 16
        self.bus.mem[addr:addr + len(data)] = data
        self.appvars[name] = (addr, bytes(data))

    # ---------------------------------------------------------- hardware
    def timer_count(self):
        elapsed = self.cpu.cycles - self.timer_base_cycles
        return (self.timer_base_value + elapsed * 32768 // CPU_HZ) & 0xFFFFFFFF

    def timer_write(self, off, v):
        self.timer_latch[off] = v
        if off == 3:
            self.timer_base_value = int.from_bytes(self.timer_latch, "little")
            self.timer_base_cycles = self.cpu.cycles

    def set_keys(self, groups):
        """groups: {group: bitmask} as read from kb_Data[group]."""
        self.keys = dict(groups)

    # --------------------------------------------------------------- HLE
    def arg(self, n):
        return self.cpu.r24(self.cpu.sp + 3 + 3 * n)

    def arg8(self, n):
        return self.arg(n) & 0xFF

    def sarg(self, n):
        v = self.arg(n)
        return v - 0x1000000 if v & 0x800000 else v

    def cstr(self, addr):
        out = bytearray()
        while self.bus.mem[addr]:
            out.append(self.bus.mem[addr])
            addr += 1
        return out.decode("latin-1")

    def install_hle(self):
        handlers = {}
        for name in dir(self):
            if name.startswith("hle_"):
                handlers["_" + name[4:]] = getattr(self, name)
        for sym, addr in self.symbols.items():
            if sym in handlers:
                self.cpu.traps[addr] = self._wrap(handlers[sym], sym)
            elif re.match(r"^_(gfx|kb|ti|os)_", sym) or (0 < addr < 0x400000 and
                                                          re.match(r"^_[a-z]", sym)):
                # library calls, and C library routines the toolchain links
                # to OS ROM entry points (there is no ROM here)
                self.cpu.traps[addr] = self._missing(sym)
        self.cpu.traps[EXIT_TRAP] = self._exit

    def _wrap(self, fn, sym):
        def trap(cpu):
            r = fn()
            if r is not None:
                cpu.hl = r & M24
                cpu.a = r & 0xFF
            cpu.cycles += 60          # call overhead through LibLoad
            cpu.pc = cpu.pop()
        return trap

    def _missing(self, sym):
        def trap(cpu):
            raise RuntimeError("no HLE implementation for %s" % sym)
        return trap

    def _exit(self, cpu):
        raise Exit(cpu.hl)

    def draw_base(self):
        return int.from_bytes(self.bus.mem[LCD_LPBASE:LCD_LPBASE + 3], "little")

    def screen_base(self):
        return int.from_bytes(self.bus.mem[LCD_UPBASE:LCD_UPBASE + 3], "little")

    def _set24(self, addr, v):
        self.bus.mem[addr:addr + 3] = (v & M24).to_bytes(3, "little")

    # graphx
    def hle_gfx_Begin(self):
        self._set24(LCD_UPBASE, VRAM)
        self._set24(LCD_LPBASE, VRAM)
        self.bus.mem[VRAM:VRAM + 320 * 240 * 2] = bytes(320 * 240 * 2)
        # xlibc-like default palette: 3-3-2 color cube.
        for i in range(256):
            r, g, b = (i >> 5) * 255 // 7, ((i >> 2) & 7) * 255 // 7, (i & 3) * 255 // 3
            self._set_pal(i, r, g, b)
        self.cpu.cycles += 200_000

    def _set_pal(self, i, r, g, b):
        v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | (((g >> 2) & 1) << 15)
        self.bus.mem[LCD_PALETTE + 2 * i:LCD_PALETTE + 2 * i + 2] = v.to_bytes(2, "little")

    def hle_gfx_End(self):
        pass

    def hle_gfx_SetPalette(self):
        src, size, off = self.arg(0), self.arg(1), self.arg8(2)
        dst = LCD_PALETTE + 2 * off
        self.bus.mem[dst:dst + size] = self.bus.mem[src:src + size]
        self.cpu.cycles += size * 7

    def hle_gfx_SetDraw(self):
        loc = self.arg8(0)
        screen = self.screen_base()
        if loc:
            self._set24(LCD_LPBASE, VRAM_B if screen == VRAM else VRAM)
        else:
            self._set24(LCD_LPBASE, screen)

    def hle_gfx_SwapDraw(self):
        screen, draw = self.screen_base(), self.draw_base()
        self._set24(LCD_UPBASE, draw)
        self._set24(LCD_LPBASE, screen)
        self.frames += 1
        self.frame_cycles.append(self.cpu.cycles - self._last_swap_cycles)
        self._last_swap_cycles = self.cpu.cycles
        if self.on_swap:
            self.on_swap(self)

    def hle_gfx_Wait(self):
        pass

    def hle_gfx_SetColor(self):
        old = self.gfx["color"]
        self.gfx["color"] = self.arg8(0)
        return old

    def _fill(self, x, y, w, h, c):
        base = self.draw_base()
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(320, x + w), min(240, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        row = bytes([c]) * (x1 - x0)
        for yy in range(y0, y1):
            a = base + yy * 320 + x0
            self.bus.mem[a:a + len(row)] = row
        self.cpu.cycles += (x1 - x0) * (y1 - y0) * 7 + 200

    def hle_gfx_FillScreen(self):
        self._fill(0, 0, 320, 240, self.arg8(0))

    def hle_gfx_FillRectangle_NoClip(self):
        self._fill(self.arg(0), self.arg8(1), self.arg(2), self.arg8(3), self.gfx["color"])

    def hle_gfx_FillRectangle(self):
        self._fill(self.sarg(0), self.sarg(1), self.sarg(2), self.sarg(3), self.gfx["color"])

    def hle_gfx_Sprite_NoClip(self):
        spr, x, y = self.arg(0), self.arg(1), self.arg8(2)
        w, h = self.bus.mem[spr], self.bus.mem[spr + 1]
        base = self.draw_base()
        for row in range(h):
            src = spr + 2 + row * w
            dst = base + (y + row) * 320 + x
            self.bus.mem[dst:dst + w] = self.bus.mem[src:src + w]
        self.cpu.cycles += w * h * 7 + h * 40 + 150

    def hle_gfx_GetStringWidth(self):
        return sum(gfx_font.SPACING[ord(c) & 0xFF] for c in self.cstr(self.arg(0))) * self.gfx["sw"]

    def hle_gfx_HorizLine_NoClip(self):
        self._fill(self.arg(0), self.arg8(1), self.arg(2), 1, self.gfx["color"])

    def hle_gfx_VertLine_NoClip(self):
        self._fill(self.arg(0), self.arg8(1), 1, self.arg8(2), self.gfx["color"])

    def _text_setter(key):
        def fn(self):
            old = self.gfx[key]
            self.gfx[key] = self.arg8(0)
            return old
        return fn

    hle_gfx_SetTextFGColor = _text_setter("fg")
    hle_gfx_SetTextBGColor = _text_setter("bg")
    hle_gfx_SetTextTransparentColor = _text_setter("transp")

    def hle_gfx_SetTextScale(self):
        self.gfx["sw"], self.gfx["sh"] = self.arg8(0), self.arg8(1)

    def hle_gfx_SetTextXY(self):
        self.gfx["tx"], self.gfx["ty"] = self.sarg(0), self.sarg(1)

    def hle_gfx_PrintStringXY(self):
        self.gfx["tx"], self.gfx["ty"] = self.sarg(1), self.sarg(2)
        self._print(self.cstr(self.arg(0)))

    def hle_gfx_PrintString(self):
        self._print(self.cstr(self.arg(0)))

    def hle_gfx_PrintChar(self):
        self._print(chr(self.arg8(0)))

    def hle_gfx_PrintUInt(self):
        n, length = self.arg(0), self.arg8(1)
        self._print(str(n).rjust(length, "0"))

    def hle_gfx_PrintInt(self):
        n, length = self.sarg(0), self.arg8(1)
        s = str(abs(n)).rjust(length, "0")
        self._print(("-" if n < 0 else "") + s)

    def _build_font(self):
        """GraphX's own 8x8 font: glyph -> set of lit (x, y) pixels."""
        glyphs = {}
        for c in range(256):
            rows = gfx_font.DATA[c * 8:c * 8 + 8]
            glyphs[c] = {(x, y) for y in range(8) for x in range(8) if rows[y] & (0x80 >> x)}
        return glyphs

    def _print(self, s):
        g = self.gfx
        base = self.draw_base()
        for ch in s:
            code = ord(ch) & 0xFF
            lit = self._font[code]
            width = gfx_font.SPACING[code]
            for yy in range(8):
                for xx in range(width):
                    c = g["fg"] if (xx, yy) in lit else g["bg"]
                    if c == g["transp"]:
                        continue
                    for sy in range(g["sh"]):
                        for sx in range(g["sw"]):
                            px = g["tx"] + xx * g["sw"] + sx
                            py = g["ty"] + yy * g["sh"] + sy
                            if 0 <= px < 320 and 0 <= py < 240:
                                self.bus.mem[base + py * 320 + px] = c
            g["tx"] += width * g["sw"]
            self.cpu.cycles += 800 * g["sw"] * g["sh"]

    # OS
    def hle_os_GetCSC(self):
        """Scan code of a pressed key, or 0. Only the keys the scripts use."""
        self.cpu.cycles += 2000
        if self.on_getcsc:
            self.on_getcsc(self)
        codes = {(6, 0x40): 0x0F, (6, 0x01): 0x09, (1, 0x20): 0x36, (2, 0x80): 0x30}
        for (g, bit), code in codes.items():
            if self.keys.get(g, 0) & bit:
                return code
        return 0

    # C library routines that live in the OS ROM
    def hle_strlen(self):
        a = self.arg(0)
        n = 0
        while self.bus.mem[a + n]:
            n += 1
        self.cpu.cycles += 40 + 12 * n
        return n

    def hle_memcmp(self):
        a, b, n = self.arg(0), self.arg(1), self.arg(2)
        self.cpu.cycles += 40 + 20 * n
        for i in range(n):
            d = self.bus.mem[a + i] - self.bus.mem[b + i]
            if d:
                return d
        return 0

    # keypadc
    def hle_kb_Scan(self):
        self.cpu.cycles += 3000

    # fileioc
    RAM_SLOTS = (0xD2C000, 0xD34000)    # free user RAM between the program and VRAM

    def hle_ti_Open(self):
        name = self.cstr(self.arg(0))
        mode = self.cstr(self.arg(1))
        if mode.startswith("w"):
            # created in RAM, empty; every RAM variable gets its own 32 KB slot
            used = {a for n, (a, _) in self.appvars.items() if a >= 0xD00000 and n != name}
            free = [a for a in self.RAM_SLOTS if a not in used]
            if not free:
                return 0
            self.appvars[name] = (free[0], b"")
        if name not in self.appvars:
            return 0
        h = 1
        while h in self.handles:
            h += 1
        self.handles[h] = [name, 0]
        return h

    def _var(self, h):
        return self.handles[h][0]

    def hle_ti_Close(self):
        self.handles.pop(self.arg8(0), None)
        return 1

    def hle_ti_GetSize(self):
        return len(self.appvars[self._var(self.arg8(0))][1])

    def hle_ti_Resize(self):
        size, h = self.arg(0), self.arg8(1)
        name = self._var(h)
        addr, _ = self.appvars[name]
        if addr < 0xD00000 or size > 0x8000:
            return 0
        self.appvars[name] = (addr, bytes(self.bus.mem[addr:addr + size]))
        return size

    def hle_ti_Write(self):
        src, size, count, h = self.arg(0), self.arg(1), self.arg(2), self.arg8(3)
        name, pos = self.handles[h]
        addr, data = self.appvars[name]
        if addr < 0xD00000:
            return 0
        chunk = bytes(self.bus.mem[src:src + size * count])
        data = data[:pos] + chunk + data[pos + len(chunk):]
        self.bus.mem[addr:addr + len(data)] = data
        self.appvars[name] = (addr, data)
        self.handles[h][1] = pos + len(chunk)
        self.cpu.cycles += 2000 + 20 * len(chunk)
        return count

    def hle_ti_SetArchiveStatus(self):
        return 1

    def hle_ti_Delete(self):
        return 1 if self.appvars.pop(self.cstr(self.arg(0)), None) else 0

    def hle_ti_GetDataPtr(self):
        return self.appvars[self._var(self.arg8(0))][0]

    def saved_var(self, name):
        """Contents of a variable the program wrote, or None."""
        v = self.appvars.get(name)
        return None if v is None else v[1]

    # menus: New Game on the title page, then Normal on the difficulty page
    START_KEYS = ({}, {1: 0x20}, {}, {1: 0x20})

    def menu_keys(self, n):
        """Keys for the n-th frame (0-based) spent in the menus."""
        return self.START_KEYS[n] if n < len(self.START_KEYS) else {}

    def in_game(self):
        return self.bus.mem[self.symbols["_game_mode"]] == 1

    # ------------------------------------------------------------ running
    def call(self, sym, max_cycles=None):
        cpu = self.cpu
        cpu.sp = STACK_TOP
        cpu.push(EXIT_TRAP)
        cpu.pc = self.symbols[sym]
        cpu.iy = 0xD00080
        limit = None if max_cycles is None else cpu.cycles + max_cycles
        try:
            while True:
                cpu.step()
                if limit is not None and cpu.cycles > limit:
                    return None
        except Exit as e:
            return e.value

    # ------------------------------------------------------------ output
    def screenshot(self, base=None, indexed=False):
        """The screen (or the buffer at base) as an RGB image, or with
        indexed=True as a palette image (exact colors, small PNGs)."""
        base = self.screen_base() if base is None else base
        pal = []
        for i in range(256):
            v = int.from_bytes(self.bus.mem[LCD_PALETTE + 2 * i:LCD_PALETTE + 2 * i + 2], "little")
            r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
            g = (g << 1) | (v >> 15)
            pal += [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]
        im = Image.frombytes("P", (320, 240), bytes(self.bus.mem[base:base + 320 * 240]))
        im.putpalette(pal)
        return im if indexed else im.convert("RGB")
