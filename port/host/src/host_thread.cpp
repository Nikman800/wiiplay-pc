/**
 * Cooperative, deterministic OS threads for the host (port/docs/HOST_BUILD.md §21).
 *
 * The audio runtime starts real threads: nw4r::snd's sound thread (woken by
 * the AX frame callback) and its task (DVD) thread, which the decompiled code
 * drives with message queues, thread queues and mutexes. This file runs them
 * as coroutines (ucontext) on one host thread, so every run is reproducible:
 *
 *   - A thread switch happens only where the Wii would reschedule: a thread
 *     blocks (full/empty message queue, OSSleepThread, a mutex held by
 *     another thread, OSJoinThread), or a thread of higher priority (lower
 *     number) becomes ready. Equal priorities do not preempt each other.
 *   - Interrupt context (PortOSEnterInterrupt / PortOSLeaveInterrupt, used
 *     by the AX frame driver around the frame callbacks) defers the
 *     reschedule to the end of the "interrupt", as on the Wii.
 *   - PortOSIdle() is the main thread waiting for the next retrace: every
 *     ready thread runs, whatever its priority, until all of them block.
 *   - Devices (PortOSAddDevice: the DVD drive and NAND of HOST_BUILD.md
 *     §22) finish their queued commands when every thread is blocked or the
 *     main thread idles, and call their callbacks in interrupt context.
 *
 * Threads get their own host stacks (the decomp's stacks are sized for the
 * PowerPC); the stack the game passes is only recorded. The main thread is
 * priority 16, as on the Wii. A main thread that blocks with nothing else
 * ready is a deadlock and aborts.
 *
 * The OSMutex / OSMessageQueue / OSGetCurrentThread definitions here replace
 * the weak single-threaded ones in src/host_sdk.cpp.
 */

#include <types.h>

#include <revolution/OS.h>

#include <port/host.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ucontext.h>

namespace {

enum State { ST_READY, ST_WAITING, ST_SUSPENDED, ST_DONE };

const size_t HOST_STACK_SIZE = 1024 * 1024;
const s32 MAIN_PRIORITY = 16;

struct HostThread {
    OSThread* os;
    ucontext_t ctx;
    void* stack;
    OSThreadFunc func;
    void* arg;
    void* result;
    State state;
    const void* waitKey;
    s32 prio;
    u32 order;
};

OSThread sMainOSThread;
HostThread sMain;
// Creation order, sMain first. Plain malloc storage: when the real EGG heap
// is linked (HOST_BUILD.md §22) operator new allocates from the game's heaps,
// which may not exist yet (or not be current) when a thread is created.
struct ThreadList {
    HostThread** items;
    size_t count;
    size_t capacity;

    size_t size() const {
        return count;
    }
    HostThread* operator[](size_t i) const {
        return items[i];
    }
    void push_back(HostThread* t) {
        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            items = static_cast<HostThread**>(std::realloc(items, capacity * sizeof(*items)));
            if (items == NULL) {
                std::fprintf(stderr, "host_thread: out of memory\n");
                std::abort();
            }
        }
        items[count++] = t;
    }
};
ThreadList sThreads;
HostThread* sCurrent = NULL;
int sInterruptDepth = 0;
bool sReschedulePending = false;

void Init() {
    if (sCurrent != NULL) {
        return;
    }
    std::memset(&sMain, 0, sizeof(sMain));
    sMain.os = &sMainOSThread;
    sMain.state = ST_READY;
    sMain.prio = MAIN_PRIORITY;
    sMainOSThread.priority = MAIN_PRIORITY;
    sMainOSThread.base = MAIN_PRIORITY;
    sThreads.push_back(&sMain);
    sCurrent = &sMain;
}

HostThread* Find(const OSThread* thread) {
    Init();
    for (size_t i = 0; i < sThreads.size(); i++) {
        if (sThreads[i]->os == thread) {
            return sThreads[i];
        }
    }
    return NULL;
}

[[noreturn]] void Fail(const char* msg) {
    std::fprintf(stderr, "host_thread: %s\n", msg);
    std::abort();
}

// The ready thread with the best priority (lowest number), oldest first;
// `skip` is left out.
HostThread* BestReady(const HostThread* skip) {
    HostThread* best = NULL;
    for (size_t i = 0; i < sThreads.size(); i++) {
        HostThread* t = sThreads[i];
        if (t == skip || t->state != ST_READY) {
            continue;
        }
        if (best == NULL || t->prio < best->prio) {
            best = t;
        }
    }
    return best;
}

OSSwitchThreadCallback sSwitchCallback = NULL;

void SwitchTo(HostThread* next) {
    if (next == sCurrent) {
        return;
    }
    HostThread* prev = sCurrent;
    if (sSwitchCallback != NULL) {
        // EGG::Thread::switchThreadCallback: onExit / onEnter of EGG threads.
        sSwitchCallback(prev->os, next->os);
    }
    sCurrent = next;
    if (swapcontext(&prev->ctx, &next->ctx) != 0) {
        Fail("swapcontext failed");
    }
}

// Devices (PortOSAddDevice): the DVD drive and the NAND of HOST_BUILD.md §22.
const int MAX_DEVICES = 8;
PortOSDeviceFunc sDevices[MAX_DEVICES];
int sDeviceCount = 0;

// Every thread is blocked (or the main thread idles): the hardware gets to
// finish what it was asked to do. Each device completes its pending work and
// calls its callbacks in interrupt context; the caller picks the next thread.
bool RunDevices() {
    bool any = false;
    sInterruptDepth++;
    for (int i = 0; i < sDeviceCount; i++) {
        if (sDevices[i]() != 0) {
            any = true;
        }
    }
    sInterruptDepth--;
    if (sInterruptDepth == 0) {
        sReschedulePending = false;
    }
    return any;
}

// The current thread can no longer run: switch to the best ready thread.
void Block(const void* key) {
    sCurrent->state = ST_WAITING;
    sCurrent->waitKey = key;
    HostThread* next;
    while ((next = BestReady(NULL)) == NULL) {
        if (!RunDevices()) {
            Fail("deadlock: every thread is blocked");
        }
    }
    SwitchTo(next);
}

// A better-priority thread became ready: let it run now (or at the end of
// the current interrupt).
void Reschedule() {
    if (sInterruptDepth > 0) {
        sReschedulePending = true;
        return;
    }
    HostThread* next = BestReady(sCurrent);
    if (next != NULL && next->prio < sCurrent->prio) {
        SwitchTo(next);
    }
}

void Wake(const void* key) {
    bool any = false;
    for (size_t i = 0; i < sThreads.size(); i++) {
        HostThread* t = sThreads[i];
        if (t->state == ST_WAITING && t->waitKey == key) {
            t->state = ST_READY;
            t->waitKey = NULL;
            any = true;
        }
    }
    if (any) {
        Reschedule();
    }
}

void Trampoline() {
    HostThread* self = sCurrent;
    self->result = self->func(self->arg);
    self->state = ST_DONE;
    Wake(self->os);
    HostThread* next = BestReady(NULL);
    if (next == NULL) {
        Fail("deadlock: a thread exited with every other thread blocked");
    }
    SwitchTo(next); // never resumed
    Fail("a finished thread was resumed");
}

} // namespace

extern "C" {

void PortOSEnterInterrupt(void) {
    Init();
    sInterruptDepth++;
}

void PortOSLeaveInterrupt(void) {
    if (--sInterruptDepth == 0 && sReschedulePending) {
        sReschedulePending = false;
        Reschedule();
    }
}

void PortOSIdle(void) {
    Init();
    if (sCurrent != &sMain) {
        Fail("PortOSIdle: only the main thread idles");
    }
    // The main thread waits; everything ready runs until it blocks, and the
    // devices finish their pending work (which may wake threads again).
    for (;;) {
        HostThread* next = BestReady(&sMain);
        if (next == NULL) {
            if (!RunDevices()) {
                return;
            }
            continue;
        }
        SwitchTo(next);
    }
}

void PortOSAddDevice(PortOSDeviceFunc func) {
    for (int i = 0; i < sDeviceCount; i++) {
        if (sDevices[i] == func) {
            return;
        }
    }
    if (sDeviceCount == MAX_DEVICES) {
        Fail("PortOSAddDevice: too many devices");
    }
    sDevices[sDeviceCount++] = func;
}

/* Threads */
BOOL OSCreateThread(OSThread* thread, OSThreadFunc func, void* funcArg, void* stackBegin,
                    u32 stackSize, s32 prio, u16 flags) {
    Init();
    std::memset(thread, 0, sizeof(*thread));
    thread->priority = prio;
    thread->base = prio;
    thread->flags = flags;
    thread->suspend = 1;
    thread->stackBegin = static_cast<u32*>(stackBegin);
    thread->stackEnd = reinterpret_cast<u32*>(static_cast<u8*>(stackBegin) - stackSize);

    HostThread* t = Find(thread);
    if (t == NULL) {
        t = static_cast<HostThread*>(std::calloc(1, sizeof(HostThread)));
        if (t == NULL) {
            Fail("OSCreateThread: out of memory");
        }
        sThreads.push_back(t);
    } else if (t->state != ST_DONE) {
        Fail("OSCreateThread: the thread is still running");
    }
    std::free(t->stack);
    t->os = thread;
    t->func = func;
    t->arg = funcArg;
    t->result = NULL;
    t->state = ST_SUSPENDED;
    t->waitKey = NULL;
    t->prio = prio;
    t->stack = std::malloc(HOST_STACK_SIZE);
    if (t->stack == NULL || getcontext(&t->ctx) != 0) {
        Fail("OSCreateThread: no host stack");
    }
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = HOST_STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, Trampoline, 0);
    return TRUE;
}

s32 OSResumeThread(OSThread* thread) {
    HostThread* t = Find(thread);
    if (t == NULL) {
        Fail("OSResumeThread: unknown thread");
    }
    s32 old = thread->suspend;
    if (thread->suspend > 0 && --thread->suspend == 0 && t->state == ST_SUSPENDED) {
        t->state = ST_READY;
        Reschedule();
    }
    return old;
}

BOOL OSJoinThread(OSThread* thread, void* val) {
    HostThread* t = Find(thread);
    if (t == NULL) {
        return FALSE;
    }
    while (t->state != ST_DONE) {
        Block(thread);
    }
    if (val != NULL) {
        *static_cast<void**>(val) = t->result;
    }
    return TRUE;
}

OSSwitchThreadCallback OSSetSwitchThreadCallback(OSSwitchThreadCallback callback) {
    OSSwitchThreadCallback old = sSwitchCallback;
    sSwitchCallback = callback;
    return old;
}

// The thread never runs again; whoever joins it gets NULL.
void OSCancelThread(OSThread* thread) {
    HostThread* t = Find(thread);
    if (t == NULL || t->state == ST_DONE) {
        return;
    }
    if (t == sCurrent) {
        Fail("OSCancelThread: a thread cancelling itself");
    }
    t->state = ST_DONE;
    t->result = NULL;
    Wake(thread);
}

// Nothing to release: a finished thread's host stack is reused by the next
// OSCreateThread of the same OSThread.
void OSDetachThread(OSThread* thread) {
    (void)thread;
}

BOOL OSIsThreadTerminated(OSThread* thread) {
    HostThread* t = Find(thread);
    return t == NULL || t->state == ST_DONE;
}

OSThread* OSGetCurrentThread(void) {
    Init();
    return sCurrent->os;
}

void OSInitThreadQueue(OSThreadQueue* queue) {
    queue->head = NULL;
    queue->tail = NULL;
}

void OSSleepThread(OSThreadQueue* queue) {
    Init();
    Block(queue);
}

void OSWakeupThread(OSThreadQueue* queue) {
    Init();
    Wake(queue);
}

void OSYieldThread(void) {
    Init();
    HostThread* next = BestReady(sCurrent);
    if (next != NULL && next->prio <= sCurrent->prio) {
        SwitchTo(next);
    }
}

/* Mutexes (recursive, as on the Wii) */
void OSInitMutex(OSMutex* mutex) {
    std::memset(mutex, 0, sizeof(*mutex));
}

void OSLockMutex(OSMutex* mutex) {
    Init();
    for (;;) {
        if (mutex->thread == NULL) {
            mutex->thread = sCurrent->os;
            mutex->lock = 1;
            return;
        }
        if (mutex->thread == sCurrent->os) {
            mutex->lock++;
            return;
        }
        Block(mutex);
    }
}

BOOL OSTryLockMutex(OSMutex* mutex) {
    Init();
    if (mutex->thread == NULL) {
        mutex->thread = sCurrent->os;
        mutex->lock = 1;
        return TRUE;
    }
    if (mutex->thread == sCurrent->os) {
        mutex->lock++;
        return TRUE;
    }
    return FALSE;
}

void OSUnlockMutex(OSMutex* mutex) {
    Init();
    if (mutex->thread != sCurrent->os) {
        return;
    }
    if (--mutex->lock == 0) {
        mutex->thread = NULL;
        Wake(mutex);
    }
}

/* Message queues */
void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* buffer, s32 capacity) {
    std::memset(queue, 0, sizeof(*queue));
    queue->buffer = buffer;
    queue->capacity = capacity;
}

BOOL OSSendMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    Init();
    while (queue->size >= queue->capacity) {
        if (!(flags & OS_MSG_BLOCKING)) {
            return FALSE;
        }
        Block(&queue->sendQueue);
    }
    queue->buffer[(queue->front + queue->size) % queue->capacity] = mesg;
    queue->size++;
    Wake(&queue->recvQueue);
    return TRUE;
}

BOOL OSJamMessage(OSMessageQueue* queue, OSMessage mesg, u32 flags) {
    Init();
    while (queue->size >= queue->capacity) {
        if (!(flags & OS_MSG_BLOCKING)) {
            return FALSE;
        }
        Block(&queue->sendQueue);
    }
    queue->front = (queue->front + queue->capacity - 1) % queue->capacity;
    queue->buffer[queue->front] = mesg;
    queue->size++;
    Wake(&queue->recvQueue);
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* mesg, u32 flags) {
    Init();
    while (queue->size == 0) {
        if (!(flags & OS_MSG_BLOCKING)) {
            return FALSE;
        }
        Block(&queue->recvQueue);
    }
    if (mesg != NULL) {
        *mesg = queue->buffer[queue->front];
    }
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    Wake(&queue->sendQueue);
    return TRUE;
}

} // extern "C"
