#include "lidsleep.h"
#include "applog.h"
#include "levels.h"
#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace app
{
	static const char* RUN_DIR = "/run/banana";
	static const char* ETC_CONF = "/etc/banana/bananad.conf";
	static const char* DATA_CONF = "/data/banana/bananad.conf";

	// ---- pure ------------------------------------------------------------------------------------------------------

	static std::string trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && isspace((unsigned char)s[a])) a++;
		while (b > a && isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}

	struct Line
	{
		size_t at = 0, len = 0;	// where it starts, how long (its '\n' not counted)
	};

	static std::vector<Line> linesOf(const std::string& text)
	{
		std::vector<Line> out;
		size_t at = 0;
		while (at < text.size())
		{
			size_t nl = text.find('\n', at);
			if (nl == std::string::npos) nl = text.size();
			out.push_back({at, nl - at});
			at = nl + 1;
		}
		return out;
	}

	// bananad's own test for a lid_sleep line: read it exactly as bananad does, or we'd change a line it ignores
	static bool isLidLine(const std::string& line, std::string* value = nullptr)
	{
		if (line.empty() || line[0] == '#') return false;
		size_t eq = line.find('=');
		if (eq == std::string::npos || trim(line.substr(0, eq)) != "lid_sleep") return false;
		if (value) *value = trim(line.substr(eq + 1));
		return true;
	}

	static int lastLidLine(const std::string& text, const std::vector<Line>& lines)
	{
		int last = -1;
		for (size_t i = 0; i < lines.size(); i++)
			if (isLidLine(text.substr(lines[i].at, lines[i].len))) last = (int)i;
		return last;
	}

	bool lidSleepIn(const std::string& conf, bool& found)
	{
		found = false;
		bool on = false;
		for (const Line& l : linesOf(conf))
		{
			std::string v;
			if (!isLidLine(conf.substr(l.at, l.len), &v)) continue;
			found = true;
			on = v == "yes" || v == "1";
		}
		return on;
	}

	bool lidSleepEffective(const std::string& etcConf, const std::string& dataConf)
	{
		bool on = true, f = false;	// bananad's default
		bool e = lidSleepIn(etcConf, f);
		if (f) on = e;
		bool d = lidSleepIn(dataConf, f);
		if (f) on = d;
		return on;
	}

	std::string lidNoteText(const LidNote& n)
	{
		return "mode=" + n.mode + "\nconf=" + n.conf + "\nwas=" + n.was + "\nwrote=" + n.wrote + "\nnl=" + (n.nl ? "1" : "0") + "\n";
	}

	bool parseLidNote(const std::string& text, LidNote& n)
	{
		n = LidNote();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "mode") n.mode = v;
			else if (k == "conf") n.conf = v;
			else if (k == "was") n.was = v;
			else if (k == "wrote") n.wrote = v;
			else if (k == "nl") n.nl = v == "1";
		}
		return (n.mode == "changed" || n.mode == "added" || n.mode == "created") && !n.wrote.empty() && !n.conf.empty();
	}

	std::string lidSleepOff(const std::string& data, bool exists, LidNote& note)
	{
		note.wrote = "lid_sleep = no";
		note.nl = false;
		note.was.clear();
		std::vector<Line> lines = linesOf(data);
		int i = lastLidLine(data, lines);
		if (i >= 0)
		{
			// the line bananad reads, in its place; every other byte as it was
			note.mode = "changed";
			note.was = data.substr(lines[i].at, lines[i].len);
			return data.substr(0, lines[i].at) + note.wrote + data.substr(lines[i].at + lines[i].len);
		}
		note.mode = exists ? "added" : "created";
		note.nl = !data.empty() && data.back() != '\n';
		return data + (note.nl ? "\n" : "") + note.wrote + "\n";
	}

	LidRestore lidRestore(const std::string& data, bool exists, const LidNote& note, std::string& out)
	{
		out.clear();
		if (!exists) return LidRestore::Skip;
		std::vector<Line> lines = linesOf(data);
		int i = lastLidLine(data, lines);
		// still ours, byte for byte? If not, the user changed it since: their choice wins
		if (i < 0 || lines[i].len != note.wrote.size() || data.compare(lines[i].at, lines[i].len, note.wrote) != 0)
			return LidRestore::Skip;
		const Line& l = lines[i];
		if (note.mode == "changed")
		{
			out = data.substr(0, l.at) + note.was + data.substr(l.at + l.len);
			return LidRestore::Write;
		}
		// added or created: our line out with its newline (and the one we put before it, if it was the last line)
		size_t end = l.at + l.len;
		out = data.substr(0, l.at) + data.substr(end < data.size() ? end + 1 : end);
		if (note.nl && (size_t)i == lines.size() - 1 && !out.empty() && out.back() == '\n') out.pop_back();
		if (note.mode == "created" && out.empty()) return LidRestore::Remove;
		return LidRestore::Write;
	}

	// ---- files and signals -----------------------------------------------------------------------------------------

	static bool slurp(const std::string& path, std::string& out)
	{
		out.clear();
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		std::stringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		return true;
	}

	// a temporary file, fsync'd, renamed over: the power can go at any moment on a handheld, and a half-written
	// bananad.conf is the user's settings lost
	static bool writeDurable(const std::string& path, const std::string& text)
	{
		struct stat st;
		mode_t mode = stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0644;
		std::string tmp = path + ".myapp-tmp";
		int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
		if (fd < 0) return false;
		bool ok = write(fd, text.data(), text.size()) == (ssize_t)text.size() && fsync(fd) == 0;
		close(fd);
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		return true;
	}

	static bool onBananaOS()
	{
		struct stat st;
		return stat(RUN_DIR, &st) == 0;	// a computer: never touch anything
	}

	bool askBananadSleep()
	{
		if (!onBananaOS()) return false;
		if (FILE* f = fopen((std::string(RUN_DIR) + "/sleep-now").c_str(), "w")) fclose(f);
		else return false;
		pokeBananad();
		return true;
	}

	void LidGuard::hold()
	{
		if (!onBananaOS()) return;
		std::string etc, data;
		slurp(ETC_CONF, etc);
		bool exists = slurp(DATA_CONF, data);
		// already off (the user's choice): nothing to change, so nothing to undo
		if (!lidSleepEffective(etc, data)) return;
		LidNote note;
		note.conf = DATA_CONF;
		std::string out = lidSleepOff(data, exists, note);
		// the note first: whatever happens after this, the next start (or the launcher) knows what to undo
		if (!writeDurable(notePath(), lidNoteText(note)))
		{
			logf("lid: can't write %s: lid sleep left on", notePath().c_str());
			return;
		}
		if (!writeDurable(note.conf, out))
		{
			logf("lid: can't write %s: lid sleep left on", note.conf.c_str());
			unlink(notePath().c_str());
			return;
		}
		held = true;
		pokeBananad();	// bananad reads its conf only at start and on SIGHUP
		logf("lid: sleep off while needed (%s)", note.mode.c_str());
	}

	void LidGuard::restore(bool lidClosed)
	{
		held = false;
		std::string text;
		if (!slurp(notePath(), text)) return;
		LidNote note;
		if (!parseLidNote(text, note))
		{
			unlink(notePath().c_str());
			return;
		}
		std::string data, out;
		bool exists = slurp(note.conf, data);
		LidRestore r = lidRestore(data, exists, note, out);
		bool ok = r == LidRestore::Write ? writeDurable(note.conf, out) : r == LidRestore::Remove ? unlink(note.conf.c_str()) == 0 : true;
		if (!ok)
		{
			logf("lid: can't put %s back; the note stays for the next try", note.conf.c_str());
			return;
		}
		unlink(notePath().c_str());
		logf("lid: %s", r == LidRestore::Skip ? "restore skipped (changed in BananaOS Settings)" : "sleep restored");
		// the lid is shut and closing it sleeps again: the sleep it would have brought, now (or the handheld stays awake
		// in a pocket, draining the battery, until the lid is opened and closed)
		if (lidClosed)
		{
			std::string etc, now;
			slurp(ETC_CONF, etc);
			slurp(note.conf, now);
			if (lidSleepEffective(etc, now))
			{
				logf("lid: closed: asking bananad to sleep");
				askBananadSleep();
				return;	// (its SIGHUP also reloads the conf)
			}
		}
		pokeBananad();
	}

	void LidGuard::recover()
	{
		struct stat st;
		if (stat(notePath().c_str(), &st) != 0) return;
		logf("lid: a note from a run that didn't end: putting the lid sleep back");
		restore(false);
	}

	void LidGuard::tick(unsigned nowMs, bool wants, bool lidClosed)
	{
		if (wants)
		{
			offSince = 0;
			if (!held && !tried) hold();
			tried = true;	// once per stretch: a failure isn't retried (and logged) every pass
			return;
		}
		tried = false;
		if (!held) return;
		// a grace before the undo: a gap between two tracks or a short pause isn't the end, and with the lid shut an
		// undo there would sleep the handheld in the middle
		if (!offSince) offSince = nowMs ? nowMs : 1;
		else if (nowMs - offSince >= 3000)
		{
			offSince = 0;
			restore(lidClosed);
		}
	}

	void LidGuard::release(bool lidClosed)
	{
		offSince = 0;
		tried = false;
		if (held) restore(lidClosed);
	}
}
