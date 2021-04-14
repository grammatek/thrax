#!/bin/bash

# This script builds all android target platforms of Thrax and installs the library in
# folder $PREFIX.
#
# You can override the build & installation behaviour with environment variables:
#   - ANDROID_NDK: Android ANDROID_NDK including its version subdirectory
#   - ANDROID_NDK_TOOLCHAIN:  Host platform depending on your build machine
#   - ANDROID_API: minSdkVersion
#   - PREFIX: installation directory
#   - OPENFST_DIR: path where openfst with the same android target platform is installed, 
#     defaults to $PREFIX
#
# Prerequisites: installed Android NDK

set -o pipefail
set -x
set -e

# Set the location of where you downloaded the ANDROID_NDK including its version subdirectory
export ANDROID_NDK="${ANDROID_NDK:-/usr/local/ndk}"

# Set toolchain, depending on your build machine...
ANDROID_NDK_TOOLCHAIN=${ANDROID_NDK_TOOLCHAIN:-$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64}
#export ANDROID_NDK_TOOLCHAIN=${ANDROID_NDK_TOOLCHAIN:-$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64}

# Your minSdkVersion.
ANDROID_API=${ANDROID_API:-21}

# This is the installation directory used for make install. Additionally, the target platform is added
# as installation directory, so that you can have all target platforms available at the same time
PREFIX=${PREFIX:-$HOME/install-android}

# Path to where OpenFST is installed with the same target platform as subdirectory name
OPENFST_DIR=${OPENFST_DIR:-$PREFIX}

# Available target platforms:
#   - aarch64-linux-android
#   - armv7a-linux-androideabi
#   - i686-linux-android
#   - x86_64-linux-android
ARCHS="armv7a-linux-androideabi aarch64-linux-android i686-linux-android x86_64-linux-android"

for TARGET in ${ARCHS}
do
  INST_DIR="$PREFIX/$TARGET"
  THIRT_PARTY_DIR="$OPENFST_DIR/$TARGET"
  if [ -d "$INST_DIR" ] && [ -f Makefile ]; then
    # Cleanup previous configuration and Makefile artifacts. Calling 'make clean' alone is unfortunately not
    # sufficient and out-of-tree builds are not supported.
    rm -f config.log config.status
  fi

  export AR=$ANDROID_NDK_TOOLCHAIN/bin/llvm-ar
  export CC=$ANDROID_NDK_TOOLCHAIN/bin/$TARGET$ANDROID_API-clang
  export AS=$CC
  export CXX=$ANDROID_NDK_TOOLCHAIN/bin/$TARGET$ANDROID_API-clang++
  export LD=$ANDROID_NDK_TOOLCHAIN/bin/ld
  export NM=$ANDROID_NDK_TOOLCHAIN/bin/nm
  export RANLIB=$ANDROID_NDK_TOOLCHAIN/bin/llvm-ranlib
  export STRIP=$ANDROID_NDK_TOOLCHAIN/bin/llvm-strip

  ./configure --host "$TARGET" --prefix="$INST_DIR" CFLAGS="-I $THIRT_PARTY_DIR/include -fPIC" \
    CXXFLAGS="-I $THIRT_PARTY_DIR/include -fPIC" CPPFLAGS="-I $THIRT_PARTY_DIR/include" \
    LDFLAGS="-L$THIRT_PARTY_DIR/lib -L$INST_DIR/lib" --disable-shared

  make clean && make -j
  make uninstall || true
  make install
done
