#!/bin/bash
# Puts dist/ports/MyApp (packaging/build-bundle.sh --quick) on the handheld over SSH, sending only the files that
# changed, then quits the running app cleanly and swaps the folders. Reopen the app from the shelf afterwards.
#
#   tools/push-handheld.sh root@<handheld-ip>
#   APP_PORTS_DIR=/run/sd2/roms/ports tools/push-handheld.sh root@<handheld-ip>    (the app on the second card)
#
# Why a copy and a swap rather than writing over the installed folder: the running binary and its libraries are never
# changed under it, and a push cut off halfway (Wi-Fi) leaves the old folder whole.
set -euo pipefail
APP_DIR=MyApp
APP_BIN=my-app
HOST="${1:-}"
[ -n "$HOST" ] || { echo "usage: tools/push-handheld.sh root@<handheld-ip>" >&2; exit 2; }
case "$HOST" in *@*) ;; *) HOST="root@$HOST" ;; esac
IP="${HOST#*@}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/dist/ports/$APP_DIR"
[ -x "$SRC/bin/$APP_BIN" ] || { echo "no bundle: run packaging/build-bundle.sh --quick first" >&2; exit 1; }
D="${APP_PORTS_DIR:-/data/roms/ports}"

# The handheld's Wi-Fi naps to save power: the first connection can fail while it wakes. A few tries at port 22 first
# (nc -G is macOS's connect timeout; Linux's nc takes -w)
for i in 1 2 3 4 5; do
	if nc -z -G 4 "$IP" 22 2>/dev/null || nc -z -w 4 "$IP" 22 2>/dev/null; then break; fi
	[ "$i" = 5 ] && { echo "push: $IP:22 doesn't answer (Wi-Fi on? SSH enabled in BananaOS?)" >&2; exit 1; }
	echo "push: waiting for $IP:22 ($i)"
	sleep 2
done
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

# a copy of what's installed, made on the handheld (fast: local), to lay the changes over
"${SSH[@]}" "mkdir -p $D && cd $D && rm -rf $APP_DIR.new && if [ -d $APP_DIR ]; then cp -a $APP_DIR $APP_DIR.new; else mkdir $APP_DIR.new; fi"

if "${SSH[@]}" "command -v rsync" >/dev/null 2>&1 && command -v rsync >/dev/null; then
	# rsync sends only what differs, and --delete drops files no longer in the bundle
	rsync -rlptz --delete --checksum -e "ssh -o BatchMode=yes" "$SRC/" "$HOST:$D/$APP_DIR.new/"
else
	# BananaOS may have no rsync: compare md5 sums (BusyBox md5sum there, md5 -r here print the same "sum path"),
	# then send the changed files in one tar stream
	remote=$("${SSH[@]}" "cd $D/$APP_DIR.new && find . -type f -exec md5sum {} + || true")
	local_list=$(cd "$SRC" && find . -type f -exec md5 -r {} + 2>/dev/null | sort -k2 || find . -type f -exec md5sum {} + | sort -k2)
	changed=$(join -1 2 -2 2 -a 1 -o 1.2,1.1,2.1 <(printf '%s\n' "$local_list") <(printf '%s\n' "$remote" | sort -k2) | awk '$2 != $3 {print $1}')
	gone=$(join -1 2 -2 2 -v 2 <(printf '%s\n' "$local_list") <(printf '%s\n' "$remote" | sort -k2) | awk '{print $1}')
	echo "push: $(printf '%s' "$changed" | grep -c . || true) changed file(s)"
	# COPYFILE_DISABLE: macOS's tar would add ._ files, which BananaOS shows as broken entries
	{ cd "$SRC"; if [ -n "$changed" ]; then printf '%s\n' "$changed" | COPYFILE_DISABLE=1 tar -cf - -T -; else tar -cf - -T /dev/null; fi; } |
		"${SSH[@]}" "tar -xf - -C $D/$APP_DIR.new && cd $D/$APP_DIR.new && for f in $(printf '%s ' $gone); do rm -f \"\$f\"; done"
fi

# the clean quit: SIGTERM (SDL turns it into SDL_QUIT, so the app saves and restores what it changed), up to 5 s,
# found by its exact process name; then the swap, the old folder kept as MyApp.old for a quick way back
"${SSH[@]}" "set -e; cd $D
	pid=\$(ps -o pid,comm | awk '\$2==\"$APP_BIN\"{print \$1}')
	if [ -n \"\$pid\" ]; then kill -TERM \$pid; for i in 1 2 3 4 5 6 7 8 9 10; do ps -o comm | grep -qx $APP_BIN || break; sleep 0.5; done; fi
	if ps -o comm | grep -qx $APP_BIN; then echo 'push: the app is still running; left as it was'; rm -rf $APP_DIR.new; exit 1; fi
	rm -rf $APP_DIR.old; [ -d $APP_DIR ] && mv $APP_DIR $APP_DIR.old; mv $APP_DIR.new $APP_DIR
	echo \"push: installed \$(cat $APP_DIR/VERSION)\${pid:+ (the app quit: reopen it)}\""
