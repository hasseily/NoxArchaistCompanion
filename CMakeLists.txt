cmake_minimum_required(VERSION 3.20)
project(NoxArchaistCompanion VERSION 2.2.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

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
# Phase 3: stub main that just brings Gamelink up and tears it back down so
# we can prove the build is green. Phases 4+ replace the stub with the SDL3
# entry point and link the emulator + post-processor static libs.
# ---------------------------------------------------------------------------
add_executable(nac
    src/main.cpp
    ${GAMELINK_SOURCES}
)

target_include_directories(nac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/NoxArchaistCompanion
)

if(WIN32)
    target_link_libraries(nac PRIVATE)
elseif(APPLE)
    target_link_libraries(nac PRIVATE pthread)
else()
    target_link_libraries(nac PRIVATE rt pthread)
endif()
