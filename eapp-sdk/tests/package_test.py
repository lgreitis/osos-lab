#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check eapp-pack's module packing against readelf and nm output in the
cross tools' format: which relocations it keeps, refuses and leaves alone, the
.emod it writes, the alignment and build id it reads and the extern script:
python3 tests/package_test.py."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import eapp_package as pack  # noqa: E402

SYMBOLS = """
Symbol table '.symtab' contains 10 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00000000     0 SECTION LOCAL  DEFAULT    1
     2: 00000010     0 SECTION LOCAL  DEFAULT    3
     3: 00000008     4 FUNC    LOCAL  DEFAULT    1 paint
     4: 00000000     8 OBJECT  GLOBAL DEFAULT    1 eapp_module
     5: 18002518   276 FUNC    GLOBAL DEFAULT  ABS eapp_fill_rect
     6: 00000010     0 NOTYPE  GLOBAL DEFAULT    3 __eapp_module_bss_start
     7: 00000018     0 NOTYPE  GLOBAL DEFAULT    3 __eapp_module_bss_end
     8: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND missing
     9: 18000400    12 FUNC    GLOBAL DEFAULT  ABS paint
    10: 0000000c    40 FUNC    GLOBAL HIDDEN     1 __aeabi_idiv
    11: 18000300    40 FUNC    GLOBAL DEFAULT  ABS __aeabi_idiv
"""

# A word naming the module's own static paint moves, though the image has a
# global paint too.
RELOCATIONS = """
Relocation section '.rel.text' at offset 0x15948 contains 6 entries:
 Offset     Info    Type                Sym. Value  Symbol's Name
00000000  00000102 R_ARM_ABS32            00000000   .text
00000008  00000502 R_ARM_ABS32            18002518   eapp_fill_rect
00000004  00000202 R_ARM_ABS32            00000010   .bss
0000000c  00000302 R_ARM_ABS32            00000008   paint
0000000c  00000028 R_ARM_V4BX
0000000c  0000041d R_ARM_JUMP24           00000000   eapp_module

Relocation section '.rel.debug_frame' at offset 0x159b0 contains 1 entry:
 Offset     Info    Type                Sym. Value  Symbol's Name
00000014  00000502 R_ARM_ABS32            00000000   .debug_frame
"""

SECTIONS = """
Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        00000000 010000 000252 00  AX  0   0  4
  [ 2] .rel.text         REL             00000000 015950 000068 08   I 14   1  4
  [ 3] .bss              NOBITS          00000254 010252 000480 00  WA  0   0 16
  [ 4] .debug_frame      PROGBITS        00000000 010254 000020 00      0   0 32
"""


def raises(text, call, *args):
    try:
        call(*args)
    except ValueError as error:
        assert text in str(error), error
        return
    raise AssertionError(f"no error for {text}")


symbols = pack.elf_symbols(SYMBOLS)
assert symbols[5] == ("eapp_fill_rect", 0x18002518, "GLOBAL", "DEFAULT", "ABS")
assert symbols[1][0] == ""
assert pack.module_symbol(symbols, "paint") == 8
assert pack.module_relocations(RELOCATIONS, symbols) == [0, 4, 12]
raises(
    "not defined",
    pack.module_relocations,
    RELOCATIONS.replace("00000502 R_ARM_ABS32", "00000802 R_ARM_ABS32", 1),
    symbols,
)
raises(
    "short call",
    pack.module_relocations,
    RELOCATIONS.replace(
        "0000041d R_ARM_JUMP24           00000000   eapp_module",
        "0000051c R_ARM_CALL             18002518   eapp_fill_rect",
    ),
    symbols,
)
raises(
    "unsupported relocation R_ARM_REL32",
    pack.module_relocations,
    RELOCATIONS.replace("R_ARM_V4BX", "R_ARM_REL32"),
    symbols,
)
raises(
    "names unknown symbol 99",
    pack.module_relocations,
    RELOCATIONS.replace("00000202 R_ARM_ABS32", "00006302 R_ARM_ABS32"),
    symbols,
)
raises(
    "not word-aligned",
    pack.module_relocations,
    RELOCATIONS.replace("00000004  00000202", "00000005  00000202"),
    symbols,
)
raises(
    "does not apply",
    pack.module_relocations,
    RELOCATIONS.replace(".rel.debug_frame", ".rel.init_array"),
    symbols,
)

image_id = bytes(range(16))
binary = bytes(range(14))
data = pack.module_file(binary, symbols, RELOCATIONS, image_id)
assert data[:36] == struct.pack("<4sI16sIII", b"EMOD", 1, image_id, 16, 8, 3)
assert data[36:52] == binary + b"\0\0"
assert data[52:] == struct.pack("<3I", 0, 4, 12)
moved = {**symbols, 4: ("eapp_module", 4, "GLOBAL", "DEFAULT", "1")}
raises("must come first", pack.module_file, binary, moved, RELOCATIONS, image_id)
short = {**symbols, 6: ("__eapp_module_bss_start", 12, "GLOBAL", "DEFAULT", "3")}
raises("segment layout", pack.module_file, binary, short, RELOCATIONS, image_id)
raises("lacks eapp_module", pack.module_file, binary, {}, RELOCATIONS, image_id)
image = {
    1: ("frames", 0x18003770, "GLOBAL", "DEFAULT", "4"),
    2: ("paint", 8, "LOCAL", "DEFAULT", "3"),
}
pack.check_module_globals(symbols, image)
clash = {**symbols, 12: ("frames", 0x1C, "GLOBAL", "DEFAULT", "3")}
raises("defines frames, which the image", pack.check_module_globals, clash, image)

assert pack.section_alignments(SECTIONS) == {".text": 4, ".bss": 16}
assert pack.build_id("    Build ID: " + image_id.hex() + "\n") == image_id
raises("no 16-byte build id", pack.build_id, "    Build ID: 0011\n")

script = pack.module_externs(
    ["memset", "frames", "__aeabi_idiv", "step"], ["step", "__aeabi_idiv"]
)
assert script.splitlines()[1:] == ["EXTERN(frames)", "EXTERN(memset)"]
print("package_test passed")
