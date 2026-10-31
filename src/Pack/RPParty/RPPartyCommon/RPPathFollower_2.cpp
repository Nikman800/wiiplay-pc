#include <Pack/RPParty/RPPartyCommon/RPPathFollower_2.h>

#if defined(PORT_HOST)
// The host's ABI returns the vector in registers, not through a hidden first argument
static inline void RPPathFollower2_MakeRot(EGG::Vector3f& rot, const EGG::Matrix34f& matrix) {
    rot = matrix.calcRPY();
}
#else
extern "C" void calcRPY__Q23EGG9Matrix34fCFv(EGG::Vector3f*, const EGG::Matrix34f*);

static inline void RPPathFollower2_MakeRot(EGG::Vector3f& rot, const EGG::Matrix34f& matrix) {
    calcRPY__Q23EGG9Matrix34fCFv(&rot, &matrix);
}
#endif

RPPathObject::RPPathObject(f32 speedPercent, f32 scalePercent, const void* pObjRes) {
    mpRes = NULL;
    mpFollower = NULL;
    mPos.x = 0.0f;
    mPos.y = 0.0f;
    mPos.z = 0.0f;
    mRot.x = 0.0f;
    mRot.y = 0.0f;
    mRot.z = 0.0f;
    mVec20.x = 0.0f;
    mVec20.y = 0.0f;
    mVec20.z = 1.0f;
    mSpeedPercent = speedPercent;
    mRadius = scalePercent;

    if (pObjRes != 0) {
        mpRes = pObjRes;
        if (mpRes != 0) {
            const be_f32* pFloats = static_cast<const be_f32*>(mpRes);
            EGG::Matrix34f matrix;
            EGG::Vector3f rot;
            matrix._00 = pFloats[0x0E];
            matrix._10 = pFloats[0x0F];
            matrix._20 = pFloats[0x10];
            matrix._01 = pFloats[0x0B];
            matrix._11 = pFloats[0x0C];
            matrix._21 = pFloats[0x0D];
            matrix._02 = pFloats[0x08];
            matrix._12 = pFloats[0x09];
            matrix._22 = pFloats[0x0A];

            const be_f32* pBeforeFloats = static_cast<const be_f32*>(mpRes);
            mPos.x = pBeforeFloats[0x02];
            mPos.y = pBeforeFloats[0x03];
            mPos.z = pBeforeFloats[0x04];
            RPPathFollower2_MakeRot(rot, matrix);
            mRot.x = rot.x;
            mRot.y = rot.y;
            mRot.z = rot.z;
            const be_f32* pAfterFloats = static_cast<const be_f32*>(mpRes);
            mVec20.x = pAfterFloats[0x08];
            mVec20.y = pAfterFloats[0x09];
            mVec20.z = pAfterFloats[0x0A];
            mRadius = scalePercent * pAfterFloats[0x05];

            if (static_cast<const RPResEntry*>(mpRes)->pPath != NULL) {
                mpFollower = new RPPathFollower(
                    speedPercent, scalePercent, static_cast<const RPResEntry*>(mpRes)->pPath);
            }
        }
    }

    if (mpFollower != 0) {
        mpFollower->ApplyPointParams();
    }
}

s32 RPPathObject::GetParam(u16 idx, s32 def) const {
    if (mpRes != NULL) {
        s32 param = static_cast<const RPResEntry*>(mpRes)->params[idx];
        if (param != 0) {
            def = param;
        }
        return def;
    }
    return 0;
}
