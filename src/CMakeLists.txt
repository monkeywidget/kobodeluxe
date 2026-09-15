set(sources
	gamestate.cpp
	map.cpp
	radar.cpp
	cfgform.cpp
	dashboard.cpp
	enemy.cpp
	game.cpp
	logger.c
	pfile.cpp
	score.cpp
	sound.cpp
	filemap.cpp
	myship.cpp
	random.cpp
	cfgparse.cpp
	gamectl.cpp
	kobo.cpp
	manage.cpp
	prefs.cpp
	screen.cpp
	states.cpp
	enemies.cpp
	form.cpp
	options.cpp
	scenes.cpp
	eel/e_builtin.c
	eel/e_getargs.c
	eel/e_lexer.c
	eel/e_path.c
	eel/e_register.c
	eel/e_script.c
	eel/e_symtab.c
	eel/e_util.c
	eel/eel.c
	graphics/cs.c
	graphics/display.cpp
	graphics/filters.c
	graphics/gfxengine.cpp
	graphics/kobo_sdl12.c
	graphics/region.c
	graphics/sofont.cpp
	graphics/sprite.c
	graphics/toolkit.cpp
	graphics/vidmodes.c
	graphics/window.cpp
	sound/a_agw.c
	sound/a_bus.c
	sound/a_channel.c
	sound/a_commands.c
	sound/a_control.c
	sound/a_delay.c
	sound/a_events.c
	sound/a_filters.c
	sound/a_globals.c
	sound/a_limiter.c
	sound/a_midi.c
	sound/a_midicon.c
	sound/a_midifile.c
	sound/a_midisock.c
	sound/a_patch.c
	sound/a_pitch.c
	sound/a_plugin.c
	sound/a_sequencer.c
	sound/a_struct.c
	sound/a_voice.c
	sound/a_wave.c
	sound/a_wca.c
	sound/audio.c
	sound/audiodev.c
	sound/sfifo.c
)

include(FindPkgConfig)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckCSourceCompiles)

include_directories(${KOBODELUXE_SOURCE_DIR})
include_directories(${KOBODELUXE_SOURCE_DIR}/src)
include_directories(${KOBODELUXE_SOURCE_DIR}/src/graphics)
include_directories(${KOBODELUXE_SOURCE_DIR}/src/eel)
include_directories(${KOBODELUXE_SOURCE_DIR}/src/sound)
include_directories(${KOBODELUXE_SOURCE_DIR}/data/sfx)
include_directories(${CMAKE_BINARY_DIR}/include)

set(KOBO_EXE_SUFFIX "")

# SDL 2. IMPORTED_TARGET is what makes this portable: PkgConfig::SDL2 carries
# absolute library paths, whereas the bare *_LIBRARIES variables give plain
# names like "SDL2" that the linker only resolves if the library happens to sit
# in a default search path. That works on Linux and fails on Homebrew with
# "ld: library 'SDL2' not found", and it would fail again when cross-compiling
# for Android.
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2IMAGE REQUIRED IMPORTED_TARGET SDL2_image>=2.0.0)

# Audiality 2 is deliberately NOT a dependency. Kobo Deluxe carries its own
# self-contained mixer and synth in src/sound/, sitting on SDL's audio
# callback, so there is no external audio library to cross-compile for the
# Android target. (Kobo Redux is the tree that needs A2.)

set(RES_FILES "")
if(MINGW)
	set(RES_FILES "${KOBODELUXE_SOURCE_DIR}/icons/windows/kobodl.rc")
	set(CMAKE_RC_COMPILER_INIT windres)
	ENABLE_LANGUAGE(RC)
	SET(CMAKE_RC_COMPILE_OBJECT
		"<CMAKE_RC_COMPILER> <FLAGS> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
endif(MINGW)

add_executable(kobodl WIN32 MACOSX_BUNDLE ${sources} ${RES_FILES})
set_target_properties(kobodl PROPERTIES OUTPUT_NAME
	"kobodl${KOBO_EXE_SUFFIX}")

target_link_libraries(kobodl
	PkgConfig::SDL2
	PkgConfig::SDL2IMAGE
	${KOBO_EXTRA_LIBRARIES})

# Record where the non-system libraries actually live.
#
# SDL2_image is installed outside the default loader paths (it has to be built
# from source on macOS), and it identifies itself as @rpath/libSDL2_image-...
# With no RPATH recorded, the executable links but dies at startup with
# "Library not loaded ... no LC_RPATH's found" -- a failure that does not
# appear until the moment you try to run the game.
set_target_properties(kobodl PROPERTIES
	INSTALL_RPATH_USE_LINK_PATH TRUE
	BUILD_WITH_INSTALL_RPATH TRUE
	INSTALL_RPATH "${SDL2IMAGE_LIBRARY_DIRS};${SDL2_LIBRARY_DIRS}")

if(WIN32)
	target_link_libraries(kobodl winmm dxguid)
endif(WIN32)

# Release build: full optimization, no debug features, no debug info
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Maintainer build: No optimizations, lots of warnings, fail on warnings
set(f "-O1 -g -DDEBUG -Wall -Wwrite-strings -Wcast-align")
set(f "${f} -Waggregate-return")
set(f "${f} -fno-builtin -Wshadow")
set(f "${f} -Wmissing-declarations -Wdisabled-optimization")
set(fc "${f} -Wbad-function-cast -Wstrict-prototypes")
set(fc "${f} -Wdeclaration-after-statement -Wmissing-prototypes")
set(CMAKE_C_FLAGS_MAINTAINER "${fc} -Werror")
set(CMAKE_CXX_FLAGS_MAINTAINER "${f} -Werror")

# Debug build: Maintainer + extra debug features, don't fail on warnings
set(CMAKE_C_FLAGS_DEBUG ${fc})
set(CMAKE_CXX_FLAGS_DEBUG ${f})

set(CMAKE_EXTRA_INCLUDE_FILES stdio.h)
check_function_exists(snprintf		KOBO_HAVE_SNPRINTF)
check_function_exists(_snprintf		KOBO_HAVE__SNPRINTF)
check_function_exists(vsnprintf		KOBO_HAVE_VSNPRINTF)
check_function_exists(_vsnprintf	KOBO_HAVE__VSNPRINTF)

set(CMAKE_EXTRA_INCLUDE_FILES unistd.h)
check_function_exists(getegid		KOBO_HAVE_GETEGID)
check_function_exists(setgid		KOBO_HAVE_SETGID)

set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h sys/stat.h unistd.h)
check_function_exists(stat		KOBO_HAVE_STAT)
check_function_exists(lstat		KOBO_HAVE_LSTAT)

set(CMAKE_EXTRA_INCLUDE_FILES)

if(NOT WIN32)
	set(KOBO_DATADIR "${CMAKE_INSTALL_PREFIX}/share/${KOBO_PACKAGE_NAME}")
	set(KOBO_USERDIR "HOME>>.${KOBO_PACKAGE_NAME}")
	set(KOBO_SYSCONFDIR "/etc")
endif(NOT WIN32)
set(KOBO_CONFIGFILE "${KOBO_PACKAGE_NAME}.cfg")

CHECK_C_SOURCE_COMPILES(
	"#include <sys/types.h>
	 #include <signal.h>
	 int main(void) {
	 	return *(signal(0,0))(0) == 1;
	 }"
	RETSIGTYPE_INT
)
if(RETSIGTYPE_INT)
	set(RETSIGTYPE int)
else(RETSIGTYPE_INT)
	set(RETSIGTYPE void)
endif(RETSIGTYPE_INT)
MESSAGE(STATUS "Signal handler return type: ${RETSIGTYPE}")

set(KOBODELUXE_AUTO_MESSAGE
	"This file is automatically generated. Do not edit!")
configure_file(${CMAKE_SOURCE_DIR}/buildconfig.h.cmake
	${CMAKE_BINARY_DIR}/include/buildconfig.h)

install(TARGETS kobodl DESTINATION "${KOBO_BIN_DIR}")
