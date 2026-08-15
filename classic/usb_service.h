/*
 * USBMIDI9 service boundary.
 *
 * The service boundary is the internal API through which MIDI-system
 * integrations (OMS first, FreeMIDI later) consume USB-MIDI devices handled
 * by the USBMIDI9 transport. Conceptually it will provide:
 *
 *   - enumerate devices
 *   - enumerate logical input/output ports
 *   - open input / open output
 *   - receive MIDI events / send MIDI data
 *   - close port
 *   - device attach/remove notifications
 *
 * STATUS: design placeholder only. The actual binary/API mechanism is an
 * open architectural question (see docs/research.md, "Service boundary
 * design"). Candidate mechanisms — Component Manager, driver control/status
 * calls, Gestalt mechanism, shared service, callback table — must be
 * evaluated against Classic Mac OS 9 practice; do not pick one merely
 * because it resembles a modern Unix/macOS API, and do not invent APIs.
 *
 * TODO(classic-service-api):
 * Resolve the verified service mechanism from primary documentation before
 * declaring any function, type, or ABI here.
 */

#ifndef USBMIDI9_CLASSIC_USB_SERVICE_H
#define USBMIDI9_CLASSIC_USB_SERVICE_H

#endif /* USBMIDI9_CLASSIC_USB_SERVICE_H */
