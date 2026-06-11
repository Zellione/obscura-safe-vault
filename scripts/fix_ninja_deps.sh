#!/usr/bin/env bash
# fix_ninja_deps.sh — repair header-dependency tracking in premake-generated Ninja.
#
# premake5 beta8's Ninja exporter emits compile rules that declare
#     deps = gcc
#     depfile = $out.d
# but the compile *command* is `g++ $cxxflags -c $in -o $out` — it never passes
# `-MF $out.d`, so the compiler never writes the depfile. The result: Ninja has
# no header-dependency information, so editing a header does NOT rebuild the
# translation units that include it. Stale object files then link into a broken
# binary (e.g. a struct whose layout changed in a header but whose other .o's
# were not recompiled → wild pointers / crashes).
#
# This script injects the missing `-MMD -MF $out.d` into the C and C++ compile
# commands of every generated *.ninja file. It is idempotent (premake rewrites
# the files on each generation; the grep guard also prevents double-patching).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

shopt -s nullglob
for f in *.ninja; do
    # Already patched? (also guards against running twice without regenerating)
    grep -q -- '-MMD -MF $out.d -c $in -o $out' "$f" && continue
    # Only the cc/cxx compile rules end in `-c $in -o $out`; the link rule and
    # the PCH rule have a different shape, so they are left untouched.
    sed -i 's| -c $in -o $out| -MMD -MF $out.d -c $in -o $out|' "$f"
done
