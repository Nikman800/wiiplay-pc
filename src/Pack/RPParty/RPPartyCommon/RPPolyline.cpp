#include <Pack/RPParty/RPPartyCommon/RPPolyline.h>
#include <Pack/RPParty/RPPartyCommon/RPLytUtil.h>
#include <Pack/RPKernel/RPSysLayout.h>
#include <Pack/RPKernel/RPSysLytResAccessor.h>
#include <Pack/RPSystem/RPSysResourceManager.h>

#include <cmath>

RPSysLytResAccessor* RPCreateLocalLytResAccessor(const char* pArcName) {
    RPSysLytResAccessor* pAccessor = RPSysLytResAccessor::create(NULL);
    EGG::Archive* pArchive = RPSysResourceManager::LoadGameLocalArchive(-1, NULL);
    void* pBinary = RPSysResourceManager::GetFileFromArchive(pArchive, pArcName, NULL);
    pAccessor->mountArchive(pBinary, NULL);
    return pAccessor;
}

RPSysLayout* RPCreateLayout(RPSysLytResAccessor* pAccessor, const char* pName) {
    return RPSysLayout::create(NULL, pAccessor, pName);
}

f32 RPInterpolate(f32 start, f32 end, s32 frame, s32 frameNum, s32 type) {
    f32 value;
    if (frame <= 0) {
        value = start;
    } else if (frame >= frameNum) {
        value = end;
    } else {
        switch (type) {
        case 0:
        default:
        {
            f32 cosine = static_cast<f32>(std::cos(3.1415927f * frame / frameNum));
            f32 weight = 1.0f - cosine;
            value = start + weight * (end - start) / 2.0f;
            break;
        }
        case 1:
            value = start + frame * (end - start) / frameNum;
            break;
        case 2:
        {
            f32 weight = static_cast<f32>(std::sin(1.5707964f * frame / frameNum));
            value = __fmadds(weight, end - start, start);
            break;
        }
        }
    }
    return value;
}

RPPolyline::RPPolyline(s32 pointNum) : mPointNum(pointNum) {
    mpPoints = new nw4r::math::VEC3[pointNum];
    mpWork[0] = new nw4r::math::VEC3[mPointNum - 1];
    mpWork[1] = new nw4r::math::VEC3[mPointNum - 1];
}

RPPolyline::~RPPolyline() {}

static inline void CopyPoint(nw4r::math::VEC3& point, const nw4r::math::VEC3& source) {
    point.x = source.x;
    point.y = source.y;
    point.z = source.z;
}

void RPPolyline::SetPoints(const nw4r::math::VEC3* pPoints) {
    s32 i = 0;
    do {
        CopyPoint(mpPoints[i], *pPoints);
        ++pPoints;
    } while (++i < mPointNum);
}

void RPPolyline::Calc(f32 t, nw4r::math::VEC3* pOut) const {
    register s32 offset;
    const nw4r::math::VEC3* point;
    nw4r::math::VEC3* work;
    const nw4r::math::VEC3* points;
    s32 src = 0;
    s32 dst = 1;
    offset = 0;
    register s32 count;
    s32 i = 0;
    register const RPPolyline* self = this;
    // NONMATCHING-DEBT: asm volatile loads mPointNum after the li of src/dst/offset/i, before the
    // mpPoints/mpWork loads (the original's order). Natural C (count = mPointNum; indexed
    // VEC3Lerp(&mpWork[src][i], &mpPoints[i], &mpPoints[i + 1], t)) hoists the three loads above
    // the li's in PCode (5 words, order only). Tried: declaration orders, for/do forms, count
    // read in the loop condition, pointer walkers, #pragma scheduling/peephole off.
    // Rule check (2026-10-29, --sched run 1): the original's three member loads come after the
    // prologue's `stw r31` (a set1=worst store), i.e. the loads had mem-raw edges from it, so their
    // object was in the worst-case set. Calc is const (CF): the this dummy is const and stays out,
    // so in natural C the post-RA scheduler interleaves the loads with the li's (73/78, order only).
#if defined(PORT_HOST)
    count = self->mPointNum;
#else
    asm volatile {
        lwz count, 0(self)
    }
#endif
    points = mpPoints;
    work = mpWork[src];
    point = points;
    do {
#if defined(PORT_HOST)
        // Pointer-sized arithmetic (port/docs/POINTERS.md §5.1).
        nw4r::math::VEC3Lerp(work, point,
            reinterpret_cast<const nw4r::math::VEC3*>(reinterpret_cast<const u8*>(points) + offset) + 1, t);
#else
        nw4r::math::VEC3Lerp(work, point,
            reinterpret_cast<const nw4r::math::VEC3*>(offset + reinterpret_cast<u32>(points)) + 1, t);
#endif
        offset += sizeof(nw4r::math::VEC3);
        ++point;
        ++work;
    } while (++i < count - 1);

    while (--count > 2) {
        s32 i = 0;
        do {
            nw4r::math::VEC3Lerp(&mpWork[dst][i], &mpWork[src][i], &mpWork[src][i + 1], t);
        } while (++i < count - 1);
        src = dst;
        dst = ~dst & 1;
    }
    nw4r::math::VEC3Lerp(pOut, &mpWork[src][0], &mpWork[src][1], t);
}
