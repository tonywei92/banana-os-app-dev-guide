#include "applog.h"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace app
{
	static timespec started;

	std::string stateDir()
	{
		// both of BananaOS's launchers set XDG_CONFIG_HOME; the rest is for a desktop run
		std::string base;
		if (const char* x = getenv("XDG_CONFIG_HOME"); x && *x) base = x;
		else if (const char* h = getenv("HOME"); h && *h) base = std::string(h) + "/.config";
		else base = "/tmp";
		std::string dir = base + "/" + APP_ID;
		// (mkdir -p, two levels: .config may not exist yet on a fresh card)
		mkdir(base.c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		return dir;
	}

	void openLog(const std::string& dir)
	{
		clock_gettime(CLOCK_MONOTONIC, &started);
		if (isatty(2)) return;
		std::string log = dir + "/log.txt", prev = dir + "/log.prev.txt";
		rename(log.c_str(), prev.c_str());	// a relaunch after a problem must not wipe the run that had it
		int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0) return;
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);
		// line-buffered: a crash or a SIGKILL loses at most the line being written
		setvbuf(stdout, nullptr, _IOLBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
	}

	void logf(const char* fmt, ...)
	{
		timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (!started.tv_sec && !started.tv_nsec) started = now;	// (no openLog: times from the first line)
		double t = (now.tv_sec - started.tv_sec) + (now.tv_nsec - started.tv_nsec) / 1e9;
		char line[1024];
		int n = snprintf(line, sizeof line, "[%8.3f] ", t);
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(line + n, sizeof line - n, fmt, ap);
		va_end(ap);
		fprintf(stderr, "%s\n", line);
	}

	// opened at start: open() is allowed in a signal handler, but a path built there isn't (no malloc)
	static int crashFd = -1;

	static void onCrash(int sig)
	{
		if (crashFd >= 0)
		{
			// only async-signal-safe calls from here: write, backtrace_symbols_fd (loaded at start, below)
			const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
				: sig == SIGFPE ? "SIGFPE" : "SIGABRT";	// (not strsignal: it may allocate)
			const char head[] = "\n=== crash: ";
			if (write(crashFd, head, sizeof head - 1) < 0) {}
			if (write(crashFd, name, strlen(name)) < 0) {}
			if (write(crashFd, " ===\n", 5) < 0) {}
			void* frames[64];
			int n = backtrace(frames, 64);
			backtrace_symbols_fd(frames, n, crashFd);
			const char tail[] = "crash: see crash.log\n";
			if (write(2, tail, sizeof tail - 1) < 0) {}
		}
		// as it would have died (exit status 128 + signal), so the launcher's log shows it
		signal(sig, SIG_DFL);
		raise(sig);
	}

	void installCrashHandler(const std::string& dir)
	{
		crashFd = open((dir + "/crash.log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
		// backtrace() loads libgcc the first time it runs (a malloc): done here, not in the handler
		void* warm[1];
		backtrace(warm, 1);
		// its own stack, so a stack overflow can still be reported
		static char altStack[64 * 1024];
		stack_t ss = {};
		ss.ss_sp = altStack;
		ss.ss_size = sizeof altStack;
		sigaltstack(&ss, nullptr);
		struct sigaction sa = {};
		sa.sa_handler = onCrash;
		sa.sa_flags = SA_ONSTACK;
		sigemptyset(&sa.sa_mask);
		for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(s, &sa, nullptr);
	}
}
