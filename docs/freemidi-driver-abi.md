# Native FreeMIDI driver ABI investigation

Status: static recovery checkpoint complete for the authenticated FreeMIDI 1.45
corpus. No FreeMIDI binary, resource fork, build project, or proprietary
driver file has been added to this repository — short byte sequences are
quoted inline as evidence only. This report records evidence through
2026-08-22 and keeps unresolved loader/service boundaries explicit.

## Executive verdict

A native USBMIDI9 FreeMIDI driver is technically feasible, but the public
interface is not adequately documented for a safe production implementation.

The outer binary contract is recovered. A FreeMIDI hardware driver is a
resource-fork file in `System Folder:FreeMIDI Folder`, normally Finder type
`'DDef'` or `'IDvr'`. The System Extension loads a DDef resource and calls its
first code pointer with an exact packed 68K frame; authentic MOTU, Roland, SCC,
and InterApplication resources corroborate lifecycle and descriptor behavior.
The analysis notation is:

```c
/* Analysis notation; not an SDK header. */
typedef SInt16 (*FreeMIDIDriverProcPtr)(SInt32 par1,
                                       SInt16 message,
                                       SInt32 par2);
```

This establishes argument order, widths, caller cleanup, callee `rts`, and the
low-word return. It does not establish the source language. MOTU's embedded
Mixed Mode descriptor and ProcInfo are separately recovered below; they must
not be substituted for the direct 68K ABI. The complete
handler/field/receive/output boundaries and the remaining opaque service
calls are itemized in the evidence ledger.

Therefore:

* **Feasibility:** a pure 68K inert lifecycle probe can be specified as an
  isolated experiment; a production transport adapter cannot yet be specified
  safely.
* **Documentation sufficiency:** the binary 1.45 ABI is substantially
  recovered. The remaining blockers are the external discovery predicate,
  selector-9 relocation/callback target, and PowerPlug/native output service
  targets—not the absence of an SDK. MOTU's wrapper-to-descriptor transition
  is statically resolved as a resource-supplied AAFE descriptor; Roland uses
  a distinct CFM/XCOF path.
* **Current USBMIDI9 boundary:** insufficient for a production adapter. It has
  a cached interface table and push notification, but no outbound operation,
  USB-MIDI cable topology, stable device identity, removal notification, or
  multi-reader/fan-out semantics.
* **Recommendation:** keep the pure-68K probe design separate from the
  production-only PPC/CFM handler paths that remain opaque. Do not build/install it or begin production code in
  this repository; use the exact future MacsBug session in the ledger when
  dynamic work becomes available.

The earlier lead document, `docs/freemidi-driver-research.md`, should be read
with this report. This report corrects two material overstatements in that
document: MOTU `'DDef'` is an executable 68K wrapper rather than a proven
“params” blob, and the PEF begins at offset `0x20` in the extracted `'Code'`
resource body, not at `0x124`; and the System Extension's selector-7 local is
a 268-byte copied record. A separate 70-byte copy exists elsewhere in object
construction; the two records must not be conflated.

The byte-level ledger, raw call sequences, derived hashes, and cross-driver
dispatch tables are in `docs/freemidi-driver-abi-evidence.md`.

## Confidence and evidence rules

* **Verified:** directly observed in an authentic resource map, caller, or
  callee disassembly; the exact location is given.
* **Strong inference:** follows from independent caller/callee agreement or a
  conventional 68K interpretation, but is not declared by a header/manual.
* **Unresolved:** plausible but not safe to encode. No implementation should
  depend on it.

Strings are used as navigation clues only. A selector is treated as
corroborated only when a caller or dispatch code also observes it.

## What counts as a FreeMIDI driver?

| Thing | What it does | Evidence/status |
|---|---|---|
| Hardware/transport driver | Owns or reaches a physical MIDI interface, creates FreeMIDI ports, receives MIDI, and transmits MIDI. | Authentic files named `MOTU USB FreeMIDI Driver` and `SC8850 USB Driver`; Finder type `'DDef'`; executable `'DDef'`/`'Code'` or `'DDef'`/`'XCOF'` resources. **Verified.** |
| Interface driver | A related FreeMIDI driver form used by serial/interface-style hardware and port/cable services. | Authentic `~Standard Interface Driver` is `'IDvr'`; Roland also carries `'IDvr'` alongside `'DDef'`. Standard exposes an 11-entry table and Roland IDvr a 13-entry table. **Verified format; exact public semantic split unresolved.** |
| Device description / patch list | Names a synthesizer, maps banks/patches, supplies icons and studio metadata. It does not claim USB, receive MIDI, or submit bulk transfers. | FreeMIDI Devices (`'Odvi'`/`'FMS_'` in the 1.45 installer), FreeMIDI Setup data, patch-list resources, and studio configuration files. **Metadata, not a transport driver.** |
| OMS compatibility/emulation | Lets FreeMIDI applications route through OMS, or supplies an OMS-facing compatibility component. | FreeMIDI 1.45 strings say “Use OMS when available” and require OMS 2.0 or higher; the installer includes an OMS Emulator and OMS drivers. **Verified fallback, not native FreeMIDI support.** |
| Application-embedded FreeMIDI | An application links to FreeMIDI PowerPlug and uses `FMS*`/`FreeMIDI*` client functions. | `FreeMIDI PowerPlug.data` exports application/client APIs such as `FMSSendMidi`, `FMSSendMidiTimeStamped`, `FMSReadInputQueue`, `FMSGetDriverInfo`, and `FMSTimeGetPollFunction`. **Verified client glue, not the hardware-driver ABI.** |

An instrument definition or patch-list file therefore cannot satisfy the
USBMIDI9 requirement. A native adapter must be loadable by the FreeMIDI System
Extension as a hardware/interface driver and must implement the driver calls.

## Evidence matrix

| Claim | Source/artifact | Exact location or observation | Confidence |
|---|---|---|---|
| Drivers live in the System Folder's FreeMIDI folder | MOTU FreeMIDI 1.45 installer; Korg OASYS PCI installation PDF | Local tree `fm145x/x/Files To Install/FreeMIDI Folder/`; Korg PDF says the OASYS FreeMIDI Driver is placed “in the FreeMIDI folder, within the System folder” | Verified |
| Hardware drivers use Finder type `'DDef'` | MOTU and Roland resource forks | MOTU `MOTU USB FreeMIDI Driver.rsrc`, `BNDL 128`/`FREF 128` map to `'DDef'`; Roland `SC8850 USB Driver.rsrc` does the same | Verified |
| Interface drivers use Finder type `'IDvr'` | MOTU Standard Interface resource fork | `~Standard Interface Driver.rsrc`, `BNDL 128`/`FREF 128` map to `'IDvr'` | Verified |
| Creators are per-driver signatures, not one universal creator | Resource maps and BNDL owner resources | MOTU `mUSB 0`; Roland `RdSU 0`; Standard `StdD 0`; SCC `SCCd 0`; InterApplication `APPd 0` | Verified |
| The filename is not a known ABI key | Authentic install names | `MOTU USB FreeMIDI Driver`, `SC8850 USB Driver`, `~Standard Interface Driver`, `SCC Driver`; no common filename prefix | Strong inference |
| Resource ID is not globally fixed by the available corpus | Driver maps vs caller | MOTU `DDef 128`; Roland `DDef 0`; Standard `IDvr 128`; System Extension caller loads `'DDef'` ID `1` at `CODE1+0x1d09c` | Verified observation; loader meaning unresolved |
| MOTU has a PPC code resource | MOTU driver resource fork | `Code 128`, length 58,044; PEF magic `Joy!peffpwpc` at extracted resource-body offset `0x20` after a 32-byte wrapper/header | Verified |
| Roland has PPC code plus 68K entry wrappers | Roland FreeMIDI resource fork | `XCOF 10000`, length 1,122; `DDef 0`, length 17,879; `IDvr 128`, length 1,003; both driver resources begin with executable 68K code | Verified |
| Top-level entry receives setup, descriptor, callback, and teardown calls | FreeMIDI System Extension `CODE1` | `CODE1+0x1d0dc` selector 1; `+0x1d0fc` selector 7; `+0x1d112` selector 9; teardown `+0x1d328` selector 8 | Verified caller observation |
| Source argument order is `(long, short, long)` | Caller and MOTU DDef wrapper | Caller pushes `par2`, selector word, `par1`; MOTU wrapper reads the selector as a word and dispatches a native call; ordinary 68K C right-to-left interpretation gives `(par1, message, par2)` | Strong inference |
| Return is at least a 16-bit value | Caller and wrappers | Caller stores `d0` as a word after selectors 1, 7, 8, 9; Roland dispatch paths return word-sized values | Verified |
| Selector 8 is teardown | Caller and MOTU wrapper | `CODE1+0x1d328`; MOTU DDef setup/teardown wrapper releases the Code resource after selector 8 | Verified |
| Selectors 2–6 exist in a DDef implementation | Roland DDef | Dispatch table near body offset `0x794`: 2→`0x760`, 3→`0x50c`, 4→`0x580`, 5→`0x630`, 6→`0x740` | Verified dispatch; meanings partly inferred from adjacent strings |
| SCC DDef accepts selectors 0–13 | SCC `DDef 128` | Body `0xec0`: compares selector with `0xd`; table at `0xeee`; selector 7→`0xf34`, selector 13→`0xfb2`, 9–12→default | Verified dispatch |
| InterApplication DDef accepts selectors 0–8 | InterApplication `DDef 128` | Body `0x11e`: compares selector with `8`; table at `0x144`; selector 7→`0x170`, selectors 1/3/4/5 have handlers | Verified dispatch |
| Roland DDef selector 7 returns driver setup information | Roland DDef plus caller | Handler at body `0x794` handles 7, writes an `RdSU` descriptor into `par2`, loads `ICON 129`, writes `par2+4 = 0x100`; caller passes a local block | Strong inference |
| Selector 9 likely registers a callback/notification record | FreeMIDI caller | `CODE1+0x1d112` passes an 8-byte block: context at offset 0 and function pointer at offset 4; returns a boolean; Roland DDef does not handle 9 | Strong inference |
| Receive delivery exists | Roland DDef | Strings/functions at body `0x44c` (`ReceiveFreeMidi`) and `0x508` (`RECEIVEFROMDRIVER`); code calls internal callback/global pointers | Verified existence; signature/timestamp unresolved |
| Transmit delivery exists in at least one driver | Roland DDef strings; FreeMIDI output classes | `TransmitData`, `SendNextCableByte`, `DriverGetBuffer` strings and code | Verified existence; call contract unresolved |
| MOTU driver handles USB notification and Name Registry lookup | MOTU `Code 128` PEF | Imports `USBInstallDeviceNotification`, `USBRemoveDeviceNotification`, `USBGetNextDeviceByClass`, `RegistryPropertyGet`, `FindSymbol` | Verified; FreeMIDI dynamic-port semantics not thereby proven |
| FreeMIDI can use OMS as backend | FreeMIDI 1.45 System Extension strings | “Use OMS when available”; “requires OMS version 2.0 or higher”; “FreeMIDI applications only” compatibility-mode text | Verified |
| A public SDK was not in the installer corpus | Local 1.41/1.45 materials and public search | No headers, samples, libraries, or SDK directory in the local FreeMIDI installers; public archive search found no authentic SDK | Verified negative result for searched corpus |
| MOTU SDK distribution was restricted | Contemporary Sweetwater support note | `https://www.sweetwater.com/sweetcare/articles/motu-issue-developer-kit-for-freemidi-mas-unisyn-profile/`, lines 538–546 in the captured page: developer agreement, faxed signature, supplied “as is”, limited distribution | Secondary, corroborating |

## Discovery and packaging

### What is established

The authenticated 1.45 install catalog contains this relevant shape:

```text
System Folder
├── Extensions
│   └── FreeMIDI System Extension       INIT / FMS_
└── FreeMIDI Folder
    ├── MOTU USB FreeMIDI Driver        DDef / mUSB
    ├── SCC Driver                      DDef / SCCd
    ├── InterApplication Driver         DDef / APPd
    └── ~Standard Interface Driver      IDvr / StdD
```

The leading space in the extracted ` SCC Driver.rsrc` is an extraction/Finder
name detail, not evidence of a filename convention. The known driver names are
human-readable and differ substantially.

The BNDL/FREF records are the strongest available Finder metadata evidence:

| File | BNDL owner resource | FREF target | Relevant resources |
|---|---|---|---|
| MOTU USB | `mUSB 0` | `DDef` | `Code 128`, `DDef 128`, `BNDL 128`, `FREF 128`, `vers 1` |
| Standard Interface | `StdD 0` | `IDvr` | `IDvr 128`, `BNDL 128`, `FREF 128`, `vers 1` |
| Roland SC-8850 | `RdSU 0` | `DDef` | `XCOF 10000`, `DDef 0`, `IDvr 128`, `BNDL 128`, `FREF 128`, `vers 1/2` |
| SCC | `SCCd 0` | `DDef` | `DDef 128`, `BNDL 128`, `FREF 128`, `vers 1` |
| InterApplication | `APPd 0` | `DDef` | `DDef 128`, `BNDL 128`, `FREF 128`, `vers 1` |

The resource IDs are implementation data, not a single universal constant:
MOTU uses `DDef 128`, Roland uses `DDef 0`, and the FreeMIDI System Extension
caller statically loads `DDef 1` in one path. The exact file-enumeration
predicate—creator only, type only, both, or a database-assisted scan—was not
recovered. The safe statement is that a driver must be a resource-fork file in
the FreeMIDI Folder with authentic-looking Finder metadata and the driver
resources expected by the installed FreeMIDI version.

### Native code form

MOTU and Roland show a mixed 68K/PPC arrangement:

1. a 68K resource entry wrapper (`DDef` and/or `IDvr`),
2. a PPC PEF code resource (`Code` for MOTU, `XCOF` for Roland), and
3. Mixed Mode/CFM handoff to native PPC code where applicable.

The MOTU PEF imports `InterfaceLib`, `USBManagerLib`, `NameRegistryLib`, and
`USBServicesLib`, and imports `FindSymbol`, `Get1Resource`, resource-memory
management, and USB/Name Registry functions. Its PEF export table is empty in
the analyzed `Code 128` resource. The literal `TheMOTUShimInterface` appears
in the binary but is not a verified PEF export; the previous report must not
use that literal as an ABI declaration.

The USBMIDI9 adapter should not copy this device-claiming architecture. It
should use the extension's dispatch symbol and avoid importing USB Manager
device-claim/notification functions itself.

### Native bridge boundaries

MOTU Code 128 has a 32-byte resource prefix containing an AAFE Mixed Mode
RoutineDescriptor; the PEF starts at resource offset `0x20`. Its embedded
ProcInfo is `0x00000EE5`, mechanically meaning Think-C stack convention, a
4-byte result, and stack parameter widths 4/2/4. The 68K DDef wrapper obtains
the Code handle, loads it, takes the first long from the handle, and calls it
with `jsr (a0)`. This makes the outer bridge and resource lifetime static
facts; the PPC PEF's own special-main/relocation path remains a separate CFM
boundary. See the raw ledger section for bytes and hashes.

Roland is not evidence of the same bridge. Its DDef loads `XCOF 10000`, calls
CFM setup, resolves `mixd`, `cfrg`, and `sysa`, and then makes repeated service
calls. Its XCOF has no code section and contains Name Registry UPP descriptors.
The two native arrangements must remain separate in any implementation plan.

### What is not established

No source or manual proves all of the following:

* whether FreeMIDI enumerates by Finder type, creator, both, or a registry;
* whether every version accepts both 68K and PPC forms;
* the required `DDef`/`IDvr` resource ID for a newly authored driver;
* whether a `Code` PEF is required for 68K-only installations;
* the precise RoutineDescriptor/ProcInfo behavior of Roland's CFM service
  calls and the PPC PEF's internal startup path (MOTU's outer descriptor and
  ProcInfo `0xEE5` are verified);
* a filename prefix or extension.

Do not clone the MOTU resource map mechanically and call that a specification.

## Recovered caller ABI and lifecycle

### Outer entry candidate

The FreeMIDI System Extension's 68K core (`CODE1`) contains this call shape at
`+0x1d0dc`:

```text
push long 0                 ; par2
push word 1                 ; message
push long 0                 ; par1
load driver resource pointer
dereference first code pointer
jsr (a0)
store d0 as a word
```

The same code passes `par2` as a pointer for selectors 7 and 9. The MOTU 68K
`DDef 128` wrapper reads the selector as a word, dispatches on it, and invokes
the PPC/native entry with a 16-bit result. Under the normal 68K C convention
where arguments are pushed right-to-left, this gives the candidate declaration
shown above. It does **not** prove that the original source used C rather than
Pascal, nor does it prove the exact Mixed Mode `ProcInfo` value. Those must be
resolved before publishing a header.

The observed 68K resource bodies start with a 12-byte wrapper/header. Examples:

```text
MOTU DDef 128: 60 0a 00 00 44 44 65 66 00 80 00 00
Roland DDef 0: 60 0a 00 00 44 44 65 66 00 00 00 00
Roland IDvr 128:60 0a 00 00 49 44 76 72 00 80 00 00
```

The bytes are evidence of a self-identifying executable wrapper, not a safe C
structure definition. Keep the wrapper private to the eventual build layer.

The stack offsets are directly recoverable. At the MOTU `DDef 128` dispatcher
body `0x5c`, after `movem.l d3-d5,-(a7)`, the selector is read from `$14(a7)`;
the caller's `par1` is at `$10(a7)` and `par2` at `$16(a7)`. The native call
sequence at `0xd0` pushes `par2`, the selector word, and `par1`, then cleans
`$a` bytes. The System Extension caller at `CODE1+0x1d0dc` has the same
10-byte argument area and cleans three calls with `lea.l $1e(a7),a7`.
Consequently the safe analysis notation is:

```text
entry frame:  +8  par1: SInt32
              +c  selector: SInt16
              +e  par2: SInt32
return:       d0 low word; caller cleanup; callee uses rts
```

The `+e` offset is intentional: the middle selector is a 16-bit stack item;
there is no padding demonstrated by these callers.

### DDef selector table

The following table separates observed calls from semantic interpretation.

| Selector/message | Observed `par1` | Observed `par2` | Return observed | What can be said safely | Execution/lifetime |
|---:|---|---|---|---|---|
| 1 | zero | zero in the caller | word saved | Setup/initialization. MOTU wrapper loads/holds the native `Code` resource and obtains its entry. Roland has a setup path named `SetupPPCNativeCode`. | Resource/code becomes live before later calls; verified. Exact setup record absent. |
| 7 | zero | pointer to local caller block | word saved to a status/boolean path | Driver setup/descriptor information is a strong inference. Roland writes an `RdSU` descriptor, uses `ICON 129`, and writes `0x100` at `par2+4`. | The observed caller copies 67 longwords (268 bytes) from `-$114(a6)` into `a2+0xc`. Field meanings unknown. A separate constructor at `CODE1+0x1cfac` copies 17 longwords plus one word (70 bytes) from its own descriptor pointer; that is not proof that selector 7's local is 70 bytes. |
| 9 | zero | pointer to 8-byte block: caller context at offset 0, function-pointer-sized value at offset 4 | word converted to boolean | Optional callback or notification registration is a strong inference. The Roland DDef default path does not implement selector 9. | The literal `0x0000c1a2` is placed in the block, but the extracted CODE1 bytes at that raw offset decode as the middle of an instruction. Its loaded/relocated address must be observed on a real Mac before this is called a valid callback pointer. |
| 8 | zero | zero | word ignored by caller | Teardown. MOTU calls native teardown, releases the held `Code` resource, and disposes its pointer. | Last call before code/resource release in the observed path. |

The selector numbers are not an invented enum: 1, 7, 9, and 8 are direct
caller observations. Their names and parameter records remain provisional.

No separate, independently verified `open`, `close`, or `rescan` message was
found. The observed port-install/dispose paths may serve those roles, but that
is not established. Likewise, no caller-side live-rescan sequence was found.

### Roland DDef corroboration

The Roland `DDef 0` handler uses a dispatch table with the following observed
paths (resource-body offsets are within the extracted `DDef 0` body):

| Selector | Callee | Adjacent implementation evidence |
|---:|---:|---|
| 0 | default | zero/default return path |
| 1 | setup path | `SetupPPCNativeCode` / native setup strings |
| 2 | `0x760` | adjacent `DisposePort` string; likely port disposal |
| 3 | `0x50c` | `RECEIVEFROMDRIVER` helper; likely receive/forward path |
| 4 | `0x580` | adjacent `InstallPort` string; likely port installation |
| 5 | `0x630` | adjacent `CreatePortData` string |
| 6 | `0x740` | adjacent `InitializePort` string |
| 7 | special path | writes setup/descriptor information as described above |
| 8 | teardown | `TearDownPPCNativeCode` and cleanup |
| 9 | default | no handler observed |

The functions also contain strings `TransmitData`, `TickleProcSub`,
`FMTickleProc`, `ReceiveFreeMidi`, `ReleasePort`, `DeletePortData`,
`GetPortUsage`, and `CountPorts`. These names corroborate the existence of
port and I/O operations, but strings alone do not define their parameters.

The `ReceiveFreeMidi` routine at approximately `0x44c` accepts a long and a
word, parses packed input values, and loops bytes through a helper. The
`RECEIVEFROMDRIVER` routine at approximately `0x508` uses a port context and
calls internal callback/global pointers. This is enough to prove an inbound
path, not enough to write its callback declaration or timestamp interpretation.

### IDvr dispatch is a separate, partially recovered layer

Roland's `IDvr 128` body has a 13-entry dispatch table. The handler at body
`0x30a` uses `a6+8` as a context/record pointer, `a6+0xc` as a selector word,
and `a6+0xe` as a long parameter. This is a different entry layer from the
outer DDef call, although the same packed `(long, word, long)` frame shape is
visible. Its dispatch table is at `0x33a` and accepts selectors 0 through 12.
The observed paths are:

| IDvr selector | Callee/body offset | Evidence |
|---:|---:|---|
| 0 | default | zero/default path |
| 1 | `0x166` | `CableHookProc`, returns byte result widened to a word |
| 2 | `0x17e` | `QueryPort`; clears `context+0x3c`, obtains FMS address/callback fields |
| 3 | `0x1da` | `InstallPort`; invokes the stored port callback |
| 4 | `0x212` | `DisposePort`, returns a word |
| 5–8 | default | no handler observed |
| 9 | `0x22c` | `ComparePorts`; writes a function pointer to the long pointed to by the second parameter |
| 10 | `0x272` | `CableProc`; fills a port/cable record at the context pointer |
| 11 | inline | writes byte 4 at `context+0xc`, returns 1 |
| 12 | inline | returns 1 |

The System Extension's internal wrapper at `CODE1+0x38e70` is not the same
thing as the top-level DDef call. It loads a driver object/vtable and pushes,
in order, a long from `a6+0x10`, a word from `a6+0xe`, a word from `a6+0xc`,
and a context pointer before calling the first vtable entry. Callers around
`+0x389bc` and `+0x389f8` pass a port index and either a per-port value or a
local output buffer through this wrapper. This corroborates a port/cable
interface but does not justify a formal declaration. Do not merge IDvr
selectors with DDef selectors 1–9.

### Standard Interface, SCC, and InterApplication corroboration

The smaller drivers expose the same ABI-shaped entry without relying on the
MOTU USB implementation:

| Resource | selector dispatch | observed handlers/records |
|---|---|---|
| Standard `IDvr 128` | body `0x29a`, table `0x2c6`, accepted 0–10 | 0/5–8→`0x328` default; 1→`0x2dc`; 2→`0x2ea`; 3→`0x2f6`; 4→`0x300`; 9→`0x312`; 10→`0x31e`. Handler `0x1f6` initializes a record: word 2 at +0, zero longs at +2/+6, zero words at +a/+c/+12/+14/+16/+1a, words 1 at +e/+10, `0xffff` at +18, word `0x25e` at +1c, and fills string slots at +22 and +0x122. |
| SCC `DDef 128` | body `0xec0`, table `0xeee`, accepted 0–13 | 0/9–12→default; 1→`0xf0a`; 2→`0xf72`; 3→`0xf98`; 4→`0xfa6`; 5→`0xf80`; 6→`0xf8c`; 7→`0xf34`; 8→`0xf22`; 13→`0xfb2`. Selector 7 writes creator `SCCd` at `par2+0`, loads `ICON 128` into `par2+6`, writes `0x100` at `par2+4`, copies a Pascal string into the record, and sets bytes at +a/+b. |
| InterApplication `DDef 128` | body `0x11e`, table `0x144`, accepted 0–8 | 0/2/6/8→default; 1→`0x156`; 3→`0x1b8`; 4→`0x1c4`; 5→`0x1ac`; 7→`0x170`. Selector 7 writes creator `APPd`, loads `ICON 128`, writes `0x100` at `par2+4`, and copies the application name. |

These tables establish a family of executable dispatchers, not a single
universal selector enum. The repeated selector-7 descriptor action is strong
corroboration for a setup-information role; exact record sizes and string
ownership still vary.

### The internal wrapper at `CODE1+0x38e70`

This wrapper has an ordinary 68K frame (`link a6,#0`) and receives an object at
`8(a6)`. It pushes, before the vtable call, `10(a6)` as a long, `e(a6)` as a
word, `c(a6)` as a word, and the object's long at `e(a2)`, then calls the first
entry through the object at `a2+4`. It cleans `$10` bytes and returns with
`rts`. The raw call sequence is:

```text
38e70: 4e56 0000 2f0a 246e 0008 2f0a 4e ba 0030
38e80: 2f2e 0010 3f2e 000e 3f2e 000c 2f2a 000e
38e90: 206a 0004 2050 4e90 4fef 0010 245f 4e5e 4e75
```

The two argument words are deliberately retained as separate words in this
evidence. They may be a nested interface-method signature rather than a third
DDef message form.

## Driver lifecycle and I/O contract

| Area | Direct evidence | Safe conclusion | Unknown/blocker |
|---|---|---|---|
| Initialization | DDef selector 1 caller and both driver wrappers | Entry is called before the driver descriptor/port paths; native code may be loaded here. | Exact setup parameters, allocation responsibility, and failure codes. |
| Shutdown | DDef selector 8 caller and MOTU wrapper | Selector 8 is called before resource unhold/disposal. | Whether FreeMIDI can shut down/reopen without a system restart; idempotence. |
| Port count/enumeration | Roland strings `CountPorts`, `GetPortUsage`; caller loops per-port values; README says six SC-8850 ports | A driver can expose multiple logical ports; six SC-8850 ports are a concrete example. | Exact call/return record and whether enumeration is push, pull, or descriptor-driven. |
| Naming | Roland README: “SC8850 Driver”, “SC8850 Port”; resource strings and icons | Driver and port names are part of setup metadata/descriptor data. | Exact string encoding, ownership, length, localization, and rename calls. |
| Install/remove | Roland functions/strings `InstallPort`, `ReleasePort`, `DisposePort`; IDvr selector 3/4 | Port creation and disposal exist in the implementation. | Whether FreeMIDI itself sends add/remove messages after startup; no direct dynamic enumeration contract found. |
| USB hotplug | MOTU PEF imports USB install/remove notification APIs | MOTU's USB-facing layer observes USB lifecycle. | This does not prove FreeMIDI ports can appear/disappear live. The adapter must assume no hotplug support until tested. |
| Incoming MIDI | Roland `ReceiveFreeMidi`/`RECEIVEFROMDRIVER`; packed-word/byte loop; PowerPlug queue helper | Driver-to-FreeMIDI input delivery exists; client queue records include a 32-bit value plus an 8-byte timestamp output. | Native callback type, argument ownership, message framing, SysEx continuation, timestamp fields, and legal execution level. |
| Outgoing MIDI | Roland `TransmitData`; FreeMIDI internal `NewStyleOutputChannel`, `DriverGetBuffer`, `SendNextCableByte`; PowerPlug send APIs | FreeMIDI has an output path and cable-byte-oriented internal machinery. | Native driver entry selector/record, scheduling contract, whether FreeMIDI pushes bytes or driver pulls a buffer, and buffer lifetime. |
| Timestamp | PowerPlug section 0 and its loader descriptors | `FMSReadInputQueue` reaches `0x1d5c→0x3800→0x386c`, which writes an 8-byte `lfd/stfd` value to the caller's third pointer; timestamped send wrapper at `0x1c44` stores PPC `f1` as an 8-byte value | Client queue/timestamp representation verified; native DDef timestamp remains unresolved |
| SysEx | Roland receive loop and FreeMIDI byte/cable strings | Byte streams are involved. | No verified maximum, continuation marker, allocation ownership, or end-of-exclusive callback contract. |
| Errors | Callers store 16-bit returns; selector 9 is used as boolean | Entry returns a word-sized status/boolean. | No authenticated error enum or success convention. Do not assume `noErr` for every zero. |
| Interrupt/task context | No FreeMIDI SDK/manual; USBMIDI9 callback is secondary interrupt level | The FreeMIDI callback context is unknown. | Treat all FreeMIDI calls as task-time only until a header or trace proves otherwise. The current USB callback may only dequeue and wake a deferred worker. |
| Reentrancy/threading | No direct declaration; Classic Mac OS has cooperative tasking plus interrupt callbacks | Driver must be written defensively. | No proof of reentrancy, callback nesting, or whether output can call back synchronously. |
| Ownership | Caller holds code resource through teardown; `DriverGetBuffer` names are internal | Code and callback targets must remain live for the full registration lifetime. | Who allocates/frees port records, receive buffers, output buffers, and callback context. |

This is the central implementation boundary: only initialization, teardown,
the outer call shape, and the existence of the port/I/O paths are currently
safe to encode. The actual MIDI data contract is not.

## Two-driver comparison

| Property | MOTU USB FreeMIDI Driver 1.42 in FreeMIDI 1.45 | Roland SC-8850 USB FreeMIDI 2.0 | Interpretation |
|---|---|---|---|
| File | `MOTU USB FreeMIDI Driver.rsrc` | `SC8850 USB Driver.rsrc` | Both are resource-fork drivers, not metadata files. |
| Finder mapping | `mUSB` creator; BNDL/FREF → `DDef` | `RdSU` creator; BNDL/FREF → `DDef` | Common outer packaging. |
| Top-level code | 68K `DDef 128` wrapper + PPC `Code 128` PEF | 68K `DDef 0` wrapper + PPC `XCOF 10000` PEF | PEF/resource IDs and container types vary. |
| Interface code | No separate `IDvr` in this file | `IDvr 128` also present | Device-specific or version-specific layering; do not infer that every USB driver must carry IDvr. |
| USB linkage | Imports USB Manager, Name Registry, USB Services, `FindSymbol`; has USB notification imports | FreeMIDI PEF uses a small Name Registry/USB linkage and the driver has explicit port/cable routines | Shared strategy is registry/service linkage; exact exports and transport plumbing differ. |
| Native export | No verified PEF export; literal `TheMOTUShimInterface` is not sufficient | No claim of a shared named export from the FreeMIDI resources | Do not require a guessed export symbol in USBMIDI9. |
| Observed dispatch | DDef wrapper directly dispatches selector 1/8 and calls native code | DDef dispatch visibly handles 1–8 and IDvr handles 1–4/9–12 | Independent corroboration of the outer and port layers. |
| Ports | Device family strings include MIDI Timepiece/Express variants | README documents six SC-8850 ports | Port count/topology is device-specific. |

The comparison is enough to reject a one-driver generalization, while still
supporting a common outer call shape and resource model.

## Candidate declarations and raw layouts

The recovered binary analysis declaration for the outer stack shape is:

```c
typedef SInt16 (*FreeMIDIDefEntryProcPtr)(SInt32 par1,
                                          SInt16 message,
                                          SInt32 par2);
```

The following deliberately use opaque storage rather than invented public
field names. The executable field accesses are recorded in the evidence
ledger; these typedefs remain analysis-only:

```c
typedef struct {
    UInt8 raw[268];      /* selector-7 local copied by CODE1; fields unknown */
} FreeMIDISelector7BlockObserved;

typedef struct {
    UInt8 raw[70];       /* separate CODE1 constructor copy; not selector-7 */
} FreeMIDIDescriptorCopyObserved;

typedef struct {
    UInt32 context;      /* observed at par2+0 in selector 9 caller */
    UInt32 callback;     /* observed at par2+4; exact proc type unknown */
} FreeMIDICallbackBlockObserved;
```

These are analysis notation, not headers to ship. The 268-byte size is the
observed selector-7 local copy, not a decoded field layout. The separate
70-byte constructor copy is a different object path. The 8-byte selector-9
block may be a registration record, but its callback ABI and loaded address
are not recovered. No public `DDef` parameter struct or `IDvr` vtable
declaration is invented; opaque port/cable layouts and the PowerPlug queue
record are documented as byte offsets in the evidence ledger.

## SDK and build investigation

### Authentic artifacts located

| Artifact | Version/provenance | Result |
|---|---|---|
| `Install_FreeMIDI_1.45.sit` | MOTU FreeMIDI 1.45 installer, Macintosh Garden/Macintosh Repository-era mirror; extracted locally | Full runtime, Setup, PowerPlug, and drivers; no SDK headers/projects. |
| `MOTU_USB_Install-FreeMIDI_1.41.sit` | MOTU USB/FreeMIDI 1.41 installer, local research corpus | No public SDK; useful version lead only. |
| `SC-8850 USB 2.0E_FM` / `SC8850 USB Driver.rsrc` | Roland archived support package; original HQX URL below; local Internet Archive-derived corpus | Authentic FreeMIDI driver plus README; no SDK. |
| `FreeMIDI PowerPlug.data/.rsrc` | Installed MOTU 1.45 runtime | Application/client glue exports many `FMS*` functions; not hardware-driver headers. |
| `FreeMIDI System Extension.rsrc` | Installed MOTU 1.45 runtime | Caller and class/string evidence; no public declarations. |

No CodeWarrior FreeMIDI project, `FreeMIDI.h`, driver header, import library,
sample driver, or MOTU developer kit was found in the repository, the local
1.41/1.45 installers, or the searched public archive material. The Sweetwater
page is evidence that such an SDK existed and was gated by a developer
agreement, but it does not establish redistributable terms or the exact
version.

### Build implications

The observed artifacts imply the following likely build pieces, all marked as
inference until the SDK is recovered:

* a resource-fork driver file with Finder metadata, BNDL/FREF, signature,
  icons, and version resources;
* a 68K entry wrapper in the appropriate `'DDef'` or `'IDvr'` resource;
* a PPC PEF code fragment if targeting the observed native PPC path;
* a Mixed Mode descriptor/bridge matching the installed FreeMIDI caller;
* resource compilation and CodeWarrior-era CFM/PEF linking.

CodeWarrior IDE 4.0.4 is a reasonable candidate because the authentic
artifacts are classic 68K/PPC CFM-era resources, but no evidence here says that
FreeMIDI's SDK officially supported that exact IDE release. The CodeWarrior
target, `ProcInfo`, CFM fragment exports, resource compiler settings, and
link/import libraries must be verified from the SDK or reproduced by a
controlled probe. Do not claim “CodeWarrior 4.0.4 supported” yet. No FreeMIDI
SDK license or redistribution terms were located; the observed MOTU and Roland
artifacts remain copyrighted/proprietary research material.

### Version scope

FreeMIDI 1.45 is the fully authenticated local baseline. The Macintosh
Repository lead lists a 1.48 archive (`usb_fm_1.48.sit_.hqx`, also linked as
`fm1.48.hqx` by secondary mirrors). A second mirror reports a 5.00 MB
`fm1.48.hqx` with MD5 `5dfc138c53810f75428f398c3367fbc1`, but its download
endpoint returned HTTP 403 during this investigation; no local 1.48 binary
was available for static comparison. Searches for `FreeMIDI SDK`, `FreeMIDI
Developer Kit`, and CodeWarrior driver material found only period references
to a gated SDK, not an authentic downloadable SDK. FreeMIDI 1.48 must not be
treated as ABI-confirmed. Roland's README requires FreeMIDI 1.35 or later,
which is useful compatibility evidence but not a 1.48 contract.

## Fit with USBMIDI9

The preferred architecture remains correct at the ownership level:

```text
USB device
    ↓
USBMIDI9 system extension + portable USB-MIDI core
    ├── OMS adapter
    └── native FreeMIDI adapter
```

The FreeMIDI adapter should not independently enumerate or claim the USB
device. The MOTU driver's imports show why: a real FreeMIDI driver may use USB
Manager notifications, Name Registry lookup, and a separate transport
interface. Duplicating that path would create two owners and two lifecycles.

### What can be reused now

The adapter can reuse:

* the cached `USBMIDI9DispatchTable` location;
* `enumerateInterfaces` and `getInterfaceInfo` for the initial snapshot;
* the v2 `setEventCallback` only as a wakeup notification;
* the portable USB-MIDI packet/descriptor/cable parser;
* the extension's receive ring, if the adapter is the sole consumer.

The event callback is documented in
`classic/usbmidi9_dispatch.h:44-50` as secondary-interrupt-level and restricted
to cheap work/dequeue. It must not enter an unverified FreeMIDI callback or
perform allocation, File Manager I/O, or blocking work. A deferred task should
drain the ring, convert USB-MIDI event packets to the FreeMIDI receive framing,
and then call the FreeMIDI-side delivery routine at a proven legal level.

### Required boundary additions before production

The current v2 table, `classic/usbmidi9_dispatch.h:69-100`, is not enough:

* `dequeueBytes` is destructive and there is only one event-callback slot;
* interface indices are stable only until removal;
* `USBMIDI9InterfaceInfo` has vendor/product/interface data but no stable
  device instance identity or MIDI jack/cable topology;
* there is no bulk-OUT enqueue/submit operation or completion status;
* there is no add/remove notification;
* there is no timestamp or USB-frame association.

A future versioned service boundary should provide, without changing the
current v2 ABI:

1. a stable device/interface identity and generation number;
2. a logical MIDI topology snapshot: jack, endpoint, cable, direction, and
   stable logical-port identity;
3. add/remove notifications with a lifetime/refcount rule;
4. outbound packet/byte submission with bounded-buffer ownership and
   completion/error reporting;
5. either multi-reader subscriptions or one extension-owned fan-out that
   delivers the same decoded receive stream to OMS and FreeMIDI;
6. a timestamp contract, or an explicit statement that the adapter stamps at
   dequeue time using a documented clock.

This can be a new dispatch-table version or a service object behind the
existing table. It should be designed once for OMS and FreeMIDI, not duplicated
in either adapter.

### USB-MIDI topology

The portable core already discovers MIDIStreaming jacks and derives endpoint /
cable / embedded-jack logical ports. A native FreeMIDI adapter should map each
usable USB-MIDI cable across every MIDIStreaming interface to the corresponding
FreeMIDI logical port(s), preserving endpoint direction and the device's stable
identity. A duplex cable may be one logical
FreeMIDI port with input and output capabilities; a unidirectional jack may
need only one side. The exact FreeMIDI record shape is unresolved, so this is
an integration requirement, not a recovered ABI fact.

### OMS coexistence and arbitration

FreeMIDI's “Use OMS when available” mode is an interoperability fallback. It
does not prove that a native FreeMIDI driver is unnecessary, and it means a
FreeMIDI application may already consume an OMS-published device list.

If native OMS and FreeMIDI adapters are both active, the extension must remain
the sole USB owner. It needs either:

* one extension-owned reader that fans out decoded input to both adapters and
  serializes output submissions; or
* an explicit ownership policy that lets exactly one adapter consume the
  hardware while the other is disabled/bridged.

The current one-ring/one-callback table cannot safely support two independent
consumers: one will drain bytes before the other, and the callback registration
is singular. This is a concrete blocker, independent of the missing FreeMIDI
ABI.

## Minimum viable adapter skeleton (description only)

No implementation is proposed in this report. Once the missing ABI is
authenticated, the smallest sensible module boundary is:

* `freemidi_driver_entry` — the 68K/resource-facing entry wrapper; validates
  the selector and forwards to the native adapter using the verified Mixed
  Mode contract.
* `fm_driver_init` — acquires the shared USBMIDI9 service, snapshots devices,
  installs no USB ownership, and creates adapter state.
* `fm_driver_setup_info` — fills the opaque descriptor/port information record
  required by the actual FreeMIDI ABI, including names and stable identity.
* `fm_driver_register_callbacks` — registers the authenticated receive/output
  hooks and owns their lifetime until teardown.
* `fm_port_table` — maps FreeMIDI port identity to USB interface, endpoint,
  cable, direction, and generation; it must invalidate stale entries on remove.
* `fm_receive_deferred` — drains the shared byte/packet service at task time,
  decodes USB-MIDI packets, handles SysEx state, timestamps according to the
  recovered contract, and invokes FreeMIDI delivery.
* `fm_submit_output` — accepts the recovered FreeMIDI output/buffer form,
  converts to USB-MIDI event packets, and submits through the extension's
  outbound service; it must define backpressure and buffer completion.
* `fm_driver_remove` — withdraws ports, stops callbacks, flushes/abandons
  buffers according to the real contract, and releases the shared service.
* a resource/build layer — creates the Finder file, BNDL/FREF, signature,
  version, 68K wrapper, and PPC PEF without embedding USB transport code.

The names above are USBMIDI9 design names, not FreeMIDI exported symbols.

## PowerPlug client layer: verified, but not the hardware ABI

`FreeMIDI PowerPlug.data` is a PPC PEF with one code section and a descriptor
section. Its loader reports one imported library (`InterfaceLib`) and nine
imports; the export names are stored as adjacent-offset strings, so a naive
NUL-string parser makes them appear concatenated. Using the export offsets and
the descriptor section yields the following exact paths:

| Export | descriptor offset → PPC code offset | observed operation |
|---|---|---|
| `FMSSendMidi` | `0x57c → 0x1b60` | forwards five PPC register arguments through the internal service thunk at `0x3914`; several are narrowed to 16 bits |
| `FMSSendMidiTimeStamped` | `0x564 → 0x1c44` | forwards the send arguments and stores PPC `f1` to a stack 8-byte slot before the service call |
| `FMSReadInputQueue` | `0x544 → 0x1d5c → 0x3800 → 0x386c` | queue helper writes a 32-bit value to `*r4`, an 8-byte `lfd/stfd` value to `*r5`, then advances by 0xc bytes; returns zero when empty or the next record pointer on success |
| `FMSReadInputTimeStamped` | `0x53c → 0x1d84 → 0x3800` | same internal queue family, without the extra output-pointer setup visible in the ordinary wrapper |
| `FMSGetCurrentTime` | `0x9db → 0x2d5c` | obtains/initializes a time service object and returns its current 32-bit word through the service state |
| `FMSTimeGetPollFunction` | `0xdec → 0x3550` | obtains the time-service poll routine |

The queue helper at `0x386c` is particularly useful negative evidence: its
timestamp is an 8-byte PPC floating value in the application/client queue,
not proof of the native DDef receive record. The PowerPlug exports are client
entry points and should not be copied into a hardware-driver adapter.

## Smallest decisive future G4 experiment

No G4 experiment was performed. The following is the smallest safe future
experiment now that the direct 68K frame is recovered. It deliberately avoids
the still-unrecovered native handler/service bodies.

### Artifact

Build one inert file named `USBMIDI9 FreeMIDI Probe` with a dedicated creator
such as `U9FM`, Finder type `'DDef'`, and a BNDL/FREF mapping to `'DDef'`. It
must contain only the minimum resource set accepted by the target FreeMIDI
version: version/signature/icon metadata, the wrapper `'DDef'` resource, and
the native code resource form proven for that version. It must not import
USBManager, Name Registry, or USBMIDI9, claim a device, allocate a port, send
MIDI, or install a persistent callback. Its entry should log selector, `par1`,
`par2`, and return value into a fixed preallocated ring and return the
authenticated harmless status for each observed selector.

The exact `DDef` resource ID and whether a null icon is accepted still require
one observation; those are packaging/acceptance facts, not a Mixed Mode
blocker. An authentic Roland driver may be used only as a non-modified
positive-control reference, not copied into USBMIDI9.

### Placement and run

1. Clone the test OS 9.2.2 System Folder and record its original FreeMIDI
   Folder contents.
2. Place only `USBMIDI9 FreeMIDI Probe` in
   `System Folder:FreeMIDI Folder:`. Do not replace the System Extension,
   OMS files, or any USB driver.
3. Launch FreeMIDI Setup once. Do not connect a USB device for the first
   discovery/lifecycle run.
4. Capture the fixed-ring log using the companion reader or debugger after
   FreeMIDI Setup has displayed/ignored the driver. The expected positive
   sequence is an entry call for setup, a descriptor/setup call, optional
   callback registration, and teardown on quit. The exact selector order is
   an observation to verify, not an assumption.
5. A successful inert load must not create a USB port or alter the existing
   device list. A failure is either no entry call (discovery predicate), an
   entry with an unexpected argument shape/selector, a rejected return, or a
   crash; record which one.

### Logging, rollback, and restart minimization

The first run should use a fixed preallocated memory log or debugger output,
not File Manager I/O from the entry/callback. If FreeMIDI caches its driver
list, quit FreeMIDI Setup before changing anything and make at most the one
restart explicitly required by the target installation. Roll back by quitting
FreeMIDI Setup, removing only the probe from the cloned FreeMIDI Folder, and
restoring the recorded folder contents. Do not touch the repository or
production G4 artifacts. This plan separates discovery/lifecycle from USB
transport, so no MIDI device or outbound endpoint is needed for the decisive
first result.

## Recovered boundaries and implementation readiness

The exhaustive 1.45 pass completed the static work that can be completed from
the present corpus. The remaining items are exact boundaries, not broad
unknown labels:

| Item | Status | Exact boundary |
|---|---|---|
| Outer DDef ABI | safe now for binary analysis | CODE1 `0x1d0dc`, MOTU DDef `0x5c/0xd0`: packed long/word/long, caller cleanup, callee `rts`, low `d0` word |
| Discovery and `object+0x118` | static boundary documented | CODE1 `0x27d30`, `dc.w $aa52`; candidate/resource predicate is external service state |
| Selector 7 common prefix | safe now | creator `+0`, version/capability word `+4`, icon handle `+6`, flags `+a/+b`, name start `+c`; remainder opaque through 268 bytes |
| Selector 9 | requires one dynamic observation | CODE1 `0x1d112`, literal `lea.l $c1a2.l`; loaded/relocated target unavailable in raw CODE1 |
| Roland receive | safe to exact callback boundary | DDef `+0x44c`, helper `+0x414`, port record `+0x6a`, `jsr (a1)` |
| PowerPlug queue/output service | requires one dynamic fragment observation | PowerPlug `+0x3914`, indirect table target; client frames and queue records are static |
| MOTU 68K-to-PPC transition | safe now for binary transition analysis | Code 128 AAFE descriptor at resource `+0x00`, PEF at `+0x20`, ProcInfo `0xEE5`, DDef `jsr (a0)` at `+0xde` |
| Roland CFM/XCOF transition | exact setup boundary; internal service requires dynamic trace | DDef `+0x118`, XCOF `10000`, `mixd`/`cfrg`/`sysa` lookup, repeated `AA5A` calls |
| Pure 68K inert lifecycle probe | safe only as a probe specification | no PPC/USB/port publication; see [freemidi-68k-probe-plan.md](freemidi-68k-probe-plan.md) |
| Production transport driver | not safe now | callback signature/context, discovery predicate, native output target and ownership remain boundary-specific |
| SDK or another FreeMIDI version | not required for the 1.45 result | useful for future corroboration only; its absence is not evidence exhaustion |

The complete call-site, handler, selector-7, IDvr, receive, output, and audit
ledger is in [freemidi-driver-abi-evidence.md](freemidi-driver-abi-evidence.md).

## Go/no-go

**GO** for a research branch that extends the shared USBMIDI9 service boundary,
builds host-side topology/arbitration tests, and preserves this evidence.

**NO-GO** for a production native FreeMIDI adapter or installation of the
probe. A pure 68K inert probe can be specified for a later controlled run, but
the unresolved dynamic/native boundaries must not be guessed.

FreeMIDI OMS compatibility mode remains a useful fallback for applications
that can route through OMS. It is not evidence that the native adapter can be
omitted: it does not expose a native FreeMIDI hardware-driver contract and it
does not solve coexistence or direct FreeMIDI port publication.

## Provenance and hashes

The proprietary/authentic material remains outside the repository under
`/home/vadim/research/oms/`. It must not be committed or redistributed. The
paths and hashes below identify exactly what was inspected.

### Original/archive and extracted artifacts

| Local artifact | SHA-256 | Provenance |
|---|---|---|
| `Install_FreeMIDI_1.45.sit` | `06be2bcc5343383fa41d0c4eed8aac4fff32ae13d3ff0858bab2c19b9ba698e3` | Local archive; MOTU FreeMIDI 1.45 installer. Archive-page lead: [Macintosh Repository FreeMIDI](https://www.macintoshrepository.org/32705-freemidi). Mirror record: `https://old.mac.gdn/apps/Install_FreeMIDI_1.45.sit`. |
| `MOTU_USB_Install-FreeMIDI_1.41.sit` | `89a248eb017a4880ec57cda0a68d3a0207774dc608bfb37108f81bcd966b866e` | Local archive; MOTU USB/FreeMIDI 1.41 installer. |
| `fm145/Install FreeMIDI 1.45` | `c6a0b8b3bddbff7cba2c2e17d417d7eebc30f14905178fe2ce122b8ffaee7264` | Extracted installer data fork. |
| `fm145/Install FreeMIDI 1.45.rsrc` | `e503a9812aa605c94e6d6d4281861c37893700d118376dbbb0d4bf162b157acd` | Extracted installer resource fork. |
| `fm145x/x/Files To Install/FreeMIDI System Extension.rsrc` | `e04e8ae9fd9eaba60b41251f4a04e30112d8b9e2216fd8e08870b0683ac0169e` | FreeMIDI 1.45 System Extension resource fork. |
| `fm145x/x/Files To Install/FreeMIDI PowerPlug.data` | `2159e638b539e3bf57692418d31931134bbf5c88a8822ab5eb2b7a9d567fbe60` | FreeMIDI 1.45 PPC PEF data fork. |
| `fm145x/x/Files To Install/FreeMIDI PowerPlug.rsrc` | `ddab42cab7be70e909edaac7ff863ebeab3f8d01f5abbcd896f41125351bf191` | FreeMIDI 1.45 PowerPlug resource fork. |
| `fm145x/x/Files To Install/FreeMIDI Folder/MOTU USB FreeMIDI Driver.rsrc` | `09bec550f0e5a498aca4189d7345edfddbcd99302eb9796770e7fd7591566794` | MOTU USB FreeMIDI driver resource fork, version resource says 1.42. |
| `fm145x/x/Files To Install/FreeMIDI Folder/~Standard Interface Driver.rsrc` | `4deb07db9bdcdaa6cca7ca9325048ce5e366e4d0bbeb801c163a7ab7cb510286` | MOTU Standard Interface driver resource fork. |
| `sc8850/fm-x/x/SC8850 USB Driver.rsrc` | `bb101bda0945fd2aaec0f3fa16ea7e44b989be8565f0b42b88270ff7df794248` | Roland SC-8850 FreeMIDI driver resource fork; version resources say 2.00. |
| `sc8850/fm-x/x/USBSC8850Driver.data` | `5f98414eed4d7e881ad4629ac5d4569a633b7e228d090bc5e0dfd2078d2b9b90` | Roland USB class-driver PEF data fork, inspected only for comparison. |
| `sc8850/sc8850_usb_fm_v20e/README` | `6822c48dd80f1b0f0e605589deb523437775b57431f66dd589809da104216ac0` | Roland README; original support package is linked below. |

### Temporary derived analysis blobs

These were generated under `/tmp` and are not repository artifacts:

| Derived blob | SHA-256 | Meaning |
|---|---|---|
| `motu-ddef-128.bin` | `b6f4664933c861fc531e3c3e2584fd960f6fd7fe9bca36f843ba78339efaa1ae` | Extracted MOTU `DDef 128` body. |
| `motu-code-128.bin` | `e15194750f793ad671e33e9f96ca19b3bf33544621de142e837d528a429fbdea` | Extracted MOTU `Code 128` body. |
| `roland-ddef-0.bin` | `0d9b22708a15dd66b39d78c4071e1c7a81a38a6d5767380a570b0f4b583e2b7a` | Extracted Roland `DDef 0` body. |
| `roland-idvr-128.bin` | `1f7226bdb12c804c6cc9fe56e865e7d84d6a55b05a2361c5490bfb02a5e6d19e` | Extracted Roland `IDvr 128` body. |
| `roland-xcof-10000.bin` | `8845de1b10e497db2a66e5ce5e1711a6b106376dee874fff9ad060fb407860e1` | Extracted Roland `XCOF 10000` PEF body. |
| `CODE1` | `bbefde2c6f46e7c00969c7e522c7c7cae1111ed544194bd3d02e362a55effcc8` | Extracted FreeMIDI 1.45 68K System Extension `CODE 1`. |
| `DATA0` | `023b4e68e7550336cda47a74b68d0f0c4cb6b2bd07ba7f2448a9fef3040e2fb2` | Extracted FreeMIDI 1.45 `DATA 0`. |
| `code200.pef` | `a50bdbc7f3df6d042d9fb7ed0d9d05e4bb678edbead96a7ecaf0aba2e67025c5` | Stripped PPC PEF from System Extension `Code 200`. |
| `code201.pef` | `094a0c0a7953023ea5c1127c12fa48143f3df34040343e54de9c909776efc968` | Stripped PPC PEF from System Extension `Code 201`. |

### Primary and contemporary web sources

* MOTU/FreeMIDI archive listing and 1.45/1.48 leads: [Macintosh Repository
  FreeMIDI](https://www.macintoshrepository.org/32705-freemidi). The page lists
  1.48 as `usb_fm_1.48.sit_.hqx` and the archive SHA-1, but the direct download
  was inaccessible during this run.
* Roland original support index: [Roland legacy archive downloads](https://www.roland.com/global/support/archives/archive_downloads_n-s/),
  entry “SC-8850 FreeMIDI USB Driver Ver.2.0 for MacOS”.
* Roland original readme: [SC-8850 FreeMIDI USB driver readme](https://static.roland.com/support_archive/en/SC-8850_1812761_readme_en.html).
* Roland original package URL recorded by the support page:
  `https://lib.roland.co.jp/support/en/downloads/res/1812761/sc8850_usb_fm_v20e.hqx`.
  The local extracted package was obtained through the research corpus, not
  redistributed here.
* Contemporary official MOTU manual: [MOTU USB MIDI Interface User Guide](https://cdn-data.motu.com/manuals/midi/MOTU_USB_MIDI_Interface_User_Guide_Mac.pdf),
  Appendix D. It describes the USB system extension communicating with OMS or
  FreeMIDI, but does not publish the native third-party driver ABI.
* Korg contemporary installation documentation: [OASYS PCI installation PDF](https://cdn.korg.com/us/support/download/files/bbb125ba93b9d23975cc5b713d86c273.pdf),
  FreeMIDI installation section. This is independent corroboration of the
  FreeMIDI Folder placement.
* MOTU support-file archive lead: [FreeMIDISupportFiles.sit in the Wayback Machine](https://web.archive.org/web/19970114002459id_/http://motu.com/downloads/FreeMIDI/FreeMIDISupportFiles.sit).
  These are device-description/support files, not the SDK or transport ABI.
* SDK-access contemporary report: [Sweetwater MOTU developer kits notice](https://www.sweetwater.com/sweetcare/articles/motu-issue-developer-kit-for-freemidi-mas-unisyn-profile/).
  This is secondary evidence and is not being used to infer declarations.
* Contemporary developer discussion: [comp.music.midi FreeMIDI SDK thread](https://groups.google.com/g/comp.music.midi/c/JLY2tD8YfqY).
  Lead only; no ABI claim rests on it.
* 1.48 mirror lead: [ReByte `fm1.48.hqx` record](https://rebyte.me/en/motu/160427/file-1550311/).
  It reports the filename, 5.00 MB size, and MD5 above; the endpoint rejected
  the retrieval, so it is not a locally authenticated artifact.

The local provenance ledger is
`/home/vadim/research/oms/PROVENANCE.md`. It records mirror provenance and
explicitly prohibits committing or redistributing the proprietary artifacts.

## Repository state and safety record

### Run record

* initial branch: `main`;
* initial HEAD: `64c8816246ab520e045f6639f953aab40dc052c0`;
* initial status: modified `docs/freemidi-driver-research.md`; untracked
  `RD2-SESSION.TXT`, `RD2-SESSION2.TXT`, `docs/freemidi-driver-abi.md`, and
  `docs/freemidi-driver-abi-evidence.md`;
* available tools used: `/home/vadim/.local/share/fnm/node-versions/v22.22.3/installation/lib/node_modules/@openai/codex/node_modules/@openai/codex-linux-x64/vendor/x86_64-unknown-linux-musl/codex-path/rg`, `/usr/bin/r2`, `/usr/bin/objdump`, Python 3 with Capstone 5.0.7, and a generic scanner added at `tools/re/scan_freemidi_calls.py`;
* checkpoint log: `/tmp/fmabi-20260822/checkpoint.log`;
* preserved session hashes: `RD2-SESSION.TXT` =
  `de52a5fd32de47ec0c5722a4a63a6931d108373fcf5fd52da1d47f772fd98c52`,
  `RD2-SESSION2.TXT` =
  `f4e884fcb356ca45b06065a67a4403844a679b5ab6344a120394f394ce17f2c4`.

At the beginning of this recovery run:

* branch: `main`;
* HEAD: `64c8816246ab520e045f6639f953aab40dc052c0`;
* status: modified `docs/freemidi-driver-research.md`, untracked
  `RD2-SESSION.TXT`, `RD2-SESSION2.TXT`, `docs/freemidi-driver-abi.md`, and
  `docs/freemidi-driver-abi-evidence.md`;
* no `AGENTS.md` was present;
* no source files, resource forks, build projects, OMS files, G4 artifacts, or
  session logs were modified;
* all new extraction/inspection material was kept in `/tmp` or the existing
  external research corpus;
* no Power Mac G4 installation, reboot, or experiment was performed.

The intended repository changes from this recovery are the three corrected/
expanded ABI documents, `docs/freemidi-68k-probe-plan.md`, and the generic
scanner `tools/re/scan_freemidi_calls.py`. The RD2 session files remain
untracked and untouched.
