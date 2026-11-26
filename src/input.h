#pragma once
#include <SDL.h>

namespace app
{
	// The RG DS Plus's buttons by what is printed on them. F is the function key under the screen (SDL's GUIDE).
	enum class Button { None, A, B, X, Y, L, R, L2, R2, L3, Start, Select, F, Up, Down, Left, Right };
	const char* buttonName(Button b);

	// SDL events in, the handheld's buttons out (down and up). Holds the triggers' state, so keep one.
	class Input
	{
	public:
		struct Press
		{
			Button button = Button::None;
			bool down = false;
		};
		// None when the event isn't a button (or is a trigger that didn't cross its line)
		Press translate(const SDL_Event& e);
		// open every pad there is now and any plugged in later (call on SDL_CONTROLLERDEVICEADDED/REMOVED too)
		void hotplug(const SDL_Event& e);
		// the left stick, -32768..32767 each way, as last reported (there is no right stick on the RG DS Plus)
		int stickX = 0, stickY = 0;

	private:
		bool l2 = false, r2 = false;
	};
}
