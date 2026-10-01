cmake_minimum_required(VERSION 3.24)

project(EraShift
        VERSION 0.1.0
        DESCRIPTION "Era Shift - a 2D action-adventure built on a shared, mutable timeline"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ERASHIFT_BUILD_TESTS   "Build the automated test suite"            ON)
option(ERASHIFT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"    OFF)
option(ERASHIFT_USE_SYSTEM_DEPS   "Prefer system SDL3 packages over the project-local prefix" ON)
option(ERASHIFT_ENABLE_SANITIZERS "Build with ASan+UBSan (Debug only)"      OFF)
option(ERASHIFT_ENABLE_UBSAN       "Build with UBSan only (Debug only)"      OFF)
option(ERASHIFT_ENABLE_TSAN        "Build with ThreadSanitizer (Debug only)" OFF)

# ---------------------------------------------------------------------------
# Global language settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Single output directory keeps `./build/EraShift` valid for every generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------
# Dependency discovery
#
# Era Shift prefers distribution packages. When they are absent (for example on
# a machine without root access) `scripts/fetch_deps.sh` builds the SDL3 family
# into `.deps/install`, and we fall back to that prefix.
# ---------------------------------------------------------------------------
set(ERASHIFT_LOCAL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install")

# The local prefix is appended rather than prepended so that distribution
# packages always win when both are present. Appending it unconditionally also
# means a fresh `cmake --preset <name>` works with no extra flags, which is
# what makes the Debug/Release/ASan/UBSan/TSan presets usable out of the box.
if(EXISTS "${ERASHIFT_LOCAL_PREFIX}")
    if(ERASHIFT_USE_SYSTEM_DEPS)
        list(APPEND CMAKE_PREFIX_PATH "${ERASHIFT_LOCAL_PREFIX}")
    else()
        list(PREPEND CMAKE_PREFIX_PATH "${ERASHIFT_LOCAL_PREFIX}")
    endif()
endif()

find_package(SDL3        CONFIG QUIET)
find_package(SDL3_image  CONFIG QUIET)
find_package(SDL3_ttf    CONFIG QUIET)

if(NOT SDL3_FOUND)
    message(FATAL_ERROR
        "SDL3 was not found.\n"
        "Either install the development packages (e.g. libsdl3-dev) or run\n"
        "  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_deps.sh\n"
        "which builds SDL3 into ${ERASHIFT_LOCAL_PREFIX} without root.")
endif()

message(STATUS "EraShift: SDL3        ${SDL3_VERSION} (${SDL3_DIR})")
message(STATUS "EraShift: SDL3_image  ${SDL3_image_VERSION} (${SDL3_image_DIR})")
message(STATUS "EraShift: SDL3_ttf    ${SDL3_ttf_VERSION} (${SDL3_ttf_DIR})")
message(STATUS "EraShift: SQLite3     ${SQLite3_VERSION} (${SQLite3_INCLUDE_DIRS})")

# nlohmann/json is used for configuration and save data. Prefer a system copy,
# otherwise use the single header vendored by scripts/fetch_deps.sh.
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(ERASHIFT_JSON_INCLUDE_DIR nlohmann/json.hpp
              HINTS ${ERASHIFT_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH}
              PATH_SUFFIXES include)
    if(NOT ERASHIFT_JSON_INCLUDE_DIR)
        message(FATAL_ERROR
            "nlohmann/json.hpp not found. Install nlohmann-json3-dev or run scripts/fetch_deps.sh")
    endif()
    add_library(erashift_json INTERFACE)
    target_include_directories(erashift_json SYSTEM INTERFACE "${ERASHIFT_JSON_INCLUDE_DIR}")
    add_library(erashift::json ALIAS erashift_json)
endif()

# SQLite backs the progression database: unlocked levels, per-level completion,
# tutorial progress and checkpoints. Offline and local, which is the whole point.
#
# Found as a library rather than vendored: it is present on every distribution
# this project targets, and the amalgamation `scripts/fetch_deps.sh` can drop in
# covers the case where it is not.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_path(ERASHIFT_SQLITE_INCLUDE_DIR sqlite3.h
              HINTS ${CMAKE_PREFIX_PATH}
              PATH_SUFFIXES include)
    find_library(ERASHIFT_SQLITE_LIBRARY NAMES sqlite3)
    if(ERASHIFT_SQLITE_INCLUDE_DIR AND ERASHIFT_SQLITE_LIBRARY)
        add_library(erashift_sqlite INTERFACE)
        target_include_directories(erashift_sqlite SYSTEM INTERFACE "${ERASHIFT_SQLITE_INCLUDE_DIR}")
        target_link_libraries(erashift_sqlite INTERFACE "${ERASHIFT_SQLITE_LIBRARY}")
        add_library(SQLite::SQLite3 ALIAS erashift_sqlite)
        set(SQLite3_FOUND TRUE)
        message(STATUS "EraShift: sqlite3 ${ERASHIFT_SQLITE_LIBRARY} (found by hand)")
    endif()
endif()

if(NOT SQLite3_FOUND)
    message(FATAL_ERROR
        "SQLite3 was not found.\n"
        "Either install the development package (e.g. libsqlite3-dev) or run\n"
        "  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_deps.sh\n"
        "which vendors the SQLite amalgamation into ${ERASHIFT_LOCAL_PREFIX}.")
endif()

# ---------------------------------------------------------------------------
# Warning / hardening flags shared by every Era Shift target
# ---------------------------------------------------------------------------
add_library(erashift_project_options INTERFACE)
add_library(erashift::options ALIAS erashift_project_options)

target_compile_options(erashift_project_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Wformat=2
        -Wundef
        -Wdouble-promotion
        -Wnull-dereference
        -Wimplicit-fallthrough
        $<$<BOOL:${ERASHIFT_WARNINGS_AS_ERRORS}>:-Werror>>)

# Sanitizers are mutually exclusive: ASan and TSan cannot be combined, and
# mixing them produces confusing link-time errors rather than useful reports.
set(ERASHIFT_SANITIZER_COUNT 0)
foreach(flag ERASHIFT_ENABLE_SANITIZERS ERASHIFT_ENABLE_UBSAN ERASHIFT_ENABLE_TSAN)
    if(${flag})
        math(EXPR ERASHIFT_SANITIZER_COUNT "${ERASHIFT_SANITIZER_COUNT} + 1")
    endif()
endforeach()

if(ERASHIFT_SANITIZER_COUNT GREATER 1)
    message(FATAL_ERROR
        "Enable at most one of ERASHIFT_ENABLE_SANITIZERS, "
        "ERASHIFT_ENABLE_UBSAN, ERASHIFT_ENABLE_TSAN.")
endif()

if(ERASHIFT_SANITIZER_COUNT EQUAL 1)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "Sanitizers require -DCMAKE_BUILD_TYPE=Debug")
    endif()

    if(ERASHIFT_ENABLE_SANITIZERS)
        set(ERASHIFT_SANITIZERS address,undefined)
    elseif(ERASHIFT_ENABLE_TSAN)
        set(ERASHIFT_SANITIZERS thread)
    else()
        set(ERASHIFT_SANITIZERS undefined)
    endif()

    target_compile_options(erashift_project_options INTERFACE
        -fsanitize=${ERASHIFT_SANITIZERS} -fno-omit-frame-pointer -g)
    target_link_options(erashift_project_options INTERFACE
        -fsanitize=${ERASHIFT_SANITIZERS})
    message(STATUS "EraShift: sanitizers enabled (${ERASHIFT_SANITIZERS})")
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(ERASHIFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS EraShift RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY config/ DESTINATION ${CMAKE_INSTALL_DATADIR}/EraShift/config)
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/EraShift/assets)
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/EraShift/data)
