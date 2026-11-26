#!/bin/bash
# The app's launcher, inside its own folder (MyApp/launch.sh). Two ways in:
#   - the Store's tile: this is the catalog entry's menu.launch. BananaOS's run-store-app gives the app its own session,
#     HOME=/data/home, XDG_CONFIG_HOME=/data/home/.config, SDL_KMSDRM_FIRST (top panel = SDL display 0), both panels on;
#   - "My App.sh" on the Ports shelf, which execs this. run-port does the same but darkens the bottom backlight.
D="$(cd "$(dirname "$0")" && pwd)"

# run-port turned the bottom backlight off (it expects a port to draw on the top panel only). This app uses the bottom
# panel, so on again; run-port puts it back as it was when the app quits. Delete this block for a top-only app.
if [ -f /run/banana/panel-off ]; then
	BL=$(cat /run/banana/panel-off)
	rm -f /run/banana/panel-off
	case "$BL" in /sys/class/backlight/*) [ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power" ;; esac
fi

STATE="${XDG_CONFIG_HOME:-/data/home/.config}/myapp"
mkdir -p "$STATE"
# the bundled libraries first; BananaOS's own (SDL2, Mesa, glibc...) after
export LD_LIBRARY_PATH="$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# The app writes its own log.txt in $STATE (src/applog.cpp). What comes before main() can't: a missing library
# ("error while loading shared libraries") is the dynamic loader's, so the launcher keeps that, and the exit status.
# The app runs in the background and is waited for, so a TERM to the launcher (BananaOS ending the session) is passed on.
"$D/bin/my-app" "$@" > "$STATE/launcher.txt" 2>&1 &
PID=$!
trap 'kill -TERM $PID 2>/dev/null' TERM INT HUP
wait $PID
RC=$?
# (a trap ends the first wait early: wait again for the real end)
while kill -0 $PID 2>/dev/null; do wait $PID; RC=$?; done
# 128 + the signal: 137 a SIGKILL, 139 a segfault, 134 an abort
echo "launcher: my-app ended with $RC" >> "$STATE/launcher.txt"
# a crash or a kill can leave BananaOS's lid sleep turned off (the app's "lid-restore" note): undone by the app's own
# rule, so the launcher never has a second copy of it to keep in step
[ -f "$STATE/lid-restore" ] && "$D/bin/my-app" --lid-restore >> "$STATE/launcher.txt" 2>&1
exit $RC
