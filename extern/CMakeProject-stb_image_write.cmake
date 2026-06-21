set(STB_IMAGE_WRITE_SRC "${SM_EXTERN_DIR}/stb_image_write.c")
set(STB_IMAGE_WRITE_HPP "${SM_EXTERN_DIR}/stb/stb_image_write.h")

source_group(
  TREE "${SM_EXTERN_DIR}"
  FILES ${STB_IMAGE_WRITE_SRC} ${STB_IMAGE_WRITE_HPP})

add_library(
  "stb_image_write" STATIC
  ${STB_IMAGE_WRITE_SRC}
  ${STB_IMAGE_WRITE_HPP})

set_property(TARGET "stb_image_write" PROPERTY FOLDER "External Libraries")

disable_project_warnings("stb_image_write")

target_include_directories("stb_image_write" PUBLIC "${SM_EXTERN_DIR}/stb")