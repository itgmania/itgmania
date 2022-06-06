#!/bin/sh
set -eux

cd "$(dirname $0)"

docker build -f Dockerfile-linux-amd64 . -t itgmania-linux-build:amd64

docker run -i -v $(pwd)/..:/data itgmania-linux-build:amd64 sh -eux <<'EOF'
cmake -S /data -B /tmp/Build -DCMAKE_BUILD_TYPE=Release -DWITH_FULL_RELEASE=On
cmake --build /tmp/Build -j $(nproc)
cmake --build /tmp/Build --target package
mkdir -p /data/Build/release
cp /tmp/Build/ITGmania-* /data/Build/release/
EOF
