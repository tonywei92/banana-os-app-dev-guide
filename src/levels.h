#pragma once
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace app
{
	// The handheld's volume, only through BananaOS's own script: /usr/libexec/banana/volume get | set N (0..100), then
	// SIGHUP to bananad so it stores the level (alsactl) as its own menu would. Never amixer: the mixer controls behind
	// the script differ between devices and BananaOS releases, and bananad would not know the level changed.
	// set() returns at once: the script takes tens of milliseconds, so it runs on a thread of its own, and only the
	// latest level is sent (a held key or a dragged slider doesn't queue up dozens of runs).
	class Volume
	{
	public:
		std::string script = "/usr/libexec/banana/volume";
		bool present() const;	// false on a computer: everything else does nothing
		int get() const;	// 0..100, -1 where unknown (blocks briefly: call at start, not every frame)
		void set(int percent);
		~Volume();

	private:
		void run();
		std::mutex lock;
		std::condition_variable wake;
		std::thread worker;
		int pending = -1;
		bool stopping = false;
	};

	// SIGHUP to bananad (found by name in /proc: it has no pid file): it reads its conf again, keeps a new volume level,
	// and sleeps if /run/banana/sleep-now exists. Nothing on a computer.
	void pokeBananad();

	// The panels' backlights, as BananaOS's Settings sets them: each in thousandths of its max_brightness, written to
	// /sys/class/backlight/backlight1 (top) or backlight0 (bottom), and both levels kept in /data/banana/backlight
	// ("<top> <bottom>") for bananad to restore at boot. Nothing else: bananad reads sysfs at each brightness key, so its
	// keys carry on from a level set here.
	struct Brightness
	{
		enum Panel { TOP, BOTTOM };
		std::string sysDir = "/sys/class/backlight", store = "/data/banana/backlight";
		bool present(Panel p) const;
		int permille(Panel p) const;	// -1 where it can't be read
		bool set(Panel p, int permille);	// 100 (10%) .. 1000; never 0 (that is off, and bananad rejects it)
	};

	// The bottom panel's backlight off and on, as BananaOS's one-panel launchers do: bl_power 4/0 on the backlight that
	// bananad names in /run/banana/backlight-bottom, and /run/banana/panel-off naming it while it is off so bananad keeps
	// it dark after a wake. Turned back on by the destructor (and by launch.sh if the app was killed).
	class BottomPanel
	{
	public:
		void init();
		void set(bool on);
		bool on() const { return isOn; }
		~BottomPanel() { set(true); }

	private:
		bool isOn = true;
		std::string backlight;	// empty: none here (a computer)
	};

	// The battery as BananaOS's menu reads it from /sys/class/power_supply. A charger online counts as charging: the
	// RG DS's rk817 gauge says "Discharging" while it charges. percent < 0: no battery (a computer).
	struct Battery
	{
		int percent = -1;
		bool charging = false;
		static Battery read(const std::string& base = "/sys/class/power_supply");
	};
}
