#pragma once
#include <functional>
#include <string>
#include <thread>

namespace app
{
	// A finger on the bottom panel; x and y from 0 to 1 across it.
	struct Pointer
	{
		enum Kind { Down, Move, Up } kind;
		float x, y;
	};

	// The bottom touchscreen read straight from evdev. SDL can't be used for it: the handheld's touch devices can share a
	// name and SDL doesn't say which panel a finger is on, so the bottom one is found by where it sits in sysfs (its I2C
	// controller, "fe5e0000.i2c" on the RG DS / RG DS Plus, as BananaOS's own menu finds it). One finger (slot 0).
	// onPointer runs on the reader's own thread: push an SDL user event from it, touch nothing else.
	class BottomTouch
	{
	public:
		// false where there's no such device (a computer): use SDL's mouse events on the bottom window instead
		bool start(const std::string& sysfsFragment, std::function<void(Pointer)> onPointer);
		void stop();
		~BottomTouch() { stop(); }

	private:
		void run();
		std::function<void(Pointer)> onPointer;
		std::thread thread;
		int fd = -1, stopPipe[2] = {-1, -1};
		int minX = 0, maxX = 0, minY = 0, maxY = 0;
	};
}
