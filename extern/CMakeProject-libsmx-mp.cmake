set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build as static library" FORCE)

# Add the libsmx-mp subdirectory - it has its own CMakeLists.txt
add_subdirectory("libsmx-mp" EXCLUDE_FROM_ALL)

# Set folder for IDE organization
set_property(TARGET "smx-mp" PROPERTY FOLDER "External Libraries")

message(STATUS "libsmx-mp library included as smx-mp (static)")


