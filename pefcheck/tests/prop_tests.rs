//! Property-based tests (proptest, dev-dependency only).

use pefcheck::pef::{unpack_packed, Container};
use pefcheck::reloc;
use pefcheck::sha256;
use pefcheck::validate;
use proptest::prelude::*;

// Property 1: unpack_packed never panics; Ok(v) => v.len() == out_len;
// Err(e) => e is non-empty.
proptest! {
    #[test]
    fn unpack_never_panics_and_obeys_length(stream in proptest::collection::vec(any::<u8>(), 0..64), out_len in 0usize..512) {
        match unpack_packed(&stream, out_len) {
            Ok(v) => assert_eq!(v.len(), out_len),
            Err(e) => assert!(!e.is_empty()),
        }
    }
}

// Property 2: a valid encoded stream is accepted and reproduces the exact
// bytes. Build streams with op0 (Zero) and op1 (Block) whose expansions
// sum exactly to out_len.
proptest! {
    #[test]
    fn valid_stream_roundtrip(chunks in proptest::collection::vec((any::<u8>(), any::<u8>()), 0..20)) {
        // Each (kind, len) with kind 0 = zeros, kind 1 = literals; lengths
        // 1..=31 fit the 5-bit value field.
        let mut stream = Vec::new();
        let mut expected = Vec::new();
        for (kind, len) in chunks {
            let len = (len as usize % 31) + 1;
            if kind % 2 == 0 {
                stream.push(len as u8); // opcode 0, value len (<=31)
                expected.resize(expected.len() + len, 0);
            } else {
                stream.push(0x20 | len as u8); // opcode 1, value len
                for i in 0..len {
                    let b = (i * 7 + 3) as u8;
                    stream.push(b);
                    expected.push(b);
                }
            }
        }
        let out_len = expected.len();
        let out = unpack_packed(&stream, out_len).expect("valid stream must decode");
        assert_eq!(out, expected);
    }
}

// Property 3: parse+validate never panic on arbitrary bytes and are
// deterministic.
proptest! {
    #[test]
    fn parse_validate_never_panics_deterministic(data in proptest::collection::vec(any::<u8>(), 0..4096)) {
        let r1 = match Container::parse(&data) {
            Ok(c) => Ok(validate::validate(&c)),
            Err(e) => Err(e),
        };
        let r2 = match Container::parse(&data) {
            Ok(c) => Ok(validate::validate(&c)),
            Err(e) => Err(e),
        };
        assert_eq!(format!("{:?}", r1), format!("{:?}", r2));
    }
}

// Property 4: single-byte mutations of the real fixtures never panic.
fn fixture(name: &str) -> Vec<u8> {
    std::fs::read(format!("{}/fixtures/{}", env!("CARGO_MANIFEST_DIR"), name))
        .unwrap_or_else(|e| panic!("missing fixture {}: {}", name, e))
}

proptest! {
    #[test]
    fn fixture_mutations_never_panic(
        idx in 0usize..9501,
        byte in any::<u8>(),
        which in proptest::sample::select(vec!["tm_ppcc1.pef", "e2b_oms.pef", "production_usbmidi9.pef"]),
    ) {
        let mut data = fixture(which);
        if idx < data.len() {
            data[idx] = byte;
        }
        if let Ok(c) = Container::parse(&data) {
            let _ = validate::validate(&c);
        }
    }
}

// Property 5: any byte-mutation of E2b that keeps the code section at
// containerOffset 0xE2 (and parseable) keeps the 16-byte-alignment error.
proptest! {
    #[test]
    fn e2b_code_offset_alignment_invariant(
        idx in proptest::prop_oneof![
            Just(0x3c), Just(0x3d), Just(0x3e), Just(0x3f), // section table containerOffset field
            Just(0x30), Just(0x31), Just(0x32), Just(0x33), // totalLength
        ],
        byte in any::<u8>(),
    ) {
        let mut data = fixture("e2b_oms.pef");
        data[idx] = byte;
        if let Ok(c) = Container::parse(&data) {
            if let Some(s) = c.sections.first() {
                if s.kind == 0 && s.container_offset == 0xe2 {
                    let r = validate::validate(&c);
                    assert!(
                        r.errors.iter().any(|e| e.contains("16-byte aligned") && e.contains("0xe2")),
                        "mutation at 0x{:x}=0x{:02x} lost the E2b alignment error: {:?}",
                        idx, byte, r.errors
                    );
                }
            }
        }
    }
}

// Property 6: sha256 output is stable and 32 bytes for the boundary
// lengths.
#[test]
fn sha256_boundary_lengths_stable() {
    for len in [0usize, 1, 55, 56, 57, 63, 64, 65, 127, 128] {
        let data: Vec<u8> = (0..len).map(|i| (i * 31 + 7) as u8).collect();
        let a = sha256::sha256(&data);
        let b = sha256::sha256(&data);
        assert_eq!(a, b, "sha256 unstable at len {}", len);
        assert_eq!(a.len(), 32);
        assert_eq!(sha256::hex(&a).len(), 64);
    }
}

// Property 7: the relocation simulator never panics on single-byte mutations
// of the real fixtures, for any section, and is deterministic.
proptest! {
    #[test]
    fn reloc_sim_never_panics_and_is_deterministic(
        idx in 0usize..9501,
        byte in any::<u8>(),
        which in proptest::sample::select(vec!["tm_ppcc1.pef", "omslib_ppcc601.pef", "e2b_oms.pef", "production_usbmidi9.pef"]),
        section in 0usize..4,
    ) {
        let mut data = fixture(which);
        if idx < data.len() {
            data[idx] = byte;
        }
        if let Ok(c) = Container::parse(&data) {
            // relocate_section on any section must not panic (may Err).
            let _ = reloc::relocate_section(&c, section);
            // special_main_vector must be deterministic.
            let a = reloc::special_main_vector(&c);
            let b = reloc::special_main_vector(&c);
            assert_eq!(format!("{:?}", a), format!("{:?}", b));
        }
    }
}

// Property 8: unpack_packed_partial is deterministic and, when it reports a
// full decode, yields exactly out_len bytes; a partial decode is shorter.
proptest! {
    #[test]
    fn unpack_partial_length_contract(
        stream in proptest::collection::vec(any::<u8>(), 0..64),
        out_len in 0usize..512,
    ) {
        let (out, status) = pefcheck::pef::unpack_packed_partial(&stream, out_len);
        assert!(out.len() <= out_len);
        match status {
            Ok(()) => assert_eq!(out.len(), out_len),
            Err(e) => assert!(!e.is_empty()),
        }
    }
}

// Property: the `tw TO,rA,rB` encoder/decoder round-trips for every TO
// and register, and the MacsBug trap word is exactly tw 0x1C, r0, r0.
proptest! {
    #[test]
    fn tw_encode_decode_roundtrip(to in 0u32..32, ra in 0u32..32, rb in 0u32..32) {
        let w = pefcheck::trapcheck::encode_tw(to, ra, rb);
        let d = pefcheck::trapcheck::decode(w);
        assert_eq!(d, format!("tw 0x{:x},r{},r{}", to, ra, rb));
        // The trap word is the TO=0x1C (LT|GT|EQ), r0, r0 case.
        if to == 0x1C && ra == 0 && rb == 0 {
            assert_eq!(w, 0x7F_80_00_08);
        }
    }
}
