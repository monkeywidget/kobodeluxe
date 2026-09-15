#!/bin/sh
# Launch the game, let it reach the title screen, and assert on its own log.
#
# This is the machine-verifiable replacement for "I looked at it and it seemed
# fine". Every check below corresponds to something that was actually broken at
# some point during the SDL 2 port, so a regression in any of them is caught
# here rather than by eye.
#
# NOTE: must be run OUTSIDE a sandbox. SDL video init needs WindowServer, and
# under a sandbox it fails with "Connection Invalid error for service
# com.apple.hiservices-xpcservice" before the game gets anywhere.
#
# Usage: tools/port/smoke-test.sh <install-prefix> [seconds]
set -eu

PREFIX="${1:?usage: smoke-test.sh <install-prefix> [seconds]}"
SECS="${2:-7}"
BIN="$PREFIX/bin/kobodl.app/Contents/MacOS/kobodl"
LOG="${TMPDIR:-/tmp}/kobodl-smoke.log"

[ -x "$BIN" ] || { echo "FAIL: no executable at $BIN"; exit 1; }

cd "$(dirname "$BIN")"
./kobodl -nofullscreen -width 640 -height 480 -logverbosity 3 > "$LOG" 2>&1 &
pid=$!
i=0
while [ "$i" -lt "$SECS" ]; do
	kill -0 "$pid" 2>/dev/null || break
	sleep 1
	i=$((i + 1))
done
if kill -0 "$pid" 2>/dev/null; then
	survived=yes
	kill "$pid" 2>/dev/null || true
	sleep 1
else
	survived=no
fi

rc=0
check() {	# check <description> <condition-result>
	if [ "$2" = "0" ]; then
		printf 'ok    %s\n' "$1"
	else
		printf 'FAIL  %s\n' "$1"
		rc=1
	fi
}

# Survived the run without exiting: catches dyld failures (a missing LC_RPATH
# for SDL2_image killed it at launch) and early aborts.
[ "$survived" = "yes" ]; check "stayed running for ${SECS}s" $?

# The shim opened a window and SDL chose a backend. The renderer name is worth
# printing: SDL picks a backend per platform (Metal here), so a renderer bug
# is not necessarily reproducible everywhere.
grep -q 'kobo_sdl12: .* renderer ' "$LOG"; check "video mode set via kobo_sdl12 shim" $?

# Sound data actually loaded. This read "Total 0 waveforms" while the install
# rules shipped *.agw but not the kobosfx.h that those EEL scripts include at
# runtime -- silent effects, with no error at the point of failure.
waves=$(sed -n 's/.*Total \([0-9]*\) waveforms.*/\1/p' "$LOG" | tail -1)
[ -n "$waves" ] && [ "$waves" -gt 0 ]; check "sound data loaded (${waves:-0} waveforms)" $?

# Got through engine startup to the title screen.
grep -q "Pushed state 'intro_title'" "$LOG"; check "reached title screen" $?

# Nothing complained. "Could not get double buffered display" lived here until
# the shim reported SDL_DOUBLEBUF back on the screen surface.
n=$(grep -icE '(^|\])[^a-z]*(error|failed|could not)' "$LOG" || true)
[ "$n" -eq 0 ]; check "no errors or warnings logged ($n found)" $?

printf '\nrenderer: %s\nlog: %s\n' \
	"$(sed -n 's/.*renderer "\(.*\)".*/\1/p' "$LOG" | tail -1)" "$LOG"
[ "$rc" -eq 0 ] && echo "SMOKE TEST PASSED" || echo "SMOKE TEST FAILED"
exit "$rc"
