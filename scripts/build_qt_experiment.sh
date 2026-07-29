#!/usr/bin/env bash
# Build the Qt Quick UI experiment (osv-qt) — standalone CMake, premake untouched.
#
# Builds run niced and with capped parallelism so the desktop stays responsive
# (owner request). Override the job count with OSV_QT_BUILD_JOBS.
set -euo pipefail
cd "$(dirname "$0")/.."
JOBS="${OSV_QT_BUILD_JOBS:-$(( $(nproc) / 2 ))}"
(( JOBS < 1 )) && JOBS=1
nice -n 19 cmake -S src/qtui -B build/qt-experiment -G Ninja -DCMAKE_BUILD_TYPE=Debug
nice -n 19 cmake --build build/qt-experiment -- -j "$JOBS"
echo "binary: build/qt-experiment/osv-qt"
