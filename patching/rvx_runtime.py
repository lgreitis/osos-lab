# SPDX-License-Identifier: GPL-3.0-only
"""Link the builtin RVX host independently of its guest heap address."""

import struct
from concurrent.futures import ThreadPoolExecutor

from .symbols import read, require
from .toolchain import FLAGS, run

RUNTIME_CFLAGS = ("-flto", "-fno-inline-functions-called-once")
WORD = struct.Struct("<I")


def relocation_words(raw, mirror, base, memory_size):
    """Find the internal pointers that move with the image's link address."""
    if len(raw) != len(mirror) or len(raw) % WORD.size:
        raise ValueError("RVX runtime changes layout at the second link address")
    relocations = []
    for offset in range(0, len(raw), WORD.size):
        original = WORD.unpack_from(raw, offset)[0]
        moved = WORD.unpack_from(mirror, offset)[0]
        if moved == original:
            continue
        if original > memory_size or moved != original + base:
            raise ValueError(f"Non-relocatable RVX word at {offset:#x}")
        index = offset // WORD.size
        if index > 0xFFFF:
            raise ValueError(f"RVX relocation at {offset:#x} exceeds the 16-bit index")
        relocations.append(index)
    return relocations


def build_trailer(source, directory, resident, prefix, jobs):
    out = directory / "rvx"
    out.mkdir(exist_ok=True)
    sources = sorted(
        path for path in (source / "rvx").glob("*.c") if path.name != "gate.c"
    )

    sources.append(source.parent / "rvx-sdk/rv_libc.c")

    def compile_unit(path):
        obj = path.with_suffix(".o").name
        run(
            [
                prefix + "gcc",
                *FLAGS,
                *RUNTIME_CFLAGS,
                *(
                    ["-fno-tree-loop-distribute-patterns"]
                    if path.name == "rv_libc.c"
                    else []
                ),
                "-mthumb",
                "-std=c99",
                "-I",
                source,
                "-I",
                source / "native",
                "-I",
                source / "patches",
                "-I",
                directory,
                "-c",
                path,
                "-o",
                obj,
            ],
            out,
        )
        return obj

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        objects = list(pool.map(compile_unit, sources))

    # Keep the fixed firmware addresses in local veneers so the compiler
    # can use normal relative calls within the loadable code.
    functions = [
        "__aeabi_idiv",
        "__aeabi_idivmod",
        "__aeabi_uidiv",
        "__aeabi_uidivmod",
        "cfw_dark_mode_enabled",
        "cfw_album_artist_read",
        "cfw_store_get",
        "cfw_rvx_lock",
        "cfw_rvx_unlock",
        "cfw_rvx_sync",
        "cfw_rvx_write",
    ]
    assembly = [".syntax unified", ".cpu arm926ej-s", ".arm", ".text", ".balign 4"]
    for name in functions:
        assembly.extend(
            [
                f".global {name}",
                f".type {name},%function",
                f"{name}:",
                "ldr ip,[pc,#0]",
                "bx ip",
                f".word {require(resident, name):#x}",
            ]
        )
    # GCC can request either the AEABI or the libgcc name before LTO completes.
    for alias, target in [("__divsi3", "__aeabi_idiv"), ("__udivsi3", "__aeabi_uidiv")]:
        assembly.extend([f".global {alias}", f".set {alias}, {target}"])
    (out / "imports.S").write_text("\n".join(assembly) + "\n")
    run([prefix + "gcc", *FLAGS, "-c", "imports.S", "-o", "imports.o"], out)
    absolute = [
        "__rvx_reserved_start",
        "__rvx_reserved_end",
        "rvx_vectors",
        "rvx_insert_vector",
        "rvx_insert_slot",
        "rvx_chains",
        "cfw_rvx_has_canvas",
    ]
    layout = (
        "\n".join(f"{n} = {require(resident, n):#x};" for n in absolute)
        + """
SECTIONS {
    . = RVX_BASE;
    .image : { KEEP(*(.rvx.exports)) *(.text*) *(.rodata*) __rvx_data_start = .; *(.data*) __rvx_data_end = .; . = ALIGN(4); }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(4); }
    __runtime_end = .;
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
"""
    )
    (out / "image.lds").write_text(layout)

    def link_at(base):
        name = f"link-{base:x}"
        run(
            [
                prefix + "gcc",
                "-mcpu=arm926ej-s",
                "-mthumb-interwork",
                *RUNTIME_CFLAGS,
                "-nostdlib",
                "-Wl,-u,memset",
                f"-Wl,--defsym,RVX_BASE={base}",
                "-Wl,-T,image.lds",
                *objects,
                "imports.o",
                "-lgcc",
                "-o",
                name + ".elf",
            ],
            out,
        )
        run([prefix + "objcopy", "-O", "binary", name + ".elf", name + ".bin"], out)
        data = (out / (name + ".bin")).read_bytes()
        syms = read(out / (name + ".elf"), prefix)
        if require(syms, "rvx_exports") != base:
            raise ValueError("Unexpected runtime entry")
        return data, require(syms, "__runtime_end") - base

    raw, memory = link_at(0)
    mirror_base = 0x10000
    mirror, mirror_memory = link_at(mirror_base)
    if mirror_memory != memory:
        raise ValueError("RVX runtime changes memory size at the second link address")
    relocs = relocation_words(raw, mirror, mirror_base, memory)

    # Check the encoded relocations against a direct link at a guest heap address.
    heap_base = 0x09501238
    check, check_mem = link_at(heap_base)
    relocated = bytearray(raw)
    for index in relocs:
        offset = index * WORD.size
        WORD.pack_into(relocated, offset, heap_base + WORD.unpack_from(raw, offset)[0])
    if relocated != check or memory != check_mem:
        raise ValueError(
            "RVX relocations differ from a direct link in guest heap range"
        )
    trailer = raw + struct.pack(f"<{len(relocs)}H", *relocs)
    h = 2166136261
    for byte in trailer:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return trailer, struct.pack("<4I", len(raw), memory, len(relocs), h)
