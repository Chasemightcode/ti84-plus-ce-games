"""eZ80 CPU interpreter (ADL mode) for testing DREAD builds on a PC.

Implements the instructions the CE toolchain's compiler, its runtime
library and DREAD's hand-written assembly use. Unknown opcodes raise, so
gaps show up immediately instead of silently misbehaving.

Timing model (TI-84 Plus CE, approximate): every byte read from RAM,
including opcode fetches, costs 4 cycles; every byte written costs 2;
taken branches, calls and returns add 1; MLT adds 4; LDIR/LDDR add 1 per
byte. Real hardware also loses some bandwidth to LCD refresh, so treat
results as estimates.
"""

FS, FZ, FH, FPV, FN, FC = 0x80, 0x40, 0x10, 0x04, 0x02, 0x01
M24 = 0xFFFFFF
PARITY = [0] * 256
for _i in range(256):
    PARITY[_i] = FPV if bin(_i).count("1") % 2 == 0 else 0
SZP = [(_i & 0x80) | (FZ if _i == 0 else 0) | PARITY[_i] for _i in range(256)]


class Unimplemented(Exception):
    pass


class CPU:
    READ_CYCLES = 4
    WRITE_CYCLES = 2

    def __init__(self, bus):
        self.bus = bus
        self.a = self.f = 0
        self.bc = self.de = self.hl = 0
        self.ix = self.iy = 0
        self.sp = 0
        self.pc = 0
        self.af_ = self.bc_ = self.de_ = self.hl_ = 0
        self.iff = False
        self.mb = 0xD0
        self.cycles = 0
        self.traps = {}          # pc -> callable(cpu); runs instead of code there
        self.instructions = 0

    # ---------------------------------------------------------- memory
    def rb(self, addr):
        self.cycles += 4
        return self.bus.read(addr & M24)

    def wb(self, addr, v):
        self.cycles += 2
        self.bus.write(addr & M24, v & 0xFF)

    def r24(self, addr):
        return self.rb(addr) | (self.rb(addr + 1) << 8) | (self.rb(addr + 2) << 16)

    def w24(self, addr, v):
        self.wb(addr, v)
        self.wb(addr + 1, v >> 8)
        self.wb(addr + 2, v >> 16)

    def r16(self, addr):
        return self.rb(addr) | (self.rb(addr + 1) << 8)

    def fetch(self):
        v = self.rb(self.pc)
        self.pc = (self.pc + 1) & M24
        return v

    def fetchd(self):
        v = self.fetch()
        return v - 256 if v & 0x80 else v

    def fetch24(self):
        v = self.fetch()
        v |= self.fetch() << 8
        v |= self.fetch() << 16
        return v

    def fetch16(self):
        v = self.fetch()
        return v | (self.fetch() << 8)

    def push(self, v):
        self.sp = (self.sp - 3) & M24
        self.w24(self.sp, v)

    def pop(self):
        v = self.r24(self.sp)
        self.sp = (self.sp + 3) & M24
        return v

    # -------------------------------------------------------- registers
    def get8(self, r, idx=None, addr=None):
        """r: 0 B,1 C,2 D,3 E,4 H,5 L,6 (HL)/(IX+d),7 A."""
        if r == 7:
            return self.a
        if r == 0:
            return (self.bc >> 8) & 0xFF
        if r == 1:
            return self.bc & 0xFF
        if r == 2:
            return (self.de >> 8) & 0xFF
        if r == 3:
            return self.de & 0xFF
        if r == 4:
            if idx == "ix":
                return (self.ix >> 8) & 0xFF
            if idx == "iy":
                return (self.iy >> 8) & 0xFF
            return (self.hl >> 8) & 0xFF
        if r == 5:
            if idx == "ix":
                return self.ix & 0xFF
            if idx == "iy":
                return self.iy & 0xFF
            return self.hl & 0xFF
        return self.rb(self.hl if addr is None else addr)

    def set8(self, r, v, idx=None, addr=None):
        v &= 0xFF
        if r == 7:
            self.a = v
        elif r == 0:
            self.bc = (self.bc & 0xFF00FF) | (v << 8)
        elif r == 1:
            self.bc = (self.bc & 0xFFFF00) | v
        elif r == 2:
            self.de = (self.de & 0xFF00FF) | (v << 8)
        elif r == 3:
            self.de = (self.de & 0xFFFF00) | v
        elif r == 4:
            if idx == "ix":
                self.ix = (self.ix & 0xFF00FF) | (v << 8)
            elif idx == "iy":
                self.iy = (self.iy & 0xFF00FF) | (v << 8)
            else:
                self.hl = (self.hl & 0xFF00FF) | (v << 8)
        elif r == 5:
            if idx == "ix":
                self.ix = (self.ix & 0xFFFF00) | v
            elif idx == "iy":
                self.iy = (self.iy & 0xFFFF00) | v
            else:
                self.hl = (self.hl & 0xFFFF00) | v
        else:
            self.wb(self.hl if addr is None else addr, v)

    def get_rp(self, p, idx=None):
        """p: 0 BC, 1 DE, 2 HL/IX/IY, 3 SP."""
        if p == 0:
            return self.bc
        if p == 1:
            return self.de
        if p == 2:
            return self.ix if idx == "ix" else self.iy if idx == "iy" else self.hl
        return self.sp

    def set_rp(self, p, v, idx=None):
        v &= M24
        if p == 0:
            self.bc = v
        elif p == 1:
            self.de = v
        elif p == 2:
            if idx == "ix":
                self.ix = v
            elif idx == "iy":
                self.iy = v
            else:
                self.hl = v
        else:
            self.sp = v

    def get_rp2(self, p, idx=None):
        """push/pop pairs: 0 BC, 1 DE, 2 HL/IX/IY, 3 AF."""
        if p == 3:
            return (self.a << 8) | self.f
        return self.get_rp(p, idx)

    def set_rp2(self, p, v, idx=None):
        if p == 3:
            self.a = (v >> 8) & 0xFF
            self.f = v & 0xFF
        else:
            self.set_rp(p, v, idx)

    # -------------------------------------------------------------- ALU
    def alu(self, op, v):
        a = self.a
        if op == 0 or op == 1:          # add, adc
            c = (self.f & FC) if op == 1 else 0
            r = a + v + c
            self.f = ((r & 0x80) | (FZ if (r & 0xFF) == 0 else 0) | ((a ^ v ^ r) & FH)
                      | (FPV if ((a ^ ~v) & (a ^ r) & 0x80) else 0) | (FC if r > 0xFF else 0))
            self.a = r & 0xFF
        elif op in (2, 3, 7):           # sub, sbc, cp
            c = (self.f & FC) if op == 3 else 0
            r = a - v - c
            self.f = ((r & 0x80) | (FZ if (r & 0xFF) == 0 else 0) | ((a ^ v ^ r) & FH)
                      | (FPV if ((a ^ v) & (a ^ r) & 0x80) else 0) | FN | (FC if r < 0 else 0))
            if op != 7:
                self.a = r & 0xFF
        elif op == 4:
            self.a = a & v
            self.f = SZP[self.a] | FH
        elif op == 5:
            self.a = a ^ v
            self.f = SZP[self.a]
        else:
            self.a = a | v
            self.f = SZP[self.a]

    def inc8(self, v):
        r = (v + 1) & 0xFF
        self.f = (self.f & FC) | (r & 0x80) | (FZ if r == 0 else 0) | (FH if (r & 0x0F) == 0 else 0) | (FPV if r == 0x80 else 0)
        return r

    def dec8(self, v):
        r = (v - 1) & 0xFF
        self.f = (self.f & FC) | (r & 0x80) | (FZ if r == 0 else 0) | (FH if (r & 0x0F) == 0x0F else 0) | (FPV if r == 0x7F else 0) | FN
        return r

    def add24(self, a, b, short=False):
        mask = 0xFFFF if short else M24
        r = (a & mask) + (b & mask)
        hbit = 0x1000 if not short else 0x1000
        self.f = (self.f & (FS | FZ | FPV)) | (FH if ((a ^ b ^ r) & hbit) else 0) | (FC if r > mask else 0)
        return r & mask

    def adc_sbc24(self, a, b, sub, short=False):
        mask, sign = (0xFFFF, 0x8000) if short else (M24, 0x800000)
        a &= mask
        b &= mask
        c = self.f & FC
        if sub:
            r = a - b - c
            ov = (a ^ b) & (a ^ r) & sign
            f = FN | (FC if r < 0 else 0)
        else:
            r = a + b + c
            ov = (a ^ ~b) & (a ^ r) & sign
            f = FC if r > mask else 0
        res = r & mask
        f |= (FS if res & sign else 0) | (FZ if res == 0 else 0) | (FPV if ov else 0)
        f |= FH if ((a ^ b ^ r) & 0x1000) else 0
        self.f = f
        return res

    def rot(self, op, v):
        c = self.f & FC
        if op == 0:     # rlc
            co = v >> 7
            r = ((v << 1) | co) & 0xFF
        elif op == 1:   # rrc
            co = v & 1
            r = (v >> 1) | (co << 7)
        elif op == 2:   # rl
            co = v >> 7
            r = ((v << 1) | c) & 0xFF
        elif op == 3:   # rr
            co = v & 1
            r = (v >> 1) | (c << 7)
        elif op == 4:   # sla
            co = v >> 7
            r = (v << 1) & 0xFF
        elif op == 5:   # sra
            co = v & 1
            r = (v >> 1) | (v & 0x80)
        elif op == 6:   # sll (undocumented on z80; not on ez80)
            raise Unimplemented("sll")
        else:           # srl
            co = v & 1
            r = v >> 1
        self.f = SZP[r] | (FC if co else 0)
        return r

    def cond(self, cc):
        f = self.f
        if cc == 0:
            return not (f & FZ)
        if cc == 1:
            return bool(f & FZ)
        if cc == 2:
            return not (f & FC)
        if cc == 3:
            return bool(f & FC)
        if cc == 4:
            return not (f & FPV)
        if cc == 5:
            return bool(f & FPV)
        if cc == 6:
            return not (f & FS)
        return bool(f & FS)

    # -------------------------------------------------------- execution
    def step(self):
        trap = self.traps.get(self.pc)
        if trap is not None:
            trap(self)
            return
        self.instructions += 1
        op = self.fetch()
        short = False
        self.imm16 = False
        if op in (0x40, 0x49, 0x52, 0x5B):
            # Mode suffixes: .sis/.sil operate on 16-bit data (the upper
            # byte of a written register pair becomes 0); .sis/.lis take a
            # 2-byte immediate. .lil is plain ADL behavior.
            short = op in (0x40, 0x52)
            self.imm16 = op in (0x40, 0x49)
            op = self.fetch()
        if op == 0xDD:
            self.exec_index("ix", short)
        elif op == 0xFD:
            self.exec_index("iy", short)
        elif op == 0xED:
            self.exec_ed(short)
        elif op == 0xCB:
            if short:
                raise Unimplemented(".sis CB")
            self.exec_cb(None, None)
        else:
            self.exec_main(op, None, short)

    def exec_main(self, op, idx, short):
        """Unprefixed opcodes; idx is 'ix'/'iy' when under a DD/FD prefix."""
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1

        if x == 1:
            if op == 0x76:
                raise Unimplemented("halt")
            if idx and (y == 6 or z == 6):
                addr = (self.get_rp(2, idx) + self.fetchd()) & M24
                if y == 6:
                    self.set8(6, self.get8(z), None, addr)
                else:
                    self.set8(y, self.get8(6, None, addr))
            else:
                self.set8(y, self.get8(z, idx), idx)
            return
        if x == 2:
            if idx and z == 6:
                addr = (self.get_rp(2, idx) + self.fetchd()) & M24
                self.alu(y, self.get8(6, None, addr))
            else:
                self.alu(y, self.get8(z, idx))
            return

        if short and op not in (0x01, 0x11, 0x21, 0x31, 0x09, 0x19, 0x29, 0x39,
                                0x03, 0x0B, 0x13, 0x1B, 0x23, 0x2B, 0x33, 0x3B, 0xEB):
            raise Unimplemented(".s suffix on %02X" % op)

        if x == 0:
            if z == 0:
                if y == 0:
                    return
                if y == 1:
                    self.a, self.f, self.af_ = (self.af_ >> 8) & 0xFF, self.af_ & 0xFF, (self.a << 8) | self.f
                    return
                d = self.fetchd()
                if y == 2:
                    b = (((self.bc >> 8) & 0xFF) - 1) & 0xFF
                    self.bc = (self.bc & 0xFF00FF) | (b << 8)
                    if b:
                        self.pc = (self.pc + d) & M24
                        self.cycles += 1
                    return
                if y == 3 or self.cond(y - 4):
                    self.pc = (self.pc + d) & M24
                    self.cycles += 1
                return
            if z == 1:
                if q == 0:
                    v = self.fetch16() if self.imm16 else self.fetch24()
                    self.set_rp(p, v & 0xFFFF if short else v, idx)
                else:
                    self.set_rp(2, self.add24(self.get_rp(2, idx), self.get_rp(p, idx), short), idx)
                return
            if z == 2:
                if p == 0:
                    if q == 0:
                        self.wb(self.bc, self.a)
                    else:
                        self.a = self.rb(self.bc)
                elif p == 1:
                    if q == 0:
                        self.wb(self.de, self.a)
                    else:
                        self.a = self.rb(self.de)
                elif p == 2:
                    nn = self.fetch24()
                    if q == 0:
                        self.w24(nn, self.get_rp(2, idx))
                    else:
                        self.set_rp(2, self.r24(nn), idx)
                else:
                    nn = self.fetch24()
                    if q == 0:
                        self.wb(nn, self.a)
                    else:
                        self.a = self.rb(nn)
                return
            if z == 3:
                v = self.get_rp(p, idx)
                v = v + 1 if q == 0 else v - 1
                self.set_rp(p, v & 0xFFFF if short else v, idx)
                return
            if z == 4 or z == 5:
                if y == 6:
                    addr = (self.get_rp(2, idx) + self.fetchd()) & M24 if idx else self.hl
                    v = self.rb(addr)
                    self.wb(addr, self.inc8(v) if z == 4 else self.dec8(v))
                else:
                    v = self.get8(y, idx)
                    self.set8(y, self.inc8(v) if z == 4 else self.dec8(v), idx)
                return
            if z == 6:
                if y == 6:
                    addr = (self.get_rp(2, idx) + self.fetchd()) & M24 if idx else self.hl
                    self.wb(addr, self.fetch())
                else:
                    self.set8(y, self.fetch(), idx)
                return
            # z == 7
            a = self.a
            if y == 0:      # rlca
                c = a >> 7
                self.a = ((a << 1) | c) & 0xFF
                self.f = (self.f & (FS | FZ | FPV)) | c
            elif y == 1:    # rrca
                c = a & 1
                self.a = (a >> 1) | (c << 7)
                self.f = (self.f & (FS | FZ | FPV)) | c
            elif y == 2:    # rla
                c = a >> 7
                self.a = ((a << 1) | (self.f & FC)) & 0xFF
                self.f = (self.f & (FS | FZ | FPV)) | c
            elif y == 3:    # rra
                c = a & 1
                self.a = (a >> 1) | ((self.f & FC) << 7)
                self.f = (self.f & (FS | FZ | FPV)) | c
            elif y == 4:    # daa
                self.daa()
            elif y == 5:    # cpl
                self.a = a ^ 0xFF
                self.f |= FH | FN
            elif y == 6:    # scf
                self.f = (self.f & (FS | FZ | FPV)) | FC
            else:           # ccf
                c = self.f & FC
                self.f = (self.f & (FS | FZ | FPV)) | (FH if c else 0) | (0 if c else FC)
            return

        # x == 3
        if z == 0:
            if self.cond(y):
                self.pc = self.pop()
                self.cycles += 1
            return
        if z == 1:
            if q == 0:
                self.set_rp2(p, self.pop(), idx)
                return
            if p == 0:
                self.pc = self.pop()
                self.cycles += 1
            elif p == 1:
                self.bc, self.bc_ = self.bc_, self.bc
                self.de, self.de_ = self.de_, self.de
                self.hl, self.hl_ = self.hl_, self.hl
            elif p == 2:
                self.pc = self.get_rp(2, idx)
                self.cycles += 1
            else:
                self.sp = self.get_rp(2, idx)
            return
        if z == 2:
            nn = self.fetch24()
            if self.cond(y):
                self.pc = nn
                self.cycles += 1
            return
        if z == 3:
            if y == 0:
                self.pc = self.fetch24()
                self.cycles += 1
            elif y == 2:
                self.fetch()             # out (n),a: ignored
            elif y == 3:
                self.fetch()
                self.a = 0               # in a,(n)
            elif y == 4:
                v = self.r24(self.sp)
                self.w24(self.sp, self.get_rp(2, idx))
                self.set_rp(2, v, idx)
            elif y == 5:
                if short:       # 16-bit exchange (upper bytes cleared)
                    self.de, self.hl = self.hl & 0xFFFF, self.de & 0xFFFF
                else:
                    self.de, self.hl = self.hl, self.de
            elif y == 6:
                self.iff = False
            elif y == 7:
                self.iff = True
            else:
                raise Unimplemented("op %02X" % op)
            return
        if z == 4:
            nn = self.fetch24()
            if self.cond(y):
                self.push(self.pc)
                self.pc = nn
                self.cycles += 1
            return
        if z == 5:
            if q == 0:
                self.push(self.get_rp2(p, idx))
                return
            if p == 0:
                nn = self.fetch24()
                self.push(self.pc)
                self.pc = nn
                self.cycles += 1
                return
            raise Unimplemented("op %02X" % op)
        if z == 6:
            self.alu(y, self.fetch())
            return
        self.push(self.pc)          # rst
        self.pc = y * 8
        self.cycles += 1

    def daa(self):
        a, f = self.a, self.f
        corr = 0
        c = f & FC
        if (f & FH) or (a & 0x0F) > 9:
            corr |= 0x06
        if c or a > 0x99:
            corr |= 0x60
            c = FC
        if f & FN:
            r = (a - corr) & 0xFF
        else:
            r = (a + corr) & 0xFF
        h = (a ^ r) & FH
        self.a = r
        self.f = SZP[r] | (f & FN) | c | h

    def exec_index(self, idx, short):
        op = self.fetch()
        base = self.get_rp(2, idx)
        other = "iy" if idx == "ix" else "ix"
        # eZ80 24-bit loads/stores relative to the index register.
        if op in (0x07, 0x17, 0x27, 0x37, 0x31, 0x0F, 0x1F, 0x2F, 0x3F, 0x3E):
            if short:
                raise Unimplemented(".sis index load")
            addr = (base + self.fetchd()) & M24
            if op == 0x07:
                self.bc = self.r24(addr)
            elif op == 0x17:
                self.de = self.r24(addr)
            elif op == 0x27:
                self.hl = self.r24(addr)
            elif op == 0x37:
                self.set_rp(2, self.r24(addr), idx)
            elif op == 0x31:
                self.set_rp(2, self.r24(addr), other)
            elif op == 0x0F:
                self.w24(addr, self.bc)
            elif op == 0x1F:
                self.w24(addr, self.de)
            elif op == 0x2F:
                self.w24(addr, self.hl)
            elif op == 0x3F:
                self.w24(addr, self.get_rp(2, idx))
            else:
                self.w24(addr, self.get_rp(2, other))
            return
        if op == 0xCB:
            if short:
                raise Unimplemented(".sis index CB")
            addr = (base + self.fetchd()) & M24
            self.exec_cb(idx, addr)
            return
        if op in (0xDD, 0xFD, 0xED):
            raise Unimplemented("prefix %02X after index" % op)
        self.exec_main(op, idx, short)

    def exec_cb(self, idx, addr):
        op = self.fetch()
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        if idx is not None:
            v = self.rb(addr)
        else:
            v = self.get8(z)
        if x == 0:
            r = self.rot(y, v)
        elif x == 1:
            self.f = (self.f & FC) | FH | (0 if v & (1 << y) else (FZ | FPV)) | (FS if (y == 7 and v & 0x80) else 0)
            return
        elif x == 2:
            r = v & ~(1 << y)
        else:
            r = v | (1 << y)
        if idx is not None:
            self.wb(addr, r)
        else:
            self.set8(z, r)

    def exec_ed(self, short):
        op = self.fetch()
        if short and op not in (0x42, 0x52, 0x62, 0x72, 0x4A, 0x5A, 0x6A, 0x7A):
            raise Unimplemented(".sis ED %02X" % op)
        # LEA / PEA
        lea = {0x02: (0, "ix"), 0x03: (0, "iy"), 0x12: (1, "ix"), 0x13: (1, "iy"),
               0x22: (2, "ix"), 0x23: (2, "iy")}
        if op in lea:
            p, src = lea[op]
            self.set_rp(p, self.get_rp(2, src) + self.fetchd())
            return
        if op in (0x32, 0x33, 0x54, 0x55):
            dst, src = {0x32: ("ix", "ix"), 0x33: ("iy", "iy"), 0x54: ("ix", "iy"), 0x55: ("iy", "ix")}[op]
            self.set_rp(2, self.get_rp(2, src) + self.fetchd(), dst)
            return
        if op in (0x65, 0x66):
            src = "ix" if op == 0x65 else "iy"
            self.push((self.get_rp(2, src) + self.fetchd()) & M24)
            return
        # 24-bit loads through (HL)
        if op in (0x07, 0x17, 0x27, 0x37, 0x31):
            v = self.r24(self.hl)
            {0x07: lambda: setattr(self, "bc", v), 0x17: lambda: setattr(self, "de", v),
             0x27: lambda: setattr(self, "hl", v), 0x37: lambda: setattr(self, "ix", v),
             0x31: lambda: setattr(self, "iy", v)}[op]()
            return
        if op in (0x0F, 0x1F, 0x2F, 0x3F, 0x3E):
            v = {0x0F: self.bc, 0x1F: self.de, 0x2F: self.hl, 0x3F: self.ix, 0x3E: self.iy}[op]
            self.w24(self.hl, v)
            return
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1
        if x == 1:
            if z == 2:
                self.hl = self.adc_sbc24(self.hl, self.get_rp(p), q == 0, short)
                return
            if z == 3:
                nn = self.fetch24()
                if q == 0:
                    self.w24(nn, self.get_rp(p))
                else:
                    self.set_rp(p, self.r24(nn))
                return
            if op == 0x44:
                a = self.a
                self.a = 0
                self.alu(2, a)
                return
            if z == 4 and q == 1:   # mlt rr
                v = self.get_rp(p)
                self.set_rp(p, ((v >> 8) & 0xFF) * (v & 0xFF))
                self.cycles += 4
                return
            if op == 0x47 or op == 0x4F:
                return                   # ld i,a / ld r,a
            if op == 0x57:
                self.a = 0
                self.f = (self.f & FC) | (FPV if self.iff else 0) | FZ
                return
            if op == 0x6D:
                self.mb = self.a
                return
            if op == 0x6E:
                self.a = self.mb
                return
            if op in (0x7D, 0x7E):
                return                   # stmix / rsmix
        if x == 0 and z == 4:            # tst a,r
            v = self.get8(y) if y != 6 else self.rb(self.hl)
            r = self.a & v
            self.f = SZP[r] | FH
            return
        if op == 0x64:                   # tst a,n
            r = self.a & self.fetch()
            self.f = SZP[r] | FH
            return
        if op in (0xA0, 0xA8, 0xB0, 0xB8):
            step = 1 if op in (0xA0, 0xB0) else -1
            repeat = op >= 0xB0
            while True:
                self.wb(self.de, self.rb(self.hl))
                self.hl = (self.hl + step) & M24
                self.de = (self.de + step) & M24
                self.bc = (self.bc - 1) & M24
                if repeat:
                    self.cycles += 1
                if not repeat or self.bc == 0:
                    break
            self.f = (self.f & (FS | FZ | FC)) | (FPV if self.bc else 0)
            return
        if op in (0xA1, 0xA9, 0xB1, 0xB9):  # cpi, cpd, cpir, cpdr
            step = 1 if op in (0xA1, 0xB1) else -1
            repeat = op >= 0xB1
            while True:
                v = self.rb(self.hl)
                r = (self.a - v) & 0xFF
                self.hl = (self.hl + step) & M24
                self.bc = (self.bc - 1) & M24
                if repeat:
                    self.cycles += 1
                if not repeat or self.bc == 0 or r == 0:
                    break
            self.f = ((self.f & FC) | (r & FS) | (FZ if r == 0 else 0) | FN |
                      (FH if (self.a & 0x0F) < (v & 0x0F) else 0) | (FPV if self.bc else 0))
            return
        raise Unimplemented("ED %02X at %06X" % (op, self.pc - 2))
