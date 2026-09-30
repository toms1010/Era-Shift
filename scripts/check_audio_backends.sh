#!/usr/bin/env bash
#
# Fails loudly when the SDL that was just built cannot talk to a real audio
# device on Linux.
#
# This exists because "SDL built successfully" and "SDL can make a noise" are
# different facts. SDL compiles whichever backends it found headers for at build
# time, and a build with neither PulseAudio nor ALSA available still links and
# still runs - it just opens a device that goes nowhere. Nothing in a normal build
# says so. A machine can be in exactly that state and look healthy.
#
# `disk` and `dummy` are not real outputs: `disk` writes a file, `dummy` discards
# samples. Neither is evidence that a player would hear anything, so a build with
# only those is reported as a failure.
#
# Usage:
#   scripts/check_audio_backends.sh [path/to/EraShift]
#
# Exits 0 if at least one real Linux backend is compiled in, 1 otherwise.

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-}"

if [[ -z "${binary}" ]]; then
    # Prefer a release build if there is one, otherwise the first one found.
    for candidate in \
        "${repo_root}/build/release/EraShift" \
        "${repo_root}/build/relwithdebinfo/EraShift" \
        "${repo_root}/build/debug/EraShift" \
        "${repo_root}/build/EraShift"
    do
        if [[ -x "${candidate}" ]]; then
            binary="${candidate}"
            break
        fi
    done
fi

if [[ -z "${binary}" || ! -x "${binary}" ]]; then
    echo "error: no EraShift binary found; build first or pass one as an argument" >&2
    exit 1
fi

echo "checking audio backends in ${binary}"

# `--audio-test` prints the compiled-in driver list and then exits. It is used
# rather than `--frames 1` because it is the mode that reports the driver list,
# and it does not need a window.
output="$("${binary}" --audio-test 2>&1 || true)"

drivers_line="$(printf '%s\n' "${output}" | sed -n 's/.*available_drivers=//p' | head -1)"

if [[ -z "${drivers_line}" ]]; then
    echo "error: the binary did not report its SDL audio drivers." >&2
    echo "       Expected an 'available_drivers=' line from --audio-test." >&2
    exit 1
fi

echo "  compiled-in drivers: ${drivers_line}"

real_backends=""
for driver in ${drivers_line//,/ }; do
    case "${driver}" in
        pulseaudio|pulse|alsa|pipewire|jack|sndio)
            real_backends="${real_backends} ${driver}"
            ;;
        disk|dummy|*)
            # disk and dummy produce no physical output; anything else unknown is
            # not counted either, because counting it would defeat the check.
            ;;
    esac
done

if [[ -z "${real_backends// /}" ]]; then
    echo "error: SDL has no real audio backend compiled in (only: ${drivers_line})." >&2
    echo "       The game will run and every log line will look healthy, but no" >&2
    echo "       player will hear anything. Install libpulse-dev or libasound2-dev" >&2
    echo "       and re-run ./scripts/fetch_deps.sh --clean" >&2
    exit 1
fi

echo "  real backends available:${real_backends}"
echo "OK"
exit 0
