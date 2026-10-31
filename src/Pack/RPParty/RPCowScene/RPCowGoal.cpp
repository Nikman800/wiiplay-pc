// Charge! goal gate (RPCowGoal): placed from the course's object table (resource type 0x70002);
// detects a player crossing its plane. Unit 0x802ABAD0-0x802AC118 (dtk/game_units.tsv).
// Functions in reverse address order.
#include <Pack/RPParty/RPCowScene/RPCowFieldObj.h>

#include <Pack/RPParty/RPCowScene/RPCowField.h>
#include <Pack/RPParty/RPCowScene/RPCowGame.h>
#include <Pack/RPParty/RPCowScene/RPCowPlayer.h>
#include <Pack/RPParty/RPPartyCommon/RPPathFollower.h>
#include <Pack/RPParty/RPPartyCommon/RPResFile.h>

#include <egg/gfxe/eggDrawGX.h>
#include <egg/math.h>
#include <nw4r/math.h>
#include <revolution/GX.h>

extern "C" void RP_CollisionShape_getMtx(EGG::Matrix34f* pOut, const RPCollisionShape* pShape);
// Column of a 3x4 matrix (ledger RP_Mtx34_getColumn, RPCollisionShape.cpp) [M]
#if defined(PORT_HOST)
// RPCollisionShape.cpp defines it returning the column. The declaration in the #else is the
// Wii's ABI form of that (a returned struct through a hidden first argument, the matrix by
// address); x86-64 returns the 12 bytes in registers, so the host calls the definition's form.
// (Calling the Wii form crashed in RPCowPerfectMsg::Show on Charge!'s first knocked group.)
extern "C" EGG::Vector3f RP_Mtx34_getColumn(const EGG::Matrix34f& rMtx, int col);
static inline void RP_Mtx34_getColumnInto(nw4r::math::VEC3* pOut, const nw4r::math::MTX34& rMtx, int col) {
    EGG::Vector3f v = RP_Mtx34_getColumn(reinterpret_cast<const EGG::Matrix34f&>(rMtx), col);
    pOut->x = v.x;
    pOut->y = v.y;
    pOut->z = v.z;
}
#define RP_Mtx34_getColumn(pOut, rMtx, col) RP_Mtx34_getColumnInto(pOut, rMtx, col)
#else
extern "C" void RP_Mtx34_getColumn(nw4r::math::VEC3* pOut, const nw4r::math::MTX34& rMtx, int col);
#endif

EGG::Vector3f RPCowGoal::GetPos() const {
    return mpCollision->GetPos();
}

void RPCowGoal::DrawDebugBox() {
    EGG::Matrix34f mtx;
    PSMTXScale(mtx, sBoxScaleX, sBoxScaleY, sBoxScaleZ);
    mtx._03 = 23.6f + mpCollision->GetPos().x;
    mtx._13 = sBoxOfsY + mpCollision->GetPos().y;
    mtx._23 = sBoxOfsZ + mpCollision->GetPos().z;
    EGG::DrawGX::BeginDrawShadowVolume(0);
    EGG::DrawGX::SetVtxState(EGG::DrawGX::VTXTYPE_CUBE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
    GXSetCullMode(GX_CULL_BACK);
    GXColor color = EGG::DrawGX::WHITE;
    EGG::DrawGX::DrawDLWorld(EGG::DrawGX::DL_CUBE, mtx, color);
    GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP);
    GXSetCullMode(GX_CULL_FRONT);
    EGG::DrawGX::DrawDLWorld(EGG::DrawGX::DL_CUBE, mtx, color);
}

bool RPCowGoal::IsEnabled(IRPActorPriority* pOther) {
    RPCowPlayer* pPlayer = static_cast<RPCowPlayer*>(pOther);
    EGG::Matrix34f mtx;
    RP_CollisionShape_getMtx(&mtx, mpCollision);
    EGG::Vector3f axis;
    RP_Mtx34_getColumn(&axis, mtx, 2);
    f32 prev = axis.dot(pPlayer->GetPrevPos() - mpCollision->GetPos());
    f32 cur = axis.dot(pPlayer->GetPos() - mpCollision->GetPos());
    if (prev <= 0.01f && cur >= -0.01f) {
        return true;
    }
    if (prev >= -0.01f && cur <= 0.01f) {
        return true;
    }
    return false;
}

RPCowGoal::RPCowGoal() {}

RPCowGoal::~RPCowGoal() {}

void RPCowGoal::LoadResource() {
    RPCowGame* pGame = RPCowGame::GetInstance();
    mpCollision = new RPCollisionShape();
    mpPathObj = new RPPathObject(0.1f, sPathScale,
                                 pGame->GetResTable()->FindNthEntryOfType(0x70002, GetUnk1A()));
}

void RPCowGoal::OnStart() {
    mpCollision->Reset();
    mpCollision->GetPos() = mpPathObj->GetPos();
    mpCollision->GetVec40().y =
        nw4r::math::Atan2Rad(mpPathObj->GetVec20().x, mpPathObj->GetVec20().z);
    mpCollision->mpOwner = static_cast<IRPActorEnable*>(this);
    Cow_objectColliders.Append(mpCollision, 0x20);
}

void RPCowGoal::OnStop() {
    Cow_objectColliders.Remove(mpCollision);
}

void RPCowGoal::OnActivate() {
    mpCollision->SetRadius(sRadius);
}

void RPCowGoal::Calculate() {}

// Named consts after the code (loaded, not folded).
const f32 RPCowGoal::sPathScale = 1.0f;
const f32 RPCowGoal::sRadius = 300.0f;
const f32 RPCowGoal::sBoxOfsY = 60.0f;
const f32 RPCowGoal::sBoxOfsZ = -53.0f;
const f32 RPCowGoal::sBoxScaleX = 520.0f;
const f32 RPCowGoal::sBoxScaleY = 120.0f;
const f32 RPCowGoal::sBoxScaleZ = 35.0f;
