#!/usr/bin/env bash
#
# Era Shift - third-party dependency bootstrap
#
# Builds the SDL3 family and doctest into a project-local prefix so that no
# root/sudo access is required. If your distribution already ships SDL3
# development packages you can skip this script entirely; CMakeLists.txt
# prefers system packages and only falls back to the local prefix.
#
#   SDL3        3.4.16
#   SDL3_image  3.4.6
#   SDL3_mixer  3.2.4
#   SDL3_ttf    3.2.2
#   doctest     2.4.12
#
# Usage:
#   ./scripts/fetch_deps.sh            # build everything
#   ./scripts/fetch_deps.sh --clean    # wipe the local prefix and rebuild
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

# Keep third-party build artefacts out of the game build tree.
DEPS_DIR="${PROJECT_ROOT}/.deps"
PREFIX="${DEPS_DIR}/install"
BUILD_DIR="${DEPS_DIR}/build"
JOBS="$(nproc 2>/dev/null || echo 4)"

SDL_VERSION="3.4.16"
SDL_IMAGE_VERSION="3.4.6"
SDL_MIXER_VERSION="3.2.4"
SDL_TTF_VERSION="3.2.2"
DOCTEST_VERSION="2.4.12"

if [[ "${1:-}" == "--clean" ]]; then
    echo "[deps] removing ${DEPS_DIR}"
    rm -rf "${DEPS_DIR}"
fi

mkdir -p "${PREFIX}" "${BUILD_DIR}"

fetch() {
    local url="$1" dest="$2"
    if [[ -f "${dest}" ]]; then
        echo "[deps] cached: $(basename "${dest}")"
        return 0
    fi
    echo "[deps] downloading $(basename "${dest}")"
    curl --fail --location --silent --show-error --retry 3 -o "${dest}" "${url}"
}

# ---------------------------------------------------------------------------
# doctest (header only)
# ---------------------------------------------------------------------------
fetch "https://github.com/doctest/doctest/archive/refs/tags/v${DOCTEST_VERSION}.tar.gz" \
      "${DEPS_DIR}/doctest.tar.gz"
if [[ ! -f "${PREFIX}/include/doctest/doctest.h" ]]; then
    echo "[deps] unpacking doctest"
    rm -rf "${DEPS_DIR}/doctest-${DOCTEST_VERSION}"
    tar -xzf "${DEPS_DIR}/doctest.tar.gz" -C "${DEPS_DIR}"
    mkdir -p "${PREFIX}/include"
    cp -r "${DEPS_DIR}/doctest-${DOCTEST_VERSION}/doctest" "${PREFIX}/include/"
fi

build_sdl() {
    local name="$1" repo="$2" version="$3"
    shift 3
    local extra=("$@")

    fetch "https://github.com/libsdl-org/${repo}/archive/refs/tags/release-${version}.tar.gz" \
          "${DEPS_DIR}/${repo}.tar.gz"

    if [[ -f "${PREFIX}/lib/cmake/${repo}/data/Config.cmake" || -f "${PREFIX}/lib/cmake/SDL3/SDL3Config.cmake" ]]; then
        :
    fi

    if [[ ! -d "${DEPS_DIR}/${repo}-release-${version}" ]]; then
        echo "[deps] unpacking ${repo} ${version}"
        tar -xzf "${DEPS_DIR}/${repo}.tar.gz" -C "${DEPS_DIR}"
    fi

    local src="${DEPS_DIR}/${repo}-release-${version}"
    local bld="${BUILD_DIR}/${repo}"
    mkdir -p "${bld}"

    # SDL's configure step aborts when an *optional* platform feature has no
    # development headers (XSCRNSAVER on a minimal desktop, for example). Retry
    # the configure step, disabling each missing feature the build asks about,
    # instead of hard-coding a list of flags that varies per machine.
    local attempt=0
    local max_attempts=12
    local -a disable_flags=()
    while (( attempt < max_attempts )); do
        attempt=$(( attempt + 1 ))
        echo "[deps] configuring ${repo} ${version} (attempt ${attempt})"
        if cmake -S "${src}" -B "${bld}" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
            -DCMAKE_INSTALL_LIBDIR=lib \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DSDL_SHARED=ON \
            -DSDL_STATIC=OFF \
            -DSDL_TEST_LIBRARY=OFF \
            -DSDL_INSTALL_TESTS=OFF \
            -DSDL_TESTS=OFF \
            "${extra[@]}" "${disable_flags[@]}" > "${bld}/configure.log" 2>&1; then
            break
        fi

        local missing
        missing="$(grep -o "configure with -D[A-Za-z0-9_]*=OFF" "${bld}/configure.log" | head -1 | sed 's/configure with //')"
        if [[ -z "${missing}" ]]; then
            echo "[deps] FAILED configuring ${repo}; tail of log:" >&2
            tail -40 "${bld}/configure.log" >&2
            exit 1
        fi

        if printf '%s\n' "${disable_flags[@]}" | grep -qx -- "${missing}"; then
            echo "[deps] FAILED configuring ${repo} even with ${missing} disabled; tail of log:" >&2
            tail -40 "${bld}/configure.log" >&2
            exit 1
        fi

        echo "[deps] optional dependency missing - retrying with ${missing}"
        disable_flags+=("${missing}")
    done

    if (( attempt >= max_attempts )); then
        echo "[deps] FAILED configuring ${repo} after ${max_attempts} attempts" >&2
        tail -40 "${bld}/configure.log" >&2
        exit 1
    fi

    echo "[deps] building ${repo} ${version} (-j${JOBS}) - this takes a few minutes"
    cmake --build "${bld}" --parallel "${JOBS}" > "${bld}/build.log" 2>&1 || {
        echo "[deps] FAILED building ${repo}; tail of log:" >&2
        tail -40 "${bld}/build.log" >&2
        exit 1
    }
    cmake --install "${bld}" > "${bld}/install.log" 2>&1
}

# Audio backends: ALSA/pipewire dev headers are frequently absent on minimal
# installs, so we probe for them and only enable what is actually available.
audio_flags=()
if pkg-config --exists alsa && [[ -f /usr/include/alsa/asoundlib.h ]]; then
    audio_flags+=("-DSDL_AUDIO_DRIVER_ALSA=ON" "-DSDL_AUDIO_DRIVER_PULSEAUDIO=ON")
else
    echo "[deps] no ALSA development headers found - SDL audio falls back to the dummy backend"
    audio_flags+=("-DSDL_AUDIO_DRIVER_ALSA=OFF" "-DSDL_AUDIO_DRIVER_PULSEAUDIO=OFF" "-DSDL_AUDIO_DRIVER_DUMMY=ON")
fi

# --- SDL3 core -------------------------------------------------------------
build_sdl "SDL3" "SDL" "${SDL_VERSION}" \
    -DSDL_VIDEO=ON \
    -DSDL_RENDER=ON \
    -DSDL_AUDIO=ON \
    "${audio_flags[@]}"

# --- SDL3_image ------------------------------------------------------------
build_sdl "SDL3_image" "SDL_image" "${SDL_IMAGE_VERSION}"

# --- SDL3_ttf --------------------------------------------------------------
build_sdl "SDL3_ttf" "SDL_ttf" "${SDL_TTF_VERSION}" \
    -DSDL_TTF_DYNAMIC=ON

# --- SDL3_mixer ------------------------------------------------------------
# Build only the codecs that are available as system libraries; the game
# degrades gracefully when a codec is missing.
mixer_flags=("-DSDL_MIXER_SAMPLES=16" "-DSDL_MIXER_WAV=ON")
if pkg-config --exists ogg vorbisfile; then
    mixer_flags+=("-DSDL_MIXER_OGG=ON")
else
    mixer_flags+=("-DSDL_MIXER_OGG=OFF")
fi
if pkg-config --exists flac; then
    mixer_flags+=("-DSDL_MIXER_FLAC=ON")
else
    mixer_flags+=("-DSDL_MIXER_FLAC=OFF")
fi
if pkg-config --exists opusfile; then
    mixer_flags+=("-DSDL_MIXER_OPUS=ON")
else
    mixer_flags+=("-DSDL_MIXER_OPUS=OFF")
fi
if pkg-config --exists mad; then
    mixer_flags+=("-DSDL_MIXER_MAD=ON")
else
    mixer_flags+=("-DSDL_MIXER_MAD=OFF")
fi
if pkg-config --exists mpg123; then
    mixer_flags+=("-DSDL_MIXER_MP3=ON")
else
    mixer_flags+=("-DSDL_MIXER_MP3=OFF")
fi
build_sdl "SDL3_mixer" "SDL_mixer" "${SDL_MIXER_VERSION}" "${mixer_flags[@]}"

echo ""
echo "[deps] done. Installed into ${PREFIX}"
echo "[deps] export CMAKE_PREFIX_PATH=${PREFIX} to use it, or re-run cmake in the build dir."
