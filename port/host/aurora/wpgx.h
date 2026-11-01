/*
 * wpgx: the port's C glue around Aurora (port/extern/aurora), built as
 * libwiiplay_gx.so by port/host/aurora/CMakeLists.txt. Only these functions
 * are exported; Aurora's own GX/OS/VI entry points stay inside the library,
 * so the decomp's revolution SDK can live in the same process.
 *
 * Milestone 1 (port/docs/HOST_BUILD.md §25): bring-up (windowed or
 * headless), frames, synchronous EFB readback, and a few helpers that drive
 * Aurora's GX API directly for the bring-up test (gx_harness). From
 * milestone 2 the game's GX calls reach Aurora through the FIFO instead
 * (WGPIPE redirect); the wpgx_test_* helpers stay for tests only.
 */
#ifndef WPGX_H
#define WPGX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t width;  /* EFB / window size; 0 = 640 */
    uint32_t height; /* 0 = 480 */
    int headless;    /* 1: no window or surface; render offscreen only */
    /* Directory for Aurora's user files and caches. Required: nothing is
       written under the platform's per-user data directory. */
    const char* data_dir;
    int disk_cache;         /* 0: no pipeline/Dawn cache on disk */
    uint32_t cache_key;     /* pipeline cache key; 0 = the library's own (its patch version and build ID) */
    int blocking_pipelines; /* 1: draws wait for their pipeline (none skipped) */
    int allow_cpu_adapter;  /* 1: accept a software adapter (lavapipe) */
    int log_level;          /* 0 debug, 1 info, 2 warning, 3 error */
    /* Windowed mode only (headless 0), milestone 5 (HOST_BUILD.md §30). The
       EFB stays width x height (frame buffer scale 1, aspect-fit viewport
       policy); the window only scales the presented picture. */
    uint32_t window_width;  /* initial window size in points; 0 = width x height */
    uint32_t window_height;
    int fullscreen;         /* 1: start fullscreen */
    int vsync;              /* 1: present with vsync (FIFO / FIFO relaxed) */
    const char* title;      /* window title; NULL = "wiiplay" */
    /* HOST_BUILD.md §35 */
    const char* adapter;    /* graphics adapter: index, type (discrete, integrated, cpu) or part of the
                               name (wpgx_adapter_list); NULL = the high-performance default */
    int vsync_mailbox;      /* with vsync: present in mailbox mode where offered (never waits for a refresh) */
} wpgx_config;

/* Returns 0 on success. */
int wpgx_init(const wpgx_config* config);
void wpgx_shutdown(void);

/* One frame. wpgx_begin_frame() returns 0 when the frame must be skipped. */
int wpgx_begin_frame(void);
void wpgx_end_frame(void);

/* Colour of the EFB as of the last wpgx_end_frame(): RGBA8, top row first,
   width * 4 bytes per row. Waits for the GPU. Returns 0 on success. */
int wpgx_read_frame(uint8_t* rgba, size_t size, uint32_t* width, uint32_t* height);

/* Name of the graphics adapter in use (for logs). */
const char* wpgx_adapter_name(void);
/* Its type ("discrete", "integrated", "cpu", "unknown") and backend ("vulkan", ...). */
const char* wpgx_adapter_type(void);
const char* wpgx_backend_name(void);
/* The adapters offered, one per line ("index: name (type, backend, driver)"). */
const char* wpgx_adapter_list(void);
/* The window's present mode ("fifo", "fifo-relaxed", "mailbox", "immediate"; "none" headless). */
const char* wpgx_present_mode(void);
/* Draws wait for their pipeline (1) or are skipped until it is compiled (0); from the next draw. */
void wpgx_set_blocking_pipelines(int blocking);

/* --- Performance counters (HOST_BUILD.md §35) ---------------------------- */
/* Cumulative since wpgx_init; the caller takes differences per frame. Times
   in nanoseconds of the calling thread's wall clock unless noted. */
typedef struct {
    uint64_t begin_frame_ns;     /* wpgx_begin_frame: waiting for a free frame slot (GPU / present behind) */
    uint64_t end_frame_ns;       /* wpgx_end_frame without the writeback: FIFO drain, recording, submission */
    uint64_t writeback_ns;       /* copy writeback: waiting for the GPU to finish the frame's copies */
    uint64_t read_frame_ns;      /* wpgx_read_frame */
    uint64_t pipeline_compile_ns; /* compiling pipelines, any thread */
    uint64_t pipeline_wait_ns;   /* draws waiting for their pipeline (FIFO thread) */
    uint32_t pipelines_created;
    uint32_t pipelines_queued;   /* now */
    uint32_t pipeline_waits;
    uint32_t skipped_draws;      /* draws dropped: pipeline not ready (non-blocking pipelines) */
    uint32_t copies_written;
    uint32_t draw_calls;         /* the last submitted frame */
} wpgx_perf;
void wpgx_get_perf(wpgx_perf* perf);

/* An overlay drawn over the presented picture (windowed only; Dear ImGui):
   lines of text and a bar graph of `count` values, full height at
   `graph_max`, with a mark at `graph_mark`. Call between wpgx_begin_frame and
   wpgx_end_frame; it shows in that frame. Returns 0 when nothing can be drawn
   (headless). */
int wpgx_overlay(const char* text, const float* values, int count, float graph_max, float graph_mark);

/* --- The port's GX command stream (milestone 2, HOST_BUILD.md §26) -------- */

/* Big-endian GX command bytes from the decomp's GX SDK (the host write-gather
   pipe), in order; not necessarily split at command boundaries. */
void wpgx_fifo_write(const void* data, uint32_t size);

/* At a command boundary. WPGX_SYNC_FLUSH: the commands so far may run
   (GXFlush). WPGX_SYNC_DRAWDONE: run them all and wait (GXDrawDone). */
enum { WPGX_SYNC_FLUSH = 0, WPGX_SYNC_DRAWDONE = 1 };
void wpgx_fifo_sync(uint32_t kind);

/* Resolves 32-bit addresses found in the stream (CP array bases, CALL_DL,
   texture images, TLUT loads) to host memory: returns NULL if unmapped and
   stores the bytes readable from the result in *size. Called from Aurora's
   FIFO thread. NULL removes it. */
typedef const void* (*wpgx_address_resolver)(uint32_t address, uint32_t* size, void* user);
void wpgx_set_address_resolver(wpgx_address_resolver resolver, void* user);

/* --- EFB copies (milestone 3b, HOST_BUILD.md §28) ------------------------- */
/* Texture copies (GXCopyTex) stay on the GPU, keyed by their emulated
   destination: later texture loads from that address sample them. With
   writeback on, each copy is also encoded in the Wii texture format and
   written to its destination through the address resolver's memory when the
   frame ends (wpgx_end_frame), so CPU readers see what the hardware would
   have written. */
typedef struct {
    uint32_t address, format;           /* destination, GXTexFmt (GX_CTF_* too) */
    uint16_t width, height;             /* copy size */
    uint16_t src_left, src_top, src_width, src_height; /* EFB source rectangle */
    uint8_t half, clear, depth, vfilter; /* box-filtered half size, cleared, Z copy, vertical filter */
    uint32_t pixel_format;              /* GXPixelFmt of the EFB */
    float vfilter_weights[3];           /* row above, row, row below */
    const void* data;                   /* the copy as written to memory */
    uint32_t size;
    const void* efb;                    /* capture_sources: the EFB (RGBA8) just before the copy */
    uint32_t efb_width, efb_height;
} wpgx_copy_info;
typedef void (*wpgx_copy_observer)(const wpgx_copy_info* copy, void* user);
/* writeback 0: copies stay GPU-side only. observer (may be NULL) sees each
   copy after it was written back, in copy order. */
void wpgx_set_copy_writeback(int writeback, int capture_sources, wpgx_copy_observer observer, void* user);
/* Copies written back so far. */
uint32_t wpgx_copies_written(void);

/* --- Window, input and audio (milestone 5, HOST_BUILD.md §30) ------------ */
/* SDL lives inside this library (its symbols are hidden), so the port's live
   platform layer reaches the window, the input devices and the audio device
   through these functions only. All of them run on the thread that called
   wpgx_init. */

/* Display aspect the EFB is presented at (4:3 or 16:9 from SC: the Wii's
   640x456 frame shown on a 4:3 or a widescreen TV); 0 = square pixels. The
   picture is letterboxed or pillarboxed into the window at this aspect. */
void wpgx_set_display_aspect(float aspect);
float wpgx_display_aspect(void);
/* Window size in points (headless: the EFB size). */
void wpgx_window_size(float* width, float* height);
/* A window position (points, origin top left) -> position on the presented
   picture, 0..1 across it (outside that range in the bars). */
void wpgx_window_to_picture(float x, float y, float* u, float* v);
void wpgx_set_fullscreen(int fullscreen);
int wpgx_is_fullscreen(void);
/* Hides the system cursor over the window (the game draws its own). */
void wpgx_show_cursor(int show);

/* Input devices (keyboard and mouse come with the window). Opens the SDL
   gamepad, joystick, sensor and haptic subsystems. Returns 0 on success. */
int wpgx_input_init(void);

enum {
    WPGX_EV_QUIT = 1,         /* window closed / quit requested */
    WPGX_EV_KEY,              /* name: SDL key name; down */
    WPGX_EV_MOUSE_MOTION,     /* x, y: window position in points */
    WPGX_EV_MOUSE_BUTTON,     /* code: SDL button (1 left, 2 middle, 3 right, ...); down; x, y */
    WPGX_EV_GAMEPAD_ADDED,    /* pad: slot 0-3; name: the gamepad's name */
    WPGX_EV_GAMEPAD_REMOVED,  /* pad */
    WPGX_EV_GAMEPAD_BUTTON,   /* pad; code: SDL_GamepadButton; down */
    WPGX_EV_GAMEPAD_AXIS,     /* pad; code: SDL_GamepadAxis; x: -1..1 (triggers 0..1) */
    WPGX_EV_FOCUS,            /* down: 1 gained, 0 lost */
    WPGX_EV_RESIZED,          /* x, y: new window size in points */
};
typedef struct {
    uint32_t type;
    int32_t down;
    int32_t code;
    int32_t pad;
    float x, y;
    char name[48];
} wpgx_event;
/* Polls the window system (also done by wpgx_begin_frame) and queues the
   events. */
void wpgx_pump_events(void);
/* The next queued event: 1, or 0 when there is none. */
int wpgx_next_event(wpgx_event* event);

/* Gamepad slots 0-3 (in connection order). Sensors: gyro in rad/s, accel in
   m/s^2 (SDL's axes); returns a bit mask of what the pad has (1 gyro,
   2 accel). */
int wpgx_gamepad_sensors(int pad, float gyro[3], float accel[3]);
/* Rumble: strengths 0..1 for duration_ms (0 stops). */
void wpgx_gamepad_rumble(int pad, float low, float high, uint32_t duration_ms);

/* Audio output: an SDL audio stream on the default playback device, fed
   interleaved s16 at `rate` (SDL resamples to the device). Returns 0 on
   success. */
int wpgx_audio_open(int rate, int channels);
void wpgx_audio_close(void);
/* Appends frames to the stream. */
void wpgx_audio_queue(const int16_t* samples, uint32_t frames);
/* Frames queued and not yet taken by the device. */
uint32_t wpgx_audio_queued_frames(void);
/* Drops everything queued. */
void wpgx_audio_clear(void);
void wpgx_audio_set_gain(float gain);
/* The SDL audio driver in use ("pipewire", "pulseaudio", "dummy", ...). */
const char* wpgx_audio_driver(void);
/* The playback device's format: sample rate, channels and buffer size in
   sample frames (0 if unknown). Returns 0 on success. */
int wpgx_audio_device_format(int* rate, int* channels, int* buffer_frames);

/* Tests: synthetic SDL events, delivered through the same path as real ones. */
void wpgx_test_push_key(const char* name, int down);
void wpgx_test_push_mouse_motion(float x, float y);
void wpgx_test_push_mouse_button(int button, int down);
void wpgx_test_push_quit(void);
/* A virtual SDL gamepad with gyro and accelerometer (SDL's virtual joystick
   driver). Returns 0 on success; it arrives as WPGX_EV_GAMEPAD_ADDED. */
int wpgx_test_gamepad_attach(void);
void wpgx_test_gamepad_button(int button, int down);
void wpgx_test_gamepad_axis(int axis, float value);
void wpgx_test_gamepad_sensor(int accel, float x, float y, float z);
/* Rumble requests the virtual gamepad received (count; the last strengths). */
uint32_t wpgx_test_gamepad_rumbles(float* low, float* high);

/* --- Test helpers on Aurora's GX API (milestone 1) ------------------------ */

/* Clear colour (0xRRGGBBAA), viewport/scissor and an orthographic projection
   in EFB pixels (0,0 top left), identity position matrix, vertex format 0 =
   position xyz f32 + colour rgba8 + texcoord st f32 (direct), colour channel 0
   from the vertex colour, texcoord gens 0..n-1 = identity on TEX0. */
void wpgx_test_setup_2d(uint32_t clear_rgba, uint32_t num_texgens);
/* GXInitTexObj + GXInitTexObjLOD + GXLoadTexObj on Aurora, texels in Wii
   format (big-endian, tiled, as stored on the disc). */
void wpgx_test_load_texture(uint32_t map, const void* texels, uint16_t width, uint16_t height,
                            uint32_t gx_format, uint32_t wrap_s, uint32_t wrap_t, int linear);
/* One stage, GX_REPLACE (or GX_PASSCLR) on texcoord/map 0, blend
   SRCALPHA/INVSRCALPHA, alpha compare always, no z. */
void wpgx_test_set_tev_replace(int passclr);
/* GXSetNumTevStages(n) (orders and stages come from a material DL). */
void wpgx_test_set_num_tev_stages(uint32_t n);
/* GXCallDisplayList: BP/CP/XF commands, big-endian as on the disc. */
void wpgx_test_call_display_list(const void* dl, uint32_t size);
/* A quad (x0,y0)-(x1,y1) in pixels, z = -0.5, white, texcoords 0..1. */
void wpgx_test_quad(float x0, float y0, float x1, float y1);

#ifdef __cplusplus
}
#endif

#endif
