#include "sleepwatch.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#ifdef __linux__
#include <cstring>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

namespace app
{
	// The SIGCONT handler only writes a byte (write() is async-signal-safe; SDL_PushEvent is not); the watch's thread
	// reads it and does the rest.
	static int contPipe[2] = {-1, -1};

	static void onCont(int)
	{
		int saved = errno;
		char c = 'c';
		if (write(contPipe[1], &c, 1) < 0) {}
		errno = saved;
	}

#ifdef __linux__
	static bool hasBit(const unsigned long* bits, int bit)
	{
		return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
	}

	// found by what a device can report (EV_SW with SW_LID), as bananad finds it: names and numbers vary
	static std::vector<int> openLidSwitches()
	{
		std::vector<int> out;
		DIR* d = opendir("/dev/input");
		if (!d) return out;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
			if (hasBit(sw, SW_LID)) out.push_back(fd);
			else close(fd);
		}
		closedir(d);
		return out;
	}

	// the switch's state now (an event can be missed across a stop: ask the device)
	static bool lidIsShut(const std::vector<int>& fds)
	{
		for (int fd : fds)
		{
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			if (ioctl(fd, EVIOCGSW(sizeof sw), sw) >= 0 && hasBit(sw, SW_LID)) return true;
		}
		return false;
	}
#endif

	bool SleepWatch::start(std::function<void(bool)> lid, std::function<void()> wake)
	{
		onLid = std::move(lid);
		onWake = std::move(wake);
		if (pipe(stopPipe) < 0 || pipe(contPipe) < 0) return false;
		for (int fd : {contPipe[0], contPipe[1]})
		{
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);	// a burst of signals must never block the handler
		}
		struct sigaction sa = {};
		sa.sa_handler = onCont;
		sa.sa_flags = SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGCONT, &sa, nullptr);
#ifdef __linux__
		lidFds = openLidSwitches();
		if (lidFds.empty()) fprintf(stderr, "sleep: no lid switch found; only SIGCONT is seen\n");
		lidShut = lidIsShut(lidFds);
#endif
		thread = std::thread([this] { run(); });
		return true;
	}

	void SleepWatch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		signal(SIGCONT, SIG_DFL);
		for (int fd : {stopPipe[0], stopPipe[1], contPipe[0], contPipe[1]}) close(fd);
		for (int fd : lidFds) close(fd);
		lidFds.clear();
	}

	void SleepWatch::run()
	{
		for (;;)
		{
			std::vector<pollfd> fds = {{stopPipe[0], POLLIN, 0}, {contPipe[0], POLLIN, 0}};
			for (int fd : lidFds) fds.push_back({fd, POLLIN, 0});
			if (poll(fds.data(), fds.size(), -1) < 0)
			{
				if (errno == EINTR) continue;	// SIGCONT itself interrupts the poll; its byte is in the pipe
				return;
			}
			if (fds[0].revents) return;
			if (fds[1].revents)
			{
				char buf[16];
				while (read(contPipe[0], buf, sizeof buf) > 0) {}
#ifdef __linux__
				lidShut = lidIsShut(lidFds);
#endif
				if (onWake) onWake();
			}
#ifdef __linux__
			for (size_t k = 2; k < fds.size(); k++)
			{
				if (!fds[k].revents) continue;
				input_event ev[16];
				ssize_t r;
				bool changed = false;
				while ((r = read(fds[k].fd, ev, sizeof ev)) > 0)
					for (size_t i = 0; i < r / sizeof *ev; i++)
						if (ev[i].type == EV_SW && ev[i].code == SW_LID && (ev[i].value != 0) != lidShut.load())
						{
							lidShut = ev[i].value != 0;
							changed = true;
						}
				// only a real change: a repeated "closed" is the same shut lid, not another close
				if (changed && onLid) onLid(lidShut.load());
				if (r < 0 && errno != EAGAIN)
				{
					// the device went away: dropped, or poll() would report it again at once, forever
					close(fds[k].fd);
					for (auto it = lidFds.begin(); it != lidFds.end(); ++it)
						if (*it == fds[k].fd) { lidFds.erase(it); break; }
				}
			}
#endif
		}
	}
}
