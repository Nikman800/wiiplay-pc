#!/bin/bash
# make_source_zip.sh OUT_DIR AURORA_BUILD: the source code of the Windows package, for
# the GPL (wiiplay.exe is GPL-2.0-or-later: README, Legal). port/host/Makefile's `dist`
# runs it next to wiiplay-windows-x64.zip. Writes
#   OUT_DIR/wiiplay-source.zip              this repository and the Aurora submodule: the
#                                           files git tracks, as they are in the working
#                                           tree (what was built), and SOURCE.txt
#   OUT_DIR/wiiplay-third-party-source.zip  the libraries Aurora's build fetched and
#                                           compiled into wiiplay_gx.dll (CMake's _deps/*-src),
#                                           unmodified upstream releases, with their licences
# No Nintendo data: git tracks none (tools/release_check.sh).
set -euo pipefail
out=$(realpath "$1")
aurora_build=$(realpath "$2")
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel 2>/dev/null) || {
    echo "dist: not a git checkout (built from a source zip?): no source zips made; pass" \
         "on the source zips this tree came from" >&2
    exit 0
}
stage="$out/dist-src"
rm -rf "$stage" "$out/wiiplay-source.zip" "$out/wiiplay-third-party-source.zip"
mkdir -p "$stage/wiiplay-source" "$stage/wiiplay-third-party-source"

# Tracked files of $1 (a repository) into $2, submodules (mode 160000) skipped.
copy_tracked() {
    (cd "$1" && git ls-files -s -z | while IFS= read -r -d '' entry; do
        mode=${entry%% *}
        path=${entry#*$'\t'}
        [ "$mode" = 160000 ] && continue
        [ -e "$path" ] || continue   # deleted in the working tree: not built
        printf '%s\0' "$path"
    done | xargs -0 cp -P --parents -t "$2")
}
copy_tracked "$repo" "$stage/wiiplay-source"
untracked=$(git -C "$repo" ls-files --others --exclude-standard -- port tools include src config configure.py)
if [ -n "$untracked" ]; then
    echo "dist: warning: files git does not track are left out of wiiplay-source.zip" \
         "(git add them if the build uses them):" >&2
    echo "$untracked" | sed 's/^/  /' | head -20 >&2
fi
subs=()
while read -r _ sub_path _; do
    [ -n "$sub_path" ] || continue
    mkdir -p "$stage/wiiplay-source/$sub_path"
    copy_tracked "$repo/$sub_path" "$stage/wiiplay-source/$sub_path"
    subs+=("$sub_path")
done < <(git -C "$repo" submodule status)

state() { # $1: repository; "COMMIT" or "COMMIT plus local changes"
    local c
    c=$(git -C "$1" rev-parse HEAD)
    if git -C "$1" diff --quiet HEAD -- 2>/dev/null; then echo "$c"; else echo "$c plus uncommitted changes (included as built)"; fi
}
dawn_version=$(sed -n 's/^AURORA_DAWN_VERSION:STRING=//p' "$aurora_build/CMakeCache.txt")
dawn_ref=$(sed -n 's/^AURORA_DAWN_REF:STRING=//p' "$aurora_build/CMakeCache.txt")
{
    echo "Source code of the Wii Play PC port (and of the Windows package, wiiplay-windows-x64.zip,"
    echo "when one is made with it: make -C port/host WIN64=1 dist)."
    echo
    echo "wiiplay-pc   $(state "$repo")"
    for s in "${subs[@]}"; do
        echo "$s   $(state "$repo/$s")"
    done
    echo "packaged     $(date -u +%Y-%m-%dT%H:%MZ)"
    echo
    echo "Building: port/docs/RUNNING.md, \"Windows\" (on Linux: make -C port/host WIN64=1 dist)."
    echo "You need your own Wii Play disc to play; this archive holds no Nintendo data."
    echo
    echo "Licences: CC0-1.0 (LICENSE) except the files README.md (Legal) lists; wiiplay.exe"
    echo "includes GPL-2.0-or-later code from the Dolphin emulator, so it is GPL-2.0-or-later"
    echo "as a whole (LICENSES/GPL-2.0-or-later.txt). Aurora: MIT (port/extern/aurora/LICENSE)."
    echo
    echo "Libraries compiled into wiiplay_gx.dll: wiiplay-third-party-source.zip, as fetched"
    echo "(pinned by URL and SHA-256 in port/extern/aurora/extern/CMakeLists.txt and cmake/)."
    echo
    echo "Shipped as unmodified prebuilt DLLs, not in these archives:"
    echo "  webgpu_dawn.dll, dxcompiler.dll, dxil.dll: Dawn (BSD-3-Clause) and the DirectX"
    echo "  Shader Compiler (University of Illinois/NCSA), the package"
    echo "  https://github.com/encounter/dawn/releases/tag/${dawn_version:-?}"
    echo "  built from https://github.com/encounter/dawn/tree/${dawn_ref:-?}"
    echo "  libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll: the MinGW-w64 GCC runtime"
    echo "  (GPL with the GCC Runtime Library Exception; winpthreads MIT/BSD), from the"
    echo "  distribution's mingw-w64 packages."
} > "$stage/wiiplay-source/SOURCE.txt"

# The fetched libraries' sources (Dawn's is a prebuilt package: listed above instead)
for d in "$aurora_build"/_deps/*-src; do
    name=$(basename "$d" -src)
    [ "$name" = dawn_prebuilt ] && continue
    cp -r "$d" "$stage/wiiplay-third-party-source/$name"
    rm -rf "$stage/wiiplay-third-party-source/$name/.git"
done
{
    echo "Sources of the libraries Aurora's build fetched and compiled into wiiplay_gx.dll,"
    echo "as fetched (unmodified upstream releases; each folder keeps its own licence)."
    echo "The URLs and SHA-256 hashes: port/extern/aurora/extern/CMakeLists.txt and cmake/"
    echo "in wiiplay-source.zip."
    echo
    ls "$stage/wiiplay-third-party-source" | grep -v '^README.txt$'
} > "$stage/wiiplay-third-party-source/README.txt"

(cd "$stage" && zip -q -r -X -y "$out/wiiplay-source.zip" wiiplay-source &&
    zip -q -r -X -y "$out/wiiplay-third-party-source.zip" wiiplay-third-party-source)
rm -rf "$stage"
echo "dist: $out/wiiplay-source.zip ($(du -h "$out/wiiplay-source.zip" | cut -f1)), " \
     "$out/wiiplay-third-party-source.zip ($(du -h "$out/wiiplay-third-party-source.zip" | cut -f1))"
