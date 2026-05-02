cmake_minimum_required(VERSION 3.20)
project(NoxArchaistCompanion VERSION 2.2.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# SDL3: try the system / vcpkg copy first, fall back to FetchContent.
# ---------------------------------------------------------------------------
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found via find_package — fetching from upstream")
    include(FetchContent)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.20
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# nac_emulator: stripped, vendored AppleWin core (Phase 1).
# ---------------------------------------------------------------------------
add_subdirectory(NoxArchaistCompanion/Emulator)

# ---------------------------------------------------------------------------
# Gamelink: platform-neutral protocol layer + one per-platform backend.
# ---------------------------------------------------------------------------
set(GAMELINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/NoxArchaistCompanion/RemoteControl)
set(GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink.cpp)

if(WIN32)
    list(APPEND GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink_win32.cpp)
elseif(NOT APPLE)
    list(APPEND GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink_posix.cpp)
endif()

# ---------------------------------------------------------------------------
# nac executable.
# Phase 4: SDL3 main loop + GL context. The emulator framebuffer is wired
# in during Phase 5 — for now we just open a black window and shut down
# cleanly to prove SDL3 + OpenGL + the emulator static lib all link.
# ---------------------------------------------------------------------------
add_executable(nac WIN32
    src/main.cpp
    src/Renderer.cpp
    ${GAMELINK_SOURCES}
)

target_include_directories(nac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/NoxArchaistCompanion
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nac PRIVATE
    nac_emulator
    SDL3::SDL3
    OpenGL::GL
)

if(APPLE)
    target_link_libraries(nac PRIVATE pthread)
elseif(NOT WIN32)
    target_link_libraries(nac PRIVATE rt pthread)
endif()

# Copy SDL3.dll next to nac.exe on Windows so it can run from the build tree.
if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET nac POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:nac>
    )
endif()
