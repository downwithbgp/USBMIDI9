//! pefcheck — mechanical structural checker for PowerPC PEF containers.
//!
//! Usage: pefcheck [--trapcheck [--expect N]] <file>...
//! Exit: 0 = all files PASS, 1 = any file INVALID / trap mismatch,
//!       2 = parse error only.

use pefcheck::pef;
use pefcheck::reloc;
use pefcheck::sha256;
use pefcheck::trapcheck;
use pefcheck::validate;

use std::process::ExitCode;

fn main() -> ExitCode {
    let mut trapcheck_mode = false;
    let mut expect: Option<usize> = None;
    let mut files: Vec<String> = Vec::new();
    let mut args = std::env::args().skip(1).peekable();
    while let Some(a) = args.next() {
        match a.as_str() {
            "--trapcheck" => trapcheck_mode = true,
            "--expect" => {
                let n = match args.next() {
                    Some(n) => n,
                    None => {
                        eprintln!("--expect requires a value");
                        return ExitCode::from(2);
                    }
                };
                expect = match n.parse::<usize>() {
                    Ok(v) => Some(v),
                    Err(_) => {
                        eprintln!("invalid --expect value: {}", n);
                        return ExitCode::from(2);
                    }
                };
            }
            _ => {
                if let Some(n) = a.strip_prefix("--expect=") {
                    expect = match n.parse::<usize>() {
                        Ok(v) => Some(v),
                        Err(_) => {
                            eprintln!("invalid --expect value: {}", n);
                            return ExitCode::from(2);
                        }
                    };
                } else {
                    files.push(a);
                }
            }
        }
    }
    if files.is_empty() {
        eprintln!("usage: pefcheck [--trapcheck [--expect N]] <pef-file>...");
        return ExitCode::from(2);
    }

    if trapcheck_mode {
        return trapcheck_run(&files, expect);
    }

    let mut any_invalid = false;
    let mut any_parse_error = false;
    for path in &files {
        let data = match std::fs::read(path) {
            Ok(d) => d,
            Err(e) => {
                eprintln!("{}: cannot read: {}", path, e);
                any_parse_error = true;
                continue;
            }
        };
        println!(
            "=== {} ({} bytes, sha256 {}) ===",
            path,
            data.len(),
            sha256::hex(&sha256::sha256(&data))
        );
        match pef::Container::parse(&data) {
            Err(e) => {
                println!("PARSE ERROR: {}", e);
                any_parse_error = true;
            }
            Ok(c) => {
                let r = validate::validate(&c);
                print_report(&c, &r);
                if r.valid() {
                    println!("VERDICT: PASS");
                } else {
                    println!("VERDICT: INVALID");
                    any_invalid = true;
                }
            }
        }
        println!();
    }

    if any_invalid {
        ExitCode::from(1)
    } else if any_parse_error {
        ExitCode::from(2)
    } else {
        ExitCode::from(0)
    }
}

fn print_report(c: &pef::Container, r: &validate::Report) {
    println!(
        "containerVersion={} timestamp=0x{:08x} currentVersion=0x{:08x} sections={}",
        c.container_version, c.timestamp, c.current_version, c.section_count
    );
    for (i, s) in c.sections.iter().enumerate() {
        println!("  section {}: {}", i, validate::section_summary(s));
    }
    let l = &c.loader;
    println!(
        "  loader: mainSection={} mainOffset=0x{:x} initSection={} initOffset=0x{:x} \
         termSection={} termOffset=0x{:x}",
        l.main_section, l.main_offset, l.init_section, l.init_offset, l.term_section, l.term_offset
    );
    println!(
        "  loader: libs={} imports={} relocSections={} relocInstrOffset=0x{:x} \
         stringsOffset=0x{:x} exportHashOffset=0x{:x} power={} exports={}",
        l.imported_library_count,
        l.total_imported_symbol_count,
        l.reloc_section_count,
        l.reloc_instr_offset,
        l.loader_strings_offset,
        l.export_hash_offset,
        l.export_hash_table_power,
        l.exported_symbol_count
    );
    for e in &c.exports {
        println!(
            "  export: name={:?} class={} nameOffset=0x{:x} value=0x{:x} section={}",
            e.name, e.class, e.name_offset, e.value, e.section_index
        );
    }
    match &r.main_target {
        Some(t) => println!(
            "  special main (mechanical): section {} ({} kind {}) + 0x{:x}",
            t.section_index,
            c.sections[t.section_index as usize].kind_name(),
            t.kind,
            t.offset
        ),
        None => println!("  special main: (loader fields)"),
    }
    if let Some(b) = &r.target_bytes {
        println!(
            "  main target bytes: {:02x?}",
            b.iter()
                .map(|x| format!("{:02x}", x))
                .collect::<Vec<_>>()
                .join(" ")
        );
    }
    if let Some(v) = &r.vector {
        println!(
            "  transition vector: [code=0x{:08x}, toc=0x{:08x}] (stored in container)",
            v.word0, v.word1
        );
    }
    if let Some(reloc) = &r.reloc_stream {
        println!(
            "  relocation stream (first {} bytes at relocInstrOffset): {:02x?}",
            reloc.len(),
            reloc
                .iter()
                .map(|x| format!("{:02x}", x))
                .collect::<Vec<_>>()
                .join(" ")
        );
    }
    if let Some(rv) = &r.reloc_vector {
        let d = match &rv.decode_status {
            Ok(()) => "ok".to_string(),
            Err(e) => format!("partial: {}", e),
        };
        println!(
            "  relocation sim: section {} + 0x{:x} (decode {}) bytes={}",
            rv.section_index,
            rv.main_offset,
            d,
            rv.bytes
                .map(|b| b
                    .iter()
                    .map(|x| format!("{:02x}", x))
                    .collect::<Vec<_>>()
                    .join(" "))
                .unwrap_or_else(|| "-".to_string())
        );
        match &rv.status {
            reloc::VectorStatus::Valid { entry, toc } => {
                let e = resolve_str(entry);
                let t = resolve_str(toc);
                println!(
                    "  relocation sim: VALID transition vector entry=0x{:08x} ({}) toc=0x{:08x} ({})",
                    entry.word, e, toc.word, t
                );
            }
            reloc::VectorStatus::Provisional {
                word0,
                word1,
                reason,
            } => {
                println!(
                    "  relocation sim: PROVISIONAL vector (word0=0x{:08x} word1=0x{:08x}) - {}; not labelled valid and not used to strengthen the verdict",
                    word0, word1, reason
                );
            }
            reloc::VectorStatus::NotAVector { word0, word1 } => {
                println!(
                    "  relocation sim: not a valid vector (word0=0x{:08x} word1=0x{:08x})",
                    word0, word1
                );
            }
            reloc::VectorStatus::BeyondDecodedPrefix => {
                println!(
                    "  relocation sim: cannot reconstruct (special-main location beyond decoded prefix)"
                );
            }
        }
    }
    for n in &r.notes {
        println!("  note: {}", n);
    }
    for e in &r.errors {
        println!("  ERROR: {}", e);
    }
}

fn resolve_str(p: &reloc::ResolvedPointer) -> String {
    if p.section_index == usize::MAX {
        format!("unresolved 0x{:08x}", p.word)
    } else {
        format!("section {} + 0x{:x}", p.section_index, p.offset)
    }
}

/// --trapcheck mode: scan code sections for the MacsBug low-level
/// debugger trap word (0x7F800008) and report each checkpoint
/// mechanically: code offset, PPCC-relative (= container) offset,
/// breadcrumb tag, and the decoded next instruction.
fn trapcheck_run(files: &[String], expect: Option<usize>) -> ExitCode {
    let mut any_fail = false;
    let mut any_parse_error = false;
    for path in files {
        let data = match std::fs::read(path) {
            Ok(d) => d,
            Err(e) => {
                eprintln!("{}: cannot read: {}", path, e);
                any_parse_error = true;
                continue;
            }
        };
        println!(
            "=== trapcheck: {} ({} bytes, sha256 {}) ===",
            path,
            data.len(),
            sha256::hex(&sha256::sha256(&data))
        );
        let c = match pef::Container::parse(&data) {
            Ok(c) => c,
            Err(e) => {
                println!("PARSE ERROR: {}", e);
                any_parse_error = true;
                println!();
                continue;
            }
        };

        let mut total = 0usize;
        let mut unknown_tags = 0usize;
        let mut tags: Vec<&str> = Vec::new();
        for (si, s) in c.sections.iter().enumerate() {
            if s.kind != 0 && s.kind != 6 {
                continue; // Code / ExecutableData only
            }
            if s.container_length != s.unpacked_length {
                println!(
                    "  WARNING: section {} ({} kind) is packed (container {} != unpacked {}); scan may be incomplete",
                    si,
                    s.kind_name(),
                    s.container_length,
                    s.unpacked_length
                );
            }
            let bytes = match trapcheck::section_bytes(&c, s) {
                Ok(b) => b,
                Err(e) => {
                    println!("  WARNING: section {} bytes unavailable: {}", si, e);
                    continue;
                }
            };
            let hits = trapcheck::scan_section(&bytes, s.container_offset as usize);
            if hits.is_empty() {
                continue;
            }
            println!(
                "  section {} ({}): {} traps @container 0x{:x}",
                si,
                s.kind_name(),
                hits.len(),
                s.container_offset
            );
            for h in &hits {
                total += 1;
                if h.tag.is_none() {
                    unknown_tags += 1;
                }
                let tag_txt = match h.tag {
                    Some((t, name)) => {
                        tags.push(name);
                        format!("tag 0x{:03x} ({})", t, name)
                    }
                    None => "tag ?".to_string(),
                };
                let nb = h.next_word.to_be_bytes();
                println!(
                    "  trap {:>2}: code 0x{:04x}  ppcc-rel 0x{:04x}  {}  bytes 7F 80 00 08  {}  | next 0x{:04x}: {:02X} {:02X} {:02X} {:02X}  {}",
                    total,
                    h.code_offset,
                    h.container_offset,
                    tag_txt,
                    trapcheck::decode(trapcheck::TRAP_WORD),
                    h.container_offset + 4,
                    nb[0],
                    nb[1],
                    nb[2],
                    nb[3],
                    h.next_decode
                );
            }
        }

        // Mechanical proof: every hit is the exact trap word (the scan
        // matches bytes 7F 80 00 08 word-aligned) and it decodes as
        // tw 0x1c,r0,r0.
        let decode_ok = trapcheck::decode(trapcheck::TRAP_WORD) == "tw 0x1c,r0,r0";
        let mut verdict = "PASS";
        if !decode_ok {
            verdict = "FAIL";
            any_fail = true;
        }
        if let Some(n) = expect {
            if total != n {
                verdict = "FAIL";
                any_fail = true;
            }
        }
        if unknown_tags > 0 {
            println!(
                "  WARNING: {} trap(s) without an identified checkpoint tag",
                unknown_tags
            );
        }
        let expect_txt = expect
            .map(|n| format!(" (expected {})", n))
            .unwrap_or_default();
        println!(
            "VERDICT: {} — {} traps{}; all decode tw 0x1c,r0,r0; tags: {}",
            verdict,
            total,
            expect_txt,
            if tags.is_empty() {
                "-".to_string()
            } else {
                tags.join(" ")
            }
        );
        println!();
    }
    if any_fail {
        ExitCode::from(1)
    } else if any_parse_error {
        ExitCode::from(2)
    } else {
        ExitCode::from(0)
    }
}
