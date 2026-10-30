/**
 * RPPolyline::Calc on the host (POINTERS.md §5.1; port/docs/HOST_BUILD.md §16).
 *
 * The Wii code walks its control points with `offset + reinterpret_cast<u32>(points)`
 * and loads mPointNum with inline assembly; the host forms are pointer-sized
 * arithmetic and a plain member read. This runs the real Calc for the
 * five-point paths the Duck Hunt / Tanks / Billiards cameras build (and for
 * 2 to 8 points) and compares every result, bit for bit, with de Casteljau's
 * algorithm written out here with the same nw4r::math::VEC3Lerp.
 */
#include <Pack/RPParty/RPPartyCommon/RPPolyline.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int sErrors = 0, sChecks = 0;

void Reference(const std::vector<nw4r::math::VEC3>& rPoints, f32 t, nw4r::math::VEC3* pOut) {
    std::vector<nw4r::math::VEC3> work(rPoints);
    for (size_t n = work.size(); n > 1; n--) {
        for (size_t i = 0; i + 1 < n; i++) {
            nw4r::math::VEC3 v;
            nw4r::math::VEC3Lerp(&v, &work[i], &work[i + 1], t);
            work[i] = v;
        }
    }
    *pOut = work[0];
}

} // namespace

int main() {
    for (s32 num = 3; num <= 8; num++) {
        std::vector<nw4r::math::VEC3> points;
        for (s32 i = 0; i < num; i++) {
            points.push_back(nw4r::math::VEC3(100.0f * i - 37.5f, 12.25f * i * i - 300.0f,
                                              -55.0f + 3.0f * i));
        }

        RPPolyline line(num);
        line.SetPoints(points.data());

        for (int step = 0; step <= 64; step++) {
            f32 t = step / 64.0f;
            nw4r::math::VEC3 got, want;
            line.Calc(t, &got);
            Reference(points, t, &want);
            sChecks++;
            // At t = 0 the curve starts at the first control point.
            if (step == 0 && std::memcmp(&got, &points[0], sizeof(got)) != 0) {
                std::fprintf(stderr, "RPPolyline(%d).Calc(0) is not the first point\n", num);
                sErrors++;
            }
            if (std::memcmp(&got, &want, sizeof(got)) != 0 && sErrors++ < 10) {
                std::fprintf(stderr, "RPPolyline(%d).Calc(%g): (%g %g %g), want (%g %g %g)\n",
                             num, t, got.x, got.y, got.z, want.x, want.y, want.z);
            }
        }
    }

    std::printf("test_polyline: %d checks, %d errors\n", sChecks, sErrors);
    return sErrors == 0 ? 0 : 1;
}
