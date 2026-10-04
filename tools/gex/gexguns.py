"""Print GoldenEye X's guns as Perfect Dark's code sees them: each weapon's
model file, position, and its fire and reload gun-command scripts with the
animation rows they play. Read-only; the ROM is the player's own.

The addresses are PD NTSC 1.1's, which GE-X 6a keeps (docs/gex-weapons.md):
the data segment loads at 0x80059fe0, g_Weapons is at 0x8006ff18, the
animations segment starts at ROM 0x1a15c0 and its table of 12-byte rows sits
0x38a0 before the mpconfigs segment at 0x7d0a40.

usage: python gexguns.py gex.z64 [slot ...]
"""
import struct
import sys

from pdrom import PdRom

DATA_BASE = 0x80059FE0
G_WEAPONS = 0x8006FF18
ANIM_OFS = 0x1A15C0
ANIM_TABLE = 0x7D0A40 - 0x38A0

GUNCMD = {0: "END", 1: "SHOWPART", 2: "HIDEPART", 3: "WAITFORZRELEASED", 4: "WAITTIME",
          5: "PLAYSOUND", 6: "INCLUDE", 7: "RANDOM", 8: "REPEATUNTILFULL",
          9: "POPOUTSACKOFPILLS", 10: "PLAYANIMATION", 11: "SETSOUNDSPEED"}

# GE-X's slots for GoldenEye's guns (Dab's Mod port/src/modborrow.c)
SLOTS = {2: "knife", 3: "PP7", 4: "PP7 silenced", 5: "DD44", 6: "Klobb", 7: "KF7",
         8: "ZMG", 9: "D5K", 10: "D5K silenced", 11: "Phantom", 12: "AR33", 13: "RC-P90",
         14: "shotgun", 15: "auto shotgun", 16: "sniper rifle", 17: "Cougar",
         18: "Golden Gun", 21: "Moonraker laser", 23: "grenade launcher",
         24: "rocket launcher", 26: "grenade"}


class GexCode:
    def __init__(self, rom):
        self.rom = rom
        self.seg = rom.data   # the inflated data segment

    def u8(self, a): return self.seg[a - DATA_BASE]
    def u16(self, a): return struct.unpack_from(">H", self.seg, a - DATA_BASE)[0]
    def s16(self, a): return struct.unpack_from(">h", self.seg, a - DATA_BASE)[0]
    def u32(self, a): return struct.unpack_from(">I", self.seg, a - DATA_BASE)[0]
    def f32(self, a): return struct.unpack_from(">f", self.seg, a - DATA_BASE)[0]

    def anim(self, n):
        row = ANIM_TABLE + 4 + 12 * n
        frames, bpf, data, hdr, flen, flags = struct.unpack_from(">HHIHBB", self.rom.rom, row)
        return dict(frames=frames, bpf=bpf, rom=ANIM_OFS + data, hdr=hdr, framelen=flen, flags=flags)

    def script(self, addr, indent="    ", seen=None):
        seen = set() if seen is None else seen
        lines = []
        while addr and addr not in seen:
            seen.add(addr)
            kind, u1 = self.u8(addr), self.u8(addr + 1)
            u2, word = self.u16(addr + 2), self.u32(addr + 4)
            text = "%s%-17s u1=%-3d u2=%-5d w=0x%08x" % (indent, GUNCMD.get(kind, kind), u1, u2, word)
            if kind == 10:
                n = self.s16(addr + 2)   # negative: played backwards
                a = self.anim(abs(n))
                text += "  anim %d, %d frames, rom 0x%x" % (n, a["frames"], a["rom"])
            lines.append(text)
            if kind in (6, 7):
                lines += self.script(word, indent + "  ", seen)
            if kind == 0:
                break
            addr += 8
        return lines

    def weapon(self, slot):
        d = self.u32(G_WEAPONS + 4 * slot)
        hi, lo = self.u16(d), self.u16(d + 2)
        names = {f[0]: f[1] for f in self.rom.files}
        out = ["slot %d %s: def 0x%08x model %d %s (lod %d %s), pos %.0f %.0f %.0f, flags 0x%08x"
               % (slot, SLOTS.get(slot, "?"), d, hi, names.get(hi), lo, names.get(lo),
                  self.f32(d + 0x2C), self.f32(d + 0x30), self.f32(d + 0x34), self.u32(d + 0x4C))]
        for f in range(2):
            fa = self.u32(d + 0x14 + 4 * f)
            if fa and self.u32(fa + 0xC):
                out.append("  fire[%d]:" % f)
                out += self.script(self.u32(fa + 0xC))
        for a in range(2):
            am = self.u32(d + 0x1C + 4 * a)
            if am and self.u32(am + 0xC):
                out.append("  reload[%d] (clip %d):" % (a, self.u16(am + 8)))
                out += self.script(self.u32(am + 0xC))
        return out


if __name__ == "__main__":
    code = GexCode(PdRom(sys.argv[1]))
    count = struct.unpack_from(">I", code.rom.rom, ANIM_TABLE)[0]
    print("%s (%s): animation table at rom 0x%x, %d rows" % (code.rom.title, code.rom.code, ANIM_TABLE, count))
    for slot in [int(s) for s in sys.argv[2:]] or [3, 7]:
        print("\n".join(code.weapon(slot)))
