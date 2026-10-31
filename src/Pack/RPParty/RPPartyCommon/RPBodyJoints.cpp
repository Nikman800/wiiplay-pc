#include <Pack/RPParty/RPPartyCommon/RPPartyBodyManager.h>

#include <Pack/RPGraphics/RPGrpModel.h>
#include <Pack/RPKernel/RPSysKokeshiManager.h>

// GCC does not accept a C linkage block around static member definitions
#if !defined(PORT_HOST)
extern "C" {
#endif
const char* RPPartyBodyManager::sClothesFileName[2] = {"G3D/clothes.brres", NULL};

const char* RPPartyBodyManager::sClothesNames[24][2][2] = {
    {{"body_tex00_a", "body_tex00_b"}, {"body_tex00_a", "body_tex00_b"}},
    {{"body_tex01_a", "body_tex01_b"}, {"body_tex01_a", "body_tex01_b"}},
    {{"body_tex02_a", "body_tex02_b"}, {"body_tex02_a", "body_tex02_b"}},
    {{"body_tex03_a", "body_tex03_b"}, {"body_tex03_a", "body_tex03_b"}},
    {{"body_tex04_a", "body_tex04_b"}, {"body_tex04_a", "body_tex04_b"}},
    {{"body_tex05_a", "body_tex05_b"}, {"body_tex05_a", "body_tex05_b"}},
    {{"body_tex06_a", "body_tex06_b"}, {"body_tex06_a", "body_tex06_b"}},
    {{"body_tex07_a", "body_tex07_b"}, {"body_tex07_a", "body_tex07_b"}},
    {{"body_tex08_a", "body_tex08_b"}, {"body_tex09_a", "body_tex09_b"}},
    {{"body_tex09_a", "body_tex09_b"}, {"body_tex09_a", "body_tex09_b"}},
    {{"body_tex10_a", "body_tex10_b"}, {"body_tex10_a", "body_tex10_b"}},
    {{"body_tex11_a", "body_tex11_b"}, {"body_tex11_a", "body_tex11_b"}},
    {{"body_tex50_a", "body_tex50_b"}, {"body_tex50_a", "body_tex50_b"}},
    {{"body_tex51_a", "body_tex51_b"}, {"body_tex51_a", "body_tex51_b"}},
    {{"body_tex52_a", "body_tex52_b"}, {"body_tex52_a", "body_tex52_b"}},
    {{"body_tex53_a", "body_tex53_b"}, {"body_tex53_a", "body_tex53_b"}},
    {{"body_tex54_a", "body_tex54_b"}, {"body_tex54_a", "body_tex54_b"}},
    {{"body_tex55_a", "body_tex55_b"}, {"body_tex55_a", "body_tex55_b"}},
    {{"body_tex56_a", "body_tex56_b"}, {"body_tex56_a", "body_tex56_b"}},
    {{"body_tex57_a", "body_tex57_b"}, {"body_tex57_a", "body_tex57_b"}},
    {{"body_tex58_a", "body_tex58_b"}, {"body_tex58_a", "body_tex58_b"}},
    {{"body_tex59_a", "body_tex59_b"}, {"body_tex59_a", "body_tex59_b"}},
    {{"body_tex60_a", "body_tex60_b"}, {"body_tex60_a", "body_tex60_b"}},
    {{"body_tex61_a", "body_tex61_b"}, {"body_tex61_a", "body_tex61_b"}},
};

const char* RPPartyBodyManager::sBodyFileNames[10][2] = {
    {"G3D/ava_bodyA0.brres", "G3D/ava_bodyB0.brres"},
    {"G3D/ava_bodyA0.brres", "G3D/ava_bodyA0.brres"},
    {"G3D/ava_bodyA1.brres", "G3D/ava_bodyB1.brres"},
    {"G3D/ava_bodyA1.brres", "G3D/ava_bodyA1.brres"},
    {"G3D/ava_bodyA2.brres", "G3D/ava_bodyB2.brres"},
    {"G3D/ava_bodyA2.brres", "G3D/ava_bodyA2.brres"},
    {"G3D/ava_bodyA3.brres", "G3D/ava_bodyB3.brres"},
    {"G3D/ava_bodyC0.brres", "G3D/ava_bodyC1.brres"},
    {"G3D/ava_bodyC1.brres", "G3D/ava_bodyC1.brres"},
    {"G3D/ava_bodyE0.brres", "G3D/ava_bodyE0.brres"},
};

// Friend clothes start index; defined at the end of the file (read from .sdata2, not folded)
extern "C" const u16 lbl_8043ED78;
#if !defined(PORT_HOST)
}
#endif

const char* RPPartyBodyManager::GetClothesFileName() {
    return sClothesFileName[0];
}

u16 RPPartyBodyManager::GetBodyModelIndex(u16, RFLSex, u32, u32) {
    return 0;
}

u16 RPPartyBodyManager::GetFriendClothesIndexRange() {
    return static_cast<u16>(24 - lbl_8043ED78);
}

u16 RPPartyBodyManager::GetFriendClothesStartIndex() {
    return lbl_8043ED78;
}

RPPartyBodyManager::RPPartyBodyManager() {}

RPPartyBodyManager::~RPPartyBodyManager() {}

const char* RPPartyBodyManager::GetBodyFileName(u16 bodyType, RFLSex sex) {
    return sBodyFileNames[bodyType][sex];
}

void RPPartyBodyManager::GetClothesName(u16 clothesType, RFLSex sex,
                                        const char** ppNameA,
                                        const char** ppNameB) {
    *ppNameA = RPPartyBodyManager::sClothesNames[clothesType][sex][0];
    *ppNameB = RPPartyBodyManager::sClothesNames[clothesType][sex][1];
}

void RPPartyBodyManager::GetHeadJointMtx(u16, RPGrpModel* pBodyModel,
                                         EGG::Matrix34f* pMtx) {
    RPSysKokeshiManager* pManager = RPSysKokeshiManager::instance();
    u16 jointIndex = pBodyModel->GetJointIndex("head_set");
    pBodyModel->GetWorldMtx(jointIndex, pMtx);
    pManager->GetModelBaseScaleInvalidatedMtx(pMtx, pBodyModel, pMtx);
}

void RPPartyBodyManager::GetLeftHandJointMtx(u16 bodyType,
                                             RPGrpModel* pBodyModel,
                                             EGG::Matrix34f* pMtx) {
    RPSysKokeshiManager* pManager = RPSysKokeshiManager::instance();
    u16 jointIndex;
    switch (bodyType) {
    case 0:
    case 1:
        jointIndex = pBodyModel->GetJointIndex("hand_l");
        break;
    default:
        jointIndex = pBodyModel->GetJointIndex("wrist_l");
        break;
    }
    pBodyModel->GetWorldMtx(jointIndex, pMtx);
    pManager->GetModelBaseScaleInvalidatedMtx(pMtx, pBodyModel, pMtx);
}

void RPPartyBodyManager::GetRightHandJointMtx(u16 bodyType,
                                              RPGrpModel* pBodyModel,
                                              EGG::Matrix34f* pMtx) {
    RPSysKokeshiManager* pManager = RPSysKokeshiManager::instance();
    u16 jointIndex;
    switch (bodyType) {
    case 0:
    case 1:
        jointIndex = pBodyModel->GetJointIndex("hand_r");
        break;
    default:
        jointIndex = pBodyModel->GetJointIndex("wrist_r");
        break;
    }
    pBodyModel->GetWorldMtx(jointIndex, pMtx);
    pManager->GetModelBaseScaleInvalidatedMtx(pMtx, pBodyModel, pMtx);
}

extern "C" const u16 lbl_8043ED78 = 12;
