#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace app
{
	// The lid and BananaOS's sleep. bananad sends the app's whole process group SIGSTOP when the handheld sleeps (lid
	// closed with lid sleep on, or the power key) and SIGCONT when it wakes; the app gets no warning before the stop.
	//   - SW_LID (the lid's hall sensor, read from evdev as bananad reads it): onLid(true) as it closes, so a game or a
	//     player can pause before the stop, and onLid(false) as it opens. With lid sleep off, closing the lid only
	//     darkens the panels and the process is never stopped: this event is then all you get.
	//   - SIGCONT: onWake(), after any sleep. Pause here too (the lid's event may not have been read before the stop),
	//     and redraw both panels for a while: KMSDRM drops the first frames after a wake as after a start.
	// Callbacks run on the watch's own thread: push an SDL user event from them, touch nothing else.
	class SleepWatch
	{
	public:
		bool start(std::function<void(bool closed)> onLid, std::function<void()> onWake);
		void stop();
		~SleepWatch() { stop(); }
		bool lidClosed() const { return lidShut.load(); }

	private:
		void run();
		std::function<void(bool)> onLid;
		std::function<void()> onWake;
		std::thread thread;
		int stopPipe[2] = {-1, -1};
		std::vector<int> lidFds;	// input devices that can report SW_LID (Linux only)
		std::atomic<bool> lidShut{false};
	};
}
