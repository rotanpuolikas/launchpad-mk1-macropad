cmake_minimum_required(VERSION 3.16)
project(launc-macro C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# ── system dependencies ────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
endif()

# ── sources ────────────────────────────────────────────────────────────────────
add_executable(launc-macro
    src/main.c
    src/midi.c
    src/keys.c
    src/config.c
    src/gif.c
    src/colours.c
    ${inih_SOURCE_DIR}/ini.c
)

target_include_directories(launc-macro PRIVATE
    src
    ${inih_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(launc-macro PRIVATE
        winmm
        user32
        shell32
        Threads::Threads
        m
    )
    target_compile_options(launc-macro PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
else()
    target_link_libraries(launc-macro PRIVATE
        ${ALSA_LIBRARIES}
        Threads::Threads
        m
    )
    target_compile_options(launc-macro PRIVATE
        ${ALSA_CFLAGS_OTHER}
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

# GIF support via stb_image (always on when stb is fetched)
target_compile_definitions(launc-macro PRIVATE HAVE_GIF=1 INI_MAX_LINE=512)

# ── install ────────────────────────────────────────────────────────────────────
install(TARGETS launc-macro DESTINATION bin)
