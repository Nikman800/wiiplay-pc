#!/bin/sh
# wine_run.sh EXE [ARGS...]: runs a Windows test program under Wine, headless
# (docs/HOST_BUILD.md §41, "Running the tests under Wine"). port/host/Makefile writes
# $(OUT)/NAME scripts that exec this with NAME.exe.
#
# The prefix: WINEPREFIX if set, else one shared prefix, ~/.cache/wiiplay/wine (on disk:
# /tmp may be RAM). It is made once, under a lock, in a scratch directory that is moved
# into place only when complete: wineserver can crash while it sets up a new prefix
# (Ubuntu's wine 10.0, an integer divide error), leaving a half-made prefix and Wine
# processes that wait for it forever. A failed attempt is cleaned up and retried.
#
# The run: the program fails (exit 125) as soon as the prefix's wineserver is gone while
# it runs, and (exit 124) after WIIPLAY_WINE_TIMEOUT seconds (default 7200, the longest
# test script's own limit; 0: none), instead of waiting forever.
set -u
exe=$1
shift

WINE=${WINE:-wine}
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES="mscoree,mshtml="
unset DISPLAY WAYLAND_DISPLAY
# Wine offers no Direct3D 12 here: Vulkan (winevulkan on the host driver, lavapipe in the tests)
export WIIPLAY_BACKEND="${WIIPLAY_BACKEND:-vulkan}"

say() { echo "wine_run: $*" >&2; }

# Wine processes (loader, wineserver, Windows programs) using prefix $1. Only processes
# whose executable is Wine's: these scripts carry the same WINEPREFIX.
wine_pids() {
    for d in /proc/[0-9]*; do
        case $(readlink "$d/exe" 2>/dev/null) in */wine*) ;; *) continue ;; esac
        tr '\0' '\n' < "$d/environ" 2>/dev/null | grep -qxF "WINEPREFIX=$1" && echo "${d#/proc/}"
    done
}
server_running() {
    for p in $(wine_pids "$1"); do
        case $(readlink "/proc/$p/exe" 2>/dev/null) in */wineserver) return 0 ;; esac
    done
    return 1
}
kill_prefix() { pids=$(wine_pids "$1"); [ -n "$pids" ] && kill -9 $pids 2>/dev/null; return 0; }
prefix_ok() { [ -f "$1/system.reg" ] && [ -f "$1/drive_c/windows/system32/kernel32.dll" ]; }

if [ -n "${WINEPREFIX:-}" ]; then
    prefix_ok "$WINEPREFIX" || [ ! -e "$WINEPREFIX" ] || {
        say "WINEPREFIX=$WINEPREFIX is not a complete Wine prefix (no system.reg or kernel32.dll):"
        say "delete it, or unset WINEPREFIX to use the shared one"; exit 125; }
    shared=
else
    WINEPREFIX=${XDG_CACHE_HOME:-$HOME/.cache}/wiiplay/wine
    shared=1
fi
export WINEPREFIX

if ! prefix_ok "$WINEPREFIX"; then
    mkdir -p "$(dirname "$WINEPREFIX")"
    exec 9>"$WINEPREFIX.lock"
    flock 9
    n=0
    while ! prefix_ok "$WINEPREFIX"; do
        n=$((n + 1))
        if [ $n -gt 3 ]; then say "could not make a Wine prefix at $WINEPREFIX in 3 tries"; exit 125; fi
        [ -n "$shared" ] && [ -e "$WINEPREFIX" ] && rm -rf "$WINEPREFIX"   # a half-made shared one
        tmp="$WINEPREFIX.new.$$"
        rm -rf "$tmp"
        say "making the Wine prefix $WINEPREFIX (try $n)"
        WINEPREFIX=$tmp timeout -k 10 300 "$WINE" wineboot -i > /dev/null 2>&1
        # wineboot returns before its server has saved the registry: wait for it to go.
        t=0
        while server_running "$tmp" && [ $t -lt 60 ]; do sleep 1; t=$((t + 1)); done
        kill_prefix "$tmp"
        if prefix_ok "$tmp"; then
            mv "$tmp" "$WINEPREFIX"
        else
            say "Wine's set-up failed (wineserver crashed or hung); trying again"
            rm -rf "$tmp"
        fi
    done
    exec 9>&-
fi

limit=${WIIPLAY_WINE_TIMEOUT:-7200}
exec 3<&0    # an & job's stdin would otherwise be /dev/null
# its own process group: Wine starts a program's children detached from it (their
# parent becomes init), but they keep its process group, so the group stops them all
setsid "$WINE" "$exe" "$@" 0<&3 3<&- &
pid=$!
exec 3<&-
trap 'kill -- -$pid 2>/dev/null; exit 143' TERM INT HUP

start=$(date +%s)
delay=0.05
gone=0
while kill -0 $pid 2>/dev/null; do
    sleep $delay
    case $delay in 0.05) delay=0.2 ;; 0.2) delay=1 ;; 1) delay=2 ;; esac
    kill -0 $pid 2>/dev/null || break
    now=$(date +%s)
    if [ $((now - start)) -lt 2 ] || server_running "$WINEPREFIX"; then
        gone=0
    else
        # the loader starts the server first thing; two misses in a row: it is gone
        gone=$((gone + 1))
        if [ $gone -ge 2 ]; then
            say "wineserver for $WINEPREFIX exited while $exe ran (crashed?); stopping it"
            kill_prefix "$WINEPREFIX"
            wait $pid 2>/dev/null
            exit 125
        fi
    fi
    if [ "$limit" -gt 0 ] && [ $((now - start)) -ge "$limit" ]; then
        say "$exe still running after ${limit}s (WIIPLAY_WINE_TIMEOUT); stopping it"
        kill -- -$pid 2>/dev/null
        sleep 5
        kill -9 -- -$pid 2>/dev/null
        wait $pid 2>/dev/null
        exit 124
    fi
done
wait $pid
