#!/usr/bin/env bash
#
# Era Shift - crash reproduction and regression harness.
#
# Re-runs the exact scenario behind the SIGSEGV reported against
# `timeout 20 ./build/EraShift --frames 60` and reports the exit status and
# signal of every variant, so a regression is obvious at a glance.
#
#   ./scripts/reproduce_crash.sh              # build + test + all variants
#   ./scripts/reproduce_crash.sh --no-build   # use the existing binary
#   ./scripts/reproduce_crash.sh --binary build/debug/EraShift
#
# Exit status: 0 when every case behaved, 1 when any case crashed.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

BINARY="build/debug/EraShift"
DO_BUILD=1
SANITIZERS=0

for arg in "$@"; do
    case "${arg}" in
        --no-build)  DO_BUILD=0 ;;
        --binary)    shift; BINARY="$1" ;;
        --sanitize)  SANITIZERS=1 ;;
        -h|--help)   sed -n '2,20p' "$0"; exit 0 ;;
    esac
done

FAILURES=0
RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BOLD=$'\033[1m'; OFF=$'\033[0m'
if [[ ! -t 1 ]]; then RED=""; GREEN=""; YELLOW=""; BOLD=""; OFF=""; fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# describe <status> -> a human readable "exit=0" or "signal 11 (SEGV)".
describe() {
    local status=$1
    if (( status > 128 )); then
        local signal=$(( status - 128 ))
        local name
        case ${signal} in
            2)  name="INT"  ;;
            6)  name="ABRT" ;;
            7)  name="BUS"  ;;
            9)  name="KILL" ;;
            11) name="SEGV" ;;
            15) name="TERM" ;;
            *)  name="?"    ;;
        esac
        printf 'signal %d (%s)' "${signal}" "${name}"
    else
        printf 'exit %d' "${status}"
    fi
}

# run_case <label> <expectation> <command...>
# expectation: "zero" (must exit 0) or "nonzero" (must exit non-zero)
run_case() {
    local label=$1 expect=$2; shift 2

    local output status
    output="$("$@" 2>&1)"
    status=$?

    local verdict="${GREEN}PASS${OFF}"
    if [[ ${expect} == zero && ${status} -ne 0 ]]; then
        verdict="${RED}FAIL${OFF}"; FAILURES=$((FAILURES + 1))
    elif [[ ${expect} == nonzero && ${status} -eq 0 ]]; then
        verdict="${RED}FAIL${OFF}"; FAILURES=$((FAILURES + 1))
    fi

    printf '  %-52s %-22s %s\n' "${label}" "$(describe ${status})" "${verdict}"

    if [[ ${verdict} == "${RED}FAIL${OFF}" ]]; then
        printf '      %s\n' "${output}" | tail -5
    fi
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if (( DO_BUILD )); then
    echo "${BOLD}Building${OFF}"
    cmake --preset debug >/dev/null || { echo "configure failed"; exit 1; }
    cmake --build --preset debug >/dev/null || { echo "build failed"; exit 1; }
fi

if [[ ! -x "${BINARY}" ]]; then
    echo "${RED}binary not found: ${BINARY}${OFF}"
    exit 1
fi

echo
echo "${BOLD}Binary${OFF}  ${BINARY}"
"${BINARY}" --help >/dev/null 2>&1 && echo "  --help works"

# ---------------------------------------------------------------------------
# 1. The reported scenario
# ---------------------------------------------------------------------------
echo
echo "${BOLD}1. Reported scenario${OFF}"
run_case "timeout 20 <bin> --frames 60" zero timeout 20 "${BINARY}" --frames 60
run_case "<bin> --frames 60 (no wrapper)" zero "${BINARY}" --frames 60

# ---------------------------------------------------------------------------
# 2. Frame counts
# ---------------------------------------------------------------------------
echo
echo "${BOLD}2. Frame counts${OFF}"
for n in 1 10 60 300 1000; do
    run_case "--frames ${n}" zero "${BINARY}" --headless --frames "${n}"
done

# ---------------------------------------------------------------------------
# 3. Signal handling: the game must shut down, not hang
# ---------------------------------------------------------------------------
echo
echo "${BOLD}3. Signal handling${OFF}"
for sig in TERM INT HUP; do
    "${BINARY}" --headless >/dev/null 2>&1 &
    local_pid=$!
    sleep 1.5
    kill -"${sig}" "${local_pid}" 2>/dev/null
    # Allow 5 seconds for a clean exit, then fail loudly rather than hanging.
    for _ in $(seq 1 50); do
        kill -0 "${local_pid}" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "${local_pid}" 2>/dev/null; then
        printf '  %-52s %-22s %s\n' "SIG${sig} -> clean shutdown" "still running" "${RED}FAIL${OFF}"
        kill -9 "${local_pid}" 2>/dev/null
        wait "${local_pid}" 2>/dev/null
        FAILURES=$((FAILURES + 1))
    else
        wait "${local_pid}" 2>/dev/null
        printf '  %-52s %-22s %s\n' "SIG${sig} -> clean shutdown" "$(describe $?)" "${GREEN}PASS${OFF}"
    fi
done

# ---------------------------------------------------------------------------
# 4. Argument validation
# ---------------------------------------------------------------------------
echo
echo "${BOLD}4. Argument validation${OFF}"
run_case "--frames -5 (must be rejected)"      nonzero "${BINARY}" --frames -5
run_case "--frames abc (must be rejected)"      nonzero "${BINARY}" --frames abc
run_case "--frames 1e30 (must be rejected)"     nonzero "${BINARY}" --frames 99999999999999999999
run_case "--frames (missing value, rejected)"   nonzero "${BINARY}" --frames
run_case "--unknownflag (rejected)"             nonzero "${BINARY}" --unknownflag
run_case "--content /nope (rejected)"           nonzero "${BINARY}" --content /nope
run_case "--log-level BOGUS (warns, continues)" zero    "${BINARY}" --headless --frames 5 --log-level BOGUS
run_case "--set graphics.windowWidth=abc"      zero    "${BINARY}" --headless --frames 5 --set graphics.windowWidth=abc

# ---------------------------------------------------------------------------
# 5. Alternate video drivers
# ---------------------------------------------------------------------------
echo
echo "${BOLD}5. Video drivers${OFF}"
run_case "SDL_VIDEODRIVER=dummy"  zero env SDL_VIDEODRIVER=dummy "${BINARY}" --frames 60
run_case "--headless flag"        zero "${BINARY}" --headless --frames 60

# ---------------------------------------------------------------------------
# 6. Unit tests
# ---------------------------------------------------------------------------
echo
echo "${BOLD}6. Test suite${OFF}"
if ctest --preset debug >/dev/null 2>&1; then
    printf '  %-52s %-22s %s\n' "ctest --preset debug" "all passed" "${GREEN}PASS${OFF}"
else
    printf '  %-52s %-22s %s\n' "ctest --preset debug" "failures" "${RED}FAIL${OFF}"
    ctest --preset debug --output-on-failure 2>&1 | tail -20
    FAILURES=$((FAILURES + 1))
fi

# ---------------------------------------------------------------------------
# 7. Sanitizers
# ---------------------------------------------------------------------------
if (( SANITIZERS )); then
    echo
    echo "${BOLD}7. Sanitizers${OFF}"
    cmake --preset asan >/dev/null 2>&1 && cmake --build --preset asan >/dev/null 2>&1
    if [[ -x build/asan/EraShift ]]; then
        run_case "ASan+UBSan 300 frames" zero env \
            ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
            "${PWD}/build/asan/EraShift" --headless --frames 300
    else
        echo "  ${YELLOW}asan build unavailable, skipped${OFF}"
    fi
fi

# ---------------------------------------------------------------------------
echo
if (( FAILURES == 0 )); then
    echo "${GREEN}${BOLD}All checks passed.${OFF}"
    exit 0
fi
echo "${RED}${BOLD}${FAILURES} check(s) failed.${OFF}"
exit 1
