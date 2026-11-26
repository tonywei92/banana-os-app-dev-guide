#!/bin/bash
# The app on BananaOS's Ports shelf: roms/ports/My App.sh, beside the MyApp folder (the same folder the Store installs).
# One launcher for both: this only hands over to the folder's own, so an update of the folder updates both.
exec "$(cd "$(dirname "$0")" && pwd)/MyApp/launch.sh" "$@"
