#!/bin/sh
# Build the Release configuration and install+launch it on the paired iPad.
#
# Free "Personal Team" signing expires 7 days after it's applied -- there is
# no paid Apple Developer Program on this project, so that wall is a fact of
# life, not a bug. Re-running this one script is the entire fix each time it
# expires: it rebuilds, re-signs via -allowProvisioningUpdates (which renews
# the provisioning profile automatically), reinstalls, and relaunches.
#
# The iPad must be unlocked for devicectl to install/launch on it. If the
# provisioning profile was just renewed (e.g. the 7-day expiry above, or
# any time this is the first install of a fresh profile), the launch will
# fail with "has an invalid code signature ... has not been explicitly
# trusted by the user" until the device trusts the new profile by hand:
# Settings > General > VPN & Device Management > under "Developer App",
# tap the Apple ID/certificate > Trust. Re-run this script afterward.
#
# Usage: tools/port/ipados-install.sh
set -eu

BUNDLE_ID="io.github.monkeywidget.KoboDeluxe"
SCHEME="KoboDeluxe iOS"
DEVICE_PLATFORM="iOS"   # vs. "iOS Simulator" -- picks the real device, not a simulator

# project.pbxproj deliberately leaves DEVELOPMENT_TEAM blank (it's tied to
# one person's Apple Developer account, so it doesn't belong in history --
# see the .gitignore entry for this file). tools/port/ipados.env.example is
# the checked-in template; copy it to tools/port/ipados.env and fill it in.
ENV_FILE="$(dirname "$0")/ipados.env"
TEAM_ARG=""
if [ -f "$ENV_FILE" ]; then
	# shellcheck disable=SC1090
	. "$ENV_FILE"
	[ -n "${DEVELOPMENT_TEAM:-}" ] && TEAM_ARG="DEVELOPMENT_TEAM=$DEVELOPMENT_TEAM"
fi

# Look up the paired physical device's UDID by platform rather than
# hardcoding one, so this script isn't tied to any specific iPad.
DEVICE_UDID=$(xcrun devicectl list devices --json-output - 2>/dev/null \
	| python3 -c '
import json, sys
devices = json.load(sys.stdin)["result"]["devices"]
for d in devices:
	hw = d.get("hardwareProperties", {})
	conn = d.get("connectionProperties", {})
	# Simulators always report reality=="simulated"; a real device in some
	# connection states (e.g. tunnel disconnected) omits the field entirely
	# rather than reporting "physical" -- so check what it is not, not what
	# it is, or a perfectly good physical device can be missed.
	if (hw.get("platform") == "'"$DEVICE_PLATFORM"'"
			and hw.get("reality") != "simulated"
			and conn.get("pairingState") == "paired"):
		print(hw["udid"])
		break
')

[ -n "$DEVICE_UDID" ] || { echo "FAIL: no paired physical $DEVICE_PLATFORM device found"; exit 1; }

cd "$(dirname "$0")/../../apple/KoboDeluxe"

xcodebuild -scheme "$SCHEME" -configuration Release \
	-destination "id=$DEVICE_UDID" -allowProvisioningUpdates build $TEAM_ARG

APP=$(xcodebuild -scheme "$SCHEME" -configuration Release \
	-destination "id=$DEVICE_UDID" -showBuildSettings $TEAM_ARG 2>/dev/null \
	| awk -F' = ' '/ CODESIGNING_FOLDER_PATH /{print $2; exit}')

[ -d "$APP" ] || { echo "FAIL: built app not found at $APP"; exit 1; }

xcrun devicectl device install app --device "$DEVICE_UDID" "$APP"
xcrun devicectl device process launch --device "$DEVICE_UDID" \
	--terminate-existing "$BUNDLE_ID"

sleep 2
# The process list reports the executable's path (.../KoboDeluxe.app/KoboDeluxe),
# not the bundle identifier, so match on that instead.
if xcrun devicectl device info processes --device "$DEVICE_UDID" 2>/dev/null \
		| grep -q "KoboDeluxe.app/KoboDeluxe"
then
	echo "ok    running on device"
else
	echo "FAIL  not found in the device's process list after launch"
	exit 1
fi
