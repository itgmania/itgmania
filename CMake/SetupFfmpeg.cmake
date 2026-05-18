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
            "--disable-vaapi"
            "--disable-bzlib"
            "--enable-gpl"
            "--enable-version3"
            "--enable-pthreads"
            "--enable-static"
            "--enable-zlib"
            "--prefix=/")

if(CMAKE_POSITION_INDEPENDENT_CODE)
  list(APPEND FFMPEG_CONFIGURE "--enable-pic")
endif()

if(MACOSX)
  list(APPEND FFMPEG_CONFIGURE "--enable-cross-compile")
  list(APPEND FFMPEG_CONFIGURE "--enable-videotoolbox")
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=-mmacosx-version-min=11")
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    list(APPEND FFMPEG_CONFIGURE "--arch=arm64" "--extra-cflags=-arch arm64" "--extra-ldflags=-arch arm64")
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    list(APPEND FFMPEG_CONFIGURE "--arch=x86_64" "--extra-cflags=-arch x86_64" "--extra-ldflags=-arch x86_64")
  else()
    message(FATAL_ERROR
      "Unsupported macOS architecture: ${CMAKE_OSX_ARCHITECTURES}, set CMAKE_OSX_ARCHITECTURES to either arm64 or x86_64"
    )
  endif()
endif()

if(NOT WITH_EXTERNAL_WARNINGS)
  list(APPEND FFMPEG_CONFIGURE "--extra-cflags=-w")
endif()

if(CMAKE_GENERATOR STREQUAL "Xcode" OR CMAKE_GENERATOR MATCHES "Ninja")
  if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(NUM_CORES $ENV{CMAKE_BUILD_PARALLEL_LEVEL})
  else()
    cmake_host_system_information(RESULT NUM_CORES QUERY NUMBER_OF_LOGICAL_CORES)
  endif()
  list(APPEND SM_FFMPEG_MAKE "make" "-j${NUM_CORES}")
else()
  list(APPEND SM_FFMPEG_MAKE $(MAKE))
endif()
list(APPEND SM_FFMPEG_MAKE "&&" "make" "DESTDIR=./dest" "install")

externalproject_add("ffmpeg"
                    SOURCE_DIR "${SM_FFMPEG_SRC_DIR}"
                    CONFIGURE_COMMAND ${FFMPEG_CONFIGURE}
                    BUILD_COMMAND "${SM_FFMPEG_MAKE}"
                    UPDATE_COMMAND ""
                    INSTALL_COMMAND ""
                    TEST_COMMAND ""
                    BYPRODUCTS
                      "<BINARY_DIR>/dest/lib/libavformat.a"
                      "<BINARY_DIR>/dest/lib/libavcodec.a"
                      "<BINARY_DIR>/dest/lib/libswscale.a"
                      "<BINARY_DIR>/dest/lib/libavutil.a")

externalproject_get_property("ffmpeg" BINARY_DIR)
set(SM_FFMPEG_LIB ${BINARY_DIR}/dest/lib)
set(SM_FFMPEG_INCLUDE ${BINARY_DIR}/dest/include)
