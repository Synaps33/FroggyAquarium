#!/bin/bash
# Builds the aquarium binary into build/ (native compile; CI runs this on arm64).
set -e
cd "$(dirname "$0")"
mkdir -p build
CXXFLAGS="-std=c++17 -O2 -Wall $(pkg-config --cflags sdl2 glesv2 zlib)"
LIBS="$(pkg-config --libs sdl2 glesv2 zlib)"
g++ $CXXFLAGS -o build/aquarium src/*.cpp $LIBS
strip build/aquarium
echo "built: build/aquarium"
