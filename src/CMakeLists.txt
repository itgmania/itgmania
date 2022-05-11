file(MAKE_DIRECTORY "${SM_SRC_DIR}/generated")

if(MSVC)
  # Add the mapconv program of the past to allow for debugging information.
  include("CMakeProject-mapconv.cmake")
  if(WITH_TEXTURE_GENERATOR OR WITH_FULL_RELEASE)
    include("CMakeProject-texture.cmake")
  endif()
endif()

# Keep the module path local for easier grabbing.
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

# Main project is below.

include(CMakeData-arch.cmake)
include(CMakeData-rage.cmake)
include(CMakeData-os.cmake)
include(CMakeData-actor.cmake)
include(CMakeData-screen.cmake)
include(CMakeData-data.cmake)
include(CMakeData-file-types.cmake)
include(CMakeData-globals.cmake)
include(CMakeData-singletons.cmake)

list(APPEND SMDATA_ALL_FILES_SRC
            ${SMDATA_GLOBAL_FILES_SRC}
            ${SMDATA_GLOBAL_SINGLETON_SRC}
            ${SMDATA_ALL_ACTORS_SRC}
            ${SMDATA_ALL_ARCH_SRC}
            ${SMDATA_ALL_DATA_SRC}
            ${SMDATA_ALL_RAGE_SRC}
            ${SMDATA_ALL_SCREENS_SRC}
            ${SMDATA_OS_SRC}
            ${SMDATA_FILE_TYPES_SRC})
list(APPEND SMDATA_ALL_FILES_HPP
            ${SMDATA_GLOBAL_FILES_HPP}
            ${SMDATA_GLOBAL_SINGLETON_HPP}
            ${SMDATA_ALL_ACTORS_HPP}
            ${SMDATA_ALL_ARCH_HPP}
            ${SMDATA_ALL_DATA_HPP}
            ${SMDATA_ALL_RAGE_HPP}
            ${SMDATA_ALL_SCREENS_HPP}
            ${SMDATA_OS_HPP}
            ${SMDATA_FILE_TYPES_HPP})

if(NOT APPLE)
  list(APPEND SMDATA_ALL_FILES_SRC "Main.cpp")
  source_group("" FILES "Main.cpp")
endif()

if(MSVC OR APPLE)
  set(SM_NAME_RELEASE "ITGmania")
  set(SM_NAME_DEBUG "ITGmania-debug")
  set(SM_NAME_MINSIZEREL "ITGmania-min-size")
  set(SM_NAME_RELWITHDEBINFO "ITGmania-release-symbols")
else()
  set(SM_NAME_RELEASE "itgmania")
  set(SM_NAME_DEBUG "itgmania-debug")
  set(SM_NAME_MINSIZEREL "itgmania-min-size")
  set(SM_NAME_RELWITHDEBINFO "itgmania-release-symbols")
endif()

# TODO: Make this actually be data and not an executable.
if(APPLE)
  add_executable("${SM_EXE_NAME}"
                 MACOSX_BUNDLE
                 ${SMDATA_ALL_FILES_SRC}
                 ${SMDATA_ALL_FILES_HPP})
  set_target_properties("${SM_EXE_NAME}" PROPERTIES MACOSX_BUNDLE TRUE)
else()
  if(MSVC)
    foreach(sm_src_file ${SMDATA_ALL_FILES_SRC})
      get_filename_component(sm_src_raw "${sm_src_file}" NAME_WE)
      if(${sm_src_raw} MATCHES "global")
        set_source_files_properties("${sm_src_raw}.cpp"
                                    PROPERTIES
                                    COMPILE_FLAGS
                                    "/Ycglobal.h")
      elseif(NOT (${sm_src_raw} MATCHES "verstub"))
        set_source_files_properties("${sm_src_raw}.cpp"
                                    PROPERTIES
                                    COMPILE_FLAGS
                                    "/Yuglobal.h")
      endif()
    endforeach()
  endif()
  add_executable("${SM_EXE_NAME}" ${SMDATA_ALL_FILES_SRC}
                 ${SMDATA_ALL_FILES_HPP})
endif()

set_property(TARGET "${SM_EXE_NAME}" PROPERTY CXX_STANDARD 11)
set_property(TARGET "${SM_EXE_NAME}" PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET "${SM_EXE_NAME}" PROPERTY CXX_EXTENSIONS ON)

set_target_properties("${SM_EXE_NAME}"
                      PROPERTIES OUTPUT_NAME "${SM_NAME_RELEASE}"
                                 RELEASE_OUTPUT_NAME "${SM_NAME_RELEASE}"
                                 DEBUG_OUTPUT_NAME "${SM_NAME_DEBUG}"
                                 MINSIZEREL_OUTPUT_NAME "${SM_NAME_MINSIZEREL}"
                                 RELWITHDEBINFO_OUTPUT_NAME "${SM_NAME_RELWITHDEBINFO}")

if(WITH_LTO)
  include(CheckIPOSupported)
  check_ipo_supported()
  set_property(TARGET "${SM_EXE_NAME}" PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Required for usage of tomcrypt and tommath.
target_compile_definitions("${SM_EXE_NAME}" PRIVATE LTM_DESC)

if(WITH_PORTABLE_TOMCRYPT)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE LTC_NO_ASM)
endif()
if(WITH_NO_ROLC_TOMCRYPT)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE LTC_NO_ROLC)
endif()

# Compilation flags per project here.
target_compile_definitions("${SM_EXE_NAME}" PRIVATE $<$<CONFIG:Debug>:DEBUG>)
target_compile_definitions("${SM_EXE_NAME}" PRIVATE $<$<CONFIG:Release>:RELEASE>)
target_compile_definitions("${SM_EXE_NAME}" PRIVATE $<$<CONFIG:MinSizeRel>:MINSIZEREL>)
target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                           $<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO>)

set(SM_COMPILE_FLAGS "")

if(WITH_SSE2)
  if(MSVC)
    if(SM_WIN32_ARCH MATCHES "x86")
      set(SM_COMPILE_FLAGS "${SM_COMPILE_FLAGS} /arch:SSE2")
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86")
    set(SM_COMPILE_FLAGS "${SM_COMPILE_FLAGS} -msse2")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(SM_COMPILE_FLAGS "${SM_COMPILE_FLAGS} -mtune=cortex-a72")
  else()
    message("Unrecognized host processor type")
  endif()
endif()

if(MSVC)
  # TODO: Find a way to do this cleanly for non MSVC users.
  set(SM_COMPILE_FLAGS "${SM_COMPILE_FLAGS} /MP2")
endif()

set_target_properties("${SM_EXE_NAME}"
                      PROPERTIES COMPILE_FLAGS "${SM_COMPILE_FLAGS}")

set_target_properties("${SM_EXE_NAME}"
                      PROPERTIES OUTPUT_NAME_DEBUG "${SM_NAME_DEBUG}"
                                 OUTPUT_NAME_MINSIZEREL "${SM_NAME_MINSIZEREL}"
                                 OUTPUT_NAME_RELWITHDEBINFO "${SM_NAME_RELWITHDEBINFO}")

if(WIN32)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE WINDOWS)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE _WINDOWS) # TODO: Remove this potential duplicate.
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE GLEW_STATIC)

  set_target_properties("${SM_EXE_NAME}"
                        PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                   "${SM_PROGRAM_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_RELEASE
                                   "${SM_PROGRAM_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_DEBUG
                                   "${SM_PROGRAM_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                                   "${SM_PROGRAM_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                                   "${SM_PROGRAM_DIR}")

  if(MSVC)
    # Allow for getting a virtualdub stack trace.
    add_custom_command(
      TARGET "${SM_EXE_NAME}" POST_BUILD
      COMMAND
        "mapconv"
        "${SM_PROGRAM_DIR}/$<$<CONFIG:DEBUG>:${SM_NAME_DEBUG}>$<$<CONFIG:MINSIZEREL>:${SM_NAME_MINSIZEREL}>$<$<CONFIG:RELWITHDEBINFO>:${SM_NAME_RELWITHDEBINFO}>$<$<CONFIG:RELEASE>:${SM_NAME_RELEASE}>.map"
        "${SM_PROGRAM_DIR}/$<$<CONFIG:DEBUG>:${SM_NAME_DEBUG}>$<$<CONFIG:MINSIZEREL>:${SM_NAME_MINSIZEREL}>$<$<CONFIG:RELWITHDEBINFO>:${SM_NAME_RELWITHDEBINFO}>$<$<CONFIG:RELEASE>:${SM_NAME_RELEASE}>.vdi"
      COMMENT "Generating file to allow for easier stack traces.")
  endif()
elseif(APPLE)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE BACKTRACE_METHOD_X86_DARWIN)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE MACOSX)
  set_target_properties(
    "${SM_EXE_NAME}"
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
      "${SM_ROOT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE
      "${SM_ROOT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG
      "${SM_ROOT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
      "${SM_ROOT_DIR}"
      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
      "${SM_ROOT_DIR}"
      MACOSX_BUNDLE_INFO_PLIST
      "${SM_XCODE_DIR}/Info.plist.in"
      XCODE_ATTRIBUTE_INFOPLIST_PREPROCESS
      "YES"
      XCODE_ATTRIBUTE_INFOPLIST_PREPROCESSOR_DEFINITIONS[variant=Release]
      "RELEASE"
      XCODE_ATTRIBUTE_INFOPLIST_PREPROCESSOR_DEFINITIONS[variant=Debug]
      "DEBUG"
      XCODE_ATTRIBUTE_INFOPLIST_PREPROCESSOR_DEFINITIONS[variant=MinSizeRel]
      "MINSIZEREL"
      XCODE_ATTRIBUTE_INFOPLIST_PREPROCESSOR_DEFINITIONS[variant=RelWithDebInfo]
      "RELWITHDEBINFO"
      XCODE_ATTRIBUTE_GCC_PREFIX_HEADER
      "${CMAKE_CURRENT_SOURCE_DIR}/archutils/Darwin/StepMania.pch"
      XCODE_ATTRIBUTE_GCC_ENABLE_CPP_EXCEPTIONS
      "NO")

  set(APPLE_BUNDLE_RESOURCES
    "${SM_ROOT_DIR}/Announcers"
    "${SM_ROOT_DIR}/BackgroundEffects"
    "${SM_ROOT_DIR}/BackgroundTransitions"
    "${SM_ROOT_DIR}/BGAnimations"
    "${SM_ROOT_DIR}/Characters"
    "${SM_ROOT_DIR}/Courses"
    "${SM_ROOT_DIR}/Data"
    "${SM_ROOT_DIR}/NoteSkins"
    "${SM_ROOT_DIR}/Scripts"
    "${SM_ROOT_DIR}/Songs"
    "${SM_ROOT_DIR}/Themes"
  )
  
  target_sources("${SM_EXE_NAME}" PUBLIC ${APPLE_BUNDLE_RESOURCES})
  set_source_files_properties(${APPLE_BUNDLE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  target_compile_definitions("${SM_EXE_NAME}" PRIVATE _XOPEN_SOURCE)

  if(${HAS_FFMPEG})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_FFMPEG)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SM_APP_RELEASE_NAME "${SM_NAME_DEBUG}")
  elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(SM_APP_RELEASE_NAME "${SM_NAME_MINSIZEREL}")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(SM_APP_RELEASE_NAME "${SM_NAME_RELWITHDEBINFO}")
  else()
    set(SM_APP_RELEASE_NAME "${SM_NAME_RELEASE}")
  endif()

  # Add the ability to copy the resource file.
  add_custom_command(TARGET "${SM_EXE_NAME}" POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                             "$<TARGET_FILE_DIR:ITGmania>/../Resources"
                     COMMAND ${CMAKE_COMMAND} -E copy
                             "${SM_XCODE_DIR}/smicon.icns"
                             "$<TARGET_FILE_DIR:ITGmania>/../Resources/"
                     COMMAND ${CMAKE_COMMAND} -E copy
                             "${SM_XCODE_DIR}/Hardware.plist"
                             "$<TARGET_FILE_DIR:ITGmania>/../Resources/")
else() # Linux
  set_target_properties("${SM_EXE_NAME}"
                        PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                   "${SM_ROOT_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_RELEASE
                                   "${SM_ROOT_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_DEBUG
                                   "${SM_ROOT_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                                   "${SM_ROOT_DIR}"
                                   RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                                   "${SM_ROOT_DIR}")

  if(${WITH_CRASH_HANDLER})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE CRASH_HANDLER)
    if(LINUX)
      # used only in the ArchHooks_Unix.cpp file
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86" OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        target_compile_definitions("${SM_EXE_NAME}" PRIVATE BACKTRACE_METHOD_X86_LINUX)
        target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                   BACKTRACE_METHOD_TEXT="x86 custom backtrace")
      else()
        target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                   BACKTRACE_METHOD_TEXT="no backtrace")
      endif()
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                 BACKTRACE_LOOKUP_METHOD_TEXT="backtrace_symbols")
      if(${DL_FOUND})
        target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                   BACKTRACE_LOOKUP_METHOD_DLADDR)
      else()
        target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                                   BACKTRACE_LOOKUP_METHOD_BACKTRACE_SYMBOLS)
      endif()
    endif()
  endif()
  if(${HAS_PTHREAD})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_LIBPTHREAD)

    message("Host processor is ${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      message("Host processor is 64bit X86")
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_X86_64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86"
           OR CMAKE_SYSTEM_PROCESSOR MATCHES "i686")
      message("Host processor is 32bit X86")
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_X86)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
      message("Host processor is 64bit ARM")
      target_compile_definitions("${SM_EXE_NAME}" PRIVATE CPU_AARCH64)
    else()
      message("Unrecognized host processor type")
    endif()
  endif()
  if(${HAS_FFMPEG})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_FFMPEG)
  endif()
  if(${HAS_XRANDR})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_XRANDR)
  endif()
  if(${HAS_LIBXTST})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_LIBXTST)
  endif()
  if(${HAS_XINERAMA})
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_XINERAMA)
  endif()

  if(BSD)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE BSD)
  endif()

  if(GTK3_FOUND)
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE HAVE_GTK)
  endif()

  target_compile_definitions("${SM_EXE_NAME}" PRIVATE UNIX)
  if("${CMAKE_SYSTEM}" MATCHES "Linux")
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE LINUX)
  endif()
endif()

set_property(TARGET "${SM_EXE_NAME}" PROPERTY FOLDER "Internal Libraries")

list(APPEND SMDATA_LINK_LIB "lua-5.1" "miniz")
include(../extern/CMakeProject-mmmagic.cmake)

if(HAS_MP3)
  if(WITH_SYSTEM_MAD)
    list(APPEND SMDATA_LINK_LIB ${LIBMAD_LIBRARY})
  else()
    list(APPEND SMDATA_LINK_LIB "mad")
  endif()
endif()

if(WITH_SYSTEM_PCRE)
  list(APPEND SMDATA_LINK_LIB ${PCRE_LIBRARY})
else()
  list(APPEND SMDATA_LINK_LIB "pcre")
endif()

if(WITH_SYSTEM_GLEW)
  list(APPEND SMDATA_LINK_LIB ${GLEW_LIBRARIES} ${GLEW_LIBRARY_RELEASE})
  list(APPEND SM_INCLUDE_DIRS ${GLEW_INCLUDE_DIRS})
else()
  list(APPEND SMDATA_LINK_LIB "glew")
endif()

if(WITH_SYSTEM_TOMCRYPT)
  list(APPEND SMDATA_LINK_LIB ${TOMCRYPT_LIBRARY})
else()
  list(APPEND SMDATA_LINK_LIB "tomcrypt")
endif()

if(WITH_SYSTEM_TOMMATH)
  list(APPEND SMDATA_LINK_LIB ${TOMMATH_LIBRARY})
else()
  list(APPEND SMDATA_LINK_LIB "tommath")
endif()

if(WITH_SYSTEM_JPEG)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE SYSTEM_JPEGLIB)
  list(APPEND SMDATA_LINK_LIB ${JPEG_LIBRARIES})
else()
  list(APPEND SMDATA_LINK_LIB "jpeg")
endif()

if(WITH_SYSTEM_ZLIB)
  list(APPEND SMDATA_LINK_LIB ${ZLIB_LIBRARIES} ${ZLIB_LIBRARY_RELEASE})
else()
  list(APPEND SMDATA_LINK_LIB "zlib")
endif()

if(WITH_SYSTEM_PNG)
  target_compile_definitions("${SM_EXE_NAME}" PRIVATE SYSTEM_PNG)
  list(APPEND SMDATA_LINK_LIB ${PNG_LIBRARIES})
else()
  list(APPEND SMDATA_LINK_LIB "png")
endif()

if(WITH_SYSTEM_JSONCPP)
  list(APPEND SMDATA_LINK_LIB ${JSONCPP_LIBRARIES})
else()
  list(APPEND SMDATA_LINK_LIB "jsoncpp")
endif()

if(WITH_OGG)
  if(WITH_SYSTEM_OGG)
    list(APPEND SMDATA_LINK_LIB
                "${VORBISFILE_LIBRARY}"
                "${VORBIS_LIBRARY}"
                "${OGG_LIBRARY}")
  else()
    list(APPEND SMDATA_LINK_LIB
                "vorbisfile"
                "vorbis"
                "ogg")
  endif()
endif()

list(APPEND SMDATA_LINK_LIB "ixwebsocket")

if(WIN32)
  list(APPEND SMDATA_LINK_LIB
              # The misc libraries are here.
              "${LIB_SWSCALE}"
              "${LIB_AVCODEC}"
              "${LIB_AVFORMAT}"
              "${LIB_AVUTIL}")

  list(APPEND SMDATA_LINK_LIB
              "dbghelp.lib"
              "setupapi.lib"
              "hid.lib")

  sm_add_link_flag("${SM_EXE_NAME}" "/LIBPATH:\"${SM_EXTERN_DIR}/ffmpeg-w32/${SM_WIN32_ARCH}\"")
  sm_add_link_flag("${SM_EXE_NAME}"
                   "/LIBPATH:\"${SM_SRC_DIR}/archutils/Win32/ddk/${SM_WIN32_ARCH}\"")
  sm_add_link_flag("${SM_EXE_NAME}" "/ERRORREPORT:SEND")
  sm_add_link_flag("${SM_EXE_NAME}" "/MAPINFO:EXPORTS")
  sm_add_link_flag("${SM_EXE_NAME}" "/SAFESEH:NO")
  sm_add_link_flag("${SM_EXE_NAME}" "/NOLOGO")
  sm_add_link_flag("${SM_EXE_NAME}" "/MAP")
  sm_add_link_flag("${SM_EXE_NAME}" "/NODEFAULTLIB:wininet.lib")
  sm_add_link_flag("${SM_EXE_NAME}" "/NODEFAULTLIB:msimg32.lib")
  sm_add_link_flag("${SM_EXE_NAME}" "/NODEFAULTLIB:libci.lib")
  set_target_properties("${SM_EXE_NAME}"
                        PROPERTIES LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
  set_target_properties("${SM_EXE_NAME}"
                        PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
  set_target_properties("${SM_EXE_NAME}"
                        PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS")

elseif(APPLE)
  # The following were removed from SMDATA_LINK_LIB to "match" StepMania's
  # pbxproj. ${MAC_FRAME_APPKIT} ${MAC_FRAME_FOUNDATION} "ffmpeg"

  list(INSERT SMDATA_LINK_LIB
              0
              ${MAC_FRAME_ACCELERATE}
              ${MAC_FRAME_CARBON}
              ${MAC_FRAME_COCOA}
              ${MAC_FRAME_IOKIT}
              ${MAC_FRAME_OPENGL}
              ${MAC_FRAME_COREFOUNDATION}
              ${MAC_FRAME_AUDIOTOOLBOX}
              ${MAC_FRAME_AUDIOUNIT}
              ${MAC_FRAME_COREAUDIO}
              ${MAC_FRAME_CORESERVICES}
              ${MAC_FRAME_SYSTEM})
  list(APPEND SMDATA_LINK_LIB
              "${BZIP2_LIBRARY_RELEASE}"
              "${ICONV_LIBRARIES}")

  if(HAS_FFMPEG)
    list(APPEND SMDATA_LINK_LIB
                "${SM_FFMPEG_LIB_ARM64}/libavformat.a"
                "${SM_FFMPEG_LIB_ARM64}/libavcodec.a"
                "${SM_FFMPEG_LIB_ARM64}/libswscale.a"
                "${SM_FFMPEG_LIB_ARM64}/libavutil.a"
                "${SM_FFMPEG_LIB_X86_64}/libavformat.a"
                "${SM_FFMPEG_LIB_X86_64}/libavcodec.a"
                "${SM_FFMPEG_LIB_X86_64}/libswscale.a"
                "${SM_FFMPEG_LIB_X86_64}/libavutil.a"
                "${MAC_FRAME_COREMEDIA}"
                "${MAC_FRAME_COREVIDEO}"
                "${MAC_FRAME_VIDEOTOOLBOX}")
  endif()
else() # Unix / Linux TODO: Remember to find and locate the zip archive files.
  if(HAS_FFMPEG)
    if(WITH_SYSTEM_FFMPEG)
      list(APPEND SMDATA_LINK_LIB
                  "${FFMPEG_avformat_LIBRARY}"
                  "${FFMPEG_avcodec_LIBRARY}"
                  "${FFMPEG_swscale_LIBRARY}"
                  "${FFMPEG_avutil_LIBRARY}")
    else()
      list(APPEND SMDATA_LINK_LIB
                  "${SM_FFMPEG_LIB}/libavformat.a"
                  "${SM_FFMPEG_LIB}/libavcodec.a"
                  "${SM_FFMPEG_LIB}/libswscale.a"
                  "${SM_FFMPEG_LIB}/libavutil.a")
    endif()

    if(NOT WITH_SYSTEM_ZLIB)
      list(REMOVE_ITEM SMDATA_LINK_LIB "zlib")
      list(APPEND SMDATA_LINK_LIB "zlib")
    endif()

    if(NOT WITH_SYSTEM_FFMPEG)
      list(APPEND SMDATA_LINK_LIB ${VA_LIBRARY})
      list(APPEND SMDATA_LINK_LIB ${VA_DRM_LIBRARY})
    endif()
  endif()

  if(${DL_FOUND})
    list(APPEND SMDATA_LINK_LIB ${DL_LIBRARIES})
  endif()

  list(APPEND SMDATA_LINK_LIB "${OPENGL_LIBRARY}")

  if(GTK3_FOUND)
    list(APPEND SMDATA_LINK_LIB "${GTK3_LIBRARIES}")
  endif()

  list(APPEND SMDATA_LINK_LIB "${BZIP2_LIBRARIES}" "${CMAKE_THREAD_LIBS_INIT}")

  if(HAS_ALSA)
    list(APPEND SMDATA_LINK_LIB ${ALSA_LIBRARIES})
  endif()

  if(HAS_JACK)
    list(APPEND SMDATA_LINK_LIB ${JACK_LIBRARIES})
  endif()

  if(HAS_OSS)
    # No mention of OSS libraries.
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE "HAVE_OSS_VERSION=1")
  endif()

  if(HAS_PULSE)
    list(APPEND SMDATA_LINK_LIB ${PULSEAUDIO_LIBRARY})
    # PACKAGE_NAME and PACKAGE_VERSION are only used in this scenario. Why is
    # not clear. TODO: Remove this silliness.
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE PACKAGE_NAME="ITGmania")
    set(PACKAGE_VERSION "${SM_VERSION_MAJOR}.${SM_VERSION_MINOR}")
    target_compile_definitions("${SM_EXE_NAME}" PRIVATE
                               PACKAGE_VERSION="${PACKAGE_VERSION}")
  endif()

  if(X11_FOUND)
    list(APPEND SMDATA_LINK_LIB ${X11_LIBRARIES})
  endif()

  if(PCRE_FOUND)
    list(APPEND SMDATA_LINK_LIB ${PCRE_LIBRARY})
  endif()
  
  if(LIBXTST_FOUND)
    list(APPEND SMDATA_LINK_LIB ${LIBXTST_LIBRARY})
  endif()

  list(APPEND SMDATA_LINK_LIB ${XRANDR_LIBRARIES} ${XINERAMA_LIBRARIES})

  list(REMOVE_DUPLICATES SMDATA_LINK_LIB)
endif()

target_link_libraries("${SM_EXE_NAME}" ${SMDATA_LINK_LIB})

list(APPEND SM_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            "${SM_SRC_DIR}/generated"
            "${SM_EXTERN_DIR}/miniz")

list(APPEND SM_INCLUDE_DIRS "${JPEG_INCLUDE_DIR}")

if(NOT APPLE)
  if(NOT WITH_SYSTEM_GLEW)
    list(APPEND SM_INCLUDE_DIRS
                "${SM_EXTERN_DIR}/glew-1.5.8/include")
  endif()

  if(WITH_SYSTEM_JSONCPP)
    list(APPEND SM_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIRS})
  else()
    list(APPEND SM_INCLUDE_DIRS
                "${SM_EXTERN_DIR}/jsoncpp/include")
  endif()

  if(NOT WITH_SYSTEM_ZLIB)
    list(APPEND SM_INCLUDE_DIRS
                "${SM_EXTERN_DIR}/zlib")
  endif()

  if(MSVC)
    if(WITH_OGG)
      list(APPEND SM_INCLUDE_DIRS
                  "${SM_EXTERN_DIR}/newogg/include"
                  "${SM_EXTERN_DIR}/newvorbis/lib"
                  "${SM_EXTERN_DIR}/newvorbis/include")
    endif()
    list(APPEND SM_INCLUDE_DIRS "${SM_EXTERN_DIR}/ffmpeg-w32/include")
  else()
    if(WITH_OGG AND NOT WITH_SYSTEM_OGG)
      list(APPEND SM_INCLUDE_DIRS "${SM_EXTERN_DIR}/vorbis")
    endif()
    if(HAS_FFMPEG)
      if(WITH_SYSTEM_FFMPEG)
        list(APPEND SM_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIR}")
      else()
        add_dependencies("${SM_EXE_NAME}" "ffmpeg")
        list(APPEND SM_INCLUDE_DIRS "${SM_FFMPEG_INCLUDE}")
      endif()
    endif()
    if(HAS_GTK3)
      list(APPEND SM_INCLUDE_DIRS "${GTK3_INCLUDE_DIRS}")
    endif()
    if(X11_FOUND)
      list(APPEND SM_INCLUDE_DIRS "${X11_INCLUDE_DIR}")
    endif()
    if(PCRE_FOUND)
      list(APPEND SM_INCLUDE_DIRS "${PCRE_INCLUDE_DIR}")
    endif()
    if(DL_FOUND)
      list(APPEND SM_INCLUDE_DIRS "${DL_INCLUDE_DIR}")
    endif()
  endif()
else()
  if(WITH_OGG)
    list(APPEND SM_INCLUDE_DIRS "${SM_EXTERN_DIR}/newogg/include"
                "${SM_EXTERN_DIR}/newvorbis/include")
  endif()
  list(APPEND SM_INCLUDE_DIRS "archutils/Unix")
  if(HAS_FFMPEG)
    add_dependencies("${SM_EXE_NAME}" "ffmpeg_arm64" "ffmpeg_x86_64")
    list(APPEND SM_INCLUDE_DIRS "${SM_FFMPEG_INCLUDE}")
  endif()
endif()

if(WIN32)
  # FIXME: This makes no sense...
  #list(APPEND SM_INCLUDE_DIRS ${DIRECTX_INCLUDE_DIR})
endif()

if(NOT WITH_SYSTEM_PNG)
  list(APPEND SM_INCLUDE_DIRS "${SM_EXTERN_DIR}/libpng")
endif()

if(NOT WITH_SYSTEM_TOMCRYPT)
  list(APPEND SM_INCLUDE_DIRS "${SM_SRC_DIR}/libtomcrypt/src/headers")
endif()

target_include_directories("${SM_EXE_NAME}" PUBLIC ${SM_INCLUDE_DIRS})

if(WIN32)
  set(SM_INSTALL_DESTINATION ".")
else()
  set(SM_INSTALL_DESTINATION "itgmania")
endif()

if(WIN32)
  set(SM_FULL_INSTALLATION_PATH_LIST "${SM_INSTALL_DESTINATION}" "Program")
  list(JOIN SM_FULL_INSTALLATION_PATH_LIST "/" SM_FULL_INSTALLATION_PATH)
  # Hardcoding the values for now since the foreach loop is not working as
  # intended.
  install(TARGETS "${SM_EXE_NAME}" DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  if(WITH_FULL_RELEASE OR WITH_TEXTURE_GENERATOR)
    install(FILES "${SM_PROGRAM_DIR}/Texture Font Generator.exe"
            DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  endif()
  install(FILES "${SM_PROGRAM_DIR}/avcodec-59.dll"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  install(FILES "${SM_PROGRAM_DIR}/avformat-59.dll"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  install(FILES "${SM_PROGRAM_DIR}/avutil-57.dll"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  install(FILES "${SM_PROGRAM_DIR}/parallel_lights_io.dll"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  install(FILES "${SM_PROGRAM_DIR}/swscale-6.dll"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
  install(FILES "${SM_PROGRAM_DIR}/ITGmania.vdi"
          DESTINATION "${SM_FULL_INSTALLATION_PATH}")
else()
  install(TARGETS "${SM_EXE_NAME}" DESTINATION "${SM_INSTALL_DESTINATION}")
endif()
install(DIRECTORY "${SM_ROOT_DIR}/Announcers"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/BGAnimations"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Themes"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Characters"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Scripts"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Courses"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/BackgroundEffects"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Data"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/BackgroundTransitions"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/Docs"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(DIRECTORY "${SM_ROOT_DIR}/NoteSkins"
        DESTINATION "${SM_INSTALL_DESTINATION}")
install(FILES "${SM_ROOT_DIR}/Songs/instructions.txt"
        DESTINATION "${SM_INSTALL_DESTINATION}/Songs")

if(WITH_FULL_RELEASE)
  # Fetch ClubFantastic packs
  include(FetchContent)
  FetchContent_Declare(
    ClubFantastic
    URL https://download.clubfantastic.dance/club_fantastic_season2_songs_9ms.zip
    URL_HASH SHA256=845b62da6bb2994fc2ba7871b35da02f6eb36ea1ae78c2b9a59a6ec046946787
  )
  FetchContent_MakeAvailable(ClubFantastic)
  FetchContent_GetProperties(ClubFantastic SOURCE_DIR CLUB_FANTASTIC_DIR)

  install(DIRECTORY "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 1"
          DESTINATION "${SM_INSTALL_DESTINATION}/Songs")
  install(DIRECTORY "${CLUB_FANTASTIC_DIR}/Club Fantastic Season 2"
          DESTINATION "${SM_INSTALL_DESTINATION}/Songs")
else()
  install(DIRECTORY "${SM_ROOT_DIR}/Songs/StepMania 5"
          DESTINATION "${SM_INSTALL_DESTINATION}/Songs")
endif()
