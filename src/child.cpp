#include "child.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace app
{
	pid_t spawnChild(const std::vector<std::string>& argv, const std::string& logPath, const std::string& pidFile)
	{
		if (argv.empty()) return -1;
		std::vector<char*> av;
		for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
		av.push_back(nullptr);
		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
		// its own log: a JVM's output in the app's log.txt would bury the app's lines
		posix_spawn_file_actions_addopen(&fa, 1, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
		posix_spawn_file_actions_adddup2(&fa, 1, 2);
		posix_spawnattr_t attr;
		posix_spawnattr_init(&attr);
		// a group of its own: a JVM (or a shell script) starts more processes, and kill(-pid) reaches them all
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
		posix_spawnattr_setpgroup(&attr, 0);
		pid_t pid = -1;
		int rc = posix_spawn(&pid, av[0], &fa, &attr, av.data(), environ);
		posix_spawn_file_actions_destroy(&fa);
		posix_spawnattr_destroy(&attr);
		if (rc != 0)
		{
			fprintf(stderr, "child: %s didn't start (%d)\n", av[0], rc);
			return -1;
		}
		if (!pidFile.empty()) std::ofstream(pidFile) << pid << "\n";
		fprintf(stderr, "child: started %s, pid %ld (log %s)\n", av[0], (long)pid, logPath.c_str());
		return pid;
	}

	void stopChild(pid_t pid, const std::string& pidFile)
	{
		if (!pidFile.empty()) unlink(pidFile.c_str());
		if (pid <= 1) return;
		// a JVM given SIGTERM runs its shutdown hooks (a server saves its state): the polite way first
		if (kill(-pid, SIGTERM) != 0) kill(pid, SIGTERM);
		fprintf(stderr, "child: SIGTERM to %ld\n", (long)pid);
		// SIGKILL later, never waited for: a quit must not hang on a helper. (If the app has exited by then, the thread
		// dies with it and the group still had its SIGTERM.)
		std::thread([pid] {
			for (int i = 0; i < 30; i++)
			{
				if (waitpid(pid, nullptr, WNOHANG) == pid || kill(pid, 0) != 0) return;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (kill(-pid, SIGKILL) != 0) kill(pid, SIGKILL);
			waitpid(pid, nullptr, 0);
		}).detach();
	}

	void stopLeftover(const std::string& pidFile, const std::string& marker)
	{
		long pid = 0;
		if (FILE* f = fopen(pidFile.c_str(), "r"))
		{
			if (fscanf(f, "%ld", &pid) != 1) pid = 0;
			fclose(f);
		}
		unlink(pidFile.c_str());
		if (pid <= 1) return;
		// still ours? pids are reused: only a process whose command line names our folder is ended (Linux's /proc;
		// elsewhere nothing is killed)
		std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
		std::string cmd((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (cmd.empty() || cmd.find(marker) == std::string::npos) return;
		fprintf(stderr, "child: a helper left by the last run (pid %ld): stopping it\n", pid);
		if (kill(-(pid_t)pid, SIGTERM) != 0) kill((pid_t)pid, SIGTERM);
	}

	bool childEnded(pid_t pid)
	{
		if (pid <= 0) return true;
		int status = 0;
		pid_t r = waitpid(pid, &status, WNOHANG);
		return r == pid || (r < 0 && kill(pid, 0) != 0);
	}
}
