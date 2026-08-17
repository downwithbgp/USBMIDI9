//! Validation rules and the mechanical report (spec/pefcheck/tasks.md).

use crate::pef::{Container, Section};
use crate::reloc;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MainTarget {
    pub section_index: u32,
    pub offset: u32,
    pub kind: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VectorInfo {
    pub word0: u32,
    pub word1: u32,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Report {
    pub errors: Vec<String>,
    pub notes: Vec<String>,
    pub main_target: Option<MainTarget>,
    /// 8 raw bytes at the special-main target (when the main section is
    /// non-code and the bytes are readable).
    pub target_bytes: Option<Vec<u8>>,
    pub vector: Option<VectorInfo>,
    /// Post-relocation special-main analysis from the relocation simulator
    /// (informational; never flips a verdict).
    pub reloc_vector: Option<reloc::VectorAnalysis>,
    pub reloc_stream: Option<Vec<u8>>,
}

impl Report {
    pub fn valid(&self) -> bool {
        self.errors.is_empty()
    }
}

/// The PEF code-section container-alignment rule (E2b violated it with
/// containerOffset 0xE2): code sections must be >= 16-byte aligned in the
/// container.
const CODE_CONTAINER_ALIGNMENT: u32 = 16;

pub fn validate(c: &Container) -> Report {
    let mut r = Report::default();

    // Rule 1/2: section table already bounds-checked at parse time; per
    // section: container bounds + alignment.
    for (i, s) in c.sections.iter().enumerate() {
        let end = (s.container_offset as u64).saturating_add(s.container_length as u64);
        if end > c.data.len() as u64 {
            r.errors.push(format!(
                "section {} ({}) container [0x{:x}, 0x{:x}) exceeds file size 0x{:x}",
                i,
                s.kind_name(),
                s.container_offset,
                end,
                c.data.len()
            ));
        }
        // Rule 3 (checked before the generic alignment rule for code
        // sections, so E2b reports the code-offset error specifically).
        if s.kind == 0 && s.container_offset % CODE_CONTAINER_ALIGNMENT != 0 {
            r.errors.push(format!(
                "code section {} containerOffset 0x{:x} is not {}-byte aligned \
                 (0x{:x} % {} = {})",
                i,
                s.container_offset,
                CODE_CONTAINER_ALIGNMENT,
                s.container_offset,
                CODE_CONTAINER_ALIGNMENT,
                s.container_offset % CODE_CONTAINER_ALIGNMENT
            ));
        }
        // Rule 2: generic alignment byte (clamped: only meaningful for
        // 2^alignment <= 8192, per the spec's field semantics).
        let align = s.alignment;
        if align <= 13 {
            let required = 1u64 << align;
            if !(s.container_offset as u64).is_multiple_of(required) {
                r.errors.push(format!(
                    "section {} ({}) containerOffset 0x{:x} not aligned to 2^{} = {}",
                    i,
                    s.kind_name(),
                    s.container_offset,
                    align,
                    required
                ));
            }
        }
    }

    // Rule 5: loader offsets within the loader container.
    let lc_len = c.loader_container.len();
    let loader_rel = |off: u32, n: usize, what: &str| -> Result<(), String> {
        let end = (off as usize)
            .checked_add(n)
            .ok_or_else(|| format!("{} offset overflow", what))?;
        if end > lc_len {
            return Err(format!(
                "{} (0x{:x} + {} = 0x{:x}) exceeds the loader container (0x{:x})",
                what, off, n, end, lc_len
            ));
        }
        Ok(())
    };
    if let Err(e) = loader_rel(c.loader.reloc_instr_offset, 0, "relocInstrOffset") {
        r.errors.push(e);
    }
    if let Err(e) = loader_rel(c.loader.loader_strings_offset, 1, "loaderStringsOffset") {
        r.errors.push(e);
    }
    if let Err(e) = loader_rel(c.loader.export_hash_offset, 1, "exportHashOffset") {
        r.errors.push(e);
    }
    if c.loader.export_hash_table_power > 20 {
        r.errors.push(format!(
            "implausible exportHashTablePower {}",
            c.loader.export_hash_table_power
        ));
    }

    // Special main (mechanical, rule 7).
    let ms = c.loader.main_section;
    if ms == -1 {
        r.notes
            .push("loader mainSection = -1 (no main symbol)".to_string());
    } else if ms < 0 || ms as usize >= c.sections.len() {
        r.errors.push(format!(
            "mainSection {} out of range ({} sections)",
            ms,
            c.sections.len()
        ));
    } else {
        let s = &c.sections[ms as usize];
        let target = MainTarget {
            section_index: ms as u32,
            offset: c.loader.main_offset,
            kind: s.kind,
        };
        r.main_target = Some(target);
        if s.kind == 0 {
            // Rule 4: PPC instruction-address 4-byte alignment.
            let addr = (s.default_address as u64) + (c.loader.main_offset as u64);
            if !addr.is_multiple_of(4) {
                r.errors.push(format!(
                    "special main address 0x{:x} (code + 0x{:x}) is not 4-byte aligned",
                    addr, c.loader.main_offset
                ));
            }
        } else {
            // Attempt the transition-vector decode at content + offset.
            let content: Result<Vec<u8>, String> = if s.kind == 2 {
                c.unpack_section(s)
            } else {
                let off = s.container_offset as usize;
                let len = s.container_length as usize;
                if off.checked_add(len).is_none_or(|e| e > c.data.len()) {
                    Err(format!("section {} container out of bounds", ms))
                } else {
                    Ok(c.data[off..off + len].to_vec())
                }
            };
            match content {
                Ok(content) => {
                    let off = c.loader.main_offset as usize;
                    if off.checked_add(8).is_none_or(|e| e > content.len()) {
                        r.notes.push(format!(
                            "special main target (section {} + 0x{:x}) shorter than 8 bytes",
                            ms, c.loader.main_offset
                        ));
                    } else {
                        let bytes = content[off..off + 8].to_vec();
                        r.target_bytes = Some(bytes.clone());
                        let w0 = u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
                        let w1 = u32::from_be_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]);
                        // A stored transition vector needs BOTH words non-zero
                        // and 4-byte aligned (entry + TOC). A zero word1 (or
                        // word0) means the container does not store a vector.
                        if w0 != 0 && w0 % 4 == 0 && w1 != 0 && w1 % 4 == 0 {
                            r.vector = Some(VectorInfo {
                                word0: w0,
                                word1: w1,
                            });
                        } else {
                            r.notes.push(format!(
                                "raw special-main bytes {:02x?} are pre-relocation contents \
                                 (zero/stub); this does not establish the vector is absent \
                                 from the PEF representation - CFM relocation materializes \
                                 pointer values at preparation time",
                                bytes
                            ));
                        }
                    }
                }
                Err(e) => r.notes.push(format!(
                    "main target content not decodable: {} (informational)",
                    e
                )),
            }
        }
        // Post-relocation special-main analysis (relocation simulator).
        // Informational only — never flips a verdict.
        if s.kind != 0 {
            r.reloc_vector = reloc::special_main_vector(c);
            if let Some(rv) = &r.reloc_vector {
                if let Err(e) = &rv.decode_status {
                    r.notes.push(format!(
                        "relocated section {} decode: {}",
                        rv.section_index, e
                    ));
                }
                for imp in &rv.import_fixups {
                    r.notes.push(format!(
                        "external import fixup at section {} + 0x{:x} -> {}",
                        rv.section_index, imp.offset, imp.symbol
                    ));
                }
                r.notes.extend(rv.notes.iter().cloned());
            }
        }
    }

    // Relocation stream presence (informational).
    if c.loader.reloc_section_count > 0 {
        let off = c.loader.reloc_instr_offset as usize;
        let avail = lc_len.saturating_sub(off);
        let n = avail.min(16);
        if n > 0 {
            r.reloc_stream = Some(c.loader_container[off..off + n].to_vec());
        }
    }

    r
}

pub fn section_summary(s: &Section) -> String {
    format!(
        "{} @0x{:x} len={} unpacked={} total={} align={} default=0x{:x}",
        s.kind_name(),
        s.container_offset,
        s.container_length,
        s.unpacked_length,
        s.total_length,
        s.alignment,
        s.default_address
    )
}
