//! MacsBug low-level debugger trap (0x7F800008) scan + minimal PPC decode.
//!
//! `pefcheck --trapcheck` scans a PEF container's code sections for the
//! native PPC instruction word 0x7F800008 — "tw LT|GT|EQ,r0,r0"
//! (kPowerPCLowLevelDebuggerTrap, MacsBug 6.5.3 Read Me) — and reports
//! each hit with its offsets, the breadcrumb checkpoint tag (identified
//! from the `li rX,<tag>` immediately before the trap), and the decoded
//! next instruction. This mechanically proves the USBMIDI9 OMS trace
//! build contains the literal trap bytes at every checkpoint and that
//! the instruction after each trap is the intended continuation.

use crate::pef::{Container, Section};

/// The trap word: tw 0x1C, r0, r0 (opcode 31, TO=28=LT|GT|EQ, rA=0,
/// rB=0, XO=4). Encoded 0x7F800008.
pub const TRAP_WORD: u32 = 0x7F_80_00_08;

/// Breadcrumb checkpoint tags (oms/oms_driver.c, USBMIDI9_OMS_TRACE_SEARCH).
pub const TAGS: &[(u32, &str)] = &[
    (0x0E0, "E0"),
    (0x100, "I0"),
    (0x101, "I1"),
    (0x102, "I2"),
    (0x103, "I3"),
    (0x104, "I4"),
    (0x105, "I5"),
    (0x106, "I6"),
    (0x1F0, "IR"),
    (0x200, "T0"),
    (0x201, "T1"),
    (0x202, "T2"),
    (0x203, "T3"),
    (0x204, "T4"),
    (0x205, "T5"),
];

/// How far back (in instructions) the tag scan looks for the `li`
/// that loads the checkpoint's breadcrumb tag.
const TAG_SCAN_WINDOW: usize = 64;

/// One trap found in a code section.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TrapHit {
    /// Offset of the trap within its code section (4-byte aligned).
    pub code_offset: usize,
    /// Offset within the PEF container (= PPCC-relative offset: the
    /// `'PPCC'` resource data is the raw PEF container, no length prefix).
    pub container_offset: usize,
    /// Breadcrumb tag identified from the preceding instructions.
    pub tag: Option<(u32, &'static str)>,
    /// The instruction word immediately following the trap.
    pub next_word: u32,
    /// Decoded next instruction.
    pub next_decode: String,
}

/// Raw bytes of a section, unpacking PackedData when needed.
/// Returns Err for truncated or undecodable sections.
pub fn section_bytes(c: &Container, s: &Section) -> Result<Vec<u8>, String> {
    let off = s.container_offset as usize;
    let len = s.container_length as usize;
    if off + len > c.data.len() {
        return Err(format!(
            "section container 0x{:x}+0x{:x} exceeds file 0x{:x}",
            off,
            len,
            c.data.len()
        ));
    }
    if s.container_length == s.unpacked_length {
        Ok(c.data[off..off + len].to_vec())
    } else {
        c.unpack_section(s)
    }
}

/// Scan one code section's bytes for the trap word.
pub fn scan_section(bytes: &[u8], section_container_offset: usize) -> Vec<TrapHit> {
    let mut hits = Vec::new();
    let n = bytes.len() / 4;
    for i in 0..n {
        let w = u32::from_be_bytes([
            bytes[4 * i],
            bytes[4 * i + 1],
            bytes[4 * i + 2],
            bytes[4 * i + 3],
        ]);
        if w == TRAP_WORD {
            let next_word = if i + 1 < n {
                u32::from_be_bytes([
                    bytes[4 * (i + 1)],
                    bytes[4 * (i + 1) + 1],
                    bytes[4 * (i + 1) + 2],
                    bytes[4 * (i + 1) + 3],
                ])
            } else {
                0
            };
            let tag = identify_tag(bytes, i);
            hits.push(TrapHit {
                code_offset: 4 * i,
                container_offset: section_container_offset + 4 * i,
                tag,
                next_word,
                next_decode: decode(next_word),
            });
        }
    }
    hits
}

/// Identify the breadcrumb checkpoint tag immediately before the trap at
/// `trap_index`: scan back up to TAG_SCAN_WINDOW instructions for the
/// LAST `li rX,<known tag>` (addi rX,r0,SIMM) or `lis rX,hi` + `ori`
/// two-instruction form, and return the matching tag. Returns None if no
/// known tag is found (reported as `tag ?`, never guessed). NOTE: a
/// stale tag-valued `li` closer than the checkpoint's own tag load would
/// mislabel — with CW4 codegen the crumb's `li` is a few instructions
/// before the trap and the tags are unique, so the risk is negligible;
/// an unidentified tag is always reported, never silently dropped.
pub fn identify_tag(words: &[u8], trap_index: usize) -> Option<(u32, &'static str)> {
    let start = trap_index.saturating_sub(TAG_SCAN_WINDOW);
    let mut found: Option<(u32, &'static str)> = None;
    for i in (start..trap_index).rev() {
        let w = match word_at(words, i) {
            Some(w) => w,
            None => continue,
        };
        // li rX, SIMM: opcode 14 (addi), rA = 0.
        if w >> 26 == 14 && ((w >> 16) & 31) == 0 {
            let imm = ((w & 0xFFFF) as u16) as i16 as i32;
            if let Some(tag) = TAGS.iter().find(|(t, _)| *t as i32 == imm) {
                found = Some(*tag);
                break;
            }
        }
        // lis rX, hi followed by ori rX, rX, lo.
        if w >> 26 == 15 && ((w >> 16) & 31) == 0 {
            let hi = ((w & 0xFFFF) as u16) as i16 as i32;
            if let Some(nw) = word_at(words, i + 1) {
                if nw >> 26 == 24 && ((nw >> 16) & 31) == ((nw >> 21) & 31) {
                    let lo = nw & 0xFFFF;
                    let val = (((hi as u32) << 16) | lo) as i32;
                    if let Some(tag) = TAGS.iter().find(|(t, _)| *t as i32 == val) {
                        found = Some(*tag);
                        break;
                    }
                }
            }
        }
    }
    found
}

fn word_at(bytes: &[u8], i: usize) -> Option<u32> {
    let o = 4 * i;
    if o + 4 > bytes.len() {
        None
    } else {
        Some(u32::from_be_bytes([
            bytes[o],
            bytes[o + 1],
            bytes[o + 2],
            bytes[o + 3],
        ]))
    }
}

/// Encode `tw TO,rA,rB` (opcode 31, XO=4).
pub fn encode_tw(to: u32, ra: u32, rb: u32) -> u32 {
    (31 << 26) | ((to & 31) << 21) | ((ra & 31) << 16) | ((rb & 31) << 11) | (4 << 1)
}

/// Minimal PowerPC disassembler for compiler-generated code: one word ->
/// readable mnemonic. Unknown/uncovered words fall back to "opcode N".
pub fn decode(w: u32) -> String {
    let op = w >> 26;
    match op {
        3 => {
            // twi TO,rA,SIMM
            format!(
                "twi 0x{:x},r{},{}",
                (w >> 21) & 31,
                (w >> 16) & 31,
                sign16(w)
            )
        }
        7 => format!(
            "mulli r{},r{},{}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            sign16(w)
        ),
        8 => format!(
            "subfic r{},r{},{}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            sign16(w)
        ),
        10 => format!(
            "cmplwi cr{},{},{}",
            (w >> 21) & 7,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        11 => format!("cmpwi cr{},{},{}", (w >> 21) & 7, (w >> 16) & 31, sign16(w)),
        12 => format!(
            "addic r{},r{},{}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            sign16(w)
        ),
        13 => format!(
            "addic. r{},r{},{}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            sign16(w)
        ),
        14 => {
            let d = (w >> 21) & 31;
            let a = (w >> 16) & 31;
            let imm = sign16(w);
            if a == 0 {
                format!("li r{},{}", d, imm)
            } else {
                format!("addi r{},r{},{}", d, a, imm)
            }
        }
        15 => {
            let d = (w >> 21) & 31;
            let a = (w >> 16) & 31;
            if a == 0 {
                format!("lis r{},0x{:x}", d, (w & 0xFFFF) as u16)
            } else {
                format!("addis r{},r{},0x{:x}", d, a, (w & 0xFFFF) as u16)
            }
        }
        16 => {
            // bc BO,BI,BD (B-form)
            let bo = (w >> 21) & 31;
            let bi = (w >> 16) & 31;
            let aa = (w >> 1) & 1;
            let lk = w & 1;
            let bd = (((w & 0xFFFC) as u16) as i16) as i32;
            let name = match (bo, bi) {
                (12, 0) => "blt",
                (4, 0) => "bge",
                (12, 1) => "bgt",
                (4, 1) => "ble",
                (12, 2) => "beq",
                (4, 2) => "bne",
                (16, 0) => "bdnz",
                (18, 0) => "bdz",
                _ => {
                    return format!("bc 0x{:x},0x{:x} {}", bo, bi, target(aa, bd));
                }
            };
            format!(
                "{}{} {}",
                name,
                if lk == 1 { "l" } else { "" },
                target(aa, bd)
            )
        }
        17 => "sc".to_string(),
        18 => {
            // b/bl LI,AA,LK
            let li = (w & 0x03FF_FFFC) as i32;
            let li = if li & 0x0200_0000 != 0 {
                li | !0x03FF_FFFF
            } else {
                li
            };
            let aa = (w >> 1) & 1;
            let lk = w & 1;
            let name = if lk == 1 { "bl" } else { "b" };
            if aa == 1 {
                format!("{}a 0x{:x}", name, li)
            } else {
                let s = if li >= 0 { "+" } else { "-" };
                let v = if li >= 0 { li } else { -li };
                format!("{} {}0x{:x}", name, s, v)
            }
        }
        19 => decode_xl(w),
        20 => {
            // rlwimi rA,rS,SH,MB,ME
            format!(
                "rlwimi{} r{},r{},{},{},{}",
                rec_suffix(w),
                (w >> 16) & 31,
                (w >> 21) & 31,
                (w >> 11) & 31,
                (w >> 6) & 31,
                (w >> 1) & 31,
            )
        }
        21 => {
            // rlwinm rA,rS,SH,MB,ME
            format!(
                "rlwinm{} r{},r{},{},{},{}",
                rec_suffix(w),
                (w >> 16) & 31,
                (w >> 21) & 31,
                (w >> 11) & 31,
                (w >> 6) & 31,
                (w >> 1) & 31,
            )
        }
        23 => {
            // rlwnm rA,rS,rB,MB,ME
            format!(
                "rlwnm{} r{},r{},r{},{},{}",
                rec_suffix(w),
                (w >> 16) & 31,
                (w >> 21) & 31,
                (w >> 11) & 31,
                (w >> 6) & 31,
                (w >> 1) & 31,
            )
        }
        24 => {
            // ori rS,rA,UIMM
            let s = (w >> 21) & 31;
            let a = (w >> 16) & 31;
            if s == a && (w & 0xFFFF) == 0 {
                "nop".to_string()
            } else {
                format!("ori r{},r{},0x{:x}", s, a, w & 0xFFFF)
            }
        }
        25 => format!(
            "oris r{},r{},0x{:x}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        26 => format!(
            "xori r{},r{},0x{:x}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        27 => format!(
            "xoris r{},r{},0x{:x}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        28 => format!(
            "andi. r{},r{},0x{:x}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        29 => format!(
            "andis. r{},r{},0x{:x}",
            (w >> 21) & 31,
            (w >> 16) & 31,
            w & 0xFFFF
        ),
        32 => format!("lwz r{},{}", (w >> 21) & 31, dform(w)),
        33 => format!("lwzu r{},{}", (w >> 21) & 31, dform(w)),
        34 => format!("lbz r{},{}", (w >> 21) & 31, dform(w)),
        35 => format!("lbzu r{},{}", (w >> 21) & 31, dform(w)),
        36 => format!("stw r{},{}", (w >> 21) & 31, dform(w)),
        37 => format!("stwu r{},{}", (w >> 21) & 31, dform(w)),
        38 => format!("stb r{},{}", (w >> 21) & 31, dform(w)),
        39 => format!("stbu r{},{}", (w >> 21) & 31, dform(w)),
        40 => format!("lhz r{},{}", (w >> 21) & 31, dform(w)),
        41 => format!("lhzu r{},{}", (w >> 21) & 31, dform(w)),
        42 => format!("lha r{},{}", (w >> 21) & 31, dform(w)),
        43 => format!("lhau r{},{}", (w >> 21) & 31, dform(w)),
        44 => format!("sth r{},{}", (w >> 21) & 31, dform(w)),
        45 => format!("sthu r{},{}", (w >> 21) & 31, dform(w)),
        46 => format!("lmw r{},{}", (w >> 21) & 31, dform(w)),
        47 => format!("stmw r{},{}", (w >> 21) & 31, dform(w)),
        48 => format!("lfs f{},{}", (w >> 21) & 31, dform(w)),
        49 => format!("lfsu f{},{}", (w >> 21) & 31, dform(w)),
        50 => format!("lfd f{},{}", (w >> 21) & 31, dform(w)),
        51 => format!("lfdu f{},{}", (w >> 21) & 31, dform(w)),
        52 => format!("stfs f{},{}", (w >> 21) & 31, dform(w)),
        53 => format!("stfsu f{},{}", (w >> 21) & 31, dform(w)),
        54 => format!("stfd f{},{}", (w >> 21) & 31, dform(w)),
        55 => format!("stfdu f{},{}", (w >> 21) & 31, dform(w)),
        31 => decode_x(w),
        59 => decode_fp_a(w),
        63 => decode_fp(w),
        _ => format!("opcode {}", op),
    }
}

fn dform(w: u32) -> String {
    format!("{}(r{})", sign16(w), (w >> 16) & 31)
}

fn sign16(w: u32) -> i32 {
    ((w & 0xFFFF) as u16) as i16 as i32
}

fn rec_suffix(w: u32) -> String {
    if w & 1 == 1 { "." } else { "" }.to_string()
}

fn target(aa: u32, d: i32) -> String {
    if aa == 1 {
        format!("0x{:x}", d)
    } else if d >= 0 {
        format!("+0x{:x}", d)
    } else {
        format!("-0x{:x}", -d)
    }
}

/// Opcode 19 (XL-form): bclr/bcctr/mcrf/isync/rfi.
fn decode_xl(w: u32) -> String {
    let xo = (w >> 1) & 0x3FF;
    let lk = w & 1;
    match xo {
        0 => format!(
            "mcrf cr{},{},cr{}",
            (w >> 23) & 7,
            (w >> 18) & 7,
            (w >> 13) & 7
        ),
        16 => {
            // bclr BO,BI (LKL bit 31)
            let bo = (w >> 21) & 31;
            let bi = (w >> 16) & 31;
            let lk = if lk == 1 { "l" } else { "" };
            if bo == 20 && bi == 0 {
                format!("blr{}", lk)
            } else {
                format!("bclr{} 0x{:x},0x{:x}", lk, bo, bi)
            }
        }
        50 => "rfi".to_string(),
        150 => "isync".to_string(),
        528 => {
            // bcctr BO,BI
            let bo = (w >> 21) & 31;
            let bi = (w >> 16) & 31;
            let lk = if lk == 1 { "l" } else { "" };
            format!("bcctr{} 0x{:x},0x{:x}", lk, bo, bi)
        }
        _ => format!("opcode 19 xo 0x{:x}", xo),
    }
}

/// Opcode 31 (X-form) — the compiler-relevant subset.
fn decode_x(w: u32) -> String {
    let xo = (w >> 1) & 0x3FF;
    let r_s = (w >> 21) & 31;
    let r_a = (w >> 16) & 31;
    let r_b = (w >> 11) & 31;
    let o = if (w >> 10) & 1 == 1 { "o" } else { "" };
    let dot = if w & 1 == 1 { "." } else { "" };
    match xo {
        0 => format!("cmpw cr{},{},r{}", (w >> 23) & 7, r_a, r_b),
        4 => format!("tw 0x{:x},r{},r{}", r_s, r_a, r_b),
        8 => format!("subfc{} r{},r{},r{}", o, r_s, r_a, r_b),
        10 => format!("addc{} r{},r{},r{}", o, r_s, r_a, r_b),
        11 => format!("mulhwu r{},r{},r{}", r_s, r_a, r_b),
        19 => format!("mfcr r{}", r_s),
        20 => format!("lwarx r{},r{},r{}", r_s, r_a, r_b),
        23 => format!("lwzx r{},r{},r{}", r_s, r_a, r_b),
        24 => format!("slw{} r{},r{},r{}", dot, r_s, r_a, r_b),
        26 => format!("cntlzw{} r{},r{}", dot, r_s, r_a),
        27 => format!("sld{} r{},r{},r{}", dot, r_s, r_a, r_b),
        28 => format!("and{} r{},r{},r{}", dot, r_s, r_a, r_b),
        32 => format!("cmplw cr{},{},r{}", (w >> 23) & 7, r_a, r_b),
        40 => format!("subf{} r{},r{},r{}", o, r_s, r_a, r_b),
        54 => format!("dcbst r{},r{}", r_a, r_b),
        60 => format!("andc{} r{},r{},r{}", dot, r_s, r_a, r_b),
        75 => format!("mulhw{} r{},r{},r{}", dot, r_s, r_a, r_b),
        86 => format!("dcbf r{},r{}", r_a, r_b),
        87 => format!("lbzx r{},r{},r{}", r_s, r_a, r_b),
        104 => format!("neg{} r{},r{}", o, r_s, r_a),
        119 => format!("lbzux r{},r{},r{}", r_s, r_a, r_b),
        124 => format!("nor{} r{},r{},r{}", dot, r_s, r_a, r_b),
        144 => format!("mtcrf 0x{:x},r{}", (w >> 18) & 0xFF, r_s),
        150 => format!("stwcx. r{},r{},r{}", r_s, r_a, r_b),
        151 => format!("stwx r{},r{},r{}", r_s, r_a, r_b),
        183 => format!("stwux r{},r{},r{}", r_s, r_a, r_b),
        200 => format!("subfze{} r{},r{}", o, r_s, r_a),
        202 => format!("addze{} r{},r{}", o, r_s, r_a),
        215 => format!("stbx r{},r{},r{}", r_s, r_a, r_b),
        232 => format!("subfme{} r{},r{}", o, r_s, r_a),
        234 => format!("addme{} r{},r{}", o, r_s, r_a),
        235 => format!("mullw{} r{},r{},r{}", o, r_s, r_a, r_b),
        247 => format!("stbux r{},r{},r{}", r_s, r_a, r_b),
        266 => format!("add{} r{},r{},r{}", o, r_s, r_a, r_b),
        279 => format!("lhzx r{},r{},r{}", r_s, r_a, r_b),
        284 => format!("eqv{} r{},r{},r{}", dot, r_s, r_a, r_b),
        311 => format!("lhzux r{},r{},r{}", r_s, r_a, r_b),
        316 => format!("xor{} r{},r{},r{}", dot, r_s, r_a, r_b),
        339 => match spr_num(w) {
            8 => format!("mflr r{}", r_s),
            9 => format!("mfctr r{}", r_s),
            s => format!("mfspr r{},spr 0x{:x}", r_s, s),
        },
        343 => format!("lhax r{},r{},r{}", r_s, r_a, r_b),
        375 => format!("lhaux r{},r{},r{}", r_s, r_a, r_b),
        407 => format!("sthx r{},r{},r{}", r_s, r_a, r_b),
        412 => format!("orc{} r{},r{},r{}", dot, r_s, r_a, r_b),
        439 => format!("sthux r{},r{},r{}", r_s, r_a, r_b),
        444 => format!("or{} r{},r{},r{}", dot, r_s, r_a, r_b),
        459 => format!("divwu{} r{},r{},r{}", o, r_s, r_a, r_b),
        467 => match spr_num(w) {
            8 => format!("mtlr r{}", r_s),
            9 => format!("mtctr r{}", r_s),
            s => format!("mtspr spr 0x{:x},r{}", s, r_s),
        },
        470 => format!("dcbi r{},r{}", r_a, r_b),
        476 => format!("nand{} r{},r{},r{}", dot, r_s, r_a, r_b),
        491 => format!("divw{} r{},r{},r{}", o, r_s, r_a, r_b),
        520 => format!("subfco r{},r{},r{}", r_s, r_a, r_b),
        522 => format!("addco r{},r{},r{}", r_s, r_a, r_b),
        534 => format!("lwbrx r{},r{},r{}", r_s, r_a, r_b),
        536 => format!("srw{} r{},r{},r{}", dot, r_s, r_a, r_b),
        539 => format!("srd{} r{},r{},r{}", dot, r_s, r_a, r_b),
        552 => format!("subfo r{},r{},r{}", r_s, r_a, r_b),
        598 => "sync".to_string(),
        616 => format!("nego r{},r{}", r_s, r_a),
        662 => format!("sthbrx r{},r{},r{}", r_s, r_a, r_b),
        712 => format!("subfzeo r{},r{}", r_s, r_a),
        714 => format!("addzeo r{},r{}", r_s, r_a),
        744 => format!("subfmeo r{},r{}", r_s, r_a),
        746 => format!("addmeo r{},r{}", r_s, r_a),
        747 => format!("mullwo r{},r{},r{}", r_s, r_a, r_b),
        778 => format!("addo r{},r{},r{}", r_s, r_a, r_b),
        790 => format!("lhbrx r{},r{},r{}", r_s, r_a, r_b),
        792 => format!("sraw{} r{},r{},r{}", dot, r_s, r_a, r_b),
        824 => format!("srawi{} r{},r{},{}", dot, r_a, r_s, (w >> 11) & 31),
        854 => "eieio".to_string(),
        918 => format!("stwbrx r{},r{},r{}", r_s, r_a, r_b),
        922 => format!("extsh{} r{},r{}", dot, r_s, r_a),
        954 => format!("extsb{} r{},r{}", dot, r_s, r_a),
        971 => format!("divwuo r{},r{},r{}", r_s, r_a, r_b),
        982 => format!("icbi r{},r{}", r_a, r_b),
        1003 => format!("divwo r{},r{},r{}", r_s, r_a, r_b),
        1014 => format!("dcbz r{},r{}", r_a, r_b),
        _ => format!("opcode 31 xo 0x{:x}", xo),
    }
}

/// Extract the SPR number from an mfspr/mtspr instruction. The field is
/// split: spr[0:4] = bits 11-15, spr[5:9] = bits 16-20. Verified against
/// mflr r0 = 0x7C0802A6 (LR = 8: bits 11-15 = 01000, bits 16-20 = 00000).
fn spr_num(w: u32) -> u32 {
    (((w >> 11) & 31) << 5) | ((w >> 16) & 31)
}

/// Opcode 59 (floating-point A-form, single precision): name only.
fn decode_fp_a(w: u32) -> String {
    let xo = (w >> 1) & 0x3FF;
    let name = match xo {
        18 => "fdivs",
        20 => "fsubs",
        21 => "fadds",
        25 => "fmuls",
        _ => return format!("opcode 59 xo 0x{:x}", xo),
    };
    format!(
        "{} f{},f{},f{}",
        name,
        (w >> 21) & 31,
        (w >> 16) & 31,
        (w >> 11) & 31
    )
}

/// Opcode 63 (floating-point): the common subset.
fn decode_fp(w: u32) -> String {
    let xo = (w >> 1) & 0x3FF;
    let fr_d = (w >> 21) & 31;
    let fr_a = (w >> 16) & 31;
    let fr_b = (w >> 11) & 31;
    let fr_c = (w >> 6) & 31;
    let name = match xo {
        0 => "fcmpu",
        12 => "frsp",
        14 => "fctiw",
        15 => "fctiwz",
        18 => "fdiv",
        20 => "fsub",
        21 => "fadd",
        22 => "fsqrt",
        25 => "fmul",
        28 => "fmsub",
        29 => "fmadd",
        30 => "fnmsub",
        31 => "fnmadd",
        32 => "fcmpo",
        40 => "fneg",
        72 => "fmr",
        264 => "fabs",
        _ => return format!("opcode 63 xo 0x{:x}", xo),
    };
    match name {
        "fcmpu" | "fcmpo" => format!("{} cr{},{},f{}", name, (w >> 23) & 7, fr_a, fr_b),
        "fneg" | "fabs" | "fmr" | "frsp" | "fctiw" | "fctiwz" | "fsqrt" => {
            format!("{} f{},f{}", name, fr_d, fr_b)
        }
        "fadd" | "fsub" | "fdiv" | "fmul" => format!("{} f{},f{},f{}", name, fr_d, fr_a, fr_b),
        _ => format!("{} f{},f{},f{},f{}", name, fr_d, fr_a, fr_b, fr_c),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trap_encoding_is_macsbug_word() {
        // tw LT|GT|EQ,r0,r0 = tw 0x1C, r0, r0 = 0x7F800008
        assert_eq!(encode_tw(0x1C, 0, 0), 0x7F_80_00_08);
        assert_eq!(encode_tw(28, 0, 0), 0x7F_80_00_08);
        assert_eq!(decode(0x7F_80_00_08), "tw 0x1c,r0,r0");
        // The bytes, big-endian (as stored in a PPC code section):
        let bytes = 0x7F_80_00_08u32.to_be_bytes();
        assert_eq!(bytes, [0x7F, 0x80, 0x00, 0x08]);
    }

    #[test]
    fn decode_known_words() {
        assert_eq!(decode(0x7C08_02A6), "mflr r0");
        assert_eq!(decode(0x7C08_03A6), "mtlr r0");
        assert_eq!(decode(0x9421_FFF0), "stwu r1,-16(r1)");
        assert_eq!(decode(0x4E80_0020), "blr");
        assert_eq!(decode(0x6000_0000), "nop");
        assert_eq!(decode(0x3860_0000), "li r3,0");
        assert_eq!(decode(0x4800_0005), "bl +0x4");
        assert_eq!(decode(0x9001_0008), "stw r0,8(r1)");
        assert_eq!(decode(0x8063_0000), "lwz r3,0(r3)");
        assert_eq!(decode(0x4082_0014), "bne +0x14");
        assert_eq!(decode(0x4BFF_FFE9), "bl -0x18");
        assert_eq!(decode(0x7C00_0120), "mtcrf 0x0,r0");
        assert_eq!(decode(0x7C60_F120), "mtcrf 0x18,r3");
    }

    #[test]
    fn scan_and_identify_tags() {
        // Synthetic code: [li r5,0x203][7 filler words][trap][mflr r0]
        let mut code: Vec<u8> = Vec::new();
        code.extend_from_slice(&0x38A0_0203u32.to_be_bytes()); // li r5,0x203
        for _ in 0..7 {
            code.extend_from_slice(&0x6000_0000u32.to_be_bytes()); // nop
        }
        code.extend_from_slice(&TRAP_WORD.to_be_bytes());
        code.extend_from_slice(&0x7C08_02A6u32.to_be_bytes()); // mflr r0

        let hits = scan_section(&code, 0x280);
        assert_eq!(hits.len(), 1);
        let h = &hits[0];
        assert_eq!(h.code_offset, 4 * 8);
        assert_eq!(h.container_offset, 0x280 + 4 * 8);
        assert_eq!(h.tag, Some((0x203, "T3")));
        assert_eq!(h.next_word, 0x7C08_02A6);
        assert_eq!(h.next_decode, "mflr r0");
    }

    #[test]
    fn no_false_tag_from_unrelated_li() {
        // A li with a non-tag immediate must not be identified.
        let mut code: Vec<u8> = Vec::new();
        code.extend_from_slice(&0x3860_0001u32.to_be_bytes()); // li r3,1
        code.extend_from_slice(&TRAP_WORD.to_be_bytes());
        let hits = scan_section(&code, 0);
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].tag, None);
    }

    #[test]
    fn lis_ori_tag_form() {
        // lis r4,0 + ori r4,r4,0x203 -> tag 0x203 (two-instruction form)
        let mut code: Vec<u8> = Vec::new();
        code.extend_from_slice(&0x3C80_0000u32.to_be_bytes()); // lis r4,0
        code.extend_from_slice(&0x6084_0203u32.to_be_bytes()); // ori r4,r4,0x203
        code.extend_from_slice(&TRAP_WORD.to_be_bytes());
        let hits = scan_section(&code, 0);
        assert_eq!(hits[0].tag, Some((0x203, "T3")));
    }
}
