/**
 * The performance tracker of build/host64/boot (port/docs/HOST_BUILD.md §35):
 * per displayed frame, where the wall time went (the game's own work, GX
 * encoding, Aurora's frame end, waiting for the GPU, waiting for a frame slot
 * / the display, sleeping in the pacer), the audio queue, new pipelines and
 * the cursor (the mouse's target against the game's KPAD pointer). `--perf`
 * writes a CSV of every frame and a summary at exit; F3 in a window shows an
 * overlay. Only clocks are read on the game's path: the deterministic
 * headless runs are unchanged.
 */

#ifndef PORT_PERF_H
#define PORT_PERF_H

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phases timed by the host itself (the others come from wpgx_get_perf). */
enum {
    PORT_PERF_GX = 0, /* the GX stream into Aurora: FIFO writes, flushes, draw-done syncs */
    PORT_PERF_SLEEP,  /* the real-time pacer sleeping */
    PORT_PERF_INPUT,  /* the window system: events, input devices */
    PORT_PERF_HOST_PHASES
};

/* Starts tracking. csvPath: the per-frame CSV (the summary goes next to it,
 * .txt); NULL keeps the numbers in memory for the overlay only. overlay:
 * start with the overlay shown. label: the run's settings (adapter, present
 * mode, pipelines, ...) for the summary's header. */
void PortPerfStart(const char* csvPath, int overlay, const char* label);
int PortPerfActive(void);

/* Monotonic nanoseconds. */
u64 PortPerfNow(void);
/* Adds time to a host phase of the current frame. */
void PortPerfAdd(int phase, u64 ns);
/* A draw-done sync of the GX stream (the game thread waiting for Aurora's
 * FIFO processor to run everything sent so far): counted, and its time kept
 * apart (it is also part of PORT_PERF_GX). */
void PortPerfSync(u64 ns);

/* The game's view of remote 0's pointer this frame against the target the
 * input layer gave it (both -1..1 across the picture). */
typedef struct PortPerfCursor {
    f32 targetX, targetY;
    int targetValid;
    f32 kpadX, kpadY;
    int kpadValid;
} PortPerfCursor;

/* At each displayed frame's boundary (the next Aurora frame begun). */
void PortPerfFrame(int frame, int scene, const PortPerfCursor* cursor);

/* A line for the summary, kept in order with the frame it came at (the
 * window's geometry at start and at each change; at most 32). */
void PortPerfNote(const char* line);

/* F3. */
void PortPerfToggleOverlay(void);
/* Inside the Aurora frame, before it ends: the overlay, if shown. */
void PortPerfDrawOverlay(void);

/* At exit: the CSV and the summary (also on stderr). */
void PortPerfFinish(void);

#ifdef __cplusplus
}
#endif

#endif
