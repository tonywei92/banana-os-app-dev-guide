// The pure parts' tests: no window, no handheld, a fraction of a second. Run after every change: ./build/app-test
#include "lidsleep.h"
#include <cstdio>
#include <string>

using namespace app;

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
	} while (0)

// off and back must give the user's file back byte for byte, whatever shape it had
static void roundTrip(const std::string& data, bool exists, const char* mode)
{
	LidNote note;
	note.conf = "/data/banana/bananad.conf";
	std::string off = lidSleepOff(data, exists, note);
	bool found = false;
	CHECK(!lidSleepIn(off, found) && found);
	CHECK(note.mode == mode);
	LidNote back;
	CHECK(parseLidNote(lidNoteText(note), back));
	std::string out;
	LidRestore r = lidRestore(off, true, back, out);
	if (!exists) CHECK(r == LidRestore::Remove);
	else CHECK(r == LidRestore::Write && out == data);
}

static void testLidSleep()
{
	bool found = false;
	// read as bananad reads it: comments and lines without '=' skipped, trimmed, last line wins, default on
	CHECK(lidSleepIn("lid_sleep = yes\n# lid_sleep = no\nlid_sleep=1", found) && found);
	CHECK(!lidSleepIn("  lid_sleep  =  off  \n", found) && found);
	CHECK(lidSleepEffective("", ""));
	CHECK(!lidSleepEffective("lid_sleep = no\n", ""));
	CHECK(lidSleepEffective("lid_sleep = no\n", "lid_sleep = yes\n"));

	roundTrip("idle = 300\nlid_sleep = yes\nother = 1\n", true, "changed");
	roundTrip("idle = 300\nlid_sleep=1", true, "changed");	// no final newline kept as it was
	roundTrip("idle = 300\n", true, "added");
	roundTrip("idle = 300", true, "added");	// the newline we put in before our line goes again
	roundTrip("", false, "created");

	// the user changed it in BananaOS's Settings while we held it: theirs wins
	LidNote note;
	note.conf = "x";
	lidSleepOff("lid_sleep = yes\n", true, note);
	std::string out;
	CHECK(lidRestore("lid_sleep = no \n", true, note, out) == LidRestore::Skip);
	CHECK(lidRestore("lid_sleep = yes\n", true, note, out) == LidRestore::Skip);
	CHECK(lidRestore("", false, note, out) == LidRestore::Skip);
}

int main()
{
	testLidSleep();
	if (failures) return 1;
	printf("app-test: all passed\n");
	return 0;
}
