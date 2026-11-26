// A starter app for BananaOS on the RG DS / RG DS Plus: both panels drawn, the buttons, the bottom touchscreen, the
// lid and sleep, volume, brightness, battery and a log. Read BANANAOS-APP-GUIDE.md for the why of each part.
//
// Demo controls: Left/Right volume -/+5, Up/Down top brightness +/-10%, Select bottom backlight off (any button or
// touch turns it on again), X keeps it running with the lid shut, A resumes after a pause, F + Start quits. A computer: arrows, Enter (A), Esc (B), Tab
// (Select), S (Start), F (F), the mouse on the bottom window is the finger.
#include "applog.h"
#include "input.h"
#include "levels.h"
#include "lidsleep.h"
#include "sleepwatch.h"
#include "touch.h"
#include "version.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace app;

namespace
{
	// Everything is laid out in 640x480 units on both panels; each panel's real size (640x480 on the RG DS, 1024x768 on
	// the RG DS Plus, 4:3 both) is only a scale. Text is rendered at the real size so it stays sharp.
	constexpr int W = 640, H = 480;

	struct Panel
	{
		SDL_Window* window = nullptr;
		SDL_Renderer* ren = nullptr;
		TTF_Font* font = nullptr;
		float scale = 1;	// real pixels per unit
		std::string drawn;	// the signature of what's on it now: drawn again only when it changes

		bool init(SDL_Window* w, const std::string& fontPath)
		{
			window = w;
			ren = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
			if (!ren) ren = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
			if (!ren) return false;
			int ow = 0, oh = 0;
			SDL_GetRendererOutputSize(ren, &ow, &oh);
			scale = std::min(ow / (float)W, oh / (float)H);
			font = TTF_OpenFont(fontPath.c_str(), (int)std::lround(22 * scale));
			if (!font) logf("font %s: %s", fontPath.c_str(), TTF_GetError());
			logf("panel: %dx%d pixels, scale %.2f", ow, oh, scale);
			return font != nullptr;
		}
		SDL_Rect px(int x, int y, int w, int h) const
		{
			return {(int)std::lround(x * scale), (int)std::lround(y * scale), (int)std::lround(w * scale), (int)std::lround(h * scale)};
		}
		void fill(int x, int y, int w, int h, SDL_Color c)
		{
			SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
			SDL_Rect r = px(x, y, w, h);
			SDL_RenderFillRect(ren, &r);
		}
		// Text through a cache: TTF_Render* (glyph rasterising) plus the upload to a texture is by far the costliest
		// thing a 2D page does, and a page mostly shows the same strings redraw after redraw. Keyed by colour + text;
		// emptied whole when it grows past a bound (simple, and a page's strings come back in one redraw).
		struct Tex
		{
			SDL_Texture* tex = nullptr;
			int w = 0, h = 0;
		};
		std::unordered_map<std::string, Tex> textCache;

		void text(int x, int y, const std::string& s, SDL_Color c = {235, 235, 235, 255})
		{
			if (s.empty() || !font) return;
			char key[16];
			snprintf(key, sizeof key, "%02x%02x%02x|", c.r, c.g, c.b);
			std::string k = key + s;
			auto it = textCache.find(k);
			if (it == textCache.end())
			{
				if (textCache.size() > 256) clearText();
				SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s.c_str(), c);
				if (!surf) return;
				Tex t;
				t.tex = SDL_CreateTextureFromSurface(ren, surf);
				t.w = surf->w;
				t.h = surf->h;
				SDL_FreeSurface(surf);
				it = textCache.emplace(std::move(k), t).first;
			}
			SDL_Rect r = {(int)std::lround(x * scale), (int)std::lround(y * scale), it->second.w, it->second.h};
			SDL_RenderCopy(ren, it->second.tex, nullptr, &r);
		}
		void clearText()
		{
			for (auto& [k, t] : textCache) SDL_DestroyTexture(t.tex);
			textCache.clear();
		}
	};

	// the font: beside the binary in the bundle (bin/../share/myapp/fonts), else the source tree's. BananaOS has no
	// fonts of its own, so an app that doesn't ship one draws no text
	std::string findFont()
	{
		std::string candidates[2];
		if (char* base = SDL_GetBasePath())
		{
			candidates[0] = std::string(base) + "../share/" + APP_ID + "/fonts/Inter-Regular.ttf";
			SDL_free(base);
		}
		candidates[1] = std::string(APP_SOURCE_ASSETS) + "/fonts/Inter-Regular.ttf";
		for (auto& c : candidates)
			if (!c.empty())
				if (FILE* f = fopen(c.c_str(), "rb")) { fclose(f); return c; }
		return candidates[1];
	}

	// what the panels show: plain data, default-initialised (an uninitialised member reads as 0 on a Mac far more often
	// than on glibc, so the bug only shows on the handheld)
	struct State
	{
		std::string lastButton = "-";
		float touchX = -1, touchY = -1;	// 0..1 on the bottom panel, -1 none
		bool touching = false;
		bool paused = false;
		std::string pausedWhy;
		bool lidClosed = false;
		bool keepAwake = false;	// X: go on with the lid shut (the lid sleep turned off while running)
		int volume = -1, topBrightness = -1;
		Battery battery;
		int seconds = 0;	// a running counter: stops while paused
	};

	std::string signatureTop(const State& s)
	{
		return std::to_string(s.seconds) + "|" + std::to_string(s.paused) + s.pausedWhy + "|" + std::to_string(s.volume) +
			"|" + std::to_string(s.topBrightness) + "|" + std::to_string(s.battery.percent) + std::to_string(s.battery.charging) +
			"|" + std::to_string(s.lidClosed) + std::to_string(s.keepAwake);
	}

	std::string signatureBottom(const State& s)
	{
		return s.lastButton + "|" + std::to_string((int)(s.touchX * W)) + "," + std::to_string((int)(s.touchY * H)) + "|" +
			std::to_string(s.touching);
	}

	void drawTop(Panel& p, const State& s)
	{
		p.fill(0, 0, W, H, {20, 24, 32, 255});
		p.fill(0, 0, W, 56, {196, 150, 20, 255});
		p.text(20, 14, std::string("My App ") + VERSION + " (" + COMMIT + ")", {20, 20, 20, 255});
		p.text(20, 90, "Top panel (SDL display " + std::to_string(SDL_GetWindowDisplayIndex(p.window)) + ")");
		p.text(20, 140, s.paused ? "Paused: " + s.pausedWhy + " (A resumes)" : "Running: " + std::to_string(s.seconds) + " s");
		p.text(20, 190, "Volume: " + (s.volume < 0 ? std::string("n/a") : std::to_string(s.volume)) + "   (Left/Right)");
		p.text(20, 230, "Top brightness: " + (s.topBrightness < 0 ? std::string("n/a") : std::to_string(s.topBrightness / 10) + "%") + "   (Up/Down)");
		p.text(20, 270, "Battery: " + (s.battery.percent < 0 ? std::string("n/a") : std::to_string(s.battery.percent) + "%" +
			(s.battery.charging ? " charging" : "")));
		p.text(20, 310, std::string("Lid: ") + (s.lidClosed ? "closed" : "open"));
		p.text(20, 350, std::string("Lid shut: ") + (s.keepAwake ? "keep running" : "pause and sleep") + "   (X)");
		p.text(20, 420, "F + Start quits", {150, 150, 150, 255});
		SDL_RenderPresent(p.ren);
	}

	void drawBottom(Panel& p, const State& s)
	{
		p.fill(0, 0, W, H, {28, 28, 36, 255});
		p.text(20, 20, "Bottom panel: touch me");
		p.text(20, 60, "Last button: " + s.lastButton);
		p.text(20, 420, "Select: backlight off", {150, 150, 150, 255});
		if (s.touchX >= 0)
		{
			int x = (int)(s.touchX * W), y = (int)(s.touchY * H);
			p.fill(x - 12, y - 12, 24, 24, s.touching ? SDL_Color{240, 190, 40, 255} : SDL_Color{110, 110, 110, 255});
			p.text(20, 100, "Touch: " + std::to_string(x) + ", " + std::to_string(y));
		}
		SDL_RenderPresent(p.ren);
	}
}

int main(int argc, char** argv)
{
#ifdef __GLIBC__
	// glibc raises its mmap threshold to the largest block freed so far (up to 32 MB), after which big buffers come from
	// the heap and freeing them doesn't give memory back. Pinned, a block of 256 KB or more is a mapping of its own,
	// returned when freed. (The Mac's allocator behaves differently: judge memory on Linux.)
	mallopt(M_MMAP_THRESHOLD, 256 << 10);
#endif
	const std::string state = stateDir();
	// --lid-restore (launch.sh, after the app ended): its lines go to the launcher's file; log.txt keeps the run that ended
	const bool lidRestoreOnly = argc > 1 && !strcmp(argv[1], "--lid-restore");
	if (!lidRestoreOnly) openLog(state);
	installCrashHandler(state);
	logf("my-app %s (%s), state %s", VERSION, COMMIT, state.c_str());

	// A lid-sleep change left by a run that never ended (power loss, a SIGKILL) is undone before anything else.
	// launch.sh runs "my-app --lid-restore" after every exit, so a crash is covered too, by this same code.
	LidGuard lidGuard(state);
	lidGuard.recover();
	if (lidRestoreOnly) return 0;

	// a window losing focus to the other panel's window must not minimise
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0 || TTF_Init() < 0)
	{
		logf("SDL: %s", SDL_GetError());
		return 1;
	}
	// our own events, posted by the reader threads (SDL_PushEvent is thread-safe; nothing else they could touch is)
	const Uint32 evTouch = SDL_RegisterEvents(3), evLid = evTouch + 1, evWake = evTouch + 2;

	// On the handheld SDL's video driver is KMSDRM and each panel is one display, a window filling it. Elsewhere two
	// windows stand in. BananaOS's launchers set SDL_KMSDRM_FIRST to the top panel's connector (DSI-2), so the top
	// panel is display 0; the name check below covers a launch without it.
	const bool handheld = strcmp(SDL_GetCurrentVideoDriver(), "KMSDRM") == 0;
	int displays = SDL_GetNumVideoDisplays(), topDisplay = 0;
	const char* topEnv = getenv("BANANA_TOP");
	const std::string topConnector = topEnv && *topEnv ? topEnv : "DSI-2";
	for (int i = 0; i < displays; i++)
	{
		const char* name = SDL_GetDisplayName(i);
		logf("display %d: %s", i, name ? name : "?");
		if (handheld && name && strstr(name, topConnector.c_str())) topDisplay = i;
	}
	const int bottomDisplay = topDisplay == 0 ? 1 : 0;

	SDL_Window *topWin, *bottomWin = nullptr;
	if (handheld)
	{
		topWin = SDL_CreateWindow("my-app top", SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay), SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay),
			W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
		if (displays > 1)
			bottomWin = SDL_CreateWindow("my-app bottom", SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay),
				SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay), W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
		else logf("only %d display: no bottom panel", displays);
	}
	else
	{
		topWin = SDL_CreateWindow("my-app: top", 80, 40, W, H, SDL_WINDOW_ALLOW_HIGHDPI);
		bottomWin = SDL_CreateWindow("my-app: bottom", 80, 40 + H + 40, W, H, SDL_WINDOW_ALLOW_HIGHDPI);
	}
	const std::string font = findFont();
	Panel top, bottom;
	if (!topWin || !top.init(topWin, font) || (bottomWin && !bottom.init(bottomWin, font)))
	{
		logf("window: %s", SDL_GetError());
		return 1;
	}

	// The bottom touchscreen, ours from evdev. While it is, SDL's own reading of the touchscreens (which can't say which
	// panel a finger is on) is ignored, and its touch-to-mouse copy and cursor are off: on KMSDRM moving the cursor is a
	// commit that can wait for the next flip inside SDL_PollEvent.
	BottomTouch touch;
	const char* touchEnv = getenv("BANANA_TOUCH");
	const bool ownTouch = touch.start(touchEnv && *touchEnv ? touchEnv : "fe5e0000.i2c", [evTouch](Pointer p) {
		SDL_Event e = {};
		e.type = evTouch;
		e.user.code = p.kind;
		// the position packed in data1/data2 as integers: no allocation to leak if the event is dropped
		e.user.data1 = (void*)(intptr_t)std::lround(p.x * 100000);
		e.user.data2 = (void*)(intptr_t)std::lround(p.y * 100000);
		SDL_PushEvent(&e);
	});
	if (ownTouch)
	{
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
		SDL_ShowCursor(SDL_DISABLE);
	}
	logf("touch: %s", ownTouch ? "the bottom touchscreen from evdev" : "none found; the mouse on the bottom window stands in");

	SleepWatch sleep;
	sleep.start(
		[evLid](bool closed) {
			SDL_Event e = {};
			e.type = evLid;
			e.user.code = closed;
			SDL_PushEvent(&e);
		},
		[evWake] {
			SDL_Event e = {};
			e.type = evWake;
			SDL_PushEvent(&e);
		});

	Volume volume;
	Brightness brightness;
	BottomPanel bottomPanel;
	bottomPanel.init();
	Input input;
	State s;
	s.volume = volume.get();
	s.topBrightness = brightness.permille(Brightness::TOP);
	s.battery = Battery::read();
	s.lidClosed = sleep.lidClosed();

	// KMSDRM drops the first frames presented after the start (and after a wake): a panel drawn once and then left
	// alone can stay black. So both are drawn again every 100 ms until freshUntil, and every 2 s after that whatever
	// happens; otherwise only when their signature changes. (8 s at the start: the first launch after boot is slow.)
	Uint32 freshUntil = SDL_GetTicks() + 8000, refreshedAt = 0, batteryAt = SDL_GetTicks(), tickAt = SDL_GetTicks();
	bool fHeld = false, fCombo = false, running = true;

	auto pause = [&](const char* why) {
		if (!s.paused) logf("pause: %s", why);
		s.paused = true;
		s.pausedWhy = why;
	};

	while (running)
	{
		// wait for an event, but not longer than the next thing the loop must do on its own (no busy loop: battery)
		// Wait for an event, but no longer than the next thing the loop must do by itself: CPU near 0 while idle (no busy
		// loop, no fixed 60 fps), and still on time for the refresh and the running counter.
		Uint32 t0 = SDL_GetTicks();
		int waitMs = t0 < freshUntil ? 100 : 2000;
		if (!s.paused) waitMs = std::min<int>(waitMs, (int)std::max<Sint64>(1, (Sint64)tickAt + 1000 - t0));
		SDL_Event e;
		bool got = SDL_WaitEventTimeout(&e, waitMs);
		while (got)
		{
			if (e.type == SDL_QUIT) running = false;	// SDL turns SIGTERM and SIGINT into this: a clean quit
			else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
			else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SHOWN))
				freshUntil = std::max(freshUntil, SDL_GetTicks() + 500);
			else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) input.hotplug(e);
			else if (e.type == evLid)
			{
				s.lidClosed = e.user.code != 0;
				logf("lid: %s", s.lidClosed ? "closed" : "open");
				// opening it resumes nothing: that's the user's to press. Keeping awake, the lid changes nothing
				if (s.lidClosed && !s.keepAwake) pause("lid closed");
			}
			else if (e.type == evWake)
			{
				logf("wake (SIGCONT)");
				pause("woke from sleep");
				freshUntil = SDL_GetTicks() + 2000;
				s.battery = Battery::read();
				s.lidClosed = sleep.lidClosed();
			}
			else if (e.type == evTouch)
			{
				s.touchX = (intptr_t)e.user.data1 / 100000.f;
				s.touchY = (intptr_t)e.user.data2 / 100000.f;
				s.touching = e.user.code != Pointer::Up;
				if (e.user.code == Pointer::Down) bottomPanel.set(true);
			}
			else if (!ownTouch && bottomWin && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION))
			{
				// a computer: the mouse on the bottom window is the finger (window coordinates are 640x480 units)
				Uint32 id = e.type == SDL_MOUSEMOTION ? e.motion.windowID : e.button.windowID;
				if (id == SDL_GetWindowID(bottomWin) && (e.type != SDL_MOUSEMOTION || (e.motion.state & SDL_BUTTON_LMASK)))
				{
					int x = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, y = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
					s.touchX = std::clamp(x / (float)W, 0.f, 1.f);
					s.touchY = std::clamp(y / (float)H, 0.f, 1.f);
					s.touching = e.type != SDL_MOUSEBUTTONUP;
				}
			}
			else if (Input::Press p = input.translate(e); p.button != Button::None)
			{
				logf("input: %s %s", buttonName(p.button), p.down ? "down" : "up");	// every press in the log
				if (p.button == Button::F)
				{
					// F is bananad's too (F + volume keys = brightness): act on F alone only as it lets go, and only if
					// nothing else was pressed while it was held
					if (p.down) fCombo = false;
					else if (fHeld && !fCombo) logf("F alone: open your quick menu here");
					fHeld = p.down;
				}
				else if (p.down)
				{
					if (fHeld) fCombo = true;
					s.lastButton = buttonName(p.button);
					if (p.button != Button::Select) bottomPanel.set(true);
					if (fHeld && p.button == Button::Start) running = false;
					else if (p.button == Button::A && s.paused) { s.paused = false; s.pausedWhy.clear(); logf("resume"); }
					else if (p.button == Button::Left || p.button == Button::Right)
					{
						if (s.volume >= 0)
						{
							s.volume = std::clamp(s.volume + (p.button == Button::Right ? 5 : -5), 0, 100);
							volume.set(s.volume);	// returns at once; the script runs on its own thread
						}
					}
					else if (p.button == Button::Up || p.button == Button::Down)
					{
						if (s.topBrightness >= 0)
						{
							int to = std::clamp((s.topBrightness + 50) / 100 * 100 + (p.button == Button::Up ? 100 : -100), 100, 1000);
							if (brightness.set(Brightness::TOP, to)) s.topBrightness = to;
						}
					}
					else if (p.button == Button::Select) bottomPanel.set(false);
					else if (p.button == Button::X) s.keepAwake = !s.keepAwake;
				}
			}
			got = SDL_PollEvent(&e);
		}

		const Uint32 now = SDL_GetTicks();
		if (now - tickAt >= 1000)
		{
			// far behind (paused, or the process stopped by a sleep): start again from now, no burst of catch-up seconds
			tickAt = now - tickAt >= 2000 ? now : tickAt + 1000;
			if (!s.paused) s.seconds++;
		}
		// sysfs reads are cheap but not free: the battery moves slowly
		if (now - batteryAt >= 30000)
		{
			batteryAt = now;
			s.battery = Battery::read();
		}
		// the lid sleep off only while the app needs it: here, keeping awake and not paused (a player: playing)
		lidGuard.tick(now, s.keepAwake && !s.paused, s.lidClosed);
		const bool refresh = now - refreshedAt >= (now < freshUntil ? 100u : 2000u);
		if (refresh) refreshedAt = now;
		if (std::string sig = signatureTop(s); refresh || sig != top.drawn)
		{
			top.drawn = sig;
			drawTop(top, s);
		}
		if (bottomWin)
			if (std::string sig = signatureBottom(s); refresh || sig != bottom.drawn)
			{
				bottom.drawn = sig;
				drawBottom(bottom, s);
			}
	}

	logf("quit");
	lidGuard.release(sleep.lidClosed());	// the lid sleep back at once (and, lid shut, the sleep it would have brought)
	// the bottom backlight back on (BottomPanel's destructor) and the threads stopped before SDL goes
	bottomPanel.set(true);
	top.clearText();
	bottom.clearText();
	touch.stop();
	sleep.stop();
	TTF_Quit();
	SDL_Quit();
	return 0;
}
