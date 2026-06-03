# ============================================================================
#  InvaderX — top-level CMakeLists.txt
#
#  Targets produced:
#      invaderx_core           static library, all production .cpp except main
#      invaderx                main executable (links core + SDL2)
#      test_cpu                CPU + flag tests             (560 assertions)
#      test_loader             ROM loader + memory tests    (185 assertions)
#      test_hardware           shift register + scheduler   (56 assertions)
#      test_renderer           VRAM rotation tests          (1223 assertions)
#      test_integration        full stack end-to-end        (26 assertions)
#
#  Build (Windows, MinGW-w64, MSYS2 environment):
#      mkdir build && cd build
#      cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
#      cmake --build .
#
#  Build (Windows, regular MinGW, no MSYS2):
#      mkdir build && cd build
#      cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
#      mingw32-make
#
#  Run:                              .\invaderx.exe ..\roms
#  Run with MAME-ordered dumps:      .\invaderx.exe ..\roms --mame
#  Run tests:                        ctest --output-on-failure
#
#  Override SDL2 location:           cmake -DSDL2_ROOT="D:/libs/SDL2" ..
#  Skip building tests:              cmake -DBUILD_TESTS=OFF ..
# ============================================================================

cmake_minimum_required(VERSION 3.15)

project(InvaderX
    VERSION    1.0.0
    DESCRIPTION "Intel 8080 Space Invaders arcade emulator"
    LANGUAGES  CXX)

# ----------------------------------------------------------------------------
#  Options
# ----------------------------------------------------------------------------
option(BUILD_TESTS "Build the five test harnesses" ON)
option(STRICT_WARNINGS "Compile with -Wall -Wextra -Wpedantic" ON)

# Default build type when none is specified (single-config generators only).
# Release gives us the optimized cycle-counting loop without surprises.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ----------------------------------------------------------------------------
#  Language standard
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # use -std=c++17, not -std=gnu++17

# Output binaries directly under build/ (no Debug/ Release/ subdir on
# multi-config generators either — keeps the "where's the exe?" question
# simple for first-time users).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ----------------------------------------------------------------------------
#  SDL2 location
#
#  The brief specifies SDL2 at C:\SDL2 — make that the default. Users can
#  override on the command line:  cmake -DSDL2_ROOT="D:/libs/SDL2" ..
#
#  On Linux/macOS, fall back to pkg-config so this CMakeLists.txt can
#  drive the host-side regression runs as well as the Windows build.
# ----------------------------------------------------------------------------
if(WIN32)
    set(SDL2_ROOT "C:/SDL2" CACHE PATH "Root of the SDL2 development distribution")

    # ------------------------------------------------------------------------
    #  The official "SDL2-devel-x.y.z-mingw" package does NOT put include/ and
    #  lib/ at its top level. It contains architecture subfolders:
    #      <root>/x86_64-w64-mingw32/   (64-bit)
    #      <root>/i686-w64-mingw32/     (32-bit)
    #  ...each with its own include/SDL2/SDL.h, lib/libSDL2.a, bin/SDL2.dll.
    #
    #  We auto-detect this. If SDL2_ROOT already points at a folder that has
    #  include/ directly (a "flattened" install), we use it as-is. Otherwise
    #  we descend into the architecture subfolder matching the pointer size.
    # ------------------------------------------------------------------------
    set(_sdl2_base "${SDL2_ROOT}")

    if(NOT EXISTS "${_sdl2_base}/include")
        # Pick the arch subfolder by compiler pointer width.
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_sdl2_arch "x86_64-w64-mingw32")
        else()
            set(_sdl2_arch "i686-w64-mingw32")
        endif()
        if(EXISTS "${SDL2_ROOT}/${_sdl2_arch}/include")
            set(_sdl2_base "${SDL2_ROOT}/${_sdl2_arch}")
            message(STATUS "SDL2: using architecture subfolder ${_sdl2_arch}")
        endif()
    endif()

    # ------------------------------------------------------------------------
    #  Locate SDL.h. The MinGW package nests it as include/SDL2/SDL.h; some
    #  flattened installs use include/SDL.h. Support both. The variable
    #  SDL2_INCLUDE_DIRS is set to whatever directory makes  #include <SDL.h>
    #  resolve (so we point at the folder CONTAINING SDL.h).
    # ------------------------------------------------------------------------
    if(EXISTS "${_sdl2_base}/include/SDL2/SDL.h")
        set(SDL2_INCLUDE_DIRS "${_sdl2_base}/include/SDL2")
    elseif(EXISTS "${_sdl2_base}/include/SDL.h")
        set(SDL2_INCLUDE_DIRS "${_sdl2_base}/include")
    else()
        message(FATAL_ERROR
            "Cannot find SDL.h under ${SDL2_ROOT}.\n"
            "Looked in:\n"
            "  ${_sdl2_base}/include/SDL2/SDL.h\n"
            "  ${_sdl2_base}/include/SDL.h\n"
            "Download 'SDL2-devel-<ver>-mingw' from libsdl.org and extract to\n"
            "C:\\SDL2 (or pass -DSDL2_ROOT=<path>). The 64-bit headers live in\n"
            "  <root>/x86_64-w64-mingw32/include/SDL2/SDL.h")
    endif()

    set(SDL2_LIB_DIR "${_sdl2_base}/lib")

    # ------------------------------------------------------------------------
    #  Locate SDL2.dll. The MinGW package ships it in bin/; some installs put
    #  it in lib/. Check both so the post-build copy step finds it.
    # ------------------------------------------------------------------------
    if(EXISTS "${_sdl2_base}/bin/SDL2.dll")
        set(SDL2_DLL_PATH "${_sdl2_base}/bin/SDL2.dll")
    elseif(EXISTS "${SDL2_LIB_DIR}/SDL2.dll")
        set(SDL2_DLL_PATH "${SDL2_LIB_DIR}/SDL2.dll")
    else()
        set(SDL2_DLL_PATH "${_sdl2_base}/bin/SDL2.dll")  # best guess; warned later
    endif()

    # IMPORTANT MinGW link order:  mingw32 -> SDL2main -> SDL2
    #   • mingw32 provides _WinMain entry-point machinery
    #   • SDL2main wraps the user's main() into a WinMain-compatible call
    #   • SDL2 itself comes last; it depends on the two above
    # Any other order produces "undefined reference to WinMain@16" errors.
    set(SDL2_LIBRARIES mingw32 SDL2main SDL2)
else()
    # Non-Windows hosts: use pkg-config. Mostly here so the test suites can
    # be built and run on Linux/CI without a Windows VM.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# ----------------------------------------------------------------------------
#  Warning flags
#  Match the exact flags every sandbox compile used (-Wall -Wextra -Wpedantic).
#  Tests still build clean under these — no exceptions, no -Wno-... knobs.
# ----------------------------------------------------------------------------
if(STRICT_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic)
    elseif(MSVC)
        add_compile_options(/W4)
    endif()
endif()

# ----------------------------------------------------------------------------
#  Source lists  (explicit — never glob)
#  Globbing breaks incremental builds because CMake doesn't notice new
#  files until you rerun `cmake ..`. Explicit lists fail loudly when
#  someone forgets to add a new file, which is the desired behavior.
# ----------------------------------------------------------------------------
set(CORE_SOURCES
    src/cpu.cpp
    src/disassembler.cpp
    src/hardware.cpp
    src/renderer.cpp
    src/rom_loader.cpp
    src/sdl_frontend.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

# ----------------------------------------------------------------------------
#  invaderx_core — shared static library
#  Compiling each .cpp once and linking the .a into every executable is
#  several times faster than recompiling per-target. Also guarantees that
#  invaderx and the test binaries see byte-identical core code.
# ----------------------------------------------------------------------------
add_library(invaderx_core STATIC ${CORE_SOURCES})

target_include_directories(invaderx_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
)

# sdl_frontend.cpp inside core uses SDL2 symbols at compile time, but the
# library itself doesn't LINK SDL2 — that's the executable's job. We do
# need the include path though, which is already PUBLIC above.

# ----------------------------------------------------------------------------
#  invaderx — the main executable
# ----------------------------------------------------------------------------
add_executable(invaderx ${MAIN_SOURCES})

target_link_libraries(invaderx
    PRIVATE
        invaderx_core
        ${SDL2_LIBRARIES}
)

if(WIN32)
    target_link_directories(invaderx PRIVATE ${SDL2_LIB_DIR})

    # ------- Post-build: copy SDL2.dll next to invaderx.exe -------
    # Without this, users get "0xc000007b" on first run because Windows
    # cannot find SDL2.dll. cmake -E copy_if_different keeps incremental
    # builds fast — it skips when the file is already up to date.
    if(EXISTS "${SDL2_DLL_PATH}")
        add_custom_command(TARGET invaderx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DLL_PATH}"
                    "$<TARGET_FILE_DIR:invaderx>/SDL2.dll"
            COMMENT "Copying SDL2.dll next to invaderx.exe")
    else()
        message(WARNING
            "SDL2.dll not found at ${SDL2_DLL_PATH}; "
            "you may need to copy it manually next to invaderx.exe before running.")
    endif()

    # MinGW console subsystem — keeps the printf banner / boot disassembly
    # visible. (Without this, SDL2main's WinMain wrapper would create a
    # GUI subsystem app and the user would never see the boot diagnostic.)
    set_target_properties(invaderx PROPERTIES
        LINK_FLAGS "-Wl,-subsystem,console")
endif()

# ----------------------------------------------------------------------------
#  Tests
# ----------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    # Each test is a standalone executable, linked against invaderx_core.
    # Tests don't need SDL2 — the renderer is SDL-free, and the integration
    # test runs headless. They DO transitively get SDL2 includes from
    # invaderx_core's PUBLIC include dirs, which is harmless.
    set(TEST_NAMES
        test_cpu
        test_loader
        test_hardware
        test_renderer
        test_integration
    )

    foreach(test_name IN LISTS TEST_NAMES)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE invaderx_core)

        # Some tests (test_renderer, test_integration) construct Display
        # objects but never call SDL — so they don't need SDL2 linked.
        # Adding the lib dir lets test_integration verify SDL2 symbols
        # would resolve if needed, without forcing the link.

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# ----------------------------------------------------------------------------
#  Configuration summary  (printed at the end of every cmake configure run)
# ----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "InvaderX configuration summary")
message(STATUS "  build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard   : ${CMAKE_CXX_STANDARD}")
message(STATUS "  compiler       : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(WIN32)
    message(STATUS "  SDL2 root      : ${SDL2_ROOT}")
    message(STATUS "  SDL2 include   : ${SDL2_INCLUDE_DIRS}")
    message(STATUS "  SDL2 lib dir   : ${SDL2_LIB_DIR}")
    message(STATUS "  SDL2 DLL       : ${SDL2_DLL_PATH}")
else()
    message(STATUS "  SDL2 (pkgcfg)  : ${SDL2_VERSION}")
endif()
message(STATUS "  build tests    : ${BUILD_TESTS}")
message(STATUS "  strict warnings: ${STRICT_WARNINGS}")
message(STATUS "")
