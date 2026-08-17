//! PEF relocation simulator (spec/pefcheck-reloc/tasks.md).
//!
//! Reconstructs instantiated internal pointers by applying a container's
//! relocation program to its relocated section using deterministic synthetic
//! section base addresses, then reports the bytes at the special-main
//! location and, when they form a valid PPC transition vector, resolves the
//! entry and TOC back to section+offset with alignment/bounds validation.
//!
//! Relocation instructions are big-endian 16-bit chunks (`PEFRelocChunk`);
//! the opcode lives in the high bits of the first chunk. All writes are
//! additive over 4-byte words (Ghidra `relocateMemoryAt` semantics).

use crate::pef::{unpack_packed_partial, Container, Section};

/// Deterministic synthetic base address of section 0.
pub const SYNTHETIC_BASE0: u32 = 0x1000_0000;

/// Deterministic synthetic section base addresses: contiguous, 16-byte
/// aligned, starting at `SYNTHETIC_BASE0`. Never derived from Ghidra.
pub fn synthetic_bases(c: &Container) -> Vec<u32> {
    let mut bases = Vec::with_capacity(c.sections.len());
    let mut cur: u64 = SYNTHETIC_BASE0 as u64;
    for s in &c.sections {
        bases.push(cur as u32);
        cur += align16(s.total_length as u64);
    }
    bases
}

fn align16(v: u64) -> u64 {
    (v + 15) & !15
}

/// A relocation that targets an external imported symbol; the simulator
/// cannot resolve its runtime address and leaves the word unchanged.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportFixup {
    pub offset: usize,
    pub symbol: String,
}

/// Result of relocating one section.
#[derive(Debug, Clone)]
pub struct RelocResult {
    pub section_index: usize,
    /// Relocated content: the decoded prefix (length may be < unpackedLength
    /// when the packed stream is partial).
    pub content: Vec<u8>,
    /// `Ok(())` = **complete** packed-data decode (exact unpackedSize output
    /// AND exact packedSize consumed); `Err(msg)` = partial (reserved opcode,
    /// exhaustion, or unconsumed trailing bytes), with `content` holding the
    /// decodable prefix.
    pub decode_status: Result<(), String>,
    /// True iff every relocCount 16-bit block was consumed exactly.
    pub reloc_complete: bool,
    /// True iff the packed-data decode AND the relocation replay are both
    /// complete. Only then is a reconstructed vector fully VALID; otherwise it
    /// is provisional evidence.
    pub complete: bool,
    pub import_fixups: Vec<ImportFixup>,
    pub notes: Vec<String>,
}

/// A pointer resolved back to a section + in-section offset.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ResolvedPointer {
    pub word: u32,
    /// `usize::MAX` when `word` does not fall in any section's range.
    pub section_index: usize,
    pub offset: u32,
}

/// Outcome of decoding the 8 bytes at the special-main location.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VectorStatus {
    /// The decode AND relocation replay are complete, and word0/word1 form a
    /// valid PPC transition vector (non-zero, 4-byte aligned); both resolved.
    Valid {
        entry: ResolvedPointer,
        toc: ResolvedPointer,
    },
    /// Reconstructed from a PARTIAL decode or relocation replay: shown as
    /// provisional evidence only, never labelled fully valid, and never used
    /// to strengthen the overall PASS verdict.
    Provisional {
        word0: u32,
        word1: u32,
        reason: String,
    },
    /// Bytes were readable, decode was complete, but they do not form an
    /// identifiable vector.
    NotAVector { word0: u32, word1: u32 },
    /// The special-main location is beyond the decoded prefix.
    BeyondDecodedPrefix,
}

/// Full post-relocation analysis of the special-main location.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VectorAnalysis {
    pub section_index: usize,
    pub main_offset: u32,
    /// True iff the packed-data decode AND the relocation replay are both
    /// complete. Only then is the vector reconstruction fully valid.
    pub complete: bool,
    /// The 8 raw bytes at the special-main location, when readable.
    pub bytes: Option<[u8; 8]>,
    pub status: VectorStatus,
    pub decode_status: Result<(), String>,
    pub import_fixups: Vec<ImportFixup>,
    pub notes: Vec<String>,
}

// ---- loader table parsing (library/symbol/relocation headers) ------------

fn need(buf: &[u8], off: usize, n: usize, what: &str) -> Result<(), String> {
    if off.checked_add(n).is_none_or(|end| end > buf.len()) {
        return Err(format!(
            "truncated loader: {} needs {} bytes at 0x{:x}",
            what, n, off
        ));
    }
    Ok(())
}

fn u16_at(buf: &[u8], off: usize) -> u16 {
    u16::from_be_bytes([buf[off], buf[off + 1]])
}
fn u32_at(buf: &[u8], off: usize) -> u32 {
    u32::from_be_bytes([buf[off], buf[off + 1], buf[off + 2], buf[off + 3]])
}

fn read_cstr(buf: &[u8], at: usize) -> Result<String, String> {
    need(buf, at, 1, "import name")?;
    let end = buf[at..]
        .iter()
        .position(|&b| b == 0)
        .map(|p| at + p)
        .ok_or_else(|| "unterminated import name".to_string())?;
    Ok(String::from_utf8_lossy(&buf[at..end]).into_owned())
}

struct RelocHeader {
    section_index: u16,
    chunks: Vec<u16>,
}

fn parse_reloc_headers(c: &Container) -> Result<Vec<RelocHeader>, String> {
    let lc = &c.loader_container;
    let libs = c.loader.imported_library_count as usize;
    let syms = c.loader.total_imported_symbol_count as usize;
    let rsc = c.loader.reloc_section_count as usize;
    // Loader info header (56) + imported library table (24 each) + imported
    // symbol table (4 each) precede the relocation headers.
    let mut pos = 56usize;
    pos = pos
        .checked_add(libs * 24)
        .ok_or_else(|| "loader table overflow".to_string())?;
    pos = pos
        .checked_add(syms * 4)
        .ok_or_else(|| "loader table overflow".to_string())?;
    let rinstr = c.loader.reloc_instr_offset as usize;
    let mut headers = Vec::with_capacity(rsc);
    for _ in 0..rsc {
        need(lc, pos, 12, "relocation header")?;
        let section_index = u16_at(lc, pos);
        let _reserved = u16_at(lc, pos + 2);
        let rcount = u32_at(lc, pos + 4) as usize;
        let first = u32_at(lc, pos + 8) as usize;
        pos += 12;
        // Each header's chunks begin `first` bytes into the relocation area.
        let base = rinstr
            .checked_add(first)
            .ok_or_else(|| "relocation chunk offset overflow".to_string())?;
        need(lc, base, rcount * 2, "relocation chunks")?;
        let mut chunks = Vec::with_capacity(rcount);
        for k in 0..rcount {
            chunks.push(u16_at(lc, base + k * 2));
        }
        headers.push(RelocHeader {
            section_index,
            chunks,
        });
    }
    Ok(headers)
}

fn parse_import_names(c: &Container) -> Result<Vec<String>, String> {
    let lc = &c.loader_container;
    let libs = c.loader.imported_library_count as usize;
    let syms = c.loader.total_imported_symbol_count as usize;
    let sstr = c.loader.loader_strings_offset as usize;
    let mut pos = 56usize
        .checked_add(libs * 24)
        .ok_or_else(|| "loader table overflow".to_string())?;
    let mut names = Vec::with_capacity(syms);
    for _ in 0..syms {
        need(lc, pos, 4, "imported symbol")?;
        let v = u32_at(lc, pos);
        let name_off = (v & 0x00ff_ffff) as usize;
        names.push(read_cstr(lc, sstr + name_off)?);
        pos += 4;
    }
    Ok(names)
}

// ---- executor -------------------------------------------------------------

fn add_word(content: &mut [u8], addr: &mut u64, val: u32, notes: &mut Vec<String>) {
    let a = *addr as usize;
    if a + 4 <= content.len() {
        let cur = u32::from_be_bytes([content[a], content[a + 1], content[a + 2], content[a + 3]]);
        content[a..a + 4].copy_from_slice(&cur.wrapping_add(val).to_be_bytes());
    } else {
        notes.push(format!(
            "relocation write at offset 0x{:x} beyond decoded content (len 0x{:x})",
            a,
            content.len()
        ));
    }
    *addr += 4;
}

fn base_of(bases: &[u32], idx: usize, notes: &mut Vec<String>) -> u32 {
    bases.get(idx).copied().unwrap_or_else(|| {
        notes.push(format!("section index {} out of range for base", idx));
        0
    })
}

fn record_import(import_names: &[String], idx: usize, addr: u64, fixups: &mut Vec<ImportFixup>) {
    let symbol = import_names
        .get(idx)
        .cloned()
        .unwrap_or_else(|| format!("<import {}>", idx));
    fixups.push(ImportFixup {
        offset: addr as usize,
        symbol,
    });
}

fn apply_program(
    chunks: &[u16],
    bases: &[u32],
    import_names: &[String],
    content: &mut [u8],
    import_fixups: &mut Vec<ImportFixup>,
    notes: &mut Vec<String>,
) -> bool {
    // Returns true iff every relocCount 16-bit chunk was consumed exactly
    // (the loop index ends == chunks.len()) AND no instruction was left
    // unapplied (undefined opcode or an un-replayed repeat), i.e. the replay
    // is COMPLETE.
    let mut complete = true;
    let mut addr: u64 = 0;
    let mut sect_c = bases.first().copied().unwrap_or(0);
    let mut sect_d = bases.get(1).copied().unwrap_or(0);
    let mut imp = 0usize;
    let mut i = 0usize;
    while i < chunks.len() {
        let ch = chunks[i];
        let v = ch as u32;
        if v & 0xc000 == 0 {
            // RelocBySectDWithSkip: skip skip*4 bytes, then write sectionD.
            let skip = ((v & 0x3fc0) >> 6) * 4;
            let n = v & 0x3f;
            addr += skip as u64;
            for _ in 0..n {
                add_word(content, &mut addr, sect_d, notes);
            }
        } else if (v & 0xe000) >> 13 == 0x2 {
            // RelocValueGroup.
            let sub = (v & 0x1e00) >> 9;
            let run = (v & 0x01ff) + 1;
            match sub {
                0 => {
                    for _ in 0..run {
                        add_word(content, &mut addr, sect_c, notes);
                    }
                }
                1 => {
                    for _ in 0..run {
                        add_word(content, &mut addr, sect_d, notes);
                    }
                }
                2 => {
                    // TVector12: sectionC, sectionD, then 4-byte gap.
                    for _ in 0..run {
                        add_word(content, &mut addr, sect_c, notes);
                        add_word(content, &mut addr, sect_d, notes);
                        addr += 4;
                    }
                }
                3 => {
                    // TVector8: adjacent sectionC + sectionD words (8-byte step).
                    for _ in 0..run {
                        add_word(content, &mut addr, sect_c, notes);
                        add_word(content, &mut addr, sect_d, notes);
                    }
                }
                4 => {
                    // VTable8: sectionD, then 4-byte gap.
                    for _ in 0..run {
                        add_word(content, &mut addr, sect_d, notes);
                        addr += 4;
                    }
                }
                5 => {
                    for _ in 0..run {
                        record_import(import_names, imp, addr, import_fixups);
                        addr += 4;
                        imp += 1;
                    }
                }
                _ => notes.push(format!("unsupported RelocValueGroup subopcode {}", sub)),
            }
        } else if (v & 0xe000) >> 13 == 0x3 {
            // RelocByIndexGroup.
            let sub = (v & 0x1e00) >> 9;
            let idx = (v & 0x01ff) as usize;
            match sub {
                0 => {
                    record_import(import_names, idx, addr, import_fixups);
                    addr += 4;
                    imp = idx + 1;
                }
                1 => sect_c = base_of(bases, idx, notes),
                2 => sect_d = base_of(bases, idx, notes),
                3 => {
                    let val = base_of(bases, idx, notes);
                    add_word(content, &mut addr, val, notes);
                }
                _ => notes.push(format!("unsupported RelocByIndexGroup subopcode {}", sub)),
            }
        } else if (v & 0xf000) >> 12 == 0x8 {
            addr += (v & 0x0fff) as u64 + 1; // RelocIncrPosition.
        } else if (v & 0xf000) >> 12 == 0x9 {
            notes.push(format!(
                "RelocSmRepeat (repeat {} chunks, {} times) not replayed; not used by the project fixtures",
                ((v & 0x0f00) >> 8) + 1,
                (v & 0x00ff) + 1
            ));
            complete = false; // repeat not replayed -> replay is PARTIAL.
        } else if (v & 0xfc00) >> 10 == 0x29 {
            i += 1; // second chunk
            let next = *chunks.get(i).unwrap_or(&0) as u32;
            let idx = (((v & 0x03ff) << 16) | next) as usize;
            record_import(import_names, idx, addr, import_fixups);
            addr += 4;
            imp = idx + 1;
        } else if (v & 0xfc00) >> 10 == 0x28 {
            i += 1; // second chunk
            let next = *chunks.get(i).unwrap_or(&0) as u32;
            addr = (((v & 0x03ff) << 16) | next) as u64; // RelocSetPosition.
        } else if (v & 0xfc00) >> 10 == 0x2c {
            notes.push(format!(
                "RelocLgRepeat (repeat {} chunks, {} times) not replayed; not used by the project fixtures",
                ((v & 0x03c0) >> 6) + 1,
                ((v & 0x003f) << 16) | (*chunks.get(i + 1).unwrap_or(&0) as u32)
            ));
            complete = false; // repeat not replayed -> replay is PARTIAL.
            i += 1; // second chunk
        } else if (v & 0xfc00) >> 10 == 0x2d {
            i += 1; // second chunk
            let next = *chunks.get(i).unwrap_or(&0) as u32;
            let sub = (v & 0x03c0) >> 6;
            let idx = (((v & 0x003f) << 16) | next) as usize;
            match sub {
                0 => {
                    let val = base_of(bases, idx, notes);
                    add_word(content, &mut addr, val, notes);
                }
                1 => sect_c = base_of(bases, idx, notes),
                2 => sect_d = base_of(bases, idx, notes),
                _ => notes.push(format!(
                    "unsupported RelocLgSetOrBySection subopcode {}",
                    sub
                )),
            }
        } else {
            notes.push(format!("undefined relocation opcode 0x{:04x}", ch));
            complete = false; // undefined opcode -> replay is PARTIAL.
        }
        i += 1;
    }
    complete && i == chunks.len()
}

// ---- public API -----------------------------------------------------------

/// Apply the relocation program for `section_index` to its content.
pub fn relocate_section(c: &Container, section_index: usize) -> Result<RelocResult, String> {
    if section_index >= c.sections.len() {
        return Err(format!("section {} out of range", section_index));
    }
    let headers = parse_reloc_headers(c)?;
    let header = headers
        .iter()
        .find(|h| h.section_index as usize == section_index)
        .ok_or_else(|| format!("no relocation program for section {}", section_index))?;
    let s = &c.sections[section_index];

    let (mut content, decode_status) = if s.kind == 2 {
        let off = s.container_offset as usize;
        let len = s.container_length as usize;
        need(&c.data, off, len, "packed container")?;
        unpack_packed_partial(&c.data[off..off + len], s.unpacked_length as usize)
    } else {
        // Non-packed sections relocate their raw container bytes directly.
        let off = s.container_offset as usize;
        let len = s.container_length as usize;
        need(&c.data, off, len, "section container")?;
        (c.data[off..off + len].to_vec(), Ok(()))
    };

    let bases = synthetic_bases(c);
    let import_names = parse_import_names(c)?;
    let mut import_fixups = Vec::new();
    let mut notes = Vec::new();
    let reloc_complete = apply_program(
        &header.chunks,
        &bases,
        &import_names,
        &mut content,
        &mut import_fixups,
        &mut notes,
    );

    let complete = decode_status.is_ok() && reloc_complete;
    Ok(RelocResult {
        section_index,
        content,
        decode_status,
        reloc_complete,
        complete,
        import_fixups,
        notes,
    })
}

fn resolve_pointer(bases: &[u32], sections: &[Section], word: u32) -> ResolvedPointer {
    for (i, s) in sections.iter().enumerate() {
        let base = bases.get(i).copied().unwrap_or(0);
        if word >= base {
            let off = word - base;
            if off < s.total_length {
                return ResolvedPointer {
                    word,
                    section_index: i,
                    offset: off,
                };
            }
        }
    }
    ResolvedPointer {
        word,
        section_index: usize::MAX,
        offset: word,
    }
}

/// Relocate the section the special main points into and analyze the 8 bytes
/// at the special-main location. Returns `None` when there is no special
/// main or that section has no relocation program.
pub fn special_main_vector(c: &Container) -> Option<VectorAnalysis> {
    let ms = c.loader.main_section;
    if ms < 0 {
        return None;
    }
    let ms = ms as usize;
    if ms >= c.sections.len() {
        return None;
    }
    let result = relocate_section(c, ms).ok()?;
    let main_offset = c.loader.main_offset;
    let a = main_offset as usize;

    let notes = result.notes;
    let complete = result.complete;
    let bytes = if a + 8 <= result.content.len() {
        let mut b = [0u8; 8];
        b.copy_from_slice(&result.content[a..a + 8]);
        Some(b)
    } else {
        None
    };

    let status = match bytes {
        None => VectorStatus::BeyondDecodedPrefix,
        Some(b) => {
            let w0 = u32::from_be_bytes([b[0], b[1], b[2], b[3]]);
            let w1 = u32::from_be_bytes([b[4], b[5], b[6], b[7]]);
            let is_vector = w0 != 0 && w0 % 4 == 0 && w1 != 0 && w1 % 4 == 0;
            if !complete {
                // Provisional: the bytes look vector-like but were
                // reconstructed from a partial decode and/or a partial
                // relocation replay. It must not be labelled fully valid and
                // must not strengthen the overall PASS verdict.
                let reason = match (&result.decode_status, result.reloc_complete) {
                    (Err(e), _) => format!("partial packed-data decode: {}", e),
                    (Ok(()), false) => {
                        "partial relocation replay (not all relocCount blocks consumed)".to_string()
                    }
                    _ => "partial reconstruction".to_string(),
                };
                VectorStatus::Provisional {
                    word0: w0,
                    word1: w1,
                    reason,
                }
            } else if is_vector {
                let bases = synthetic_bases(c);
                let entry = resolve_pointer(&bases, &c.sections, w0);
                let toc = resolve_pointer(&bases, &c.sections, w1);
                VectorStatus::Valid { entry, toc }
            } else {
                VectorStatus::NotAVector {
                    word0: w0,
                    word1: w1,
                }
            }
        }
    };

    Some(VectorAnalysis {
        section_index: ms,
        main_offset,
        complete,
        bytes,
        status,
        decode_status: result.decode_status,
        import_fixups: result.import_fixups,
        notes,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(
        chunks: &[u16],
        bases: &[u32],
        content_len: usize,
    ) -> (Vec<u8>, Vec<ImportFixup>, Vec<String>) {
        let mut content = vec![0u8; content_len];
        let mut fixups = Vec::new();
        let mut notes = Vec::new();
        let imports: Vec<String> = vec!["A".into(), "B".into()];
        apply_program(
            chunks,
            bases,
            &imports,
            &mut content,
            &mut fixups,
            &mut notes,
        );
        (content, fixups, notes)
    }

    #[test]
    fn long_set_position_is_reachable() {
        // 0xA000/0x0010 = RelocSetPosition to offset 0x10, then 0x4200 = BySectD.
        let bases = [0x1000_0000u32, 0x1000_0010u32];
        let (content, _, _) = run(&[0xA000, 0x0010, 0x4200], &bases, 32);
        assert_eq!(&content[0x10..0x14], &[0x10, 0x00, 0x00, 0x10]);
    }

    #[test]
    fn long_lg_by_import_is_reachable() {
        // 0xA400/0x0000 = RelocLgByImport index 0.
        let bases = [0x1000_0000u32, 0x1000_0010u32];
        let (_, fixups, _) = run(&[0xA400, 0x0000], &bases, 16);
        assert_eq!(fixups.len(), 1);
        assert_eq!(fixups[0].symbol, "A");
        assert_eq!(fixups[0].offset, 0);
    }

    #[test]
    fn incr_position_advances() {
        // 0x800B = RelocIncrPosition +12, then 0x4200 = BySectD at offset 12.
        let bases = [0x1000_0000u32, 0x1000_0010u32];
        let (content, _, _) = run(&[0x800B, 0x4200], &bases, 32);
        assert_eq!(&content[12..16], &[0x10, 0x00, 0x00, 0x10]);
    }

    #[test]
    fn by_sect_d_with_skip() {
        // 0x0041 = skip 1 word then relocate 1 word with sectionD.
        let bases = [0x1000_0000u32, 0x1000_0010u32];
        let (content, _, _) = run(&[0x0041], &bases, 16);
        assert_eq!(&content[0..4], &[0, 0, 0, 0]);
        assert_eq!(&content[4..8], &[0x10, 0x00, 0x00, 0x10]);
    }

    #[test]
    fn vtable8_steps_eight() {
        // 0x4800 = VTable8 x1 (writes sectionD, advances 8), then 0x4200 = BySectD.
        let bases = [0x1000_0000u32, 0x1000_0010u32];
        let (content, _, _) = run(&[0x4800, 0x4200], &bases, 32);
        assert_eq!(&content[0..4], &[0x10, 0x00, 0x00, 0x10]);
        assert_eq!(&content[8..12], &[0x10, 0x00, 0x00, 0x10]);
    }

    #[test]
    fn by_index_group_subs() {
        // 0x6602 = SmBySection idx 2 -> write base[2] at offset 0.
        let bases = [0x1000_0000u32, 0x1000_0010u32, 0x1000_0020u32];
        let (content, _, _) = run(&[0x6602], &bases, 16);
        assert_eq!(&content[0..4], &[0x10, 0x00, 0x00, 0x20]);
        // 0x6000 = SmByImport idx 0 -> external fixup.
        let (_, fixups, _) = run(&[0x6000], &bases, 16);
        assert_eq!(fixups[0].symbol, "A");
        assert_eq!(fixups[0].offset, 0);
    }

    #[test]
    fn sm_repeat_notes_not_replayed() {
        let bases = [0x1000_0000u32];
        let (_, _, notes) = run(&[0x9000], &bases, 16);
        assert!(
            notes.iter().any(|n| n.contains("RelocSmRepeat")),
            "{:?}",
            notes
        );
    }

    #[test]
    fn lg_set_or_by_section_is_reachable() {
        // 0xB400/0x0001 = LgSetOrBySection sub=0 (LgBySection) idx=1 -> write base[1].
        let bases = [0x1000_0000u32, 0x1000_0010u32, 0x1000_0020u32];
        let (content, _, _) = run(&[0xB400, 0x0001], &bases, 16);
        assert_eq!(&content[0..4], &[0x10, 0x00, 0x00, 0x10]);
        // 0xB440/0x0001 = sub=1 (LgSetSectC) idx=1 -> sectC = base[1]; then 0x4200 BySectD.
        let (content, _, _) = run(&[0xB440, 0x0001, 0x4200], &bases, 16);
        // sectC is now base[1]=0x10000010... BySectD writes sectD=base[1]=0x10000010.
        assert_eq!(&content[0..4], &[0x10, 0x00, 0x00, 0x10]);
    }

    #[test]
    fn lg_repeat_notes_not_replayed() {
        let bases = [0x1000_0000u32];
        let (_, _, notes) = run(&[0xB000, 0x0000], &bases, 16);
        assert!(
            notes.iter().any(|n| n.contains("RelocLgRepeat")),
            "{:?}",
            notes
        );
    }

    #[test]
    fn incomplete_relocation_replay_is_reported() {
        // A 2-chunk LgByImport (0xA400) as the last chunk with NO second
        // chunk: apply_program must report incomplete (all relocCount blocks
        // were not consumed exactly).
        let bases = [0x1000_0000u32];
        let mut content = vec![0u8; 8];
        let mut fixups = Vec::new();
        let mut notes = Vec::new();
        let imports = vec!["A".to_string()];
        let complete = apply_program(
            &[0xA400],
            &bases,
            &imports,
            &mut content,
            &mut fixups,
            &mut notes,
        );
        assert!(!complete, "a truncated 2-chunk op must be incomplete");
    }
}
