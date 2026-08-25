#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# MScore Video Sync Pro - Automated macOS Build & Deployment Script
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
VST3_DEST="${HOME}/Library/Audio/Plug-Ins/VST3"

echo "============================================================"
echo "  Building MScore Video Sync Pro (macOS VST3)"
echo "============================================================"

# 1. Verify / Install Homebrew prerequisites
if ! command -v brew &> /dev/null; then
    echo "[-] Homebrew not found. Please install Homebrew from https://brew.sh"
    exit 1
fi

echo "[*] Checking dependencies..."
for pkg in cmake ninja pkg-config ffmpeg; do
    if ! brew list --versions "${pkg}" &> /dev/null; then
        echo "[+] Installing ${pkg} via Homebrew..."
        brew install "${pkg}"
    fi
done

# 2. Check architecture target
ARCH_ARG="arm64"
if [[ "${1:-}" == "--universal" ]]; then
    echo "[*] Configuring Universal Binary (arm64 + x86_64)..."
    ARCH_ARG="arm64;x86_64"
else
    ARCH=$(uname -m)
    echo "[*] Configuring native architecture (${ARCH})..."
    ARCH_ARG="${ARCH}"
fi

# 3. Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 4. CMake Configuration
echo "[*] Running CMake configuration..."
cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="12.0" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCH_ARG}" \
    "${PROJECT_ROOT}"

# 5. Compile with Ninja
echo "[*] Compiling plugin..."
ninja

# 6. Verify and Deploy
echo "[*] Verifying output bundle..."
BUNDLE_PATH="${BUILD_DIR}/MScoreVideoSyncPro_artefacts/Release/VST3/MScore Video Sync Pro.vst3"

if [[ -d "${BUNDLE_PATH}" ]]; then
    echo "[+] Build successful: ${BUNDLE_PATH}"
    
    mkdir -p "${VST3_DEST}"
    echo "[+] Installing to ${VST3_DEST}..."
    cp -R "${BUNDLE_PATH}" "${VST3_DEST}/"

    BINARY_PATH="${VST3_DEST}/MScore Video Sync Pro.vst3/Contents/MacOS/MScore Video Sync Pro"
    if [[ -f "${BINARY_PATH}" ]]; then
        echo "[+] Binary architecture info:"
        lipo -info "${BINARY_PATH}"
    fi

    echo "============================================================"
    echo "  SUCCESS! MScore Video Sync Pro installed."
    echo "  Location: ${VST3_DEST}/MScore Video Sync Pro.vst3"
    echo "============================================================"
else
    echo "[-] Error: Output bundle not found at ${BUNDLE_PATH}"
    exit 1
fi
