if(CMAKE_GENERATOR MATCHES "Ninja")
  message(
    FATAL_ERROR
      "You cannot use the Ninja generator when building the bundled ffmpeg library."
    )
endif()

set(SM_FFMPEG_SRC_DIR "${SM_EXTERN_DIR}/ffmpeg")
set(SM_FFMPEG_CONFIGURE_EXE "${SM_FFMPEG_SRC_DIR}/configure")

list(APPEND FFMPEG_CONFIGURE
            "${SM_FFMPEG_CONFIGURE_EXE}"
            "--disable-autodetect"
            "--disable-avdevice"
            "--disable-avfilter"
            "--disable-devices"
            "--disable-doc"
            "--disable-filters"
            "--disable-lzma"
            "--disable-network"
            "--disable-postproc"
            "--disable-programs"
            "--disable-swresample"
            "--enable-bzlib"
            "--enable-gpl"
            "--enable-pthreads"
            "--enable-static"
            "--enable-zlib"
            "--prefix=/")

if(CMAKE_POSITION_INDEPENDENT_CODE)
  list(APPEND FFMPEG_CONFIGURE "--enable-pic")
endif()

if(MACOSX)
  list(APPEND FFMPEG_CONFIGURE "--disable-asm")
  list(APPEND FFMPEG_CONFIGURE "--enable-cross-compile")
  list(APPEND FFMPEG_CONFIGURE "--enable-videotoolbox")
else()
  list(APPEND FFMPEG_CONFIGURE "--enable-vaapi")
endif()

if(NOT WITH_EXTERNAL_WARNINGS)
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=-w")
endif()

list(APPEND SM_FFMPEG_MAKE $(MAKE))
if(WITH_FFMPEG_JOBS GREATER 0)
  list(APPEND SM_FFMPEG_MAKE "-j${WITH_FFMPEG_JOBS}")
endif()
list(APPEND SM_FFMPEG_MAKE "&&" "make" "DESTDIR=./dest" "install")

if(MACOSX)
  externalproject_add("ffmpeg_arm64"
                      SOURCE_DIR "${SM_FFMPEG_SRC_DIR}"
                      CONFIGURE_COMMAND ${FFMPEG_CONFIGURE} "--arch=arm64" "--extra-cflags=-arch arm64" "--extra-ldflags=-arch arm64"
                      BUILD_COMMAND "${SM_FFMPEG_MAKE}"
                      UPDATE_COMMAND ""
                      INSTALL_COMMAND ""
                      TEST_COMMAND "")

  externalproject_get_property("ffmpeg_arm64" BINARY_DIR)
  set(SM_FFMPEG_LIB_ARM64 ${BINARY_DIR}/dest/lib)

  externalproject_add("ffmpeg_x86_64"
                      SOURCE_DIR "${SM_FFMPEG_SRC_DIR}"
                      CONFIGURE_COMMAND ${FFMPEG_CONFIGURE} "--arch=x86_64" "--extra-cflags=-arch x86_64" "--extra-ldflags=-arch x86_64"
                      BUILD_COMMAND "${SM_FFMPEG_MAKE}"
                      UPDATE_COMMAND ""
                      INSTALL_COMMAND ""
                      TEST_COMMAND "")

  externalproject_get_property("ffmpeg_x86_64" BINARY_DIR)
  set(SM_FFMPEG_LIB_X86_64 ${BINARY_DIR}/dest/lib)

  # The header files are the same for both architectures, so it doesn't matter
  # which one of the two binary dirs we use.
  set(SM_FFMPEG_INCLUDE ${BINARY_DIR}/dest/include)
else()
  externalproject_add("ffmpeg"
                      SOURCE_DIR "${SM_FFMPEG_SRC_DIR}"
                      CONFIGURE_COMMAND ${FFMPEG_CONFIGURE}
                      BUILD_COMMAND "${SM_FFMPEG_MAKE}"
                      UPDATE_COMMAND ""
                      INSTALL_COMMAND ""
                      TEST_COMMAND "")

  externalproject_get_property("ffmpeg" BINARY_DIR)
  set(SM_FFMPEG_LIB ${BINARY_DIR}/dest/lib)
  set(SM_FFMPEG_INCLUDE ${BINARY_DIR}/dest/include)
endif()
