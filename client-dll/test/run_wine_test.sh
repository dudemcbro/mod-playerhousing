#!/usr/bin/env bash
# Tests PlayerHousing.dll and PlayerHousingLauncher.exe under Wine with a stand-in for Wow.exe
# (test/fake_game.cpp). Run after build.sh; needs Wine that runs 32-bit programs, or it runs in
# a throwaway container with podman or docker:
#
#     client-dll/test/run_wine_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX=""
for candidate in i686-w64-mingw32-g++-posix i686-w64-mingw32-g++-win32 i686-w64-mingw32-g++; do
    command -v "$candidate" >/dev/null 2>&1 && { CXX=$candidate; break; }
done
if [ -z "$CXX" ] || ! command -v wine >/dev/null 2>&1; then
    runner=$(command -v podman || command -v docker || true)
    image=${WINE_IMAGE:-}
    if [ -z "$runner" ]; then
        echo "Needs Wine and the 32-bit MinGW compiler, or podman or docker to test in." >&2
        exit 1
    fi
    setup="true"
    if [ -z "$image" ]; then
        image=docker.io/library/ubuntu:24.04
        setup="dpkg --add-architecture i386 && apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends wine wine32:i386 xvfb xauth >/dev/null"
    fi
    exec "$runner" run --rm -v "$PWD:/src:z" -w /src "$image" bash -c \
        "$setup && (command -v i686-w64-mingw32-g++ >/dev/null || (apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq g++-mingw-w64-i686 >/dev/null)) && test/run_wine_test.sh"
fi

WINDRES=${CXX/g++*/windres}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$WINDRES" test/fake_game.rc -O coff -o "$work/version.o"
"$CXX" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -Isrc test/fake_game.cpp "$work/version.o" -o "$work/fake_game.exe"
cp bin/PlayerHousing.dll bin/PlayerHousingLauncher.exe "$work/"

export WINEDEBUG=-all WINEARCH=win32 WINEPREFIX="$work/prefix"
wine=(wine)
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
    wine=(xvfb-run -a wine)
fi
"${wine[@]}" wineboot -i >/dev/null 2>&1 || true

echo "The DLL in the fake game:"
status=0
"${wine[@]}" "$work/fake_game.exe" | tr -d '\r' || status=1
echo "  log: $(tr -d '\r' < "$work/PlayerHousing.log" | tr '\n' ' ')"

echo "The launcher starting the fake game:"
rm -f "$work/fake_game.result" "$work/PlayerHousing.log"
"${wine[@]}" "$work/PlayerHousingLauncher.exe" "$work/fake_game.exe" --launched "two words" third || true
for _ in $(seq 1 30); do
    [ -f "$work/fake_game.result" ] && break
    sleep 1
done
result=$(tr -d '\r' < "$work/fake_game.result" 2>/dev/null || echo "no result")
echo "  $result"
echo "  log: $(tr -d '\r' < "$work/PlayerHousing.log" 2>/dev/null | tr '\n' ' ')"
if [ "$result" != "dll loaded; args: [--launched] [two words] [third]" ]; then
    echo "FAIL: the launcher"
    status=1
else
    echo "PASS: the launcher"
fi
exit $status
