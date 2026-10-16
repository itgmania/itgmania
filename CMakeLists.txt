cmake_minimum_required(VERSION 3.20)

project(StepMania)

option(BUILD_TESTING "Build tests (Linux only)" OFF)
include(CTest)

include(StepmaniaCore.cmake)

# The external libraries need to be included.
add_subdirectory(extern)

# The internal libraries and eventual executable to be used.
add_subdirectory(src)
