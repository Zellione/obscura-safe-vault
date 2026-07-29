#!/usr/bin/env bash
# Build the Qt Quick UI experiment (osv-qt) — standalone CMake, premake untouched.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S src/qtui -B build/qt-experiment -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt-experiment
echo "binary: build/qt-experiment/osv-qt"
