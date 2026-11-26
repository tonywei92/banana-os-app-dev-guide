#pragma once

namespace app
{
	// Generated into the build folder from version.cpp.in (CMakeLists.txt): the commit lives in that one file, so a new
	// commit recompiles one file, not every file that a -D define would have reached.
	extern const char* const VERSION;
	extern const char* const COMMIT;
}
