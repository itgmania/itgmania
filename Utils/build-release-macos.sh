#!/bin/sh
set -eux

cd "$(dirname $0)/.."

cmake -B Build/release-universal -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DWITH_CLUB_FANTASTIC=On
cmake --build Build/release-universal -j $(sysctl -n hw.logicalcpu)
cmake --build Build/release-universal --target package

cmake -B Build/release-universal -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DWITH_CLUB_FANTASTIC=Off
cmake --build Build/release-universal -j $(sysctl -n hw.logicalcpu)
cmake --build Build/release-universal --target package
