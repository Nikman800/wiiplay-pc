/**
 * Where the host's GX command stream goes (port/docs/HOST_BUILD.md §26).
 *
 * The decomp's GX SDK encodes commands through the host write-gather pipe
 * (port/wgpipe.h, src/host_wgpipe.cpp); a sink receives the stream in order
 * and the SDK's synchronisation points. The GX harnesses install one that
 * feeds libwiiplay_gx (Aurora); without one the stream is dropped.
 */

#ifndef PORT_GXSINK_H
#define PORT_GXSINK_H

#if !defined(PORT_HOST)
#error "port/host/include is only for host builds (define PORT_HOST)"
#endif

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_GX_SYNC_FLUSH = 0,   /* GXFlush: commands so far may run */
    PORT_GX_SYNC_DRAWDONE = 1, /* GXDrawDone / GXSetDrawSync: run them all, wait */
    PORT_GX_SYNC_DISPLAY = 2  /* GXCopyDisp: the frame is complete */
};

typedef struct PortGXSink {
    /* Big-endian GX command bytes, in order (not split at command bounds). */
    void (*write)(const void* data, u32 size, void* user);
    /* Called at a command boundary, after every byte before it was written. */
    void (*sync)(u32 kind, void* user);
    void* user;
} PortGXSink;

/* Returns the previous sink; NULL drops the stream. */
const PortGXSink* PortGXSetSink(const PortGXSink* sink);
/* Bytes sent to the stream so far (sink or not). */
u64 PortGXStreamBytes(void);

/* For the host GXFifo / GXMisc (src/host_gxfifo.cpp, host_gxmisc.cpp). */
void PortWGPipeSetStream(void);
void PortWGPipeSetMemory(void* base, void* writePtr, void* limit);
int PortWGPipeIsMemory(void);
/* Pads the memory FIFO to 32 bytes; returns its write pointer. */
void* PortWGPipeMemoryFinish(int* wrapped);
/* At a command boundary: hand the stream to the sink and sync (no-op in memory mode). */
void PortWGPipeSync(u32 kind);
/* GXCopyDisp (src/revolution/GX/GXFrameBuf.c, host only). */
void PortGXDisplayCopied(void* dest, int clear);

#ifdef __cplusplus
}
#endif

#endif
