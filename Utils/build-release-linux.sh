#!/bin/sh
set -eux

cd "$(dirname $0)"

docker build -f Dockerfile-linux-amd64 . -t itgmania-linux-build:amd64

docker run -i -v $(pwd)/..:/data itgmania-linux-build:amd64 sh -eux <<'EOF'
git config --global --add safe.directory /data
cmake -S /data -B /tmp/Build -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=On
cmake --build /tmp/Build -j $(nproc)
cmake --build /tmp/Build --target package
cmake -S /data -B /tmp/Build -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=Off
cmake --build /tmp/Build --target package
mkdir -p /data/Build/release
cp /tmp/Build/ITGmania-* /data/Build/release/
EOF
