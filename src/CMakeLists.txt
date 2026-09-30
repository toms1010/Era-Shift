# Era Shift - engine library
#
# Split into two objects libraries:
#   EraShiftCore  - no SDL. Logging, config, timing, state machine, maths, and
#                   the whole gameplay simulation (tile map, actors, player,
#                   enemies, world, levels, saves). Gameplay lives here rather
#                   than with the renderer for one reason: it must be testable
#                   headlessly, and the whole simulation has no reason to know
#                   what a frame is.
#   EraShiftEngine - everything that touches SDL. Window, renderer, input,
#                   resources, and the states that draw the simulation.

add_library(erashift_core STATIC
    EraShift/Core/Log.cpp
    EraShift/Core/Time.cpp
    EraShift/Core/GameLoop.cpp
    EraShift/Core/GameState.cpp
    EraShift/Core/Config.cpp
    EraShift/Core/SignalHandler.cpp
    EraShift/Core/DemoDriver.cpp
    EraShift/Core/SaveGame.cpp
    EraShift/Graphics/Math.cpp
    EraShift/Graphics/Color.cpp

    # Gameplay simulation. No SDL, no renderer, fully headless.
    EraShift/Game/Era.cpp
    EraShift/Game/TileMap.cpp
    EraShift/Game/Actor.cpp
    EraShift/Game/Player.cpp
    EraShift/Game/Enemy.cpp
    EraShift/Game/Level.cpp
    EraShift/Game/World.cpp

    # Presentation that is still pure arithmetic. The synthesiser lives here for
    # the same reason the simulation does: it is fully testable headlessly.
    EraShift/Audio/Synth.cpp
    EraShift/Game/Animation.cpp
    EraShift/Graphics/ParticleSystem.cpp
)

target_include_directories(erashift_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(erashift_core PUBLIC
    ERASHIFT_VERSION="${PROJECT_VERSION}"
    ERASHIFT_NAME="${PROJECT_NAME}"
)

target_link_libraries(erashift_core
    PUBLIC
        erashift::options
    PRIVATE
        erashift::json
)

add_library(erashift_engine STATIC
    EraShift/Graphics/Window.cpp
    EraShift/Graphics/Texture.cpp
    EraShift/Graphics/Renderer2D.cpp
    EraShift/Graphics/ResourceManager.cpp
    EraShift/Graphics/BitmapFont.cpp
    EraShift/Graphics/TextRenderer.cpp

    EraShift/Input/InputManager.cpp

    EraShift/Audio/AudioManager.cpp

    EraShift/Application/EventBus.cpp
    EraShift/Application/Engine.cpp

    EraShift/Debug/PerformanceStats.cpp
    EraShift/Debug/DebugOverlay.cpp

    EraShift/Game/GameApp.cpp
    EraShift/Game/Feedback.cpp
    EraShift/Game/states/MainMenuState.cpp
    EraShift/Game/states/PlayingState.cpp
    EraShift/Game/states/PausedState.cpp
    EraShift/Game/states/ResultState.cpp
)

target_include_directories(erashift_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(erashift_engine
    PUBLIC
        erashift_core
        erashift::options
    PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3_mixer::SDL3_mixer
)

# Threads are used by the logger's mutex and by resource loading workers.
find_package(Threads REQUIRED)
target_link_libraries(erashift_engine PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(EraShift
    EraShift/main.cpp
)

target_link_libraries(EraShift PRIVATE erashift_engine)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(erashift_engine PUBLIC ERASHIFT_DEBUG=1)
endif()
