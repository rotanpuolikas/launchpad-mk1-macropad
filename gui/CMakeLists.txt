cmake_minimum_required(VERSION 3.16)
project(launc-macro-gui C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# All source files live one level up
set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

# ── vendored / fetched dependencies ───────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(inih
    GIT_REPOSITORY https://github.com/benhoyt/inih.git
    GIT_TAG        r58
)
FetchContent_MakeAvailable(inih)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(USE_AUDIO      OFF CACHE BOOL "" FORCE)   # not needed, avoids miniaudio build errors
FetchContent_MakeAvailable(raylib)

# raygui is a single-header library — only the source directory is needed
FetchContent_Declare(raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG        4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raygui)

# ── system dependencies ────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(LIBPULSE libpulse-simple)
    if(LIBPULSE_FOUND)
        message(STATUS "PulseAudio found — equalizer enabled")
    else()
        message(STATUS "PulseAudio (libpulse-simple) not found — equalizer disabled")
    endif()
endif()

# ── sources ────────────────────────────────────────────────────────────────────
set(GUI_SOURCES
    ${SRC}/midi.c
    ${SRC}/keys.c
    ${SRC}/config.c
    ${SRC}/gif.c
    ${SRC}/colours.c
    ${SRC}/audio.c
    ${SRC}/gui/app.c
    ${SRC}/gui/theme.c
    ${SRC}/gui/main_gui.c
    ${inih_SOURCE_DIR}/ini.c
)

# Copy icon.png to the build directory so the app can find it at runtime
set(ICON_PNG "${CMAKE_CURRENT_LIST_DIR}/../assets/icon.png")
if(EXISTS "${ICON_PNG}")
    configure_file("${ICON_PNG}" "${CMAKE_CURRENT_BINARY_DIR}/icon.png" COPYONLY)
endif()

# On Windows: embed icon.ico into the .exe via a resource script
# Also suppress the console window via WIN32 subsystem
if(WIN32)
    set(ICON_ICO "${CMAKE_CURRENT_LIST_DIR}/../assets/icon.ico")
    if(EXISTS "${ICON_ICO}")
        list(APPEND GUI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/app.rc")
    endif()
    add_executable(launc-macro-gui WIN32 ${GUI_SOURCES})
else()
    add_executable(launc-macro-gui ${GUI_SOURCES})
endif()

target_include_directories(launc-macro-gui PRIVATE
    ${SRC}
    ${inih_SOURCE_DIR}
    ${stb_SOURCE_DIR}
    ${raygui_SOURCE_DIR}/src   # provides raygui.h
)

target_compile_definitions(launc-macro-gui PRIVATE HAVE_GIF=1 INI_MAX_LINE=512)

# Equalizer via PulseAudio (Linux only)
if(LIBPULSE_FOUND AND NOT WIN32)
    target_compile_definitions(launc-macro-gui PRIVATE HAVE_PULSEAUDIO=1)
    target_link_libraries(launc-macro-gui PRIVATE ${LIBPULSE_LIBRARIES})
    target_include_directories(launc-macro-gui PRIVATE ${LIBPULSE_INCLUDE_DIRS})
    target_compile_options(launc-macro-gui PRIVATE ${LIBPULSE_CFLAGS_OTHER})
endif()

# Prevent Win32 GDI/user headers from declaring Rectangle/CloseWindow/
# ShowCursor/DrawText etc., which conflict with raylib's API of the same name.
# Applied only to files that include raylib/raygui headers, because NOUSER
# also strips INPUT/SendInput which keys.c needs.
if(WIN32)
    set_source_files_properties(
        ${SRC}/gui/main_gui.c
        ${SRC}/gui/theme.c
        PROPERTIES COMPILE_DEFINITIONS "NOGDI;NOUSER"
    )
endif()

# gif.c defines the full stb_image / stb_image_resize2 implementations.
# raylib's rtextures.c does the same internally. Make gif.c's copies
# file-scoped (static) so the linker sees only one set of public symbols.
set_source_files_properties(${SRC}/gif.c PROPERTIES
    COMPILE_DEFINITIONS "STB_IMAGE_STATIC;STB_IMAGE_RESIZE_STATIC"
)

# ── platform-specific linking ─────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(launc-macro-gui PRIVATE
        raylib
        winmm
        user32
        shell32
        Threads::Threads
        m
    )
    target_compile_options(launc-macro-gui PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
else()
    target_link_libraries(launc-macro-gui PRIVATE
        raylib
        ${ALSA_LIBRARIES}
        Threads::Threads
        m
    )
    target_compile_options(launc-macro-gui PRIVATE
        ${ALSA_CFLAGS_OTHER}
        -Wall -Wextra -Wno-unused-parameter
    )
    target_include_directories(launc-macro-gui PRIVATE ${ALSA_INCLUDE_DIRS})
endif()

install(TARGETS launc-macro-gui DESTINATION bin)
