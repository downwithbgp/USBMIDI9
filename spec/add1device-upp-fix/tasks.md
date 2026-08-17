# add1Device UPP Mixed Mode fix (production integration crash)

## Problem statement

Production PPCC (a41961e1) crashes on G4 / OMS 2.3.8 during OMS Setup →
Search with an Address Error (MacsBug PC=FFFFFFF3, instruction fetch from
FFFFFFF1/FFFFFFF3; 68K-register context with A7 = 2009E456, FFFF-poisoned
stack). The PEF/CFM special-main entry is CLOSED (proven working by the
preceding diagnostic); treat this as a production-path invalid indirect
call / UPP / Mixed Mode defect.

## Root-cause hypothesis (evidence-backed)

`oms_add_devices` (oms/oms_driver.c) invokes the OMS-provided `add1Device`
callback with a **direct call**:

```c
(void)add1Device(&dev, (short)sizeof(dev));   /* oms_driver.c:460 */
```

But on the PPC CFM build, the omdvAddDevices compat-level-1 callback
`par1` is an `OMSDvrAdd1DevProc1UPP` = a `UniversalProcPtr` (routine
descriptor). The OMS Programming Interface spec and the authentic
`OMSDrvUPPs.h` require invoking it through the Mixed Mode trampoline:

```c
deviceH = CallOMSDvrAdd1DevProc1(add1Device, &dev, sizeof(dev));
/* = CallUniversalProc(add1Device, uppOMSDvrAdd1DevProc1Info, &dev, devSize) */
```

Directly calling a routine-descriptor/68K pointer as PPC code executes
non-code bytes → instruction fetch from a bad address → Address Error.

Evidence:
- Spec (spec.txt 2947-2987): compat >= 1 must call
  `CallOMSDvrAdd1DevProc1(add1Device, &dev, sizeof(dev))`.
- Authentic OMSDrvUPPs.h: `uppOMSDvrAdd1DevProc1Info` = kPascalStackBased
  | RESULT_SIZE(4) | STACK_ROUTINE_PARAMETER(1,4) | STACK_ROUTINE_PARAMETER(2,2);
  `CallOMSDvrAdd1DevProc1` = `CallUniversalProc(...)`.
- oms_driver.c line 405 comment already says "call
  CallOMSDvrAdd1DevProc1(add1Device, &dev, sizeof(dev))" — the code
  contradicts its own comment.
- The diagnostic (b46c7251) returned -1 on omdvInit, so OMS never issued
  omdvAddDevices; production returns 0, so OMS Setup Search reaches
  omdvAddDevices → the crash. Consistent with "diagnostic passed, production
  crashes."
- Only add1Device is called directly; the RX path (oms_rx_deliver) and the
  OMSReadHook2 send UPP are correctly routed via CallUniversalProc /
  NewOMSReadHook2. The dispatch-table procs (setEventCallback,
  enumerateInterfaces, getInterfaceInfo, dequeueBytes) are PPC→PPC (both
  fragments are PPC CFM) so direct calls there are correct.

## Acceptance criteria

- [c1] oms/oms_driver.c calls `CallOMSDvrAdd1DevProc1(add1Device, &dev,
  sizeof(dev))` (not a direct call) and types the omdvAddDevices par1
  through the UPP surface.
- [c2] host-check models `OMSDvrAdd1DevProc1UPP`, `uppOMSDvrAdd1DevProc1Info`,
  and `CallOMSDvrAdd1DevProc1` (mirroring authentic OMSDrvUPPs.h) so the
  shim compiles under `make check-classic`.
- [c3] host test exercises omdvAddDevices through the Mixed Mode trampoline
  and asserts the callback is dispatched via CallUniversalProc (not a
  direct call); existing device-registration assertions still hold.
- [c4] `make test`, `make test-sanitize`, `make check-classic` all green.
- [c5] No change to the ndrv target, no PEF byte patching, no new
  diagnostic PEF.

## Out of scope (do not change)

- The ndrv class driver (known-good 87f39a22, use as-is).
- The PEF/CFM special-main (closed).
- Any production OMS message semantics not touching add1Device.
