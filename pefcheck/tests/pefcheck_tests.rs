//! Fixture-based integration tests for pefcheck.

use pefcheck::pef::Container;
use pefcheck::sha256;
use pefcheck::validate;

fn fixture(name: &str) -> Vec<u8> {
    std::fs::read(format!("{}/fixtures/{}", env!("CARGO_MANIFEST_DIR"), name))
        .unwrap_or_else(|e| panic!("missing fixture {}: {}", name, e))
}

/// Full 64-char sha256 pins (spec/pefcheck/tasks.md; E1/E2a/E2b are the
/// frozen artifact hashes from docs/oms-ppcc-entry-crash.md).
fn expect_hash(name: &str, expected: &str) {
    let data = fixture(name);
    let got = sha256::hex(&sha256::sha256(&data));
    assert_eq!(got, expected, "fixture {} hash mismatch", name);
}

#[test]
fn fixture_hashes_pinned() {
    expect_hash(
        "e1_oms.pef",
        "9b5f6182dafce541a6ca02fec243af245109974afb198cd9fd2beef790579916",
    );
    expect_hash(
        "e2a_oms.pef",
        "fa86b26d440fbefe4875b255a73df94985304e587a27f71e46b7396700b99907",
    );
    expect_hash(
        "e2b_oms.pef",
        "87d12ec09db1d411e261d648f99b93cd04eb96977053e6702d1abf50b5d2dd60",
    );
    expect_hash(
        "tm_ppcc1.pef",
        "4a0978fe6ee557a31a75fa46c5941d0a249433c93f745efc022eef3635c5a295",
    );
    expect_hash(
        "omslib_ppcc601.pef",
        "e5c47142e5e844b654a09383425bdad7f28baa05b0c1bb29dfe2061db2fefce7",
    );
    expect_hash(
        "production_usbmidi9.pef",
        "d33f3d3d89a0e82fb6d759175c0936f8d5e850e34c5c19f265cf833fdd4720c2",
    );
}

#[test]
fn e2b_rejected_for_misaligned_code_offset() {
    let c = Container::parse(&fixture("e2b_oms.pef")).expect("E2b must parse");
    let r = validate::validate(&c);
    assert!(
        r.errors
            .iter()
            .any(|e| e.contains("16-byte aligned") && e.contains("0xe2")),
        "E2b must be rejected for code containerOffset 0xE2, got: {:?}",
        r.errors
    );
    assert!(!r.valid());
}

#[test]
fn pass_set_is_structurally_valid() {
    for name in [
        "e1_oms.pef",
        "e2a_oms.pef",
        "tm_ppcc1.pef",
        "omslib_ppcc601.pef",
        "production_usbmidi9.pef",
    ] {
        let c = Container::parse(&fixture(name)).unwrap_or_else(|e| panic!("{}: {}", name, e));
        let r = validate::validate(&c);
        assert!(r.valid(), "{} must PASS, got errors: {:?}", name, r.errors);
    }
}

#[test]
fn tm_loader_fields_and_materialized_vector() {
    let c = Container::parse(&fixture("tm_ppcc1.pef")).unwrap();
    assert_eq!(c.loader.main_section, 1);
    assert_eq!(c.loader.main_offset, 0x3c);
    assert_eq!(c.loader.init_section, -1);
    assert_eq!(c.loader.term_section, -1);
    let r = validate::validate(&c);
    // The TM's special-main target bytes are pre-relocation contents; a
    // vector is materialized by relocation, not stored in the container.
    assert!(r.vector.is_none(), "TM vector must not be container-stored");
    let t = r.main_target.clone().expect("TM must have a special main");
    assert_eq!((t.section_index, t.offset), (1, 0x3c));
    assert_eq!(t.kind, 2); // PackedData
    assert!(r.valid());
}

#[test]
fn omslib601_loader_fields() {
    let c = Container::parse(&fixture("omslib_ppcc601.pef")).unwrap();
    assert_eq!(c.loader.main_section, 1);
    assert_eq!(c.loader.main_offset, 0x4);
    let r = validate::validate(&c);
    assert!(r.valid(), "601 must PASS: {:?}", r.errors);
    let t = r.main_target.expect("601 must have a special main");
    assert_eq!((t.section_index, t.offset), (1, 4));
}

#[test]
fn production_loader_no_main_and_exports() {
    let c = Container::parse(&fixture("production_usbmidi9.pef")).unwrap();
    assert_eq!(c.loader.main_section, -1);
    assert_eq!(c.loader.imported_library_count, 3);
    assert_eq!(c.loader.total_imported_symbol_count, 13);
    assert_eq!(c.loader.exported_symbol_count, 1);
    let main = c
        .exports
        .iter()
        .find(|e| e.name == "main")
        .expect("production must export 'main'");
    assert_eq!((main.class, main.value, main.section_index), (2, 0x80, 1));
    let r = validate::validate(&c);
    assert!(r.valid(), "production must PASS: {:?}", r.errors);
    assert!(r.main_target.is_none(), "production has no special main");
}

#[test]
fn e_series_loader_fields() {
    for (name, main_section, main_offset) in [
        ("e1_oms.pef", -1, 0u32),
        ("e2a_oms.pef", 1, 0),
        ("e2b_oms.pef", 1, 0),
    ] {
        let c = Container::parse(&fixture(name)).unwrap();
        assert_eq!(c.loader.main_section, main_section, "{}", name);
        assert_eq!(c.loader.main_offset, main_offset, "{}", name);
        assert_eq!(c.loader.exported_symbol_count, 1, "{}", name);
    }
}

#[test]
fn loader_section_fields_decode_as_signed_negative_one() {
    // Regression: mainSection/initSection/termSection are SInt32. A raw
    // 0xFFFFFFFF in the header must surface as i32 -1, not wrap.
    let mut data = fixture("tm_ppcc1.pef");
    // TM loader container @0x80; mainSection is its first 4 bytes.
    data[0x80..0x84].copy_from_slice(&0xffff_ffffu32.to_be_bytes());
    let c = Container::parse(&data).unwrap();
    assert_eq!(c.loader.main_section, -1);
    assert_eq!(c.loader.init_section, -1);
    assert_eq!(c.loader.term_section, -1);
    // A negative-but-not-(-1) mainSection is out of range (not a panic).
    let mut data = fixture("tm_ppcc1.pef");
    data[0x80..0x84].copy_from_slice(&(-2i32).to_be_bytes());
    let c = Container::parse(&data).unwrap();
    let r = validate::validate(&c);
    assert!(
        r.errors
            .iter()
            .any(|e| e.contains("mainSection -2 out of range")),
        "negative non -1 mainSection must be out-of-range, got: {:?}",
        r.errors
    );
}

#[test]
fn e_series_target_is_eight_zeros() {
    for name in ["e1_oms.pef", "e2a_oms.pef", "e2b_oms.pef"] {
        let c = Container::parse(&fixture(name)).unwrap();
        let r = validate::validate(&c);
        // E1 has no special main; E2a/E2b point at the packed data section.
        if name != "e1_oms.pef" {
            let b = r.target_bytes.as_ref().expect("E-series target bytes");
            assert_eq!(b.as_slice(), &[0u8; 8], "{}", name);
            assert!(
                r.vector.is_none(),
                "{} vector must be not-identifiable",
                name
            );
        }
    }
}

#[test]
fn crafted_loader_strings_offset_does_not_panic() {
    // Mutate E1's loader stringsOffset (loader base 0x80 + 0x28 = 0xA8)
    // past EOF: parse must return Err, not panic.
    let mut data = fixture("e1_oms.pef");
    data[0xa8..0xac].copy_from_slice(&0x000f_ffffu32.to_be_bytes());
    let r = Container::parse(&data);
    assert!(r.is_err(), "crafted strings offset must be a parse error");
}

#[test]
fn tm_stream_hits_reserved_opcode_5_via_parser() {
    // The TM data stream decodes through ops 0/1/2/4 then hits reserved
    // opcode 5: informational note, still PASS.
    let c = Container::parse(&fixture("tm_ppcc1.pef")).unwrap();
    let r = validate::validate(&c);
    assert!(
        r.notes
            .iter()
            .any(|n| n.contains("not decodable") && n.contains("reserved packed-data opcode 5")),
        "TM note missing, got: {:?}",
        r.notes
    );
}
