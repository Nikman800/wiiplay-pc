/**
 * The OS / storage platform layer on the host (port/docs/HOST_BUILD.md §22).
 *
 * Boots the way the game does and runs the game's own resource and save code
 * on the host platform layer (src/host_os.cpp, host_dvd.cpp, host_nand.cpp,
 * host_sc.cpp, host_thread.cpp):
 *
 *   heap  PortOSInit, then EGG::ConfigurationData::initMemory (the decomp's
 *         EGG system code: OSInitAlloc over the MEM1/MEM2 arenas, the MEM1 and
 *         MEM2 root EGG::ExpHeaps, the system heap; RPSysSystem::initialize
 *         does the same with a 0x600000 system heap and leaves the MEM2 root
 *         heap current), RPSysSystem::setup's heaps (the 24 MiB resource
 *         FrmHeap and the assert heap from MEM2, the reserve heap from MEM1)
 *         and a set of allocation checks on the real EGG / MEM heaps
 *   dvd   every file of the disc through dvdfs.c: DVDConvertPathToEntrynum,
 *         DVDOpen / DVDFastOpen, DVDReadPrio in chunks (hash), plus async
 *         reads with callbacks, cancellation and a read from another thread
 *   res   RPSysResourceManager as the game uses it: LoadCacheArchives (every
 *         cached scene's common.carc / local.carc, compressed, into the
 *         resource heap), LoadGameCommonArchive / LoadGameLocalArchive for
 *         every scene (from the cache), LoadMessageArchive,
 *         LoadFontArchive, LoadKokeshiArchive, LoadStaticCommonArchive /
 *         LoadStaticLocalArchive; every mounted archive's files are listed
 *   save  RPSysSaveDataMgr as the game uses it: CreateInstance, initBanner,
 *         existPackFileAsync / createPackFileAsync / saveAsync / loadAsync on
 *         the NAND EGG::TaskThread, finished per frame the way
 *         RPSysScene::updateNandEndMessage does
 *   sc    the SC getters and RPSysSystem::setupLocalSettings
 *
 * Output lines are checked by tests/test_sys.py.
 *
 *   sys_harness --disc FILES --nand DIR [--sc FILE] [--fst PATH | --fst-tree]
 *               [--sections heap,dvd,res,save,sc] [--save-values SEED]
 */

#include <Pack/RPKernel.h>
#include <Pack/RPSystem.h>

#include <egg/core.h>
#include <egg/core/eggDisplay.h>
#include <egg/core/eggSystem.h>
#include <egg/core/eggTaskThread.h>
#include <revolution/ARC.h>
#include <revolution/DVD.h>
#include <revolution/MEM.h>
#include <revolution/NAND.h>
#include <revolution/OS.h>
#include <revolution/SC.h>

#include <port/arena.h>
#include <port/host.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

/******************************************************************************
 * Access to the private members the harness sets up the way the game's own
 * code would (RPSysSystem::setup / createFromSystemHeap). Explicit template
 * instantiation may name private members; nothing in the decomp changes.
 ******************************************************************************/

template <typename Tag, typename Tag::type M> struct Rob {
    friend typename Tag::type Get(Tag) {
        return M;
    }
};

#define ROB(Name, Type, Member)                                                \
    struct Name {                                                              \
        using type = Type;                                                     \
        friend type Get(Name);                                                 \
    };                                                                         \
    template struct Rob<Name, Member>

ROB(SysResourceHeap, EGG::Heap* RPSysSystem::*, &RPSysSystem::mpResourceHeap);
ROB(SysAssertHeap, EGG::Heap* RPSysSystem::*, &RPSysSystem::mpAssertHeap);
ROB(SysReserveHeap, EGG::Heap* RPSysSystem::*, &RPSysSystem::mpReserveHeap);
ROB(SysNandThread, EGG::TaskThread* RPSysSystem::*, &RPSysSystem::mpNandThread);
ROB(SysDvdThread, EGG::TaskThread* RPSysSystem::*, &RPSysSystem::mpDvdThread);
ROB(SysConfig, RPSysConfigData*, &RPSysSystem::sConfigData);
ROB(CfgSystemHeapSize, u32 EGG::ConfigurationData::*, &EGG::ConfigurationData::mSystemHeapSize);
ROB(CfgDisplay, EGG::Display* RPSysConfigData::*, &RPSysConfigData::mDisplay);
ROB(ResLists, nw4r::ut::List (RPSysResourceManager::*)[RPSysResourceManager::EList_Max],
    &RPSysResourceManager::mResourceLists);

/******************************************************************************
 * The scene the save code asks (RPSysSceneMgr::getCurrentSceneRP()->getHeap(),
 * ->isNandAccessEnable()). RPSysSceneMgr / RPSysScene are not linked yet;
 * these stand-ins give the save code an EGG::Scene whose heap is the scene
 * heap and say NAND access is allowed (a scene other than the boot scenes).
 ******************************************************************************/

namespace {
EGG::Scene* sScene = NULL;
}

RPSysSceneMgr* RPSysSceneMgr::spInstance = NULL;
ROB(SceneMgrInstance, RPSysSceneMgr**, &RPSysSceneMgr::spInstance);
// Stands for the scene manager object (only the stand-ins below see it).
alignas(16) static u8 sSceneMgrStorage[sizeof(RPSysSceneMgr)];

RPSysScene* RPSysSceneMgr::getCurrentSceneRP() const {
    return reinterpret_cast<RPSysScene*>(sScene);
}

bool RPSysScene::isNandAccessEnable() const {
    return true;
}

namespace {

/******************************************************************************
 * Helpers
 ******************************************************************************/

int sErrors = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("ERROR %s:%d: ", __FILE__, __LINE__);                  \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
            sErrors++;                                                         \
        }                                                                      \
    } while (0)

u64 Fnv1a(const void* p, u32 n, u64 h = 0xCBF29CE484222325ull) {
    const u8* b = static_cast<const u8*>(p);
    for (u32 i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

u32 V(const void* p) {
    return port_vaddr(p);
}


void PrintHeap(const char* name, EGG::Heap* heap) {
    EGG::Heap* parent = heap->findParentHeap();
    std::printf("H %s kind %d start %08X end %08X size %08X free %08X parent %08X self %08X\n",
                name, static_cast<int>(heap->getHeapKind()), V(heap->getStartAddress()),
                V(heap->getEndAddress()), static_cast<unsigned>(heap->getTotalSize()),
                static_cast<unsigned>(heap->getAllocatableSize(4)),
                parent ? V(parent->getStartAddress()) : 0u, V(heap));
}

/******************************************************************************
 * heap
 ******************************************************************************/

struct Counted : public EGG::Disposer {
    static int sDisposed;
    ~Counted() {
        sDisposed++;
    }
};
int Counted::sDisposed = 0;

void HeapSection() {
    EGG::Heap* mem1 = EGG::BaseSystem::getRootHeapMem1();
    EGG::Heap* mem2 = EGG::BaseSystem::getRootHeapMem2();
    EGG::Heap* sys = EGG::BaseSystem::getSystemHeap();
    RPSysSystem* rp = RPSysSystem::instance();

    std::printf("A mem1 %08X %08X mem2 %08X %08X\n", V(OSGetMEM1ArenaLo()), V(OSGetMEM1ArenaHi()),
                V(OSGetMEM2ArenaLo()), V(OSGetMEM2ArenaHi()));
    PrintHeap("root1", mem1);
    PrintHeap("root2", mem2);
    PrintHeap("system", sys);
    PrintHeap("resource", rp->*Get(SysResourceHeap()));
    if (rp->*Get(SysAssertHeap()) != NULL) {
        PrintHeap("assert", rp->*Get(SysAssertHeap()));
    } else {
        std::printf("H assert none: ExpHeap %u + MEMiHeapHead %u + 4 > 0x100\n",
                    static_cast<unsigned>(sizeof(EGG::ExpHeap)),
                    static_cast<unsigned>(sizeof(MEMiHeapHead)));
    }
    PrintHeap("reserve", rp->*Get(SysReserveHeap()));
    std::printf("C current %08X\n", V(EGG::Heap::getCurrentHeap()));

    // Expanded heap: head / tail allocations with alignments, then frees.
    EGG::ExpHeap* exp = EGG::ExpHeap::create(0x10000, mem2);
    CHECK(exp != NULL, "ExpHeap::create");
    u32 before = exp->getAllocatableSize(4);
    const s32 aligns[] = {4, 8, 16, 32, 64, 128, -4, -32, -128};
    void* blocks[9];
    for (int i = 0; i < 9; i++) {
        blocks[i] = exp->alloc(100 + i * 7, aligns[i]);
        int a = aligns[i] < 0 ? -aligns[i] : aligns[i];
        CHECK(blocks[i] != NULL && V(blocks[i]) % a == 0, "exp alloc %d", i);
        CHECK(EGG::Heap::findContainHeap(blocks[i]) == exp, "findContainHeap %d", i);
        CHECK(exp->isHeapPointer(blocks[i]), "contains %d", i);
        std::printf("X alloc %d align %d at +%05X\n", 100 + i * 7, static_cast<int>(aligns[i]),
                    V(blocks[i]) - V(exp->getStartAddress()));
    }
    for (int i = 0; i < 9; i += 2) {
        EGG::Heap::free(blocks[i], NULL); // through MEMFindContainHeap, as operator delete
    }
    for (int i = 1; i < 9; i += 2) {
        exp->free(blocks[i]);
    }
    CHECK(exp->getAllocatableSize(4) == before, "exp heap not empty after frees");
    std::printf("X free %08X == %08X\n", static_cast<unsigned>(exp->getAllocatableSize(4)),
                static_cast<unsigned>(before));

    // Frame heap: allocations from both ends.
    EGG::FrmHeap* frm = EGG::FrmHeap::create(0x8000, mem2);
    void* h0 = frm->alloc(0x100, 32);
    void* t0 = frm->alloc(0x100, -32);
    void* h1 = frm->alloc(0x10, 4);
    CHECK(V(h1) > V(h0) && V(t0) > V(h1), "frame heap order");
    CHECK(V(t0) + 0x100 <= V(frm->getEndAddress()) &&
              V(frm->getEndAddress()) - (V(t0) + 0x100) < 32,
          "frame heap tail");
    std::printf("F head +%04X +%04X tail +%04X\n", V(h0) - V(frm->getStartAddress()),
                V(h1) - V(frm->getStartAddress()), V(t0) - V(frm->getStartAddress()));
    // FrmHeap::free does nothing (frame heaps are released whole, by destroy).
    u32 frmFree = frm->getAllocatableSize(4);
    frm->free(h0);
    CHECK(frm->getAllocatableSize(4) == frmFree, "frame heap free is a no-op");
    std::printf("F free %08X end +%04X\n", static_cast<unsigned>(frmFree),
                V(frm->getEndAddress()) - V(frm->getStartAddress()));

    // Allocator over a heap (what g3d / lyt / snd get).
    EGG::Allocator allocator(exp, 32);
    void* a0 = allocator.alloc(0x40);
    void* a1 = MEMAllocFromAllocator(&allocator, 0x40);
    CHECK(a0 != NULL && a1 != NULL && V(a0) % 32 == 0 && V(a1) % 32 == 0, "allocator");
    allocator.free(a0);
    MEMFreeToAllocator(&allocator, a1);
    CHECK(exp->getAllocatableSize(4) == before, "allocator frees");

    // operator new on the current heap, placement on a given heap, disposers.
    EGG::Heap* old = exp->becomeCurrentHeap();
    int* p = new int[10];
    CHECK(EGG::Heap::findContainHeap(p) == exp, "operator new[] uses the current heap");
    delete[] p;
    Counted* c = new (frm, 4) Counted;
    CHECK(EGG::Heap::findContainHeap(c) == frm, "placement new on a heap");
    old->becomeCurrentHeap();
    CHECK(exp->getAllocatableSize(4) == before, "delete[] returned the block");
    frm->destroy(); // disposes c (EGG::Heap::dispose), then frees the heap
    CHECK(Counted::sDisposed == 1, "disposer ran on heap destroy (%d)", Counted::sDisposed);
    exp->destroy();
    std::printf("X disposed %d\n", Counted::sDisposed);
}

/******************************************************************************
 * dvd
 ******************************************************************************/

struct AsyncState {
    int calls;
    s32 result;
    int order;
};
int sOrder = 0;
AsyncState sAsync[4];

void AsyncCallback(s32 result, DVDFileInfo* info) {
    AsyncState* st = static_cast<AsyncState*>(info->block.userData);
    st->calls++;
    st->result = result;
    st->order = ++sOrder;
}

OSMessageQueue sDoneQueue;
OSMessage sDoneMsgs[4];

void QueueCallback(s32 result, DVDFileInfo* info) {
    (void)info;
    OSSendMessage(&sDoneQueue, reinterpret_cast<OSMessage>(static_cast<intptr_t>(result)),
                  0);
}

struct ThreadRead {
    const char* path;
    u64 hash;
    s32 size;
};

void* ReaderThread(void* arg) {
    ThreadRead* r = static_cast<ThreadRead*>(arg);
    DVDFileInfo fi;
    if (!DVDOpen(r->path, &fi)) {
        r->size = -1;
        return NULL;
    }
    u32 size = ROUND_UP(fi.size, 32);
    u8* buf = static_cast<u8*>(EGG::Heap::alloc(size, 32, NULL));
    r->size = DVDReadPrio(&fi, buf, static_cast<s32>(size), 0, 2);
    r->hash = Fnv1a(buf, fi.size);
    EGG::Heap::free(buf, NULL);
    DVDClose(&fi);
    return NULL;
}

void DvdSection(const char* listPath) {
    char dir[128];
    DVDGetCurrentDir(dir, sizeof(dir));
    std::printf("D cwd %s\n", dir);
    int fromTree = 0;
    u32 fstSize = 0;
    PortDVDGetFst(&fstSize, &fromTree);
    std::printf("D fst %u bytes, %s\n", static_cast<unsigned>(fstSize),
                fromTree ? "built from the tree" : "sys/fst.bin");

    // Every path in the list (written by the test from the extracted tree).
    FILE* list = std::fopen(listPath, "r");
    CHECK(list != NULL, "cannot open %s", listPath);
    if (list == NULL) {
        return;
    }
    const u32 chunk = 0x8000;
    u8* buf = static_cast<u8*>(EGG::Heap::alloc(chunk, 32, NULL));
    char line[512];
    while (std::fgets(line, sizeof(line), list) != NULL) {
        line[std::strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        bool isDir = line[std::strlen(line) - 1] == '/';
        s32 entry = DVDConvertPathToEntrynum(line);
        if (isDir) {
            DVDFileInfo fi;
            std::printf("E %s %d %d\n", line, static_cast<int>(entry),
                        DVDFastOpen(entry, &fi) ? 1 : 0);
            continue;
        }
        DVDFileInfo fi;
        BOOL opened = DVDOpen(line, &fi);
        BOOL fast = FALSE;
        DVDFileInfo ff;
        fast = DVDFastOpen(entry, &ff);
        CHECK(opened && fast && ff.offset == fi.offset && ff.size == fi.size, "open %s", line);
        if (!opened) {
            std::printf("E %s %d -1 0\n", line, static_cast<int>(entry));
            continue;
        }
        u64 h = 0xCBF29CE484222325ull;
        u32 done = 0;
        while (done < fi.size) {
            u32 want = fi.size - done < chunk ? ROUND_UP(fi.size - done, 32) : chunk;
            s32 got = DVDReadPrio(&fi, buf, static_cast<s32>(want), static_cast<s32>(done), 2);
            CHECK(got == static_cast<s32>(want), "read %s at %u: %d", line,
                  static_cast<unsigned>(done), static_cast<int>(got));
            u32 n = fi.size - done < want ? fi.size - done : want;
            h = Fnv1a(buf, n, h);
            done += n;
            if (got <= 0) {
                break;
            }
        }
        DVDClose(&fi);
        std::printf("E %s %d %u %016llX\n", line, static_cast<int>(entry),
                    static_cast<unsigned>(fi.size), static_cast<unsigned long long>(h));
    }
    std::fclose(list);

    // Lookups that must fail or are case-insensitive / relative.
    std::printf("L %d %d %d %d\n", static_cast<int>(DVDConvertPathToEntrynum("/no/such/file")),
                static_cast<int>(DVDConvertPathToEntrynum("COMMON/kokeshi/COMMON.CARC")),
                static_cast<int>(DVDConvertPathToEntrynum("Common/Kokeshi/common.carc")),
                static_cast<int>(DVDConvertPathToEntrynum("/Common/../Common/Kokeshi/common.carc")));

    // Async: callbacks come when the CPU waits, in issue order (same priority).
    const char* path = "gameConfig.ini";
    DVDFileInfo fa[3];
    for (int i = 0; i < 3; i++) {
        std::memset(&sAsync[i], 0, sizeof(sAsync[i]));
        CHECK(DVDOpen(path, &fa[i]), "open %s", path);
        fa[i].block.userData = &sAsync[i];
    }
    u8* abuf = static_cast<u8*>(EGG::Heap::alloc(0x40 * 3, 32, NULL));
    DVDReadAsyncPrio(&fa[0], abuf, 0x20, 0, AsyncCallback, 2);
    DVDReadAsyncPrio(&fa[1], abuf + 0x40, 0x20, 0x20, AsyncCallback, 2);
    DVDReadAsyncPrio(&fa[2], abuf + 0x80, 0x20, 0, AsyncCallback, 2);
    s32 s0 = DVDGetCommandBlockStatus(&fa[0].block);
    int before = sAsync[0].calls + sAsync[1].calls + sAsync[2].calls;
    DVDCancel(&fa[2].block); // still queued: cancelled, callback with DVD_RESULT_CANCELED
    PortOSIdle();            // the drive runs
    std::printf("Q status-before %d calls-before %d | %d %d %d | %d %d %d | %d %d %d | %d\n",
                static_cast<int>(s0), before, sAsync[0].calls, static_cast<int>(sAsync[0].result),
                sAsync[0].order, sAsync[1].calls, static_cast<int>(sAsync[1].result),
                sAsync[1].order, sAsync[2].calls, static_cast<int>(sAsync[2].result),
                sAsync[2].order, static_cast<int>(DVDGetCommandBlockStatus(&fa[0].block)));
    std::printf("Q bytes %016llX %016llX\n", static_cast<unsigned long long>(Fnv1a(abuf, 0x20)),
                static_cast<unsigned long long>(Fnv1a(abuf + 0x40, 0x20)));
    EGG::Heap::free(abuf, NULL);

    // A blocking wait on a message queue (what EGG::DvdFile::sync does): the
    // main thread blocks, the drive runs, the callback's message wakes it.
    OSInitMessageQueue(&sDoneQueue, sDoneMsgs, 4);
    DVDFileInfo fq;
    DVDOpen(path, &fq);
    u8* qbuf = static_cast<u8*>(EGG::Heap::alloc(0x20, 32, NULL));
    DVDReadAsyncPrio(&fq, qbuf, 0x20, 0, QueueCallback, 2);
    OSMessage msg;
    OSReceiveMessage(&sDoneQueue, &msg, OS_MSG_BLOCKING);
    std::printf("W result %d hash %016llX\n", static_cast<int>(reinterpret_cast<intptr_t>(msg)),
                static_cast<unsigned long long>(Fnv1a(qbuf, 0x20)));
    EGG::Heap::free(qbuf, NULL);

    // A read from another thread while the main thread idles.
    static OSThread thread;
    static u8 stack[0x4000];
    ThreadRead r = {"banner.tpl", 0, 0};
    OSCreateThread(&thread, ReaderThread, &r, stack + sizeof(stack), sizeof(stack), 20, 0);
    OSResumeThread(&thread);
    PortOSIdle();
    std::printf("T %s %d %016llX done %d\n", r.path, static_cast<int>(r.size),
                static_cast<unsigned long long>(r.hash), OSIsThreadTerminated(&thread) ? 1 : 0);

    EGG::Heap::free(buf, NULL);
}

/******************************************************************************
 * res
 ******************************************************************************/

void ListArchive(ARCHandle* h, const char* dir, const char* label) {
    ARCDir d;
    if (!ARCOpenDir(h, dir, &d)) {
        CHECK(false, "ARCOpenDir %s", dir);
        return;
    }
    ARCDirEntry e;
    while (ARCReadDir(&d, &e)) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s%s%s", dir, std::strcmp(dir, "/") == 0 ? "" : "/",
                      e.name);
        if (e.isDir) {
            ListArchive(h, path, label);
            continue;
        }
        ARCFileInfo fi;
        if (!ARCFastOpen(h, static_cast<s32>(e.entryNum), &fi)) {
            CHECK(false, "ARCFastOpen %s", path);
            continue;
        }
        std::printf("R %s %s %u %016llX\n", label, path + 1,
                    static_cast<unsigned>(ARCGetLength(&fi)),
                    static_cast<unsigned long long>(
                        Fnv1a(ARCGetStartAddrInMem(&fi), ARCGetLength(&fi))));
        ARCClose(&fi);
    }
    ARCCloseDir(&d);
}

// Prints the resource manager's files of `list` (only those in `heap` if
// given; none if `tag` is NULL) and, for the expanded ones, every file of the
// archive. Returns the number of files in the list.
int PrintFiles(int list, const char* tag, EGG::Heap* heap) {
    RPSysResourceManager* rm = RPSysResourceManager::instance();
    nw4r::ut::List* lists = rm->*Get(ResLists());
    int n = 0;
    for (RPSysFile* f = static_cast<RPSysFile*>(nw4r::ut::List_GetFirst(&lists[list])); f != NULL;
         f = static_cast<RPSysFile*>(nw4r::ut::List_GetNext(&lists[list], f))) {
        n++;
        if (tag == NULL ||
            (heap != NULL && !heap->isHeapPointer(const_cast<u8*>(f->GetData())))) {
            continue;
        }
        std::printf("%s %s %d %016llX\n", tag, f->GetPath(), static_cast<int>(f->GetSize()),
                    static_cast<unsigned long long>(Fnv1a(f->GetData(), f->GetSize())));
        if (list == RPSysResourceManager::EList_FileList) {
            ARCHandle h;
            if (ARCInitHandle(const_cast<u8*>(f->GetData()), &h)) {
                ListArchive(&h, "/", f->GetPath());
            } else {
                CHECK(false, "not an archive: %s", f->GetPath());
            }
        }
    }
    return n;
}

void ResSection() {
    RPSysResourceManager* rm = RPSysResourceManager::instance();
    EGG::Heap* resHeap = RPSysSystem::instance()->getResourceHeap();

    // RPSysSystem::setup -> the boot scene loads the cached scene archives.
    rm->LoadCacheArchives();
    rm->LoadMessageArchive();
    rm->LoadFontArchive();
    rm->LoadKokeshiArchive();
    EGG::Archive* sc = rm->LoadStaticCommonArchive("PartyStatic/");
    EGG::Archive* sl = rm->LoadStaticLocalArchive("PartyStatic/");
    CHECK(sc != NULL && sl != NULL, "static archives");

    PrintFiles(RPSysResourceManager::EList_FileCache, "FC", NULL);
    int statics = PrintFiles(RPSysResourceManager::EList_FileList, "FL", NULL);

    // Each scene's archives, as a scene's loader does: on the scene's heap
    // (current while the scene is built), from the cache when the scene is a
    // cached one. Destroying the heap at the scene's end disposes its files.
    static const RPSysSceneCreator::ESceneID kScenes[] = {
        RPSysSceneCreator::ESceneID_RPFshScene, RPSysSceneCreator::ESceneID_RPHkyScene,
        RPSysSceneCreator::ESceneID_RPDucScene, RPSysSceneCreator::ESceneID_RPPnpScene,
        RPSysSceneCreator::ESceneID_RPBilScene, RPSysSceneCreator::ESceneID_RPCowScene,
        RPSysSceneCreator::ESceneID_RPWlyScene, RPSysSceneCreator::ESceneID_RPTnkScene,
        RPSysSceneCreator::ESceneID_RPBomScene, RPSysSceneCreator::ESceneID_RPPartyPackTitleScene,
        RPSysSceneCreator::ESceneID_RPPartyMainScene,
    };
    for (size_t i = 0; i < sizeof(kScenes) / sizeof(kScenes[0]); i++) {
        EGG::ExpHeap* sceneHeap =
            EGG::ExpHeap::create(0x1400000, EGG::BaseSystem::getRootHeapMem2());
        CHECK(sceneHeap != NULL, "scene heap");
        EGG::Heap* old = sceneHeap->becomeCurrentHeap();
        EGG::Archive* c = RPSysResourceManager::LoadGameCommonArchive(kScenes[i], sceneHeap);
        EGG::Archive* l = RPSysResourceManager::LoadGameLocalArchive(kScenes[i]);
        CHECK(c != NULL && l != NULL, "scene %d archives", static_cast<int>(kScenes[i]));
        std::printf("S %d %s\n", static_cast<int>(kScenes[i]),
                    RPSysSceneCreator::instance()->getResDirName(kScenes[i]));
        PrintFiles(RPSysResourceManager::EList_FileList, "FL", sceneHeap);
        old->becomeCurrentHeap();
        sceneHeap->destroy();
        int left = PrintFiles(RPSysResourceManager::EList_FileList, NULL, NULL);
        CHECK(left == statics, "scene %d files not disposed (%d, %d)",
              static_cast<int>(kScenes[i]), left, statics);
    }

    // An archive's file through the game's lookup.
    u32 size = 0;
    void* msg = RPSysResourceManager::GetMessageResource("party_message.bmg");
    RPSysResourceManager::GetFileFromArchive(rm->GetMessageArchive(), "party_message.bmg", &size);
    std::printf("G party_message.bmg %u %016llX\n", static_cast<unsigned>(size),
                static_cast<unsigned long long>(msg ? Fnv1a(msg, size) : 0));
    std::printf("X exists %d %d\n", rm->IsExist("Common/Kokeshi/common.carc"),
                rm->IsExist("Common/Kokeshi/none.carc"));

    PrintHeap("resource", resHeap);
}

/******************************************************************************
 * save
 ******************************************************************************/

// The per-frame part of the game loop that drives the save code
// (RPSysScene::updateNandEndMessage; the home menu and pause checks are false).
void Frame() {
    PortOSIdle(); // the retrace wait: the NAND thread runs, NAND callbacks arrive
    RPSysSaveDataMgr* mgr = RPSysSaveDataMgr::instance();
    if (!mgr->isNandAccessFinish()) {
        BOOL success = FALSE;
        EGG::TaskThread* t = RPSysSystem::instance()->getNandThread();
        OSMessage msg = t->waitQueueMessage(t->getEndMessageQueue(), &success);
        if (success && msg == RPSysSystem::instance()->getNandEndMessage()) {
            mgr->finishNandAccess();
        }
    }
    mgr->update(false);
}

int WaitAsync(const char* what) {
    RPSysSaveDataMgr* mgr = RPSysSaveDataMgr::instance();
    int frames = 0;
    do {
        Frame();
        frames++;
    } while (!mgr->isAsyncFinish() && frames < 100);
    std::printf("N %s frames %d error %d code %d exist %d memory %d\n", what, frames,
                mgr->isErrorOccured() ? 1 : 0, static_cast<int>(mgr->getErrorCode()),
                mgr->isPackFileExist() ? 1 : 0, mgr->isNandMemoryExist() ? 1 : 0);
    return frames;
}

void FillSave(u32 seed) {
    RPSysSaveDataMgr* mgr = RPSysSaveDataMgr::instance();
    u32 x = seed;
    auto next = [&x]() {
        x = x * 1103515245u + 12345u;
        return x >> 8;
    };
    RPPartySystemData sys = mgr->getPartySystemData();
    for (int g = 0; g < RPPartySystemData::EGame_Max; g++) {
        sys.setGameOpen(g, (next() & 1) != 0);
    }
    sys.setRegistTodayCount(static_cast<u8>(next()));
    sys.setRegistLastDate(static_cast<RPTime16>(next()));
    mgr->setPartySystemData(sys);
    for (int i = 0; i < 4; i++) {
        RPPartyPlayerData p = mgr->getPartyPlayerData(i);
        p.setRegistered(true);
        RFLCreateID id;
        for (int k = 0; k < RFL_CREATEID_LEN; k++) {
            id.data[k] = static_cast<u8>(next());
        }
        p.setCreateID(&id);
        p.setDebutTime(static_cast<RPTime32>(next()));
        for (int g = 0; g < RPPartyPlayerData::EGame_Max; g++) {
            for (int k = 0; k < RPPartyPlayerData::MY_RECORDS_LENGTH; k++) {
                p.setMyRecord(static_cast<s32>(next()) - 0x400000,
                              static_cast<RPPartyPlayerData::EGame>(g), k);
            }
            p.setMedal(static_cast<u8>(next() % 4), static_cast<RPPartyPlayerData::EGame>(g));
        }
        mgr->setPartyPlayerData(p, i);
    }
}

void PrintSave(const char* tag) {
    RPSysSaveDataMgr* mgr = RPSysSaveDataMgr::instance();
    RPPartySystemData sys = mgr->getPartySystemData();
    std::printf("%s sys today %u date %u open", tag, static_cast<unsigned>(sys.getRegistTodayCount()),
                static_cast<unsigned>(sys.getRegistLastDate()));
    for (int g = 0; g < RPPartySystemData::EGame_Max; g++) {
        std::printf(" %d", sys.isGameOpen(g) ? 1 : 0);
    }
    std::printf(" broken %d\n", mgr->isSaveFileBroken() ? 1 : 0);
    for (int i = 0; i < 4; i++) {
        RPPartyPlayerData p = mgr->getPartyPlayerData(i);
        RFLCreateID id;
        p.getCreateID(&id);
        std::printf("%s player %d reg %d debut %u id", tag, i, p.isRegistered() ? 1 : 0,
                    static_cast<unsigned>(p.getDebutTime()));
        for (int k = 0; k < RFL_CREATEID_LEN; k++) {
            std::printf("%02X", id.data[k]);
        }
        for (int g = 0; g < RPPartyPlayerData::EGame_Max; g++) {
            std::printf(" %d:%d", static_cast<int>(p.getMyRecord(static_cast<RPPartyPlayerData::EGame>(g), 0)),
                        p.getMedal(static_cast<RPPartyPlayerData::EGame>(g)));
        }
        std::printf("\n");
    }
}

void SaveSection(u32 seed) {
    // RPSysSystem::setup: the save manager on the MEM2 root heap.
    RPSysSaveDataMgr::CreateInstance(EGG::BaseSystem::getRootHeapMem2());
    RPSysSaveDataMgr* mgr = RPSysSaveDataMgr::instance();
    std::printf("V savefile %u banner %u blocks %u files %u\n",
                static_cast<unsigned>(RPSysSaveData::getSaveFileSize()),
                static_cast<unsigned>(NAND_BANNER_SIZE(1)),
                static_cast<unsigned>(mgr->getUserBlockSize()),
                static_cast<unsigned>(mgr->getSaveFileNum()));

    // Boot (RPSysResourceManager::LoadStaticArchives) loads the message
    // archive; the host swaps the BMG text once (§6).
    RPSysResourceManager* rm = RPSysResourceManager::instance();
    if (rm->GetMessageArchive() == NULL) {
        rm->LoadMessageArchive();
    }
    PortSwapBmgText(RPSysResourceManager::GetMessageResource("party_message.bmg"));

    // The title scene: banner, then the boot-time save checks and loads.
    mgr->initBanner();
    mgr->existPackFileAsync();
    WaitAsync("exist");
    if (!mgr->isPackFileExist()) {
        mgr->existNandMemoryAsync();
        WaitAsync("memory");
        mgr->createPackFileAsync();
        WaitAsync("create");
    }
    mgr->loadAsync();
    WaitAsync("load");
    PrintSave("P0");

    // Play: change the data, save.
    FillSave(seed);
    PrintSave("P1");
    mgr->saveAsync();
    WaitAsync("save");

    // Read it back over reset data.
    mgr->setPartySystemData(RPPartySystemData());
    for (int i = 0; i < 4; i++) {
        mgr->setPartyPlayerData(RPPartyPlayerData(), i);
    }
    PrintSave("PR");
    mgr->loadAsync();
    WaitAsync("reload");
    PrintSave("P2");
}

/******************************************************************************
 * sc
 ******************************************************************************/

void ScSection() {
    SCIdleModeInfo idle;
    SCGetIdleMode(&idle);
    std::printf("SC lang %u aspect %u prog %u eurgb60 %u sound %u bar %u dpd %u motor %u spk %u "
                "saver %u offset %d\n",
                SCGetLanguage(), SCGetAspectRatio(), SCGetProgressiveMode(), SCGetEuRgb60Mode(),
                SCGetSoundMode(), SCGetWpadSensorBarPosition(),
                static_cast<unsigned>(SCGetBtDpdSensibility()), SCGetWpadMotorMode(),
                SCGetWpadSpeakerVolume(), SCGetScreenSaverMode(), SCGetDisplayOffsetH());
    RPSysSystem::instance()->setupLocalSettings();
    RPSysProjectLocal* local = RPSysProjectLocal::instance();
    char dir[32] = "";
    local->appendLocalDirectory(dir);
    std::printf("SC locale %d language %d dir %s\n", static_cast<int>(local->getLocale()),
                static_cast<int>(local->getLanguage()), dir);
}

bool Has(const char* list, const char* name) {
    size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)) != NULL; p += n) {
        if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == '\0')) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const char* disc = NULL;
    const char* nand = NULL;
    const char* sc = NULL;
    const char* fst = NULL;
    const char* list = NULL;
    const char* sections = "heap,dvd,res,save,sc";
    u32 seed = 1;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--disc") == 0 && i + 1 < argc) {
            disc = argv[++i];
        } else if (std::strcmp(argv[i], "--nand") == 0 && i + 1 < argc) {
            nand = argv[++i];
        } else if (std::strcmp(argv[i], "--sc") == 0 && i + 1 < argc) {
            sc = argv[++i];
        } else if (std::strcmp(argv[i], "--fst") == 0 && i + 1 < argc) {
            fst = argv[++i];
        } else if (std::strcmp(argv[i], "--fst-tree") == 0) {
            fst = "";
        } else if (std::strcmp(argv[i], "--list") == 0 && i + 1 < argc) {
            list = argv[++i];
        } else if (std::strcmp(argv[i], "--sections") == 0 && i + 1 < argc) {
            sections = argv[++i];
        } else if (std::strcmp(argv[i], "--save-values") == 0 && i + 1 < argc) {
            seed = static_cast<u32>(std::strtoul(argv[++i], NULL, 0));
        } else {
            std::fprintf(stderr, "usage: %s --disc FILES --nand DIR [--sc FILE] [--fst PATH | "
                                 "--fst-tree] [--list PATHS] [--sections LIST] "
                                 "[--save-values SEED]\n",
                         argv[0]);
            return 2;
        }
    }
    if (disc == NULL || nand == NULL) {
        std::fprintf(stderr, "--disc and --nand are required\n");
        return 2;
    }

    // What the IPL / apploader / OSInit leave behind, then the platform.
    PortOSInit();
    PortDVDSetDisc(disc, fst);
    if (PortNANDSetRoot(nand) != 0) {
        std::fprintf(stderr, "cannot use %s as the NAND\n", nand);
        return 2;
    }
    if (PortSCLoadConfig(sc) != 0) {
        std::fprintf(stderr, "bad SC config %s\n", sc);
        return 2;
    }

    // RPSysSystem::initialize (memory part) on the game's own configuration
    // object: EGG::BaseSystem::configure, DVDInit, then the heaps through
    // EGG::ConfigurationData::initMemory (RPSysConfigData does not override
    // it; RPSysSystem::initialize repeats its lines with a 0x600000 system
    // heap, and leaves the MEM2 root heap current).
    RPSysConfigData* config = Get(SysConfig());
    EGG::BaseSystem::configure(config);
    DVDInit();
    NANDInit();
    SCInit();
    config->*Get(CfgSystemHeapSize()) = 0x600000;
    config->initMemory();
    // RPSysSystem::initialize, further on: the EGG thread list (with the main
    // thread wrapped) and the DVD file list.
    EGG::Thread::initialize();
    new EGG::Thread(OSGetCurrentThread(), 4);
    EGG::DvdFile::initialize();
    EGG::BaseSystem::getRootHeapMem2()->becomeCurrentHeap();
    // No display yet (Phase 7's video layer): RPSysSystem's constructor only
    // sets its frame rate, so it gets a zeroed stand-in.
    alignas(16) static u8 displayStorage[sizeof(EGG::Display)];
    config->*Get(CfgDisplay()) = reinterpret_cast<EGG::Display*>(displayStorage);

    // RPSysSystem::create / setup / createFromSystemHeap: the parts the
    // resource and save code read.
    RPSysSystem::create();
    RPSysSystem* rp = RPSysSystem::instance();
    rp->*Get(SysResourceHeap()) = EGG::FrmHeap::create(0x1800000, EGG::BaseSystem::getRootHeapMem2());
    // The assert heap (0x100 bytes) holds the EGG::ExpHeap object and the MEM
    // expanded-heap header; with 64-bit pointers they do not fit (§22), and
    // create returns NULL there.
    rp->*Get(SysAssertHeap()) = EGG::ExpHeap::create(0x100, EGG::BaseSystem::getRootHeapMem2());
    if (rp->*Get(SysAssertHeap()) != NULL) {
        (rp->*Get(SysAssertHeap()))->disableAllocation();
    }
    rp->*Get(SysReserveHeap()) = EGG::ExpHeap::create(0x80000, EGG::BaseSystem::getRootHeapMem1());
    {
        EGG::Heap* old = EGG::BaseSystem::getSystemHeap()->becomeCurrentHeap();
        RPSysSceneCreator::CreateInstance(EGG::BaseSystem::getSystemHeap());
        RPSysProjectLocal::CreateInstance(EGG::BaseSystem::getSystemHeap());
        rp->setupLocalSettings();
        RPSysResourceManager::CreateInstance();
        rp->*Get(SysNandThread()) = EGG::TaskThread::create(64, 18, 0x4000, NULL);
        rp->*Get(SysDvdThread()) = EGG::TaskThread::create(64, 19, 0x4000, NULL);
        OSMessageQueue* queue = EGG::TaskThread::createTaskEndMessageQueue(64, NULL);
        (rp->*Get(SysNandThread()))->setEndMessageQueue(queue);
        (rp->*Get(SysDvdThread()))->setEndMessageQueue(queue);
        old->becomeCurrentHeap();
    }
    // The current scene's heap (the save code allocates its banner work there).
    {
        EGG::ExpHeap* heap = EGG::ExpHeap::create(0x200000, EGG::BaseSystem::getRootHeapMem2());
        EGG::Heap* old = heap->becomeCurrentHeap();
        sScene = new EGG::Scene();
        old->becomeCurrentHeap();
        *Get(SceneMgrInstance()) = reinterpret_cast<RPSysSceneMgr*>(sSceneMgrStorage);
    }

    if (Has(sections, "heap")) {
        HeapSection();
    }
    if (Has(sections, "dvd")) {
        if (list == NULL) {
            std::fprintf(stderr, "the dvd section needs --list\n");
            return 2;
        }
        DvdSection(list);
    }
    if (Has(sections, "res")) {
        ResSection();
    }
    if (Has(sections, "save")) {
        SaveSection(seed);
    }
    if (Has(sections, "sc")) {
        ScSection();
    }
    std::printf("sys_harness: %d errors (pointer size %d)\n", sErrors,
                static_cast<int>(sizeof(void*)));
    return sErrors == 0 ? 0 : 1;
}
