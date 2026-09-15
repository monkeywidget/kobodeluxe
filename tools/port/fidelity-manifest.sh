#!/bin/sh
# Record SHA-256 of every file carrying Akira Higuchi's copyright, at the
# current checkout. These 24 files are XKobo's game logic as it survived into
# Kobo Deluxe; the port's fidelity claim rests on them not drifting.
#
# Usage: tools/port/fidelity-manifest.sh > tools/port/fidelity-baseline.txt
set -eu
cd "$(git rev-parse --show-toplevel)"
echo "# XKobo game-logic fidelity baseline"
echo "# generated from $(git rev-parse HEAD) by $0"
git ls-files 'src/*' | while read -r f; do
	LC_ALL=C grep -qE "Copyright.*Akira Higuchi" "$f" 2>/dev/null || continue
	printf '%s  %s\n' "$(git show "HEAD:$f" | shasum -a 256 | cut -d' ' -f1)" "$f"
done
