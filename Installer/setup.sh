#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'root privileges required' >&2
    exit 1
fi

cd "$(dirname "$0")"

[ -d /opt ] || install -d -m 755 -o root -g root /opt
cp -R --preserve=mode,timestamps itgmania /opt
ln -sf /opt/itgmania/itgmania.desktop /usr/share/applications
