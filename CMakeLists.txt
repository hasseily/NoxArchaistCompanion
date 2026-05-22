cmake_minimum_required(VERSION 3.20)
project(NoxArchaistCompanion VERSION 3.0.2 LANGUAGES C CXX)

# Pre-release suffix appended to NAC_VERSION_STRING in Version.h
# (e.g. "-rc1", "-beta"). Empty string for stable releases. Bumped
# alongside project(VERSION ...) above when cutting a release.
set(NAC_VERSION_SUFFIX "")

# Bake the project version into a generated header that the About
# panel and any other consumer can #include "Version.h" against.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h
    @ONLY)

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
# Dear ImGui (docking branch) — fetched and built as a small static lib
# with the SDL3 + OpenGL3 backends. Used by the sidebar (Phase 7) and any
# future settings UI.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)
# imgui_impl_opengl3 loads GL function pointers itself via its own loader;
# no glad needed.

# ---------------------------------------------------------------------------
# nac_emulator: stripped, vendored AppleWin core (Phase 1).
# ---------------------------------------------------------------------------
add_subdirectory(NoxArchaistCompanion/Emulator)

# ---------------------------------------------------------------------------
# nac_pp: vendored AppleWin pp-branch postprocessor (CRT shaders, presets,
# bezels, ImGui settings panel). Source under Emulator/pp/, third-party
# deps (glad, glm, stb_image, ImGuiFileDialog) under third_party/.
# ---------------------------------------------------------------------------
add_library(nac_pp STATIC
    NoxArchaistCompanion/Emulator/pp/postprocessor.cpp
    NoxArchaistCompanion/Emulator/pp/shader.cpp
    third_party/glad/glad.cpp
    third_party/extras/ImGuiFileDialog.cpp
)
target_include_directories(nac_pp PUBLIC
    NoxArchaistCompanion/Emulator/pp
    third_party
    third_party/stb
)
target_link_libraries(nac_pp PUBLIC SDL3::SDL3 OpenGL::GL imgui)
target_compile_features(nac_pp PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(nac_pp PRIVATE /wd4244 /wd4267 /wd4305 /wd4458)
    target_compile_definitions(nac_pp PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        _USE_MATH_DEFINES   # M_PI / M_PI_2 etc. in <cmath> on MSVC
    )
endif()

# ---------------------------------------------------------------------------
# Gamelink: platform-neutral protocol layer + one per-platform backend.
# ---------------------------------------------------------------------------
set(GAMELINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/NoxArchaistCompanion/RemoteControl)
set(GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink.cpp)

if(WIN32)
    list(APPEND GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink_win32.cpp)
elseif(APPLE)
    # Grid Cartographer doesn't run on macOS — no-op backend keeps the
    # protocol layer linkable while leaving gamelink_up cleared at runtime.
    list(APPEND GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink_disabled.cpp)
else()
    list(APPEND GAMELINK_SOURCES ${GAMELINK_DIR}/Gamelink_posix.cpp)
endif()

# ---------------------------------------------------------------------------
# nac executable. WIN32 keeps the Windows build a GUI app (no console);
# MACOSX_BUNDLE turns the macOS build into nac.app with the Info.plist /
# icon / staged resources hung off it below.
# ---------------------------------------------------------------------------
set(NAC_ICON_FILE ${CMAKE_SOURCE_DIR}/assets/macos/nac.icns)

add_executable(nac WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/Renderer.cpp
    src/Frame.cpp
    src/AudioOutput.cpp
    src/Templates.cpp
    src/RemoteInput.cpp
    src/NoxHacks.cpp
    src/Map.cpp
    src/MemoryViewer.cpp
    src/HGRViewer.cpp
    src/RamSnapshot.cpp
    src/AppleStyle.cpp
    src/Background.cpp
    src/About.cpp
    ${GAMELINK_SOURCES}
    $<$<BOOL:${APPLE}>:${NAC_ICON_FILE}>
)

target_include_directories(nac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/NoxArchaistCompanion
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(nac PRIVATE
    nac_emulator
    nac_pp
    imgui
    SDL3::SDL3
    OpenGL::GL
)

if(APPLE)
    target_link_libraries(nac PRIVATE pthread)
    # Apple deprecated OpenGL in 10.14 in favour of Metal. We're sticking
    # with GL for cross-platform parity — silence the per-call warnings.
    target_compile_definitions(nac    PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_definitions(nac_pp PRIVATE GL_SILENCE_DEPRECATION)
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

# Stage runtime data next to the nac binary so SDL_GetBasePath()-relative
# loads (post-processor presets / shaders / bezels, Apple //e ROMs, sidebar
# profiles, Versions.json) all resolve when launched from the build dir.
# On macOS the bundle owns Contents/Resources/, and SDL_GetBasePath()
# returns Contents/Resources/ — copy the data straight in there.
if(APPLE)
    set(NAC_RESOURCE_DIR $<TARGET_BUNDLE_DIR:nac>/Contents/Resources)
    set_source_files_properties(${NAC_ICON_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(nac PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST              ${CMAKE_SOURCE_DIR}/assets/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME             "Nox Archaist Companion"
        MACOSX_BUNDLE_EXECUTABLE_NAME         nac
        MACOSX_BUNDLE_GUI_IDENTIFIER          com.asseily.NoxArchaistCompanion
        MACOSX_BUNDLE_BUNDLE_VERSION          ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING    ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT               "Copyright © Henri Asseily"
    )
else()
    set(NAC_RESOURCE_DIR $<TARGET_FILE_DIR:nac>)
endif()

# Run on every build, not just nac re-links — a POST_BUILD on the
# nac target wouldn't fire when only a NoxData/* or shader file
# changes, so edits to e.g. maps_index.json would silently fail to
# reach the staged copy. An always-stale custom target avoids that.
add_custom_target(nac_stage_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/pp/assets   ${NAC_RESOURCE_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/pp/presets  ${NAC_RESOURCE_DIR}/presets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/pp/shaders  ${NAC_RESOURCE_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NoxArchaistCompanion/NoxData    ${NAC_RESOURCE_DIR}/NoxData
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NoxArchaistCompanion/Emulator/Resources  ${NAC_RESOURCE_DIR}/Resources
    COMMENT "Staging runtime data into ${NAC_RESOURCE_DIR}"
    VERBATIM
)
# Must run after nac is built — on macOS the bundle's Contents/
# Resources/ doesn't exist until then.
add_dependencies(nac_stage_data nac)
