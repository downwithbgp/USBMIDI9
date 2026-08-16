/*
 * USBMIDI9 OMS driver — 'PPCC' 1 import (Rez).
 *
 * This file adds the driver's native-PPC code fragment to the resource
 * file built by the MacOS Merge / Project Type = Resource File target.
 * It is added to the SAME resource-only target as oms_driver.r; Rez
 * merges both into the output file's resource fork.
 *
 * The 'PPCC' 1 resource is the raw Target-A PEF container: the logical
 * resource payload begins with "Joy!peffpwpc" and contains NO length
 * prefix. The Resource Manager adds the fork's record framing
 * ([be32 length][data]) automatically when the resource is written;
 * Get1Resource returns only the PEF bytes. (The 4-byte length seen at
 * the start of resource records in authentic Opcode forks is that fork
 * framing — earlier readings of it as payload were wrong and must not
 * be reproduced.)
 *
 * The loader path (OMS 2.3.8, disassembly-verified): the OMS driver
 * search reads codeResID = OMSDriverParams.xxportNumB (word at +6 of
 * the 'OMdi' 128 data) and first tries loadCode(pref=2) =
 * Get1Resource('PPCC', codeResID); on success the fragment is
 * materialized and loaded via GetDiskFragment, and OMS calls the
 * fragment's main symbol through CallUniversalProc with
 * uppOMSDriverProcInfo (kPascalStackBased). 'OMdi' 128 in oms_driver.r
 * therefore sets xxportNumB = 1 to match this resource's id.
 *
 * Resource attributes: none. The authentic Opcode 'PPCC' 1 (OMS Time
 * Manager, OMS 2.3.8) has fork attrs = 0x00 (verified from the fork
 * map), so the Rez read carries no attribute list.
 *
 * `read` embeds the named file's data fork verbatim as the resource
 * data (Rez language reference). The path is resolved from Rez's
 * working directory — the resource project folder
 * (USBMIDI9:USBMIDI9 OMS Resources:). "::" = the parent folder, so
 * "::USBMIDI9_OMS" = USBMIDI9:USBMIDI9_OMS = the Target-A PEF built by
 * the OMS PEF target (its data fork IS the PEF container; 9501 bytes
 * as of the 2026-08-16 G4 build — well within Rez's read limits). No
 * PEF copy into the resource project folder is required.
 *
 * Fallback if CW Rez cannot resolve "::" paths: copy USBMIDI9_OMS into
 * the resource project folder and use read 'PPCC' (1) "USBMIDI9_OMS";
 * (or an absolute path USBMIDI9:USBMIDI9_OMS).
 */
read 'PPCC' (1) "::USBMIDI9_OMS";
