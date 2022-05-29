if(WITH_SYSTEM_GLEW)
  find_package(GLEW REQUIRED)
  set(GLEW_INCLUDE_DIRS ${GLEW_INCLUDE_DIRS} PARENT_SCOPE)
  set(GLEW_LIBRARIES ${GLEW_LIBRARIES} PARENT_SCOPE)
  set(GLEW_LIBRARY_RELEASE ${GLEW_LIBRARY_RELEASE} PARENT_SCOPE)
else()

  set(GLEW_SRC "glew/src/glew.c")

  set(GLEW_HPP "glew/include/GL/eglew.h"
               "glew/include/GL/glew.h"
               "glew/include/GL/glxew.h"
               "glew/include/GL/wglew.h")

  source_group("" FILES ${GLEW_SRC} ${GLEW_HPP})

  add_library("glew" STATIC ${GLEW_SRC} ${GLEW_HPP})

  set_property(TARGET "glew" PROPERTY FOLDER "External Libraries")

  target_compile_definitions("glew" PRIVATE GLEW_STATIC)

  if(MSVC)
    target_compile_definitions("glew" PRIVATE _MBCS)
  endif(MSVC)

  target_include_directories("glew" PUBLIC "glew/include")
endif()
