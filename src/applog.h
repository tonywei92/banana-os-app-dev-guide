#pragma once
#include <string>

namespace app
{
	// The app's id: its state folder's name under $XDG_CONFIG_HOME. Change it with the bundle's APP_ID.
	constexpr const char* APP_ID = "myapp";

	// Where settings and logs live. On BananaOS run-store-app and run-port set XDG_CONFIG_HOME=/data/home/.config, so
	// this is /data/home/.config/myapp (home/.config/myapp on the card): outside the app's folder, so it survives an
	// update or a removal, and the Store and Ports installs share it.
	std::string stateDir();

	// stderr (and stdout) into <state>/log.txt, the run before kept as log.prev.txt: there is no terminal on the
	// handheld, and a user's report is only as good as the file they can copy off the card. Left alone when stderr is a
	// terminal (a desktop run), so you see the output where you started it.
	void openLog(const std::string& dir);

	// A line in the log, with the time since start: the order of events around a sleep or a crash is the first thing
	// you want from a report.
	void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

	// SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT: a backtrace appended to <state>/crash.log, then the process dies as it
	// would have. The bundle's binary is stripped; build-bundle.sh keeps the unstripped copy in dist/symbols for
	// addr2line (BANANAOS-APP-GUIDE.md, "Crash logs").
	void installCrashHandler(const std::string& dir);
}
