#!/usr/bin/env bash
# Fetch PulseAudio development headers without root.
#
# Why this exists
# ---------------
# SDL's PulseAudio backend is compiled in, not dynamically loaded. No headers on
# the machine means the driver is not in the binary, and the game runs silently
# forever with a log line that says only "no available audio device" - which
# reads as "this machine has no sound card" on a machine whose sound server is
# running perfectly well.
#
# Inside a container you usually cannot `apt install libpulse-dev`, but the
# runtime library (`libpulse.so.0`) is usually present, and the headers are a
# single small .deb. So this downloads the package, unpacks it into `.deps/` and
# prints the two variables `fetch_deps.sh` needs. Nothing is installed system
# wide, and nothing outside the project directory is touched.
#
# Usage
# -----
#   ./scripts/get_audio_headers.sh
#   eval "$(./scripts/get_audio_headers.sh --print-env)"
#   ./scripts/fetch_deps.sh
#
# If you can install packages, prefer that: it is less moving parts.
#   apt install libpulse-dev        # pipewire / pulseaudio
#   apt install libasound2-dev      # plain ALSA

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/.deps/pulse"

if [[ "${1:-}" == "--print-env" ]]; then
    # Consumed with eval, so only two variables and both are quoted.
    printf 'ERASHIFT_PULSE_INCLUDE=%q\n' "${DEST}/usr/include"
    printf 'ERASHIFT_PULSE_LIBDIR=%q\n' "${DEST}/usr/lib/x86_64-linux-gnu"
    exit 0
fi

if [[ -f "${DEST}/usr/include/pulse/pulseaudio.h" ]]; then
    echo "[audio] headers already present at ${DEST}/usr/include/pulse"
    echo "[audio] export ERASHIFT_PULSE_INCLUDE=${DEST}/usr/include"
    echo "[audio] export PKG_CONFIG_PATH=${DEST}/usr/lib/x86_64-linux-gnu/pkgconfig:\${PKG_CONFIG_PATH:-}"
    exit 0
fi

# The runtime library has to exist for this to be worth doing; headers without
# it would compile and then fail to load.
# Matched with bash's own pattern matching, deliberately no pipeline.
#
# `ldconfig -p | grep -q pattern` looks correct and is not: `grep -q` exits the
# instant it matches, its producer receives SIGPIPE, and under `set -o pipefail`
# the pipeline then reports 141 - a failure - for a search that found exactly
# what it was looking for. The first version of this script had that bug and
# refused to run on a machine that does have PulseAudio.
_LD_CACHE="$(ldconfig -p 2>/dev/null || true)"
if [[ "${_LD_CACHE}" != *"libpulse.so.0"* ]]; then
    cat >&2 <<'EOF'
[audio] libpulse.so.0 is not installed either, so there is no sound server to
        talk to. Install the runtime package first, e.g.:

            apt install libpulse0        # or pipewire-audio

        Then re-run this script.
EOF
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "[audio] no apt-get available; install libpulse-dev with your package manager" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "[audio] downloading libpulse-dev headers..."
( cd "${TMP}" && apt-get download libpulse-dev >/dev/null 2>&1 )

shopt -s nullglob
DEBS=("${TMP}"/*.deb)
shopt -u nullglob
if [[ ${#DEBS[@]} -eq 0 ]]; then
    echo "[audio] could not download libpulse-dev" >&2
    exit 1
fi

echo "[audio] unpacking into ${DEST} (nothing is installed system wide)"
mkdir -p "${DEST}"
dpkg-deb -x "${DEBS[0]}" "${DEST}"

# Rewrite the .pc files to point at the unpacked tree.
#
# The shipped `libpulse.pc` hard-codes `includedir=/usr/include`, so pkg-config
# emits `-I/usr/include` and the build then fails with "pulse/pulseaudio.h: No such
# file or directory" - the header is right there, just not where the file says.
# A relocated prefix needs a relocated .pc, which is what these two lines do.
PCDIR="${DEST}/usr/lib/x86_64-linux-gnu/pkgconfig"
if [[ -d "${PCDIR}" ]]; then
    for pc in "${PCDIR}"/*.pc; do
        [[ -f "${pc}" ]] || continue
        sed -i "s|^includedir=.*|includedir=${DEST}/usr/include|" "${pc}"
        sed -i "s|^libdir=.*|libdir=${DEST}/usr/lib/x86_64-linux-gnu|" "${pc}"
    done
    echo "[audio] repointed $(ls -1 "${PCDIR}"/*.pc | wc -l) pkg-config file(s) at ${DEST}"
fi

if [[ ! -f "${DEST}/usr/include/pulse/pulseaudio.h" ]]; then
    echo "[audio] unpacked but the header is missing; unexpected package layout" >&2
    exit 1
fi

# The dev package ships `libpulse.so` as a symlink to `libpulse.so.0`, but the
# `.0` file itself lives in the *runtime* package, in the system library
# directory. So the unpacked symlink is dangling, and `-lpulse` finds nothing:
# the linker needs the unversioned name, and the system has only the versioned
# one. Point the symlink at the real file instead of leaving it broken.
LIBDIR="${DEST}/usr/lib/x86_64-linux-gnu"
SYSTEM_PULSE=""
for candidate in /usr/lib/"$(uname -m)"-linux-gnu/libpulse.so.0 \
                /usr/lib64/libpulse.so.0 /usr/lib/libpulse.so.0; do
    if [[ -e "${candidate}" ]]; then
        SYSTEM_PULSE="${candidate}"
        break
    fi
done

if [[ -n "${SYSTEM_PULSE}" ]]; then
    ln -sf "${SYSTEM_PULSE}" "${LIBDIR}/libpulse.so"
    echo "[audio] linkable name -> ${SYSTEM_PULSE}"
else
    echo "[audio] WARNING: libpulse.so.0 not found in the system library path." >&2
    echo "[audio]          SDL will compile but not link. Install the runtime" >&2
    echo "[audio]          package (apt install libpulse0) and re-run." >&2
fi

cat <<EOF

[audio] done.

  headers  ${DEST}/usr/include
  library  ${DEST}/usr/lib/x86_64-linux-gnu

[audio] Now rebuild SDL with the PulseAudio backend:

    export PKG_CONFIG_PATH="${DEST}/usr/lib/x86_64-linux-gnu/pkgconfig:\${PKG_CONFIG_PATH:-}"
    export ERASHIFT_PULSE_INCLUDE="${DEST}/usr/include"
    ./scripts/fetch_deps.sh
    cmake --preset debug && cmake --build --preset debug

[audio] Then check it worked:

    ./build/debug/EraShift --headless --frames 60
    # expect: INFO  audio: started on pulse

EOF
