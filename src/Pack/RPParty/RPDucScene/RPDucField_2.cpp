// Shooting Range field (RPDucField): ground, clouds and the three rows of grass tufts.
// Unit 0x802370B8-0x80237774 (dtk/game_units.tsv). Functions in reverse address order.
#include <Pack/RPParty/RPDucScene/RPDucField_2.h>

static const f32 sGrassRow2X[] = {-64.0f, -23.0f, 2.0f, 19.0f, 38.0f, 54.0f};
static const f32 sGrassRow3X[] = {-49.0f, -31.0f, -11.0f, 8.0f, 27.0f, 53.0f};

RPDucField* RPDucField::sInstance;

void RPDucField::CreateInstance() {
    sInstance = new RPDucField();
}

void RPDucField::DestroyInstance() {
    delete sInstance;
    sInstance = NULL;
}

RPDucField::~RPDucField() {}

void RPDucField::Create() {
    mpGround = new RPDucShtField();
    mpGround->Create();
    mpCloud = new RPDucShtCloud();
    mpCloud->Create();
    {
        RPDucShtGrass* pGrass;
        int i = 0;
        do {
            mpGrass[i] = new RPDucShtGrass();
            mpGrass[i]->Create();
        } while (++i < 27);
    }
}

void RPDucField::Load() {
    mpGround->Load();
    mpCloud->Load();
    for (int i = 0; i < 27; i++) {
        mpGrass[i]->Load();
    }
}

void RPDucField::Reset() {
    mpGround->Reset();
    mpCloud->Reset();
    for (int i = 0; i < 27; i++) {
        mpGrass[i]->Reset();
    }

    {
        // NONMATCHING-DEBT: the first row walks a byte-stepped RPDucField* and loads
        // mpGrass[0] (0xC) from it. mpGrass[i] gives the same code with registers rotated
        // (10 words in Calculate); an RPDucShtGrass** walker over mpGrass folds the 0xC into the
        // pointer (6 words); RPDucFieldSetPos like the other rows differs more.
        // Allocator replay (tools/mwcc_trace/sim.py on the natural do/while with a block-scope
        // pGrass = mpGrass[i]): the only numbering that gives the original r4/r5/r6 is walker <
        // pGrass < i, i.e. the walker must be numbered below both named locals. The natural walker
        // is the optimizer's strength-reduction temporary, always numbered above them, so only a
        // named walker (this form) reaches it. for/while forms get unrolled (86 words).
        int i = 0;
        RPDucShtGrass* pGrass;
        RPDucField* pField = this;
        do {
            pGrass = pField->mpGrass[0];
            pGrass->mPos.x = 13.0f * i + -91.0f;
            pGrass->mPos.y = 0.0f;
            pGrass->mPos.z = 60.0f;
            pField = reinterpret_cast<RPDucField*>(reinterpret_cast<u8*>(pField) + sizeof(RPDucShtGrass*));
        } while (++i < 15);
    }
    {
        int i = 15;
        do {
            RPDucFieldSetPos(mpGrass[i]->mPos, sGrassRow2X[i - 15], 0.0f, 72.0f);
        } while (++i < 21);
    }
    {
        int i = 21;
        do {
            RPDucFieldSetPos(mpGrass[i]->mPos, sGrassRow3X[i - 21], 0.0f, 84.0f);
        } while (++i < 27);
    }

    mpGrass[(u16)RPDucFieldRandU32() % 15u]->mHasSecret = true;
    mpGrass[(u16)RPDucFieldRandU32() % 6u + 15]->mHasSecret = true;
    mpGrass[(u16)RPDucFieldRandU32() % 6u + 21]->mHasSecret = true;
}

void RPDucField::Calculate() {
    mpGround->Calculate();
    mpCloud->Calculate();
    for (int i = 0; i < 27; i++) {
        mpGrass[i]->Calculate();
    }

    {
        // NONMATCHING-DEBT: the same byte-stepped walker as in Reset (see the comment there).
        int i = 0;
        RPDucShtGrass* pGrass;
        RPDucField* pField = this;
        do {
            pGrass = pField->mpGrass[0];
            pGrass->mPos.x = 13.0f * i + -91.0f;
            pGrass->mPos.y = 0.0f;
            pGrass->mPos.z = 60.0f;
            pField = reinterpret_cast<RPDucField*>(reinterpret_cast<u8*>(pField) + sizeof(RPDucShtGrass*));
        } while (++i < 15);
    }
    {
        int i = 15;
        do {
            RPDucFieldSetPos(mpGrass[i]->mPos, sGrassRow2X[i - 15], 0.0f, 72.0f);
        } while (++i < 21);
    }
    {
        int i = 21;
        do {
            RPDucFieldSetPos(mpGrass[i]->mPos, sGrassRow3X[i - 21], 0.0f, 84.0f);
        } while (++i < 27);
    }
}

void RPDucField::CalcModelMtx() {
    mpGround->CalcModelMtx();
    mpCloud->CalcModelMtx();
    for (int i = 0; i < 27; i++) {
        mpGrass[i]->CalcModelMtx();
    }
}
