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
    uint32_t cache_key;     /* pipeline cache key; 0 = the library's own */
    int blocking_pipelines; /* 1: draws wait for their pipeline (none skipped) */
    int allow_cpu_adapter;  /* 1: accept a software adapter (lavapipe) */
    int log_level;          /* 0 debug, 1 info, 2 warning, 3 error */
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
