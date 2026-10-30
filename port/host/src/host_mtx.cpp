/**
 * RVL MTX functions that the decomp does not contain (the Wii's are
 * paired-single library code that is not decompiled): written from the SDK's
 * documented C reference forms (C_MTX*). nw4r::math and the game call them
 * (math_types.h inlines, MTX34RotAxisFIdx, ...). They are not bit-exact to
 * the Wii's paired-single versions, which round some sums differently; see
 * port/docs/HOST_BUILD.md §18. PSMTXScale / PSMTXTransApply / PSMTXRotRad
 * are in host_gx.cpp; the functions that are in src/revolution/MTX are built
 * from there with their host forms.
 */

#include <revolution/MTX.h>

#include <cmath>
#include <cstring>

extern "C" {

void PSMTXTrans(Mtx m, f32 x, f32 y, f32 z) {
    PSMTXIdentity(m);
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
}

void PSMTXScaleApply(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    const f32 s[3] = {x, y, z};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            dst[i][j] = src[i][j] * s[i];
        }
    }
}

void PSMTXTranspose(const Mtx src, Mtx dst) {
    Mtx t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            t[i][j] = src[j][i];
        }
        t[i][3] = 0.0f;
    }
    std::memcpy(dst, t, sizeof(Mtx));
}

u32 PSMTXInverse(const Mtx src, Mtx inv) {
    f32 det = src[0][0] * src[1][1] * src[2][2] + src[0][1] * src[1][2] * src[2][0] +
              src[0][2] * src[1][0] * src[2][1] - src[2][0] * src[1][1] * src[0][2] -
              src[1][0] * src[0][1] * src[2][2] - src[0][0] * src[2][1] * src[1][2];
    if (det == 0.0f) {
        return FALSE;
    }
    det = 1.0f / det;
    Mtx t;
    t[0][0] = (src[1][1] * src[2][2] - src[2][1] * src[1][2]) * det;
    t[0][1] = -(src[0][1] * src[2][2] - src[2][1] * src[0][2]) * det;
    t[0][2] = (src[0][1] * src[1][2] - src[1][1] * src[0][2]) * det;
    t[1][0] = -(src[1][0] * src[2][2] - src[2][0] * src[1][2]) * det;
    t[1][1] = (src[0][0] * src[2][2] - src[2][0] * src[0][2]) * det;
    t[1][2] = -(src[0][0] * src[1][2] - src[1][0] * src[0][2]) * det;
    t[2][0] = (src[1][0] * src[2][1] - src[2][0] * src[1][1]) * det;
    t[2][1] = -(src[0][0] * src[2][1] - src[2][0] * src[0][1]) * det;
    t[2][2] = (src[0][0] * src[1][1] - src[1][0] * src[0][1]) * det;
    for (int i = 0; i < 3; i++) {
        t[i][3] = -t[i][0] * src[0][3] - t[i][1] * src[1][3] - t[i][2] * src[2][3];
    }
    std::memcpy(inv, t, sizeof(Mtx));
    return TRUE;
}

u32 PSMTXInvXpose(const Mtx src, Mtx invX) {
    Mtx inv;
    if (!PSMTXInverse(src, inv)) {
        return FALSE;
    }
    Mtx t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            t[i][j] = inv[j][i];
        }
        t[i][3] = 0.0f;
    }
    std::memcpy(invX, t, sizeof(Mtx));
    return TRUE;
}

void PSMTXRotTrig(Mtx m, f32 sinA, f32 cosA, char axis) {
    std::memset(m, 0, sizeof(Mtx));
    switch (axis) {
    case 'x':
    case 'X':
        m[0][0] = 1.0f;
        m[1][1] = cosA;
        m[1][2] = -sinA;
        m[2][1] = sinA;
        m[2][2] = cosA;
        break;
    case 'y':
    case 'Y':
        m[0][0] = cosA;
        m[0][2] = sinA;
        m[1][1] = 1.0f;
        m[2][0] = -sinA;
        m[2][2] = cosA;
        break;
    case 'z':
    case 'Z':
        m[0][0] = cosA;
        m[0][1] = -sinA;
        m[1][0] = sinA;
        m[1][1] = cosA;
        m[2][2] = 1.0f;
        break;
    default:
        break;
    }
}

void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
    f32 s = std::sin(rad), c = std::cos(rad), t = 1.0f - c;
    Vec v;
    PSVECNormalize(axis, &v);
    f32 x = v.x, y = v.y, z = v.z;
    m[0][0] = t * x * x + c;
    m[0][1] = t * x * y - s * z;
    m[0][2] = t * x * z + s * y;
    m[0][3] = 0.0f;
    m[1][0] = t * x * y + s * z;
    m[1][1] = t * y * y + c;
    m[1][2] = t * y * z - s * x;
    m[1][3] = 0.0f;
    m[2][0] = t * x * z - s * y;
    m[2][1] = t * y * z + s * x;
    m[2][2] = t * z * z + c;
    m[2][3] = 0.0f;
}

void PSMTXQuat(Mtx m, const Quaternion* q) {
    f32 s = 2.0f / (q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    f32 xs = q->x * s, ys = q->y * s, zs = q->z * s;
    f32 wx = q->w * xs, wy = q->w * ys, wz = q->w * zs;
    f32 xx = q->x * xs, xy = q->x * ys, xz = q->x * zs;
    f32 yy = q->y * ys, yz = q->y * zs, zz = q->z * zs;
    m[0][0] = 1.0f - (yy + zz);
    m[0][1] = xy - wz;
    m[0][2] = xz + wy;
    m[0][3] = 0.0f;
    m[1][0] = xy + wz;
    m[1][1] = 1.0f - (xx + zz);
    m[1][2] = yz - wx;
    m[1][3] = 0.0f;
    m[2][0] = xz - wy;
    m[2][1] = yz + wx;
    m[2][2] = 1.0f - (xx + yy);
    m[2][3] = 0.0f;
}

void C_MTXLookAt(Mtx m, const Vec* camPos, const Vec* camUp, const Vec* target) {
    Vec look = {camPos->x - target->x, camPos->y - target->y, camPos->z - target->z};
    PSVECNormalize(&look, &look);
    Vec right;
    PSVECCrossProduct(camUp, &look, &right);
    PSVECNormalize(&right, &right);
    Vec up;
    PSVECCrossProduct(&look, &right, &up);
    m[0][0] = right.x;
    m[0][1] = right.y;
    m[0][2] = right.z;
    m[0][3] = -(camPos->x * right.x + camPos->y * right.y + camPos->z * right.z);
    m[1][0] = up.x;
    m[1][1] = up.y;
    m[1][2] = up.z;
    m[1][3] = -(camPos->x * up.x + camPos->y * up.y + camPos->z * up.z);
    m[2][0] = look.x;
    m[2][1] = look.y;
    m[2][2] = look.z;
    m[2][3] = -(camPos->x * look.x + camPos->y * look.y + camPos->z * look.z);
}

void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS, f32 scaleT,
                       f32 transS, f32 transT) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = ((2.0f * n) * tmp) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = (((r + l) * tmp) * scaleS) - transS;
    m[0][3] = 0.0f;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = ((2.0f * n) * tmp) * scaleT;
    m[1][2] = (((t + b) * tmp) * scaleT) - transT;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS,
                           f32 transT) {
    f32 angle = fovY * 0.5f * (3.14159265358979323846f / 180.0f);
    f32 cot = 1.0f / std::tan(angle);
    m[0][0] = (cot / aspect) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = -transS;
    m[0][3] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = cot * scaleT;
    m[1][2] = -transT;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS,
                     f32 transT) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = (2.0f * tmp) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = ((-(r + l) * tmp) * scaleS) + transS;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = (2.0f * tmp) * scaleT;
    m[1][2] = 0.0f;
    m[1][3] = ((-(t + b) * tmp) * scaleT) + transT;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 0.0f;
    m[2][3] = 1.0f;
}

} // extern "C"
