cmake_minimum_required(VERSION 3.16)
# The version is read by packaging/build-bundle.sh into the bundle's VERSION file, which the Store compares
# (numerically: 0.1.10 > 0.1.9) against the latest GitHub release.
project(my-app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# SDL2 and SDL2_ttf come from BananaOS on the handheld (its SDL is patched), from Homebrew on a Mac.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
find_package(Threads REQUIRED)

# The version and commit, in one generated file. configure_file rewrites it only when its text changes, so a build
# with the same commit recompiles nothing, and a new commit recompiles this one file. APP_COMMIT comes from the
# bundle script (git isn't in the build container); a desktop build asks git itself.
if(NOT DEFINED APP_COMMIT)
	execute_process(COMMAND git describe --always --dirty --exclude=*
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE APP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT APP_COMMIT)
	set(APP_COMMIT unknown)
endif()
configure_file(src/version.cpp.in ${CMAKE_BINARY_DIR}/version.cpp @ONLY)

add_executable(my-app
	src/main.cpp
	${CMAKE_BINARY_DIR}/version.cpp
	src/applog.cpp
	src/input.cpp
	src/touch.cpp
	src/sleepwatch.cpp
	src/levels.cpp
	src/lidsleep.cpp
	src/child.cpp	# (not used by the demo: compiled so it stays building)
)
target_compile_definitions(my-app PRIVATE
	# a desktop build finds the fonts in the source tree; the bundle finds them beside the binary (../share)
	APP_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
target_include_directories(my-app PRIVATE src)
target_compile_options(my-app PRIVATE -Wall -Wextra)
target_link_libraries(my-app PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF Threads::Threads)

# The pure parts' tests (no window, no handheld): ./build/app-test. Cheap enough to run on every change.
add_executable(app-test tests/app_test.cpp src/lidsleep.cpp src/levels.cpp src/applog.cpp)
target_include_directories(app-test PRIVATE src)
target_compile_options(app-test PRIVATE -Wall -Wextra)
