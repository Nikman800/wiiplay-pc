/**
 * nw4r::ef resource harness (port/docs/HOST_BUILD.md §12, POINTERS.md step 6).
 *
 * Registers every BREFF/BREFT pair on the disc with the real decompiled
 * nw4r::ef::Resource (src/nw4r/ef/ef_resource.cpp), in the order and with the
 * calls the game uses:
 *
 *   RPSysEffectCreator::setEffectResource: Resource::Add, AddTexture,
 *     RelocateCommand
 *   RPSysEffectCreator::resetEffectResource: RemoveEffectProject,
 *     RemoveTextureProject, RelocateCommand
 *
 * Boot (RPSysEffectMgr, Party Pack): the cursor creator gets
 * nw4r_defcursor_pack, then the static creator basic_common, both from
 * Common/PartyStatic/common.carc. Then, for every other archive with an
 * X.breff / X.breft pair at its root (the scene archives: RPSysScene's
 * creator), the pair is set while the two boot pairs stay registered (three
 * projects linked in each list), dumped, and reset again. Files are used in
 * place inside the mounted archive, as RPSysResourceManager::GetFileFromArchive
 * hands them to the game.
 *
 * Lines (tests/test_effects.py builds the same from
 * port/assets/tools/fmt_effect.py and its own simulation of the relocation):
 *
 *   EF   BREFF registered: path, emitter count (Resource::NumEmitter)
 *   E    emitter i: EmitterResource offset, GetName(), the index FindEmitter
 *        returns for that name, particle/emit track counts, EmitterDesc and
 *        ParticleParameterDesc offsets
 *   ED   EmitterDesc fields read through the be_* members
 *   PD   ParticleParameterDesc texture names and fields
 *   K    every particle and emit track (GetPtclTrack / GetEmitTrack /
 *        ResEmitter::GetPtclTrack): offset, AnimCurveHeader fields
 *   FP   every field parameter block of a particle F32 track (gravity,
 *        random, spin, magnet, newton, vortex), located and read as
 *        ParticleManager::Calc does: offset, kind, every member
 *   TF   BREFT registered: path, texture count in its name table
 *   T    texture: FindTexture result offset and TextureData fields; the
 *        texture/tlut slots read back through RES_PTR as file offsets
 *   STAGE state after each RelocateCommand: its error count, NumEmitter(NULL),
 *        mNumTexture, the BREFF and BREFT lists walked through RES_LIST_*
 *   FILE every registered file: emulated address of its first byte
 *   W    every run of bytes that differs from the file on the disc: offset and
 *        the new bytes. The test predicts every run (and so the whole image
 *        the Wii holds for a file at the same emulated address).
 *   CAP  RPSysEffectMgr::setCaptureEfb on every active creator after each
 *        scene is set (ef-5): the dummy_tex slot holds the heap buffer's
 *        emulated address; the reset stage's W lines include those writes
 *
 * With --curves (HOST_BUILD.md §17) it also evaluates every particle and emit
 * track right after its file is registered, through the real
 * ef_animcurve.cpp (AnimCurveExecuteU8 / F32 / Rotate / Texture / Child) at
 * a set of ticks, seeds and lifetimes (tests/test_efcurves.py):
 *
 *   C    path, emitter, track, seed, life, tick, then the result: U8 target
 *        bytes, F32 / Rotate target words, the texture curve's three layer
 *        textures (TextureData name) with the wrap / reverse bits, or the
 *        child curve's creation requests (type, EmitterInheritSetting bytes
 *        and speed, emitter name, calcRemain)
 */
#include <nw4r/ef.h>

#include <port/arena.h>
#include <port/be.h>
#include <port/reslist.h>

#include "disc_walk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nw4r;
using namespace nw4r::ef;

namespace {

struct File {
    std::string path;
    u8* pData;
    u32 size;
    std::vector<u8> orig;
    void* pProject; // EffectProject* / TextureProject* once registered
};

struct Archive {
    std::string path;
    u8* pData; // expanded archive (kept alive while its files are registered)
    EGG::Archive* pArchive;
    std::vector<std::string> pairs; // X for each X.breff / X.breft at the root
};

struct Creator {
    File* pEffect;
    File* pTexture;
    EffectProject* pEffectProject;
    TextureProject* pTextureProject;
};

std::vector<File*> sRegistered;
int sBreff = 0, sBreft = 0, sEmitters = 0, sTracks = 0, sTextures = 0, sFields = 0;
int sStages = 0, sRuns = 0, sCaptures = 0, sTextureSlots = 0, sCurveSlots = 0;
int sForeignSlots = 0; // cross-file slots resolved into another project pair
bool sCurves = false;  // --curves
int sCurveEvals = 0, sChildCount = 0;

/** CreationQueue requests a child curve makes (the queue itself is not linked). */
struct ChildRequest {
    int type; // 0 particle, 1 emitter
    u8 setting[8];
    s16 speed;
    const EmitterResource* pResource;
    u16 calcRemain;
};
std::vector<ChildRequest> sChildRequests;

int& Errors() {
    return disc_walk::Errors();
}

File* FindFileOf(const void* p) {
    for (File* f : sRegistered) {
        if (p >= f->pData && p < f->pData + f->size) {
            return f;
        }
    }
    return NULL;
}

/** "path+off" for a host pointer into a registered file. */
std::string Where(const void* p) {
    if (p == NULL) {
        return "0";
    }
    File* f = FindFileOf(p);
    char buf[512];
    if (f == NULL) {
        std::snprintf(buf, sizeof buf, "?%08x", port_vaddr(p));
    } else {
        std::snprintf(buf, sizeof buf, "%s+%x", f->path.c_str(),
                      static_cast<unsigned>(static_cast<const u8*>(p) - f->pData));
    }
    return buf;
}

u32 Off(const File* f, const void* p) {
    return static_cast<u32>(static_cast<const u8*>(p) - f->pData);
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
    f->orig.assign(p, p + f->size);
    f->pProject = NULL;
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

    // Root files X.breff with a matching X.breft.
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
    a.pArchive = NULL;
    a.pData = NULL;
}

/******************************************************************************
 * Structure dumps (once per file, right after registration)
 ******************************************************************************/
void DumpTrack(const File* f, int i, char kind, int j, u8* pTrack) {
    AnimCurveHeader* h = reinterpret_cast<AnimCurveHeader*>(pTrack);
    if (FindFileOf(pTrack) != f) {
        std::fprintf(stderr, "ERROR: %s emitter %d track %c%d points outside its file\n",
                     f->path.c_str(), i, kind, j);
        Errors()++;
        return;
    }
    std::printf("K %s %d %c%d %x %02x %u %u %u %u %u %u %x %x %x %x %x\n", f->path.c_str(), i,
                kind, j, Off(f, pTrack), h->magic, h->kindType, h->curveFlag, h->kindEnable,
                h->processFlag, h->loopCount, static_cast<u16>(h->frameLength),
                static_cast<u32>(h->keyTable), static_cast<u32>(h->rangeTable),
                static_cast<u32>(h->randomTable), static_cast<u32>(h->nameTable),
                static_cast<u32>(h->infoTable));
    sTracks++;
}

/******************************************************************************
 * Field parameter blocks (FP lines): read the way ParticleManager::Calc reads
 * them, located by Calc's expression and read through the header structs
 * (ef_res_animcurve.h). Spin, magnet, newton and vortex go through the local
 * copy Calc makes (NW4R_EF_FIELD_VALUE), the other two through the pointer.
 ******************************************************************************/
void DumpField(const File* f, int i, int j, u8* pPtclTrack) {
    AnimCurveHeader* h = reinterpret_cast<AnimCurveHeader*>(pPtclTrack);
    if (h->magic != NW4R_EF_MAGIC_ANIMCURVE || h->curveFlag != AC_TYPE_PARTICLE_F32) {
        return;
    }
    switch (h->kindType) {
    case AC_TARGET_UNK32: case AC_TARGET_UNK34: case AC_TARGET_UNK35:
    case AC_TARGET_UNK36: case AC_TARGET_UNK38: case AC_TARGET_UNK39:
        break;
    default:
        // FIELD_SPEED's 4-byte info table is never read; other kinds have none.
        return;
    }
    u8* pInfo = pPtclTrack + sizeof(AnimCurveHeader) + h->keyTable + h->rangeTable +
                h->randomTable + h->nameTable;
    std::printf("FP %s %d p%d %x %u :", f->path.c_str(), i, j, Off(f, pInfo), h->kindType);
    switch (h->kindType) {
    case AC_TARGET_UNK32: {
        FieldGravityParam* pParam = reinterpret_cast<FieldGravityParam*>(pInfo);
        std::printf(" %08x %08x,%08x,%08x %u %u", Bits(pParam->power), Bits(pParam->dir.x),
                    Bits(pParam->dir.y), Bits(pParam->dir.z), pParam->space, pParam->type);
        break;
    }
    case AC_TARGET_UNK39: {
        FieldRandomParam* pParam = reinterpret_cast<FieldRandomParam*>(pInfo);
        std::printf(" %08x %08x %u %u %u", Bits(pParam->power), Bits(pParam->diffuse),
                    static_cast<u16>(pParam->interval), pParam->flags, pParam->type);
        break;
    }
    case AC_TARGET_UNK38: {
        NW4R_EF_FIELD_VALUE(FieldSpinParam) param = *reinterpret_cast<FieldSpinParam*>(pInfo);
        std::printf(" %08x %08x,%08x,%08x %u %u %u %u", Bits(param.speed), Bits(param.rot.x),
                    Bits(param.rot.y), Bits(param.rot.z), param.space, param.type,
                    param.reserved0, param.reserved1);
        break;
    }
    case AC_TARGET_UNK34: {
        NW4R_EF_FIELD_VALUE(FieldMagnetParam) param = *reinterpret_cast<FieldMagnetParam*>(pInfo);
        std::printf(" %08x %08x,%08x,%08x %u %u %u %u", Bits(param.power), Bits(param.pos.x),
                    Bits(param.pos.y), Bits(param.pos.z), param.space, param.type,
                    param.reserved0, param.reserved1);
        break;
    }
    case AC_TARGET_UNK35: {
        NW4R_EF_FIELD_VALUE(FieldNewtonParam) param = *reinterpret_cast<FieldNewtonParam*>(pInfo);
        std::printf(" %08x %08x %08x,%08x,%08x %u %u %u %u", Bits(param.power),
                    Bits(param.range), Bits(param.pos.x), Bits(param.pos.y), Bits(param.pos.z),
                    param.space, param.type, param.reserved0, param.reserved1);
        break;
    }
    case AC_TARGET_UNK36: {
        NW4R_EF_FIELD_VALUE(FieldVortexParam) param = *reinterpret_cast<FieldVortexParam*>(pInfo);
        std::printf(" %08x %08x %08x %08x,%08x,%08x %u %u %u %u", Bits(param.innerSpeed),
                    Bits(param.outerSpeed), Bits(param.radius), Bits(param.rot.x),
                    Bits(param.rot.y), Bits(param.rot.z), param.space, param.type,
                    param.reserved0, param.reserved1);
        break;
    }
    }
    std::printf("\n");
    sFields++;
}

/******************************************************************************
 * Curve evaluation (--curves)
 ******************************************************************************/
std::vector<u32> CurveTicks(u32 life) {
    std::vector<u32> ticks;
    for (u32 t = 0; t <= life + 1; t += life < 40 ? 1 : 3) {
        ticks.push_back(t);
    }
    ticks.push_back(250);
    ticks.push_back(1000);
    return ticks;
}

std::string TextureName(const TextureData* p) {
    return p != NULL ? std::string(static_cast<const char*>(p->name)) : "-";
}

void EvalTrack(const File* f, int i, char kind, int j, u8* pTrack) {
    AnimCurveHeader* h = reinterpret_cast<AnimCurveHeader*>(pTrack);
    if (h->magic != NW4R_EF_MAGIC_ANIMCURVE) {
        return;
    }
    static const u16 kSeeds[] = {0x0000, 0x1234, 0xBEEF};
    static const u32 kLives[] = {20, 97};

    // A particle and its manager chain, as far as the texture and child curves
    // follow it (raw storage: only these members are touched).
    alignas(16) static u8 sPtcl[sizeof(Particle)], sPm[sizeof(ParticleManager)],
        sEm[sizeof(Emitter)], sEf[sizeof(Effect)], sEs[sizeof(EffectSystem)];
    Particle* pPtcl = reinterpret_cast<Particle*>(sPtcl);
    pPtcl->mParticleManager = reinterpret_cast<ParticleManager*>(sPm);
    pPtcl->mParticleManager->mManagerEM = reinterpret_cast<Emitter*>(sEm);
    pPtcl->mParticleManager->mManagerEM->mManagerEF = reinterpret_cast<Effect*>(sEf);
    pPtcl->mParticleManager->mManagerEM->mManagerEF->mManagerES =
        reinterpret_cast<EffectSystem*>(sEs);
    pPtcl->mCalcRemain = 3;

    for (u16 seed : kSeeds) {
        for (u32 life : kLives) {
            for (u32 tick : CurveTicks(life)) {
                std::printf("C %s %d %c%d %x %u %u :", f->path.c_str(), i, kind, j, seed, life,
                            tick);
                switch (h->curveFlag) {
                case AC_TYPE_PARTICLE_U8: {
                    u8 target[8];
                    std::memset(target, 0xEE, sizeof target);
                    AnimCurveExecuteU8(pTrack, target, tick, seed, life);
                    for (int k = 0; k < 8; k++) {
                        std::printf(" %02x", target[k]);
                    }
                    break;
                }
                case AC_TYPE_PARTICLE_F32:
                case AC_TYPE_PARTICLE_ROTATE: {
                    f32 target[8];
                    std::memset(target, 0xEE, sizeof target);
                    if (h->curveFlag == AC_TYPE_PARTICLE_F32) {
                        AnimCurveExecuteF32(pTrack, target, tick, seed, life);
                    } else {
                        AnimCurveExecuteRotate(pTrack, target, tick, seed, life);
                    }
                    for (int k = 0; k < 8; k++) {
                        std::printf(" %08x", Bits(target[k]));
                    }
                    break;
                }
                case AC_TYPE_PARTICLE_TEXTURE: {
                    ParticleParameter& rParam = pPtcl->mParameter;
                    for (int l = 0; l < TEX_LAYER_MAX; l++) {
                        rParam.mTexture[l] = NULL;
                    }
                    rParam.mTextureWrap = 0;
                    rParam.mTextureReverse = 0;
                    AnimCurveExecuteTexture(pTrack, pPtcl, tick, seed, life);
                    for (int l = 0; l < TEX_LAYER_MAX; l++) {
                        std::printf(" %s", TextureName(rParam.mTexture[l]).c_str());
                    }
                    std::printf(" %x %x", rParam.mTextureWrap, rParam.mTextureReverse);
                    break;
                }
                case AC_TYPE_CHILD: {
                    sChildRequests.clear();
                    AnimCurveExecuteChild(pTrack, pPtcl, tick, seed, life);
                    for (const ChildRequest& c : sChildRequests) {
                        std::printf(" %d,", c.type);
                        for (int k = 0; k < 8; k++) {
                            std::printf("%02x", c.setting[k]);
                        }
                        std::printf(",%d,%s,%u", c.speed,
                                    c.pResource != NULL
                                        ? const_cast<EmitterResource*>(c.pResource)->GetName()
                                        : "-",
                                    c.calcRemain);
                        sChildCount++;
                    }
                    break;
                }
                default: {
                    std::printf(" ?");
                    break;
                }
                }
                std::printf("\n");
                sCurveEvals++;
            }
        }
    }
}

void DumpEffect(File* f, EffectProject* pProject) {
    Resource* r = Resource::GetInstance();
    u32 n = r->NumEmitter(pProject);
    std::printf("EF %s %u %u\n", f->path.c_str(), n, static_cast<u32>(pProject->headersize));
    sBreff++;

    for (u32 i = 0; i < n; i++) {
        EmitterResource* res = r->_GetEmitterIndexOf(i, pProject);
        const char* pName = res->GetName();
        EmitterResource* found = r->FindEmitter(pName, pProject).ptr();
        int foundIdx = -1;
        for (u32 k = 0; k < n; k++) {
            if (r->_GetEmitterIndexOf(k, pProject) == found) {
                foundIdx = static_cast<int>(k);
                break;
            }
        }
        if (FindFileOf(pName) != f) {
            std::fprintf(stderr, "ERROR: %s emitter %u name outside its file\n", f->path.c_str(), i);
            Errors()++;
        }
        EmitterDesc* desc = res->GetEmitterDesc();
        ParticleParameterDesc* ptcl = res->GetParticleParameterDesc();
        std::printf("E %s %u %x %s %x %d %u %u %u %u %x %x\n", f->path.c_str(), i, Off(f, res),
                    pName, Off(f, pName), foundIdx, res->NumPtclTrack(), res->NumPtclInitTrack(),
                    res->NumEmitTrack(), res->NumEmitInitTrack(), Off(f, desc), Off(f, ptcl));

        EmitterDrawSetting* ds = &desc->drawSetting;
        std::printf("ED %s %u %x %x %u %u %x %u %u %u %x %x %x %u %u %x %x %x %x %x %u %u\n",
                    f->path.c_str(), i, static_cast<u32>(desc->commonFlag),
                    static_cast<u32>(desc->emitFlag), static_cast<u16>(desc->emitLife),
                    static_cast<u16>(desc->ptclLife), Bits(desc->emitEmit),
                    static_cast<u16>(desc->emitEmitStart), static_cast<u16>(desc->emitEmitPast),
                    static_cast<u16>(desc->emitEmitInterval), Bits(desc->commonParam[0]),
                    Bits(desc->velPowerYAxis), Bits(desc->velDiffusionSpecDir),
                    static_cast<u16>(desc->emitEmitDiv), static_cast<u32>(desc->randomSeed),
                    static_cast<u16>(ds->mFlags), static_cast<u16>(ds->mAlphaFlickCycle),
                    Bits(ds->mLighting.mRadius), Bits(ds->mIndTexOffsetMtx[1][2]),
                    Bits(ds->zOffset), static_cast<unsigned>(desc->GetFormType()), ds->ptcltype);

        // The math value members (BE_MATH, HOST_BUILD.md §14).
        const BE_MATH(VEC3)* v3[5] = {&desc->velSpecDir, &desc->scale, &desc->rotate,
                                      &desc->translate, &ds->mLighting.mPosition};
        std::printf("EV %s %u", f->path.c_str(), i);
        for (int k = 0; k < 5; k++) {
            math::VEC3 v = *v3[k];
            std::printf(" %x,%x,%x", Bits(v.x), Bits(v.y), Bits(v.z));
        }
        std::printf("\n");
        std::printf("PV %s %u %x,%x %x,%x %x,%x,%x", f->path.c_str(), i, Bits(ptcl->size.x),
                    Bits(ptcl->size.y), Bits(ptcl->scale.x), Bits(ptcl->scale.y),
                    Bits(ptcl->rotate.x), Bits(ptcl->rotate.y), Bits(ptcl->rotate.z));
        for (int l = 0; l < TEX_LAYER_MAX; l++) {
            math::VEC2 ts = ptcl->textureScale[l], tt = ptcl->textureTranslate[l];
            std::printf(" %x,%x %x,%x", Bits(ts.x), Bits(ts.y), Bits(tt.x), Bits(tt.y));
        }
        std::printf("\n");

        // Texture names as RelocateCommand walks them.
        std::string names;
        const u8* p = ptcl->textureNames;
        for (int l = 0; l < TEX_LAYER_MAX; l++) {
            u16 len = *reinterpret_cast<const be_u16*>(p);
            p += 2;
            names += " ";
            names += p[0] != 0 ? reinterpret_cast<const char*>(p) : "-";
            p += len;
        }
        std::printf("PD %s %u%s %x %x %x %x\n", f->path.c_str(), i, names.c_str(),
                    static_cast<u16>(ptcl->textureWrap), Bits(ptcl->textureRotate[2]),
                    Bits(ptcl->rotateOffset[0]), Bits(ptcl->rotateOffset[2]));

        ResEmitter resEmitter(res);
        for (u16 j = 0; j < res->NumPtclTrack(); j++) {
            u8* pTrack = res->GetPtclTrack(j);
            if (resEmitter.GetPtclTrack(j).ptr() != reinterpret_cast<AnimCurveHeader*>(pTrack)) {
                std::fprintf(stderr, "ERROR: ResEmitter::GetPtclTrack mismatch\n");
                Errors()++;
            }
            DumpTrack(f, i, 'p', j, pTrack);
            DumpField(f, i, j, pTrack);
            if (sCurves) {
                EvalTrack(f, i, 'p', j, pTrack);
            }
        }
        for (u16 j = 0; j < res->NumEmitTrack(); j++) {
            DumpTrack(f, i, 'e', j, res->GetEmitTrack(j));
            if (sCurves) {
                EvalTrack(f, i, 'e', j, res->GetEmitTrack(j));
            }
        }
        sEmitters++;
    }
}

void DumpTexture(File* f, TextureProject* pProject) {
    Resource* r = Resource::GetInstance();
    // The name table, read as breftSearchName does (ef_resource.cpp).
    const u8* pTable = reinterpret_cast<u8*>(pProject) + pProject->headersize;
    u16 count = *reinterpret_cast<const be_u16*>(pTable + 4);
    const u8* p = pTable + 8;
    std::printf("TF %s %u %u\n", f->path.c_str(), count, static_cast<u32>(pProject->headersize));
    sBreft++;

    for (u16 i = 0; i < count; i++) {
        u16 len = *reinterpret_cast<const be_u16*>(p);
        const char* pName = reinterpret_cast<const char*>(p + 2);
        p += 2 + len + 8;

        TextureData* t = r->FindTexture(pName, pProject).ptr();
        if (t == NULL || FindFileOf(t) != f) {
            std::fprintf(stderr, "ERROR: FindTexture(%s) failed in %s\n", pName, f->path.c_str());
            Errors()++;
            continue;
        }
        const u8* pTex = t->texture;
        const u8* pTlut = t->tlut;
        const char* pSlotName = t->name;
        std::printf("T %s %u %s %x %x %u %u %u %u %u %u %u %u %u %u %x %x\n", f->path.c_str(), i,
                    pName, Off(f, t), Off(f, pSlotName), static_cast<u16>(t->width),
                    static_cast<u16>(t->height), t->format, t->tlutFormat,
                    static_cast<u16>(t->tlutEntries), static_cast<u32>(t->dataSize),
                    static_cast<u32>(t->tlutSize), t->mipmap, t->min_filt, t->mag_filt,
                    Off(f, pTex), Off(f, pTlut));
        sTextures++;
    }
}

/******************************************************************************
 * State dumps (after every RelocateCommand)
 ******************************************************************************/
std::string ProjectPath(void* pProject) {
    for (File* f : sRegistered) {
        if (f->pProject == pProject) {
            return f->path;
        }
    }
    return "?";
}

void DumpState(const std::string& stage, u32 relocErrors) {
    Resource* r = Resource::GetInstance();
    std::string breff, breft;
    for (void* p = RES_LIST_GetFirst(&r->mBREFFList); p != NULL;
         p = RES_LIST_GetNext(&r->mBREFFList, p)) {
        breff += (breff.empty() ? "" : ",") + ProjectPath(p);
    }
    for (void* p = RES_LIST_GetFirst(&r->mBREFTList); p != NULL;
         p = RES_LIST_GetNext(&r->mBREFTList, p)) {
        breft += (breft.empty() ? "" : ",") + ProjectPath(p);
    }
    std::printf("STAGE %s %u %u %u %s %s\n", stage.c_str(), relocErrors, r->NumEmitter(NULL),
                r->mNumTexture, breff.c_str(), breft.c_str());
    sStages++;

    for (File* f : sRegistered) {
        u32 base = port_vaddr(f->pData);
        if (port_hptr(base) != f->pData) {
            std::fprintf(stderr, "ERROR: M0 round trip failed for %s\n", f->path.c_str());
            Errors()++;
        }
        std::printf("FILE %s %s %08x %u\n", stage.c_str(), f->path.c_str(), base, f->size);
    }
    for (File* f : sRegistered) {
        u32 i = 0;
        while (i < f->size) {
            if (f->pData[i] == f->orig[i]) {
                i++;
                continue;
            }
            u32 start = i;
            std::string bytes;
            while (i < f->size && f->pData[i] != f->orig[i]) {
                char b[3];
                std::snprintf(b, sizeof b, "%02x", f->pData[i]);
                bytes += b;
                i++;
            }
            std::printf("W %s %s %x %s\n", stage.c_str(), f->path.c_str(), start, bytes.c_str());
            sRuns++;
        }
    }
}

/**
 * Cross-file slots, read back through the RES_PTR / be_u32 views the readers
 * use (DrawStrategyImpl via ParticleParameterDesc::mTexture, ef_animcurve.cpp
 * via the curve name tables): every target must be the TextureData /
 * EmitterResource that _FindTexture / _FindEmitter returns for the name.
 */
/** Count a slot whose target lies in a different X.breff/X.breft pair. */
void CountForeign(const void* pOwner, const void* pTarget) {
    File* a = FindFileOf(pOwner);
    File* b = FindFileOf(pTarget);
    if (a != NULL && b != NULL &&
        a->path.substr(0, a->path.size() - 6) != b->path.substr(0, b->path.size() - 6)) {
        sForeignSlots++;
    }
}

void CheckCrossFileSlots(const std::string& stage) {
    Resource* r = Resource::GetInstance();
    for (u32 idx = 0; idx < r->NumEmitter(NULL); idx++) {
        EmitterResource* res = r->_GetEmitterIndexOf(idx, NULL);
        ParticleParameterDesc* ptcl = res->GetParticleParameterDesc();
        const u8* p = ptcl->textureNames;
        for (int l = 0; l < TEX_LAYER_MAX; l++) {
            u16 len = *reinterpret_cast<const be_u16*>(p);
            p += 2;
            TextureData* want = r->_FindTexture(reinterpret_cast<const char*>(p), NULL);
            TextureData* got = ptcl->mTexture[l];
            CountForeign(res, got);
            if (want != got) {
                std::fprintf(stderr, "ERROR: %s: particle texture slot %s != %s\n", stage.c_str(),
                             Where(got).c_str(), Where(want).c_str());
                Errors()++;
            }
            sTextureSlots++;
            p += len;
        }
        for (u16 j = 0; j < res->NumPtclTrack(); j++) {
            AnimCurveHeader* h = reinterpret_cast<AnimCurveHeader*>(res->GetPtclTrack(j));
            if (h->magic != NW4R_EF_MAGIC_ANIMCURVE || h->kindEnable != 1 ||
                (h->curveFlag != AC_TYPE_PARTICLE_TEXTURE && h->curveFlag != AC_TYPE_CHILD)) {
                continue;
            }
            u8* pTbl = reinterpret_cast<u8*>(h) + sizeof(AnimCurveHeader) + h->keyTable +
                       h->rangeTable + h->randomTable;
            AnimCurveNameTable* pNames = reinterpret_cast<AnimCurveNameTable*>(pTbl);
            const u8* pName = pTbl + 4 + pNames->count * 4;
            for (u16 k = 0; k < pNames->count; k++) {
                u16 len = *reinterpret_cast<const be_u16*>(pName);
                pName += 2;
                const char* s = reinterpret_cast<const char*>(pName);
                void* want = h->curveFlag == AC_TYPE_CHILD
                                 ? static_cast<void*>(r->_FindEmitter(s, NULL))
                                 : static_cast<void*>(r->_FindTexture(s, NULL));
                void* got = PORT_U32_TO_PTR(void*, pNames->datas[k].work);
                if (h->curveFlag == AC_TYPE_PARTICLE_TEXTURE) {
                    // ef_animcurve.cpp reads the same words as RES_PTR(TextureData).
                    RES_PTR(TextureData)* pView =
                        reinterpret_cast<RES_PTR(TextureData)*>(&pNames->datas[0]);
                    if (static_cast<TextureData*>(pView[k]) != got) {
                        std::fprintf(stderr, "ERROR: texture curve view mismatch\n");
                        Errors()++;
                    }
                }
                CountForeign(res, got);
                if (want != got) {
                    std::fprintf(stderr, "ERROR: %s: curve slot %s != %s\n", stage.c_str(),
                                 Where(got).c_str(), Where(want).c_str());
                    Errors()++;
                }
                sCurveSlots++;
                pName += len;
            }
        }
    }
}

/******************************************************************************
 * RPSysEffectCreator
 ******************************************************************************/
void SetEffectResource(Creator& c, File* pEffect, File* pTexture, const std::string& stage) {
    Resource* r = Resource::GetInstance();
    c.pEffect = pEffect;
    c.pTexture = pTexture;

    c.pEffectProject = r->Add(pEffect->pData);
    pEffect->pProject = c.pEffectProject;
    sRegistered.push_back(pEffect);

    c.pTextureProject = r->AddTexture(pTexture->pData);
    pTexture->pProject = c.pTextureProject;
    sRegistered.push_back(pTexture);

    u32 errors = r->RelocateCommand();

    DumpEffect(pEffect, c.pEffectProject);
    DumpTexture(pTexture, c.pTextureProject);
    DumpState(stage, errors);
    CheckCrossFileSlots(stage);
}

void ResetEffectResource(Creator& c, const std::string& stage) {
    Resource* r = Resource::GetInstance();
    r->RemoveEffectProject(c.pEffectProject);
    r->RemoveTextureProject(c.pTextureProject);
    u32 errors = r->RelocateCommand();

    // The files stay in memory until the archive is unmounted; dump them with
    // the rest, then drop them.
    DumpState(stage, errors);
    CheckCrossFileSlots(stage);
    for (size_t i = 0; i < sRegistered.size();) {
        if (sRegistered[i] == c.pEffect || sRegistered[i] == c.pTexture) {
            sRegistered.erase(sRegistered.begin() + i);
        } else {
            i++;
        }
    }
}

/** RPSysEffectMgr::setCaptureEfb on one creator (ef-5). */
void CaptureEfb(Creator& c, void* pTexture) {
    Resource* r = Resource::GetInstance();
    ResTexture res = r->FindTexture("dummy_tex", c.pTextureProject);
    if (!res.IsValid()) {
        return;
    }
    // RPSysEffectMgr.cpp: format, width, height, then the heap buffer.
    res.ptr()->format = GX_TF_RGB565;
    res.ptr()->width = 304;
    res.ptr()->height = 228;
    res.ptr()->texture = static_cast<u8*>(pTexture);

    const u8* pSlot = reinterpret_cast<const u8*>(res.ptr()) + 0x18;
    u32 raw = port_load_be32(pSlot);
    bool ok = raw == port_vaddr(pTexture) && res.ptr()->texture == pTexture &&
              static_cast<u16>(res.ptr()->width) == 304 && pSlot[-0x18 + 4] == 0x01 &&
              pSlot[-0x18 + 5] == 0x30;
    if (!ok) {
        std::fprintf(stderr, "ERROR: setCaptureEfb slot not written as the Wii does\n");
        Errors()++;
    }
    // FindTexture falls back to every registered BREFT, so the boot creators
    // find the scene's dummy_tex too (as in the game).
    std::printf("CAP %s %s %08x %08x\n", c.pTexture->path.c_str(), Where(res.ptr()).c_str(),
                port_vaddr(pTexture), raw);
    sCaptures++;
}

} // namespace

// The two CreationQueue entry points a child curve calls: recorded, not queued.
namespace nw4r {
namespace ef {
void CreationQueue::AddParticleCreation(const EmitterInheritSetting* pSetting, Particle*,
                                        EmitterResource* pResource, u16 calcRemain) {
    ChildRequest c = {0, {}, pSetting->speed, pResource, calcRemain};
    std::memcpy(c.setting, pSetting, sizeof c.setting);
    sChildRequests.push_back(c);
}
void CreationQueue::AddEmitterCreation(const EmitterInheritSetting* pSetting, Particle*,
                                       EmitterResource* pResource, u16 calcRemain) {
    ChildRequest c = {1, {}, pSetting->speed, pResource, calcRemain};
    std::memcpy(c.setting, pSetting, sizeof c.setting);
    sChildRequests.push_back(c);
}
} // namespace ef
} // namespace nw4r

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <disc-files-dir> [--curves]\n", argv[0]);
        return 2;
    }
    sCurves = argc > 2 && std::strcmp(argv[2], "--curves") == 0;

    PortHostSetDiscRoot(argv[1]);
    EGG::DvdFile::initialize();

    const std::string staticArc = "Common/PartyStatic/common.carc";
    Archive boot;
    if (!MountArchive(staticArc, &boot)) {
        std::fprintf(stderr, "ERROR: cannot mount %s\n", staticArc.c_str());
        return 1;
    }

    // RPSysEffectMgr: the cursor creator first, then the static creator.
    Creator cursor, statics;
    File* pCurEff = LoadFile(boot, "nw4r_defcursor_pack.breff");
    File* pCurTex = LoadFile(boot, "nw4r_defcursor_pack.breft");
    File* pStEff = LoadFile(boot, "basic_common.breff");
    File* pStTex = LoadFile(boot, "basic_common.breft");
    if (pCurEff == NULL || pCurTex == NULL || pStEff == NULL || pStTex == NULL) {
        return 1;
    }
    SetEffectResource(cursor, pCurEff, pCurTex, "cursor");
    SetEffectResource(statics, pStEff, pStTex, "static");

    // RPSysEffectMgr::setCaptureEfb's buffer (EFB capture, heap).
    void* pEfb = new (static_cast<EGG::Heap*>(NULL), 32) u8[304 * 228 * 2];

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
            File* pEff = LoadFile(a, stem + ".breff");
            File* pTex = LoadFile(a, stem + ".breft");
            if (pEff == NULL || pTex == NULL) {
                continue;
            }
            Creator scene;
            SetEffectResource(scene, pEff, pTex, "set:" + stem);
            // setCaptureEfb runs over every active creator.
            CaptureEfb(cursor, pEfb);
            CaptureEfb(statics, pEfb);
            CaptureEfb(scene, pEfb);
            ResetEffectResource(scene, "reset:" + stem);
            delete pEff;
            delete pTex;
        }
        UnmountArchive(a);
    }

    std::fprintf(stderr,
                 "ef_harness: %d BREFF (%d emitters, %d tracks, %d field blocks), %d BREFT (%d textures), %d "
                 "stages, %d changed runs, %d particle texture slots and %d curve slots "
                 "checked (%d into another pair's file), %d capture slots; %d errors (pointer "
                 "size %zu)\n",
                 sBreff, sEmitters, sTracks, sFields, sBreft, sTextures, sStages, sRuns, sTextureSlots,
                 sCurveSlots, sForeignSlots, sCaptures, Errors(), sizeof(void*));
    if (sCurves) {
        std::fprintf(stderr, "ef_harness: %d curve evaluations, %d child creation requests\n",
                     sCurveEvals, sChildCount);
    }
    return Errors() == 0 ? 0 : 1;
}
