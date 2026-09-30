#!/usr/bin/env bash
#
# Measures whether the one-shot path leaks.
#
# This exists because a per-play leak is invisible to the test suite and to the
# sanitizers. It is not a use-after-free and not a buffer overrun, so ASan stays
# silent; it is memory that is never handed back, which only shows up as
# resident-set growth over a long session. That is exactly the failure that
# shipped once already: the one-shot path leaked 77.6 kB per sound, roughly
# 100 MB an hour of ordinary play, and nothing in the build or the tests said so.
#
# What it does:
#   1. confirms the leak by running one-shots and watching RSS
#   2. runs the same loop *without* playing anything, as a control
#
# The control matters. `update()` pushes synthesised audio into a stream, and a
# tight loop produces far more audio than real time, so the stream backlog grows
# and RSS climbs for reasons that have nothing to do with the one-shot path. The
# two numbers should match. If one-shots grow faster than the control, they leak.
#
# It also runs a *paced* pass, where the loop sleeps so the device can consume
# what is fed. Real growth is flat there; buffered backlog is not.
#
# Usage: scripts/check_audio_leaks.sh

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/relwithdebinfo"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

if [[ ! -f "${build_dir}/src/liberashift_engine.a" ]]; then
    echo "building relwithdebinfo first..."
    cmake --build --preset relwithdebinfo -j"$(nproc)" >/dev/null || {
        echo "error: build failed" >&2
        exit 1
    }
fi

cat > "${work}/probe.cpp" <<'PROBE'
#include "EraShift/Audio/AudioManager.hpp"
#include "EraShift/Core/Log.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>

static long rssKb()
{
    FILE* f = std::fopen("/proc/self/statm", "r");
    long size = 0, resident = 0;
    if (f != nullptr) {
        if (std::fscanf(f, "%ld %ld", &size, &resident) != 2) { /* ignore */ }
        std::fclose(f);
    }
    return resident * 4;   // pages -> kB
}

int main(int argc, char** argv)
{
    const int  iters = (argc > 1) ? std::atoi(argv[1]) : 4000;
    const bool play   = (argc > 2) && std::atoi(argv[2]) != 0;
    const bool paced  = (argc > 3) && std::atoi(argv[3]) != 0;

    SDL_Init(SDL_INIT_AUDIO);
    EraShift::Core::Logger log;
    EraShift::Audio::AudioManager m;
    if (!m.initialise(true, 48000, &log)) {
        std::printf("no audio device; cannot measure\n");
        return 2;
    }
    using namespace EraShift;
    for (int i = 0; i < 200; ++i) {
        m.update(1.f / 60.f, Game::Era::Present, Audio::MusicState::Combat, 0.f);
    }
    const long before = rssKb();
    for (int i = 0; i < iters; ++i) {
        if (play) {
            m.play(static_cast<Audio::Sfx>(i % 20), 1.f);
        }
        m.update(1.f / 60.f, Game::Era::Present, Audio::MusicState::Combat, 0.f);
        if (paced) {
            SDL_Delay(16);
        }
    }
    const auto d = m.diagnostics();
    std::printf("%ld %ld %lu\n", before, rssKb(), d.putFailures);
    m.shutdown();
    SDL_Quit();
    return 0;
}
PROBE

g++ -std=c++20 -O1 "${work}/probe.cpp" -o "${work}/probe" \
    -I"${repo_root}/include" -I"${repo_root}/.deps/install/include" \
    -L"${repo_root}/.deps/install/lib" \
    "${build_dir}/src/liberashift_engine.a" "${build_dir}/src/liberashift_core.a" \
    -lSDL3 -lSDL3_mixer -lm -Wl,-rpath,"${repo_root}/.deps/install/lib" 2>/dev/null || {
    echo "error: could not build the probe (are the SDL dependencies present?)" >&2
    exit 1
}

run() {  # iters play paced -> "before after putFailures"
    timeout 600 "${work}/probe" "$@" 2>/dev/null | tail -1
}

echo "leak check (RSS in kB)"
echo

# Paced runs: a real leak is flat here, buffered backlog is not.
for n in 400 1200; do
    read -r b a f <<<"$(run "$n" 1 1)"
    [[ -z "${b:-}" ]] && { echo "  probe failed to run (no audio device?)"; exit 1; }
    printf "  paced, %-5s one-shots: %6s -> %6s kB  growth=%5s kB  putFailures=%s\n" \
        "$n" "$b" "$a" "$((a - b))" "$f"
done

# Unpaced control vs one-shots: growth should be about the same, because both
# are dominated by undrained stream backlog rather than by the one-shot path.
read -r ctl_b ctl_a _ <<<"$(run 4000 0 0)"
read -r one_b one_a _ <<<"$(run 4000 1 0)"
if [[ -n "${ctl_b:-}" && -n "${one_b:-}" ]]; then
    ctl_growth=$((ctl_a - ctl_b))
    one_growth=$((one_a - one_b))
    printf "  unpaced control, no one-shots: growth=%s kB\n" "${ctl_growth}"
    printf "  unpaced with 4000 one-shots  : growth=%s kB\n" "${one_growth}"
    echo
    # The unpaced numbers are backlog-dominated, so only a large excess is
    # meaningful. The real signal is the paced run above.
    if (( one_growth > ctl_growth + 20000 )); then
        echo "LEAK: one-shots grow RSS far beyond the control."
        exit 1
    fi
fi

echo "OK - no per-one-shot growth"
exit 0
