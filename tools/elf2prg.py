#!/usr/bin/env python3
"""Linked m68k ELF (with --emit-relocs) -> Atari TOS .PRG with fixup table."""
import struct, sys
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

R_68K_32 = 1
elf = ELFFile(open(sys.argv[1], "rb"))
text, data, bss = (elf.get_section_by_name(n) for n in (".text", ".data", ".bss"))
tbytes, dbytes = text.data(), data.data()
assert text["sh_addr"] == 0 and data["sh_addr"] == len(tbytes), "unexpected layout"
blen = bss["sh_size"] if bss else 0

fix = []
for sec in elf.iter_sections():
    if not isinstance(sec, RelocationSection):
        continue
    target = elf.get_section(sec["sh_info"])
    if target.name not in (".text", ".data"):
        continue
    for r in sec.iter_relocations():
        t = r["r_info_type"]
        if t == R_68K_32:
            # linked executable: r_offset is already the absolute address
            addr = r["r_offset"] if elf["e_type"] == "ET_EXEC" else target["sh_addr"] + r["r_offset"]
            fix.append(addr)
        elif t not in (0, 4, 5, 6):   # PC-relative types need no fixup
            sys.exit(f"unsupported reloc type {t} in {target.name}")
fix = sorted(set(fix))
for a in fix:
    assert 0 <= a <= len(tbytes) + len(dbytes) - 4, "fixup outside program: %x" % a
    assert a % 2 == 0, "odd fixup address %x" % a

out = bytearray(struct.pack(">HLLLLLLH", 0x601A, len(tbytes), len(dbytes), blen, 0, 0, 0x1, 0))   # flags: fastload only (no TT-RAM: the screen buffers must be in ST RAM)
out += tbytes + dbytes
if not fix:
    out += struct.pack(">L", 0)
else:
    out += struct.pack(">L", fix[0])
    prev = fix[0]
    for a in fix[1:]:
        d = a - prev
        assert d >= 2 and d % 2 == 0
        while d > 254:
            out.append(1); d -= 254
        out.append(d)
        prev = a
    out.append(0)
open(sys.argv[2], "wb").write(out)
print(f"{sys.argv[2]}: text {len(tbytes)} data {len(dbytes)} bss {blen} fixups {len(fix)}")
