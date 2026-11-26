#include "input.h"

namespace app
{
	const char* buttonName(Button b)
	{
		static const char* names[] = {"none", "A", "B", "X", "Y", "L", "R", "L2", "R2", "L3", "Start", "Select", "F",
			"Up", "Down", "Left", "Right"};
		return names[(int)b];
	}

	// SDL names a pad's face buttons by position, Xbox style. The handheld is Nintendo style: A is the east button,
	// which SDL calls B, and B (south) is SDL's A. X and Y arrive under their printed names (SDL_CONTROLLER_BUTTON_X is
	// the key printed X), so only A and B swap.
	static Button fromPad(Uint8 b)
	{
		switch (b)
		{
		case SDL_CONTROLLER_BUTTON_B: return Button::A;
		case SDL_CONTROLLER_BUTTON_A: return Button::B;
		case SDL_CONTROLLER_BUTTON_X: return Button::X;
		case SDL_CONTROLLER_BUTTON_Y: return Button::Y;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Button::L;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Button::R;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return Button::L3;	// the one stick clicks
		case SDL_CONTROLLER_BUTTON_START: return Button::Start;
		case SDL_CONTROLLER_BUTTON_BACK: return Button::Select;
		case SDL_CONTROLLER_BUTTON_GUIDE: return Button::F;	// shared with bananad: F + volume is brightness
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return Button::Up;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Button::Down;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Button::Left;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Button::Right;
		default: return Button::None;
		}
	}

	// a keyboard stands in on a computer; SDLK_AC_BACK is the handheld's Back key (Linux KEY_BACK), taken as B
	static Button fromKey(SDL_Keycode k)
	{
		switch (k)
		{
		case SDLK_RETURN: case SDLK_SPACE: return Button::A;
		case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_AC_BACK: return Button::B;
		case SDLK_x: return Button::X;
		case SDLK_y: return Button::Y;
		case SDLK_q: return Button::L;
		case SDLK_e: return Button::R;
		case SDLK_LEFTBRACKET: return Button::L2;
		case SDLK_RIGHTBRACKET: return Button::R2;
		case SDLK_r: return Button::L3;
		case SDLK_s: return Button::Start;
		case SDLK_TAB: return Button::Select;
		case SDLK_f: return Button::F;
		case SDLK_UP: return Button::Up;
		case SDLK_DOWN: return Button::Down;
		case SDLK_LEFT: return Button::Left;
		case SDLK_RIGHT: return Button::Right;
		default: return Button::None;
		}
	}

	Input::Press Input::translate(const SDL_Event& e)
	{
		switch (e.type)
		{
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			return {fromPad(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN};
		case SDL_KEYDOWN:
			if (e.key.repeat) return {};	// key repeat is the app's to decide, as it is for the pad
			[[fallthrough]];
		case SDL_KEYUP:
			return {fromKey(e.key.keysym.sym), e.type == SDL_KEYDOWN};
		case SDL_CONTROLLERAXISMOTION:
		{
			const auto axis = e.caxis.axis;
			if (axis == SDL_CONTROLLER_AXIS_LEFTX) stickX = e.caxis.value;
			if (axis == SDL_CONTROLLER_AXIS_LEFTY) stickY = e.caxis.value;
			if (axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return {};
			// L2 and R2 are axes to SDL (0..32767), not buttons. Down past half, up again only below a quarter: a
			// trigger resting near one line would otherwise press over and over
			bool left = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
			bool& was = left ? l2 : r2;
			bool down = was ? e.caxis.value > 8000 : e.caxis.value > 16000;
			if (down == was) return {};
			was = down;
			return {left ? Button::L2 : Button::R2, down};
		}
		default:
			return {};
		}
	}

	void Input::hotplug(const SDL_Event& e)
	{
		// SDL sends DEVICEADDED for each pad present at start too, so this alone opens the built-in controls
		if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
		else if (e.type == SDL_CONTROLLERDEVICEREMOVED)
		{
			if (SDL_GameController* c = SDL_GameControllerFromInstanceID(e.cdevice.which)) SDL_GameControllerClose(c);
			stickX = stickY = 0;	// a stick held as the pad goes must not stay held
			l2 = r2 = false;
		}
	}
}
