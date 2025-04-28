#!/bin/sh
set -eux

cd "$(dirname $0)"

ARCH="${ARCH:-$(arch)}"

podman build --arch=${ARCH} -f Dockerfile-linux . -t itgmania-linux-build:${ARCH}

podman run --name build-itgmania-${ARCH} --replace  \
	-i -v $(pwd)/..:/data:rw,Z,U --arch=${ARCH} \
	itgmania-linux-build:${ARCH} sh -eux <<'EOF'
WITH_MINIMAID=On ; [ "$(arch)" != "x86_64" ] && WITH_MINIMAID=Off
export WITH_MINIMAID
git config --global --add safe.directory /data
cmake -S /data -B /tmp/Build -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=On
cmake --build /tmp/Build -j $(nproc)
cmake --build /tmp/Build --target package
cmake -S /data -B /tmp/Build -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=Off
cmake --build /tmp/Build --target package
mkdir -p /data/Build/release
cp /tmp/Build/ITGmania-* /data/Build/release/
EOF
