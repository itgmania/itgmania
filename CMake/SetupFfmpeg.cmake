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
  find_program(FFMPEG_YASM_EXECUTABLE yasm
               PATHS /usr/bin /usr/local/bin /opt/local/bin)
  list(APPEND FFMPEG_CONFIGURE "--x86asmexe=${FFMPEG_YASM_EXECUTABLE}")
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
