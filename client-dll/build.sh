#!/usr/bin/env bash
# Builds PlayerHousing.dll and PlayerHousingLauncher.exe (32-bit Windows, like Wow.exe) into
# bin/, and runs the cursor ray test. Needs the MinGW-w64 cross compiler for 32-bit Windows
# (Ubuntu/Debian: g++-mingw-w64-i686) and a C++ compiler for the test; without the cross
# compiler here, it builds in a throwaway container with podman or docker:
#
#     client-dll/build.sh
set -euo pipefail
cd "$(dirname "$0")"

CXX=""
for candidate in i686-w64-mingw32-g++-posix i686-w64-mingw32-g++-win32 i686-w64-mingw32-g++; do
    if command -v "$candidate" >/dev/null 2>&1; then
        CXX=$candidate
        break
    fi
done

if [ -z "$CXX" ]; then
    runner=$(command -v podman || command -v docker || true)
    if [ -z "$runner" ]; then
        echo "No 32-bit MinGW compiler (i686-w64-mingw32-g++) and no podman or docker to build in." >&2
        exit 1
    fi
    echo "Building in a container with $(basename "$runner")..."
    exec "$runner" run --rm -v "$PWD:/src:z" -w /src docker.io/library/ubuntu:24.04 bash -c \
        "apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq g++-mingw-w64-i686 g++ >/dev/null && ./build.sh"
fi

CC=${CXX/g++/gcc}
mkdir -p bin obj
FLAGS=(-O2 -Wall -static -static-libgcc -s)

echo "MinHook..."
for source in minhook/src/buffer.c minhook/src/hook.c minhook/src/trampoline.c minhook/src/hde/hde32.c; do
    "$CC" "${FLAGS[@]}" -w -c "$source" -o "obj/$(basename "${source%.c}").o"
done

echo "PlayerHousing.dll..."
"$CXX" "${FLAGS[@]}" -std=c++17 -static-libstdc++ -shared -Iminhook/include -Isrc src/PlayerHousing.cpp obj/*.o \
    -o bin/PlayerHousing.dll -lversion -Wl,--exclude-all-symbols

echo "PlayerHousingLauncher.exe..."
"$CXX" "${FLAGS[@]}" -std=c++17 -static-libstdc++ -municode -mwindows src/Launcher.cpp -o bin/PlayerHousingLauncher.exe

rm -rf obj
if command -v g++ >/dev/null 2>&1; then
    g++ -std=c++17 -O2 -Isrc test/cursor_ray_test.cpp -o /tmp/cursor_ray_test.$$ && /tmp/cursor_ray_test.$$
    rm -f /tmp/cursor_ray_test.$$
fi
ls -l bin
