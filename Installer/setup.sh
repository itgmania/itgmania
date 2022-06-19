#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo 'root privileges required' >&2
    exit 1
fi

copy_sl_config() {
	if [ -e "/opt/itgmania/Themes/Simply Love.old/$1" ]; then
		cp "/opt/itgmania/Themes/Simply Love.old/$1" "/opt/itgmania/Themes/Simply Love/$1"
	fi
}

cd "$(dirname "$0")"

# Move the old SL release out of the way
[ -d /opt/itgmania/Themes/Simply\ Love ] && mv /opt/itgmania/Themes/Simply\ Love{,.old}

# Install ITGm
[ -d /opt ] || install -d -m 755 -o root -g root /opt
cp -R --preserve=mode,timestamps itgmania /opt
ln -sf /opt/itgmania/itgmania.desktop /usr/share/applications

# Copy persistent files over from the old SL folder
if [ -d /opt/itgmania/Themes/Simply\ Love.old ]; then
	copy_sl_config Other/SongManager\ PreferredCourses.txt
	copy_sl_config Other/SongManager\ PreferredSongs.txt

	rm -rf /opt/itgmania/Themes/Simply\ Love.old
fi
