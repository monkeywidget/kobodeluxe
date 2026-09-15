#!/bin/sh
# Assert that XKobo's game logic has not drifted during the port.
#
# Any file listed in fidelity-baseline.txt that differs from its recorded hash
# must appear in fidelity-allowed.txt with a stated reason, or this fails.
# "The game logic is untouched" is thereby checkable rather than asserted.
set -eu
cd "$(git rev-parse --show-toplevel)"
base=tools/port/fidelity-baseline.txt
allow=tools/port/fidelity-allowed.txt
[ -f "$allow" ] || : > "$allow"

rc=0 clean=0 allowed=0
while read -r want f; do
	case "$want" in \#*) continue ;; esac
	got=$(shasum -a 256 "$f" | cut -d' ' -f1)
	if [ "$got" = "$want" ]; then
		clean=$((clean + 1))
	elif grep -q "^$f	" "$allow"; then
		allowed=$((allowed + 1))
		printf 'ALLOWED  %s  (%s)\n' "$f" "$(grep "^$f	" "$allow" | cut -f2-)"
	else
		printf 'DRIFT    %s  changed with no recorded justification\n' "$f"
		rc=1
	fi
done < "$base"

printf '\nfidelity: %d/%d game-logic files byte-identical to upstream, %d changed with justification\n' \
	"$clean" "$((clean + allowed))" "$allowed"
[ "$rc" -eq 0 ] || printf 'FAIL: undocumented change to XKobo game logic\n'
exit "$rc"
