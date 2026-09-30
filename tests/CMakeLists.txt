# Era Shift - automated tests
#
# Three binaries, split by what they need:
#   unit        - no SDL, no display. The default target for `ctest`.
#   integration - no display either, but exercises several systems together.
#   gameplay    - the whole simulation: era rules, tile collision, the player
#                controller, enemies, the world, level data and saves. No SDL
#                either, which is the reason all of it lives in erashift_core.
#
# All of them link only erashift_core plus doctest, so the suite runs on a
# headless CI machine and in a container.

find_package(Threads REQUIRED)

# doctest: prefer a system package, fall back to the vendored header.
find_path(ERASHIFT_DOCTEST_INCLUDE_DIR doctest/doctest.h
          HINTS ${ERASHIFT_CMAKE_PREFIX_PATH} ${CMAKE_PREFIX_PATH}
          PATH_SUFFIXES include)
if(NOT ERASHIFT_DOCTEST_INCLUDE_DIR)
    find_package(doctest CONFIG QUIET)
    if(doctest_FOUND)
        message(STATUS "EraShift: doctest via CMake package")
    endif()
endif()
if(NOT ERASHIFT_DOCTEST_INCLUDE_DIR AND NOT doctest_FOUND)
    message(FATAL_ERROR
        "doctest not found. Install doctest-dev or run scripts/fetch_deps.sh")
endif()

add_library(erashift_doctest INTERFACE)
if(ERASHIFT_DOCTEST_INCLUDE_DIR)
    target_include_directories(erashift_doctest SYSTEM INTERFACE "${ERASHIFT_DOCTEST_INCLUDE_DIR}")
else()
    target_link_libraries(erashift_doctest INTERFACE doctest::doctest)
endif()

# Declares a doctest binary from a list of sources.
function(erashift_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE
        erashift_core
        erashift_doctest
        ${ARG_LIBS}
    )
    # DOCTEST_CONFIG_USE_STD_HEADERS makes doctest stringify into std::string
    # rather than std::string_view; without it the binary-expression formatter
    # does not compile.
    target_compile_definitions(${name} PRIVATE
        ERASHIFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        DOCTEST_CONFIG_USE_STD_HEADERS
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

# The unit suite links the engine as well as the core: TestBitmapFont exercises
# the built-in font table, which lives next to the renderer because it draws.
# No test opens a window, so this still runs on a headless machine.
erashift_add_test(erashift_tests_unit
    SOURCES
        unit/TestMain.cpp
        unit/TestMath.cpp
        unit/TestLog.cpp
        unit/TestConfig.cpp
        unit/TestTime.cpp
        unit/TestGameLoop.cpp
        unit/TestGameState.cpp
        unit/TestBitmapFont.cpp
        unit/TestUiScale.cpp
        unit/TestMenuList.cpp
    LIBS
        erashift_engine
)

erashift_add_test(erashift_tests_integration
    SOURCES
        integration/TestMain.cpp
        integration/TestStateFlow.cpp
        integration/TestConfigRoundTrip.cpp
)

# The gameplay suite links only erashift_core. If any of it needs SDL the split
# has been broken, and this target failing to configure is the first place it
# shows up.
erashift_add_test(erashift_tests_gameplay
    SOURCES
        gameplay/TestMain.cpp
        gameplay/TestEra.cpp
        gameplay/TestTileMap.cpp
        gameplay/TestActor.cpp
        gameplay/TestPlayer.cpp
        gameplay/TestEnemy.cpp
        gameplay/TestWorld.cpp
        gameplay/TestLevel.cpp
        gameplay/TestSaveGame.cpp
        gameplay/TestSynth.cpp
        gameplay/TestAnimation.cpp
        gameplay/TestParticles.cpp
)
