/**
 * Harness-only stand-in for include/Pack/RPSystem/RPSysResourceManager.h
 * (port/docs/HOST_BUILD.md §14).
 *
 * The step 8 loader units (RPResTable, the level/map/question tables) only call
 * the two static lookups below. The real header pulls in the scene creator and,
 * through it, RPAudio headers that do not compile on the host yet (their
 * `unsigned int` / `u32` overload pairs), so these units are compiled for the
 * harnesses with tests/shadow first on the include path. The declarations are
 * copied from the real header (same signatures, so the same symbols); the
 * harness defines them to return the archive and file the disc walk mounted.
 */
#ifndef RP_SYSTEM_RESOURCE_MANAGER_H
#define RP_SYSTEM_RESOURCE_MANAGER_H
#include <Pack/types_pack.h>

#include <egg/core.h>

class RPSysResourceManager {
public:
    static EGG::Archive* LoadGameCommonArchive(s32 id = -1, EGG::Heap* pHeap = NULL);
    static void* GetFileFromArchive(EGG::Archive* pArchive, const char* pName,
                                    u32* pSize = NULL);
};

#endif
