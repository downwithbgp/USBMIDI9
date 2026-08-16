# Classic Mac OS USB Driver — Research and Design (M1A + M1B)

Status: **M1A research/design gate complete; M1B source gate complete;
M1B hardware gate PASSED for matching, dispatch, and bulk receive
(G4).** The driver and probe source exist (`classic/usb_driver.c`,
`probe/probe.c`, see §9) and are compile-checked on Linux against stub
headers (`make check-classic`). On the real Power Mac G4, generic
MIDIStreaming interface matching, driver loading, dispatch/enumeration,
bulk receive, and real USB-MIDI packet reception all PASSED (exact
packets in §9.8). The earlier bulk-read hang (completion-context
re-entry into `USBBulkRead`) was fixed by the execution-level-gated safe
bulk-read helper (§9.8). One defect remains unresolved: a hard system
freeze when an UNRELATED USB device is unplugged/plugged while USBMIDI9
is active (code audit done, hardware isolation pending — §9.9). The M1B
hardware acceptance checklist is §9.5.
Everything below is sourced
from primary historical Apple documentation; assertions carry a citation of
the form

    Apple Mac OS USB DDK API Reference, Rev. 26 (12/23/99)
    [Chapter, "Section", p. N]

The primary document (the 252-page PDF) was obtained from Apple's own
archive and is stored as local research material OUTSIDE this repository
(see "Source/provenance table"). The repository contains only summaries and
citations, per the source-discipline rules.

---

## 1. Verified driver model

### 1.1 Binary/module format and installation

* A Mac OS USB class driver is a **CFM (Code Fragment Manager) shared
  library** with **file type `'ndrv'` and creator `'usbd'`**.
  [Ch 4, "Mac OS USB Driver Overview", p. 53]
* The USB Manager discovers drivers by scanning files of type `'ndrv'` /
  creator `'usbd'`, searching every code fragment for the exported symbol
  `TheUSBDriverDescription`, and checking that the first four bytes match
  the signature constant `kTheUSBDriverDescriptionSignature` (`'usbd'`).
  [Ch 4, p. 53]
  *M1A.1 note:* Rev 26's prose names the exports `USBDriverDescription` /
  `USBClassDriverPluginDispatchTable`, but the authentic DDK 1.4.1 export
  list (`Interfaces/USBClassDriver.exp`) and every sample source export
  **`TheUSBDriverDescription`** and **`TheClassDriverPluginDispatchTable`**
  — those are the symbols to export. The signature constant in USB.h 1.4.1
  is `kTheUSBDriverDescriptionSignature` (the docs previously wrote
  `kUSBDriverDescriptionSignature`; the value is the same, `'usbd'`).
* Version 1.0–1.1 USB software: one code fragment per driver file.
  Beginning with version 1.2, multiple driver CFMs per file are supported
  (built as separate shared libraries merged with CodeWarrior's "Mac OS
  Merge" into one `'ndrv'`/`'usbd'` file).
  [Ch 4, p. 53 note; Appendix A, "Major Features Introduced In Version 1.2", p. 227]
* USB drivers are "essentially code fragments"; the Code Fragment Manager
  reference (Inside Macintosh: PowerPC System Software, Ch 3) is the
  background reading Apple recommends. [Preface, "Supplemental Reference Documents", p. 15]
* USB class drivers are loaded into the **System Zone**.
  [Ch 4, "Detecting USB Device Presence", Listing 4-1, p. 83]

### 1.2 Required exports

A class driver must export exactly two symbols for the USB software to load
it, plus any driver-specific exports for client (shim/probe) communication:

1. **`TheUSBDriverDescription`** — identifies the driver to the USB Manager
   and describes what it matches. [Ch 4, "USBDriverDescription Structure", p. 60]
2. **`TheClassDriverPluginDispatchTable`** — the driver entry points.
   [Ch 4, "USBClassDriverPlugInDispatchTable Structure", p. 65]
3. *Optional:* additional exports for communication with a shim or other
   service — "Typically, class drivers may require additional exports to
   facilitate communication with a shim of other Mac OS service."
   [Ch 4, p. 65; Ch 4, "Communicating With Client Processes", p. 72-75]
   The DDK's HID modules do exactly this: `USBHIDModule.exp` adds
   `TheHIDModuleDispatchTable`; `UniversalHIDModule.exp` adds
   `TheUHIDModuleDispatchTable`. [DDK 1.4.1 Interfaces/]

Structure verified against the authentic header (USB.h 1.4.1; the document
itself uses both "PlugIn" and "Plugin" spellings — noted as a draft
inconsistency):

```c
struct USBClassDriverPluginDispatchTable {
    UInt32                          pluginVersion;      /* kClassDriverPluginVersion, from USB.h */
    USBDValidateHWProcPtr           validateHWProc;     /* verify proper HW */
    USBDInitializeDeviceProcPtr     initializeDeviceProc; /* init the class driver (device) */
    USBDInitializeInterfaceProcPtr  initializeInterfaceProc; /* init a particular interface */
    USBDFinalizeProcPtr             finalizeProc;       /* finalize the class driver */
    USBDDriverNotifyProcPtr         notificationProc;   /* notifications to the driver */
};
```

* `pluginVersion` must be `kClassDriverPluginVersion`, defined in the
  header `USB.h`; **verified value 0x00001100** (USB.h 1.4.1). The USB
  Manager uses it to distinguish future versions of the table.
  [Ch 4, p. 65]
* Verified proc-pointer signatures (USB.h 1.4.1):
  * `USBDValidateHWProcPtr (USBDeviceRef device, USBDeviceDescriptorPtr pDesc)`
  * `USBDInitializeDeviceProcPtr (USBDeviceRef device, USBDeviceDescriptorPtr pDesc, UInt32 busPowerAvailable)`
  * `USBDInitializeInterfaceProcPtr (UInt32 interfaceNum, USBInterfaceDescriptorPtr pInterface, USBDeviceDescriptorPtr pDevice, USBInterfaceRef interfaceRef)`
  * `USBDFinalizeProcPtr (USBDeviceRef device, USBDeviceDescriptorPtr pDesc)`
  * `USBDDriverNotifyProcPtr (USBDriverNotification notification, void *pointer, UInt32 refcon)`
    (three-argument form; the header comments "Added refcon for 1.1 version
    of dispatch table". Note the type name is **`USBDDriverNotifyProcPtr`** —
    no `USBDNotificationProcPtr` exists in USB.h.)
* For **device** loading, `validateHWProc`, `initializeDeviceProc` and
  `finalizeProc` are required; nil ⇒ `kUSBBadDispatchTable` and load fails.
  [Ch 4, p. 65-66] *M1A.1 note:* the DDK 1.4.1 shipping HID modules
  (KeyboardModule, MouseModule, UniversalModule) ship `validateHWProc = 0`
  and still load on 1.4.1 software — the strict Rev 26 reading does not
  obviously match the 1.4.1 implementation. For USBMIDI9 (interface
  driver) this is moot: only `initializeInterfaceProc` + `finalizeProc`
  are required, and we will still provide a `validateHWProc` stub
  returning `kUSBNoErr` (harmless, and matches the documented rule).
* For **interface** loading, `initializeInterfaceProc` and `finalizeProc`
  are required; nil ⇒ `kUSBBadDispatchTable` and interface load fails.
  [Ch 4, p. 66]
* `notificationProc` is optional but strongly advised, for hot-unplug
  handling. [Ch 4, p. 66]
* All dispatch-table entry points are called **synchronously at system
  task time**. [Ch 4, p. 65-66]

Documented `USBDriverDescription` (Rev 26, p. 60-64):

```c
struct USBDriverDescription {
    OSType                  usbDriverDescSignature;   /* kTheUSBDriverDescriptionSignature */
    USBDriverDescVersion    usbDriverDescVersion;     /* kInitialUSBDriverDescriptor */
    USBDeviceInfo           usbDeviceInfo;            /* Product & Vendor Info */
    USBInterfaceInfo        usbInterfaceInfo;         /* Interface info */
    USBDriverType           usbDriverType;            /* Driver Info */
    USBDriverLoadingOptions usbDriverLoadingOptions;  /* Options for class driver loading */
};

struct USBDeviceInfo {
    UInt16 usbVendorID;             /* 0 = not used for matching */
    UInt16 usbProductID;            /* 0 = not used for matching */
    UInt16 usbDeviceReleaseNumber;  /* BCD device version; 0 = not used */
    UInt16 usbDeviceProtocol;       /* 0 = no device-specific protocol */
};

struct USBInterfaceInfo {
    UInt8 usbConfigValue;           /* 0 = not used for matching */
    UInt8 usbInterfaceNum;          /* 0 = not used for matching */
    UInt8 usbInterfaceClass;        /* required value for class matching */
    UInt8 usbInterfaceSubClass;     /* required value for subclass matching */
    UInt8 usbInterfaceProtocol;     /* 0 = not used for matching */
};

struct USBDriverType {
    Str31       nameInfoStr;        /* driver name for the Name Registry */
    UInt8       usbDriverClass;     /* device-class matching */
    UInt8       usbDriverSubClass;  /* device-subclass matching */
    NumVersion  usbDriverVersion;   /* driver version; used to distinguish
                                       class drivers since USB software 1.2 */
};

/* usbDriverLoadingOptions bits (Rev 26, p. 64; values verified in USB.h 1.4.1): */
/* kUSBDoNotMatchGenericDevice = 0x00000001 */
/* kUSBDoNotMatchInterface     = 0x00000002 */
/* kUSBProtocolMustMatch       = 0x00000004 */
/* kUSBInterfaceMatchOnly      = 0x00000008 — defined in USB.h 1.4.1 */
```

Doc-internal inconsistencies recorded: the sample on p. 64 spells the first
flag `kUSBDoNotMatchGeneric` and its comments mix HID class values; the
matching figures (4-1/4-2) reference an "InterfaceMatchOnly" flag. **M1A.1
resolution:** the authentic header defines `kUSBDoNotMatchGenericDevice`,
`kUSBDoNotMatchInterface`, `kUSBProtocolMustMatch` (as in the docs' list)
**and** `kUSBInterfaceMatchOnly = 0x00000008` ("Only load this driver as an
interface driver") — so the figure references are valid; the p. 64 list was
simply incomplete. (Rev 26's Appendix A also uses the short name
`kUSBDoNotMatchGeneric` in prose — header name is authoritative.)

### 1.3 Verified driver lifecycle

```
unloaded
   |  USB Manager recognizes device (startup or hot plug), assigns USB
   |  address, opens control pipe to endpoint 0, reads device descriptor
   v
matched (device or interface, per Section 1.4)
   |  USB Manager loads the CFM fragment, finds the two required exports,
   |  then calls, synchronously at system task time:
   v
validateHWProc          (device drivers only; interface drivers skip it)
   |  driver checks device descriptor; return kUSBNoErr to accept
   v
initializeDeviceProc / initializeInterfaceProc
   |  driver allocates memory, starts an asynchronous state machine to
   |  configure the device/interface, then returns kUSBNoErr
   v
active: driver runs (USL calls, completion routines, notifications)
   |  ...
   |  device disconnected (hot unplug)
   v
notificationProc(kNotifyDriverBeingRemoved)     [system task time]
   |  may return kUSBDeviceBusy to postpone finalize while a client holds
   |  the driver; USB software re-sends the notification until kUSBNoErr
   v
finalizeProc                                    [system task time, sync]
   |  must have no USB calls pending; returns kUSBNoErr
   v
fragment unloaded
```

Key verified rules:

* `initializeInterfaceProc` is called **synchronously at system task
  time**; if it returns anything other than `kUSBNoErr` the driver is not
  used and the USB Manager tries the next-best matching driver.
  [Ch 4, "InitializeInterfaceProc Function", p. 69]
* Before returning, the driver must complete any calls that must be made at
  task time, then "initiate an asynchronous state machine process" — the
  documented DDK sample pattern is an `initiateTransaction` routine driven
  by the `usbRefcon` field of the USBPB as a selector; each completion
  routine advances the state machine and issues the next USL call.
  [Ch 4, p. 68-69]
* `finalizeProc` is called at system task time, synchronously; when it
  returns the fragment may be unloaded. **A crash can occur if finalize is
  called while USB calls are pending completion** (the fragment is unloaded
  and later completion routines become invalid). [Ch 4, "FinalizeProc Function", p. 70-71]
* `kNotifyDriverBeingRemoved` is delivered before `finalizeProc`; it is the
  driver's last chance to clean up. [Ch 4, p. 70-71]
* Class drivers "typically operate at secondary interrupt time, under
  interrupt conditions"; many Toolbox calls are illegal there. The
  `initialize*Proc` and `finalizeProc` run at system task time.
  [Ch 4, "Mac OS USB Compatibility With Mac OS Toolbox Calls", p. 86]

### 1.4 Matching process

Device matching (rank order, highest first; Figure 4-1, p. 55):
1. vendor + product match (release number match adds 1) — rank 10/9
2. device class vendor-specific (0xFF) + vendor + subclass + protocol — rank 8
3. generic (DoNotMatchGenericDevice clear): class + subclass + protocol — rank 7
4. generic: class + subclass, `kUSBProtocolMustMatch` clear — rank 6

Interface matching (Figure 4-2, p. 57-58):
1. vendor + product + config value + interface number (release +1) — rank 10/9
2. interface class vendor-specific: vendor + interface subclass (+protocol) — rank 8/7
3. generic: interface class + subclass + protocol — rank 6
4. generic: interface class + subclass, protocol wildcard (`kUSBProtocolMustMatch` clear) — rank 5
5. `kUSBDoNotMatchInterface` set — never matched as interface driver (rank 0)

Rules for a **generic class driver** (our case — USB-MIDI):

* Leave `kUSBDoNotMatchGenericDevice` **clear** to participate in generic
  class/subclass matching; leave `kUSBProtocolMustMatch` clear to match
  regardless of protocol (device protocol 0 = wildcard).
  [Ch 4, p. 56-58, 64; Appendix A, "Understanding Generic Drivers", p. 231]
* "Don't abuse the load generic ability. Always check for your device(s) in
  a validateHW function." — but note that **`validateHWProc` is NOT called
  for interface drivers**; an interface driver declines by returning an
  error from `initializeInterfaceProc`, after which the next-best driver is
  tried. [Appendix A, p. 231; Ch 4, p. 58]

### 1.5 Composite devices (our Keystation case)

* Devices with device class 0 / subclass 0 are composite: the Apple USB
  composite class driver is loaded, picks a configuration based on
  available power/bandwidth, and calls `USBExpertInstallInterfaceDriver`
  for each interface. The USB Manager then matches interface drivers from
  the interface descriptor's class/subclass/protocol.
  [Ch 4, p. 56; Ch 5, "USBExpertInstallInterfaceDriver", p. 145]
* When the interface driver is loaded, **`validateHWProc` is not called**;
  `initializeInterfaceProc` is called directly. [Ch 4, p. 58]
* At startup, USB software ≤1.0.1 used the ROM generic composite driver
  and did not re-check for better disk-based drivers; v1.1+ re-match when
  the file system becomes available. [Ch 4, "Matching Class Drivers to Composite Devices", p. 59]
* Device drivers must issue `SetConfiguration`; **interface drivers cannot
  set a configuration** — the composite driver owns it. Interface drivers
  identify interfaces, find one to activate, open the pipes in the
  interface, and handle I/O. [Ch 4, "Device Driver and Interface Driver Matching Differences", p. 59]
* The Keystation 49e (class 0, subclass 0 device with AudioControl +
  MIDIStreaming interfaces) will be handled as a composite device: the
  composite driver configures it, and USBMIDI9 is matched as an **interface
  driver** for interface 1 (class 0x01, subclass 0x03).

### 1.6 Execution-context rules (verified)

* Completion routines: "The execution level that the completion routine may
  be called back at is not guaranteed... Completion usually occurs at
  secondary interrupt level, or at system task level." Use
  `CurrentExecutionLevel` to discover it; `CallSecondaryInterruptHandler2`
  to continue at secondary interrupt level; `USBDelay*` to transition to
  task level. All USL functions are safe from secondary interrupt level or
  system task level. [Ch 5, "Asynchronous Call Support", p. 102]
* Never poll from secondary interrupt time (secondary interrupts are queued
  and USL completion happens there — a poll would never see completion and
  can hang the system). Polling at task level only for a limited time with
  `USBGetFrameNumberImmediate`; generally use completion routines.
  [Ch 5, "Polling Versus Asynchronous Completion (Important)", p. 103]
* Memory: `USBAllocMem` is preferred over `NewPtrSys` (designed to use the
  appropriate allocator per system software release); `NewPtrSys` may be
  used during the initialize procs. [Ch 4, p. 86; Ch 5, "USBAllocMem", p. 150]

---

## 2. Version compatibility (USB software 1.0 → 1.4)

* The Rev 26 document covers USB software versions 1.0 through 1.4;
  1.3.5 was current at the time of writing. [Ch 3, p. 47; Appendix A]
* **Structure version fields that matter:**
  * `usbDriverDescVersion` — set to `kInitialUSBDriverDescriptor`.
    [Ch 4, p. 60]
  * `pluginVersion` — `kClassDriverPluginVersion` (named
    `kUSBClassDriverPluginVersion` in Appendix A — doc-internal
    inconsistency). **The notification-proc signature depends on it:**
    with the current version the USB Manager calls
    `OSStatus USBDriverNotifyProc(USBDriverNotification notification,
    void *pointer, UInt32 refcon)`; drivers built with older USB.h have a
    different constant and are called **without** the `refcon` parameter.
    We must build with a USB.h whose `kClassDriverPluginVersion` matches
    the three-argument notification proc we implement.
    [Appendix A, "Code Changes Required To Support The Version 1.1 USBPB", p. 226]
  * `pbVersion` — `kUSBCurrentPBVersion` (v1.0) or `kUSBIsocPBVersion`
    (v1.1, isochronous). Rev 26 says the "current" version is 1.1 and the
    v1.1 USBPB is binary-compatible with v1.0; source changes are needed
    for the union fields (`usb.cntl.*`, `OLDUSBNAMES` macro in USB.h).
    [Ch 5, p. 94-98; Appendix A, p. 226]
* **1.1:** isochronous transfers (new USBPB), multiple USB buses, improved
  startup enumeration, sleep/wake notification messages, `kUSBAddessRequest`
  control flag, `USBSetConfiguration` replaces `USBOpenDevice` (same
  behavior; old name obsolete). [Appendix A, p. 223-226; Ch 5, p. 110]
* **1.2:** multiple class-driver CFMs merged into one `'ndrv'` file;
  `usbDriverVersion` used to distinguish class drivers; bulk-transfer
  performance note (buffers should be MaxPacketSize-aligned; misaligned
  buffers cap transfer descriptors at 4K vs 8K aligned).
  [Appendix A, p. 227, 230; Ch 4, p. 63]
* **1.3:** `USBGetVersion` (MMmmRRss); 5-second no-data timeout on control
  transactions (`kUSBNo5SecTimeout` to override); `kUSBReturnOnException`
  for pending calls at unplug; `USBAddShimFromDisk`.
  [Ch 5, p. 106, 104-105, 150; Ch 6, p. 183]
* **1.4:** shims can be registered as shared libraries (`USBShimDescription`
  with `kUSBRegisterShimAsSharedLibrary` set and `libraryName`; exported
  symbol `ShimDescription`); shims and drivers may share one extension file;
  shim files use type `'usbs'`; `USBGetStringDescriptor`, `USBSetPipePolicy`,
  `USBGetBandwidthAvailableByReference`, `USBPortStatus`, HID dispatch-table
  v2 (`pHIDGetReport`/`pHIDSetReport`); USB.h moved to the Interfaces
  folder. [Appendix A, p. 228-229; Ch 4, p. 76]
* **Feature checks:** always check the USB gestalt selectors rather than
  the version number: `gestaltUSBAttr` (`'usb '`; bit 0 `gestaltUSBPresent`,
  bit 1 `gestaltUSBHasIsoch`), `gestaltUSBVersion` (`'usbv'`, MMmmRRss).
  [Ch 3, "USB Software Presence and Version Attributes", p. 45; Appendix A, p. 223]
* **1.4.2 (M1A.1-verified):** USB 1.4.2 fixes one issue — Apple Studio
  Display hubs unusable after wake-from-sleep — changes **no API**,
  reports `'usbv'` = `0x01428000`, and ships only inside the Mac OS 9
  Update (Mac OS 9.0.4); the USB Device Extension file remains 1.4.1 and
  there is no standalone installer. Consequently there is **no separate
  "USB DDK 1.4.2"**; the DDK 1.4.1 kit (USB software 1.4.1f4) is the
  authentic 1.4.x header set. (Both cited sources contain the same
  "USB 1.4.2" section — the kit's change history pre-documents the
  upcoming release.) [DDK 1.4.1 kit, "Mac OS USB Change History" §USB
  1.4.2; Apple "Mac OS USB 1.5.1f1 Change History",
  developer.apple.com/hardware/usb/download_prerelease_readme.html,
  Wayback capture 2001-06-24]
* **Verified header version constants (USB.h 1.4.1):**
  `kUSBCurrentPBVersion = 0x0100` (v1.00), `kUSBIsocPBVersion = 0x0109`
  (v1.10), `kUSBCurrentHubPB = kUSBIsocPBVersion`; the `OLDUSBNAMES` macro
  (default 0) maps `usbBMRequestType`/`usbBRequest`/`usbWValue`/`usbWIndex`
  to `usb.cntl.*`. **M1A.2 correction:** USB.h 1.4.1 is NOT types-only — it
  declares the USL prototypes itself (USBDeviceRequest, USBBulkRead,
  USBIntRead, USBConfigureInterface, USBFindNextPipe, USBAllocMem,
  USBDeallocMem, USBAbortPipeByReference, USBGetNextDeviceByClass, ...),
  gated by `#if CALL_NOT_IN_CARBON` (1 = Classic, the DDK-era default).
  The exact spellings used by the M1B driver/probe were verified against
  the actual kit header during M1B (see §8.1, §9.2). The earlier claim
  that prototypes "lived in the CodeWarrior-era Universal Headers' USB.h"
  was wrong; the M1B build can take them from the DDK header itself.

---

## 3. Answers to the 20 research questions

1. **Binary/module format:** CFM shared library, file type `'ndrv'`,
   creator `'usbd'` (single CFM per file on USB ≤1.1; merged multi-CFM
   files on 1.2+). [Ch 4, p. 53; App A, p. 227]
2. **Exported symbols:** `TheUSBDriverDescription` and
   `TheClassDriverPluginDispatchTable` (plus optional driver-specific
   exports for shims/clients). [Ch 4, p. 60, 65; USBClassDriver.exp]
3. **Dispatch table structure:** as reproduced in 1.2 (Rev 26, p. 65;
   struct verified in USB.h 1.4.1); `pluginVersion = kClassDriverPluginVersion`
   (**verified: 0x00001100**); interface loading requires
   `initializeInterfaceProc` + `finalizeProc` non-nil. The notification
   proc is `USBDDriverNotifyProcPtr`, three-argument form
   `(USBDriverNotification, void *pointer, UInt32 refcon)` — the header
   comment says the refcon was "added for 1.1 version of dispatch table";
   build against a USB.h whose `kClassDriverPluginVersion` matches the
   three-argument proc we implement (the DDK 1.4.1 header does).
   [Ch 4, p. 65-66; App A, p. 226; USB.h 1.4.1]
4. **USBDriverDescription fields for a generic interface driver:** see
   Section 4.1 (interfaceClass 0x01, interfaceSubClass 0x03, protocol 0,
   generic bits clear). [Ch 4, p. 60-64]
5. **Generic matching for class 0x01/subclass 0x03:** set
   `usbInterfaceClass = 0x01`, `usbInterfaceSubClass = 0x03`,
   `usbInterfaceProtocol = 0`, leave `kUSBDoNotMatchGenericDevice` and
   `kUSBProtocolMustMatch` clear → rank 6 (or rank 5 if protocol were
   non-matching); no VID/PID in `usbDeviceInfo`. Verify the interface in
   `initializeInterfaceProc` (decline by returning error; the next-best
   driver is tried). [Ch 4, p. 57-58, 64; App A, p. 231]
6. **Composite devices:** device class 0/subclass 0 → Apple composite
   driver selects configuration and calls `USBExpertInstallInterfaceDriver`
   per interface; our driver is loaded per-interface; `validateHWProc` is
   skipped; `initializeInterfaceProc` is the entry point. Interface drivers
   cannot SetConfiguration. [Ch 4, p. 56, 58-59; Ch 5, p. 145]
7. **initializeInterfaceProc rules:** synchronous, system task time;
   complete task-time work before returning; then start the asynchronous
   state machine; returning non-noErr declines the match.
   [Ch 4, p. 69]
8. **Opening/configuring the interface:** `USBNewInterfaceRef` (device ref
   + interface number → interface ref), `USBConfigureInterface` (opens each
   pipe in the interface; returns pipe count in `usbOther`). Note: at the
   time of Rev 26, `USBConfigureInterface` did not actually set the
   interface — a `USBDeviceRequest` set_interface is required first; the
   composite driver owns configuration for interface drivers anyway.
   [Ch 5, p. 112-114]
9. **Finding pipes:** `USBFindNextPipe` iterates pipes of an interface by
   type/direction; pass the interface ref first, then the returned pipe ref.
   [Ch 5, p. 115]
10. **Bulk IN vs bulk OUT:** `USBFindNextPipe` with `usbFlags = kUSBIn`
    + `usbClassType = kUSBBulk` yields the bulk-IN pipe (endpoint 0x81),
    and `kUSBOut` + `kUSBBulk` the bulk-OUT pipe (0x02); `usb.cntl.WValue`
    returns the endpoint's max packet size. [Ch 5, p. 115]
11. **USBPB fields for USBBulkRead:** pbLength, pbVersion,
    usbCompletion, usbRefcon, usbReference (pipe ref), usbReqCount
    (multiple of MaxPacketSize; 0 ⇒ error for non-zero transfers),
    usbBuffer, usbActCount (out), usbFlags = 0. Request MaxPacketSize
    (64) so any data terminates; short packets terminate automatically;
    check usbActCount; overrun ⇒ kUSBOverRunErr with no valid data.
    [Ch 5, "USBBulkRead", p. 128]
12. **Memory residency:** the USBPB must remain allocated until the
    completion routine runs (or an immediate error is returned); it cannot
    be reused before then. Same for the data buffer (USBAllocMem memory is
    managed with the device). MaxPacketSize alignment of the buffer and
    multiples of MaxPacketSize for `usbReqCount` are a **performance**
    consideration, not a functional requirement — see the M1A.1 correction
    in §5.5. [Ch 5, p. 101, 128; App A, p. 230]
13. **Completion context:** secondary interrupt level or system task
    level, not guaranteed; check `CurrentExecutionLevel`; only USL calls
    are guaranteed safe; Toolbox calls generally are not (class driver
    operates at secondary interrupt time). [Ch 5, p. 102; Ch 4, p. 86]
14. **Hot unplug with an outstanding bulk read:** pending calls complete
    with `kUSBNotRespondingErr` (−6911; Rev 26 prose: "kUSBNotRespondingError")
    or are aborted with `kUSBAbortedError` (−6982); never retry an
    unexpected kUSBAbortedError; order of errors vs notification is not
    guaranteed. [Ch 4, "Handling Hot Unplugging", p. 71]
15. **Removal notifications to the driver:** `notificationProc` with
    `kNotifyDriverBeingRemoved` (0x0B), at system task time, before
    finalize; also sleep notifications on PowerBooks (1.2+).
    [Ch 4, p. 70-71]
16. **Cancelling pending I/O before finalize:** call
    `USBAbortPipeByReference` for each pipe with active transactions; the
    notificationProc must wait for all transactions to complete before
    returning kUSBNoErr to kNotifyDriverBeingRemoved; otherwise the
    fragment unloads and a later completion crashes the system.
    [Ch 4, p. 71]
17. **Probe observation mechanism:** the driver exports a versioned
    dispatch table; the probe obtains the CFragConnectionID via
    `USBGetNextDeviceByClass` (or add/remove events via
    `USBInstallDeviceNotification`) and `FindSymbol`s the table; callbacks
    into the driver deliver decoded packets; SetZone(SystemZone()) around
    the lookup. [Ch 4, p. 73-74, 83-85; Ch 6, p. 179, 185-187]
18. **Exported dispatch table + FindSym vs alternatives:** the exported
    dispatch table + FindSym (the CFM API is `FindSymbol`; Rev 26 prose
    uses "FindSym") is the mechanism Apple documents for
    class-driver ↔ shim communication (HID precedent — *M1A.1:* HID.h
    documents the HID device driver's export `TheHIDDeviceDispatchTable`,
    and the DDK `.exp` files add the HID *module* tables
    `TheHIDModuleDispatchTable`/`TheUHIDModuleDispatchTable`); Apple
    explicitly notes there is **no official API** for client↔driver
    communication, and describes shim designs for other models (DRVR,
    CTB, vdig, DLPI). For a diagnostic probe, the dispatch-table approach
    is period-correct and sufficient; a DRVR/NDRV shim is only needed
    later for OMS/FreeMIDI integration. [Ch 4, p. 72-75, 81-85]
19. **CodeWarrior build/link settings (M1A.1-verified):** the DDK Readme
    states the supplied project files were created with **CodeWarrior Pro 1,
    IDE 2.0** (compatible with IDE 2.1, 3.0+), and require **Universal
    Interfaces and Libraries 3.3**; USB.h (and HID.h) from the DDK are
    copied into the Universal Headers folder. The MPW makefiles
    (USBKeypad.make) show the PPC link line: `-t 'ndrv' -c 'usbd'`,
    `-xm sharedlibrary -share context`, `-@export
    {USBInterfacesInternal}USBClassDriver.exp`, linking `USBServicesLib`
    and `InterfaceLib`; multiple driver fragments are merged into one
    `'ndrv'` file with `mergefragment` (MPW) or CodeWarrior's "Mac OS
    Merge" panel (the `.proj` files include a MacOS Merge Panel); built
    drivers land in `:Extensions-MCWBuilt:`. Export list contents are
    `TheUSBDriverDescription` + `TheClassDriverPluginDispatchTable`
    (+ driver-specific tables). [App A, p. 226-227; DDK 1.4.1 Readme;
    USBKeypad.make; USBClassDriver.exp]
20. **Headers/libraries on the G4 build system (M1A.1-resolved):** the DDK
    1.4.1 kit provides `Interfaces/USB.h` (+ HID.h, PowerClass.h,
    SerialShim.h, SIOW.h, UniversalHIDModule.h and the .exp export lists)
    and `Libraries/USBServicesLib` (+ USBManagerLib, USBPowerClassLib,
    CursorDevicesLib) — the authentic 1.4.x-era headers/import libraries.
    Note the DDK 1.4.1 USB.h is types/constants only; USL prototypes come
    from the CodeWarrior-era Universal Headers' USB.h (see §2). The Readme
    documents setup (copy USB.h/HID.h into Universal Headers; the 'usbx'
    resource ID 1984 is required for the *development* USB Support file,
    not for driver builds). [Ch 5, p. 89; App A, p. 228; DDK 1.4.1 Readme]

---

## 4. Architecture review: proposed layering vs Apple's compatibility shim

Proposed layering under review:

```text
USBMIDI9 USB class/interface driver
             |
             | USBMIDI9-neutral dispatch/service API
             |
        +----+----+
        |         |
      Probe     future shim
                  |
             +----+----+
             |         |
            OMS     FreeMIDI
```

Apple's documented model (Ch 4, "Communicating With Client Processes",
p. 72-75) **directly supports this arrangement**:

* There is **no official API** for client↔class-driver communication; the
  recommended pattern is a **compatibility shim**: a separate software
  layer that presents a familiar API to client processes and talks to the
  class driver. [Ch 4, p. 72-73]
* The shim **must not be implemented inside the class driver**: on hot
  unplug the driver fragment is unloaded, taking embedded shim code with
  it; Apple also cites Mac OS X isolation as a reason to keep shims
  separate. [Ch 4, "Where To Implement a Compatibility Shim", p. 73]
* The class driver **may export a dispatch table**; the shim obtains its
  address with `FindSym` (via the CFragConnectionID from
  `USBGetNextDeviceByClass`) and calls through it. The HID class driver is
  the worked precedent: HID.h documents the export
  `TheHIDDeviceDispatchTable` and the versioned struct
  (`dispatchTableCurrentVersion` = `kHIDCurrentDispatchTableVersion` (2),
  `dispatchTableOldestVersion` = `kHIDOldestCompatableDispatchTableVersion` (1),
  `vendorID`). *M1A.1-verified* in the DDK 1.4.1 HID.h; the HID module
  tables (`TheHIDModuleDispatchTable`/`TheUHIDModuleDispatchTable`) are the
  `.exp`-verified module-level equivalents.
  [Ch 4, p. 73-74, 76; p. 83-85; HID.h; DDK 1.4.1 .exp files]
* An application may do what a shim does, but must handle hot unplug and
  multiple devices. [Ch 4, p. 74]
* Shims are found by creator `'usbs'`; since USB 1.4 a shim can also be a
  CFM shared library registered via `USBShimDescription`
  (`kUSBRegisterShimAsSharedLibrary`), and shim + driver can share one
  extension file. [Ch 3, p. 48; App A, p. 228]

Conclusion: **the proposed layering matches Apple's compatibility-shim
model.** USBMIDI9 (class driver) exports a versioned, USBMIDI9-specific
dispatch table; the Probe and the future OMS/FreeMIDI shim(s) live in
separate code fragments and reach the driver through the documented
FindSym/USBGetNextDeviceByClass mechanism. OMS API design is deferred.

---

## 5. Proposed M1 design (interface driver for USB-MIDI)

### 5.1 Proposed matching descriptor (USBDriverDescription)

```c
/* Design (not yet implemented; constant values verified against the
   authentic USB.h from the DDK 1.4.1 kit, see §8.1). */
USBDriverDescription gUSBDriverDescription = {
    /* Signature */
    kTheUSBDriverDescriptionSignature,  /* identifies a USB class driver */
    kInitialUSBDriverDescriptor,        /* structure version (0) */
    /* Device Info — deliberately empty for a generic driver */
    0, 0, 0, 0,                         /* vendor, product, release, protocol */
    /* Interface Info — generic USB-MIDI match */
    0,                                  /* usbConfigValue: any */
    0,                                  /* usbInterfaceNum: any */
    0x01,                               /* usbInterfaceClass: Audio */
    0x03,                               /* usbInterfaceSubClass: MIDIStreaming */
    0,                                  /* usbInterfaceProtocol: any (0) */
    /* Driver Info */
    "\pUSBMIDI9",                       /* nameInfoStr */
    0, 0,                               /* device class/subclass: n/a */
    /* usbDriverVersion (NumVersion): 4 × UInt8 fields, initialized like
       the DDK samples — majorRev, minorAndBugRev, stage, nonRelRev —
       e.g. {1, 0, finalStage, 0}; value fixed at M1B */
    /* Loading Options */
    0                                   /* generic matching allowed,
                                           interface matching allowed,
                                           protocol not required to match */
};
```

Resulting interface-match rank: class + subclass + protocol match ⇒ rank 6
(rank 5 with protocol wildcard — both above any generic Apple driver that
does not match class 0x01/subclass 0x03). No VID/PID restriction anywhere:
the Keystation is not hardcoded. [Ch 4, p. 57-58, 64]

Declining non-MIDI devices of the same class/subclass (rare but possible)
happens in `initializeInterfaceProc` by validating the interface with the
portable core and returning an error, per the documented fallback to the
next-best driver. [Ch 4, p. 58]

### 5.2 Required exports

* `TheUSBDriverDescription` (above; the exported symbol name is the
  `The`-prefixed form used by the DDK `.exp` files and all samples — see
  §1.1)
* `TheClassDriverPluginDispatchTable` with:
  * `validateHWProc` — Rev 26 requires it non-nil for the table; for
    interface loading it is never called; provide a stub returning
    kUSBNoErr (note: the DDK 1.4.1 shipping HID modules ship 0 here — see
    §1.2).
  * `initializeDeviceProc` — required non-nil for the table; we are an
    interface-only driver; provide a stub returning an error (documented
    behavior: device load of USBMIDI9 is not supported).
  * `initializeInterfaceProc` — the real entry point (Section 5.3).
  * `finalizeProc` — cleanup (Section 5.6).
  * `notificationProc` — hot-unplug / sleep handling (Section 5.6).
* `USBMIDI9DispatchTable` (proposed name; final name to be fixed at M1B)
  — the driver-specific export for Probe/shim communication, modeled on
  the HID dispatch-table precedent (`TheHIDModuleDispatchTable` /
  `TheUHIDModuleDispatchTable` in the DDK `.exp` files). [Ch 4, p. 76]

### 5.3 Proposed M1 state machine

```text
unloaded
   |
   | USB Manager scans 'ndrv'/'usbd' files, matches TheUSBDriverDescription
   | (interface class 0x01, subclass 0x03), loads CFM fragment, finds
   | TheClassDriverPluginDispatchTable
   v
interface matched
   |
   | composite driver has already SetConfiguration; USB Manager calls
   | initializeInterfaceProc(interfaceNum, pInterface, pDevice, interfaceRef)
   | -- synchronous, system task time [Ch 4, p. 69]
   v
initialize
   |
   | 1. validate pInterface with portable core (MS header, jacks, bulk
   |    endpoints, max packet sizes) -- decline with error if not USB-MIDI
   | 2. record interfaceRef; USBAllocMem for buffers (64-byte aligned;
   |    alignment is a performance choice, see §5.5)
   |    [Ch 5, p. 150; App A, p. 230]
   | 3. start async state machine; return kUSBNoErr [Ch 4, p. 69]
   v
find/open pipes                        (async steps, completion-driven)
   |
   | USBNewInterfaceRef(interface) -> ifaceRef      [Ch 5, p. 112]
   | USBConfigureInterface(ifaceRef) -> pipes opened [Ch 5, p. 113]
   | USBFindNextPipe(ifaceRef, kUSBIn,  kUSBBulk) -> bulk IN  pipe (0x81)
   | USBFindNextPipe(ifaceRef, kUSBOut, kUSBBulk) -> bulk OUT pipe (0x02)
   |                                                [Ch 5, p. 115]
   v
submit IN read
   |
   | USBBulkRead: usbReqCount=64 (MaxPacketSize), usbBuffer=64-byte
   | aligned buffer, usbCompletion=ReadCompletion [Ch 5, p. 128]
   v
read pending
   |
   | completion routine at secondary interrupt or task level [Ch 5, p. 102]
   v
completion
   |
   | check usbStatus (kUSBNoErr | kUSBNotRespondingErr |
   |                  kUSBAbortedError | ...) and usbActCount
   | on success: pass buffer to portable um9_packet_decode;
   | deliver 4-byte Event Packets to the Probe via the dispatch table
   v
process packet --+--> resubmit read (loop back to "submit IN read")
                 |
                 | errors other than expected unplug: re-arm read
                 | kUSBAbortedError during unplug: do NOT retry [Ch 4, p. 71]
                 v
          removal/error path (Section 5.6)
```

The completion routine must be written so that the same USBPB can be
reused for the next read once the callback runs. [Ch 5, p. 101]

### 5.4 Proposed data structures (design only)

```c
/* USBMIDI9 driver instance (one per interface). Design sketch. */
struct usbmidi9_instance {
    USBInterfaceRef      interfaceRef;    /* from USBNewInterfaceRef */
    USBPipeRef           bulkInPipe;      /* endpoint 0x81 */
    USBPipeRef           bulkOutPipe;     /* endpoint 0x02 */
    UInt16               maxPacketSize;   /* 64, from USBFindNextPipe */
    void                *readBuffer;      /* USBAllocMem'd, 64-aligned
                                             (alignment = performance, §5.5) */
    USBPB                readPB;          /* resident while read pending */
    volatile UInt32      readPending;     /* completion/refcon state */
    UInt32               refcon;          /* state-machine selector */
    /* dispatch-table client list (probe registration) */
    ...
};
```

The design intentionally mirrors the DDK sample pattern: one instance per
interface, an asynchronous state machine selected by `usbRefcon`, and the
USBPB/buffer allocated up front so they outlive the asynchronous calls.
[Ch 4, p. 68-69; Ch 5, p. 101]

### 5.5 Bulk-read lifecycle (verified sequence)

1. `USBFindNextPipe` with `kUSBIn`/`kUSBBulk` returns the bulk-IN pipe ref
   (max packet size in `usb.cntl.WValue`). [Ch 5, p. 115]
2. `USBBulkRead` with `usbReqCount = 64`, buffer aligned to 64;
   `usbCompletion` set, `usbFlags = 0`. [Ch 5, p. 128]
3. A short packet or a full 64-byte packet terminates the request; check
   `usbActCount`; `usbStatus` holds the completion status. [Ch 5, p. 128]
4. Decode the buffer as 1..N 4-byte Event Packets with the portable core
   (`um9_packet_decode`), deliver via the probe dispatch entry, resubmit.
5. On error, never retry an unexpected `kUSBAbortedError`. [Ch 4, p. 71]

**M1A.1 correction — MaxPacketSize alignment is a performance
consideration, not a functional requirement.** Rev 26 itself says "in
order to avoid the loss of data … usbBuffer and usbReqCount *should* be a
multiple of the endpoint MaxPacketSize" and defers to the performance
appendix: a misaligned buffer caps the transfer descriptor at 4K versus 8K
for an aligned buffer; "for small transfers (less than 8K), this issue may
not make a noticeable difference"; "the buffer size must be a multiple of
MaxPacketSize for *maximum performance*". Functionally: a short packet
terminates a bulk read regardless of alignment; a request that is not a
multiple of MaxPacketSize risks overrunning the last packet
(`kUSBOverRunErr`, no valid data in the buffer). The aligned 64-byte read
(64-byte buffer, `usbReqCount = 64`) **remains the intended choice** for
the Keystation M1 test: it is simple, matches the documented performance
guidance, and guarantees any MIDI event packet (≤4 bytes) terminates the
read immediately. [Ch 5, p. 128; App A, "Bulk Data Transfer Performance
Issues", p. 230]

### 5.6 Hot-unplug lifecycle (verified sequence)

1. Device unplugged → pending USL calls complete with
   `kUSBNotRespondingErr` or are aborted with `kUSBAbortedError`, in no
   guaranteed order relative to the notification. [Ch 4, p. 71]
   (*M1A.1:* the constant is `kUSBNotRespondingErr` (−6911); Rev 26 prose
   spells it "kUSBNotRespondingError".)
2. `notificationProc(kNotifyDriverBeingRemoved)` at system task time.
   [Ch 4, p. 70]
3. While the Probe still holds the driver, return `kUSBDeviceBusy` to
   postpone `finalizeProc`; the USB software re-sends the notification
   until `kUSBNoErr`. [Ch 4, p. 71]
4. Call `USBAbortPipeByReference` on pipes with active transactions;
   wait until every transaction's completion has run. [Ch 4, p. 71]
5. Return `kUSBNoErr` → `finalizeProc` called (task time, synchronous):
   dispose interface ref (`USBDisposeInterfaceRef`, `kUSBNoCallBack`
   allowed for immediate cleanup), deallocate USB memory, notify probe.
   [Ch 4, p. 70-71; Ch 5, p. 119, 151]
6. Return from finalize → fragment unloaded. No USB call may still be
   pending, or the system crashes when its completion later fires.
   [Ch 4, p. 70-71]

**M1A.1 — the exact removal mechanics as implemented by Apple's shipping
KeyboardModule (DDK 1.4.1 `Examples/KeyboardModule/`):** the driver keeps
a `driverRemovalPending` flag and a `kCompletionPending` bit in
`usbRefcon` for every in-flight transaction. On
`kNotifyDriverBeingRemoved` it (a) sets `driverRemovalPending = true`,
(b) for each pending transaction calls
`USBAbortPipeByReference(pipeRef)` — if the abort itself errors it clears
`kCompletionPending` ("don't expect the completion to be called either"),
(c) nils the pipe ref and returns `kUSBDeviceBusy` so the USB software
re-sends the notification. The completion routine refuses to resubmit I/O
while `driverRemovalPending` is set, so the transaction count drains to
zero; `finalizeProc` then returns `noErr` immediately. The same pattern
appears in UniversalModule.c (`USBAbortPipeByReference(myUniversalPB.pipeRef)`)
and USBKeypad. Note the data-toggle caveat: aborting leaves the endpoint
toggle state uncertain (`USBAbortPipeByReference` docs, Rev 26 p. 137;
PrinterClassDriver.c comments) — irrelevant for a driver that is being
removed, relevant if an abort is used for error recovery.

### 5.7 Proposed Probe communication mechanism

* The driver exports `USBMIDI9DispatchTable` (name fixed at M1B), a
  versioned table of proc pointers — modeled on the documented HID
  dispatch-table precedent (`dispatchTableCurrentVersion` /
  `dispatchTableOldestVersion` / `vendorID` fields). [Ch 4, p. 76]
  *M1B: the implemented table exposes ONLY version, enumerate interfaces,
  get basic interface/endpoint info, and dequeue raw bytes — see
  `classic/usbmidi9_dispatch.h`.*
* The Probe (a Mac OS 9 application) finds the driver with
  `USBGetNextDeviceByClass(&deviceRef, &connID, 0x01, 0x03,
  kUSBAnyProtocol)` starting from `kNoDeviceRef`, then
  `FindSymbol(connID, "\pUSBMIDI9DispatchTable", ...)` — inside  `SetZone(SystemZone())` because class drivers load in the System Zone.
  [Ch 4, p. 83-85; Ch 6, p. 179]
  *M1B call-shape note (verified from the DDK 1.4.1 kit's own
  HIDReader.c and ShimSerialStub.c): the era call is
  `FindSymbol(connID, symName, (Ptr *)&dest, &symClass)` — symbol
  address argument before the CFragSymbolClass pointer; HIDReader
  checks nothing for data symbols (the dispatch table is data).*
* For hotplug awareness the Probe registers with
  `USBInstallDeviceNotification` (kNotifyAddInterface / kNotifyRemoveInterface);
  the notification callback runs at **task time** and may use Toolbox
  calls. [Ch 4, p. 84-85; Ch 6, p. 185-187]
  *M1B: the Probe polls instead (re-locating the driver every poll, so a
  cached table pointer can never dangle after the fragment unloads);
  notifications are deferred.*
* The dispatch table will expose, at minimum: register/unregister a packet
  consumer callback, device info, and (later) send. Function signatures
  are M1B design work — the M1A gate fixes the mechanism, not the ABI.
  *M1B: the implemented table is deliberately smaller (version,
  enumerate, get info, dequeue) — see §9.3.*
* Note: `USBGetNextDeviceByClass` appears with inconsistent prototypes in
  the two Chapter 4 listings (4 vs 5 arguments); the Chapter 6 reference
  (5 arguments, with `CFragConnectionID *connID`) is authoritative —
  **M1A.1-verified** against the Rev 26 text:
  `OSStatus USBGetNextDeviceByClass(USBDeviceRef *deviceRef,
  CFragConnectionID *connID, UInt16 theClass, UInt16 theSubClass,
  UInt16 theProtocol)`.
  [Ch 4, p. 83-84 vs Ch 6, p. 179]

### 5.8 CodeWarrior/build requirements known so far

* Project type: CodeWarrior shared library ('shlb') target(s); merge via
  "Mac OS Merge" into a single file with type `'ndrv'`, creator `'usbd'`,
  "Copy Code Fragments" checked (DDK-era toolchain verified: CodeWarrior
  Pro 1 / IDE 2.0, Universal Interfaces 3.3 — see §3 Q19). [App A, p. 227;
  DDK 1.4.1 Readme]
* Export list (.exp / linker exports): `TheUSBDriverDescription`,
  `TheClassDriverPluginDispatchTable`, `USBMIDI9DispatchTable`.
  [Ch 4, p. 60-65; USBClassDriver.exp]
* Link against the USB Services Library (USBServicesLib); weak-link it if
  any isochronous calls are used, else the driver will not load on systems
  lacking them. [App A, p. 226]
* Headers: USB.h (1.3 or later; the `kClassDriverPluginVersion` in the
  header must match the 3-arg notification proc we implement). *M1A.2:
  the DDK 1.4.1 USB.h itself declares the USL prototypes (see §2), so no
  second header is needed for them.*
  [Ch 5, p. 106; App A, p. 226; §3 Q20]
* File placement: built drivers go in the Extensions folder (DDK ReadMe
  installation instructions; Rev 26 Ch 3 p. 47); the DDK's own project
  files output drivers to `:Extensions-MCWBuilt:`.

---

## 6. Unresolved questions

* ~~USB DDK 1.4.2 media and headers~~ — **resolved by M1A.1:** no standalone
  USB DDK 1.4.2 exists (Apple change histories; USB 1.4.2 ships only inside
  the Mac OS 9 Update). The DDK 1.4.1 kit (ADC CD-ROM January 2001) is the
  authentic 1.4.x header set; see `~/research/usbddk/PROVENANCE.md`.
* ~~Exact **USL function prototypes in header form**~~ — **resolved by
  M1A.2:** the DDK 1.4.1 USB.h declares them itself (CALL_NOT_IN_CARBON
  gate); the M1B sources use the exact spellings from the kit header and
  mirror them in the compile-check stubs (`classic/host-check/USB.h`).
* `USBConfigureInterface` set-interface behavior in later USB software
  (Rev 26 documents that it does not set the interface; later behavior
  unverified — requires a 1.4.6+ USB software test).
* Exact **CodeWarrior version** used by the DDK era — **resolved:** the
  DDK 1.4.1 Readme states CodeWarrior Pro 1 / IDE 2.0 (compatible with
  IDE 2.1, 3.0+), with Universal Interfaces 3.3.
* Whether **sleep notifications** must be handled for the target G4
  (desktop Macs do not receive them per Rev 26, p. 70).
* `USBMIDI9DispatchTable` ABI and Probe UI design (M1B).
* OMS/FreeMIDI shim design (deferred; M4/M6).

---

## 7. Source/provenance table

| Source | Kind | Provenance | Used for |
|---|---|---|---|
| Apple Computer, *Mac OS USB DDK API Reference*, Preliminary Working Draft, Revision 26, 12/23/99, 252 pp. | Primary Apple doc | Downloaded from Apple's own archive: `developer.apple.com/library/archive/documentation/Hardware/DeviceManagers/usb/usb_ref/usb_api_ref_v26.pdf`; PDF metadata (Author "Apple Computer", FrameMaker 5.5, 1999-12-23) verified. Local copy (research only, NOT committed): `~/research/usbddk/usb_api_ref_v26.pdf` + `.txt` extraction (SHA-256 `c2594edf…37668`). | All assertions above; citations `[Ch N, "Section", p. N]` |
| Apple, **Mac OS USB DDK 1.4.1 installed kit** (Interfaces/USB.h, HID.h, .exp lists; Libraries/USBServicesLib; Documentation incl. DDK Readme + Change History; Examples; Extensions-AppleBuilt) | Primary Apple material | Apple ADC Developer CD-ROM January 2001, Internet Archive item `Apple_Developer_Connection_Developer_CD-ROM_January_2001`, ISO folder `Development Kits/Hardware/Mac OS USB DDK/Mac OS USB DDK 1.4.1/`; local copy (research only, NOT committed): `~/research/usbddk/adc-cd-jan2001/…`; per-file hashes in `~/research/usbddk/PROVENANCE.md`. | M1A.1 verification: exact declarations, constant values, export lists, sample patterns, CodeWarrior/UI requirements, 1.4.2 finding |
| Apple, **USB DDK 1.4.6f12 / USB DDK 1.5.1f1 disk images** | Primary Apple material | ADC CD-ROM Jan 2001 (`USB_DDK_1.4.6f12.img`) and Macintosh Garden `USB_DDK_1.5.1f1.img_.sea_.hqx` (MD5 `53b45e75…bf422e`). Images are FileCrusher/tome-mastered; contents not extractable with standard tools. Local: `~/research/usbddk/` (NOT committed). | Cross-era reference only; contents covered by the 1.4.1 kit |
| Apple, **MacErrors.h** (Universal Interfaces snapshot) | Primary Apple material | GitHub `msftguy/ssh-rd` @ `a5f3a79daeac5844edebf01916c9613563f1c390`, `_3rd/CF/MacErrors.h` ("QuickTime 7.3"-era UI header; USB error block unchanged since the DDK era). Local: `~/research/usbddk/media/MacErrors.h` (NOT committed). | USB error values (kUSBNoErr, kUSBAbortedError=−6982, kUSBDeviceBusy=−6977, kUSBNotRespondingErr=−6911, …) |
| Apple USB DDK 1.4.2-specific media | — | **Does not exist**: Apple's change histories (DDK 1.4.1 kit "Mac OS USB Change History", which pre-documents the upcoming 1.4.2 release; Apple "Mac OS USB 1.5.1f1 Change History" page, Wayback capture 2001-06-24) confirm USB 1.4.2 shipped only inside the Mac OS 9 Update (9.0.4), 'usbv' 0x01428000, no API change, no standalone installer. | Resolution of the "USB DDK 1.4.2" open item |
| Inside Macintosh: PowerPC System Software, Ch 3 (Code Fragment Manager) | Secondary Apple doc | Referenced by Rev 26 preface (p. 15); not yet obtained | CFM background (FindSymbol etc.) |
| USB specification / USB-MIDI 1.0 class spec | Industry spec | usb.org; already used by the portable core | Descriptor/event-packet formats |

Discipline notes: no IOKit / IOUSBDeviceInterface / CoreMIDI / DriverKit
material was used; those are Mac OS X APIs and are rejected for this
target. No OMS/FreeMIDI API was designed here. No Keystation VID/PID
appears anywhere in the design (the descriptor in 5.1 is fully generic).
No Apple sample code is copied into this repository — patterns and
provenance only (DDK license; "The USB Support and USB Device Extension
files may not be distributed in any manner").

---

## 8. M1A.1 dependency-acquisition and consistency pass

Sources and hashes: `~/research/usbddk/PROVENANCE.md` (outside the repo).
Everything below was read from the authentic **Mac OS USB DDK 1.4.1 kit**
(ADC Developer CD-ROM January 2001) or from **Rev 26** / **MacErrors.h**;
no Apple source code is reproduced here.

### 8.1 Verified constant values and declarations (USB.h 1.4.1)

* `kTheUSBDriverDescriptionSignature = 'usbd'`; `kInitialUSBDriverDescriptor = 0`;
  `kUSBDriverFileType = 'ndrv'`; `kUSBDriverRsrcType = 'usbd'`; `kUSBShimRsrcType = 'usbs'`.
* `kClassDriverPluginVersion = 0x00001100`.
* `kUSBCurrentPBVersion = 0x0100`; `kUSBIsocPBVersion = 0x0109`;
  `kUSBCurrentHubPB = kUSBIsocPBVersion`; `kUSBNoCallBack = (USBCompletion)-1L`.
* Direction/type enums: `kUSBOut=0, kUSBIn=1, kUSBNone=2, kUSBAnyDirn=3`;
  `kUSBControl=0, kUSBIsoc=1, kUSBBulk=2, kUSBInterrupt=3, kUSBAnyType=0xFF`.
* Loading options: `kUSBDoNotMatchGenericDevice=0x00000001`,
  `kUSBDoNotMatchInterface=0x00000002`, `kUSBProtocolMustMatch=0x00000004`,
  `kUSBInterfaceMatchOnly=0x00000008`.
* Notifications: `kNotifyDriverBeingRemoved=0x0000000B`;
  `kNotifyExpertTerminating=0x00000008`; sleep notifications
  `kNotifySystemSleepRequest/Demand/WakeUp/Revoke = 1..4`.
* Errors (MacErrors.h, UI snapshot — values stable across the DDK era):
  `kUSBNoErr=0`, `kUSBPending=1`, `kUSBOverRunErr=-6908`,
  `kUSBNotRespondingErr=-6911`, `kUSBBadDispatchTable=-6950`,
  `kUSBDeviceDisconnected=-6972`, `kUSBDeviceBusy=-6977`,
  `kUSBPipeStalledError=-6979`, `kUSBAbortedError=-6982`,
  `kUSBInternalErr=-6999`.
* `USBPB` layout and the `USBVariantBits` union (`cntl`/`isoc`/`hub`) match
  the header exactly; `OLDUSBNAMES` (default 0) provides the
  `usb.cntl.*`-style spellings.
* **M1A.2 correction:** the USL function prototypes (USBFindNextPipe,
  USBBulkRead, USBConfigureInterface, USBAllocMem, USBDeallocMem,
  USBAbortPipeByReference, USBGetNextDeviceByClass, …) **are** in the
  1.4.1 header, gated by `#if CALL_NOT_IN_CARBON`; there is no
  `USBDNotificationProcPtr` type — the notification-proc type is
  `USBDDriverNotifyProcPtr` (unchanged).
* *M1B naming note:* the MacErrors.h name for the stalled-pipe error is
  `kUSBPipeStalledError` (Rev 26 prose writes `kUSBPipeStalledErr`); the
  driver uses the MacErrors.h name.

### 8.2 Verified Apple class-driver sample patterns (DDK 1.4.1 Examples/)

Answers to the M1A.1 brief's sample-pattern questions, with the file that
demonstrates each:

1. **How `USBDriverDescription` is instantiated** — a global struct
   `USBDriverDescription TheUSBDriverDescription = { … }` initialized with
   `kTheUSBDriverDescriptionSignature, kInitialUSBDriverDescriptor`, the
   four `USBDeviceInfo` words, the five `USBInterfaceInfo` bytes, the
   `USBDriverType` (Pascal string name, class/subclass, `NumVersion` as
   four separate UInt8 fields — majorRev, minorAndBugRev, stage,
   nonRelRev — per the NumVersion layout), and the
   loading-options flags combined with `+`.
   [USBKeypadHeader-MacAlly.c; KeyboardModuleHeader.c]
2. **How the dispatch table is instantiated/exported** — a global
   `USBClassDriverPluginDispatchTable TheClassDriverPluginDispatchTable =
   { kClassDriverPluginVersion, … }`; the `.exp` export lists contain
   exactly `TheUSBDriverDescription` and `TheClassDriverPluginDispatchTable`
   (plus optional driver tables). [USBClassDriver.exp; USBHIDModule.exp]
3. **How per-device/interface state is retained** — a static/global driver
   parameter block struct with the `USBPB` embedded first, followed by
   device/interface/pipe refs, copied descriptors, buffers, flags, retry
   count and a `transDepth` reentrancy counter; `pb.pbLength` is set to
   `sizeof(whole struct)`. The DDK modules are single-instance ("not
   reentrant" guard in `InterfaceEntry`); M1B will need per-interface
   instances (allocate in initializeInterfaceProc, free in finalize).
   [USBKeypad.h `usbKeyPadPBStruct`; KeyboardModule.c]
4. **How initializeInterfaceProc starts the state machine** — the proc
   (called synchronously at task time) stores the interface ref, copies the
   descriptors, initializes the PB (`InitParamBlock`: `usbReference`,
   `pbVersion = kUSBCurrentPBVersion`, `usbBuffer = nil`, `usbStatus =
   noErr`, flags/counts zeroed), sets the initial `usbRefcon` state, then
   calls `InitiateTransaction(&pb)` and returns `noErr`. [KeyboardModule.c
   `InterfaceEntry`; USBKeypad.c `DriverEntry`]
5. **How USBPBs are initialized/reused** — one PB per transaction site,
   re-initialized (`InitParamBlock`) before every USL call; completion
   routines decode `usbStatus`, clear/advance the `usbRefcon` state, set
   `kCompletionPending` while a call is in flight, and resubmit the same
   PB (`if (!(refcon & kReturnFromDriver) && !driverRemovalPending)
   InitiateTransaction(pb)`). Async calls return `kUSBPending` when
   queued; anything else is an immediate error. [USBKeypad.c;
   KeyboardModule.c]
6. **How pending I/O is tracked during removal** — `driverRemovalPending`
   flag + `kCompletionPending` bits; `kNotifyDriverBeingRemoved` sets the
   flag, aborts pipes with `USBAbortPipeByReference(pipeRef)`, clears
   `kCompletionPending` if the abort errors, and returns `kUSBDeviceBusy`;
   completions stop resubmitting; `finalizeProc` returns `noErr` once
   drained. [KeyboardModuleHeader.c `KeyboardNotifyProc`;
   KeyboardModule.c]
7. **CodeWarrior exports/link settings** — see §3 Q19/Q20: CodeWarrior
   Pro 1 / IDE 2.0 + Universal Interfaces 3.3; PPC shared library
   (`-xm sharedlibrary -share context`), type `'ndrv'`, creator `'usbd'`,
   `-@export USBClassDriver.exp`, link `USBServicesLib` + `InterfaceLib`;
   multi-module files via "Mac OS Merge" (CW) or `mergefragment` (MPW).
   [DDK 1.4.1 Readme; USBKeypad.make; USBMultModule.proj]

Context rule preserved (unchanged from §1.6): USB completion execution
context is **not guaranteed** (secondary interrupt or task level); the
completion routine and everything it touches must be valid at both levels.
Rev 26 Ch 5 (p. 102) documents the mechanism: the driver services call
`CurrentExecutionLevel` discovers the current execution level (returning
`kHardwareInterruptLevel` / `kSecondaryInterruptLevel` / `kTaskLevel`),
and `CallSecondaryInterruptHandler2` continues execution at secondary
interrupt level. The DDK's PrinterClassDriver and USBModem implement the
mitigation for `USBBulkRead`/`USBBulkWrite` as `SafeUSBBulkRead`, which
gates on **USB version** — `Gestalt('usbv') < kUSBv12` (0x01200000) — and
only then re-enters the USL through
`CallSecondaryInterruptHandler2(SecondaryUSBBulkRead, nil, pb, &result)`;
the secondary handler writes `*(OSStatus*)result = USBBulkRead(pb)` and
the trampoline's own return is ignored. [PrinterClassDriver.c;
USBModem/ModemDriver.c; Rev 26 Ch 5, p. 102] *Correction (M1B read-path
fix): an earlier version of this note said SafeUSBBulkRead "checks the
execution level" — the samples actually check the USB version. USBMIDI9
gates on the execution level instead (§9.8), because the version gate is
inert on the G4 (USB ≥ 1.2) and the read-path hang is a
completion-context re-entry failure.*

### 8.3 Corrections applied to this document (M1A.1)

1. Exported symbol names: `USBDriverDescription`/`USBClassDriverPluginDispatchTable`
   → **`TheUSBDriverDescription`/`TheClassDriverPluginDispatchTable`** (§1.1,
   §1.2, §3 Q2, §5.2, §5.8).
2. Signature constant: `kUSBDriverDescriptionSignature` →
   **`kTheUSBDriverDescriptionSignature`** (same value `'usbd'`) (§1.1).
3. `kClassDriverPluginVersion` value now verified: **0x00001100** (§1.2, §3 Q3).
4. Notification proc: type is **`USBDDriverNotifyProcPtr`** (3-arg with
   refcon); "USBDNotificationProcPtr" does not exist in the header (§1.2, §8.1).
5. Loading options: `kUSBInterfaceMatchOnly = 0x00000008` **is** defined
   in the header; the Rev 26 figure references are valid (§1.2).
6. Error naming: `kUSBNotRespondingError` → **`kUSBNotRespondingErr`
   (−6911)** (Rev 26 prose keeps the "-Error" spelling); values verified
   for `kUSBAbortedError (−6982)`, `kUSBDeviceBusy (−6977)`, `kUSBNoErr (0)`
   (§5.3, §5.6, §8.1).
7. MaxPacketSize alignment/multiples reframed as a **performance
   consideration** (4K vs 8K transfer-descriptor limit; "for maximum
   performance"); aligned 64-byte read stays the intended Keystation M1
   choice (§3 Q12, §5.3, §5.5).
8. Version/packaging open items resolved: no standalone USB DDK 1.4.2
   (Mac OS 9 Update only, 'usbv' 0x01428000, no API change); DDK-era
   toolchain = CodeWarrior Pro 1 / IDE 2.0 + UI 3.3 (§2, §3 Q19/Q20, §6, §7).
9. ~~USB.h 1.4.1 is types-only; USL prototypes live in later header
   revisions~~ — **superseded by M1A.2:** the 1.4.1 header declares the
   USL prototypes itself (CALL_NOT_IN_CARBON); verified by reading the
   actual kit header during M1B (§2, §6, §8.1).

---

## 9. M1B implementation (source gate)

Status: **source gate complete; M1B hardware gate PASSED** — generic
interface matching, dispatch/enumeration, bulk receive, and real USB-MIDI
packet reception all PASSED on the real G4 (exact packets in §9.8). One
defect remains: hard system freeze on unrelated-device hot-plug while
USBMIDI9 is active (§9.9, audit + next-session experiment plan). All
patterns re-verified against the authentic DDK 1.4.1 kit during
implementation (sample files listed per claim below).

### 9.1 Files

| File | Role |
|---|---|
| `classic/ring.{h,c}` | Fixed resident byte ring (SPSC, volatile indices, drop-new on overflow). Portable C89, host prop-tested (`tests/test_ring.c`). |
| `classic/usbmidi9_dispatch.h` | The `USBMIDI9DispatchTable` ABI (shared driver/probe). ONLY: version, enumerate, get info, dequeue. |
| `classic/usb_driver.{h,c}` | The interface class driver (metadata, exports, state machine, removal). |
| `probe/probe.c` | Mac OS 9 console probe (SIOUX): locate table, display info, hex dump. |
| `codewarrior/USBMIDI9.exp` | Linker export list: `TheUSBDriverDescription`, `TheClassDriverPluginDispatchTable`, `USBMIDI9DispatchTable`. |
| `classic/host-check/*.h` | Minimal stub headers (NOT the real SDK) for `make check-classic` on Linux. |
| `spec/m1b/tasks.md` | M1B task plan with the DoD mapping. |

### 9.2 Verified patterns used (kit file → use in driver)

* One USBPB embedded first in the driver struct; `pb.pbLength =
  sizeof(whole struct)`; per-state `InitParamBlock`-style re-init; refcon
  state selector with `kCompletionPending` (0x8000) and
  `kReturnFromDriver` (0x1000) bits. [KeyboardModule.h, KeyboardModule.c,
  UniversalModule.c]
* Interface drivers DO call `USBConfigureInterface` on the interfaceRef
  passed to `initializeInterfaceProc`, then `USBFindNextPipe` with
  `usbFlags = kUSBIn`, `usbClassType = kUSBBulk`; the pipe ref comes back
  in `pb.usbReference`, MaxPacketSize in `pb.usb.cntl.WValue`.
  [KeyboardModule.c kConfigureInterface/kFindPipe; Rev 26 p. 113-115]
* `USBBulkRead`: usbReference = pipe ref, usbReqCount = MaxPacketSize,
  aligned buffer, usbFlags = 0; short/full packet terminates; check
  usbActCount. [Rev 26 p. 128-129; PrinterClassDriver.c]
* Resident memory via async `USBAllocMem` (usbReqCount in, usbBuffer out)
  and `USBDeallocMem` with `usbCompletion = kUSBNoCallBack` at finalize.
  [UniversalModule.c kAllocHIDOwnedDescMem / InterfaceExit; Rev 26 p. 150]
* Removal: `kNotifyDriverBeingRemoved` sets removalPending, aborts pipes
  via `USBAbortPipeByReference` (on abort error, clear the pending bit —
  "don't expect the completion"), returns `kUSBDeviceBusy` while any
  completion is outstanding; completions stop resubmitting; finalize only
  when drained. [KeyboardModuleHeader.c KeyboardNotifyProc; Rev 26 p. 71]
* Per-connection finalize: UniversalModule's finalize passes its first
  argument to InterfaceExit AS the interface ref, so finalize can
  identify its instance by interfaceRef. [UniversalModuleHeader.c,
  UniversalModule.c InterfaceExit]
* Probe lookup: `USBGetNextDeviceByClass` from `kNoDeviceRef` +
  `FindSymbol(connID, "\pUSBMIDI9DispatchTable", (Ptr *)&table,
  &symClass)` inside `SetZone(SystemZone())`. [HIDReader.c; Rev 26
  p. 83-85, 179]

### 9.3 Design decisions and limitations (M1B)

1. **Per-interface instances** live in a fixed static registry (8 max)
   — no heap allocation at interrupt time; one slot claimed per
   `initializeInterfaceProc`, disposed at finalize.
2. **Removal notification is not attributable** to a specific interface:
   `kNotifyDriverBeingRemoved` carries no documented identifying data
   (every DDK sample ignores pointer/refcon). M1B therefore drains ALL
   instances on removal. Exact for one device (the M1B target); with
   several independent devices, one unplug stops all of them (safe, no
   crash — aborts handled per the verified rules). Multi-device removal
   disambiguation is M2 work (ROADMAP M2: multiple devices).
3. **Endpoint address (0x81) is not exposed**: the verified USL flow
   returns MaxPacketSize but not the endpoint address; obtaining it
   would require `USBGetFullConfigurationDescriptor` + immediate
   descriptor iteration (extra async states). The dispatch info carries
   MaxPacketSize as the endpoint info. Deferred.
4. **No retries in the state machine** (the DDK samples retry 3x with
   stall clearing): init-stage failures stop the machine (the removal
   path cleans up; the probe shows an interface with no data). The read
   loop does clear a stalled pipe once and resubmits; unexpected
   `kUSBAbortedError` is never retried (verified rule).
5. **Synchronous-USL safety bound:** `USBMIDI9InitiateTransaction` keeps
   a re-entrancy depth counter (`transDepth`, max 16 =
   `kUSBMIDI9MaxSyncDepth`). If the USL ever completes calls
   synchronously (with the callback invoked inside the call, or with
   `kUSBNoErr` and no callback), the machine advances through the
   completion and would recurse; the bound stops the machine instead of
   overflowing the stack, in both variants. Exercised by the host
   machine tests (`tests/test_machine.c`: MODE_SYNC,
   MODE_NOERR_NO_CB, MODE_SYNC_READ_LOOP).
6. **Ring overflow drops new bytes** (never overwrites unread data);
   fixed 4096-byte ring per interface.
7. **The probe re-locates the driver every poll** (~4 Hz) so a cached
   dispatch-table pointer can never dangle after the fragment unloads.
8. **No OMS, no FreeMIDI, no vendor/product-specific production code**
   anywhere in the driver or probe. The only Keystation mentions in the
   tree are the validation fixtures (`fixtures/`, `tests/`) and these
   docs; the driver itself never names a vendor or product.

### 9.4 M1A.2 corrections applied

1. USB.h 1.4.1 DOES declare the USL prototypes (CALL_NOT_IN_CARBON) —
   the M1A.1 "types-only" claim was wrong (§2, §3 Q20, §6, §8.1, §8.3#9).
2. FindSymbol call shape confirmed from the kit's own samples: address
   argument before CFragSymbolClass (§5.7).
3. `kUSBPipeStalledError` is the MacErrors.h spelling (Rev 26 prose uses
   `kUSBPipeStalledErr`); the driver uses the MacErrors.h name (§8.1).
4. The exported `USBMIDI9DispatchTable` must be declared through the
   struct tag (`struct USBMIDI9DispatchTable USBMIDI9DispatchTable`): a
   variable cannot share a name with a typedef in the same scope.

### 9.5 Hardware acceptance checklist (M1B definition of done, item 11)

NOT claimable from this repository. Requires, on the Power Mac G4:

- [x] Build the driver (`classic/usb_driver.c`, `classic/ring.c`) in
      CodeWarrior as a shared library, merged to file type `'ndrv'`
      creator `'usbd'`, exports from `codewarrior/USBMIDI9.exp`,
      linking USBServicesLib — **done on the G4 (0 errors / 43
      warnings; see §9.7)**; installed and loaded on the G4 for the
      receive-gate run (§9.8).
- [x] Build the Probe (`probe/probe.c`) as a CodeWarrior console app
      (SIOUX) linking InterfaceLib + USBManagerLib — **done on the G4;
      launches and runs (see §9.7)**.
- [x] Boot Mac OS 9 with the driver installed; attach the Keystation 49e
      (or any class-compliant USB-MIDI device) — **done on the G4;
      the receive gate passed (§9.8)**.
- [x] The driver loads for the MIDIStreaming interface (class 0x01,
      subclass 0x03); USB Prober or the Name Registry shows `USBMIDI9`
      — **done via the Probe's FindSymbol lookup (§9.8)**.
- [x] The Probe finds `USBMIDI9DispatchTable`, displays the interface
      (vid 0a4d, pid 0090, maxPacket 64), and prints real received bytes
      in hex when keys are played — **done on the G4; exact packets in
      §9.8** (e.g. `09 90 3C 57`).
- [ ] Hot unplug of the Keystation while the Probe runs: no crash,
      driver finalizes; replug restores data flow. **Still open.** (The
      §9.9 freeze is an UNRELATED-device hot-plug, a separate item.)

### 9.6 M1B hardware/source-gate correction — NumVersion release stage

Found by the **real CodeWarrior build on the G4** (first source-gate
failure): `undefined identifier 'kReleaseStageFinal'` at
`classic/usb_driver.c` (TheUSBDriverDescription's usbDriverVersion).
`kReleaseStageFinal` had been **invented in the host-check stub header**
(`classic/host-check/MacTypes.h`); it does not exist in the real headers.

The authentic Universal Interfaces MacTypes.h (UI 3.3, the G4 build
system's header set — see §3 Q19) models the NumVersion `stage` byte as
enum constants, not `k`-prefixed defines:

```c
enum {
    developStage    = 0x20,
    alphaStage      = 0x40,
    betaStage       = 0x60,
    finalStage      = 0x80
};
```

(UI 3.3–4.x MacTypes.h also has `nonReleaseStage = 0xFF`; not used by the
driver.) The DDK 1.4.1 kit on the research media uses the same stage
encoding in its own `Interfaces/PackageVersion.h` (`kDevelopmentRelease
0x20`, `kAlphaRelease 0x40`, `kBetaRelease 0x60`, `kFinalRelease 0x80`).

Correction applied:

1. `classic/usb_driver.c`: `kReleaseStageFinal` → `finalStage`.
2. `classic/host-check/MacTypes.h`: removed the invented
   `#define kReleaseStageFinal 0x0080u`; models the authentic
   developStage/alphaStage/betaStage/finalStage enum instead.
3. Regression guard: `tests/test_machine.c`
   `test_driver_description_version` pins
   `TheUSBDriverDescription.usbDriverType.usbDriverVersion` to
   `{1, 0, finalStage, 0}`; the driver source itself compiling against
   the stub header makes a revert a compile error in `make test` and
   `make check-classic`.

Value note: `kReleaseStageFinal 0x0080u` == `finalStage` (0x80), so the
correction is name-only — the emitted NumVersion byte is unchanged. The
G4 CodeWarrior rebuild is the confirming gate (checklist item 1 above).

### 9.7 Real-target build status (Power Mac G4) and Probe corrections

Environment proven on hardware: Power Mac G4 / Mac OS 9, CodeWarrior Pro
5.3 (IDE 4.0.4), Universal Interfaces & Libraries 3.3.x, Apple USB DDK
1.4.1, with the repository served live over AFP from 10.0.3.200. The
real build is the source of truth; `classic/host-check/` must model it
and never overrule it.

**Driver — build PASSED.** The real CodeWarrior build of the USBMIDI9
driver produces 0 errors / 43 warnings (mostly duplicate-descriptor /
import-library warnings such as `ignored 'BitAtomic' (descriptor) in
InterfaceLib`, previously defined in DriverServicesLib; not cleaned
during M1 bring-up). The artifact is verified on Mac OS 9 as type
`ndrv`, creator `usbd`, exporting `TheUSBDriverDescription`,
`TheClassDriverPluginDispatchTable`, `USBMIDI9DispatchTable` via
`USBMIDI9.exp` (checklist item 1 of §9.5 is complete through the
build; the driver was then installed and loaded on the G4 for the
receive-gate run — §9.8).

**Probe — builds, links, launches (SIOUX console).** The Probe project
is Std C Console stationery ("ANSI Console Multi" template), target
`USBMIDI9 Probe`, linking `MSL RuntimePPC.Lib`, `MSL C.PPC.Lib`,
`MSL SIOUX.PPC.Lib`, `InterfaceLib`, `MathLib`, with the PPC PEF entry
point `__start` (provided by the MSL runtime; the earlier
`undefined '__start' (descriptor)` link error was missing/broken MSL
runtime linkage in the target, fixed on the Mac side — do not add a
fake `__start` or change the entry point). `HelloWorld.c` from the
stationery must stay excluded from the target (it defines a second
`main()`).

**Probe real-target corrections (found by running on the G4):**

1. **Initial no-driver state was silent.** `lastCount` was initialized
   to `0xFFFFFFFF` to force the first interface-table print, but the
   `table == nil` branch only printed when `lastCount != 0xFFFFFFFF`,
   so the probe sat silent at startup with no driver installed. Fixed
   with an explicit state flag (`struct USBMIDI9ProbeState` +
   `noDriverReported`): the probe now prints
   `(no USBMIDI9 driver loaded)` exactly once at startup and again on
   each driver-present → driver-absent transition, never per poll.
2. **Quit key used a byte-array KeyMap.** The authentic `Events.h`
   `KeyMap` is `UInt32[4]` (128 bits); the old
   `keys[k/8] & (1u << (k%8))` test indexed it with an invented
   LSB-first byte layout, so 'q' never quit on the G4. The probe now
   tests the key through the authentic `KeyMapByteArray` byte view
   (`UInt8[16]`; for keycode N the real Mac OS 9 `GetKeys` byte
   representation is byte N/8 with mask `1 << (N%8)` — verified on the
   G4; the MSB-first `0x80 >> (N%8)` inference was disproved by real
   hardware). Q stays keycode 0x0C (byte 1, mask 0x10).
   `classic/host-check/Events.h` now models the real
   `KeyMap`/`KeyMapByteArray` shapes instead of a hiding `UInt8[16]`
   simplification.
3. **Host regression coverage** (`tests/test_probe.c`, `make test` +
   `make test-sanitize`): initial no-driver prints once, repeated
   no-driver polls stay silent, driver disappearance reports again, and
   the quit-key helper accepts the authentic bit layout and rejects the
   old inverted one.

The probe is now expected to launch with no driver installed, print
`(no USBMIDI9 driver loaded)`, and quit on 'q'. The M1B hardware gate
then PASSED on the G4 (matching, dispatch, bulk receive, real USB-MIDI
packets — §9.8). Remaining hardware work: the Keystation hot-unplug
checklist item (§9.5) and the unrelated-device hot-plug freeze audit
(§9.9).

### 9.8 M1B read-path fix — safe bulk-read submission (real-G4 hang)

**Hardware record (real Power Mac G4, Mac OS 9):**

| M1B hardware item | Result |
|---|---|
| Generic MIDIStreaming interface matching (composite Keystation, iface 1, class 01/subclass 03) | **PASSED** — `Attached USB-MIDI interfaces: 1`, vid=0A4D pid=0090, maxPacket=64 |
| Driver loading / USBConfigureInterface / USBFindNextPipe | **PASSED** — bulk IN pipe found, MaxPacketSize 64 |
| Dispatch table location + enumerate/getInfo via Probe | **PASSED** — probe printed the interface table |
| Bulk receive (safe read path, §9.8 fix) | **PASSED** — the machine stayed responsive; real USB-MIDI packets were received and printed |
| Real USB-MIDI packet reception | **PASSED** — exact packets: `[iface 0] 09 90 30 50` then `[iface 0] 09 90 30 00` (one Keystation key press + release; both 4-byte USB-MIDI Event Packets, CN=0, CIN=0x9: Note On, channel 1, note 0x30 = 48 = C3, velocity 0x50 = 80; then the velocity-zero Note Off) |

**Root cause.** `USBMIDI9CompletionProc` resubmits reads by calling
`USBMIDI9InitiateTransaction()`, whose `kReadBulkInPipeState` case called
`USBBulkRead(&inst->pb)` directly — including when the completion runs at
secondary interrupt level (completion context is not guaranteed; Rev 26
p. 102). Re-entering `USBBulkRead` from its own completion context is the
documented hazard the DDK samples mitigate.

**Fix (committed).** An execution-level-gated safe bulk-read helper in
`classic/usb_driver.c`, modeled on the authentic PrinterClassDriver /
USBModem `SafeUSBBulkRead` pattern:

* `USBMIDI9SafeUSBBulkRead(inst)`: at task level (`CurrentExecutionLevel()
  == kTaskLevel`) calls `USBBulkRead` directly; from any other level
  re-enters the USL through the authentic trampoline
  `CallSecondaryInterruptHandler2(USBMIDI9SecondaryUSBBulkRead, nil,
  &inst->pb, &result)`, where the secondary handler performs
  `*(OSStatus*)result = USBBulkRead(pb)` and returns `noErr` (exact
  sample shape; the trampoline's own return is ignored).
* `kReadBulkInPipeState` is the only state routed through the helper.
  The read resubmit is the only completion-driven **bulk** USL call; the
  other completion-driven transitions (`USBAllocMem`,
  `USBConfigureInterface`, `USBFindNextPipe`) stay direct, and the
  stall-clear (`USBClearPipeStallByReference`) stays direct — exactly
  matching what Apple's samples do from completion procs: PrinterClassDriver
  and USBModem issue control-pipe and synchronous USL calls directly
  (`USBDeviceRequest`, `USBFindNextInterface`, `USBOpenDevice`,
  `USBClearPipeStallByReference`) and give the SafeUSBBulkRead treatment
  only to `USBBulkRead`/`USBBulkWrite`.
* No logging/allocation/Toolbox calls in the completion path; removal /
  `kCompletionPending` / `kReturnFromDriver` / `transDepth` semantics
  unchanged.
* Note on the authentic gate: Apple's samples gate `SafeUSBBulkRead` on
  **USB version** (`Gestalt('usbv') < kUSBv12`), not execution level; that
  gate is inert on the G4 (USB ≥ 1.2), so USBMIDI9 uses the
  DDK-documented generic mechanism (Rev 26 p. 102: "the driver services
  call CurrentExecutionLevel can be used to discover the current
  execution level") while keeping the sample's exact trampoline shape.
  §8.2 was corrected accordingly.

**Host verification** (`make test`, `make test-sanitize`,
`make check-classic`): regression tests in `tests/test_machine.c` cover
task-level direct submission, secondary-interrupt submission through the
trampoline (reaching `USBBulkRead` only via it — no direct recursion from
the completion), stall retry at secondary level (direct stall-clear +
trampolined retry), and removal still preventing resubmission at
secondary level. Plan: `spec/m1b-readpath/tasks.md`.

**Hardware gate is COMPLETE for the receive path.** On the real G4 the
re-run re-passed matching/dispatch AND received real USB-MIDI bytes
(Keystation) without hanging — bulk receive and real packet reception
are hardware-PASSED (§9.8 table above). The normal receive path
(success → enqueue → completion-driven resubmission through
`USBMIDI9SafeUSBBulkRead`) is now hardware-proven and must not be
replaced. Remaining defect: the unrelated-device hot-plug freeze, audited
in §9.9 (no driver change made; the proven path is preserved).

### 9.9 Unrelated-device hot-plug freeze — hardware record and audit

**Hardware record.** After the §9.8 receive gate passed, the following
topology change was exercised on the same G4 (Mac OS 9, CodeWarrior-built
driver + probe): with USBMIDI9 active and the Keystation continuously
attached, and no other USB hot-plug activity, the unrelated USB mouse was
unplugged and a normal HID keyboard was plugged into the G4's other USB
port. Result: **hard system freeze** — no probe 'q' response, no
Cmd-Opt-Esc; forced reboot. This is a separate defect from the §9.8
read-path hang (which is fixed and hardware-proven): the receive path
demonstrably works, and the freeze correlates with the unrelated-device
enumeration / bus topology change, NOT with ordinary successful
completion/resubmission.

**Audit scope and ground rules.** This section records a code/audit pass
only — no driver changes. Per the hardware-gate report: determine what
callbacks/status changes USBMIDI9 can receive when some OTHER device is
removed/added; inspect the DDK samples' handling of outstanding bulk
transfers during unrelated device enumeration; inspect completion-status
handling during this event; audit global/static state shared across
interface instances or USB notifications; audit execution levels observed
and allowed during enumeration. Do not broaden removal-path changes
unless our MIDI interface itself is actually receiving removal
notification; preserve the now hardware-proven normal receive path.

#### 9.9.1 What USBMIDI9 can receive when another device changes

1. **Driver notifications: none expected.** `notificationProc` is
   invoked by the USB software for the driver's OWN matched
   device/interface lifecycle: `kNotifyDriverBeingRemoved` (0x0B)
   before finalize at unplug, `kNotifyExpertTerminating` (0x08) at
   shutdown, and (PowerBooks only, USB 1.2+) sleep notifications
   [§1.3, §5.6; USB.h 1.4.1 constants]. An unrelated device's
   add/remove does not target this driver, so `USBMIDI9NotifyProc`
   should NOT run: the removal path (mark `removing`, abort pipes,
   `kUSBDeviceBusy`) is not entered, and per the ground rules it must
   not be broadened on this evidence.
2. **Completion status on the outstanding read: possible.** The only USL
   call in flight in steady state is the bulk read. If the changed port
   shares a USB controller (bus) with the Keystation, the USB software's
   re-enumeration of that bus can abort outstanding transactions on
   unrelated devices: the read completes with `kUSBAbortedError` (or
   `kUSBNotRespondingErr`). [§5.6 item 1 documents these statuses at
   unplug; the same mechanism applies to a same-bus topology change] If
   the changed port is on the other controller, no completion status
   change is expected at all.
3. **Probe-side USL calls: transient errors possible.** The probe walks
   `USBGetNextDeviceByClass` (+ `FindSymbol`) every ~0.25 s poll
   [probe.c]. During re-enumeration the global device list is
   transiently inconsistent; the call can return an error, which the
   probe treats as "no driver" and self-heals on the next poll. No crash
   path in the probe's own code (it never caches the table pointer; its
   `state` is per-process).

#### 9.9.2 DDK samples: outstanding bulk transfers during unrelated enumeration

None of the kit samples (PrinterClassDriver, USBModem, KeyboardModule,
UniversalModule) contain any handling for unrelated-device enumeration;
they handle only their own device's removal [§9.2, §5.6]. Their read
completion procs resubmit through `SafeUSBBulkRead` on success and on
stall (clear + retry), and stop on any other error, leaving cleanup to
the removal notification. The samples' contract: the USB software is
responsible for aborting outstanding transactions cleanly during
topology changes; a driver simply stops on an unexpected abort.
USBMIDI9's completion implements exactly this contract [§9.9.3].

#### 9.9.3 Completion-status handling during the event (current code)

| Completion status on the bulk read | Current behavior (`USBMIDI9CompletionProc`) |
|---|---|
| `kUSBNoErr` (with data) | enqueue to ring, resubmit via `USBMIDI9SafeUSBBulkRead` — **the hardware-proven path; unchanged** |
| `kUSBPipeStalledError` | `USBClearPipeStallByReference` direct from completion, resubmit (sample-verified pattern). The only re-arm that can fire during a topology change; a stall during reset is unlikely (a reset clears endpoint state) |
| `kUSBAbortedError` / `kUSBNotRespondingErr` / `kUSBDeviceDisconnected` / other | stop the loop (`kReturnFromDriver`); driver stays loaded; no resubmission; cleanup deferred to the removal notification |

Recorded limitation (not changed): after a non-removal abort the read
loop does not auto-restart — if a bus re-enumeration aborts the read,
data flow stops until the Keystation is re-plugged. Restarting safely
requires distinguishing removal from re-enumeration (M2-class change)
and is deferred; the ground rules require preserving the proven path.

#### 9.9.4 Global/static state audit

- `gUSBMIDI9Instances` / `gUSBMIDI9InstanceCount` are mutated only at
  system task time in the initialize/finalize procs; the notify proc
  only sets per-instance flags (`removing`) and never mutates the
  registry, and the completion routine touches only its own instance
  (PB, ring indices, `lastReadStatus`/`lastReadCount`) [usb_driver.c].
- `USBMIDI9DispatchTable` is static data; the probe re-locates it every
  poll and never caches the pointer [probe.c].
- The only cross-instance behavior — `kNotifyDriverBeingRemoved` marks
  ALL instances removing — is reachable only via our own removal
  notification, which is not expected for an unrelated-device event
  [§9.9.1 item 1]. No shared mutable state exists that an
  unrelated-device event could corrupt.

#### 9.9.5 Execution levels observed/allowed during enumeration

- initialize/finalize/notify procs: system task time [Ch 4 p. 69-71, 86].
- Completion routine: secondary interrupt or system task level (not
  guaranteed); all USL functions are safe from secondary interrupt level
  or system task level [Ch 5 p. 102]. The completion path uses only USL
  calls and pure-C ring code.
- Enumeration of the new device runs in the USB software (task time +
  secondary-interrupt completion dispatch). During the event our
  completion may fire at either level; both paths are level-safe after
  the §9.8 fix (bulk submission trampolined via
  `CallSecondaryInterruptHandler2` at non-task level; stall-clear is a
  direct USL call, sample-verified).
- The probe runs at task level (GetKeys/printf/Delay are legal there);
  it never polls at interrupt level (the documented hang hazard,
  Ch 5 p. 103, is not present).

#### 9.9.6 Ranked hypotheses (no fix attempted)

1. **H1 — bystander: the freeze is inside the Mac OS 9 USB software's
   own hot-plug/enumeration path** for the changed port, triggered by
   the topology change itself; USBMIDI9 is not on the freeze path.
   (Era-documented USB-software hot-plug instability; on a
   different-controller topology change our driver has no code running
   at all.)
2. **H2 — bus-shared abort interaction:** the Keystation shares a
   controller with the changed port; re-enumeration aborts the
   outstanding read, and the USB software's abort bookkeeping with a
   pending bulk read — or a success-completion resubmit landing
   concurrently with the teardown — wedges the USB stack. (Our
   completion never resubmits AFTER an abort; the race, if any, is
   between a last success resubmission and the USB software's own
   teardown.) Whether the USB software's abort path is robust with a
   pending bulk read is not provable from source here.
3. **H3 — probe polling during re-enumeration:** `USBGetNextDeviceByClass`
   walks the global device list at task level while the USB software
   mutates it. Low prior (USL calls are expected to be safe at task
   level) but not provable off-target.
4. **H4 — stall-clear/resubmit re-arm during a reset-induced stall:**
   only if a stall is actually reported during the event; low prior.

#### 9.9.7 Next-session hardware experiments (plan: spec/m1b-hotplug/tasks.md)

E1. Reproduce the baseline (Keystation + mouse, probe running): unplug
    mouse, plug HID keyboard → freeze?
E2. Probe NOT running (driver still loaded, Keystation attached): same
    topology change → isolates the probe's polling as a factor.
E3. Driver not installed (plain Mac OS 9, Apple drivers only): does the
    machine freeze without USBMIDI9 at all? (Decides H1 vs driver
    involvement.)
E4. Keystation moved to a port on the OTHER controller than the
    mouse/keyboard port: isolates the same-bus abort interaction (H2).
E5. Unplug-only vs plug-only: does the freeze need both events?
E6. In any configuration that does NOT freeze: record probe output after
    the event — confirms the abort-stop behavior (data flow stops until
    re-plug; documented limitation) or shows the read loop surviving.

Definition of done: identify which component is on the freeze path
(USB software / driver / probe) and record it; make no driver change
before that; keep the receive path byte-identical to the
hardware-proven version.
