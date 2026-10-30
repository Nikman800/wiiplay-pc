#include <nw4r/ef.h>

#include <cmath>



namespace nw4r {
namespace ef {

namespace {

// Key of a float curve with N components
template <int N> struct AnimCurveKeyF32 {
    u16 frame;       // at 0x0
    u16 interp;      // at 0x2, two bits per component
    u16 PADDING_0x4; // at 0x4
    u8 random;       // at 0x6
    u8 PADDING_0x7;  // at 0x7
    union {
        f32 value[N]; // at 0x8
        u16 rangeIdx; // at 0x8
    };
};

// Random range of a float curve with N components
template <int N> struct AnimCurveRangeF32 {
    struct {
        f32 base;  // at 0x0
        f32 width; // at 0x4
    } comp[N];
};

// Random range of a rotation curve
struct AnimCurveRangeRotate {
    struct {
        f32 base;  // at 0x0
        f32 width; // at 0x4
    } comp[3];
    u8 randomSign;     // at 0x18
    u8 PADDING_0x19[3]; // at 0x19
};

// Key of a byte curve with N components
template <int N> struct AnimCurveKeyU8 {
    u16 frame;       // at 0x0
    u16 interp;      // at 0x2, two bits per component
    u16 PADDING_0x4; // at 0x4
    u8 random;       // at 0x6
    u8 PADDING_0x7;  // at 0x7
    union {
        u8 value[N];  // at 0x8
        u16 rangeIdx; // at 0x8
    };
};

struct AnimCurveRangeCompU8 {
    u8 base;  // at 0x0
    u8 width; // at 0x1, scaled by a signed random in [-1, 1)
};

// Random range of a byte curve with N components
template <int N> struct AnimCurveRangeU8 {
    AnimCurveRangeCompU8 comp[N];
};

// Key of a texture curve
struct AnimCurveKeyTexture {
    u16 frame;          // at 0x0
    u8 random;          // at 0x2
    u8 PADDING_0x3[9];  // at 0x3
    union {
        struct {
            u8 wrap;    // at 0xC
            u8 reverse; // at 0xD
        };
        u16 rangeIdx; // at 0xC
    };
    u16 name; // at 0xE
};

struct AnimCurveRangeTexture {
    u8 wrap;          // at 0x0
    u8 reverse;       // at 0x1
    u16 name;         // at 0x2
    u8 randomReverse; // at 0x4
    u8 PADDING_0x5[3]; // at 0x5
};

struct AnimCurveRandomTexture {
    u8 wrap;    // at 0x0
    u8 reverse; // at 0x1
    u16 name;   // at 0x2
};

// Child created by a child curve
struct AnimCurveChild {
    EmitterInheritSetting setting; // at 0x0
    u16 resIdx;                    // at 0x8, index into the name table
};

// Key of a child curve
struct AnimCurveKeyChild {
    u16 frame;          // at 0x0
    u8 random;          // at 0x2
    u8 PADDING_0x3[9];  // at 0x3
    union {
        AnimCurveChild child; // at 0xC
        u16 rangeIdx;         // at 0xC
    };
};

template <typename T> struct AnimCurveTable {
    u16 count;       // at 0x0
    u16 PADDING_0x2; // at 0x2
    T datas[1];      // at 0x4
};

enum InterpType {
    INTERP_LINEAR,
    INTERP_SMOOTH,
    INTERP_STEP,
};

enum KeyRandomFlag {
    KEY_RANDOM_FLAG_TABLE = (1 << 1),
};


enum {
    AC_TARGET_TEXTURE1 = 14,
    AC_TARGET_TEXTURE2 = 15,
    AC_TARGET_TEXTUREIND = 16,
};

} // namespace

// Integer frame of the step curves (texture and child)
// rEnd is set on the last tick of an even number of turns.
static inline void CalcFrameStep(u8* pCmdList, u32 tick, u32 life,
                                 u16& rFrame, u32& rLoop, bool& rEnd) {
    AnimCurveHeader* pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);
    u8 flag = pHeader->processFlag;
    rLoop = 0;
    rEnd = false;

    if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
        pHeader->loopCount <= 1) {

        if (!(flag & AnimCurveHeader::PROC_FLAG_FITTING)) {
            if (tick >= pHeader->frameLength) {
                rFrame = pHeader->frameLength;
                return;
            }
            rFrame = tick;
            return;
        }

        rFrame = pHeader->frameLength * tick / life;
        return;
    }

    if (!(flag & AnimCurveHeader::PROC_FLAG_FITTING)) {
        if (pHeader->frameLength <= 1 || !(flag & AnimCurveHeader::PROC_FLAG_TURN)) {
            rLoop = tick / pHeader->frameLength;

            if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
                rLoop >= pHeader->loopCount) {
                rLoop = static_cast<u8>(pHeader->loopCount - 1);
                rFrame = pHeader->frameLength;
                return;
            }

            rFrame = tick - rLoop * pHeader->frameLength;
            return;
        }

        rLoop = tick / (pHeader->frameLength - 1);

        if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
            rLoop >= pHeader->loopCount) {
            if (pHeader->loopCount % 2 == 0) {
                if (rLoop == pHeader->loopCount &&
                    tick - rLoop * (pHeader->frameLength - 1) == 0) {
                    rEnd = true;
                }
                rFrame = 0;
            } else {
                rFrame = static_cast<u8>(pHeader->frameLength - 1);
            }
            rLoop = static_cast<u8>(pHeader->loopCount - 1);
            return;
        }

        if (rLoop % 2 == 0) {
            rFrame = tick - rLoop * (pHeader->frameLength - 1);
            return;
        }
        rFrame = (pHeader->frameLength - 1) - (tick - rLoop * (pHeader->frameLength - 1));
        return;
    }

    if (tick >= life) {
        if (!(flag & AnimCurveHeader::PROC_FLAG_TURN) ||
            pHeader->loopCount % 2 != 0) {
            rFrame = pHeader->frameLength;
        } else {
            rFrame = 0;
        }
        rLoop = static_cast<u8>(pHeader->loopCount - 1);
        return;
    }

    if (pHeader->frameLength <= 1 || !(flag & AnimCurveHeader::PROC_FLAG_TURN)) {
        f32 scale = pHeader->loopCount *
                    (static_cast<f32>(pHeader->frameLength) / static_cast<f32>(life));

        u32 loop = tick * scale / pHeader->frameLength;
        rLoop = loop;
        rFrame = tick * scale - loop * pHeader->frameLength;
        return;
    }

    f32 scale =
        (1.0f + static_cast<f32>(static_cast<int>(pHeader->frameLength - 1)) * pHeader->loopCount) / life;
    u32 pos = tick * scale;
    // The original converts tick here too (dead): the int-to-float scratch slots of the next
    // conversion alternate as if it existed (0x8/0x10).
    f32 fTick = tick;
    rLoop = pos / (pHeader->frameLength - 1);

    if (rLoop >= pHeader->loopCount) {
        if (pHeader->loopCount % 2 == 0) {
            if (tick == life - 1) {
                rEnd = true;
            }
            rFrame = 0;
        } else {
            rFrame = static_cast<u8>(pHeader->frameLength - 1);
        }
        rLoop = static_cast<u8>(pHeader->loopCount - 1);
        return;
    }

    if (rLoop % 2 == 0) {
        rFrame = pos - rLoop * (pHeader->frameLength - 1);
        return;
    }
    rFrame = (pHeader->frameLength - 1) * (rLoop + 1) - pos;
}

// Step curves: binary search for the frame. With lowerBound set this finds
// the first key at or after the frame, otherwise the last key at or before it.
template <typename TKey>
static inline int SearchKeyStep(TKey* pKeys, u16 numKey, int frame,
                                bool lowerBound) {
    int hi = numKey - 1;
    int lo = 0;
    int mid = hi / 2;
    int firstFrame = pKeys[0].frame;
    if (frame < firstFrame || (frame == firstFrame && lowerBound)) {
        return 0;
    }

    if (pKeys[hi].frame < frame ||
        (frame == pKeys[hi].frame && !lowerBound)) {
        return hi;
    }

    int midFrame = pKeys[mid].frame;

    while (lo < mid) {
        if (frame == midFrame) {
            if (lowerBound) {
                hi = mid;
            } else {
                lo = mid;
            }
        } else {
            if (midFrame < frame) {
                lo = mid;
            }
            if (midFrame >= frame) {
                hi = mid;
            }
        }

        mid = (lo + hi) / 2;
        midFrame = pKeys[mid].frame;
    }

    return lo;
}

// Frame of a key in a table with variable sized keys
static inline u16 KeyFrame(u8* pKeys, int idx, int keySize) {
    return *reinterpret_cast<u16*>(pKeys + idx * keySize);
}

// Time of a looping curve stretched over the particle life
static inline f32 CalcFittingLoopTime(AnimCurveHeader* pHeader, u8 flag,
                                      u32 tick, u32 lastTick, u32& rLoop) {
    f32 scale = pHeader->loopCount *
                (static_cast<f32>(static_cast<int>((pHeader->frameLength - 1))) / lastTick);

    u32 loop = tick * scale / static_cast<int>((pHeader->frameLength - 1));
    rLoop = loop;

    f32 time = tick * scale - loop * (pHeader->frameLength - 1);

    if (!(flag & AnimCurveHeader::PROC_FLAG_TURN)) {
        return time;
    }

    if (loop % 2 == 0) {
        return time;
    }

    return static_cast<f32>(static_cast<int>((pHeader->frameLength - 1))) - time;
}

// Converts the particle tick to a curve time (loop, turn and fitting modes)
static inline f32 CalcTime(AnimCurveHeader* pHeader, u32 tick, u32 life,
                           u16& rFrame, u32& rLoop) {
    u16 frameLength = pHeader->frameLength;
    rLoop = 0;
    f32 time;
    // Last tick of the particle life, shared by the fitting-loop test and CalcFittingLoopTime.
    // Declared here and assigned at the test: the original's register colouring shows a
    // front-end value for life - 1 (CalcTime's flag is coloured last only if it exists).
    u32 lastTick;

    if (frameLength <= 1) {
        time = 0.0f;
        rLoop = tick;
        rFrame = 0;
    } else {
        u8 flag = pHeader->processFlag;

        if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
            pHeader->loopCount <= 1) {

            if (!(flag & AnimCurveHeader::PROC_FLAG_FITTING)) {
                rFrame = tick;
                if (tick >= frameLength - 1) {
                    rFrame = frameLength - 1;
                }
                time = rFrame;
            } else {
                time = tick * (static_cast<f32>(frameLength - 1) / (life - 1));

                if (time > static_cast<f32>(frameLength - 1)) {
                    time = static_cast<f32>(frameLength - 1);
                }

                rFrame = time;
            }
        } else if (!(flag & AnimCurveHeader::PROC_FLAG_FITTING)) {
            rLoop = tick / (frameLength - 1);

            if (!(flag & AnimCurveHeader::PROC_FLAG_TURN)) {
                if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
                    rLoop >= pHeader->loopCount) {
                    rFrame = (frameLength - 1);
                    rLoop = static_cast<u8>(pHeader->loopCount - 1);
                } else {
                    rFrame = tick - rLoop * (frameLength - 1);
                }
            } else {
                if (!(flag & AnimCurveHeader::PROC_FLAG_INFLOOP) &&
                    rLoop >= pHeader->loopCount) {
                    if (pHeader->loopCount % 2 == 0) {
                        rFrame = 0;
                    } else {
                        rFrame = (frameLength - 1);
                    }
                    rLoop = static_cast<u8>(pHeader->loopCount - 1);
                } else if (rLoop % 2 == 0) {
                    rFrame = tick - rLoop * (frameLength - 1);
                } else {
                    rFrame = (frameLength - 1) * (rLoop + 1) - tick;
                }
            }

            time = rFrame;
        } else {
            lastTick = life - 1;
            if (tick >= lastTick) {
                if (!(flag & AnimCurveHeader::PROC_FLAG_TURN) ||
                    pHeader->loopCount % 2 != 0) {
                    rFrame = static_cast<u8>(frameLength - 1);
                } else {
                    rFrame = 0;
                }

                time = rFrame;
                rLoop = static_cast<u8>(pHeader->loopCount - 1);
            } else {
                time = CalcFittingLoopTime(pHeader, flag, tick, lastTick, rLoop);
                rFrame = time;
            }
        }
    }


    return time;
}

// Turning curves play the keys backwards on odd loops, so the random seed of
// the key on the far side belongs to the neighbouring loop.
static inline void AdjustTurnLoop(AnimCurveHeader* pHeader,
                                  AnimCurveKey* pKeyTbl, int idx,
                                  u32 loop, u32& rLoop0, u32& rLoop1) {
    u8 flag = pHeader->processFlag;

    if (flag & AnimCurveHeader::PROC_FLAG_TURN) {
        if ((flag & AnimCurveHeader::PROC_FLAG_INFLOOP) ||
            pHeader->loopCount > 1) {

            if (!(loop & 1) && idx + 1 >= pKeyTbl->count - 1) {
                if ((flag & AnimCurveHeader::PROC_FLAG_INFLOOP) ||
                    loop < pHeader->loopCount - 1) {
                    rLoop1 = loop + 1;
                }
            }

            if ((loop & 1) && idx == 0 && loop != 0) {
                rLoop0 = loop + 1;
            }
        }
    }
}

static inline bool SearchKeyStride(AnimCurveTable<u8>* pKeyTbl, u8* pKeys, int keySize,
                                   int frame,
                             f32 time, int& rIdx, u16& rPrevFrame,
                             u16& rNextFrame) {
    bool exact;
    int lo;
    int hi;
    int mid;

    lo = 0;
    hi = pKeyTbl->count - 1;
    mid = hi / 2;

    exact =
        std::fabs(static_cast<f32>(frame) - time) < NW4R_MATH_FLT_EPSILON;

    rPrevFrame = KeyFrame(pKeys, 0, keySize);

    if (frame < KeyFrame(pKeys, 0, keySize)) {
        rIdx = 0;
        return true;
    }

    if (frame == KeyFrame(pKeys, 0, keySize)) {
        rIdx = 0;

        if (hi == 0) {
            return true;
        }

        if (!exact) {
            rNextFrame = KeyFrame(pKeys, 1, keySize);
        }
        return exact;
    }

    rNextFrame = KeyFrame(pKeys, hi, keySize);
    if (rNextFrame <= frame) {
        rIdx = hi;
        return true;
    }

    int midFrame = KeyFrame(pKeys, mid, keySize);

    while (lo < mid) {
        if (frame == midFrame) {
            rIdx = mid;

            if (!exact) {
                rPrevFrame = midFrame;
                rNextFrame = KeyFrame(pKeys, mid + 1, keySize);
            }
            return exact;
        }

        if (midFrame < frame) {
            lo = mid;
            rPrevFrame = midFrame;
        } else {
            hi = mid;
            rNextFrame = midFrame;
        }

        mid = (lo + hi) / 2;
        midFrame = KeyFrame(pKeys, mid, keySize);
    }

    rIdx = lo;
    return false;
}


static inline u32 MixRandom(u32 rnd) {
    return rnd * 0x343FD + 0x269EC3;
}

// Random seed of a key, folded byte by byte
static inline u32 CalcRandomSeed(u16 seed, AnimCurveHeader* pHeader, u32 loop,
                                 u32 rangeIdx) {
    union {
        u32 value;
        u8 bytes[4];
    } rndSeed;

    // Flat left-to-right sum with a u32 rangeIdx: shape from doldecomp/forecast (CC0), whose
    // GetRandomRange reads `u32 rangeIdx = pKey->rangeIdx` and adds rangeIdx before loop. With it
    // both both-random seed adds of U8, F32 and Rotate match (2026-10-29k, unit 75 -> 69);
    // the flat order with a u16 rangeIdx is far worse (281 words), and the old grouping
    // `A + B + (L + R) + K` with u32 gives no change. Earlier forms (about 45 groupings, named
    // seed term, RandomSeedBase(), split `+=`) are in the 2026-10-29j notes: all worse.
    rndSeed.value = seed * 0x3F81F635 + pHeader->randomSeed * 0x30A74193 +
                    rangeIdx * 0x371097E7 + loop * 0x7B929 + 0x4BF53;

    rndSeed.bytes[2] ^= rndSeed.bytes[3];
    rndSeed.bytes[1] ^= rndSeed.bytes[2];
    rndSeed.bytes[0] ^= rndSeed.bytes[1];

    return rndSeed.value;
}

static inline u8* GetRandomRangeU8(AnimCurveHeader* pHeader,
                                   AnimCurveTable<u8>* pRangeTbl,
                                   AnimCurveTable<u8>* pRandomTbl,
                                   int rangeSize, u8 random, int rangeIdx,
                                   u32 loop, u16 seed, u32& rRnd) {
    u32 rnd = CalcRandomSeed(seed, pHeader, loop, rangeIdx);
    u8* pRange;

    if (!(random & KEY_RANDOM_FLAG_TABLE)) {
        pRange = pRangeTbl->datas + rangeIdx * rangeSize;
    } else {
        pRange = pRandomTbl->datas + (rnd >> 16) % pRandomTbl->count * rangeSize;
        rnd = MixRandom(rnd);
    }

    rRnd = rnd;
    return pRange;
}

// Base plus width scaled by a signed random in [-1, 1). The caller clamps the sum and
// advances the range: the original keeps the base load and the clamped sum apart.
static inline int RandomValueU8(const u8* pRange, u32 rnd) {
    return pRange[0] + pRange[1] * static_cast<s16>(rnd >> 16) / 32768;
}

// Next random value, unless the components share one
static inline void UpdateRandomU8(AnimCurveHeader* pHeader, u32& rRnd) {
    if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
        rRnd = MixRandom(rRnd);
    }
}

// Matching idiom: interp by const reference (not by value) for its register numbering; the
// original's exact form is unknown (docs/NONMATCHING.md, "Matching idioms").
static inline u8 InterpolateU8(const u8& interp, u32 t, u8 v0, u8 v1) {
    switch (interp & 3) {
    case INTERP_LINEAR: {
        return v0 + ((t * (v1 - v0)) >> 16);
    }
    case INTERP_SMOOTH: {
        f32 ft = t / 65536.0f;

        if (interp == 1) {
            return v0 + ft * (ft * ((3.0f - 2.0f * ft) * (v1 - v0)));
        }

        f32 easeOut;
        f32 easeIn;
        if ((interp >> 2 & 1) == 0) {
            easeIn = 0.0f;
        } else {
            easeIn = 1.5f;
        }
        if ((interp >> 3 & 1) == 0) {
            easeOut = 0.0f;
        } else {
            easeOut = 1.5f;
        }

        f32 value =
            v0 + ft * (v1 - v0) *
                     (easeOut + (ft * (ft * (easeOut + easeIn - 2.0f)) +
                                 ft * (3.0f + (-2.0f * easeOut - easeIn))));

        if (value < 0.0f) {
            return 0;
        } else if (value > 255.0f) {
            return 255;
        } else {
            return value;
        }
    }
    case INTERP_STEP: {
        return v0;
    }
    default: {
        return 0;
    }
    }
}

// Not referenced; the linker strips them. Compiled first, they give .sdata2
// its constant order.
f32 AnimCurveCalcTime(u8* pCmdList, u32 tick, u32 life) {
    u16 frame;
    u32 loop;
    return CalcTime(reinterpret_cast<AnimCurveHeader*>(pCmdList), tick, life,
                    frame, loop);
}

u16 AnimCurveCalcFrameStep(u8* pCmdList, u32 tick, u32 life) {
    u32 loop;
    bool end;
    u16 frame;
    CalcFrameStep(pCmdList, tick, life, frame, loop, end);
    return frame;
}

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING (99.29%, 16 differences): the callee-saved registers, all FPRs, every random
// loop and both seed blocks match; what is left is volatile GPR colouring of the exact path's
// pKey (r3 here, r4 in the original) and the non-exact setup (pKey0 r7 vs r6, isRandom0's neg/or
// temp r3 vs r8; sim.py: only a vreg for (idx + 1) * keySize numbered below pKey0 gives r6, i.e.
// a named product, not taken).
// 2026-10-29l (scheduler study, no source change): the setup difference is decided BEFORE
// register allocation, by the pre-RA schedule (--sched --run 0), not by numbering or the
// post-RA pass. Our run-0 order is p0 (idx * keySize), add pKey0, p1 ((idx + 1) * keySize),
// lbz random0, addi pValue0, neg0, addi pInterp, or0, add pKey1, lbz random1, addi pValue1:
// pKey0 is already dead before pKey1 is defined. Replaying the colouring with four extra
// interference edges (random1's lbz and pValue1 each against isRandom0's neg and or temps, i.e.
// or0 scheduled after the lbz of pKey1[2] and pValue1's addi) gives exactly the original's
// registers (p1 r3, pKey0 r6, neg/or r8, pKey1 r6) and changes nothing else; no single or
// paired renumbering does, and 1..3 edge toggles do not either (/tmp/re_sched, edgesearch.py).
// The run-0 timing explains ours: the divwu for t holds the multiply unit until cycle 21, so
// p0 issues at 21, p1 at 27, add pKey1 at 33, while or0 is ready at 30; the original therefore
// had a different pre-RA dependence graph or timing in this block (something delaying the
// random0 chain by about four cycles, or the multiplies issued before the divide). Statement
// orders, int/u32/u16 random0, int isRandom0/isRandom1, `(random0 = pKey0[2]) != 0`, two-step
// pKey0/pKey1/pValue0/pInterp initialisations, &pKeys[...] and a named random1 all give the
// identical run-0 graph and code.
// 2026-10-29m (t and random0 leads, no gain): the run-0 picks are by priority (late), so only a
// change of the block's dependence graph can reorder it; statement order inside the block
// cannot. The original also has divwu at 0x6BC right after __cvt_fp2unsigned, so the divide is
// in this block there too. The conversion is a call (block end): t after the key products or the
// random flags moves the call into the setup and the keys live across it (211/530 words vs 12
// for the function); t before AdjustTurnLoop 402. Converting first and dividing later (`u32 ratio
// = ...;` or `t = ...; t /= nextFrame - prevFrame;` after the products or the flags): 16, same
// setup order (t reuses its register). u32/u16 casts of the divisor, a named `ratio` used at
// once: identical; a `range` local computed before the call 19 (lives across it); an inline
// CalcRatio helper (by value or const&) 69. random0 forms (isRandom0 = pKey0[2] != 0 with
// pKey0[2] at the branches, random0 read after the flags, `(random0 = pKey0[2]) != 0`, a copy
// through a second u8 local): identical code. Rotate (fdivs t): t after the flags or after the
// products 14 vs 11, int-to-float casts of the divisor 11/37. Reordering needs p1 above p0 in
// priority (p1's height 11 vs p0's 14, which reaches the isRandom0 cmpi) or a later-ready
// random0 chain; no natural form changed either.
// 2026-10-29k: the seed add order fixed in CalcRandomSeed (forecast's flat sum, u32 rangeIdx).
// Forecast's setup shapes (pKey0/pValue0 then pKey1/pValue1; random flags read directly as
// pKey[2] != 0): identical code. 2026-10-29j: the seed adds: see CalcRandomSeed. The setup block's pre-RA order (--sched run 0)
// keeps source order (pKey0, pKey1 products, then the pValue/pInterp adds, then each random flag);
// the original's order (pKey1's add before isRandom0's or, pInterp/pValue1 between the flags in
// Rotate) is what the post-RA scheduler makes from the original's registers (pKey1 reuses pKey0's
// register, the products share r3), so it is the colouring difference above (itself from the
// pre-RA order: 2026-10-29l), not a statement order. No effect: statement orders of the setup, random0 assigned in the test, isRandom via
// `? true : false`, a GetKeyPtr(pKeys, idx, keySize) inline (by value, const& idx/keySize, an
// inline offset local; also at the exact path); worse: KeyRandom()/IsKeyRandom() accessors
// (+113/+244 words), pKey1 = pKey0 + keySize (+9).
// 2026-10-29f (54 -> 18, unit 143 -> 107): the clamped random value is the caller's named local
// (`int v0 = RandomValueU8(pRange, rnd); pRange += 2;` then the clamp, then UpdateRandomU8):
// named outer locals are numbered lowest, so the clamped value is coloured after the interp
// temporaries and kept apart from the base load (base r10, value r11 as in the original); the
// four random sites now match. Writing RandomValueU8's expression inline at each site scores the
// same. Worse with the clamp still in a helper (CalcRandomU8 variants: value = quot; value +=
// base 189, quot + base 165, an inner RandomValueU8 with or without a named base, by value or
// const&, 151 words unit-wide vs 123), ClampU8(RandomValueU8(...)) as the initializer (155), and
// RandomValueU8 accumulating into a local (118).
// 2026-10-29e, pairwise sim.py search (~117k hits over ~1.5M replays of two named-local
// renumberings, function and block scope): no pair fixes the exact path's pKey or the
// isRandom0 neg/or temp. Only pKey0 (with the (idx + 1) * keySize product) moves to r6/r3,
// and only with 22+ other volatiles changed: pInterp declared between pKey1 and pValue1 with
// pRange1 declared before rnd1 (103 words, worse), or isRandom0's rangeIdx renumbered above
// random0 (the function-scope form already measured worse). The loop values are a form
// difference (the original keeps base and sum apart), not a numbering one.
// 2026-10-29d (92 -> 54): InterpolateU8 takes `const u8& interp` (the *pInterp loads become
// late binding temporaries: the interpolation code of every random loop now matches, 306 vs 320
// words unit-wide) and CalcRandomU8 accumulates (`int value = rpRange[0]; value += ...`, -27).
// Left in the loops: the original keeps the clamped value apart from the base load (isRandom0:
// base r10, value r11); the accumulating form merges them (r10/r10), and the form with a
// separate value (`value = rpRange[0] + ...`, 348) numbers it above the interp temporaries
// (sim.py: it must sit at or below ~85.5, i.e. below the const& temps), which no form reached.
// Worse or no effect: CalcRandomU8 returning u8, ut::Max/Min or ternary clamps, a ClampU8
// helper, named base/width locals or a CalcRandomValueU8(base, width, rnd) inline (by value or
// const&), a local copy of rRnd, rpRange++ forms, an out-parameter (`int&`/`u8&`, also written
// directly, block- or function-scope), u8 v0/v1, the call as InterpolateU8's argument;
// InterpolateU8 with const& or int v0/v1, int interp, const u32& t.
// 2026-10-29 (150 -> 92, Rotate's live-range lever): the both-random branch's rangeIdx0 and
// rangeIdx1 are function-scope u16 locals (assigned in that branch only), so each lhz is the
// first web of a low-numbered variable: the both-random seed block now matches (rangeIdx0 r4,
// rnd0 r8, the constant in r12). The isRandom0 branch keeps its block-scope rangeIdx (now u16,
// same code); the isRandom1 site reads the key directly. Worse: one shared rangeIdx for the
// single-random branches as in Rotate (+76 words), every site function-scope (+76), isRandom0's
// at function scope (+101), rangeIdx0 shared with isRandom0's or rangeIdx1 with isRandom1's
// (+63/+76), a named local at the isRandom1 site (+231). Declaration position and type
// (u16/int/u32) of the pair: no effect. No effect: the exact path reusing pKey0/pValue0/random0
// or a function-scope pKey; statement orders of the non-exact setup; v0/v1 at function scope
// (int: identical, u8: worse); CalcRandomU8 as InterpolateU8's argument (identical IR).
// Rejected: `u8 interp = *pInterp;` before CalcRandomU8 in the isRandom0 and both loops
// (92 -> 88) is a codegen-only temporary and schedules the lbz before the call, unlike the
// original. rangeSize as a plain int is still worse (+32..+107 words).
// Earlier (150 and before): 2026-10-29
// (166 -> 150): CalcRandomSeed groups `(loop * C + rangeIdx * C) + 0x4BF53` (the original's
// both-random block shares seed * C + randomSeed * C in r0 and adds it to loop * C first:
// (L + S) + (R + K); Texture and the single-seed blocks are unchanged), and pValue0 is
// declared before t (sim.py: pValue0 must be coloured before t, r4/r5). Left there: the
// constant 0x3F81F635 is in r16 (the original's r12; ours schedules seed * C before
// randomSeed * C, the original after), the operand order of the L + S adds, the loop values
// of the random loops (sim.py: CalcRandomU8's clamped value must be numbered below the
// *pInterp/*pValue loads; an out-parameter form, u8 locals, the call as an argument and
// loads in named locals were worse or identical). Declaration-order sweep (moves to top/
// bottom, adjacent swaps): no gain. Earlier (204 -> 166): the random0-side rangeIdx read
// into a named local (`int rangeIdx = *(u16*)pValue0;` in the isRandom0 branch, rangeIdx0 after
// rnd0 in the both branch; GetRandomRangeU8 now takes the index by value, as Texture's caller
// does): named locals get low vregs, so the lhz is coloured after random0, which now holds r0
// and the isRandom0 seed block matches (rangeIdx r3, rnd r4). Found with tools/mwcc_trace/sim.py:
// the lhz vreg must sit between the isRandom0 branch's rnd and random0 (the both branch's below
// its rnd0). Worse: the same local at the exact or isRandom1 sites (279/378 words), every site
// (380), the declarations hoisted to the top of the both block (+14); by value, const int&,
// u16/u32 locals all equal. Left: the non-exact setup (pKey0 r7 here, r6 in the original, the
// (idx + 1) * keySize mullw scheduled earlier), the both-path seed block (rangeIdx0 r8/rnd0 r9 vs
// r4/r8, a callee-saved r16 for seed * C), the exact path's pKey (r3 vs r4) and the loop volatiles.
// No effect (2026-10-29, F32's scope lever): each branch's block locals moved to function
// scope (one branch at a time, with mask last or kept; exact+isRandom0+isRandom1 together worse,
// 271), plus single top/bottom moves of every local after the move: no gain.
// Earlier notes (before 2026-10-29's rangeIdx locals):
// is left is volatile GPR colouring in the random paths and the interpolation setup.
// CalcRandomSeed's rangeIdx temps (lhz, inline-time temps) are coloured before random0 and
// take r0, where the original keeps random0 in r0 (rangeIdx in r3, the seed in r0); the exact
// path's pKey takes r3 where the original has r4 (pKey and pValue share r4). The colouring
// replay needs random0 numbered above the lhz temps, which no form reached: random0 read
// directly, through an accessor inline, assigned inside isRandom0's test, as const int& (worse).
// No effect: rangeIdx forms in GetRandomRangeU8 and CalcRandomSeed (no local, by value from the
// caller, int/u32, by const ref), the random parameter's type, pKey/pValue forms, function-scope
// rnd/v0/v1, declaration orders (hill-climb at a local optimum).
// Also no effect (2026-10-29, identical front-end IR): CalcRandomSeed with the seed as an
// out-parameter, randomSeed read in the caller, u32 parameters, rangeIdx first, a union
// initializer; the random parameter as const int&/u32& or int. Worse: rangeIdx read through a
// pointer inside CalcRandomSeed (reloads), GetRandomRangeU8 taking the key pointer
// (lhz 0xc(pKey) instead of 0(pValue)), the random parameter or random0 as const u8&.
// 2026-10-29 (289 -> 204): random1 read straight from the key (pKey1[2] in both places, so it
// is a late CSE temp, r3 as in the original); easeIn/easeOut set by if/else with `== 0` tests,
// easeOut declared first (ft f13, easeOut f12, and the original's bne block order).
// 2026-10-28 (319 -> 289): rangeSize bound as a const reference (its binding temp is promoted
// by the back end to a fresh, top-numbered vreg, coloured before CalcTime's processFlag:
// r22/r21 as in the original), computed after the table pointers (the original schedules
// the slwi after pRandomTbl's add), nextFrame declared just before mask (r27), pKeys after
// loop1 (r21/r20), SearchKeyStride's hi as count - 1 (the count load is its own temp).
void AnimCurveExecuteU8(u8* pCmdList, u8* pTarget, u32 tick, u16 seed,
                        u32 life) {
    bool exact;
    u8 enable;
    bool isRandom1;
    u32 loop1;
    u16 rangeIdx0;
    u16 rangeIdx1;
    u8* pKeys;
    bool isRandom0;
    AnimCurveTable<u8>* pRangeTbl;
    int keySize;
    u8* pKey0;
    f32 time;
    AnimCurveHeader* pHeader;
    AnimCurveTable<u8>* pKeyTbl;
    AnimCurveTable<u8>* pRandomTbl;
    u8* pValue0;
    u32 t;
    int num;
    u8* pKey1;
    u8* pValue1;
    u16 frame;
    u32 loop;
    int idx;
    u16 nextFrame;
    u8 mask;
    u8* pInterp;
    u8 random0;
    u16 prevFrame;
    pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);
    enable = pHeader->kindEnable;

    num = 8;
    for (mask = 0x80; mask != 0; mask >>= 1) {
        if (enable & mask) {
            break;
        }
        num--;
    }

    if (num == 0) {
        return;
    }

    keySize = (num + 13) & ~1;

    pKeyTbl =
        reinterpret_cast<AnimCurveTable<u8>*>(pCmdList + sizeof(AnimCurveHeader));
    pRangeTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pKeyTbl) + pHeader->keyTable);
    pRandomTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pRangeTbl) + pHeader->rangeTable);
    // Matching idiom: a const reference (not a plain int) for its register numbering; the original's exact form is unknown (docs/NONMATCHING.md, "Matching idioms").
    const int& rangeSize = num * 2;

    time = CalcTime(pHeader, tick, life, frame, loop);

    pKeys = pKeyTbl->datas;
    exact = SearchKeyStride(pKeyTbl, pKeys, keySize, frame, time, idx,
                                 prevFrame, nextFrame);

    if (exact) {
        u8* pKey = pKeys + idx * keySize;
        u8 random = pKey[2];
        u8* pValue = pKey + 12;

        if (random == 0) {
            for (; mask != 0; mask >>= 1) {
                if (enable & mask) {
                    *pTarget = *pValue++;
                }
                pTarget++;
            }
        } else {
            u32 rnd;
            u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                          rangeSize, random,
                                          *reinterpret_cast<u16*>(pValue),
                                          loop, seed, rnd);

            for (; mask != 0; mask >>= 1) {
                if (enable & mask) {
                    int value = RandomValueU8(pRange, rnd);
                    pRange += 2;
                    if (value < 0) {
                        value = 0;
                    }
                    if (value > 255) {
                        value = 255;
                    }
                    UpdateRandomU8(pHeader, rnd);
                    *pTarget = value;
                }
                pTarget++;
            }
        }
        return;
    }

    loop1 = loop;
    AdjustTurnLoop(pHeader, reinterpret_cast<AnimCurveKey*>(pKeyTbl), idx, loop, loop,
                   loop1);

    t = static_cast<u32>(65536.0f * (time - prevFrame)) /
            (nextFrame - prevFrame);

    pKey0 = pKeys + idx * keySize;
    pKey1 = pKeys + (idx + 1) * keySize;

    pValue0 = pKey0 + 12;
    pInterp = pKey0 + 4;
    pValue1 = pKey1 + 12;
    random0 = pKey0[2];
    isRandom0 = random0 != 0;
    isRandom1 = pKey1[2] != 0;

    if (!isRandom0 && !isRandom1) {
        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                u8 v0;
                u8 v1;
                v1 = *pValue1;
                v0 = *pValue0;
                if (v0 == v1) {
                    *pTarget = v0;
                } else {
                    *pTarget = InterpolateU8(*pInterp, t, v0, v1);
                }
                pValue0++;
                pValue1++;
            }
            pInterp++;
            pTarget++;
        }
    } else if (isRandom0 && !isRandom1) {
        u16 rangeIdx = *reinterpret_cast<u16*>(pValue0);
        u32 rnd;
        u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                      rangeSize, random0,
                                      rangeIdx, loop,
                                      seed, rnd);

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                int v0 = RandomValueU8(pRange, rnd);
                pRange += 2;
                if (v0 < 0) {
                    v0 = 0;
                }
                if (v0 > 255) {
                    v0 = 255;
                }
                UpdateRandomU8(pHeader, rnd);
                *pTarget = InterpolateU8(*pInterp, t, v0, *pValue1);
                pValue1++;
            }
            pInterp++;
            pTarget++;
        }
    } else if (!isRandom0 && isRandom1) {
        u32 rnd;
        u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                      rangeSize, pKey1[2],
                                      *reinterpret_cast<u16*>(pValue1), loop1,
                                      seed, rnd);

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                int v1 = RandomValueU8(pRange, rnd);
                pRange += 2;
                if (v1 < 0) {
                    v1 = 0;
                }
                if (v1 > 255) {
                    v1 = 255;
                }
                UpdateRandomU8(pHeader, rnd);
                *pTarget = InterpolateU8(*pInterp, t, *pValue0, v1);
                pValue0++;
            }
            pInterp++;
            pTarget++;
        }
    } else {
        u32 rnd0;
        rangeIdx0 = *reinterpret_cast<u16*>(pValue0);
        u8* pRange0 = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                       rangeSize, random0,
                                       rangeIdx0, loop,
                                       seed, rnd0);
        u32 rnd1;
        rangeIdx1 = *reinterpret_cast<u16*>(pValue1);
        u8* pRange1 = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                       rangeSize, pKey1[2],
                                       rangeIdx1, loop1,
                                       seed, rnd1);

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                int v0 = RandomValueU8(pRange0, rnd0);
                pRange0 += 2;
                if (v0 < 0) {
                    v0 = 0;
                }
                if (v0 > 255) {
                    v0 = 255;
                }
                UpdateRandomU8(pHeader, rnd0);
                int v1 = RandomValueU8(pRange1, rnd1);
                pRange1 += 2;
                if (v1 < 0) {
                    v1 = 0;
                }
                if (v1 > 255) {
                    v1 = 255;
                }
                UpdateRandomU8(pHeader, rnd1);
                *pTarget = InterpolateU8(*pInterp, t, v0, v1);
            }
            pInterp++;
            pTarget++;
        }
    }
}

// Random value in [base, base + width * 0xFFFF]
static inline f32 CalcRandomValue(f32 base, f32 width, u32 rnd) {
    return base + width * (rnd >> 16);
}

// The random value of one component's range (base, width)
static inline f32 RandomValueF32(const f32* pRange, u32 rnd) {
    return CalcRandomValue(pRange[0], pRange[1], rnd);
}

static inline f32 CalcRandomF32(AnimCurveHeader* pHeader, f32*& rpRange,
                                u32& rRnd) {
    f32 value = CalcRandomValue(rpRange[0], rpRange[1], rRnd);
    rpRange += 2;

    if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
        rRnd = MixRandom(rRnd);
    }

    return value;
}

static inline f32 InterpolateF32(u8 interp, f32 t, f32 v0, f32 v1) {
    switch (interp & 3) {
    case INTERP_LINEAR: {
        return v0 + t * (v1 - v0);
    }
    case INTERP_SMOOTH: {
        if (interp == 1) {
            return v0 + t * (t * ((3.0f - 2.0f * t) * (v1 - v0)));
        }

        f32 easeOut;
        f32 easeIn;
        if ((interp >> 2 & 1) == 0) {
            easeIn = 0.0f;
        } else {
            easeIn = 1.5f;
        }
        if ((interp >> 3 & 1) == 0) {
            easeOut = 0.0f;
        } else {
            easeOut = 1.5f;
        }

        return v0 + t * (v1 - v0) *
                        (easeOut + (t * (t * (easeOut + easeIn - 2.0f)) +
                                    t * (3.0f + (-2.0f * easeOut - easeIn))));
    }
    case INTERP_STEP: {
        return v0;
    }
    default: {
        return 0.0f;
    }
    }
}

static inline f32 InterpolateF32Eq(u8 interp, f32 t, f32 v0, f32 v1) {
    if (v0 == v1) {
        return v0;
    }
    return InterpolateF32(interp, t, v0, v1);
}

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING (98.89%, 31 differences): the callee-saved registers, every random loop, the
// seed blocks, num/keySize/pKeys and the setup's random0/random1/isRandom1 match;
// what is left is SearchKeyStride's exact/lo (r28/r22 here, r22/r28 in the original) and
// pKey0/pKey1 (r10/r12 here, r12/r9 in the original, and the post-RA schedule of the pKey0/pKey1
// setup that follows from it).
// 2026-10-29k: seed add order fixed in CalcRandomSeed (forecast's flat sum, u32 rangeIdx).
// From doldecomp/forecast and wii-news-channel (later nw4r, different F32 body): their
// SearchKey shapes (initialised declarations, `hi = count; hi--;`, exact first) are worse
// (72-440 unit words vs 58); pKey0/pValue0/pKey1/pValue1 interleaved setup and random flags read
// directly from pKey[2]: identical or worse.
// 2026-10-29j: the seed add order is shared with U8/Rotate (see CalcRandomSeed); the setup
// block is post-RA scheduling forced by the pKey colouring (U8's note).
// 2026-10-29i (37 -> 33): the exact path's random value from RandomValueF32(pRange, rnd), the
// pointer-taking inline over CalcRandomValue that Rotate's loops use (width f2, base f3 as in the
// original; CalcRandomValue(pRange[0], pRange[1], rnd) directly swaps them). Worse: CalcRandomF32
// built on RandomValueF32 (+16 words); same: it on CalcRandomValue (kept).
// 2026-10-29h (pairwise sim.py search, no gain): every pair of declaration moves over the
// named locals (vregs 37..77, block-scope rnd/pRange/pValue/random/pKey hoisted included, ~1.4M
// replays) never flips exact/lo (@6359/@6358) and never gives pKey0 r12 and pKey1 r9 together.
// pKey0 r12 alone needs pInterp numbered above pValue1 (pInterp/pValue1 then swap r9/r10;
// compiled: 37 -> 56); pKey1 r9 alone only with random0 hoisted below the block locals and 38
// other changes. So these are not caller declaration order.
// 2026-10-29g (shared-helper study, no gain): exact is SearchKeyStride's getcrbit node (@ of
// the inline local exact) and lo its `li 0` node; both are simplify level 1, so the higher vreg
// is coloured first. F32's original needs exact above lo, U8 and Rotate need lo above exact
// (exact r7/lo r6 in U8, r11/r10 in Rotate). sim.py on a lo-first U8 trace: only moving exact
// below lo fixes it, no caller renumbering does; adding up to two interference edges to F32's
// exact does not flip it either. Helper shapes tried (all callers): exact as bool& out-param
// (void or bool return: identical code, the caller's exact is propagated away and its
// declaration slot has no effect, 36 slots), idx returned with exact out (worse), pKeys read
// inside the helper / count passed instead of the table / u32 or const int& keySize (identical
// or worse), rIdx as the lower bound (worse), exact computed by the caller and passed by value
// (fabs scheduled before the search: far worse), while as for, operand orders, initialised
// declarations (worse). So no single helper body gives both numberings; an F32-only lo-first copy
// would give F32 -14 but there is no code-shape evidence for a second helper (not taken).
// 2026-10-29f (202 -> 37): laid out from a sim.py replay of the trace (target registers mapped
// per vreg): a function-scope u16 rangeIdx assigned in the two single-random branches (the
// isRandom0 branch's load is its first web, numbered below random0; the isRandom1 branch's load
// is a late web), rangeIdx0/rangeIdx1 for the both-random branch declared just before rnd0
// (numbered right above rnd0: rnd0 r11, rnd1 r4, rangeIdx1 r6 as in the original), and random1
// declared between nextFrame and loop1 (random1 r5, isRandom1 r11, random0 r4). Same layout as
// U8/Rotate's rangeIdx locals; the earlier rangeIdx tries were worse only because random1 stayed
// where it was. After it, no gain: rangeIdx types (int/u32) and declaration slots, setup
// statement order in eight interleavings (pKey1 after isRandom0, pInterp last, ...), pKey0/pKey1
// as &pKeys[...] / keySize * idx / (1 + idx), t after the pKeys; SearchKeyStride lo before exact
// (F32 -14 objcmp words, U8 +14, Rotate +12). sim.py: no renumbering gives pKey0 r12/pKey1 r9
// (our pre-RA order defines pKey1 before the isRandom0 `or`, so it interferes with the neg temp).
// 2026-10-29e (U8's two levers, no gain; objcmp words 191): InterpolateF32 with `const u8&
// interp` (alone or with const f32& t: identical code; with const f32& v0/v1 worse, Rotate too),
// CalcRandomF32 accumulating (`f32 value = rpRange[0]; value += ...`, +35), CalcRandomValue
// accumulating (+18, Rotate +19) or with const f32& base/width (+16..+25), the exact path's
// value accumulated in the loop (+3), const u32& rnd / const AnimCurveHeader* (identical);
// shared helpers by const& (GetRandomRangeU8's random, MixRandom's rnd: U8/Rotate much worse).
// The non-exact setup's statement order (pValue0/pValue1/pInterp/random*/isRandom* in every
// grouping) gives identical code. Left (trace): the both-random loop's rnd0 r6/rnd1 r5/pRange1 r9
// (original r11/r4/r7) and the seed blocks' volatile numbering.
// 2026-10-29 (314 -> 202): the two-random branch's rnd0/pRange0/rnd1/pRange1 declared at function
// scope and mask declared last, so mask is numbered below the block-scope rnd/pRange of the other
// branches and is coloured first (simplify level 3: r0 as in the original; a replay with
// tools/mwcc_trace/sim.py showed mask had to be numbered below the third branch's rnd). With that,
// rangeSize is a plain int again (the const int& matching idiom is no longer needed).
// Tried after it: single declaration moves of every local and keySize/pKeys swapped with moves of
// num/idx/prevFrame/exact (no gain; the sim agrees no renumbering fixes num), rangeSize inlined /
// after the search / from keySize (worse), lo before exact in SearchKeyStride (total 597 -> 609).
// 2026-10-29 (no gain, objcmp words 191): sim.py confirms exact/lo only flip when lo's inline
// temp is numbered below exact's (SearchKeyStride's declaration order: lo first gives F32 -14 but
// U8 +14, Rotate +12 at Rotate 61); in F32: `if (SearchKeyStride(...))`, exact at use / const /
// int / BOOL / u8, pKeyTbl->datas or num * 4 + 12 passed directly (worse), keySize/pKeys at use
// or reordered, keySize as sizeof/12 + /(num + 3) * 4 (no gain). sim.py: colouring @6434 (the
// third branch's rangeIdx lhz) in sweep 1 fixes num/keySize; named rangeIdx locals (int/u16,
// block or function scope, as in U8/Rotate) are worse (191 -> 240..487). The exact path's
// pRange[0]/pRange[1] FPRs follow inline order (CalcRandomF32 reverses them but stores after the
// SYNC test, +6); a direct expression is +3.
// 2026-10-29 (347 -> 314): CalcTime's life - 1 held in lastTick (declared at CalcTime's top,
// assigned at the fitting-loop test and passed to CalcFittingLoopTime): as a front-end value
// numbered below the flag temp it lets the flag drop to simplify level 1 (flag r22, tick r23,
// frameLength - 1 r28 as in the original); pKeyTbl declared right after enable (r26, above
// pRangeTbl); CalcRandomValue in the exact path's loop (the original stores before the SYNC
// test, so not CalcRandomF32 there) and CalcRandomF32 for both values in the two-random loop.
// Tried: SearchKeyStride declaration orders (exact before lo: F32 -14, U8 +14, Rotate +12),
// rangeSize declared at the top / inlined (worse), InterpolateF32Eq or a
// conditional in the no-random loop (no gain), a declaration hill-climb (local optimum).
// Earlier (425 -> 347): v0/v1 declared at function scope (before t, so t is coloured after
// them: f2 as in the original), InterpolateF32's easeIn/easeOut by if/else (easeOut declared
// first). Kept: declaration order from a hill-climb.
void AnimCurveExecuteF32(u8* pCmdList, f32* pTarget, u32 tick, u16 seed,
                         u32 life) {
    u8 enable;
    AnimCurveTable<u8>* pKeyTbl;
    f32 time;
    int num;
    AnimCurveTable<u8>* pRangeTbl;
    bool exact;
    u8* pKey1;
    AnimCurveTable<u8>* pRandomTbl;
    u32 loop;
    u8 random0;
    u16 frame;
    AnimCurveHeader* pHeader;
    u8* pKey0;
    u16 nextFrame;
    u8 random1;
    u32 loop1;
    bool isRandom1;
    f32* pValue0;
    f32 v0;
    f32 v1;
    f32 t;
    f32* pValue1;
    bool isRandom0;
    u8* pInterp;
    u8* pKeys;
    int keySize;
    int idx;
    u16 prevFrame;
    u16 rangeIdx;
    u16 rangeIdx0;
    u16 rangeIdx1;
    u32 rnd0;
    f32* pRange0;
    u32 rnd1;
    f32* pRange1;
    u8 mask;
    pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);

    pKeyTbl =
        reinterpret_cast<AnimCurveTable<u8>*>(pCmdList + sizeof(AnimCurveHeader));
    pRangeTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pKeyTbl) + pHeader->keyTable);
    pRandomTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pRangeTbl) + pHeader->rangeTable);

    enable = pHeader->kindEnable;

    time = CalcTime(pHeader, tick, life, frame, loop);

    num = 8;
    for (mask = 0x80; mask != 0; mask >>= 1) {
        if (enable & mask) {
            break;
        }
        num--;
    }

    if (num == 0) {
        return;
    }

    keySize = num * 4 + 12;
    int rangeSize = num * 8;

    pKeys = pKeyTbl->datas;
    exact = SearchKeyStride(pKeyTbl, pKeys, keySize, frame, time, idx,
                                 prevFrame, nextFrame);

    if (exact) {
        u8* pKey = pKeys + idx * keySize;
        u8 random = pKey[2];
        f32* pValue = reinterpret_cast<f32*>(pKey + 12);

        if (random == 0) {
            for (; mask != 0; mask >>= 1) {
                if (enable & mask) {
                    *pTarget = *pValue;
                }
                pValue++;
                pTarget++;
            }
        } else {
            u32 rnd;
            f32* pRange = reinterpret_cast<f32*>(GetRandomRangeU8(
                pHeader, pRangeTbl, pRandomTbl, rangeSize, random,
                *reinterpret_cast<u16*>(pValue), loop, seed, rnd));

            for (; mask != 0; mask >>= 1) {
                if (enable & mask) {
                    *pTarget = RandomValueF32(pRange, rnd);
                    pRange += 2;
                    if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                        rnd = MixRandom(rnd);
                    }
                }
                pTarget++;
            }
        }
        return;
    }

    loop1 = loop;
    AdjustTurnLoop(pHeader, reinterpret_cast<AnimCurveKey*>(pKeyTbl), idx, loop,
                   loop, loop1);

    t = (time - prevFrame) / (nextFrame - prevFrame);

    pKey0 = pKeys + idx * keySize;
    pKey1 = pKeys + (idx + 1) * keySize;

    pValue0 = reinterpret_cast<f32*>(pKey0 + 12);
    pInterp = pKey0 + 4;
    pValue1 = reinterpret_cast<f32*>(pKey1 + 12);
    random0 = pKey0[2];
    random1 = pKey1[2];
    isRandom0 = random0 != 0;
    isRandom1 = random1 != 0;

    if (!isRandom0 && !isRandom1) {
        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                v0 = *pValue0;
                v1 = *pValue1;
                f32 value;
                if (v0 == v1) {
                    value = v0;
                } else {
                    value = InterpolateF32(*pInterp, t, v0, v1);
                }
                *pTarget = value;
                pValue0++;
                pValue1++;
            }
            pInterp++;
            pTarget++;
        }
    } else if (isRandom0 && !isRandom1) {
        u32 rnd;
        rangeIdx = *reinterpret_cast<u16*>(pValue0);
        f32* pRange = reinterpret_cast<f32*>(GetRandomRangeU8(
            pHeader, pRangeTbl, pRandomTbl, rangeSize, random0,
            rangeIdx, loop, seed, rnd));

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                v0 = CalcRandomF32(pHeader, pRange, rnd);
                *pTarget = InterpolateF32(*pInterp, t, v0, *pValue1);
                pValue1++;
            }
            pInterp++;
            pTarget++;
        }
    } else if (!isRandom0 && isRandom1) {
        u32 rnd;
        rangeIdx = *reinterpret_cast<u16*>(pValue1);
        f32* pRange = reinterpret_cast<f32*>(GetRandomRangeU8(
            pHeader, pRangeTbl, pRandomTbl, rangeSize, random1,
            rangeIdx, loop1, seed, rnd));

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                v1 = CalcRandomF32(pHeader, pRange, rnd);
                *pTarget = InterpolateF32(*pInterp, t, *pValue0, v1);
                pValue0++;
            }
            pInterp++;
            pTarget++;
        }
    } else {
        rangeIdx0 = *reinterpret_cast<u16*>(pValue0);
        pRange0 = reinterpret_cast<f32*>(GetRandomRangeU8(
            pHeader, pRangeTbl, pRandomTbl, rangeSize, random0,
            rangeIdx0, loop, seed, rnd0));
        rangeIdx1 = *reinterpret_cast<u16*>(pValue1);
        pRange1 = reinterpret_cast<f32*>(GetRandomRangeU8(
            pHeader, pRangeTbl, pRandomTbl, rangeSize, random1,
            rangeIdx1, loop1, seed, rnd1));

        for (; mask != 0; mask >>= 1) {
            if (enable & mask) {
                v0 = CalcRandomF32(pHeader, pRange0, rnd0);
                v1 = CalcRandomF32(pHeader, pRange1, rnd1);
                *pTarget = InterpolateF32(*pInterp, t, v0, v1);
            }
            pInterp++;
            pTarget++;
        }
    }
}

enum {
    ROTATE_KEY_SIZE = 24,
    ROTATE_RANGE_SIZE = 28,
};

// Rotation ranges can pick a random sign, written to the caller's bool (its named local:
// see AnimCurveExecuteRotate). rRnd advances when they do.
static inline void GetRandomSign(u8* pRange, u32& rRnd, bool& rNegate) {
    rNegate = false;

    if (pRange[24]) {
        if (!(rRnd & 0x10000)) {
            rNegate = true;
        }
        rRnd = MixRandom(rRnd);
    }
}

static inline f32 CalcRandomRotate(f32* pRange, int i, u32 rnd, bool negate) {
    f32 value = CalcRandomValue(pRange[i * 2], pRange[i * 2 + 1], rnd);
    if (negate) {
        value = -value;
    }
    return value;
}

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING (98.78%, 18 differences): prologue, CalcTime, SearchKeyStride, all callee-saved
// registers, the FPRs, the rangeIdx loads, the seed sums and every random loop match. Left: the
// pKey0/pKey1 setup (pKey1 r11, original r7; the isRandom0/1 neg/or temps r6/r6, r7/r9,
// original r3/r9, r9/r9). 2026-10-29k: seed adds fixed in CalcRandomSeed (forecast's shape);
// random flags read directly from pKey[2] (forecast): identical or worse.
// 2026-10-29j: the seed adds are shared with U8/F32 (see CalcRandomSeed); the setup order
// is post-RA scheduling forced by the colouring (U8's note); random0/random1 as int/u32: no effect.
// 2026-10-29i (48 -> 20, unit 107 -> 75): the random loops written like U8's: the value from
// RandomValueF32(pRangeValue, rnd) (a pointer-taking inline over CalcRandomValue: base/width
// FPRs f6/f4 as in the original; CalcRandomValue(pRangeValue[0], pRangeValue[1], rnd) directly
// swaps them), then `pRangeValue += 2`, then the negate test at the site (the original's add
// comes right after the loads; inside CalcRandomRotate it was scheduled after the fneg); in the
// both-random loop each value is built, advanced and negated in turn (-2 words; both negates
// after both values: +128). GetRandomSign writes the caller's bool through a reference
// (`bool negate; GetRandomSign(pRange, rnd, negate);`, every call site): sim.py on a fresh
// trace showed negate (an inline temp before) had to be numbered below pRangeValue in both
// single-random branches, with no side effects; as the caller's named local declared after
// pRangeValue it is (r9, pRangeValue r6/r7 as in the original, -23 words). Same score: negate's
// test spelled out in the single branches. No effect: negate/pRangeValue declared at function
// scope (15 slots each) or swapped, bool return (the inline's local is propagated into the
// caller's). Worse: RandomValueF32 inside CalcRandomRotate for the exact path (+20).
// 2026-10-29h (71 -> 48): pKeys declared where idx was and idx where pKeys was (last). sim.py on a
// fresh trace: no single renumbering works because two orderings are needed at once: pKeys
// numbered above loop1 (coloured first: r3) and idx numbered below loop1 (coloured after loop1
// r5 and pKeys r3, loop r4: r6); {pKeys:66.5, idx:65.5} gave exactly the original. The pKey setup
// is decided by the pre-RA scheduler (the pcode shows the random loads and the neg/or of
// isRandom0 interleaved with the pKey1 add whatever the statement order): the original has pKey1
// computed before isRandom0's neg (that temp is r3, so pKeys is dead) and pInterp/pValue1 before
// isRandom1's neg (r9). No effect after the swap: every statement order of the pKey0/pKey1/
// random/pValue/pInterp setup, isRandom0/1 next to their loads; worse: the isRandom flags
// replaced by random0/random1 tests (48 words), `pRangeValue += 2` after the Interpolate store
// (40); no effect: it at the end of the loop body.
// Before 2026-10-29h: volatile colouring inside SearchKeyStride (idx/hi and pKeys swapped r3/r6)
// and what followed from it; 2026-10-29g: no single renumbering of any vreg fixed it.
// 2026-10-29 (241 -> 71 by cmp, 228 -> 61 words): function-scope u16 rangeIdx (single-random
// branches) and rangeIdx0/rangeIdx1 (both-random branch). CW splits a variable's live ranges:
// the first range keeps the declaration's number, later ranges get late temp numbers, so the
// isRandom0 load and the both branch's loads must each be the first range of a function-scope
// variable (sim.py: numbered between pValue1 and random1) and the isRandom1 load may be a late
// range; rangeIdx0/1 shared with the single branches were 214 words, block locals worse. Decl
// position of the three (after random1 .. after pKey1) and their type (int/u32) no effect.
// No effect after that: single moves of pKeys/idx/pKey0/pKey1/rangeIdx* through every
// declaration slot (189 variants), loop pRangeValue indexed by i (worse), CalcRandomValue
// spelled out in the loops, the both branch's two loads together at its top (worse).
// Earlier notes (before rangeIdx): U8's rangeIdx locals (isRandom0
// and both branches) give random0 r0 here too but move pValue0 to r3 (original r6), 229 -> 277
// words; sim.py says rangeIdx1 must also be numbered above rnd0 and random1 be a late temp
// (pKey1[2] read directly): with random1 direct and the rangeIdx decls at the top of the both
// block 262-269 words, all worse than the pointer form (kept). Declaration order from a hill-climb
// (474 -> 330; at a local optimum again after the changes below).
// 2026-10-29 (330 -> 245): the random value built by CalcRandomValue(base, width, rnd) (its
// by-value parameters are created before the conversion, so the conversion is coloured first:
// f0, then the constant f1, width f2, base f3 as in the original), CalcRandomRotate also in the
// loops, v0/v1 declared at function scope before t (t f2, v0 f1 as in the original).
// No effect or worse: statement order of the pKey0/pKey1 setup, function-scope loop counter,
// InterpolateF32Eq, pKeys assigned earlier, as a const pointer or as pKeyTbl->datas, random0 or
// random1 read directly from the key / through an accessor / as const int&, function-scope rnd,
// the random parameter's type, if/else ease without easeOut first (worse here), the
// commutative order of base + width * r.
// No effect (2026-10-29, F32's scope lever): each branch's block locals (exact, isRandom0,
// isRandom1, both; singly, the three rnd/pRange/negate/pRangeValue branches shared, all) moved to
// function scope at the top or bottom of the declarations, with random0 swept through every
// position and single top/bottom moves of every local: 228 words or worse (random0 last 281).
void AnimCurveExecuteRotate(u8* pCmdList, f32* pTarget, u32 tick, u16 seed,
                            u32 life) {
    u8 random0;
    AnimCurveHeader* pHeader;
    AnimCurveTable<u8>* pKeyTbl;
    bool isRandom1;
    AnimCurveTable<u8>* pRangeTbl;
    u32 loop;
    AnimCurveTable<u8>* pRandomTbl;
    bool exact;
    u8* pKeys;
    u16 frame;
    f32 time;
    bool isRandom0;
    u32 loop1;
    u8 random1;
    u16 rangeIdx;
    u16 rangeIdx0;
    u16 rangeIdx1;
    u16 prevFrame;
    f32 v0;
    f32 v1;
    f32 t;
    u8* pKey0;
    u8* pKey1;
    f32* pValue1;
    u8* pInterp;
    u16 nextFrame;
    f32* pValue0;
    int idx;
    pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);

    pKeyTbl =
        reinterpret_cast<AnimCurveTable<u8>*>(pCmdList + sizeof(AnimCurveHeader));
    pRangeTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pKeyTbl) + pHeader->keyTable);
    pRandomTbl = reinterpret_cast<AnimCurveTable<u8>*>(
        reinterpret_cast<u8*>(pRangeTbl) + pHeader->rangeTable);

    time = CalcTime(pHeader, tick, life, frame, loop);

    pKeys = pKeyTbl->datas;
    exact = SearchKeyStride(pKeyTbl, pKeys, ROTATE_KEY_SIZE, frame, time,
                                 idx, prevFrame, nextFrame);

    if (exact) {
        u8* pKey = pKeys + idx * ROTATE_KEY_SIZE;
        u8 random = pKey[2];
        f32* pValue = reinterpret_cast<f32*>(pKey + 12);

        if (random == 0) {
            for (int i = 0; i < 3; i++) {
                pTarget[i] = pValue[i];
            }
        } else {
            u32 rnd;
            u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                          ROTATE_RANGE_SIZE, random,
                                          *reinterpret_cast<u16*>(pValue),
                                          loop, seed, rnd);
            bool negate;
            GetRandomSign(pRange, rnd, negate);

            f32* pRangeValue = reinterpret_cast<f32*>(pRange);
            pTarget[0] = CalcRandomRotate(pRangeValue, 0, rnd, negate);
            if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                rnd = MixRandom(rnd);
            }
            pTarget[1] = CalcRandomRotate(pRangeValue, 1, rnd, negate);
            if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                rnd = MixRandom(rnd);
            }
            pTarget[2] = CalcRandomRotate(pRangeValue, 2, rnd, negate);
        }
        return;
    }

    loop1 = loop;
    AdjustTurnLoop(pHeader, reinterpret_cast<AnimCurveKey*>(pKeyTbl), idx, loop,
                   loop, loop1);

    t = (time - prevFrame) / (nextFrame - prevFrame);

    pKey0 = pKeys + idx * ROTATE_KEY_SIZE;
    pKey1 = pKeys + (idx + 1) * ROTATE_KEY_SIZE;

    pValue0 = reinterpret_cast<f32*>(pKey0 + 12);
    pInterp = pKey0 + 4;
    pValue1 = reinterpret_cast<f32*>(pKey1 + 12);
    random0 = pKey0[2];
    random1 = pKey1[2];
    isRandom0 = random0 != 0;
    isRandom1 = random1 != 0;

    if (!isRandom0 && !isRandom1) {
        for (int i = 0; i < 3; i++) {
            v0 = *pValue0;
            v1 = *pValue1;
            f32 value;
            if (v0 == v1) {
                value = v0;
            } else {
                value = InterpolateF32(*pInterp, t, v0, v1);
            }
            *pTarget = value;
            pTarget++;
            pValue0++;
            pValue1++;
            pInterp++;
        }
    } else if (isRandom0 && !isRandom1) {
        u32 rnd;
        rangeIdx = *reinterpret_cast<u16*>(pValue0);
        u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                      ROTATE_RANGE_SIZE, random0,
                                      rangeIdx, loop,
                                      seed, rnd);
        f32* pRangeValue = reinterpret_cast<f32*>(pRange);
        bool negate;
        GetRandomSign(pRange, rnd, negate);

        for (int i = 0; i < 3; i++) {
            v0 = RandomValueF32(pRangeValue, rnd);
            pRangeValue += 2;
            if (negate) {
                v0 = -v0;
            }
            *pTarget = InterpolateF32(*pInterp, t, v0, *pValue1);
            pTarget++;
            pInterp++;
            if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                rnd = MixRandom(rnd);
            }
            pValue1++;
        }
    } else if (!isRandom0 && isRandom1) {
        u32 rnd;
        rangeIdx = *reinterpret_cast<u16*>(pValue1);
        u8* pRange = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                      ROTATE_RANGE_SIZE, random1,
                                      rangeIdx, loop1,
                                      seed, rnd);
        f32* pRangeValue = reinterpret_cast<f32*>(pRange);
        bool negate;
        GetRandomSign(pRange, rnd, negate);

        for (int i = 0; i < 3; i++) {
            v1 = RandomValueF32(pRangeValue, rnd);
            pRangeValue += 2;
            if (negate) {
                v1 = -v1;
            }
            *pTarget = InterpolateF32(*pInterp, t, *pValue0, v1);
            pTarget++;
            pInterp++;
            pValue0++;
            if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                rnd = MixRandom(rnd);
            }
        }
    } else {
        rangeIdx0 = *reinterpret_cast<u16*>(pValue0);
        u32 rnd0;
        u8* pRange0 = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                       ROTATE_RANGE_SIZE, random0,
                                       rangeIdx0, loop,
                                       seed, rnd0);
        u32 rnd1;
        rangeIdx1 = *reinterpret_cast<u16*>(pValue1);
        u8* pRange1 = GetRandomRangeU8(pHeader, pRangeTbl, pRandomTbl,
                                       ROTATE_RANGE_SIZE, random1,
                                       rangeIdx1, loop1,
                                       seed, rnd1);
        bool negate0;
        GetRandomSign(pRange0, rnd0, negate0);
        bool negate1;
        GetRandomSign(pRange1, rnd1, negate1);
        f32* pRangeValue0 = reinterpret_cast<f32*>(pRange0);
        f32* pRangeValue1 = reinterpret_cast<f32*>(pRange1);

        for (int i = 0; i < 3; i++) {
            v0 = RandomValueF32(pRangeValue0, rnd0);
            pRangeValue0 += 2;
            if (negate0) {
                v0 = -v0;
            }
            v1 = RandomValueF32(pRangeValue1, rnd1);
            pRangeValue1 += 2;
            if (negate1) {
                v1 = -v1;
            }
            *pTarget = InterpolateF32(*pInterp, t, v0, v1);
            pTarget++;
            pInterp++;
            if (!(pHeader->processFlag & AnimCurveHeader::PROC_FLAG_SYNC)) {
                rnd0 = MixRandom(rnd0);
                rnd1 = MixRandom(rnd1);
            }
        }
    }
}

void AnimCurveExecuteTexture(u8* pCmdList, Particle* pParticle, u32 tick,
                             u16 seed, u32 life) {
    AnimCurveHeader* pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);

    AnimCurveTable<AnimCurveKeyTexture>* pKeyTbl =
        reinterpret_cast<AnimCurveTable<AnimCurveKeyTexture>*>(
            reinterpret_cast<u8*>(pHeader) + sizeof(AnimCurveHeader));
    AnimCurveTable<AnimCurveRangeTexture>* pRangeTbl =
        reinterpret_cast<AnimCurveTable<AnimCurveRangeTexture>*>(
            reinterpret_cast<u8*>(pKeyTbl) + pHeader->keyTable);
    AnimCurveTable<AnimCurveRandomTexture>* pRandomTbl =
        reinterpret_cast<AnimCurveTable<AnimCurveRandomTexture>*>(
            reinterpret_cast<u8*>(pRangeTbl) + pHeader->rangeTable);
    AnimCurveTable<RES_PTR(TextureData)>* pNameTbl =
        reinterpret_cast<AnimCurveTable<RES_PTR(TextureData)>*>(
            reinterpret_cast<u8*>(pRandomTbl) + pHeader->randomTable);

    u32 loop;
    bool end;
    u16 frame;
    int layer;
    TextureData** ppTexture;
    AnimCurveKeyTexture* pKeys;
    AnimCurveKeyTexture* pKey;
    u16 name;
    u8 wrap;
    u8 reverse;

    CalcFrameStep(reinterpret_cast<u8*>(pHeader), tick, life, frame, loop, end);


    switch (pHeader->kindType) {
    case AC_TARGET_TEXTURE1: {
        ppTexture = &pParticle->mParameter.mTexture[TEX_LAYER_1];
        layer = TEX_LAYER_1;
        break;
    }
    case AC_TARGET_TEXTURE2: {
        ppTexture = &pParticle->mParameter.mTexture[TEX_LAYER_2];
        layer = TEX_LAYER_2;
        break;
    }
    case AC_TARGET_TEXTUREIND: {
        ppTexture = &pParticle->mParameter.mTexture[TEX_LAYER_IND];
        layer = TEX_LAYER_IND;
        break;
    }
    default: {
        return;
    }
    }

    pKeys = pKeyTbl->datas;
    pKey =
        &pKeys[SearchKeyStep(pKeys, pKeyTbl->count, frame, false)];


    if (pKey->random == 0) {
        name = pKey->name;
        wrap = pKey->wrap;
        reverse = pKey->reverse;
    } else {
        u16 rangeIdx = pKey->rangeIdx;
        u32 rnd = CalcRandomSeed(seed, pHeader, loop, rangeIdx);

        if (!(pKey->random & KEY_RANDOM_FLAG_TABLE)) {
            AnimCurveRangeTexture* pRange = &pRangeTbl->datas[rangeIdx];

            name = pRange->name;
            wrap = pRange->wrap;
            reverse = pRange->reverse;

            switch (pRange->randomReverse) {
            case 1: {
                reverse = (reverse & 2) | ((rnd >> 16) & 1);
                break;
            }
            case 2: {
                reverse = (reverse & 1) | ((rnd >> 16) & 2);
                break;
            }
            case 3: {
                reverse = (rnd >> 16) & 3;
                break;
            }
            }
        } else {
            AnimCurveRandomTexture* pRandom =
                &pRandomTbl->datas[(rnd >> 16) % pRandomTbl->count];

            name = pRandom->name;
            wrap = pRandom->wrap;
            reverse = pRandom->reverse;
        }
    }

    *ppTexture = pNameTbl->datas[name];

    pParticle->mParameter.mTextureWrap =
        static_cast<u16>(pParticle->mParameter.mTextureWrap & ~(0xF << layer * 4)) |
        ((wrap & 0xF) << layer * 4);
    pParticle->mParameter.mTextureReverse =
        static_cast<u8>(pParticle->mParameter.mTextureReverse & ~(3 << layer * 2)) |
        ((reverse & 3) << layer * 2);
}

void createChild(u8* pKeyData, u16 seed, AnimCurveHeader* pHeader,
                 AnimCurveNameTable* pNameTbl,
                 AnimCurveRandomTable* pRandomTbl, Particle* pParticle,
                 u32 loop) {
    AnimCurveKeyChild* pKey = reinterpret_cast<AnimCurveKeyChild*>(pKeyData);
    AnimCurveChild* pChild;

    if (pKey->random == 0) {
        pChild = &pKey->child;
    } else {
        union {
            u32 value;
            u8 bytes[4];
        } rndSeed;

        u32 rangeIdx = pKey->rangeIdx;
        rndSeed.value = seed * 0x3F81F635 + pHeader->randomSeed * 0x30A74193 +
                        rangeIdx * 0x371097E7 + loop * 0x7B929 + 0x4BF53;

        rndSeed.bytes[2] ^= rndSeed.bytes[3];
        rndSeed.bytes[1] ^= rndSeed.bytes[2];
        rndSeed.bytes[0] ^= rndSeed.bytes[1];

        // @bug pChild is left unset for keys with a random range
        if (pKey->random & KEY_RANDOM_FLAG_TABLE) {
            u16 count = pRandomTbl->count;
            if (count == 0) {
                return;
            }

            pChild = &reinterpret_cast<AnimCurveChild*>(
                pRandomTbl->datas)[(rndSeed.value >> 16) % count];
        }
    }

    EmitterResource* pResource = PORT_U32_TO_PTR(EmitterResource*,
        pNameTbl->datas[pChild->resIdx].work);

    if (pResource == NULL) {
        return;
    }

    if (pChild->setting.type == 0) {
        pParticle->mParticleManager->mManagerEM->mManagerEF->mManagerES
            ->mCreationQueue.AddParticleCreation(&pChild->setting, pParticle,
                                                 pResource,
                                                 pParticle->mCalcRemain);
    } else {
        pParticle->mParticleManager->mManagerEM->mManagerEF->mManagerES
            ->mCreationQueue.AddEmitterCreation(&pChild->setting, pParticle,
                                                pResource,
                                                pParticle->mCalcRemain);
    }
}

// FUNCTIONAL (matching abandoned, user decision 2026-10-29): behaviour equals the original; the
// differences below are register allocation / instruction order only (docs/NONMATCHING.md).
// NONMATCHING (99.75%, 4 differences): frame1's u16 narrowing for frame0 == frame1
// is hoisted into the loop0 == loop1 block in the original (the int->float scratch-slot
// difference is fixed by the dead tick conversion in CalcFrameStep). Registers otherwise match
// (mwcc_trace): pHeader is its own node (every use goes through pHeader, so the parameter is not
// coalesced into it), frame0/frame1 are named locals (CalcFrameStep writes the frame through a
// reference), end is set inside CalcFrameStep, and the loop counters/key pointers are
// function-scope locals that CodeWarrior splits per loop, which numbers them after the tables.
// Tried for (2): pKeys before the second call, frame/loop declaration order, if (end)/else,
// split frame0 == frame1 tests, frame1 == frame0, loop1 == loop0, a u16-returning CalcFrameStep,
// if (end) frame1 = 1; else return;, frame1++, pKeys inside each block.
// --pcode/--sched (2026-10-29): our narrowings are created by codegen per compare (B85 holds
// rlwinm frame0 and rlwinm frame1 for frame0 == frame1, B88 two new ones for frame0 < frame1); the
// loop0 == loop1 block (cmplw, addi pKeys, bf) is scheduled on its own, so the original's
// clrlwi r3,r25 (kept in r3 and used by the B85 compare) was emitted in that block by codegen, not
// moved by the scheduler. Front end (final IR): node 85 = pKeys and loop0 == loop1, node 86 =
// EEQU frame0 frame1 with no conversion. No effect: the shared end for both calls, (int) casts on
// the compare, if (end) frame1++, a u16-returning wrapper around CalcFrameStep for either call;
// testing loop0 == loop1 && frame0 == frame1 first is structural (7 words, 2 extra).
// 2026-10-29f, no gain: frame1 as an int from a u16-returning CalcFrameStepFrame wrapper for the
// second call (int or u32 frame1: +68 words), frame1 - frame0 == 0 / frame0 - frame1 < 0 /
// frame1 - frame0 > 0 (+1..+2), frame1 == frame0 with frame1 > frame0 (+1), `frame0 - frame1 == 0`
// (-1 word: frame1's clrlwi then frame0's, registers as in the original, but a subf. in the inner
// block instead of the hoisted clrlwi + cmplw; not natural, not taken), if (frame0 != frame1) {}
// else if (!end) ..., frame0 == frame1 && !end then a second test, frame1++ / frame1 = frame0 + 1,
// loop1 == loop0, the turn path as an else-if, pKeys assigned after the frame test (+170).
// 2026-10-29k, from doldecomp/forecast and wii-news-channel (same Child size 0xDA0; their frame
// logic is the same as ours): a u16-returning CalcFrameStep (forecast's, multi-return or
// single-exit like wnc's) +209 words; rEnd reset left to the caller (`end = false` before the
// calls, forecast) +1; caller-initialised loop0/loop1/end/end1 with our helper: identical;
// forecast's pKey pointer loops in the loop0 == loop1 block: +1..+2.
void AnimCurveExecuteChild(u8* pCmdList, Particle* pParticle, u32 tick,
                           u16 seed, u32 life) {
    AnimCurveHeader* pHeader;
    AnimCurveTable<AnimCurveKeyChild>* pKeyTbl;
    u8* pRangeTbl;
    AnimCurveRandomTable* pRandomTbl;
    AnimCurveNameTable* pNameTbl;
    u16 frame0;
    u32 loop0;
    u16 frame1;
    u32 loop1;
    bool end1;
    AnimCurveKeyChild* pKeys;
    AnimCurveKeyChild* pKey;
    bool end;
    int i;

    pHeader = reinterpret_cast<AnimCurveHeader*>(pCmdList);

    pKeyTbl =
        reinterpret_cast<AnimCurveTable<AnimCurveKeyChild>*>(
            reinterpret_cast<u8*>(pHeader) + sizeof(AnimCurveHeader));
    pRangeTbl = reinterpret_cast<u8*>(pKeyTbl) + pHeader->keyTable;
    pRandomTbl =
        reinterpret_cast<AnimCurveRandomTable*>(pRangeTbl +
                                                pHeader->rangeTable);
    pNameTbl = reinterpret_cast<AnimCurveNameTable*>(
        reinterpret_cast<u8*>(pRandomTbl) + pHeader->randomTable);

    // Children are created for the keys passed between this tick and the
    // next one
    CalcFrameStep(reinterpret_cast<u8*>(pHeader), tick, life, frame0, loop0, end);
    CalcFrameStep(reinterpret_cast<u8*>(pHeader), tick + 1, life, frame1, loop1, end1);

    pKeys = pKeyTbl->datas;

    if (loop0 == loop1) {
        if (frame0 == frame1) {
            if (!end) {
                return;
            }
            frame1 = 1;
        }

        if (frame0 < frame1) {
            for (i = SearchKeyStep(pKeys, pKeyTbl->count, frame0, true); i < pKeyTbl->count; i++) {
                if (pKeys[i].frame >= frame0) {
                    if (frame1 <= pKeys[i].frame) {
                        return;
                    }

                    createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            }
        } else {
            for (i = SearchKeyStep(pKeys, pKeyTbl->count, frame0, false); i >= 0; i--) {
                if (frame0 >= pKeys[i].frame) {
                    if (pKeys[i].frame <= frame1) {
                        return;
                    }

                    createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            }
        }
        return;
    }

    if (pHeader->processFlag & AnimCurveHeader::PROC_FLAG_TURN) {
        // Rest of the current loop
        if (!(loop0 & 1)) {
            i = SearchKeyStep(pKeys, pKeyTbl->count, frame0, true);
            pKey = &pKeys[i];

            for (; i < pKeyTbl->count; pKey++, i++) {
                if (pKey->frame >= frame0) {
                    if (pKey->frame == pHeader->frameLength - 1) {
                        break;
                    }

                    createChild(reinterpret_cast<u8*>(pKey), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            }
        } else {
            for (i = SearchKeyStep(pKeys, pKeyTbl->count, frame0, false); i >= 0; i--) {
                if (frame0 >= pKeys[i].frame) {
                    if (pKeys[i].frame == 0) {
                        break;
                    }

                    createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            }
        }

        // Whole loops in between
        for (u32 loop = loop0 + 1; loop < loop1; loop++) {
            if (!(loop0 & 1)) {
                pKey = pKeys;

                for (i = 0; i < pKeyTbl->count; pKey++, i++) {
                    if (pKey->frame == pHeader->frameLength - 1) {
                        break;
                    }

                    createChild(reinterpret_cast<u8*>(pKey), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            } else {
                i = pKeyTbl->count - 1;
                pKey = &pKeys[i];

                for (; i >= 0; pKey--, i--) {
                    if (pKey->frame == 0) {
                        break;
                    }

                    createChild(reinterpret_cast<u8*>(pKey), seed, pHeader,
                                pNameTbl, pRandomTbl, pParticle, loop0);
                }
            }
        }

        // Start of the next loop
        if (!(loop1 & 1)) {
            for (i = 0; i < pKeyTbl->count; i++) {
                if (frame1 <= pKeys[i].frame) {
                    return;
                }

                createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader,
                            pNameTbl, pRandomTbl, pParticle, loop0);
            }
        } else {
            i = pKeyTbl->count - 1;
            pKey = &pKeys[i];

            for (; i >= 0; pKey--, i--) {
                if (frame1 >= pKey->frame) {
                    return;
                }

                createChild(reinterpret_cast<u8*>(pKey), seed, pHeader,
                            pNameTbl, pRandomTbl, pParticle, loop0);
            }
        }
    } else {
        for (i = SearchKeyStep(pKeys, pKeyTbl->count, frame0, true); i < pKeyTbl->count; i++) {
            if (pKeys[i].frame >= frame0) {
                createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader,
                            pNameTbl, pRandomTbl, pParticle, loop0);
            }
        }

        for (u32 loop = loop0 + 1; loop < loop1; loop++) {
            pKey = pKeys;

            for (i = 0; i < pKeyTbl->count; pKey++, i++) {
                createChild(reinterpret_cast<u8*>(pKey), seed, pHeader,
                            pNameTbl, pRandomTbl, pParticle, loop0);
            }
        }

        for (i = 0; i < pKeyTbl->count; i++) {
            if (frame1 <= pKeys[i].frame) {
                return;
            }

            createChild(reinterpret_cast<u8*>(&pKeys[i]), seed, pHeader, pNameTbl,
                        pRandomTbl, pParticle, loop0);
        }
    }
}


} // namespace ef
} // namespace nw4r
