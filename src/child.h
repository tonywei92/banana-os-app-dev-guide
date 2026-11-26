#pragma once
#include <string>
#include <sys/types.h>
#include <vector>

namespace app
{
	// A helper process (a JVM server, a downloader...) started and stopped so that it never outlives the app: on a 1 GB
	// handheld with no swap, an orphan holding hundreds of MB makes the next app get killed. Not used by the demo;
	// BANANAOS-APP-GUIDE.md, "Running Java (or any big helper process)", says how to use it.

	// Starts argv[0] (a full path) in a process group of its own (one signal reaches it and every process it starts),
	// stdin /dev/null, stdout and stderr appended to logPath. posix_spawn returns at once, so this is safe on the main
	// thread; waiting for the helper to be *ready* is not (poll it from a thread, or each pass of the loop).
	// pidFile (optional): the pid is written there, so the next start can end a helper a crash left behind.
	// Returns the pid, or -1.
	pid_t spawnChild(const std::vector<std::string>& argv, const std::string& logPath, const std::string& pidFile = "");

	// SIGTERM to its group now, SIGKILL 3 s later if it's still there (from a detached thread: the caller never waits,
	// quitting included). Removes pidFile.
	void stopChild(pid_t pid, const std::string& pidFile = "");

	// at start: a helper left running by a run that crashed (its pid in pidFile, and /proc/<pid>/cmdline still naming
	// `marker`, so a pid the system has since reused for something else is left alone) is stopped
	void stopLeftover(const std::string& pidFile, const std::string& marker);

	// whether it has ended (reaps it: call it from the loop now and then, or it stays a zombie)
	bool childEnded(pid_t pid);
}
