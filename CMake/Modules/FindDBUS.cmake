# Use pkg-config to find the D-Bus library.
#
# Once found, the following are defined:
#  DBUS_FOUND
#  DBUS_INCLUDE_DIRS
#  DBUS_LIBRARIES

find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
