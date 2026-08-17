//! pefcheck — mechanical structural checker for PowerPC PEF containers.
//!
//! Usage: pefcheck <file>...
//! Exit: 0 = all files PASS, 1 = any file INVALID, 2 = parse error only.

use pefcheck::pef;
use pefcheck::reloc;
use pefcheck::sha256;
use pefcheck::validate;

use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        eprintln!("usage: pefcheck <pef-file>...");
        return ExitCode::from(2);
    }

    let mut any_invalid = false;
    let mut any_parse_error = false;
    for path in &args {
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
