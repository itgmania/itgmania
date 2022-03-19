if(WITH_SYSTEM_JSONCPP)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JSONCPP REQUIRED jsoncpp)
else()
  list(APPEND JSON_SRC "jsoncpp/jsoncpp.cpp")

  list(APPEND JSON_HPP
              "jsoncpp/json/json-forwards.h"
              "jsoncpp/json/json.h")

  source_group("" FILES ${JSON_SRC} ${JSON_HPP})

  add_library("jsoncpp" STATIC ${JSON_SRC} ${JSON_HPP})

  set_property(TARGET "jsoncpp" PROPERTY FOLDER "External Libraries")
  set_property(TARGET "jsoncpp" PROPERTY CXX_STANDARD 11)
  set_property(TARGET "jsoncpp" PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET "jsoncpp" PROPERTY CXX_EXTENSIONS OFF)

  disable_project_warnings("jsoncpp")

  target_include_directories("jsoncpp" PUBLIC "jsoncpp")

  if(MSVC)
    target_compile_definitions("jsoncpp" PRIVATE _CRT_SECURE_NO_WARNINGS)
  elseif(APPLE)
    set_target_properties("jsoncpp" PROPERTIES XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
    target_compile_options("jsoncpp" PRIVATE "-stdlib=libc++")
  endif()
endif()
