#pragma once
#include <string>

namespace app
{
	// Keep running with the lid shut (BANANAOS-APP-GUIDE.md, "Keep running with the lid shut").
	// With BananaOS's `lid_sleep` on (the default), bananad suspends the handheld the moment the lid shuts, whatever runs.
	// An app that must go on (music in a pocket, a download) turns that line of /data/banana/bananad.conf to `no` while
	// it needs to, and puts it back after, keeping every other byte of the file: it's the user's settings file.

	// ---- the pure part: text in, text out (test these on a computer) ---------------------------------------------

	// `lid_sleep` read from a conf's text as bananad reads it: a line starting with '#' or without '=' is skipped, key
	// and value trimmed, "yes" or "1" is on, anything else off; the last line wins. found: whether there was one.
	bool lidSleepIn(const std::string& conf, bool& found);
	// what bananad ends up with: on by default, then /etc/banana/bananad.conf, then /data/banana/bananad.conf
	bool lidSleepEffective(const std::string& etcConf, const std::string& dataConf);

	// what was changed, kept as "lid-restore" in the state folder until it is undone
	struct LidNote
	{
		std::string mode;	// "changed" (a line rewritten), "added" (a line appended), "created" (no file before)
		std::string conf;	// the file's path
		std::string was;	// changed: the line as it was
		std::string wrote;	// the line we wrote
		bool nl = false;	// added: the file didn't end in a newline, so one went in before our line
	};
	std::string lidNoteText(const LidNote& n);
	bool parseLidNote(const std::string& text, LidNote& n);

	// the conf's text with its effective line made `lid_sleep = no` (the last one rewritten, or one appended)
	std::string lidSleepOff(const std::string& data, bool exists, LidNote& note);

	enum class LidRestore { Write, Remove, Skip };
	// the undo: Write `out` over the file, Remove it (we made it and nothing else went in), or Skip: the line is no
	// longer byte for byte what we wrote, so the user changed it in BananaOS's Settings meanwhile, and that wins
	LidRestore lidRestore(const std::string& data, bool exists, const LidNote& note, std::string& out);

	// ---- the guard: files and signals, on the main thread ----------------------------------------------------------

	class LidGuard
	{
	public:
		explicit LidGuard(std::string stateDir) : noteDir(std::move(stateDir)) {}
		// at start, before anything else: a note left by a run that never ended (power loss, SIGKILL) is undone
		void recover();
		// every pass of the main loop: wants = the app needs to stay awake now; lidClosed = the lid's switch now
		void tick(unsigned nowMs, bool wants, bool lidClosed);
		// quitting: put it back now, no grace
		void release(bool lidClosed);
		bool holding() const { return held; }

	private:
		void hold();
		void restore(bool lidClosed);
		std::string notePath() const { return noteDir + "/lid-restore"; }
		std::string noteDir;
		bool held = false, tried = false;
		unsigned offSince = 0;
	};

	// ask bananad for a sleep, as closing the lid does: create /run/banana/sleep-now, then SIGHUP bananad (its reload
	// removes the file and sleeps). The only sanctioned way: never write to /sys/power yourself, bananad would not know.
	bool askBananadSleep();
}
