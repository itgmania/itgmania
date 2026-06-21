#!/bin/sh
set -eux

cd "$(dirname $0)/.."

cmake -B Build/release-arm64 -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES=arm64 -DWITH_CLUB_FANTASTIC=On
cmake --build Build/release-arm64 -j $(sysctl -n hw.logicalcpu)
cmake --build Build/release-arm64 --target package
cmake -B Build/release-arm64 -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES=arm64 -DWITH_CLUB_FANTASTIC=Off
cmake --build Build/release-arm64 --target package

cmake -B Build/release-x86_64 -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES=x86_64 -DWITH_CLUB_FANTASTIC=On
cmake --build Build/release-x86_64 -j $(sysctl -n hw.logicalcpu)
cmake --build Build/release-x86_64 --target package
cmake -B Build/release-x86_64 -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES=x86_64 -DWITH_CLUB_FANTASTIC=Off
cmake --build Build/release-x86_64 --target package

cmake -B Build/release-x86_64-portable -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES=x86_64 -DWITH_CLUB_FANTASTIC=Off -DWITH_MACOS_PORTABLE_APP=On
cmake --build Build/release-x86_64-portable -j $(sysctl -n hw.logicalcpu)
# The portable app deliberately doesn't have a .dmg target, so we just move the .app bundle to the right place.
mv ITGmania.app Build/release-x86_64-portable/ITGmania.app
