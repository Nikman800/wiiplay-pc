/**
 * --record DIR[:N] and --latency-marker (port/docs/HOST_BUILD.md §37): a log of
 * a live run for finding where time goes between an input and the picture.
 *
 *   DIR/frames.csv  one row per displayed frame: the frame, wall time since the
 *                   start (ns), game time (VI units of 1/60000 s), retrace, the
 *                   GPU latency of the latest completed frame and the frames in
 *                   flight, and whether the latency marker was shown
 *   DIR/input.csv   every window-system input event as it was applied: wall
 *                   time (ns), the frame it went into, what
 *   DIR/frame_NNNNNN.ppm  every Nth frame (N > 0), half size
 *
 * The latency marker: a white square in the picture's top-left corner on the
 * frame an input (key, mouse button, gamepad button) is applied, so that a
 * screen recording shows input-to-photon latency.
 */
#ifndef PORT_RECORD_H
#define PORT_RECORD_H

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0, or -1 with a message in err. */
int PortRecordStart(const char* spec, char* err, int errLen);
void PortRecordStop(void);
int PortRecordActive(void);
/* After each displayed frame began (the previous one presented). */
void PortRecordFrame(int frame, long long gameUnits, u32 retrace);
/* An input event, as text ("key Space down", "mouse 120.0 33.0"). */
void PortRecordInput(const char* what, int pressed);

void PortRecordSetMarker(int enabled);
/* Inside the frame, before it ends: draws the marker if an input came. */
void PortRecordDrawMarker(void);

#ifdef __cplusplus
}
#endif

#endif
