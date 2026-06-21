set(STB_IMAGE_SRC "${SM_EXTERN_DIR}/stb_image.c")
set(STB_IMAGE_HPP "${SM_EXTERN_DIR}/stb/stb_image.h")

source_group(TREE "${SM_EXTERN_DIR}" FILES ${STB_IMAGE_SRC} ${STB_IMAGE_HPP})

add_library("stb_image" STATIC ${STB_IMAGE_SRC} ${STB_IMAGE_HPP})

set_property(TARGET "stb_image" PROPERTY FOLDER "External Libraries")

disable_project_warnings("stb_image")

target_include_directories("stb_image" PUBLIC "${SM_EXTERN_DIR}/stb")

if(WIN32)
  target_compile_definitions("stb_image" PUBLIC STBI_WINDOWS_UTF8)
endif()