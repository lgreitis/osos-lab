// SPDX-License-Identifier: GPL-3.0-only
// Adapted from https://github.com/freemyipod/wInd3x:
// pkg/exploit/{exploit,wind3x_n3g}.go, pkg/exploit/dumpmem, and pkg/uasm.
// Copyright (C) 2022 Serge 'q3k' Bazanski (q3k@q3k.org).
// Upstream is GPL-2.0-or-later; this port uses GPL-3.0-only.

use crate::{invalid, Result};

pub(crate) const PREFIX_SIZE: usize = 0x220284a8 - 0x22028220;

// Deduplicate literals in encounter order to match wInd3x's assembler.
#[derive(Default)]
struct Arm {
    code: Vec<u32>,
    literals: Vec<(usize, u32)>,
}

impl Arm {
    fn fast_footer(&mut self, address: u32, response_size: usize) {
        self.aes_call(0x20000468, &[]);
        self.aes_call(0x2000a514, &[]);
        self.aes_call(0x2000aa40, &[address, response_size as u32]);
        self.code
            .extend([0xe59d4000, 0xe59de004, 0xe28dd008, 0xe12fff1e]);
    }

    // Match wInd3x's literal-load encoding for ROM calls.
    fn aes_call(&mut self, address: u32, arguments: &[u32]) {
        assert!(arguments.len() <= 5);
        if arguments.len() == 5 {
            self.code.push(0xe24dd008); // sub sp, sp, #8 (keep alignment)
            self.literal(0, arguments[4]);
            self.code.push(0xe58d0000); // str r0, [sp]
        }
        for (register, value) in arguments.iter().take(4).enumerate() {
            self.literal(register as u32, *value);
        }
        self.literal(14, address);
        self.code.push(0xe12fff3e);
        if arguments.len() == 5 {
            self.code.push(0xe28dd008);
        }
    }

    fn branch_to(&mut self, instruction: usize, target: usize) {
        let displacement = target as i32 - instruction as i32 - 2;
        self.code[instruction] |= displacement as u32 & 0x00ff_ffff;
    }

    fn literal(&mut self, register: u32, value: u32) {
        self.literals.push((self.code.len(), value));
        self.code.push(0xe59f0000 | (register << 12));
    }

    fn value(&mut self, register: u32, value: u32) {
        if value < 256 {
            self.code.push(0xe3a00000 | (register << 12) | value);
        } else {
            self.literal(register, value);
        }
    }

    fn call(&mut self, address: u32, arguments: &[u32]) {
        assert!(arguments.len() <= 4);
        for (register, value) in arguments.iter().enumerate() {
            self.value(register as u32, *value);
        }
        self.literal(14, address);
        self.code.push(0xe12fff3e); // blx lr
    }

    fn footer(&mut self, address: u32) {
        self.literal(0, address);
        self.value(1, 64);
        self.literal(2, 0x2000aa40);
        self.code.push(0xe12fff32); // blx r2: send 64 bytes over EP0
        self.literal(14, 0x200048d4);
        self.code.push(0xe12fff1e); // bx lr: return to BootROM USB handler
    }

    fn finish(mut self) -> Vec<u8> {
        let code_len = self.code.len();
        let mut pool = Vec::new();
        for (instruction, value) in self.literals {
            let index = match pool.iter().position(|v| *v == value) {
                Some(index) => index,
                None => {
                    pool.push(value);
                    pool.len() - 1
                }
            };
            let offset = (code_len + index) * 4 - (instruction * 4 + 8);
            assert!(offset < 4096);
            self.code[instruction] |= offset as u32;
        }
        self.code.extend(pool);
        let bytes: Vec<_> = self.code.into_iter().flat_map(u32::to_le_bytes).collect();
        assert!(PREFIX_SIZE + bytes.len() <= 0x400);
        bytes
    }
}

pub(crate) fn fast_aes(size: usize, decrypt: bool) -> Result<Vec<u8>> {
    fast_aes_key(size, decrypt, 1)
}

pub(crate) fn fast_aes_key(size: usize, decrypt: bool, key: u32) -> Result<Vec<u8>> {
    if !matches!(key, 1 | 2) {
        return Err(invalid("Unsupported hardware AES key"));
    }
    if !(16..=512).contains(&size) || !size.is_multiple_of(16) {
        return Err(invalid("AES chunk must be 16..512 bytes, aligned to 16"));
    }
    let buffer = 0x22028220;
    let mut arm = Arm::default();
    arm.call(0x20000418, &[]);
    if decrypt {
        arm.aes_call(0x2000147c, &[10]);
        arm.aes_call(0x20000474, &[]);
        arm.aes_call(
            0x20001f04,
            &[buffer + 16, size as u32, key, 0, buffer + 16 + size as u32],
        );
        arm.literal(1, 0x38c0000c);
        arm.literal(2, 100000);
        let poll = arm.code.len();
        arm.code.extend([0xe5910000, 0xe200000f, 0xe3500000]);
        let complete_branch = arm.code.len();
        arm.code.push(0x1a000000);
        arm.code.extend([0xe2422001, 0xe3520000]);
        let poll_branch = arm.code.len();
        arm.code.push(0x1a000000);
        arm.branch_to(poll_branch, poll);
        arm.aes_call(0x20001afc, &[]);
        arm.code.push(0xe3a02001);
        let return_branch = arm.code.len();
        arm.code.push(0xea000000);
        arm.branch_to(complete_branch, arm.code.len());
        arm.aes_call(0x20001d48, &[]);
        arm.code.push(0xe3a02000);
        arm.branch_to(return_branch, arm.code.len());
        arm.aes_call(0x20000454, &[]); // preserves r2 (status)
    } else {
        arm.code.push(0xe3a02000);
    }
    arm.literal(1, buffer);
    arm.code.push(0xe5812000);
    // Rearm OUT before scheduling IN, then unwind the dispatcher's {r4,lr}
    // frame directly to preserve multi-packet continuation.
    arm.fast_footer(buffer, size + 16);
    Ok(arm.finish())
}

/// Reply layout: status u32, 12 reserved bytes, then up to 512 data bytes.
pub(crate) fn fast_nor(offset: u32, size: usize) -> Result<Vec<u8>> {
    if !(1..=512).contains(&size) || u64::from(offset) + size as u64 > 0x100000 {
        return Err(invalid(
            "Fast NOR read must be 1..512 bytes within the 1 MiB flash",
        ));
    }
    let mut arm = Arm::default();
    arm.call(0x20008f90, &[0, 200]);
    arm.call(0x20004c7c, &[0]);
    arm.code.push(0xe3500000);
    let branch = arm.code.len();
    arm.code.push(0x1a000000);
    // Reset the FIFO/count after the ready poll. Command/address consume four
    // receive bytes, which must be included in the receive count.
    arm.call(0x20008f90, &[0, size as u32 + 4]);
    arm.call(0x20004e5c, &[0, offset, size as u32, 0x22020010]);
    arm.branch_to(branch, arm.code.len());
    arm.literal(1, 0x22020000);
    arm.code.push(0xe5810000);
    arm.fast_footer(0x22020000, size + 16);
    Ok(arm.finish())
}

pub(crate) fn memory_read(address: u32) -> Vec<u8> {
    let mut arm = Arm::default();
    arm.call(0x20000418, &[]); // disable instruction cache
    arm.footer(address);
    arm.finish()
}

/// Send 512 bytes directly from ROM after the caller fingerprints its routines.
pub(crate) fn fast_bootrom(offset: u32) -> Result<Vec<u8>> {
    if offset > 0xfe00 || !offset.is_multiple_of(512) {
        return Err(invalid(
            "Fast BootROM offset must be 512-byte aligned within 64 KiB",
        ));
    }
    let mut arm = Arm::default();
    arm.call(0x20000418, &[]);
    arm.fast_footer(0x20000000 + offset, 512);
    Ok(arm.finish())
}

pub(crate) fn nor_init() -> Vec<u8> {
    let mut arm = Arm::default();
    arm.call(0x20000418, &[]);
    arm.call(0x20001924, &[0]); // SPI0 GPIO
    arm.call(0x2000147c, &[0x22]); // SPI0 clock
    arm.footer(0x20000000);
    arm.finish()
}

pub(crate) fn nor_read(offset: u32, size: u32) -> Result<Vec<u8>> {
    if !(1..=60).contains(&size) || u64::from(offset) + u64::from(size) > 0x100000 {
        return Err(invalid(
            "NOR read must be 1–60 bytes within the 1 MiB flash",
        ));
    }
    let mut arm = Arm::default();
    arm.call(0x20008f90, &[0, 200]);
    arm.call(0x20004c7c, &[0]); // bounded ready poll
    arm.code.push(0xe3500000); // cmp r0, #0
    let branch = arm.code.len();
    arm.code.push(0x1a000000); // bne done

    // Reset after polling; command/address consume four receive bytes.
    arm.call(0x20008f90, &[0, size + 4]);
    arm.call(0x20004e5c, &[0, offset, size, 0x22020004]);
    let done = arm.code.len();
    arm.code[branch] |= (done - branch - 2) as u32;
    arm.literal(1, 0x22020000);
    arm.code.push(0xe5810000); // str r0, [r1]: ROM status before returned data
    arm.footer(0x22020000);
    Ok(arm.finish())
}

pub(crate) fn fast_memory(address: u32, size: usize) -> Result<Vec<u8>> {
    if !(1..=512).contains(&size) || u64::from(address) + size as u64 > 1 << 32 {
        return Err(invalid("Invalid memory reply range"));
    }
    let mut arm = Arm::default();
    arm.call(0x20000418, &[]);
    arm.fast_footer(address, size);
    Ok(arm.finish())
}

// The copied IMG1 hook includes its literal pool; its ROM ABI requires r4.
pub(crate) fn image_hook() -> Vec<u8> {
    let mut a = Arm::default();
    a.call(0x20000418, &[]);
    a.literal(0, 0x2203fff8);
    a.code.push(0xe5900000);
    a.literal(1, 379);
    a.code.push(0xe0800001);
    let descriptor_ref = a.literals.len();
    a.literal(1, 0);
    a.code.push(0xe5d12000);
    let copy = a.code.len();
    a.code.extend([
        0xe5d13000, 0xe5c03000, 0xe2811001, 0xe2800001, 0xe2422001, 0xe3520000,
    ]);
    let branch = a.code.len();
    a.code.push(0x1a000000);
    a.branch_to(branch, copy);
    let skip = a.code.len();
    a.code.push(0xea000000);
    let hook = a.code.len();
    a.code.extend([0xe24dd004, 0xe58de000]);
    a.literal(4, 0x2203fff8);
    a.code.extend([0xe5941000, 0xe5910738, 0xe590002c]);
    a.literal(1, 0x800);
    a.code.push(0xe0811000);
    a.literal(2, 0x20000ac4);
    a.code.push(0xe12fff32);
    a.literal(0, 0x2203fff8);
    a.code.extend([0xe5900000, 0xe3a01001, 0xe580102c]);
    a.literal(1, 0x302e31);
    a.code.extend([
        0xe5801034, 0xe3a01000, 0xe5801030, 0xe59de000, 0xe28dd004, 0xe3a00000, 0xe12fff1e,
    ]);
    a.branch_to(skip, a.code.len());
    a.literal(0, 0x2203d800);
    a.literal(1, 0x220284a8 + hook as u32 * 4);
    a.literal(2, 0x100);
    let copy = a.code.len();
    a.code.extend([
        0xe5d13000, 0xe5c03000, 0xe2811001, 0xe2800001, 0xe2422001, 0xe3520000,
    ]);
    let branch = a.code.len();
    a.code.push(0x1a000000);
    a.branch_to(branch, copy);
    a.literal(0, 0x2203fff8);
    a.code.push(0xe5900000);
    a.literal(1, 0x2203d800);
    a.code.push(0xe5801028);
    a.footer(0x20000000);
    a.literals[descriptor_ref].1 = 0x220284a8 + a.code.len() as u32 * 4;
    let mut descriptor = vec![20, 3];
    for c in "haxed dfu".encode_utf16() {
        descriptor.extend(c.to_le_bytes());
    }
    a.code.extend(
        descriptor
            .chunks_exact(4)
            .map(|v| u32::from_le_bytes(v.try_into().unwrap())),
    );
    a.finish()
}
