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
#include <vector>

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
std::vector<HostThread*> sThreads; // creation order, sMain first
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

void SwitchTo(HostThread* next) {
    if (next == sCurrent) {
        return;
    }
    HostThread* prev = sCurrent;
    sCurrent = next;
    if (swapcontext(&prev->ctx, &next->ctx) != 0) {
        Fail("swapcontext failed");
    }
}

// The current thread can no longer run: switch to the best ready thread.
void Block(const void* key) {
    sCurrent->state = ST_WAITING;
    sCurrent->waitKey = key;
    HostThread* next = BestReady(NULL);
    if (next == NULL) {
        Fail("deadlock: every thread is blocked");
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
    // The main thread waits; everything ready runs until it blocks.
    for (;;) {
        HostThread* next = BestReady(&sMain);
        if (next == NULL) {
            return;
        }
        SwitchTo(next);
    }
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
        t = new HostThread();
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
