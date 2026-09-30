// Entry point for the input/event-pump suite.
//
// Separate from the other three binaries because this is the only one that
// links `erashift_engine`: `EventPump` is the boundary where `SDL_Event` becomes
// the game's own types, so it cannot live in `erashift_core`. It still needs no
// display, because `EventPump::pump` takes the events as an argument rather than
// pulling them from SDL itself.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
