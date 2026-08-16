/*
 * USBMIDI9 OMS driver — 'OMdv' 128 import (Rez).
 *
 * This file adds the driver's code resource to the resource file built
 * by the MacOS Merge / Project Type = Resource File target. It is added
 * to the SAME resource-only target as oms_driver.r; Rez merges both
 * into the output file's resource fork.
 *
 * The 'OMdv' 128 resource is the Target-A PEF container with the OMS
 * 4-byte length header (`00 00 <len>` big-endian = PEF container
 * length) — the authenticated Roland SC-8850 shape (see
 * docs/g4-handoff.md, "The 'OMdv' resource").
 *
 * `read` embeds the file's ENTIRE contents verbatim as the resource
 * data (Rez language reference): OMdvData must therefore already
 * contain header + PEF bytes. OMdvData is produced from the Target-A
 * PEF by tools/omdvdata.c, which also runs the four validations:
 *   - raw PEF begins with "Joy!peffpwpc"
 *   - OMdvData size = PEF size + 4
 *   - OMdvData[0:4] = big-endian PEF size
 *   - OMdvData[4:16] = "Joy!peffpwpc"
 *
 * The file path is resolved from Rez's working directory (the project
 * folder in CodeWarrior): keep OMdvData in the project folder, or use a
 * full path here.
 */
read 'OMdv' (128) "OMdvData";
