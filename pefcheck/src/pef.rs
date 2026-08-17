//! PEF container parser (PowerPC, big-endian).
//!
//! Layouts are byte-derived from authentic CodeWarrior-built fixtures
//! (see spec/pefcheck/tasks.md); the loader info is 14 x u32.

pub const MAGIC: &[u8; 12] = b"Joy!peffpwpc";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Section {
    pub name_offset: u32,
    pub default_address: u32,
    pub total_length: u32,
    pub unpacked_length: u32,
    pub container_length: u32,
    pub container_offset: u32,
    pub kind: u8,
    pub share_kind: u8,
    pub alignment: u8,
    pub reserved_a: u8,
}

impl Section {
    pub fn kind_name(&self) -> &'static str {
        match self.kind {
            0 => "Code",
            1 => "UnpackedData",
            2 => "PackedData",
            3 => "Constant",
            4 => "Loader",
            5 => "Debug",
            6 => "ExecutableData",
            7 => "Exception",
            _ => "Unknown",
        }
    }
}

/// The PEF loader info header — a fixed 56-byte structure of 14 four-byte
/// fields (Mac OS Runtime Architectures). `mainSection`, `initSection` and
/// `termSection` are SInt32 (a value of -1 = no such symbol); all other
/// count/offset fields are UInt32. There is no SInt16 loader-header layout
/// to override — the SInt16 sectionIndex belongs to the PEF exported-symbol
/// table entry (`Export::section_index`), not to the loader header. The
/// parser keeps the raw big-endian u32 decode internally and exposes the
/// three signed fields as `i32`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LoaderInfo {
    pub main_section: i32,
    pub main_offset: u32,
    pub init_section: i32,
    pub init_offset: u32,
    pub term_section: i32,
    pub term_offset: u32,
    pub imported_library_count: u32,
    pub total_imported_symbol_count: u32,
    pub reloc_section_count: u32,
    pub reloc_instr_offset: u32,
    pub loader_strings_offset: u32,
    pub export_hash_offset: u32,
    pub export_hash_table_power: u32,
    pub exported_symbol_count: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Export {
    pub name: String,
    pub class: u8,
    pub name_offset: u32,
    pub value: u32,
    pub section_index: u16,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Container {
    pub data: Vec<u8>,
    pub container_version: u32,
    pub timestamp: u32,
    pub current_version: u32,
    pub section_count: u16,
    pub sections: Vec<Section>,
    pub loader: LoaderInfo,
    pub loader_container: Vec<u8>,
    pub exports: Vec<Export>,
}

fn need(data: &[u8], off: usize, n: usize, what: &str) -> Result<(), String> {
    if off.checked_add(n).is_none_or(|end| end > data.len()) {
        return Err(format!(
            "truncated PEF: {} needs {} bytes at 0x{:x} (file len 0x{:x})",
            what,
            n,
            off,
            data.len()
        ));
    }
    Ok(())
}

fn u16_at(data: &[u8], off: usize) -> u16 {
    u16::from_be_bytes([data[off], data[off + 1]])
}

fn u32_at(data: &[u8], off: usize) -> u32 {
    u32::from_be_bytes([data[off], data[off + 1], data[off + 2], data[off + 3]])
}

impl Container {
    pub fn parse(data: &[u8]) -> Result<Container, String> {
        if data.len() < 0x28 {
            return Err(format!(
                "file too small for a PEF container header: {} bytes",
                data.len()
            ));
        }
        if &data[0..12] != MAGIC {
            return Err(format!(
                "bad magic: expected Joy!peffpwpc, got {:02x?}",
                &data[0..12]
            ));
        }
        let container_version = u32_at(data, 0x0c);
        let timestamp = u32_at(data, 0x10);
        let current_version = u32_at(data, 0x1c);
        let section_count = u16_at(data, 0x20);
        need(data, 0x28, section_count as usize * 0x1c, "section table")?;

        let mut sections = Vec::with_capacity(section_count as usize);
        for i in 0..section_count as usize {
            let o = 0x28 + i * 0x1c;
            sections.push(Section {
                name_offset: u32_at(data, o),
                default_address: u32_at(data, o + 4),
                total_length: u32_at(data, o + 8),
                unpacked_length: u32_at(data, o + 12),
                container_length: u32_at(data, o + 16),
                container_offset: u32_at(data, o + 20),
                kind: data[o + 24],
                share_kind: data[o + 25],
                alignment: data[o + 26],
                reserved_a: data[o + 27],
            });
        }

        // Loader section (kind 4); its container is the loader info.
        let loader_sec = sections
            .iter()
            .find(|s| s.kind == 4)
            .ok_or_else(|| "no Loader section (kind 4)".to_string())?;
        let lo = loader_sec.container_offset as usize;
        let ll = loader_sec.container_length as usize;
        need(data, lo, ll, "loader container")?;
        need(data, lo, 0x38, "loader info header")?;
        let loader = LoaderInfo {
            main_section: u32_at(data, lo) as i32,
            main_offset: u32_at(data, lo + 4),
            init_section: u32_at(data, lo + 8) as i32,
            init_offset: u32_at(data, lo + 12),
            term_section: u32_at(data, lo + 16) as i32,
            term_offset: u32_at(data, lo + 20),
            imported_library_count: u32_at(data, lo + 24),
            total_imported_symbol_count: u32_at(data, lo + 28),
            reloc_section_count: u32_at(data, lo + 32),
            reloc_instr_offset: u32_at(data, lo + 36),
            loader_strings_offset: u32_at(data, lo + 40),
            export_hash_offset: u32_at(data, lo + 44),
            export_hash_table_power: u32_at(data, lo + 48),
            exported_symbol_count: u32_at(data, lo + 52),
        };

        let loader_container = data[lo..lo + ll].to_vec();
        let exports = parse_exports(data, lo, ll, &loader)?;

        Ok(Container {
            data: data.to_vec(),
            container_version,
            timestamp,
            current_version,
            section_count,
            sections,
            loader,
            loader_container,
            exports,
        })
    }

    /// Unpacked content of a PackedData (kind 2) section, if decodable.
    pub fn unpack_section(&self, s: &Section) -> Result<Vec<u8>, String> {
        let off = s.container_offset as usize;
        let len = s.container_length as usize;
        need(&self.data, off, len, "packed container")?;
        unpack_packed(&self.data[off..off + len], s.unpacked_length as usize)
    }
}

fn parse_exports(
    data: &[u8],
    lo: usize,
    ll: usize,
    loader: &LoaderInfo,
) -> Result<Vec<Export>, String> {
    if loader.exported_symbol_count == 0 {
        return Ok(Vec::new());
    }
    let power = loader.export_hash_table_power;
    // Guard absurd powers (2^31 would overflow).
    if power > 20 {
        return Err(format!(
            "implausible export hash table power {} (2^{})",
            power, power
        ));
    }
    let slots = 1usize << power;
    let keys = loader.exported_symbol_count as usize;
    let entries = loader.exported_symbol_count as usize;
    let hash_off = loader.export_hash_offset as usize;
    let table_end = hash_off
        .checked_add(slots * 4)
        .and_then(|v| v.checked_add(keys * 4))
        .and_then(|v| v.checked_add(entries * 10))
        .ok_or_else(|| "export table size overflow".to_string())?;
    if table_end > ll {
        return Err(format!(
            "export table (slots {} + keys {} + entries {}) exceeds loader container ({} > {})",
            slots, keys, entries, table_end, ll
        ));
    }
    let mut exports = Vec::with_capacity(entries);
    for i in 0..entries {
        let e = lo + hash_off + slots * 4 + keys * 4 + i * 10;
        let class_and_name = u32_at(data, e);
        let value = u32_at(data, e + 4);
        let section_index = u16_at(data, e + 8);
        let class = (class_and_name >> 24) as u8;
        let name_offset = class_and_name & 0x00ff_ffff;
        // Name is relative to the loader strings area.
        let name_at = lo
            .checked_add(loader.loader_strings_offset as usize)
            .and_then(|v| v.checked_add(name_offset as usize))
            .ok_or_else(|| "export name offset overflow".to_string())?;
        if name_at >= data.len() {
            return Err(format!(
                "export name offset 0x{:x} (strings 0x{:x} + 0x{:x}) past end of file 0x{:x}",
                name_at,
                loader.loader_strings_offset,
                name_offset,
                data.len()
            ));
        }
        let end = data[name_at..]
            .iter()
            .position(|&b| b == 0)
            .map(|p| name_at + p)
            .ok_or_else(|| "export name unterminated".to_string())?;
        if end > lo + ll {
            return Err(format!("export name {} runs past the loader container", i));
        }
        let name = String::from_utf8_lossy(&data[name_at..end]).into_owned();
        exports.push(Export {
            name,
            class,
            name_offset,
            value,
            section_index,
        });
    }
    Ok(exports)
}

/// Packed-data decompressor (Ghidra `SectionHeader.getUnpackedData` semantics).
///
/// Each stream byte: opcode = b >> 5, value = b & 0x1F; value == 0 means a
/// big-endian 7-bit varint follows (high bit = continuation, first byte most
/// significant). Ops: 0 = Zero (emit `value` zeros), 1 = Block (copy `value`
/// literal bytes), 2 = Repeat (count = varint; copy the `value`-byte block
/// count+1 times), 3 = RepeatBlock (commonSize = value, customSize = varint,
/// repeatCount = varint; emit the common block, then repeatCount times
/// [common block + a FRESH custom block], then the common block once more),
/// 4 = RepeatZero (commonSize = value, customSize = varint, repeatCount =
/// varint; repeatCount times [commonSize ZERO bytes + a fresh custom block],
/// then commonSize ZERO bytes). Opcodes 5-7 are reserved. A decode is COMPLETE
/// only when it produces exactly unpackedSize bytes AND consumes exactly the
/// packedSize bytes, ending on a valid instruction boundary (see
/// `unpack_packed_partial`).
pub fn unpack_packed(data: &[u8], out_len: usize) -> Result<Vec<u8>, String> {
    let (out, status) = unpack_packed_partial(data, out_len);
    status.map(|_| out)
}

fn grow(out: &[u8], out_len: usize, add: usize, op: &str) -> Result<(), String> {
    if out.len().saturating_add(add) > out_len {
        return Err(format!(
            "packed-data overrun: {} would exceed unpackedLength {}",
            op, out_len
        ));
    }
    Ok(())
}

/// Like `unpack_packed`, but returns the longest decodable prefix even when
/// the stream hits a reserved opcode or runs out, so a relocation simulator
/// Like `unpack_packed`, but returns the longest decodable prefix even when
/// the stream hits a reserved opcode, runs out, or does not consume the whole
/// container, so a relocation simulator can still use the bytes it did decode.
///
/// Returns `(content, Ok(()))` only on a **complete** decode: it produced
/// exactly `out_len` initialized bytes AND consumed exactly `data.len()`
/// packed bytes, ending on a valid instruction boundary. Any other outcome is
/// `(content, Err(msg))` where `content` is the decodable prefix (shorter than
/// `out_len`, or `out_len` with unconsumed trailing bytes). A partial decode is
/// not a successful reconstruction.
pub fn unpack_packed_partial(data: &[u8], out_len: usize) -> (Vec<u8>, Result<(), String>) {
    let mut out = Vec::with_capacity(out_len);
    let mut pos = 0usize;
    loop {
        if out.len() >= out_len {
            if pos == data.len() {
                return (out, Ok(()));
            }
            let got = out.len();
            return (
                out,
                Err(format!(
                    "packed stream produced unpackedSize {} but consumed only {} of {} packedSize bytes",
                    got, pos, data.len()
                )),
            );
        }
        if pos >= data.len() {
            let got = out.len();
            return (
                out,
                Err(format!(
                    "packed stream exhausted at {}/{} output bytes (stream pos {})",
                    got, out_len, pos
                )),
            );
        }
        let b = data[pos];
        pos += 1;
        let opcode = b >> 5;
        let mut value = (b & 0x1f) as usize;
        if value == 0 {
            let (v, p) = match varint(data, pos) {
                Ok(x) => x,
                Err(e) => return (out, Err(e)),
            };
            value = v;
            pos = p;
        }
        match opcode {
            0 => {
                if grow(&out, out_len, value, "Zero").is_err() {
                    return (
                        out,
                        Err(format!(
                            "packed-data overrun: Zero would exceed unpackedLength {}",
                            out_len
                        )),
                    );
                }
                out.resize(out.len() + value, 0);
            }
            1 => {
                let (block, p) = match take(data, pos, value, "Block") {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                if grow(&out, out_len, value, "Block").is_err() {
                    return (
                        out,
                        Err(format!(
                            "packed-data overrun: Block would exceed unpackedLength {}",
                            out_len
                        )),
                    );
                }
                out.extend_from_slice(block);
                pos = p;
            }
            2 => {
                let (count, p) = match varint(data, pos) {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let (block, p) = match take(data, pos, value, "Repeat") {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let n = match count
                    .checked_add(1)
                    .and_then(|c| c.checked_mul(block.len()))
                {
                    Some(x) => x,
                    None => return (out, Err("Repeat size overflow".to_string())),
                };
                if grow(&out, out_len, n, "Repeat").is_err() {
                    return (
                        out,
                        Err(format!(
                            "packed-data overrun: Repeat would exceed unpackedLength {}",
                            out_len
                        )),
                    );
                }
                if n > 0 {
                    for _ in 0..=count {
                        out.extend_from_slice(block);
                    }
                }
            }
            3 => {
                // RepeatBlock (Ghidra kPEFPkDataRepeatBlock): commonSize =
                // value, then customSize (varint), then repeatCount (varint);
                // read the commonData block once, then for each of
                // repeatCount iterations emit commonData then read+emit a
                // FRESH customData block; finally emit commonData again.
                let common_size = value;
                let (custom_size, p) = match varint(data, pos) {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let (repeat_count, p) = match varint(data, pos) {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let (common, p) = match take(data, pos, common_size, "RepeatBlock") {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let per_iter = match common_size.checked_add(custom_size) {
                    Some(x) => x,
                    None => return (out, Err("RepeatBlock size overflow".to_string())),
                };
                let n = match repeat_count
                    .checked_mul(per_iter)
                    .and_then(|v| v.checked_add(common_size))
                {
                    Some(x) => x,
                    None => return (out, Err("RepeatBlock size overflow".to_string())),
                };
                if grow(&out, out_len, n, "RepeatBlock").is_err() {
                    return (
                        out,
                        Err(format!(
                            "packed-data overrun: RepeatBlock would exceed unpackedLength {}",
                            out_len
                        )),
                    );
                }
                if n > 0 {
                    for _ in 0..repeat_count {
                        let (custom, p) = match take(data, pos, custom_size, "RepeatBlock") {
                            Ok(x) => x,
                            Err(e) => return (out, Err(e)),
                        };
                        pos = p;
                        out.extend_from_slice(common);
                        out.extend_from_slice(custom);
                    }
                    out.extend_from_slice(common);
                }
            }
            4 => {
                // RepeatZero (Ghidra kPEFPkDataRepeatZero): commonSize =
                // value, then customSize (varint), then repeatCount (varint);
                // for each of repeatCount iterations emit commonSize ZERO bytes
                // then read+emit a fresh customData block; finally emit
                // commonSize ZERO bytes. (Interleaved zero + unique data.)
                let common_size = value;
                let (custom_size, p) = match varint(data, pos) {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let (repeat_count, p) = match varint(data, pos) {
                    Ok(x) => x,
                    Err(e) => return (out, Err(e)),
                };
                pos = p;
                let per_iter = match common_size.checked_add(custom_size) {
                    Some(x) => x,
                    None => return (out, Err("RepeatZero size overflow".to_string())),
                };
                let n = match repeat_count
                    .checked_mul(per_iter)
                    .and_then(|v| v.checked_add(common_size))
                {
                    Some(x) => x,
                    None => return (out, Err("RepeatZero size overflow".to_string())),
                };
                if grow(&out, out_len, n, "RepeatZero").is_err() {
                    return (
                        out,
                        Err(format!(
                            "packed-data overrun: RepeatZero would exceed unpackedLength {}",
                            out_len
                        )),
                    );
                }
                if n > 0 {
                    for _ in 0..repeat_count {
                        out.resize(out.len() + common_size, 0);
                        let (custom, p) = match take(data, pos, custom_size, "RepeatZero") {
                            Ok(x) => x,
                            Err(e) => return (out, Err(e)),
                        };
                        pos = p;
                        out.extend_from_slice(custom);
                    }
                    out.resize(out.len() + common_size, 0);
                }
            }
            _ => {
                return (
                    out,
                    Err(format!(
                        "reserved packed-data opcode {} at stream byte {}",
                        opcode,
                        pos - 1
                    )),
                );
            }
        }
    }
}

fn varint(data: &[u8], mut pos: usize) -> Result<(usize, usize), String> {
    // Big-endian 7-bit groups with high-bit continuation (Ghidra
    // `unpackNextValue`: `unpacked <<= 7; unpacked += (value & 0x7f)`).
    let mut v = 0usize;
    let mut count = 0usize;
    loop {
        if pos >= data.len() {
            return Err("packed stream truncated in varint".to_string());
        }
        let b = data[pos];
        pos += 1;
        v = v
            .checked_mul(128)
            .and_then(|x| x.checked_add((b & 0x7f) as usize))
            .ok_or_else(|| "varint overflow".to_string())?;
        if b & 0x80 == 0 {
            return Ok((v, pos));
        }
        count += 1;
        if count > 4 {
            return Err("varint too long".to_string());
        }
    }
}

fn take<'a>(data: &'a [u8], pos: usize, n: usize, what: &str) -> Result<(&'a [u8], usize), String> {
    if pos.checked_add(n).is_none_or(|end| end > data.len()) {
        return Err(format!(
            "packed stream truncated in {}: need {} bytes at {} (stream len {})",
            what,
            n,
            pos,
            data.len()
        ));
    }
    Ok((&data[pos..pos + n], pos + n))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn unpack(b: &[u8], n: usize) -> Result<Vec<u8>, String> {
        unpack_packed(b, n)
    }

    #[test]
    fn op_zero_skip() {
        // 0x08: opcode 0, value 8 -> 8 zeros.
        assert_eq!(unpack(&[0x08], 8).unwrap(), vec![0u8; 8]);
    }

    #[test]
    fn op_zero_with_varint() {
        // 0x00 then varint 0x03 -> skip 3.
        assert_eq!(unpack(&[0x00, 0x03], 3).unwrap(), vec![0u8; 3]);
        // Multi-byte big-endian varint (Ghidra unpackNextValue: first byte is
        // the most significant 7-bit group): 0xB0 0x00 -> 48<<7 | 0 = 6144.
        let out = unpack(&[0x00, 0xb0, 0x00], 0x1800).unwrap();
        assert_eq!(out.len(), 0x1800);
        assert!(out.iter().all(|&b| b == 0));
    }

    #[test]
    fn op_block_literal() {
        // 0x21: opcode 1, value 1 -> one literal byte.
        assert_eq!(unpack(&[0x21, 0xaa], 1).unwrap(), vec![0xaa]);
    }

    #[test]
    fn op_repeat() {
        // 0x44: opcode 2, value 4; count varint 0x00 -> 1 copy of 4 bytes.
        let out = unpack(&[0x44, 0x00, 0x01, 0x02, 0x03, 0x04], 4).unwrap();
        assert_eq!(out, vec![1, 2, 3, 4]);
        // count 1 -> 2 copies (0x42 = opcode 2, value 2).
        let out = unpack(&[0x42, 0x01, 0xaa, 0xbb], 4).unwrap();
        assert_eq!(out, vec![0xaa, 0xbb, 0xaa, 0xbb]);
    }

    #[test]
    fn op_repeat_block() {
        // 0x22: opcode 3, value 2; count varint 1; gap varint 1;
        // block "ab", gap "X" -> "abX" then final "ab".
        let out = unpack(&[0x62, 0x01, 0x01, b'a', b'b', b'X'], 5).unwrap();
        assert_eq!(out, b"abXab");
    }

    #[test]
    fn op_repeat_zero() {
        // 0x82: op4, commonSize 2; customSize varint 1; repeatCount varint 2.
        // -> [00 00] [aa] [00 00] [bb] [00 00]  (interleaved zero + data).
        assert_eq!(
            unpack(&[0x82, 0x01, 0x02, 0xaa, 0xbb], 8).unwrap(),
            vec![0, 0, 0xaa, 0, 0, 0xbb, 0, 0]
        );
    }

    #[test]
    fn op_repeat_block_reads_fresh_custom_each_iteration() {
        // 0x61: op3, commonSize 1; customSize varint 1; repeatCount varint 2;
        // commonData 'A'. Each iteration emits A + a FRESH custom byte, then a
        // final A. The two custom bytes differ, proving the block is read once
        // per iteration (not reused).
        assert_eq!(
            unpack(&[0x61, 0x01, 0x02, b'A', 0x10, 0x11], 5).unwrap(),
            b"A\x10A\x11A"
        );
    }

    #[test]
    fn overrun_rejected() {
        assert!(unpack(&[0x08], 7).is_err());
    }

    #[test]
    fn truncated_rejected() {
        assert!(unpack(&[0x21], 1).is_err()); // Block needs a literal byte.
        assert!(unpack(&[0x00], 1).is_err()); // varint needs a byte.
    }

    #[test]
    fn reserved_opcode_rejected() {
        // 0xAC = opcode 5.
        assert!(unpack(&[0xac, 0x00], 8).is_err());
    }

    #[test]
    fn zero_length_repeat_with_huge_count_terminates() {
        // op2 (0x40) with value-varint 0 (empty block) and a huge count:
        // must skip the empty copy and terminate via stream exhaustion.
        let err = unpack(&[0x40, 0x00, 0xff, 0xff, 0xff, 0xff, 0x7f], 8).unwrap_err();
        assert!(err.contains("exhausted"), "{}", err);
        // op3 (0x60) with commonSize 0 and customSize 0, huge repeatCount:
        // same - empty emit, terminates via exhaustion, no hang.
        let err = unpack(&[0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x7f], 8).unwrap_err();
        assert!(err.contains("exhausted"), "{}", err);
    }

    #[test]
    fn e_series_stream_is_eight_zeros() {
        // The E1/E2a/E2b data section: packed 1 byte 0x08, unpacked 8.
        assert_eq!(unpack(&[0x08], 8).unwrap(), vec![0u8; 8]);
    }

    #[test]
    fn incomplete_decode_with_leftover_bytes_rejected() {
        // 0x08 (Zero 8) produces 8 bytes but leaves a trailing byte
        // unconsumed: a COMPLETE decode must consume exactly packedSize, so
        // unpack_packed rejects it; the partial form keeps the 8 bytes + Err.
        let (out, status) = unpack_packed_partial(&[0x08, 0xff], 8);
        assert!(status.is_err(), "leftover stream byte must be incomplete");
        assert_eq!(out.len(), 8);
        assert!(unpack(&[0x08, 0xff], 8).is_err());
    }

    #[test]
    fn tm_stream_decodes_fully_with_correct_op_semantics() {
        // The authentic TM pattern-data stream (container 0x5E0, 75 packed
        // bytes, unpacked 0xA7 = 167). The earlier "reserved opcode 5 at
        // stream byte 16" was a parser bug from wrong op3/op4 semantics: the
        // 0xAC at idx 16 is literal/custom data inside the preceding op4
        // (RepeatZero) instruction, not a reserved opcode. With the correct
        // Ghidra semantics the stream decodes fully to 167 bytes consuming all
        // 75 packed bytes; the special-main location 0x3C holds the
        // pre-relocation vector prefix 00 00 01 6c 00 00 00 00.
        let stream: Vec<u8> = vec![
            0x1e, 0x22, 0x09, 0x9c, 0x82, 0x02, 0x08, 0x00, 0xa8, 0x00, 0x44, 0x00, 0x4c, 0x09,
            0xa0, 0x00, 0xac, 0x00, 0x88, 0x00, 0x54, 0x01, 0x6c, 0x04, 0x22, 0x03, 0x5c, 0x06,
            0x22, 0x03, 0x04, 0x04, 0x23, 0xaa, 0xfe, 0x07, 0x08, 0x21, 0x01, 0x19, 0x21, 0x01,
            0x0e, 0x3f, 0x1e, 0x4f, 0x4d, 0x53, 0x54, 0x69, 0x6d, 0x65, 0x72, 0x3a, 0x20, 0x69,
            0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20, 0x73, 0x74, 0x75, 0x62, 0x20, 0x6d, 0x65,
            0x73, 0x73, 0x61, 0x67, 0x65,
        ];
        let (out, status) = unpack_packed_partial(&stream, 0xa7);
        assert!(status.is_ok(), "TM stream must fully decode: {:?}", status);
        assert_eq!(out.len(), 0xa7);
        assert_eq!(
            &out[0x3c..0x3c + 8],
            &[0x00, 0x00, 0x01, 0x6c, 0x00, 0x00, 0x00, 0x00]
        );
    }
}
