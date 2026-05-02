# nac_emulator: stripped, vendored AppleWin core.
# Sources are split by Phase 1 sub-commit groups (CPU+memory, video, audio,
# disk + I/O) — the groups grow as each sub-commit lands.

set(EMU_CORE
    StdAfx.cpp
    StrFormat.cpp
    Utilities.cpp
    SynchronousEventManager.cpp
    YamlHelper.cpp
    Log.cpp
    Registry.cpp
    Core.cpp
    FrameBase.cpp
    CmdLine.cpp
    SaveState.cpp
    CPU.cpp
    Memory.cpp
    Card.cpp
    CardManager.cpp
)

set(EMU_VIDEO
    Video.cpp
    NTSC.cpp
    NTSC_CharSet.cpp
    RGBMonitor.cpp
    VidHD.cpp
)

set(EMU_AUDIO
    AY8910.cpp
    Mockingboard.cpp
    MockingboardCardManager.cpp
    SoundCore.cpp
    Speaker.cpp
    SSI263.cpp
    SAM.cpp
    6522.cpp
    6821.cpp
    Riff.cpp
)

# ---------------------------------------------------------------------------
# libyaml — vendored under Emulator/libyaml/, built as a small static lib.
# ---------------------------------------------------------------------------
set(YAML_SOURCES
    libyaml/src/api.c
    libyaml/src/dumper.c
    libyaml/src/emitter.c
    libyaml/src/loader.c
    libyaml/src/parser.c
    libyaml/src/reader.c
    libyaml/src/scanner.c
    libyaml/src/writer.c
)

add_library(yaml STATIC ${YAML_SOURCES})
target_include_directories(yaml PUBLIC libyaml/include)
target_include_directories(yaml PRIVATE libyaml/win32)
target_compile_definitions(yaml PRIVATE HAVE_CONFIG_H YAML_DECLARE_STATIC)

# ---------------------------------------------------------------------------
# zlib — vendored under Emulator/zlib/, used by minizip for ZIP/GZ disk images.
# ---------------------------------------------------------------------------
set(ZLIB_SOURCES
    zlib/adler32.c
    zlib/compress.c
    zlib/crc32.c
    zlib/deflate.c
    zlib/gzclose.c
    zlib/gzlib.c
    zlib/gzread.c
    zlib/gzwrite.c
    zlib/infback.c
    zlib/inffast.c
    zlib/inflate.c
    zlib/inftrees.c
    zlib/trees.c
    zlib/uncompr.c
    zlib/zutil.c
)

add_library(nac_zlib STATIC ${ZLIB_SOURCES})
target_include_directories(nac_zlib PUBLIC zlib)
if(MSVC)
    target_compile_definitions(nac_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# minizip — ZIP archive read/write on top of zlib.
# ---------------------------------------------------------------------------
set(MINIZIP_SOURCES
    minizip/ioapi.c
    minizip/mztools.c
    minizip/unzip.c
    minizip/zip.c
)
if(WIN32)
    list(APPEND MINIZIP_SOURCES minizip/iowin32.c)
endif()

add_library(nac_minizip STATIC ${MINIZIP_SOURCES})
target_include_directories(nac_minizip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nac_minizip PUBLIC nac_zlib)
if(MSVC)
    target_compile_definitions(nac_minizip PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# nac_emulator static lib. Files are added in sub-commit groups.
# ---------------------------------------------------------------------------
add_library(nac_emulator STATIC
    ${EMU_CORE}
    ${EMU_VIDEO}
    ${EMU_AUDIO}
)

target_include_directories(nac_emulator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nac_emulator PUBLIC yaml nac_minizip nac_zlib)

if(WIN32)
    target_link_libraries(nac_emulator PUBLIC
        winmm
        dsound
        dxguid
    )
endif()
