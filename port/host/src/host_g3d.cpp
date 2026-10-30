/**
 * Host stand-in for nw4r::g3d (port/docs/HOST_BUILD.md §20).
 *
 * AnmObj::Release is declared (non-pure) in g3d_anmobj.h but has no definition
 * in the decomp: every concrete animation object overrides it and nothing calls
 * the base version, so the Wii binary has neither the function nor an AnmObj
 * vtable. GCC emits a class's vtable next to its first non-inline virtual
 * function, so the host needs the definition for the AnmObj constructor's
 * vtable. It is never called; it aborts if it is.
 */
#include <nw4r/g3d.h>

#include <cstdio>
#include <cstdlib>

namespace nw4r {
namespace g3d {

void AnmObj::Release() {
    std::fprintf(stderr, "host_g3d: g3d::AnmObj::Release called on the host\n");
    std::abort();
}

} // namespace g3d
} // namespace nw4r
