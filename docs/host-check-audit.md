# Host-check Classic header audit

Every header in `host-check/` is a **stub**: it exists only so
`make check-classic` can syntax/type-check the Classic Mac OS sources on
Linux. The G4 build uses the authentic headers (CodeWarrior Universal
Headers, the DDK 1.4.1 `Interfaces/USB.h`, the Opcode OMS 2.0 SDK
headers). This table records, for every stub symbol/type, the authentic
source and the exact or intentional difference — the lesson of the
`Notifications.h` audit is that a plausible stub is not evidence, so
nothing below is asserted without a cited authentic source.

Authentic sources used:

- **UI 3.3.2** — Universal Interfaces 3.3.2 `CIncludes`
  (`~/research/UniversalInterfaces3.3.2.sit.hqx`; the local .sit image
  is a truncated HFS slice and cannot be extracted, so the comparison
  uses the identical content mirrored at
  `github.com/elliotnunn/UniversalInterfaces` `3.3.2/Universal/Interfaces/CIncludes`).
- **OMS SDK** — Opcode OMS 2.0 SDK (28-Jan-98) `Headers/`
  (`~/research/oms/sdk/OMS 2.0 SDK 28-Jan-98/Headers/`).
- **DDK 1.4.1** — Mac OS USB DDK 1.4.1 `Interfaces/USB.h`
  (`~/research/usbddk/adc-cd-jan2001/`).
- **Rev 26** — Mac OS USB DDK API Reference Rev. 26, 12/23/99
  (`~/research/usbddk/usb_api_ref_v26.txt`).
- **PEF disassembly** — Opcode OMS 2.3.8 USB components
  (`~/research/oms/oms238c/`), Ghidra 12.1.

## The audit table

| Stub symbol/type | Authentic source | Exact / intentional difference |
|---|---|---|
| `MacTypes.h`: UInt8/16/32, SInt32, Boolean, OSType, OSStatus, OSErr, Ptr, Handle, StringPtr, Str31/63, NumVersion + stage constants, THz, nil/true/false, CALLBACK_API_C/EXTERN_API_C | UI 3.3.2 MacTypes.h (688 lines) | Subset of basic types used by the Classic sources; definitions verbatim (Handle/StringPtr added in the OMS audit). All macro machinery (PRAGMA_*, TARGET_*, FOUR_CHAR_CODE, CALLBACK_API/STACK_UPP_TYPE, mac68k alignment) intentionally omitted — the G4 compiler provides it. No invented types. |
| `MacErrors.h`: noErr, paramErr, kUSB* errors (-6908..-6999) | UI 3.3.2 MacErrors.h (2707 lines) + DDK-era USB error block | Numeric values copied verbatim; only the error block the driver/probe compare against is present. No invented values. |
| `Memory.h`: GetZone, SetZone, THz | UI 3.3.2 Memory.h (renames to MacMemory.h) | Two zone calls; signatures verbatim. No invented calls. |
| `Events.h`: KeyMap, KeyMapByteArray, GetKeys | UI 3.3.2 Events.h (417 lines) | KeyMap = UInt32[4] / KeyMapByteArray = UInt8[16] modeled exactly on the real header (the byte view with mask 1 << (N%8) is G4-verified). Only GetKeys is declared. |
| `OSUtils.h`: QElem/QElemPtr, SystemZone, Delay, Ticks | UI 3.3.2 OSUtils.h (467 lines) | QElem added in the OMS audit (NMRec.qLink model). Signatures verbatim; only the calls used are declared. |
| `Notifications.h`: NMRec, NMRecPtr, NMProcPtr(NMRecPtr), NMInstall/NMRemove | UI 3.3.2 **Notification.h** (the authentic file name is singular; 123 lines) | **Corrected in the OMS G4 audit.** The pre-audit stub invented `eventTime`/`nMsg` and modeled NMInstall as a timer — false. Now models the authentic NMRec verbatim (qLink, qType, nmFlags, nmPrivate, nmReserved, nmMark, nmIcon, nmSound, nmStr, nmResp, nmRefCon) and NMProcPtr = void(*)(NMRecPtr). NMUPP is aliased to NMProcPtr for the host check (the real header's UPP machinery is a G4-only concern). No invented fields. Cross-checked against the real OMS 2.3.8 binaries: both Opcode USB components build NMRecs with qType = 8, nmStr, nmResp and call NMInstall ONCE per alert (PEF disassembly) — never as a timer. |
| `CodeFragments.h`: CFragConnectionID, CFragSymbolClass, kTVectorCFragSymbol/kDataCFragSymbol, FindSymbol | UI 3.3.2 CodeFragments.h (538 lines); call shape per DDK 1.4.1 samples (HIDReader.c) | FindSymbol's name parameter is `const char *` instead of the Str63 array form to keep gcc's -Wstringop-overflow quiet; the authentic parameter is read as a length-prefixed string either way (documented in the stub). Two symbol-class constants only. |
| `DriverServices.h`: kTaskLevel/kHardwareInterruptLevel/kSecondaryInterruptLevel, CurrentExecutionLevel, CallSecondaryInterruptHandler2 | DDK 1.4.1 kit (PrinterClassDriver.c, USBModem/ModemDriver.c, ShimSerialHAL.c) | kTaskLevel == 0 is kit-verified; the handler typedef name is unverifiable from the kit so the stub uses the sample-verified raw function-pointer shape (documented in the stub). Only the two calls used by the safe bulk-read helper. |
| `USB.h`: USL types/constants/prototypes (USBPB, USBCompletion, USBGetNextDeviceByClass, ...), USBClassDriverPluginDispatchTable | DDK 1.4.1 Interfaces/USB.h; Rev 26 Ch 4/6 for the USB Manager surface | **Extended in the OMS G4 audit** with the authentic USB Manager notification surface: USBDeviceNotificationParameterBlock, USBDeviceNotificationCallbackProcPtr, USBInstallDeviceNotification, USBRemoveDeviceNotification, USBGetDriverConnectionID, kNotifyAddDevice=0/kNotifyRemoveDevice=1/kNotifyAddInterface=2/kNotifyRemoveInterface=3/kNotifyAnyEvent=0xFF (values verified from the OMS USB Manager's notification callback disassembly). USL subset verbatim. No invented calls. |
| `OMS.h`: OMSPacket/OMSMIDIPacket, OMSDevice, OMSPortID, OMSSendParams, OMSString, OD_MAX_* , oms* packet flags, OMSErr, LinkToOMSGlue, OMSGetCallAddress, OMSOpenDriverResFile/OMSCloseDriverResFile | OMS SDK 2.0 OMS.h + OMSTypes.h (28-Jan-98) | Field order/types copied verbatim from the SDK; mac68k alignment pragmas and the pascal/UPP machinery intentionally omitted (the G4 compiler handles them; the stub defines `pascal` empty). **PPC bridge additions (spec/oms-ppc-bridge):** `typedef short OMSErr` (OMSTypes.h) and the two glue calls with authentic signatures — `OMSErr LinkToOMSGlue(void)`, `long OMSGetCallAddress(short callNum)` (OMS.h; the Spec: drivers call LinkToOMSGlue, then resolve 68K routine addresses). No invented fields. |
| `OMSDriver.h`: omdv* message enum, OMSDvrAdd1DevProc1, OMSPortID-based messages, driver params flags, OMSReceivedFromPort | OMS SDK 2.0 OMSDriver.h + the OMS Programming Interface spec (Mar 1995) "OMS Drivers" | Message numbers and parameter conventions copied verbatim from the SDK header (cross-checked against the Spec). **PPC bridge addition (spec/oms-ppc-bridge):** the OMSReceivedFromPort declaration is now wrapped in `#ifndef powerc` exactly like the authentic header (`#pragma parameter OMSReceivedFromPort(__A1, __D0)` noted in the comment) so a `-Dpowerc` host build enforces the PPC 68K-bridge path. No invented messages. |
| `OMSGlueProcs.h`: callOMSReceivedFromPort | OMS SDK 2.0 OMSGlueProcs.h (28-Jan-98; the file is generated — "*** THIS FILE GENERATED AUTOMATICALLY -- DO NOT MODIFY ***") | Only the one call number the shim resolves (112) is modeled; value verbatim. |
| `MixedMode.h`: kRegisterBased, kRegisterD0/kRegisterA1, ProcInfoType, UniversalProcPtr, SIZE_CODE, REGISTER_ROUTINE_PARAMETER, CallUniversalProc, ProcInfo phase enum | UI 3.3.2 MixedMode.h (699 lines) | **Added in the PPC bridge audit (spec/oms-ppc-bridge).** Constants/macros copied verbatim from the authentic header; only the register-based surface the wrapper needs. `UniversalProcPtr` = `struct RoutineDescriptor *` (MacTypes.h). The host **cannot** validate actual PPC<->68K mode switching — `CallUniversalProc` is declared but only test-supplied forwarding exists on the host; register dispatch is a G4 hardware-gate item. |

## Eliminated inventions

The OMS G4 audit removed the one invented API found:

- `host-check/Notifications.h` pre-audit NMRec fields
  `eventTime`/`nMsg` and the `NMProcPtr(UInt32, UInt32)` signature —
  replaced with the authentic UI 3.3.2 model (see row above).

## What this audit does NOT certify

Green host tests against these stubs prove only that the Classic
sources are self-consistent. Classic API correctness is established by
the primary sources above and, ultimately, by the G4 gates.
