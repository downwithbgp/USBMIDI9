# Classic Mac OS USB Driver — Research and Design (M1A)

Status: **M1A research/design gate output.** No driver code exists yet.
Everything in this document is sourced from primary historical Apple
documentation; assertions carry a citation of the form

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
  `USBDriverDescription`, and checking that the first four bytes match the
  signature constant `kUSBDriverDescriptionSignature`.
  [Ch 4, p. 53]
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

1. **`USBDriverDescription`** — identifies the driver to the USB Manager
   and describes what it matches. [Ch 4, "USBDriverDescription Structure", p. 60]
2. **`USBClassDriverPluginDispatchTable`** — the driver entry points.
   [Ch 4, "USBClassDriverPlugInDispatchTable Structure", p. 65]
3. *Optional:* additional exports for communication with a shim or other
   service — "Typically, class drivers may require additional exports to
   facilitate communication with a shim of other Mac OS service."
   [Ch 4, p. 65; Ch 4, "Communicating With Client Processes", p. 72-75]

Documented structure (Rev 26, p. 65; the document itself uses both
"PlugIn" and "Plugin" spellings — noted as a draft inconsistency):

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
  header `USB.h`; the USB Manager uses it to distinguish future versions of
  the table. [Ch 4, p. 65]
* For **device** loading, `validateHWProc`, `initializeDeviceProc` and
  `finalizeProc` are required; nil ⇒ `kUSBBadDispatchTable` and load fails.
  [Ch 4, p. 65-66]
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

/* usbDriverLoadingOptions bits (Rev 26, p. 64): */
/* kUSBDoNotMatchGenericDevice = 0x00000001 */
/* kUSBDoNotMatchInterface     = 0x00000002 */
/* kUSBProtocolMustMatch       = 0x00000004 */
```

Doc-internal inconsistencies recorded: the sample on p. 64 spells the first
flag `kUSBDoNotMatchGeneric` and its comments mix HID class values; the
matching figures (4-1/4-2) reference an "InterfaceMatchOnly" flag that is
not defined in the loading-options list. The p. 64 bit definitions are
treated as authoritative here.

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
* **Unresolved:** Rev 26 (12/23/99) does not cover USB software **1.4.2**,
  the last Mac OS 9-era release (Mac OS 9.1 era). Anything specific to
  1.4.2 (and the USB DDK 1.4.2 headers/ReadMe) must be verified from the
  USB DDK 1.4.2 media before M1B implementation. Marked unresolved in
  Section 7.

---

## 3. Answers to the 20 research questions

1. **Binary/module format:** CFM shared library, file type `'ndrv'`,
   creator `'usbd'` (single CFM per file on USB ≤1.1; merged multi-CFM
   files on 1.2+). [Ch 4, p. 53; App A, p. 227]
2. **Exported symbols:** `USBDriverDescription` and
   `USBClassDriverPluginDispatchTable` (plus optional driver-specific
   exports for shims/clients). [Ch 4, p. 60, 65]
3. **Dispatch table structure:** as reproduced in 1.2 (Rev 26, p. 65);
   `pluginVersion = kClassDriverPluginVersion`; interface loading requires
   `initializeInterfaceProc` + `finalizeProc` non-nil. The notification
   proc signature (with/without refcon) is selected by the plugin version
   constant in the USB.h you build against. [Ch 4, p. 65-66; App A, p. 226]
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
    managed with the device; buffers should be MaxPacketSize-aligned
    multiples). [Ch 5, p. 101, 128; App A, p. 230]
13. **Completion context:** secondary interrupt level or system task
    level, not guaranteed; check `CurrentExecutionLevel`; only USL calls
    are guaranteed safe; Toolbox calls generally are not (class driver
    operates at secondary interrupt time). [Ch 5, p. 102; Ch 4, p. 86]
14. **Hot unplug with an outstanding bulk read:** pending calls complete
    with `kUSBNotRespondingError` or are aborted with `kUSBAbortedError`;
    never retry an unexpected kUSBAbortedError; order of errors vs
    notification is not guaranteed. [Ch 4, "Handling Hot Unplugging", p. 71]
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
    dispatch table + FindSym is the mechanism Apple documents for
    class-driver ↔ shim communication (HID precedent:
    `TheUSBHIDModuleDispatchTable`/`TheHIDDeviceDispatchTable`); Apple
    explicitly notes there is **no official API** for client↔driver
    communication, and describes shim designs for other models (DRVR,
    CTB, vdig, DLPI). For a diagnostic probe, the dispatch-table approach
    is period-correct and sufficient; a DRVR/NDRV shim is only needed
    later for OMS/FreeMIDI integration. [Ch 4, p. 72-75, 81-85]
19. **CodeWarrior build/link settings:** shared-library ('shlb') targets
    merged via CodeWarrior "Mac OS Merge" into an `'ndrv'`/`'usbd'` file
    (merge feature documented for CW IDE 2.0+; exact DDK-era version
    unresolved); export the required symbols (USB.h-style export list /
    .exp); weak-link `USBServicesLib` if calling isochronous functions
    (hard links prevent driver load on systems without those calls);
    include USB.h (1.3+). [App A, p. 226-227]
20. **Headers/libraries on the G4 build system:** the USB DDK headers
    (USB.h at minimum; the DDK's USBClassDriver.h/USBDriver.h equivalents
    per the DDK media) and the USB Services Library import library
    (USBServicesLib). Exact file inventory: **unresolved** — requires the
    USB DDK 1.4.2 media; the DDK ReadMe documents setup.
    [Ch 5, p. 89; App A, p. 228]

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
  the worked precedent (`TheHIDDeviceDispatchTable`, versioned with
  dispatchTableCurrentVersion/dispatchTableOldestVersion, vendorID field).
  [Ch 4, p. 73-74, 76; p. 83-85]
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
/* Design (not yet implemented; constant values from USB.h to be verified
   against the actual DDK header). */
USBDriverDescription gUSBDriverDescription = {
    /* Signature */
    kTheUSBDriverDescriptionSignature,  /* identifies a USB class driver */
    kInitialUSBDriverDescriptor,        /* structure version */
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
    /* usbDriverVersion (NumVersion) — layout per USB.h, value TBD at M1B */
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

* `USBDriverDescription` (above)
* `USBClassDriverPluginDispatchTable` with:
  * `validateHWProc` — required non-nil for the table; for interface
    loading it is never called; provide a stub returning kUSBNoErr.
  * `initializeDeviceProc` — required non-nil for the table; we are an
    interface-only driver; provide a stub returning an error (documented
    behavior: device load of USBMIDI9 is not supported).
  * `initializeInterfaceProc` — the real entry point (Section 5.3).
  * `finalizeProc` — cleanup (Section 5.6).
  * `notificationProc` — hot-unplug / sleep handling (Section 5.6).
* `USBMIDI9DispatchTable` (proposed name; final name to be fixed at M1B)
  — the driver-specific export for Probe/shim communication, modeled on
  the HID dispatch-table precedent (versioned). [Ch 4, p. 76]

### 5.3 Proposed M1 state machine

```text
unloaded
   |
   | USB Manager scans 'ndrv'/'usbd' files, matches USBDriverDescription
   | (interface class 0x01, subclass 0x03), loads CFM fragment, finds
   | USBClassDriverPluginDispatchTable
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
   | 2. record interfaceRef; USBAllocMem for buffers (64-byte aligned,
   |    multiple of 64) [Ch 5, p. 150; App A, p. 230]
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
   | check usbStatus (kUSBNoErr | kUSBNotRespondingError |
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
    void                *readBuffer;      /* USBAllocMem'd, 64-aligned */
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
   `usbReqCount` must be a multiple of MaxPacketSize. [Ch 5, p. 128; App A, p. 230]
3. A short packet or a full 64-byte packet terminates the request; check
   `usbActCount`; `usbStatus` holds the completion status. [Ch 5, p. 128]
4. Decode the buffer as 1..N 4-byte Event Packets with the portable core
   (`um9_packet_decode`), deliver via the probe dispatch entry, resubmit.
5. On error, never retry an unexpected `kUSBAbortedError`. [Ch 4, p. 71]

### 5.6 Hot-unplug lifecycle (verified sequence)

1. Device unplugged → pending USL calls complete with
   `kUSBNotRespondingError` or are aborted with `kUSBAbortedError`, in no
   guaranteed order relative to the notification. [Ch 4, p. 71]
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

### 5.7 Proposed Probe communication mechanism

* The driver exports `USBMIDI9DispatchTable` (name to be fixed at M1B), a
  versioned table of proc pointers — modeled on the documented HID
  dispatch-table precedent (`dispatchTableCurrentVersion` /
  `dispatchTableOldestVersion` / `vendorID` fields). [Ch 4, p. 76]
* The Probe (a Mac OS 9 application) finds the driver with
  `USBGetNextDeviceByClass(&deviceRef, &connID, 0x01, 0x03,
  kUSBAnyProtocol)` starting from `kNoDeviceRef`, then
  `FindSymbol(connID, "\pUSBMIDI9DispatchTable", ...)` — inside
  `SetZone(SystemZone())` because class drivers load in the System Zone.
  [Ch 4, p. 83-85; Ch 6, p. 179]
* For hotplug awareness the Probe registers with
  `USBInstallDeviceNotification` (kNotifyAddInterface / kNotifyRemoveInterface);
  the notification callback runs at **task time** and may use Toolbox
  calls. [Ch 4, p. 84-85; Ch 6, p. 185-187]
* The dispatch table will expose, at minimum: register/unregister a packet
  consumer callback, device info, and (later) send. Function signatures
  are M1B design work — the M1A gate fixes the mechanism, not the ABI.
* Note: `USBGetNextDeviceByClass` appears with inconsistent prototypes in
  the two Chapter 4 listings (4 vs 5 arguments); the Chapter 6 reference
  (5 arguments, with `CFragConnectionID *connID`) is authoritative.
  [Ch 4, p. 83-84 vs Ch 6, p. 179]

### 5.8 CodeWarrior/build requirements known so far

* Project type: CodeWarrior shared library ('shlb') target(s); merge via
  "Mac OS Merge" into a single file with type `'ndrv'`, creator `'usbd'`,
  "Copy Code Fragments" checked (merge feature documented for CodeWarrior
  IDE 2.0+; the DDK-era exact version is unresolved). [App A, p. 227]
* Export list (.exp / linker exports): `USBDriverDescription`,
  `USBClassDriverPluginDispatchTable`, `USBMIDI9DispatchTable`.
  [Ch 4, p. 60-65]
* Link against the USB Services Library (USBServicesLib); weak-link it if
  any isochronous calls are used, else the driver will not load on systems
  lacking them. [App A, p. 226]
* Headers: USB.h (1.3 or later; the `kClassDriverPluginVersion` in the
  header must match the 3-arg notification proc we implement).
  [Ch 5, p. 106; App A, p. 226]
* File placement: the built `'ndrv'`/`'usbd'` extension goes in the
  Extensions folder (or System Folder for the driver search to find it);
  exact packaging to be confirmed from the DDK ReadMe. [Ch 3, p. 47]

---

## 6. Unresolved questions

* **USB DDK 1.4.2 media and headers** (USB.h, USBClassDriver.h,
  USBDriver.h, USBServicesLib import library, sample class drivers) — not
  yet obtained; leads: Macintosh Garden / period Apple developer CDs.
  Record provenance when obtained; do not commit to this MIT repository.
* Exact **kClassDriverPluginVersion value** and the exact
  **notification-proc signature** for the USB software version on the
  target G4 — to be confirmed against the actual USB.h.
* USB software version actually shipped on the target Power Mac G4
  (Mac OS 9.x) and any **1.4.2-specific changes** not in Rev 26.
* `USBConfigureInterface` set-interface behavior in later USB software
  (Rev 26 documents that it does not set the interface; later behavior
  unverified).
* Exact **CodeWarrior version** used by the DDK era (ReadMe).
* Whether **sleep notifications** must be handled for the target G4
  (desktop Macs do not receive them per Rev 26, p. 70).
* `USBMIDI9DispatchTable` ABI and Probe UI design (M1B).
* OMS/FreeMIDI shim design (deferred; M4/M6).

---

## 7. Source/provenance table

| Source | Kind | Provenance | Used for |
|---|---|---|---|
| Apple Computer, *Mac OS USB DDK API Reference*, Preliminary Working Draft, Revision 26, 12/23/99, 252 pp. | Primary Apple doc | Downloaded from Apple's own archive: `developer.apple.com/library/archive/documentation/Hardware/DeviceManagers/usb/usb_ref/usb_api_ref_v26.pdf`; PDF metadata (Author "Apple Computer", FrameMaker 5.5, 1999-12-23) verified. Local copy (research only, NOT committed): `~/research/usbddk/usb_api_ref_v26.pdf` + `.txt` extraction. | All assertions above; citations `[Ch N, "Section", p. N]` |
| Apple USB DDK headers (USB.h etc.) and sample class drivers | Primary Apple material | **Not yet obtained** (see Section 6) | Constant values (kClassDriverPluginVersion, kUSBCurrentPBVersion, kUSBNoErr, kUSBIn/kUSBOut/kUSBBulk, ...) to be verified at M1B |
| Inside Macintosh: PowerPC System Software, Ch 3 (Code Fragment Manager) | Secondary Apple doc | Referenced by Rev 26 preface (p. 15); not yet obtained | CFM background (FindSymbol etc.) |
| USB specification / USB-MIDI 1.0 class spec | Industry spec | usb.org; already used by the portable core | Descriptor/event-packet formats |

Discipline notes: no IOKit / IOUSBDeviceInterface / CoreMIDI / DriverKit
material was used; those are Mac OS X APIs and are rejected for this
target. No OMS/FreeMIDI API was designed here. No Keystation VID/PID
appears anywhere in the design (the descriptor in 5.1 is fully generic).
