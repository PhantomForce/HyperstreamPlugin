#!/bin/sh

OSTYPE=$(uname)

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[hyperstream-plugin - Error] macOS build script can be run on Darwin-type OS only."
    exit 1
fi

HAS_CMAKE=$(type cmake 2>/dev/null)

if [ "${HAS_CMAKE}" = "" ]; then
    echo "[hyperstream-plugin - Error] CMake not installed - please run 'install-dependencies-macos.sh' first."
    exit 1
fi

echo "[hyperstream-plugin] Building 'hyperstream-plugin' for macOS."

mkdir -p build && cd build
cmake .. \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.11 \
	-DQTDIR=/usr/local/opt/qt \
	-DLIBOBS_INCLUDE_DIR=../../obs-studio/libobs \
	-DLIBOBS_LIB=../../obs-studio/libobs \
	-DOBS_FRONTEND_LIB="$(pwd)/../../obs-studio/build/UI/obs-frontend-api/libobs-frontend-api.dylib" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_INSTALL_PREFIX=/usr \
&& make -j4
