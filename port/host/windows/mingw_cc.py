#!/usr/bin/env python3
"""The MinGW-w64 compiler driver for the Windows build (port/host/Makefile, WIN64=1;
port/docs/HOST_BUILD.md §41), with weak definitions made to work at link time.

    mingw_cc.py REAL-DRIVER ARGS...

Compiles (-c, -E, -S, -M) go straight to the real driver. A link is run as given; if
it fails with undefined references, each undefined symbol W that some input defines
weakly gets that default and the link is retried:

    -Wl,-u,.weak.W.ANCHOR -Wl,--defsym,W=.weak.W.ANCHOR

Why: on PE/COFF GCC emits a weak definition of W as a weak external W whose default
is the global `.weak.W.ANCHOR` (ANCHOR: the unit's first global symbol). The GNU ld
in use (2.45.90) never lets such a weak external satisfy another object's reference
to W, and an archive's index does not list W at all, so the host's weak defaults
(src/host_sdk.cpp, host_gx.cpp, host_gfx_stubs.c, ...) are never linked. A strong
definition of W, linked or pulled from an archive, still wins as on ELF: only
symbols the link leaves undefined get a default, in the order the inputs are given.

The C++ runtime defines operator new / delete too, so those references never stay
undefined; on ELF the executable's archive member still wins (archives come before
the runtime library). Their weak defaults are applied up front, unless an input
defines them strongly (the game's eggHeap.cpp).

Then, with --gc-sections, undefined references from sections the link discards: ELF's
ld ignores them (the harnesses link whole units and rely on that), PE's reports them
and has no --unresolved-symbols. If every reference left is in a section the link
removes (--print-gc-sections), each such symbol is aliased to abort, which nothing
kept can reach; any reference from a kept section still fails the link.
"""

import os
import re
import subprocess
import sys

COMPILE_ONLY = {"-c", "-E", "-S", "-M", "-MM", "-fsyntax-only"}
UNDEF_RE = re.compile(r"undefined reference to `([^']+)'")
# "...:(.text$NAME+0x1c): undefined reference to `W'"
UNDEF_AT_RE = re.compile(r"\((\.[^()+]+)\+0x[0-9a-fA-F]+\): undefined reference to `([^']+)'")
REMOVED_RE = re.compile(r"removing unused section '([^']+)'")


OPERATOR_RE = re.compile(r"^_Z(nw|na|dl|da)")


def weak_defaults(inputs, nm):
    """W -> `.weak.W.ANCHOR` for every weak default in the inputs (first one wins),
    and the set of symbols the inputs define strongly."""
    found = {}
    strong = set()
    for path in inputs:
        try:
            out = subprocess.run([nm, "--defined-only", path], capture_output=True, text=True).stdout
        except OSError:
            continue
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1] in "TDRB" and not parts[2].startswith(".weak."):
                strong.add(parts[2])
            if len(parts) != 3 or parts[1] not in "TDRB" or not parts[2].startswith(".weak."):
                continue
            name = parts[2]
            sym = name[len(".weak."):].split(".", 1)[0]
            found.setdefault(sym, name)
    return found, strong


def gc_only_undefined(driver, cmd, stderr):
    """The undefined symbols if every reference to them is in a removed section, else None."""
    if "-Wl,--gc-sections" not in cmd:
        return None
    refs = UNDEF_AT_RE.findall(stderr)
    if not refs or len(refs) != len(UNDEF_RE.findall(stderr)):
        return None
    r = subprocess.run([driver] + cmd + ["-Wl,--print-gc-sections"], capture_output=True, text=True)
    removed = set(REMOVED_RE.findall(r.stderr))
    kept = [ref for ref in refs if ref[0] not in removed]
    if kept:
        if os.environ.get("MINGW_CC_DEBUG"):
            sys.stderr.write("mingw_cc: referenced from kept sections: %s\n" % kept[:10])
        return None
    return {w for _, w in refs}


def main():
    driver, args = sys.argv[1], sys.argv[2:]
    if any(a in COMPILE_ONLY for a in args) or not args:
        os.execvp(driver, [driver] + args)

    nm = driver.rsplit("-", 2)[0] + "-nm" if driver.endswith(("-gcc-posix", "-g++-posix")) else "nm"
    inputs = [a for a in args if a.endswith((".o", ".a")) and not a.startswith("-") and os.path.isfile(a)]
    defaults, strong = weak_defaults(inputs, nm)
    extra = []
    done = set()
    for w in sorted(defaults):
        if OPERATOR_RE.match(w) and w not in strong:
            extra += ["-Wl,-u," + defaults[w], "-Wl,--defsym," + w + "=" + defaults[w]]
            done.add(w)
    while True:
        r = subprocess.run([driver] + args + ["-Wl,--no-demangle"] + extra, capture_output=True, text=True)
        if r.returncode == 0:
            sys.stdout.write(r.stdout)
            sys.stderr.write(r.stderr)
            return 0
        undefined = set(UNDEF_RE.findall(r.stderr)) - done
        fixable = sorted(w for w in undefined if defaults and w in defaults)
        if os.environ.get("MINGW_CC_DEBUG"):
            sys.stderr.write("mingw_cc: weak defaults for %s\n" % fixable)
        if not fixable:
            dropped = gc_only_undefined(driver, args + ["-Wl,--no-demangle"] + extra, r.stderr)
            if not dropped:
                sys.stdout.write(r.stdout)
                sys.stderr.write(r.stderr)
                return r.returncode
            extra += ["-Wl,-u,abort"] + ["-Wl,--defsym," + w + "=abort" for w in sorted(dropped)]
            done |= dropped
            continue
        for w in fixable:
            extra += ["-Wl,-u," + defaults[w], "-Wl,--defsym," + w + "=" + defaults[w]]
            done.add(w)


if __name__ == "__main__":
    sys.exit(main())
