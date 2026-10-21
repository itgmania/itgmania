set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

add_subdirectory("googletest" "${CMAKE_CURRENT_BINARY_DIR}/googletest")

foreach(target gtest gtest_main)
  set_property(TARGET "${target}" PROPERTY FOLDER "External Libraries")
  set_property(TARGET "${target}" PROPERTY COMPILE_WARNING_AS_ERROR OFF)
  disable_project_warnings("${target}")
endforeach()
