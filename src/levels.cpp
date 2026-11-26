#include "levels.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <unistd.h>

namespace app
{
	// ---- volume ----------------------------------------------------------------------------------------------------

	bool Volume::present() const
	{
		return access(script.c_str(), X_OK) == 0;
	}

	int Volume::get() const
	{
		if (!present()) return -1;
		int v = -1;
		if (FILE* p = popen(("'" + script + "' get 2>/dev/null").c_str(), "r"))
		{
			if (fscanf(p, "%d", &v) != 1) v = -1;
			pclose(p);
		}
		return v >= 0 && v <= 100 ? v : -1;
	}

	void Volume::set(int percent)
	{
		if (!present()) return;
		std::lock_guard<std::mutex> g(lock);
		pending = std::clamp(percent, 0, 100);
		if (!worker.joinable()) worker = std::thread([this] { run(); });
		wake.notify_one();
	}

	void pokeBananad()
	{
		DIR* d = opendir("/proc");
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
			char path[300], comm[32] = "";
			snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
			if (FILE* f = fopen(path, "r"))
			{
				if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
				fclose(f);
			}
			if (!strcmp(comm, "bananad")) kill(atoi(e->d_name), SIGHUP);
		}
		closedir(d);
	}

	void Volume::run()
	{
		std::unique_lock<std::mutex> g(lock);
		for (;;)
		{
			wake.wait(g, [this] { return pending >= 0 || stopping; });
			if (stopping) return;
			int v = pending;
			pending = -1;
			g.unlock();	// set() never waits on the script
			std::string cmd = "'" + script + "' set " + std::to_string(v) + " >/dev/null 2>&1";
			if (system(cmd.c_str()) != 0) fprintf(stderr, "volume: set %d failed\n", v);
			pokeBananad();
			g.lock();
		}
	}

	Volume::~Volume()
	{
		{
			std::lock_guard<std::mutex> g(lock);
			stopping = true;
		}
		wake.notify_one();
		if (worker.joinable()) worker.join();	// at most one script run: bounded, never a network wait
	}

	// ---- brightness ------------------------------------------------------------------------------------------------

	static const char* BL_NAMES[2] = {"backlight1", "backlight0"};	// top, bottom (as bananad has them)

	static int readInt(const std::string& path)
	{
		std::ifstream in(path);
		int v = -1;
		if (!(in >> v)) return -1;
		return v;
	}

	bool Brightness::present(Panel p) const
	{
		return readInt(sysDir + "/" + BL_NAMES[p] + "/max_brightness") > 0;
	}

	int Brightness::permille(Panel p) const
	{
		std::string d = sysDir + "/" + BL_NAMES[p];
		int max = readInt(d + "/max_brightness"), v = readInt(d + "/brightness");
		if (max <= 0 || v < 0) return -1;
		return (v * 1000 + max / 2) / max;
	}

	bool Brightness::set(Panel p, int pm)
	{
		pm = std::clamp(pm, 100, 1000);
		std::string d = sysDir + "/" + BL_NAMES[p];
		int max = readInt(d + "/max_brightness");
		if (max <= 0) return false;
		{
			std::ofstream out(d + "/brightness");
			out << std::max(1, (max * pm + 500) / 1000) << "\n";	// rounded as bananad rounds: no creep
			if (!out.flush()) return false;
		}
		// both levels in the store: this one, and the other as stored (else as it is now). Never under 10%: bananad
		// throws such a file away and starts both panels at the kernel's default
		Panel o = p == TOP ? BOTTOM : TOP;
		int other = -1;
		{
			std::ifstream in(store);
			int t = -1, b = -1;
			if (in >> t)
			{
				if (!(in >> b)) b = t;	// an old one-number file: both
				other = o == TOP ? t : b;
			}
		}
		if (other < 100 || other > 1000) other = permille(o);
		if (other < 100 || other > 1000) other = pm;
		std::ofstream out(store);
		out << (p == TOP ? pm : other) << " " << (p == TOP ? other : pm) << "\n";
		return true;
	}

	// ---- the bottom panel's backlight ------------------------------------------------------------------------------

	static const char* PANEL_OFF = "/run/banana/panel-off";

	void BottomPanel::init()
	{
		std::ifstream f("/run/banana/backlight-bottom");
		std::string path;
		std::getline(f, path);
		// only a sysfs backlight: never write where a stray file points
		if (path.rfind("/sys/class/backlight/", 0) != 0 || path.find("..") != std::string::npos) return;
		if (access((path + "/bl_power").c_str(), W_OK) != 0) return;
		backlight = path;
	}

	void BottomPanel::set(bool on)
	{
		if (on == isOn) return;
		isOn = on;
		if (backlight.empty()) return;
		if (on)
		{
			std::ofstream(backlight + "/bl_power") << "0";
			unlink(PANEL_OFF);
			return;
		}
		// the marker first: bananad keeps the panel dark from the moment it can see it
		std::ofstream(PANEL_OFF) << backlight << "\n";
		std::ofstream(backlight + "/bl_power") << "4";
	}

	// ---- battery ---------------------------------------------------------------------------------------------------

	static std::string line(const std::string& path)
	{
		std::ifstream f(path);
		std::string s;
		std::getline(f, s);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
		return s;
	}

	Battery Battery::read(const std::string& base)
	{
		Battery b;
		bool charger = false;
		DIR* d = opendir(base.c_str());
		if (!d) return b;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.') continue;
			std::string p = base + "/" + e->d_name;
			if (line(p + "/type") != "Battery")
			{
				if (line(p + "/online") == "1") charger = true;
				continue;
			}
			if (b.percent >= 0) continue;
			std::string cap = line(p + "/capacity");
			if (cap.empty()) continue;
			b.percent = std::clamp(atoi(cap.c_str()), 0, 100);
			std::string st = line(p + "/status");
			b.charging = st == "Charging" || st == "Full";
		}
		closedir(d);
		if (charger && b.percent >= 0) b.charging = true;
		return b;
	}
}
