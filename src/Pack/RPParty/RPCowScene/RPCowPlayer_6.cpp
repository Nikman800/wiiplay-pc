// Charge! player file: unit 0x802A0EF0-0x802A64BC (dtk/game_units.tsv; the old RPCowPlayer_5
// range was merged in: both share one .sdata2 literal pool). Functions in reverse address
// order: the player (RPCowPlayer), its position rings and sounds first, the arrow
// (RPCowArrow) last.
#define RP_COW_PLAYER_SOUND_LATE
#include <Pack/RPParty/RPPartyCommon/RPCollisionModel.h>
#include <Pack/RPParty/RPCowScene/RPCowFieldObj.h>

#include <Pack/RPParty/RPCowScene/RPCowPlayer_6.h>

#include <Pack/RPAudio/RPSndAudioMgr.h>
#include <Pack/RPGraphics/RPGrpDrawPathManager.h>
#include <Pack/RPGraphics/RPGrpDrawPathShadowTexture.h>
#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPGraphics/RPGrpModelAnm.h>
#include <Pack/RPGraphics/RPGrpModelManager.h>
#include <Pack/RPGraphics/RPGrpShadowTexture.h>
#include <Pack/RPKernel/RPSysKokeshi.h>
#include <Pack/RPParty/RPCowScene/RPCowCowModel.h>
#include <Pack/RPParty/RPCowScene/RPCowCamera.h>
#include <Pack/RPKernel/RPSysController.h>
#include <Pack/RPParty/RPCowScene/RPCowField.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPCowScene/RPCowHud.h>
#include <Pack/RPParty/RPCowScene/RPCowKakasiGroup.h>
#include <Pack/RPParty/RPCowScene/RPCowPointPopup.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile.h>
#include <Pack/RPParty/RPPartyCommon/RPRumblePattern.h>
#include <Pack/RPParty/RPPartyCommon/RPTextWriter.h>

#include <nw4r/math.h>

// RPCowPlayerSound is declared after RPCowPlayer here (vtable order, see RPCowSound.h)
#undef RP_COW_PLAYER_SOUND_LATE
#include <Pack/RPParty/RPCowScene/RPCowSound.h>

extern "C" void RP_approachStep(f32* pValue, f32* pVel, f32 target, f32 rate);
extern "C" void RP_wrapAngleNear(f32* pAngle, f32 ref);
extern "C" void RP_CollisionShape_getMtx(EGG::Matrix34f* pOut, const RPCollisionShape* pShape);
extern "C" void RP_pushDebugGraphSamples(f32 a, f32 b, f32 c);
extern "C" void RP_debugLogPrintf(const wchar_t* pFormat, ...);
extern "C" void RP_drawDebugLogAndGraphs();
#if defined(PORT_HOST)
// PORT_HOST: RPCollisionShape.cpp defines these with the pointers first. The PowerPC ABI (and
// x86-64 System V) pass floats and pointers in separate registers, so the order did not
// matter; Windows x64 assigns registers by position. These wrappers keep this unit's calls
// and call the definitions' form (port/docs/HOST_BUILD.md §41).
extern "C" void RP_clampVec3Abs_Def(EGG::Vector3f*, f32) __asm__("RP_clampVec3Abs");
static inline void RP_clampVec3Abs(f32 abs, EGG::Vector3f* pVec) {
    RP_clampVec3Abs_Def(pVec, abs);
}
#else
extern "C" void RP_clampVec3Abs(f32 abs, EGG::Vector3f* pVec);
#endif
extern "C" void RP_CollisionShape_accumPosOffset(RPCollisionShape* pShape,
                                                 const EGG::Vector3f& rOfs);
extern "C" void RP_CollisionShape_accumRotOffset(RPCollisionShape* pShape,
                                                 const EGG::Vector3f& rOfs);
#if defined(PORT_HOST)
// PORT_HOST: as RP_clampVec3Abs above.
extern "C" bool RP_isVec3WithinAbs_Def(const EGG::Vector3f*, f32) __asm__("RP_isVec3WithinAbs");
static inline bool RP_isVec3WithinAbs(f32 abs, const EGG::Vector3f* pVec) {
    return RP_isVec3WithinAbs_Def(pVec, abs);
}
#else
extern "C" bool RP_isVec3WithinAbs(f32 abs, const EGG::Vector3f* pVec);
#endif
// EGG::ShadowTexture members missing from the vendored EGG header (called by their symbols)
// NONMATCHING-DEBT: including eggShadowTexture.h to call them reorders the .sdata2 pool and adds a local GXFastCallDisplayList.
extern "C" void BeginDrawQuad__Q23EGG13ShadowTextureFQ33EGG13ShadowTexture5Blend(int blend);
extern "C" void DrawQuad__Q23EGG13ShadowTextureCFRCQ34nw4r4math5MTX34RC8_GXColor(
    const EGG::ShadowTexture* pTexture, const nw4r::math::MTX34& rMtx, const GXColor& rColor);

namespace {
inline RPCowPlayer* GetPlayer(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowPlayer*>(pGame->GetSlotActor(7))[idx];
}
inline RPCowKakasiGroup* GetKakasiGroup(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowKakasiGroup*>(pGame->GetSlotActor(8))[idx];
}
inline bool IsPlaying(RPCowGame* pGame) {
    return pGame->GetState() == 7 || pGame->GetState() == 8;
}
inline RPCowCamera* GetCamera(RPCowGame* pGame, u16 idx) {
    return &static_cast<RPCowCamera*>(pGame->GetSlotActor(0))[idx];
}
inline void UpdateAngleDisp(RPCowPlayer* pPlayer, RPCowGame* pGame, const f32& disp, f32 angle,
                            f32 max) {
    RPCowAngleDisp* pDisp = static_cast<RPCowAngleDisp*>(pGame->GetSlotActor(28));
    pDisp->SetAngle(pPlayer->GetUnk1A(), disp);
    if (IsPlaying(pGame) && angle >= max) {
        pDisp->PlaySwitch(pPlayer->GetUnk1A());
    }
}
inline bool IsBeforePlay(RPCowGame* pGame) {
    // States 0, 1, 2, 4, 5, 6
    return (u32)pGame->GetState() <= 6 && ((1 << pGame->GetState()) & 0x77);
}
inline EGG::Matrix34f GetShapeMtx(const RPCollisionShape* pShape) {
    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, pShape);
    return mtx;
}
inline f32 Min(f32 a, f32 b) {
    return a < b ? a : b;
}
// States 9-24: goal and result sequence
inline bool IsResult(RPCowGame* pGame) {
    return (u32)(pGame->GetState() - 9) <= 15;
}
// States 9-14
inline bool IsGoalSequence(RPCowGame* pGame) {
    return (u32)(pGame->GetState() - 9) <= 5;
}
inline bool IsNonNegative(f32 x) {
    return x >= 0.0f;
}
inline bool IsPositive(f32 x) {
    return x > 0.0f;
}
inline f32 GetPushRate(s32 lead) {
    return lead * 0.1f + 0.2f;
}
inline bool IsBeforeStart(RPCowGame* pGame) {
    return pGame->GetState() == 0 || pGame->GetState() == 1;
}
}

extern "C" EGG::Vector3f Cow_Transform_getDelta(const RPCollisionShape* pShape);

void RPCowPlayer::SetArrowVisible(bool visible) {
    mArrow.mpModel->SetVisible(visible);
}

EGG::Vector3f RPCowPlayer::GetPos() const {
    return mpCollision->GetPos();
}

// Ledger name; RPCollisionShape's position change since the last ApplyOffsets [M]
#pragma auto_inline off
extern "C" EGG::Vector3f Cow_Transform_getDelta(const RPCollisionShape* pShape) {
    return pShape->GetCurrentPos() - pShape->GetPrevPos();
}
#pragma auto_inline reset

EGG::Vector3f RPCowPlayer::GetPrevPos() {
    return mpCollision->GetPos() - Cow_Transform_getDelta(mpCollision);
}

EGG::Vector3f RPCowPlayer::GetMoveDelta() {
    return GetForwardMove() + GetVelocity();
}

EGG::Vector3f RPCowPlayer::GetForwardMove() {
    return GetForwardDir() * mSpeed * sMoveScale;
}

EGG::Vector3f RPCowPlayer::GetVelocity() const {
    return mpCollision->GetRot();
}

EGG::Vector3f RPCowPlayer::GetRotation() {
    EGG::Vector3f rot;
    rot.x = 0.0f;
    rot.y = unk71C8;
    rot.z = 0.0f;
    return rot;
}

EGG::Vector3f RPCowPlayer::GetForwardDir() {
    EGG::Vector3f dir;
    dir.x = nw4r::math::SinRad(unk71C8);
    dir.y = 0.0f;
    dir.z = nw4r::math::CosRad(unk71C8);
    return dir;
}

EGG::Vector3f RPCowPlayer::GetRightDir() {
    EGG::Vector3f dir;
    dir.x = nw4r::math::CosRad(unk71C8);
    dir.y = 0.0f;
    dir.z = -nw4r::math::SinRad(unk71C8);
    return dir;
}

EGG::Vector3f RPCowPlayer::GetCameraYawVec() {
    EGG::Vector3f rot;
    rot.x = 0.0f;
    rot.y = unk71D8;
    rot.z = 0.0f;
    return rot;
}

// Matches with RPCowKakasiGroup::IsRemaining written as a single return expression (a flag
// variable there swaps the group and flag registers).
EGG::Vector3f RPCowPlayer::GetTargetPos() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    RPCowKakasiGroup* pGroup = GetKakasiGroup(pGame, mArrow.mTargetGroup);
    if (pGroup->IsRemaining()) {
        return pGroup->GetPos();
    } else if (pGame->GetSlotNum(15) > 0) {
        return static_cast<RPCowGoal*>(pGame->GetSlotActor(15))->GetPos();
    } else {
        return GetPlayer(pGame, GetUnk1A() == 0)->GetPos();
    }
}

void RPCowPlayer::SetTargetGroup(s32 group) {
    mArrow.mTargetGroup = group;
}

s32 RPCowPlayer::GetTotalScore() const {
    if (unkA8 == 2) {
        return 0;
    }
    return mScore + mBonusScore;
}

void RPCowPlayer::AddScore(s32 points, const EGG::Vector3f& rPos) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if (IsPlaying(pGame)) {
        mScore += points;
        RPCowPointPopup::Spawn(rPos, GetUnk1A(), points, false);
        static_cast<RPCowMainHud*>(pGame->GetSlotActor(18))
            ->SetPlayerScore(GetUnk1A(), mScore + mBonusScore, true);
    }
}

void RPCowPlayer::PlayKakasiHitSound(s32 bonusLevel) {
    mpSound->startSound(0x99);
}

void RPCowPlayer::AddGroupScore(s32 points, s32 bonusLevel) {
    if (IsPlaying(RPCowGame::GetInstance())) {
        RPCowGame* pGame = RPCowGame::GetInstance();
        mScore += points;
        RPCowPointPopup::Spawn(mpCollision->GetPos(), GetUnk1A(), points, true);
        static_cast<RPCowMainHud*>(pGame->GetSlotActor(18))
            ->SetPlayerScore(GetUnk1A(), mScore + mBonusScore, true);
        static_cast<RPCowPerfectMsg*>(pGame->GetSlotActor(19))
            ->Show(GetUnk1A(), points, bonusLevel);
    }
}

void RPCowPlayer::AddBonusScore(s32 points) {
    mBonusScore += points;
}

// Ledger name; the Nunchuk's acceleration of a KPAD status (EGG::CoreStatus has no getter
// for it) [H]
extern "C" EGG::Vector3f Cow_getFsAccel(const EGG::CoreStatus* pStatus) {
    return EGG::Vector3f(pStatus->ex_status.fs.acc.x, pStatus->ex_status.fs.acc.y,
                         pStatus->ex_status.fs.acc.z);
}

void RPCowPlayer::CalcInput() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    RPCowCamera* pCamera = GetCamera(pGame, GetUnk1A());

    if (IsBeforeStart(pGame)) {
        if (static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->isCoreBaseDevice() &&
            static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->getReadLength() > 0 &&
            static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->getCoreStatus(0)->getDPDValidFlag()) {
            RPCowGame* pGame2 = RPCowGame::GetInstance();
            if (unk9C == 0 || unk9C == 2) {
                unkA0 = 31;
                unk9C = 2;
            }
            static_cast<RPCowExMsg*>(pGame2->GetSlotActor(20))->ShowInputMsg(GetUnk1A());
            unk72E0 = 60;
        }
        if (unk72E0 > 0) {
            unk72E0--;
        }
    }

    if (static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->isCoreBaseDevice()) {
        for (int i = 0; i < 2; i++) {
            EGG::Vector3f accel(0.0f, 0.0f, 0.0f);
            s32 num = static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->getReadLength();
            if (num > 0) {
                if (i == 0) {
                    for (int j = 0; j < num; j++) {
                        EGG::Vector3f a = static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->getCoreStatus(j)->getAccel();
                        RP_clampVec3Abs(3.4f, &a);
                        accel.x += a.x;
                        accel.y += a.y;
                        accel.z += a.z;
                    }
                } else {
                    for (int j = 0; j < num; j++) {
                        EGG::Vector3f a = Cow_getFsAccel(static_cast<RPSysCoreController*>(EGG::CoreControllerMgr::instance()->getNthController(GetUnk1A()))->getCoreStatus(j));
                        RP_clampVec3Abs(3.4f, &a);
                        accel.x += a.x;
                        accel.y += a.y;
                        accel.z += a.z;
                    }
                }
                f32 scale = 1.0f / num;
                accel.x *= scale;
                accel.y *= scale;
                accel.z *= scale;
            } else {
                accel = mPosHistory[i].mRing0.GetLast();
            }
            mPosHistory[i].mRing0.Push(EGG::Vector3f(accel));
        }
    }

    if (unk72E4 == 1) {
        unk7168 = 0.0f;
        unk716C = 0.0f;
        unk7170 = 0.0f;
        unk7174 = 0.0f;
        unk7178 = 0.0f;
        f32 angle = mArrow.mpCollision->GetVec40().y - GetRotation().y;
        if (angle > 3.1415927f) {
            angle -= 6.2831855f;
        } else if (angle < -3.1415927f) {
            angle += 6.2831855f;
        }
        unk717C = angle * 0.8f;
        unk7180 = -0.8f;
    } else if (unk72E0 > 0 || pCamera->IsInIntro()) {
        unk7170 = 0.0f;
        unk7174 = 0.0f;
        unk7178 = 0.0f;
        unk7168 = 0.0f;
        unk716C = 0.0f;
        unk717C = 0.0f;
        unk7180 = 0.0f;
    } else {
        unk7170 = mPosHistory[0].mRing0.GetLast().x;
        unk7174 = mPosHistory[0].mRing0.GetLast().y;
        unk7178 = mPosHistory[0].mRing0.GetLast().z;
        f32 sqXY = unk7170 * unk7170 + unk7174 * unk7174;
        f32 sqYZ = unk7174 * unk7174 + unk7178 * unk7178;
        f32 lenXY = nw4r::math::FSqrt(sqXY);
        f32 lenYZ = nw4r::math::FSqrt(sqYZ);
        bool flipX = false;
        bool flipZ = false;
        unk716C = __fabsf(sqXY - unk7168);
        unk7168 = sqXY;
        if (unk7174 > 0.5f) {
            if (unk7170 < 0.25f && unk7170 > -0.25f) {
                flipX = true;
            }
            if (unk7178 < 0.25f && unk7178 > -0.25f) {
                flipZ = true;
            }
        }
        if (flipX) {
            lenXY = -lenXY;
        }
        unk717C = nw4r::math::Atan2FIdx(unk7178, lenXY) * 4.0f / 256.0f;
        if (unk717C > 2.0f) {
            unk717C -= 2.0f;
        } else if (unk717C < -2.0f) {
            unk717C += 2.0f;
        }
        if (unk717C > 1.5f) {
            unk717C = (unk717C - 1.5f) * -2.0f + 1.0f;
        } else if (unk717C > 1.0f) {
            unk717C = 1.0f;
        } else if (unk717C < -1.5f) {
            unk717C = (unk717C - -1.5f) * -2.0f - 1.0f;
        } else if (unk717C < -1.0f) {
            unk717C = -1.0f;
        }
        if (flipZ) {
            lenYZ = -lenYZ;
        }
        unk7180 = nw4r::math::Atan2FIdx(unk7170, lenYZ) * 4.0f / 256.0f;
        if (unk7180 > 2.0f) {
            unk7180 -= 2.0f;
        } else if (unk7180 < -2.0f) {
            unk7180 += 2.0f;
        }
        if (unk7180 > 1.0f) {
            unk7180 = (unk7180 - 1.0f) * -2.0f + 1.0f;
        } else if (unk7180 < -1.0f) {
            unk7180 = -1.0f;
        }
    }

    if (unk7168 < 1.44f) {
        f32 pitch = unk7180;
        f32 roll = unk717C;
        if (IsPlaying(pGame) && unk71E4 <= 0.0f) {
            if (pitch <= 0.0f) {
                unk71B8 = pitch * -0.1f;
            } else {
                unk71B8 = pitch * -0.01f;
            }
        } else {
            unk71B8 = 0.0f;
        }
        if (IsPlaying(pGame)) {
            if (unk71E4 <= 0.0f) {
                unk71D4 = roll * 0.01f;
            } else {
                unk71D4 = (roll * 0.01f) * 0.5f;
            }
        } else {
            unk71D4 = 0.0f;
        }
        unk71C4 = pitch * -2.0f;
        if (unk71B8 > 0.011f) {
            unk71B8 = 0.011f;
        } else if (unk71B8 < -0.001f) {
            unk71B8 = -0.001f;
        }
        if (unk71E4 <= 0.0f) {
            if (unk71D4 > 0.01f) {
                unk71D4 = 0.01f;
            } else if (unk71D4 < -0.01f) {
                unk71D4 = -0.01f;
            }
        } else {
            if (unk71D4 > 0.005f) {
                unk71D4 = 0.005f;
            } else if (unk71D4 < -0.005f) {
                unk71D4 = -0.005f;
            }
        }
        if (unk71C4 > 1.0f) {
            unk71C4 = 1.0f;
        } else if (unk71C4 < 0.0f) {
            unk71C4 = 0.0f;
        }
        if (unk71E4 <= 0.0f) {
            unk7200 = pitch * -1.0f;
            unk720C = roll * -1.0f;
        } else {
            unk7200 = (pitch * -1.0f) * 0.5f;
            unk720C = (roll * -1.0f) * 0.5f;
        }
        if (unk7200 > 1.0f) {
            unk7200 = 1.0f;
        } else if (unk7200 < -1.0f) {
            unk7200 = -1.0f;
        }
        if (unk720C < -1.0f) {
            unk720C = -1.0f;
        } else if (unk720C > 1.0f) {
            unk720C = 1.0f;
        }
    }

    RP_approachStep(&unk71B0, &unk71B4, unk71B8, 0.5f);
    RP_approachStep(&unk71CC, &unk71D0, unk71D4, 0.5f);
    RP_approachStep(&unk71BC, &unk71C0, unk71C4, 0.1f);
    RP_approachStep(&unk71F8, &unk71FC, unk7200, 0.5f);
    RP_approachStep(&unk7204, &unk7208, unk720C, 0.5f);
    f32 angle = unk71F8 * sC_B04;
    if (angle > sC_B08) {
        angle = sC_B08;
    } else if (angle < sZ_19AC) {
        angle = sZ_19AC;
    }
    RP_approachStep(&unk722C[1], &unk722C[2], angle, 0.4f);
    UpdateAngleDisp(this, pGame, unk722C[1], angle, sC_B08);
}

void RPCowPlayer::CalcMovement() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCollision->SetRadius(sC_AD4);
    mpShadowTex->FLOAT_0x74 = 1.1f;
    mpShadowTex->BYTE_0x72 = 120;

    if (unk71E4 > 0.0f && (u32)(pGame->GetState() - 9) > 15) {
        mSpeed *= 0.999f;
    } else if (unkAC & 2) {
        mSpeed *= 0.984f;
    } else {
        mSpeed *= 0.99f;
    }

    if (unkA6 && unk71E4 <= 0.0f) {
        f32 accel = unk71B0;
        if (accel > 0.0f) {
            accel *= unk7184;
            f32 max = unk7188 - mSpeed;
            accel = Min(accel, max);
            if (accel > 0.0f) {
                f32 speed = mSpeed + accel;
                f32 lim = unk71BC;
                mSpeed = speed < lim ? speed : (lim > mSpeed ? lim : mSpeed);
            }
        } else {
            f32 speed = mSpeed + accel;
            mSpeed = speed > 0.0f ? speed : 0.0f;
        }
    }

    if (unk7218[2] <= 0 && unkA6 && unk72E4 != 2 && IsPlaying(pGame) && unk71E4 <= 0.0f &&
        IsCharging()) {
        f32 add = 0.3f - mSpeed;
        add = 0.1f < add ? 0.1f : add;
        if (add > 0.0f) {
            mSpeed += add;
        }
    }

    if (IsBeforePlay(pGame)) {
        if (unk7180 < -0.2f) {
            unk7190 = 0;
        } else if (unk7180 >= sZ_19BC) {
            unk7190 = 20;
        }
    }

    if (IsPlaying(pGame) && unk7190 > 0) {
        if (unk7180 < -0.2f) {
            unk7194 = unk7190 * 0.003f;
            if (unk7190 >= 5) {
                if (GetUnk1A() == 0) {
                    mpSound->startSound(0x37);
                } else {
                    mpSound->startSound(0x38);
                }
            }
            unk7190 = 0;
        } else {
            unk7190--;
        }
    }

    if (IsPlaying(pGame)) {
        if (unk72DC < 180) {
            if (++unk72DC >= 180) {
                static_cast<RPCowExMsg*>(pGame->GetSlotActor(20))->ShowMoveMsg(GetUnk1A());
                GetCamera(pGame, GetUnk1A())->SetFollowNearState();
                unkA5 = true;
            }
        }
        if (unk7180 <= -0.2f) {
            unk72DC = 180;
            unkA6 = true;
            if (unkA5) {
                static_cast<RPCowExMsg*>(pGame->GetSlotActor(20))->Hide(GetUnk1A());
                GetCamera(pGame, GetUnk1A())->SetFollowState();
                unkA5 = false;
            }
        }
    } else if (unkA5) {
        static_cast<RPCowExMsg*>(pGame->GetSlotActor(20))->Hide(GetUnk1A());
        unkA5 = false;
    }

    if (IsPlaying(pGame) && unk71E4 <= 0.0f && unk7194 > 0.0f) {
        f32 add = unk7194;
        f32 max = 1.2f - mSpeed;
        add = add < max ? add : max;
        if (add > 0.0f) {
            mSpeed += add;
        }
        f32 rest = unk7194 - 0.001f;
        unk7194 = rest > 0.0f ? rest : 0.0f;
        unkA6 = true;
    }

    unk71A8 = 0.0f;
    if (IsPlaying(pGame) && unk71E4 <= 0.0f) {
        if (unkAC & 2) {
            mpSound->HoldRunStepSound(1, mSpeed);
        } else {
            mpSound->HoldRunStepSound(0, mSpeed);
        }
        if (mSpeed >= 0.35f && unk71B8 <= -0.0005f) {
            mpSound->HoldSpeedSound(mSpeed);
            unk71A8 = 1.0f;
        } else if (unk71A4 > 0.0f || unk71A4 < 0.0f) {
            mpSound->HoldSpeedSound(mSpeed);
            unk71A8 = 1.0f;
        }
    }

    if (unk71B8 <= -0.0005f) {
        unk719C = 20;
        unk71A0 = 60;
    }
    if (unk719C > 0 && unk71A0 > 0) {
        unk719C--;
        if (mSpeed > 0.4f && unk71E4 <= 0.0f) {
            if (unk71CC > 0.003f) {
                unk719C = 20;
                unk71A0--;
                unk71A4 = mSpeed * 0.04f;
            } else if (unk71CC < -0.003f) {
                unk719C = 20;
                unk71A0--;
                unk71A4 = mSpeed * -0.04f;
            } else {
                unk71A4 = 0.0f;
            }
        } else {
            unk719C = 0;
            unk71A0 = 0;
            unk71A4 = 0.0f;
        }
    } else {
        unk719C = 0;
        unk71A0 = 0;
        unk71A4 = 0.0f;
    }

    f32 decel = 0.0f;
    if (unk71E4 > 0.0f) {
        if (unk71CC > 0.0f) {
            decel = unk71CC * 0.2f;
        } else if (unk71CC < 0.0f) {
            decel = -unk71CC * 0.2f;
        }
    } else if (unk71A4 > 0.0f || unk71A4 < 0.0f) {
        if (unk71CC > 0.0f) {
            decel = sZ_19B8 * unk71CC;
        } else if (unk71CC < 0.0f) {
            decel = sZ_19B8 * -unk71CC;
        }
    } else {
        if (unk71CC > 0.0f) {
            decel = unk71CC * 1.5f;
        } else if (unk71CC < 0.0f) {
            decel = -unk71CC * 1.5f;
        }
    }
    if (decel > 0.015f) {
        decel = 0.015f;
    }
    mSpeed -= decel;
    if (mSpeed <= 0.0f) {
        mSpeed = 0.0f;
    }
    if (unkA6) {
        unk71C8 += unk71CC;
    }
    unk71C8 += unk7228;
    RP_approachStep(&unk71D8, &unk71DC, unk71C8, 0.2f);

    if (unkA6 && IsPlaying(pGame)) {
        if (unk71F0 == 0) {
            if (unk716C <= 0.64000005f) {
                if (unk71F4 <= 0.0f && unk7178 <= -sC_B10) {
                    unk71F4 = unk7178;
                    unk71F0 = 10;
                } else if (unk71F4 >= 0.0f && unk7178 >= sC_B10) {
                    unk71F4 = unk7178;
                    unk71F0 = 10;
                }
            }
        } else if (unk71F0 > 0) {
            if (unk716C <= 0.64000005f && unk71E4 <= 0.0f) {
                if (unk71F4 <= 0.0f) {
                    if (unk7178 <= -sC_B10) {
                        unk71F4 = unk7178;
                        unk71F0 = 10;
                    } else if (unk7178 >= sC_B14) {
                        f32 boost = unk71F4 * 0.8f;
                        if (boost < -2.0f) {
                            boost = -2.0f;
                        }
                        unk71A4 += boost;
                        unk71F0 = -10;
                        mSpeed += sZ_19B0;
                    }
                } else if (unk71F4 >= 0.0f) {
                    if (unk7178 >= sC_B10) {
                        unk71F4 = unk7178;
                        unk71F0 = 10;
                    } else if (unk7178 <= -sC_B14) {
                        f32 boost = unk71F4 * 0.8f;
                        if (boost > 2.0f) {
                            boost = 2.0f;
                        }
                        unk71A4 += boost;
                        unk71F0 = -10;
                        mSpeed += sZ_19B0;
                    }
                }
            }
            if (--unk71F0 <= 0) {
                unk71F0 = 0;
                unk71F4 = 0.0f;
            }
        } else if (unk71F0 < 0) {
            if (++unk71F0 >= 0) {
                unk71F0 = 0;
                unk71F4 = 0.0f;
            }
        }
    }

    if (unk71E4 <= 0.0f) {
        if (unk7168 >= 6.25f && unk7178 <= sC_B0C && unk7178 >= -sC_B0C) {
            if (unk7198 >= 30) {
                f32 add = unk7198 * 0.006f;
                f32 max = 1.5f - mSpeed;
                add = add < max ? add : max;
                if (add > 0.0f) {
                    mSpeed += add;
                }
            }
            unk71E0 = 10;
            unk71EC = 0.08f;
            unk71E8 = mSpeed * 0.5f + 1.0f;
            unk71E4 += unk71E8;
            if (unk7198 >= 30) {
                if (pGame->GetPlayerNum() <= 1) {
                    mpSound->startSound(0x40);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(6);
                    }
                } else if (GetUnk1A() == 0) {
                    mpSound->startSound(0x43);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(6);
                    }
                } else {
                    mpSound->startSound(0x46);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(7);
                    }
                }
            } else {
                if (pGame->GetPlayerNum() <= 1) {
                    mpSound->startSound(0x3E);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(4);
                    }
                } else if (GetUnk1A() == 0) {
                    mpSound->startSound(0x41);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(4);
                    }
                } else {
                    mpSound->startSound(0x44);
                    if (IsPlaying(pGame) || IsBeforePlay(pGame)) {
                        mpSound->startSound(5);
                    }
                }
            }
            mpEffects->SpawnLandSmoke(unk7198 >= 30, mSpeed);
        } else {
            unk71EC = 0.0f;
            unk71E8 = 0.0f;
            unk71E4 = 0.0f;
            if (mpCowModel->mState == 3) {
                if (++unk7210 >= 30) {
                    unk7210 = 0;
                    mpCowModel->ClearLandState();
                }
            }
        }
    } else {
        if (unk71E0 > 0) {
            if (unk7168 >= 6.25f && unk7178 <= sC_B0C && unk7178 >= -sC_B0C) {
                unk71E0--;
                unk71EC -= 0.002f;
                unk71E8 += 0.1f;
                unk71E8 -= unk71EC;
                unk71E4 += unk71E8;
            } else {
                unk71E0 = 0;
            }
        }
        unk71E8 -= unk71EC;
        unk71E4 += unk71E8;
        if (unk71E8 <= sC_AD8) {
            mpCowModel->PlayJumpAnim();
        }
        if (unk71E8 < 0.0f && unk71E4 <= sC_ADC) {
            unk7210 = 0;
            mpCowModel->PlayLandAnim();
        }
        if (unk71E4 <= 0.0f) {
            unk71EC = 0.0f;
            unk71E8 = 0.0f;
            unk71E4 = 0.0f;
            mpSound->startSound(0x10);
            mpEffects->SpawnLandSmoke(unk7198 >= 30, mSpeed);
            unk7198 = 0;
        }
    }

    EGG::Vector3f fwd;
    fwd.x = nw4r::math::SinRad(unk71C8);
    fwd.y = 0.0f;
    fwd.z = nw4r::math::CosRad(unk71C8);
    EGG::Vector3f side;
    side.x = fwd.z;
    side.y = 0.0f;
    side.z = -fwd.x;
    if (unk71A4 > 0.0f || unk71A4 < 0.0f) {
        mpCollision->GetRot().x -= side.x * unk71A4;
        mpCollision->GetRot().z -= side.z * unk71A4;
    }
    f32 move = mSpeed * sMoveScale;
    mpCollision->GetPos().x += move * fwd.x;
    mpCollision->GetPos().y = unk71E4;
    mpCollision->GetPos().z += move * fwd.z;
    mpCollision->GetRot().x *= 0.96f;
    mpCollision->GetRot().z *= 0.96f;
    mpCollision->GetPos().x += mpCollision->GetRot().x;
    mpCollision->GetPos().z += mpCollision->GetRot().z;
    unk7228 *= 0.8f;
    mpEffects->SetMtx(GetShapeMtx(mpCollision));
}

inline void RPCowArrow::Show() {
    mTimer = 5;
    if (mScale <= 0.0f) {
        mScaleVel = 0.2f;
        mpModel->GetModelAnm()->SetFrame(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
        mpModel->GetModelAnm()->SetFrame(RPGrpModelAnm::Anm_Clr, 0, 0.0f);
    }
}

void RPCowPlayer::HandleCollisions() {
    // The two nonoverlapping uses share storage so CodeWarrior colours the tutorial
    // message pointer into r29 while keeping the later collision registers unchanged.
    union {
        RPCowExMsg* pMsg;
        s32 lead;
    } scratch;
    RPCowGame* pGame = RPCowGame::GetInstance();
    EGG::Vector3f fwd = GetForwardDir();
    EGG::Vector3f nrm;

    // Scarecrows
    if (Cow_objectColliders.CheckHit(this, 2, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), NULL, &nrm,
                                 NULL)) {
        nrm.y = 0.0f;
        f32 dot = fwd.dot(nrm);
        dot *= mSpeed;
        f32 hit = dot < 0.0f ? -dot : 0.0f;
        f32 rumble = hit > 0.4f ? 1.0f : 0.0f; // unused
        if (!IsResult(pGame)) {
            if (unk7218[0] <= 0) {
                unk7218[0] = 10;
                RPRumblePattern rumble1(7, sC_AF4, 0);
                RPRumblePattern::Add(GetUnk1A(), rumble1);
            } else {
                unk7218[0] = 10;
                RPRumblePattern rumble2(1, sC_AF8, 0);
                RPRumblePattern::Add(GetUnk1A(), rumble2);
            }
            mpSound->PlayHit(0x4A, mSpeed);
        }
        if (pGame->GetPlayerNum() <= 1) {
            mpEffects->SpawnEffectAtMtx(mpCollision->GetPos() - nrm * mpCollision->GetRadius(),
                                        "Cow_kakashi_hit01");
        } else {
            mpEffects->SpawnEffectAtMtx(mpCollision->GetPos() - nrm * mpCollision->GetRadius(),
                                        "Cow_kakashi_hit02");
        }
    }
    if (unk7218[0] > 0) {
        unk7218[0]--;
    }

    // Scarecrow kings
    if (Cow_objectColliders.CheckHit(this, 0x40, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), NULL, &nrm,
                                 NULL)) {
        nrm.y = 0.0f;
        f32 dot = fwd.dot(nrm);
        dot *= mSpeed;
        f32 hit = dot < 0.0f ? -dot : 0.0f;
        f32 rumble = hit > 0.4f ? 1.0f : 0.0f; // unused
        if (!IsResult(pGame)) {
            RPRumblePattern rumble3(12, sC_AFC, 0);
            RPRumblePattern::Add(GetUnk1A(), rumble3);
            mpSound->PlayHit(0x4E, mSpeed);
        }
        if (pGame->GetPlayerNum() <= 1) {
            mpEffects->SpawnEffectAtMtx(mpCollision->GetPos() - nrm * mpCollision->GetRadius(),
                                        "Cow_kakashi_hit01");
        } else {
            mpEffects->SpawnEffectAtMtx(mpCollision->GetPos() - nrm * mpCollision->GetRadius(),
                                        "Cow_kakashi_hit02");
        }
    }

    // Hurdles
    EGG::Vector3f hurdleOfs;
    EGG::Vector3f hurdleNrm;
    if (Cow_objectColliders.CheckHit(this, 4, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), &hurdleOfs,
                                 &hurdleNrm, NULL)) {
        f32 dot = fwd.dot(hurdleNrm);
        dot *= mSpeed;
        f32 hit = dot < 0.0f ? -dot : 0.0f;
        if (unkA8 == 1) {
            if (!IsResult(pGame)) {
                mpSound->PlayHit(0x4D, mSpeed);
            }
            mpEffects->SpawnEffectAtMtx(
                mpCollision->GetPos() - hurdleNrm * mpCollision->GetRadius(), "Cow_hurdle_crash01");
        } else {
            hurdleOfs.y = 0.0f;
            RP_CollisionShape_accumPosOffset(mpCollision, hurdleOfs);
            hurdleNrm.y = 0.0f;
            RP_CollisionShape_accumRotOffset(mpCollision, (hurdleNrm * 0.8f));
            mSpeed += dot * 0.2f;
            if (IsNonNegative(GetRightDir().dot(hurdleNrm))) {
                unk7228 += 0.01f;
            } else {
                unk7228 -= 0.01f;
            }
            if (unk7218[1] <= 0) {
                unk7218[1] = 20;
                f32 rumble = hit > 0.4f ? 1.0f : 0.0f; // unused
                if (!IsResult(pGame)) {
                    RPRumblePattern rumble4(12, sC_AE4, 0);
                    RPRumblePattern::Add(GetUnk1A(), rumble4);
                    mpSound->PlayHit(0x4D, mSpeed);
                }
                mpEffects->SpawnEffectAtMtx(
                    mpCollision->GetPos() - hurdleNrm * mpCollision->GetRadius(),
                    "Cow_hurdle_crash01");
            }
            if (!unkA5 && !unkA4) {
                if (pGame->IsPlayIntro() && IsPlaying(pGame)) {
                    RPCowGame* pGame2 = RPCowGame::GetInstance();
                    if (unk9C == 0 || unk9C == 4) {
                        unkA0 = 181;
                        unk9C = 4;
                    }
                    scratch.pMsg = static_cast<RPCowExMsg*>(pGame2->GetSlotActor(20));
                    scratch.pMsg->ShowCollisionMsg(GetUnk1A());

                }
                unkA4 = true;
            }
        }
    }
    if (unk7218[1] > 0) {
        unk7218[1]--;
    }

    // Trees
    EGG::Vector3f treeNrm;
    if (Cow_objectColliders.CheckHit(this, 0x10, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), NULL, &treeNrm,
                                 NULL)) {
        treeNrm.y = 0.0f;
        f32 dot = fwd.dot(treeNrm);
        dot *= mSpeed;
        f32 hit = dot < 0.0f ? -dot : 0.0f;
        f32 rumble = hit > 0.4f ? 1.0f : 0.0f; // unused
        if (!IsResult(pGame)) {
            RPRumblePattern rumble5(7, sC_B00, 0);
            RPRumblePattern::Add(GetUnk1A(), rumble5);
            mpSound->PlayHit(0x50, mSpeed);
        }
        mpEffects->SpawnEffectAtMtx(mpCollision->GetPos() - treeNrm * mpCollision->GetRadius(),
                                    "Cow_tree_clash");
    }

    // Goal
    if (Cow_objectColliders.CheckHit(this, 0x20, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), NULL, NULL,
                                 NULL)) {
        unkA0 = 0;
        unk9C = 0;
        pGame->OnCourseComplete(GetUnk1A());
    }
    if (IsGoalSequence(pGame) || pGame->IsWinner(GetUnk1A())) {
        mpCowModel->PlayGoalAnim();
    }

    // The other player
    EGG::Vector3f playerOfs;
    EGG::Vector3f playerNrm;
    if (Cow_objectColliders.CheckHit(this, 1, mpCollision->GetPos(),
                                 mpCollision->GetRadius(), mpCollision->GetRot(), &playerOfs,
                                 &playerNrm, NULL)) {
        playerOfs.y = 0.0f;
        RP_CollisionShape_accumPosOffset(mpCollision, playerOfs);
        playerNrm.y = 0.0f;
        scratch.lead = unk72CC < 0 ? 0 : (unk72CC >= 30 ? 30 : unk72CC);
        s32 lead = scratch.lead;
        f32 dot = fwd.dot(playerNrm);
        EGG::Vector3f along = fwd * dot;
        EGG::Vector3f perp = playerNrm - along;
        f32 alongRate = lead * 0.1f + 0.2f;
        f32 perpRate = lead * 0.1f + 0.2f;
        RP_CollisionShape_accumRotOffset(mpCollision, along * alongRate + perp * perpRate);
        f32 push = dot * mSpeed;
        f32 unused = push > 0.0f ? 1.0f : 0.0f;
        mSpeed += push * 0.1f;
        if (IsNonNegative(GetRightDir().dot(playerNrm))) {
            unk7228 += 0.01f;
        } else {
            unk7228 -= 0.01f;
        }
        if (unk7218[3] <= 0) {
            unk7218[3] = 20;
            RPCowPlayer* pOther = GetPlayer(pGame, GetUnk1A() == 0);
            f32 power = GetPushRate(lead) * 0.5f;
            if (power < 0.6f) {
                power = 0.6f;
            }
            if (!IsResult(pGame)) {
                RPRumblePattern rumble6(8, sC_AEC * power, 0);
                RPRumblePattern::Add(GetUnk1A(), rumble6);
            }
            if (GetUnk1A() == 0) {
                f32 volume = 0.5f + power * (sC_AE8 - 0.5f);
                volume = volume < 0.5f ? 0.5f : (volume > sC_AE8 ? sC_AE8 : volume);
                if (!IsResult(pGame)) {
                    mpSound->PlayHit(0x4B, volume);
                }
                mpEffects->EmitWool(volume, (GetPos() + pOther->GetPos()) * 0.5f,
                                    (GetMoveDelta() + pOther->GetMoveDelta()) * 0.5f);
            }
            if (pOther->unkA8 == 1) {
                unk7214 = 1;
            }
        }
    }
    if (unk7218[3] > 0) {
        unk7218[3]--;
    }

    // The course
    EGG::Vector3f fieldOfs;
    EGG::Vector3f fieldNrm;
    unkAC = 0;
    EGG::Vector3f fieldPos = mpCollision->GetPos();
    fieldPos.y = sZ_19A8;
    if (pGame->GetCollisionModel()
            ->CheckSphere(sC_AE0, fieldPos, &fieldOfs, &fieldNrm, &unkAC)) {
        if ((unkAC & 2) && IsPlaying(pGame)) {
            mArrow.Show();
        }
        if (unkAC & 4) {
            fieldOfs.y = 0.0f;
            RP_CollisionShape_accumPosOffset(mpCollision, fieldOfs);
            fieldNrm.y = 0.0f;
            RP_CollisionShape_accumRotOffset(mpCollision, (fieldNrm * 0.2f));
            f32 dot = fwd.dot(fieldNrm);
            dot *= mSpeed;
            f32 hit = dot < 0.0f ? -dot : 0.0f;
            mSpeed -= hit * 0.2f;
            if (fieldNrm.x < 0.0f) {
                unk7228 += 0.01f;
            } else {
                unk7228 -= 0.01f;
            }
            if (unk7218[2] <= 0) {
                unk7218[2] = 20;
                f32 rumble = hit > 0.4f ? 1.0f : 0.0f; // unused
                if (!IsResult(pGame)) {
                    RPRumblePattern rumble7(4, sC_AF0, 0);
                    RPRumblePattern::Add(GetUnk1A(), rumble7);
                    mpSound->PlayHit(0x4C, mSpeed);
                }
            }
        }
    }
    if (unk7218[2] > 0) {
        unk7218[2]--;
    }

    bool facing = IsPlaying(pGame) && IsPositive(GetForwardDir().z);
    if (facing) {
        mArrow.Show();
        mArrow.unk4C = 1.0f;
    } else {
        mArrow.unk4C = 0.0f;
    }
    if (mSpeed < 0.0f) {
        mSpeed = 0.0f;
    }
}

void RPCowPlayerSound::set3DPosition(const nw4r::math::VEC3& rPos) {
    int i = 0;
    nw4r::snd::Sound3DActor** ppActor = mSound3DActors;
    for (; i < 4; i++, ppActor++) {
        (*ppActor)->SetPosition(rPos);
    }
    if (unk90) {
        unk90 = false;
    } else {
        unk94 = -1.0f;
    }
}

void RPCowPlayer::CalcProgress() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    unk7188 = 0.8f;
    if (pGame->GetPlayerNum() >= 2) {
        RPCowPlayer* pOther = GetPlayer(pGame, GetUnk1A() == 0);
        s32 diff = mScore - pOther->mScore;
        unkA8 = 0;
        unk72CC = diff;
        f32 lead = mpCollision->GetPos().z - pOther->GetPos().z;
        if (lead < sLeadDistMin) {
            mProgress = 0.0f;
        } else if (lead > sLeadDistMax) {
            mProgress = 1.0f;
        } else {
            mProgress = (lead - sLeadDistMin) / (sLeadDistMax - sLeadDistMin);
        }
        unk7188 = 0.8f + mProgress * (sMaxRateMax - 0.8f);
        unk7184 = sSpeedRateMin + mProgress * (1.5f - sSpeedRateMin);
        if (IsPlaying(pGame)) {
            EGG::Vector3f d =
                (pOther->GetPos() + pOther->GetForwardDir() * sLeadAhead) - mpCollision->GetPos();
            d.y = 0.0f;
            f32 angle = nw4r::math::Atan2Rad(d.x, d.z) - pOther->GetRotation().y;
            if (angle > 3.1415927f) {
                angle -= 6.2831855f;
            } else if (angle < -3.1415927f) {
                angle += 6.2831855f;
            }
            if (unk71E4 <= 0.0f && angle > -sChaseAngle * 0.017453292f &&
                angle < sChaseAngle * 0.017453292f &&
                d.squaredLength() < sChaseDist * sChaseDist) {
                if (++unk7198 >= 60) {
                    unk7198 = 60;
                    if (GetUnk1A() == 0) {
                        mpSound->holdSound(0x35);
                    } else {
                        mpSound->holdSound(0x36);
                    }
                }
            } else if (unk7198 > 0) {
                s32 t = unk7198 - 4;
                unk7198 = t > 0 ? t : 0;
            }
            if (unk7198 >= 30 && (unk9C == 0 || unk9C == 5)) {
                unkA0 = 2;
                unk9C = 5;
            }
        } else {
            unk7198 = 0;
        }
    } else {
        if (unkA8 == 1) {
            unkA8 = 0;
        }
        unk7188 = sMaxRateSolo;
        unk7184 = sSpeedRateSolo;
    }

    if (RP_isVec3WithinAbs(0.01f, &mpCollision->GetRot()) && unk7214 > 0) {
        unk7214--;
    }
    if (mBonusScore > 0) {
        mBonusScore--;
        mScore++;
        static_cast<RPCowMainHud*>(pGame->GetSlotActor(18))
            ->SetPlayerScore(GetUnk1A(), mScore, false);
    }
    if (IsPlaying(pGame) && mTime <= 0.0f) {
        mTime = 0.0f;
        if (pGame->GetPlayerNum() <= 1) {
            unkA8 = 2;
        }
        unk7190 = 0;
        unk7198 = 0;
        unkA0 = 0;
        unk9C = 0;
        pGame->OnTimeUp(GetUnk1A());
    }
    mpCollision->ApplyOffsets();
    mpCowModel->SetSpeedBlend(mSpeed);
    mpCowModel->SetTailPush(unk71E8);
    mpCowModel->SetUnk1A4(unk71F8);
    mpCowModel->SetUnk1A8(unk7204);
    mpCowModel->CalcAnim();
    if (unkA0 > 0 && --unkA0 <= 0) {
        unkA0 = 0;
        unk9C = 0;
    }
    mArrow.Calc();
    if (pGame->GetPlayerNum() >= 2) {
        mCursor.Calc();
    }
    mpSound->set3DPosition(mpCollision->GetPos());
}


void RPCowPlayer::UpdateModel() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    EGG::Vector3f dir;
    dir.x = nw4r::math::SinRad(unk71C8);
    dir.y = 0.0f;
    dir.z = nw4r::math::CosRad(unk71C8);
    mpCollision->GetVec40().y = unk71C8;
    EGG::Matrix34f nodeMtx;
    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, mpCollision);
    mMtx = mtx;
    mpCowModel->mpModel->SetCallback(mpCowModel);
    mpCowModel->mpModel->SetCallbackJointIndex(0);
    CalcModel(mpCowModel->mpModel, sModelScale, &mMtx);
    mShadowMtx.makeS(EGG::Vector3f(sShadowScale, sShadowScale, sShadowScale));
    mShadowMtx._03 = mpCollision->GetPos().x;
    mShadowMtx._13 = 0.1f;
    mShadowMtx._23 = mpCollision->GetPos().z;
    mpEffects->mSpeedSmoke = IsPlaying(pGame) && unkA8 == 1;
    mpEffects->mJump = IsPlaying(pGame) && unk7198 >= 30;
    mpEffects->mDamage = IsPlaying(pGame) && unk7214 > 0;
    mpEffects->mOnWeed = (unkAC >> 1) & 1;
    mpEffects->SetSpeed(mSpeed);
    mpEffects->SetDashRate(mProgress);
    mpEffects->mSlip2 = unk71A8;
    mpEffects->mSlip = unk71A4;
    mpEffects->Calc();

    // Cached joint index of the cow's j_body (flag, index) [M]
    static u16 sBodyNode[2];
    if (sBodyNode[0] == 0) {
        RPCowCowModel* pCowModel = mpCowModel;
        sBodyNode[0] = 1;
        sBodyNode[1] = pCowModel->mpModel->GetJointIndex("j_body");
    }
    mpCowModel->mpModel->GetWorldMtx(sBodyNode[1], &nodeMtx);
    CalcModel(mpCowModel->mpAvatarModel, 0.6f, &nodeMtx);
    mpCowModel->mpAvatar->GetHeadJointMtx(&nodeMtx);
    CalcModel(mpCowModel->mpAvatar->GetNigaoeModel(), 0.6f, &nodeMtx);
}

bool RPCowPlayer::IsEnabled(IRPActorPriority* pOther) {
    if (static_cast<RPCowPlayer*>(pOther) != this) {
        return true;
    }
    return false;
}

RPCowObjSound::~RPCowObjSound() {}

void RPCowPlayer::VF_0x3C() {
    mpCowModel = new RPCowCowModel(GetUnk1A());
    mpSound = new RPCowPlayerSound(RPSndAudioMgr::instance()->getPlayer());
}

RPCowPlayerSound::~RPCowPlayerSound() {}

void RPCowPlayer::VF_0x40() {
    delete mpSound;
}

void RPCowPlayer::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCowModel->LoadModels();
    mpShadowTex = new RPGrpShadowTexture(0x80, 1, 3);
    mpShadowTex->InitList();
    mpShadowTex->SHORT_0x70 |= 2;
    mpShadowTex->VEC_0x78 = nw4r::math::VEC3(0.0f, -1.0f, -1.0f);
    RPGrpModelManager::GetCurrent()
        ->GetModelScene(0)
        ->GetDrawPathManager()
        ->GetDrawPathShadowTexture()
        ->SetTexture(mpShadowTex);
    mpShadowTex->ResetList();
    mpShadowTex->AddModel(mpCowModel->mpModel, 1.0f);
    mpShadowTex->AddModel(mpCowModel->mpAvatar->GetBodyModel(), 1.0f);
    mpShadowTex->AddModel(mpCowModel->mpAvatar->GetNigaoeModel(), 1.0f);
    mpCollision = new RPCollisionShape();
    u16 num = pGame->GetResTable()->CountEntriesOfType(0x70001);
    u16 idx = GetUnk1A() < num ? GetUnk1A() : 0;
    mpPathObj = new RPPathObject(0.1f, 1.0f, pGame->GetResTable()->FindNthEntryOfType(0x70001, idx));
    mArrow.Init(GetUnk1A());
    if (pGame->GetPlayerNum() >= 2) {
        mCursor.Init(GetUnk1A(), GetUnk1A() == 0);
    }
    mpEffects = new RPCowPlayerEffects(mpCowModel);
    if (pGame->GetPlayerNum() <= 1) {
        mpSound->SetMode(0);
        mpSound->SetRemoteFlag(1);
    } else if (GetUnk1A() == 0) {
        mpSound->SetMode(2);
        mpSound->SetRemoteFlag(1);
    } else if (GetUnk1A() == 1) {
        mpSound->SetMode(3);
        mpSound->SetRemoteFlag(2);
    }
}

#pragma auto_inline off
void RPCowPosRing::Push(const EGG::Vector3f& rPos) {
    mPos[mNext] = rPos;
    s32 next = mNext + 1;
    mNext = next < 600 ? next : 0;
    mFull = mFull | (mNext == mStart);
}
#pragma auto_inline reset

void RPCowPlayer::OnStart() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCollision->Reset();
    mpCollision->GetPos() = mpPathObj->GetPos();
    mpCollision->GetVec40().y =
        nw4r::math::Atan2Rad(mpPathObj->GetVec20().x, mpPathObj->GetVec20().z);
    mpCollision->mpOwner = static_cast<IRPActorEnable*>(this);
    unkA0 = 0;
    unk9C = 0;
    unkA4 = false;
    unkA5 = false;
    unkA6 = true;
    unk7168 = 0.0f;
    unk7170 = 0.0f;
    unk7174 = 0.0f;
    unk7178 = 0.0f;
    unk717C = 0.0f;
    unk7180 = 0.0f;
    unk7184 = sSpeedRateSolo;
    unk7188 = sMaxRateSolo;
    mProgress = 0.0f;
    unk7190 = 0;
    unk7194 = 0.0f;
    unk7198 = 0;
    unk71A0 = 0;
    unk71A4 = 0.0f;
    mSpeed = 0.0f;
    unk71B0 = 0.0f;
    unk71B4 = 0.0f;
    unk71B8 = 0.0f;
    unk71BC = 1.0f;
    unk71C0 = 0.0f;
    unk71C4 = 1.0f;
    unk71C8 = mpCollision->GetVec40().y;
    unk71CC = 0.0f;
    unk71D0 = 0.0f;
    unk71D4 = 0.0f;
    unk71D8 = mpCollision->GetVec40().y;
    unk71DC = 0.0f;
    unk71E0 = 0;
    unk71E4 = 0.0f;
    unk71E8 = 0.0f;
    unk71EC = 0.0f;
    unk71F0 = 0;
    unk71F4 = 0.0f;
    unk71F8 = 0.0f;
    unk71FC = 0.0f;
    unk7200 = 0.0f;
    unk7204 = 0.0f;
    unk7208 = 0.0f;
    unk720C = 0.0f;
    unk7210 = 0;
    unk7214 = 0;
    mScore = 0;
    unk72CC = 0;
    mBonusScore = 0;
    mTime = mpPathObj->GetParam(0, 1800);
    unk72D8 = 0.0f;
    unk72DC = 0;
    unk72E0 = 10;
    unk7218[0] = 0;
    unk7218[1] = 0;
    unk7218[2] = 0;
    unk7218[3] = 0;
    unk7228 = 0.0f;
    unk722C[0] = 0.0f;
    unk722C[1] = 0.0f;
    unk722C[2] = 0.0f;
    unk72E4 = 0;
    mArrow.Reset(GetUnk1A());
    mArrow.unk38 = mpCollision->GetVec40().y;
    if (pGame->GetPlayerNum() >= 2) {
        mCursor.Reset();
    }
    for (int i = 0; i < 600; i++) {
        mPosHistory[0].mRing0.Push(EGG::Vector3f(0.0f, 0.0f, 0.0f));
        mPosHistory[0].mRing1.Push(EGG::Vector3f(0.0f, 0.0f, 0.0f));
        mPosHistory[1].mRing0.Push(EGG::Vector3f(0.0f, 0.0f, 0.0f));
        mPosHistory[1].mRing1.Push(EGG::Vector3f(0.0f, 0.0f, 0.0f));
    }
    unkA8 = 0;
    Cow_objectColliders.Append(mpCollision, 1);
    mpCowModel->Reset();
    mpEffects->ResetWool();
}

void RPCowPlayer::OnStop() {
    Cow_objectColliders.Remove(mpCollision);
}

void RPCowPlayer::OnActivate() {
    if (GetUnk1A() == 0) {
        RP_pushDebugGraphSamples(unk717C * 0.5f, unk7180 * 0.5f, mSpeed * 2.0f);
        RP_debugLogPrintf(L"%.2f %d", unk71A4, unk71A8);
    }
}

void RPCowPlayer::Calculate() {
    UpdateModel();
    RPCowPlayer* pPlayer = GetPlayer(RPCowGame::GetInstance(), mArrow.mPlayer);
    if (mArrow.mScale > 0.0f) {
        pPlayer->EntryModel(mArrow.mpModel);
    }
    EntryModel(mpCowModel->mpModel);
    EntryModel(mpCowModel->mpAvatar->GetNigaoeModel());
    EntryModel(mpCowModel->mpAvatar->GetBodyModel());
}

void RPCowPlayer::DrawScreen(u8 pass) {
    RPCowGame* pGame = RPCowGame::GetInstance();
    if ((GetUnk1A() == 0 && pass == 2) || (GetUnk1A() == 1 && pass == 3)) {
        RPTextWriter writer;
        writer.Begin(-1);
        writer.End();
        if (pGame->GetPlayerNum() >= 2) {
            mCursor.Draw();
        }
    }
    if (GetUnk1A() == 0 && pass == 0) {
    } else if (GetUnk1A() == 1) {
    }
}

void RPCowPlayer::DrawPass(u8 pass) {
    if (pass == 4) {
        RP_drawDebugLogAndGraphs();
    }
}

void RPCowPlayer::DrawShadowTex() {
    BeginDrawQuad__Q23EGG13ShadowTextureFQ33EGG13ShadowTexture5Blend(0);
    GXColor color = sShadowColor;
    DrawQuad__Q23EGG13ShadowTextureCFRCQ34nw4r4math5MTX34RC8_GXColor(mpShadowTex, mShadowMtx,
                                                                    color);
}

// Named consts after the player code (loaded, not folded); `= 0` puts them in .sbss2
const f32 RPCowPlayer::sModelScale = 1.0f;
const f32 RPCowPlayer::sC_AD4 = 6.0f;
const f32 RPCowPlayer::sC_AD8 = 1.0f;
const f32 RPCowPlayer::sC_ADC = 20.0f;
const f32 RPCowPlayer::sC_AE0 = 2.0f;
const f32 RPCowPlayer::sC_AE4 = 4.0f;
const f32 RPCowPlayer::sC_AE8 = 1.0f;
const f32 RPCowPlayer::sC_AEC = 2.0f;
const f32 RPCowPlayer::sC_AF0 = 4.0f;
const f32 RPCowPlayer::sC_AF4 = 4.0f;
const f32 RPCowPlayer::sC_AF8 = 4.0f;
const f32 RPCowPlayer::sC_AFC = 4.0f;
const f32 RPCowPlayer::sC_B00 = 4.0f;
const f32 RPCowPlayer::sC_B04 = 45.0f;
const f32 RPCowPlayer::sC_B08 = 30.0f;
const f32 RPCowPlayer::sC_B0C = 3.0f;
const f32 RPCowPlayer::sC_B10 = 1.0f;
const f32 RPCowPlayer::sC_B14 = 1.0f;
const f32 RPCowPlayer::sMoveScale = 6.0f;
const f32 RPCowPlayer::sMaxRateSolo = 1.0f;
const f32 RPCowPlayer::sMaxRateMax = 1.0f;
const f32 RPCowPlayer::sSpeedRateSolo = 1.0f;
const f32 RPCowPlayer::sSpeedRateMin = 1.0f;
const f32 RPCowPlayer::sLeadDistMax = 60.0f;
const f32 RPCowPlayer::sLeadAhead = 10.0f;
const f32 RPCowPlayer::sChaseDist = 70.0f;
const f32 RPCowPlayer::sChaseAngle = 10.0f;
const f32 RPCowPlayer::sZ_19A8 = 0;
const f32 RPCowPlayer::sZ_19AC = 0;
const f32 RPCowPlayer::sZ_19B0 = 0;
const f32 RPCowPlayer::sLeadDistMin = 0;
const f32 RPCowPlayer::sZ_19B8 = 0;
const f32 RPCowPlayer::sZ_19BC = 0;

// Unreferenced stand-in (dropped by the linker): the original's literal pool has 0.2f right
// after the arrow's constants, as if a function using it were defined here (most likely an
// out-of-line RPCowArrow::Show, inlined into HandleCollisions and dead-stripped)
extern "C" {
static void Cow_arrowPoolStandIn1(f32* pValue) {
    *pValue = 0.2f;
}
}

#pragma auto_inline off
void RPCowArrow::Init(s32 player) {
    if (player <= 0) {
        mpModel = RPGrpModel::Construct(0x43, 2, 0, 0x108);
    } else {
        mpModel = RPGrpModel::Construct(0x44, 2, 0, 0x108);
    }
    mpModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Chr, 1);
    mpModel->GetModelAnm()->Regist(RPGrpModelAnm::Anm_Chr, 0, 0x45, 0);
    mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_Chr, 0, 0.0f);
    mpModel->GetModelAnm()->CreateBuffer(RPGrpModelAnm::Anm_Clr, 1);
    if (player <= 0) {
        mpModel->GetModelAnm()->Regist(RPGrpModelAnm::Anm_Clr, 0, 0x46, 0);
    } else {
        mpModel->GetModelAnm()->Regist(RPGrpModelAnm::Anm_Clr, 0, 0x47, 0);
    }
    mpModel->GetModelAnm()->Start(RPGrpModelAnm::Anm_Clr, 0, 0.0f);
    mpCollision = new RPCollisionShape();
}
#pragma auto_inline reset

void RPCowArrow::Reset(s32 player) {
    mTimer = 0;
    mPlayer = player;
    mTargetGroup = 0;
    RPCowPlayer* pPlayer = GetPlayer(RPCowGame::GetInstance(), player);
    unk14 = pPlayer->GetPos();
    mTargetPos = pPlayer->GetPos();
    unk38 = 0.0f;
    unk3C = 0.0f;
    mScale = 0.0f;
    mScaleVel = 0.0f;
    unk4C = 0.0f;
    mpCollision->Reset();
}

inline void RPCowArrow::ApproachTarget(const EGG::Vector3f& rTarget) {
    RP_approachStep(&mTargetPos.x, &mTargetVel.x, rTarget.x, sRate);
    RP_approachStep(&mTargetPos.y, &mTargetVel.y, rTarget.y, sRate);
    RP_approachStep(&mTargetPos.z, &mTargetVel.z, rTarget.z, sRate);
}

void RPCowArrow::Calc() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    RPCowPlayer* pPlayer = GetPlayer(pGame, mPlayer);
    RPCowKakasiGroup* pGroup = GetKakasiGroup(pGame, mTargetGroup);

    mpCollision->GetPos().x = pPlayer->GetPos().x;
    mpCollision->GetPos().y =
        sOfsY + pPlayer->GetPos().y;
    mpCollision->GetPos().z = pPlayer->GetPos().z;

    if (pGroup->IsRemaining()) {
        ApproachTarget(pGroup->GetPos());
    } else if (pGame->GetPlayerNum() <= 1 && pGame->GetSlotNum(15) > 0) {
        ApproachTarget(
            static_cast<RPCowGoal*>(pGame->GetSlotActor(15))->GetPos());
    } else {
        ApproachTarget(GetPlayer(pGame, mPlayer == 0)->GetPos());
    }

    EGG::Vector3f dir = mTargetPos - mpCollision->GetPos();
    f32 yaw = nw4r::math::Atan2Rad(dir.x, dir.z);
    RP_wrapAngleNear(&mpCollision->GetVec40().y, yaw);
    RP_approachStep(&mpCollision->GetVec40().y, &mpCollision->GetVec4C().y, yaw, 0.1f);

    f32 angle = mpCollision->GetVec40().y - pPlayer->GetRotation().y;
    while (angle > 3.1415927f) {
        angle -= 6.2831855f;
    }
    while (angle < -3.1415927f) {
        angle += 6.2831855f;
    }
    if (angle <= -1.5707964f) {
        mAngle = -3.1415927f - angle;
    } else if (angle >= 1.5707964f) {
        mAngle = 3.1415927f - angle;
    } else {
        mAngle = angle;
    }

    if (mTimer > 0) {
        if (mScale > 1.0f || mScaleVel > 0.0f || mScaleVel < 0.0f) {
            mScale += mScaleVel;
            mScaleVel -= 0.02f;
            if (mScale <= 1.0f && mScaleVel <= 0.0f) {
                mScale = 1.0f;
                mScaleVel = 0.0f;
            }
        } else {
            mTimer--;
        }
    } else if (mpModel->GetModelAnm()->GetFrame(RPGrpModelAnm::Anm_Chr, 0) ==
               0.0f) {
        if (mScale > 0.0f) {
            mScale += mScaleVel;
            mScaleVel -= 0.02f;
            if (mScale <= 0.0f && mScaleVel <= 0.0f) {
                mScale = 0.0f;
                mScaleVel = 0.0f;
            }
        }
    }

    if (mpModel->GetModelAnm()->GetFrame(RPGrpModelAnm::Anm_Chr, 0) == 0.0f) {
        if (unk4C) {
            mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0,
                                                  1.0f);
            mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Clr, 0,
                                                  1.0f);
        } else {
            mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Chr, 0,
                                                  0.0f);
            mpModel->GetModelAnm()->SetUpdateRate(RPGrpModelAnm::Anm_Clr, 0,
                                                  0.0f);
        }
    }

    if (mScale > 0.0f) {
        EGG::Matrix34f mtx;
        RP_CollisionShape_getMtx(&mtx, mpCollision);
        nw4r::math::MTX34 rotMtx;
        nw4r::math::MTX34RotXYZRad(&rotMtx, 0.0f, 0.0f, mAngle);
        PSMTXConcat(mtx, rotMtx, mtx);
        pPlayer->UpdateModelFrame(mpModel);
        pPlayer->CalcModel(mpModel, mScale, &mtx);
    }
}

// Unreferenced stand-in (dropped by the linker): the original's literal pool starts with 0.0f,
// as if a function using it were defined after RPCowArrow::Calc
extern "C" {
static void Cow_arrowPoolStandIn0(f32* pValue) {
    *pValue = 0.0f;
}
}

const f32 RPCowArrow::sRate = 1.0f;
const f32 RPCowArrow::sOfsY = 20.0f;

// Shadow quad of the player, defined after the arrow's constants
const f32 RPCowPlayer::sShadowScale = 200.0f;
const GXColor RPCowPlayer::sShadowColor = {0, 0, 20, 255};
