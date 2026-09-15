#!/bin/sh
# Turn an installed kobodl.app into one that runs on someone else's Mac.
#
# As built, the app is not distributable. It links libSDL2 by the absolute path
# /opt/homebrew/opt/sdl2/lib/..., finds libSDL2_image through an RPATH pointing
# into this checkout, and loads its graphics and sound from an install prefix
# that will not exist on anyone else's machine. Each of those is a dyld failure
# or a missing-data error at launch.
#
# This copies the two libraries and the game data into the bundle and rewrites
# every reference to be relative to the executable, so the .app is
# self-contained.
#
# Usage: tools/port/bundle-macos.sh <install-prefix> [share-dir]
set -eu

PREFIX="${1:?usage: bundle-macos.sh <install-prefix> [share-dir]}"
SHARE="${2:-$PREFIX/share/kobodeluxe}"
APP="$PREFIX/bin/kobodl.app"
EXE="$APP/Contents/MacOS/kobodl"
FW="$APP/Contents/Frameworks"
RES="$APP/Contents/Resources"

# Call the toolchain binaries directly. The /usr/bin/* wrappers are xcrun
# shims that try to write a cache into the system temp dir, which fails inside
# a sandbox with an error that has nothing to do with the actual task.
TC=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
INT="$TC/install_name_tool"
OTOOL="$TC/llvm-otool"

[ -x "$EXE" ] || { echo "FAIL: no executable at $EXE"; exit 1; }
[ -d "$SHARE" ] || { echo "FAIL: no game data at $SHARE"; exit 1; }

mkdir -p "$FW" "$RES"

# --- libraries ------------------------------------------------------------
# Resolve symlinks: Homebrew's libSDL2-2.0.0.dylib is a link into Cellar, and
# copying the link would leave the bundle depending on this machine again.
for src in \
	"$(readlink -f /opt/homebrew/lib/libSDL2-2.0.0.dylib 2>/dev/null || \
		python3 -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' \
		/opt/homebrew/lib/libSDL2-2.0.0.dylib)" \
	"$(cd "$(dirname "$0")/../.." && cd .. && pwd)/.deps/lib/libSDL2_image-2.0.0.dylib"
do
	[ -f "$src" ] || { echo "FAIL: missing library $src"; exit 1; }
	cp -f "$src" "$FW/"
done

SDL2=$(basename "$(ls "$FW"/libSDL2-2.0.0*.dylib | head -1)")
IMG=$(basename "$(ls "$FW"/libSDL2_image-2.0.0*.dylib | head -1)")

# Give each library an @rpath identity, so anything linking it records a
# relative reference rather than wherever it happened to be built.
chmod u+w "$FW/$SDL2" "$FW/$IMG"
"$INT" -id "@rpath/libSDL2-2.0.0.dylib" "$FW/$SDL2"
"$INT" -id "@rpath/libSDL2_image-2.0.0.dylib" "$FW/$IMG"

# SDL2_image links SDL2 by absolute path too -- fixing only the executable
# would still leave the bundle reaching outside itself.
OLD_SDL2=$("$OTOOL" -L "$FW/$IMG" | awk '/libSDL2-2\.0\.0\.dylib/ && !/@rpath/ {print $1; exit}')
[ -n "$OLD_SDL2" ] && "$INT" -change "$OLD_SDL2" "@rpath/libSDL2-2.0.0.dylib" "$FW/$IMG"

# --- executable -----------------------------------------------------------
OLD_EXE_SDL2=$("$OTOOL" -L "$EXE" | awk '/libSDL2-2\.0\.0\.dylib/ && !/@rpath/ {print $1; exit}')
[ -n "$OLD_EXE_SDL2" ] && "$INT" -change "$OLD_EXE_SDL2" "@rpath/libSDL2-2.0.0.dylib" "$EXE"

# Drop the build machine's RPATHs, or dyld would still search them first and a
# developer's own copy would mask the bundled one -- hiding exactly the bug
# this script exists to prevent.
"$OTOOL" -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' | while read -r rp; do
	case "$rp" in
		@executable_path/*) ;;
		*) "$INT" -delete_rpath "$rp" "$EXE" 2>/dev/null || true ;;
	esac
done
"$INT" -add_rpath "@executable_path/../Frameworks" "$EXE" 2>/dev/null || true

# --- game data ------------------------------------------------------------
# setup_dirs() registers EXE>>../Resources first on Apple, so this is where the
# bundled copy is found.
rm -rf "$RES/gfx" "$RES/sfx"
cp -R "$SHARE/gfx" "$SHARE/sfx" "$RES/"

# --- signature ------------------------------------------------------------
# Mandatory, not cosmetic: on Apple Silicon every binary must carry at least an
# ad-hoc signature, and install_name_tool invalidates whatever was there. An
# unsigned arm64 binary is killed by the kernel at exec with SIGKILL and no
# useful message.
codesign --force --sign - "$FW/$SDL2" "$FW/$IMG" >/dev/null 2>&1
codesign --force --sign - "$EXE" >/dev/null 2>&1
codesign --force --sign - "$APP" >/dev/null 2>&1

# --- verify ---------------------------------------------------------------
rc=0
check() {
	if [ "$2" = "0" ]; then printf 'ok    %s\n' "$1"
	else printf 'FAIL  %s\n' "$1"; rc=1; fi
}

! "$OTOOL" -L "$EXE" | grep -qE '/opt/homebrew|/Users/'; check "executable references nothing outside the bundle" $?
! "$OTOOL" -L "$FW/$IMG" | grep -qE '/opt/homebrew|/Users/'; check "libSDL2_image references nothing outside the bundle" $?
"$OTOOL" -l "$EXE" | grep -q '@executable_path/../Frameworks'; check "bundle-relative RPATH present" $?
! "$OTOOL" -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' | grep -qE '^/'; check "no absolute RPATHs remain" $?
[ -f "$RES/sfx/sfx.agw" ] && [ -f "$RES/sfx/kobosfx.h" ]; check "sound data bundled (incl. the runtime-included kobosfx.h)" $?
[ "$(ls "$RES/gfx"/*.png 2>/dev/null | wc -l | tr -d ' ')" -ge 45 ]; check "graphics bundled" $?
codesign --verify --deep "$APP" >/dev/null 2>&1; check "bundle signature valid" $?

printf '\napp: %s (%s)\n' "$APP" "$(du -sh "$APP" | cut -f1)"
[ "$rc" -eq 0 ] && echo "BUNDLE OK" || echo "BUNDLE INCOMPLETE"
exit "$rc"
