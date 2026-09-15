#!/bin/sh
#
# Prepare the public release of this fork, across three remotes:
#
#   origin    NAS          every branch, full development history
#   public    GitHub fork  ONE squashed commit, and the release tag
#   upstream  olofson      the parent of the fork -- READ ONLY, never written
#
# Sequence:
#   1. Commit anything outstanding and push EVERY branch to the NAS. Nothing
#      destructive happens until that has succeeded, so the development
#      history is safe off this machine before any of it is rewritten.
#   2. Build a release branch that is one commit on top of the fork's upstream
#      history, with the Android/TV work absent from the history rather than
#      deleted by a later commit.
#   3. Remove the Android files, and drop the branch the fork inherited from
#      upstream.
#
# It does NOT commit the release: the squashed change is left staged so the
# message is yours. It never pushes to 'upstream'.
#
# Usage:
#   tools/port/prepare-fork.sh            # dry run: print the plan, change nothing
#   tools/port/prepare-fork.sh --apply    # actually do it
#
set -eu

NAS=origin                    # everything
PUBLIC=public                 # squashed release only
UPSTREAM=upstream             # read only
PRESERVE_BRANCH=android-port  # snapshot of the full work, incl. the TV port
RELEASE_BRANCH=macos-port     # what gets pushed to PUBLIC and tagged
BASE_REF="$PUBLIC/master"     # the fork's copy of upstream history

APPLY=0
[ "${1:-}" = "--apply" ] && APPLY=1

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
	echo "FAIL: not inside a git repository"; exit 1; }

say()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\nFAIL: %s\n' "$*"; exit 1; }
run()  {
	if [ "$APPLY" -eq 1 ]; then
		printf '   $ %s\n' "$*"
		# shellcheck disable=SC2068
		$@
	else
		printf '   would run: %s\n' "$*"
	fi
}
rc=0

# ---------------------------------------------------------------- preconditions
step "Preconditions"

for r in "$NAS" "$PUBLIC" "$UPSTREAM"; do
	git remote get-url "$r" >/dev/null 2>&1 || die "no '$r' remote configured"
	say "   $r: $(git remote get-url "$r")"
done

# Hard guard. Everything below writes to NAS and PUBLIC only; upstream is
# somebody else's repository and this script must never push to it.
UPSTREAM_URL=$(git remote get-url "$UPSTREAM")
for r in "$NAS" "$PUBLIC"; do
	[ "$(git remote get-url "$r")" = "$UPSTREAM_URL" ] &&
		die "'$r' points at the same URL as '$UPSTREAM'. Refusing to write to upstream."
done
case "$(git remote get-url "$PUBLIC")" in
	*olofson*) die "'$PUBLIC' points at the upstream account. Check your remotes." ;;
esac
say "   upstream is read-only in this script"

SOURCE=$(git rev-parse --abbrev-ref HEAD)
say "   source branch: $SOURCE"

DIRTY=0
[ -n "$(git status --porcelain)" ] && DIRTY=1
[ "$DIRTY" -eq 1 ] && say "   working tree: dirty (will be committed before anything else)"

[ "$APPLY" -eq 1 ] || say "
   DRY RUN. Nothing will be changed. Re-run with --apply to act."

# ------------------------------------------------------------------ safety net
step "Safety net"
BACKUP="prefork-backup-$(date +%Y%m%d-%H%M%S)"
say "   tagging the current commit '$BACKUP'"
run git tag "$BACKUP"

# ---------------------------------------------- 1. preserve everything on NAS
step "1. Preserve the full history on '$NAS' (NAS)"

if [ "$DIRTY" -eq 1 ]; then
	say "   committing outstanding work so it is preserved"
	if [ "$APPLY" -eq 1 ]; then
		printf '   $ git add -A && git commit -m "<wip>"\n'
		git add -A
		git commit -q -m "Work in progress at fork preparation"
	else
		printf '   would run: git add -A && git commit -m "<wip>"\n'
	fi
fi

say "   snapshotting the full work (incl. the TV port) as '$PRESERVE_BRANCH'"
run git branch -f "$PRESERVE_BRANCH" HEAD

say "   pushing every local branch and tag to $NAS"
if [ "$APPLY" -eq 1 ]; then
	git ls-remote --heads "$NAS" >/dev/null 2>&1 ||
		die "cannot reach $NAS.
      Nothing has been rewritten yet, so you have lost nothing. Bring the NAS
      up and re-run: the development history must be somewhere else before
      this script starts squashing it away."
	printf '   $ git push --all %s\n' "$NAS"
	git push --all "$NAS"
	printf '   $ git push --tags %s\n' "$NAS"
	git push --tags "$NAS"
	say "   NAS now holds the full history"
else
	say "   would run: git push --all $NAS && git push --tags $NAS"
	say "   (on --apply this must SUCCEED, or the script stops here)"
fi

# ------------------------------------------------------ 2. build the release
step "2. Build '$RELEASE_BRANCH': one commit on top of $BASE_REF"

if [ "$APPLY" -eq 1 ]; then
	printf '   $ git fetch %s\n' "$PUBLIC"
	git fetch -q "$PUBLIC" || die "cannot reach $PUBLIC"
else
	say "   would run: git fetch $PUBLIC"
fi

if git rev-parse --verify "$BASE_REF" >/dev/null 2>&1; then
	# The squash is only clean if our work sits on top of that base.
	if git merge-base --is-ancestor "$BASE_REF" HEAD 2>/dev/null; then
		say "   history: HEAD descends from $BASE_REF (squash will be clean)"
	else
		die "HEAD has diverged from $BASE_REF. Rebase onto it first."
	fi
	say "   commits being squashed into one:"
	git log --oneline "$BASE_REF"..HEAD | sed 's/^/      /'
else
	if [ "$APPLY" -eq 1 ]; then
		die "$BASE_REF not found after fetching $PUBLIC"
	fi
	say "   ($BASE_REF not fetched yet; --apply fetches it first)"
fi

say "   starting the release branch from the fork's upstream history"
run git checkout -q -B "$RELEASE_BRANCH" "$BASE_REF"

# 'git merge --squash' rather than copying files: it stages the complete
# difference, DELETIONS INCLUDED. This port removes src/graphics/glSDL.{c,h},
# and a file-copy would silently leave both behind -- restoring the very
# OpenGL layer the port exists to remove.
say "   squashing $PRESERVE_BRANCH into the index (carries deletions)"
run git merge --squash "$PRESERVE_BRANCH"

step "3. Drop the Android port from the release"
for path in \
	android \
	tools/port/android-env.sh \
	tools/port/android-setup.sh \
	tools/port/check-apk.sh
do
	if [ -e "$path" ]; then
		say "   removing $path"
		run git rm -rq -f "$path"
	else
		say "   (absent) $path"
	fi
done

# ---------------------------------------------- 4. tidy the public fork
step "4. Drop branches the fork inherited from upstream"
# wip-4 is an abandoned 2020 Autotools-to-CMake attempt that master superseded.
# Deleted on PUBLIC only -- upstream's own copy is not ours to touch.
for rb in wip-4; do
	if [ "$APPLY" -eq 0 ]; then
		say "   would delete $PUBLIC/$rb if present"
	elif ! git ls-remote --heads "$PUBLIC" >/dev/null 2>&1; then
		say "   SKIPPED: cannot reach $PUBLIC. By hand: git push $PUBLIC --delete $rb"
		rc=1
	elif git ls-remote --exit-code --heads "$PUBLIC" "$rb" >/dev/null 2>&1; then
		say "   deleting $PUBLIC/$rb"
		run git push "$PUBLIC" --delete "$rb"
	else
		say "   (absent) $PUBLIC/$rb"
	fi
done

# ------------------------------------------------------------------- verify
step "5. Verify"

if [ "$APPLY" -eq 0 ]; then
	say "   skipped: a dry run changes nothing, so this cannot mean anything yet"
else
	if git ls-files | grep -iqE 'android|xiaomi|koboredux'; then
		say "   FAIL: tracked files still named for another port:"
		git ls-files | grep -iE 'android|xiaomi|koboredux' | sed 's/^/     /'
		rc=1
	else
		say "   ok   no tracked file is named for another port"
	fi

	if [ "$(git rev-list --count "$BASE_REF"..HEAD)" -le 1 ]; then
		say "   ok   release branch is at most one commit above $BASE_REF"
	else
		say "   FAIL: release branch has more than one commit above $BASE_REF"
		rc=1
	fi

	if git log "$BASE_REF"..HEAD --oneline | grep -qi 'apk\|android'; then
		say "   FAIL: an Android commit is still in the release history"
		rc=1
	else
		say "   ok   no Android commit in the release history"
	fi

	if git ls-remote --heads "$NAS" >/dev/null 2>&1 &&
	   git ls-remote --heads "$NAS" | grep -q "$PRESERVE_BRANCH"; then
		say "   ok   $PRESERVE_BRANCH is on the NAS"
	else
		say "   FAIL: $PRESERVE_BRANCH is NOT on the NAS -- do not delete anything locally"
		rc=1
	fi
fi

# ------------------------------------------------------------------- closing
step "Done"
if [ "$APPLY" -eq 1 ]; then
	cat <<EOF
   On '$RELEASE_BRANCH', with the whole port staged as ONE change.
   Nothing is committed: the message is yours.

      git diff --cached --stat
      git commit
      git push -u $PUBLIC $RELEASE_BRANCH
      git tag -a v0.6.0-macos.1 -m 'macOS port'
      git push $PUBLIC v0.6.0-macos.1
      git push $NAS --all && git push $NAS --tags   # keep the NAS complete

   Full history, including the TV port, is on $NAS as '$PRESERVE_BRANCH'.
   Attach the bundled .app to the release; it is ad-hoc signed, so first
   launch needs right-click -> Open.

   Once you are happy: git tag -d $BACKUP
EOF
else
	say "   Dry run complete. Re-run with --apply to act."
fi
exit "$rc"
