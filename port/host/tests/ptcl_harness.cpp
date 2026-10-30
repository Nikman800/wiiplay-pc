/**
 * nw4r::ef runtime harness (port/docs/HOST_BUILD.md §19).
 *
 * Creates every emitter on the disc as an effect through the real
 * EffectSystem API and steps it with EffectSystem::Calc, linked from the
 * decompiled ef runtime (EffectSystem, Effect, Emitter, ParticleManager,
 * Particle, the emit forms, CreationQueue, ef_animcurve) and the game's own
 * RPSysParticleManager / RPSysEffectMemoryManager. Set-up follows the game:
 *
 *   RPSysSystem / RPSysEffectMgr::createMemoryManager: an
 *     RPSysEffectMemoryManager of 100 effects, 250 emitters, 250 particle
 *     managers, 1500 particles and MAX_GROUP_ID groups, handed to
 *     EffectSystem::SetMemoryManager (which runs EffectSystem::Initialize)
 *   RPSysEffectCreator::setEffectResource: Resource::Add, AddTexture,
 *     RelocateCommand (the cursor and static pairs stay registered; each
 *     scene pair is registered, run, and removed again)
 *   RPSysEffectCreator::createEffect: the group ID from the creator's base
 *     (drawScene << 7), the draw group (0 here) and the emitter's userdata
 *     flags, then EffectSystem::CreateEffect(name, groupID, 0)
 *   RPSysEffectCreator::update: EffectSystem::Calc(groupID, false) once a
 *     frame (the root matrix stays the identity, as RPSysEffect::update sets
 *     it for an effect with no transform)
 *
 * Every emitter of every registered BREFF is created by name, one effect at a
 * time (FindEmitter semantics: the first registered project with that name
 * wins; the E line names the resource that was found). Before each
 * CreateEffect the system random is reseeded (EffectSystem::mRandom, which
 * seeds emitters whose randomSeed is 0; 0x1234, or argv[2]), so each run is
 * deterministic and independent of the order. After the last frame the effect is retired as
 * RPSysEffect::kill does, and the pools must drain back to empty.
 *
 * Lines (tests/test_particles.py):
 *
 *   E    path, emitter index, name, resolved resource (path+offset), group ID
 *        ("FAIL" if CreateEffect returned NULL)
 *   F    frame, active emitters in the effect, the root emitter's tick, then
 *        for the root emitter's resource: live particles, particles new this
 *        frame; then the same two counts over every particle manager
 *   P    at selected frames, every live particle: frame, object ID, its
 *        manager's resource, tick, life, random seed, then as hex bits:
 *        position, velocity, momentum, size, scale, rotate; then the four
 *        colours (layer 0/1 x primary/secondary) as RRGGBBAA
 *   X    after teardown: active effects, emitters, managers, particles
 */
#include <Pack/RPKernel/RPSysEffectDefine.h>
#include <Pack/RPKernel/RPSysEffectMemoryManager.h>
#include <Pack/RPKernel/RPSysEffectMgr.h>

#include <nw4r/ef.h>

#include <port/be.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::ef;

namespace {

const int kFrames = 60;
u32 sSeed = 0x1234; // argv[2] overrides

struct File {
    std::string path;
    u8* pData;
    u32 size;
};

struct Archive {
    std::string path;
    u8* pData;
    EGG::Archive* pArchive;
    std::vector<std::string> pairs; // X for each X.breff / X.breft at the root
};

struct Pair {
    File* pEffect;
    File* pTexture;
    EffectProject* pEffectProject;
    TextureProject* pTextureProject;
};

std::vector<File*> sRegistered;
int sEffects = 0, sFailed = 0, sParticleLines = 0, sLeaks = 0;
int sMaxParticles = 0;

int& Errors() {
    return disc_walk::Errors();
}

bool PrintFrame(int frame) {
    switch (frame) {
    case 1: case 2: case 3: case 4: case 5: case 10: case 11: case 20: case 21:
    case 40: case 41: case 60:
        return true;
    }
    return false;
}

/** "path+off" for a pointer into a registered file. */
std::string Where(const void* p) {
    for (File* f : sRegistered) {
        if (p >= f->pData && p < f->pData + f->size) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s+%x", f->path.c_str(),
                          static_cast<unsigned>(static_cast<const u8*>(p) - f->pData));
            return buf;
        }
    }
    return "?";
}

u32 Bits(f32 v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    return u;
}

File* LoadFile(Archive& a, const std::string& name) {
    EGG::Archive::FileInfo info;
    // RPSysResourceManager::GetFileFromArchive
    u8* p = static_cast<u8*>(a.pArchive->getFile(name.c_str(), &info));
    if (p == NULL) {
        std::fprintf(stderr, "ERROR: %s/%s not found\n", a.path.c_str(), name.c_str());
        Errors()++;
        return NULL;
    }
    File* f = new File;
    f->path = a.path + "/" + name;
    f->pData = p;
    f->size = info.getLength();
    return f;
}

bool MountArchive(const std::string& path, Archive* pOut) {
    s32 bufSize = 0;
    u32 trueSize = 0;
    u8* pFile = disc_walk::LoadFromDVD(("/" + path).c_str(), &bufSize, &trueSize);
    if (pFile == NULL) {
        return false;
    }
    s32 expandSize = 0;
    u8* pExpand = disc_walk::ExpandCompressed(pFile, trueSize, &expandSize);
    delete[] pFile;
    if (!disc_walk::IsU8(pExpand, expandSize)) {
        delete[] pExpand;
        return false;
    }
    pOut->path = path;
    pOut->pData = pExpand;
    pOut->pArchive = EGG::Archive::mount(pExpand, NULL, 4);
    if (pOut->pArchive == NULL) {
        std::fprintf(stderr, "ERROR: mount failed: %s\n", path.c_str());
        Errors()++;
        delete[] pExpand;
        return false;
    }
    ARCHandle handle;
    ARCDir dir;
    ARCDirEntry entry;
    std::vector<std::string> names;
    if (ARCInitHandle(pExpand, &handle) && ARCOpenDir(&handle, "/", &dir)) {
        while (ARCReadDir(&dir, &entry)) {
            if (!entry.isDir) {
                names.push_back(entry.name);
            }
        }
        ARCCloseDir(&dir);
    }
    for (const std::string& n : names) {
        if (n.size() > 6 && n.compare(n.size() - 6, 6, ".breff") == 0) {
            std::string stem = n.substr(0, n.size() - 6);
            for (const std::string& m : names) {
                if (m == stem + ".breft") {
                    pOut->pairs.push_back(stem);
                }
            }
        }
    }
    return true;
}

void UnmountArchive(Archive& a) {
    delete a.pArchive;
    delete[] a.pData;
}

/** RPSysEffectCreator::createEffect's group ID, draw group 0. */
u32 GroupID(u32 drawScene, EmitterResource* pResource) {
    u32 groupID = drawScene << 7;
    const u8* pUser = pResource->GetEmitterDesc()->userdata;
    u16 flags = pUser[2] << 8 | pUser[3];
    if (flags & RPSysEffectDefine::EUserFlag_15) {
        groupID |= (flags & RPSysEffectDefine::EUserFlag_0) ? 0x30 : 0x20;
    } else if (!(flags & RPSysEffectDefine::EUserFlag_0)) {
        groupID |= 0x10;
    }
    return groupID;
}

void PrintParticle(int frame, Particle* p) {
    const ParticleParameter& r = p->mParameter;
    std::printf("P %d %x %s %u %u %u", frame, p->mObjectID,
                Where(p->mParticleManager->mResource).c_str(), p->mTick, p->mLife,
                p->mRandSeed);
    std::printf(" %08x %08x %08x %08x %08x %08x %08x", Bits(r.mPosition.x), Bits(r.mPosition.y),
                Bits(r.mPosition.z), Bits(r.mVelocity.x), Bits(r.mVelocity.y),
                Bits(r.mVelocity.z), Bits(r.mMomentum));
    std::printf(" %08x %08x %08x %08x %08x %08x %08x", Bits(r.mSize.x), Bits(r.mSize.y),
                Bits(r.mScale.x), Bits(r.mScale.y), Bits(r.mRotate.x), Bits(r.mRotate.y),
                Bits(r.mRotate.z));
    for (int i = 0; i < COLOR_LAYER_MAX; i++) {
        for (int j = 0; j < COLOR_IDX_MAX; j++) {
            const GXColor& c = r.mColor[i][j];
            std::printf(" %02x%02x%02x%02x", c.r, c.g, c.b, c.a);
        }
    }
    std::printf("\n");
    sParticleLines++;
}

/** Create one emitter as an effect (RPSysEffect::create) and step it. */
void RunEmitter(const File* f, int index, const char* pName, u32 drawScene) {
    EffectSystem* pSystem = EffectSystem::GetInstance();
    MemoryManagerBase* pMemory = pSystem->GetMemoryManager();

    EmitterResource* pResource = Resource::GetInstance()->_FindEmitter(pName, NULL);
    u32 groupID = pResource != NULL ? GroupID(drawScene, pResource) : 0;

    pSystem->mRandom.Srand(sSeed);
    Effect* pEffect = pSystem->CreateEffect(pName, groupID, 0);
    if (pEffect == NULL) {
        std::printf("E %s %d %s FAIL\n", f->path.c_str(), index, pName);
        std::fprintf(stderr, "ERROR: %s %s: CreateEffect failed\n", f->path.c_str(), pName);
        sFailed++;
        Errors()++;
        return;
    }
    sEffects++;
    Emitter* pRoot = pEffect->GetEmitter(0);
    std::printf("E %s %d %s %s %x\n", f->path.c_str(), index, pName, Where(pResource).c_str(),
                groupID);

    std::set<u32> seen;
    for (int frame = 1; frame <= kFrames; frame++) {
        pSystem->Calc(groupID, false);

        // The root emitter may have retired (it leaves the effect's list).
        u32 rootTick = 0;
        int rootLive = 0, rootNew = 0, allLive = 0, allNew = 0;
        u16 numEmitter = pEffect->GetNumEmitter();
        std::vector<Particle*> particles;
        for (u16 i = 0; i < numEmitter; i++) {
            Emitter* pEmitter = pEffect->GetEmitter(i);
            if (pEmitter == pRoot) {
                rootTick = pEmitter->mTick;
            }
            u16 numManager = pEmitter->GetNumParticleManager();
            for (u16 j = 0; j < numManager; j++) {
                ParticleManager* pManager = pEmitter->GetParticleManager(j);
                NW4R_UT_LIST_FOREACH (Particle, it, pManager->GetParticleList()->mActiveList, {
                    bool fresh = seen.insert(it->mObjectID).second;
                    allLive++;
                    allNew += fresh;
                    if (pManager->mResource == pResource) {
                        rootLive++;
                        rootNew += fresh;
                    }
                    particles.push_back(it);
                });
            }
        }
        if (allLive > sMaxParticles) {
            sMaxParticles = allLive;
        }
        std::printf("F %d %u %u %d %d %d %d\n", frame, numEmitter, rootTick, rootLive, rootNew,
                    allLive, allNew);
        if (PrintFrame(frame)) {
            for (Particle* p : particles) {
                PrintParticle(frame, p);
            }
        }
    }

    // RPSysEffect::kill (RetireEmitterAll, RetireParticleAll), then the
    // effect itself; the next Calc collects what is closing.
    pEffect->RetireEmitterAll();
    pEffect->RetireParticleAll();
    pSystem->RetireEffect(pEffect);
    pSystem->Calc(groupID, false);
    u32 effects = pMemory->GetNumActiveEffect(), emitters = pMemory->GetNumActiveEmitter(),
        managers = pMemory->GetNumActiveParticleManager(),
        particles = pMemory->GetNumActiveParticle();
    std::printf("X %u %u %u %u\n", effects, emitters, managers, particles);
    if (effects + emitters + managers + particles != 0) {
        std::fprintf(stderr, "ERROR: %s %s: pools not empty after teardown\n", f->path.c_str(),
                     pName);
        sLeaks++;
        Errors()++;
    }
}

void RunPair(const Pair& c, u32 drawScene) {
    Resource* r = Resource::GetInstance();
    u32 num = r->NumEmitter(c.pEffectProject);
    for (u32 i = 0; i < num; i++) {
        EmitterResource* pRes = r->_GetEmitterIndexOf(i, c.pEffectProject);
        RunEmitter(c.pEffect, i, pRes->GetName(), drawScene);
    }
}

/** RPSysEffectCreator::setEffectResource */
void SetPair(Pair& c) {
    Resource* r = Resource::GetInstance();
    c.pEffectProject = r->Add(c.pEffect->pData);
    c.pTextureProject = r->AddTexture(c.pTexture->pData);
    sRegistered.push_back(c.pEffect);
    sRegistered.push_back(c.pTexture);
    u32 errors = r->RelocateCommand();
    if (errors != 0) {
        std::fprintf(stderr, "ERROR: %s: %u relocation errors\n", c.pEffect->path.c_str(), errors);
        Errors()++;
    }
}

/** RPSysEffectCreator::resetEffectResource */
void ResetPair(Pair& c) {
    Resource* r = Resource::GetInstance();
    r->RemoveEffectProject(c.pEffectProject);
    r->RemoveTextureProject(c.pTextureProject);
    r->RelocateCommand();
    sRegistered.pop_back();
    sRegistered.pop_back();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [system-random-seed]\n", argv[0]);
        return 2;
    }
    if (argc > 2) {
        sSeed = static_cast<u32>(std::strtoul(argv[2], NULL, 0));
    }
    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    // RPSysSystem's EffectArg and RPSysEffectMgr::createMemoryManager.
    const u16 maxEffect = 100, maxEmitter = 250, maxManager = 250, maxParticle = 1500;
    u32 workSize = RPSysEffectMemoryManager::CalcMemorySize(
        maxEffect, maxEmitter, maxManager, maxParticle, RPSysEffectDefine::MAX_GROUP_ID);
    void* pWork = ut::RoundUp(new u8[workSize + 32], 32);
    RPSysEffectMemoryManager* pMemory = new RPSysEffectMemoryManager(
        pWork, workSize, maxEffect, maxEmitter, maxManager, maxParticle);
    EffectSystem::GetInstance()->SetMemoryManager(pMemory, RPSysEffectDefine::MAX_GROUP_ID);

    const std::string staticArc = "Common/PartyStatic/common.carc";
    Archive boot;
    if (!MountArchive(staticArc, &boot)) {
        std::fprintf(stderr, "ERROR: cannot mount %s\n", staticArc.c_str());
        return 1;
    }
    // RPSysEffectMgr: the cursor creator first, then the static creator.
    Pair cursor = {LoadFile(boot, "nw4r_defcursor_pack.breff"),
                   LoadFile(boot, "nw4r_defcursor_pack.breft"), NULL, NULL};
    Pair statics = {LoadFile(boot, "basic_common.breff"), LoadFile(boot, "basic_common.breft"),
                    NULL, NULL};
    if (cursor.pEffect == NULL || cursor.pTexture == NULL || statics.pEffect == NULL ||
        statics.pTexture == NULL) {
        return 1;
    }
    SetPair(cursor);
    SetPair(statics);
    RunPair(cursor, RPSysEffectMgr::EDrawScene_Cursor);
    RunPair(statics, RPSysEffectMgr::EDrawScene_Static);

    // Every scene pair on the disc (RPSysScene's creator), one at a time.
    std::vector<std::string> paths;
    disc_walk::ListDisc(argv[1], "", &paths);
    for (const std::string& p : paths) {
        if (p == staticArc || p.size() < 5 || p.compare(p.size() - 5, 5, ".carc") != 0) {
            continue;
        }
        Archive a;
        if (!MountArchive(p, &a)) {
            continue;
        }
        for (const std::string& stem : a.pairs) {
            Pair scene = {LoadFile(a, stem + ".breff"), LoadFile(a, stem + ".breft"), NULL, NULL};
            if (scene.pEffect == NULL || scene.pTexture == NULL) {
                continue;
            }
            SetPair(scene);
            RunPair(scene, RPSysEffectMgr::EDrawScene_Scene);
            ResetPair(scene);
            delete scene.pEffect;
            delete scene.pTexture;
        }
        UnmountArchive(a);
    }

    std::fprintf(stderr,
                 "ptcl_harness: %d effects stepped %d frames (%d failed), %d particle lines, "
                 "at most %d live particles, %d pool leaks; %d errors (pointer size %zu)\n",
                 sEffects, kFrames, sFailed, sParticleLines, sMaxParticles, sLeaks, Errors(),
                 sizeof(void*));
    return Errors() == 0 ? 0 : 1;
}
