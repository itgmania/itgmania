if(WITH_SYSTEM_MAD)
  find_package(Mad REQUIRED)
else()
  set(MAD_DIR "${SM_EXTERN_DIR}/mad-0.15.1b")

  list(APPEND MAD_SRC
              "${MAD_DIR}/bit.c"
              "${MAD_DIR}/decoder.c"
              "${MAD_DIR}/fixed.c"
              "${MAD_DIR}/frame.c"
              "${MAD_DIR}/huffman.c"
              "${MAD_DIR}/layer12.c"
              "${MAD_DIR}/layer3.c"
              "${MAD_DIR}/stream.c"
              "${MAD_DIR}/synth.c"
              "${MAD_DIR}/timer.c"
              "${MAD_DIR}/version.c")

  list(APPEND MAD_HPP
              "${MAD_DIR}/bit.h"
              "${MAD_DIR}/config.h"
              "${MAD_DIR}/decoder.h"
              "${MAD_DIR}/fixed.h"
              "${MAD_DIR}/frame.h"
              "${MAD_DIR}/global.h"
              "${MAD_DIR}/huffman.h"
              "${MAD_DIR}/layer12.h"
              "${MAD_DIR}/layer3.h"
              "${MAD_DIR}/mad.h"
              "${MAD_DIR}/stream.h"
              "${MAD_DIR}/synth.h"
              "${MAD_DIR}/timer.h"
              "${MAD_DIR}/version.h")

  list(APPEND MAD_DAT
              "${MAD_DIR}/D.dat"
              "${MAD_DIR}/imdct_s.dat"
              "${MAD_DIR}/qc_table.dat"
              "${MAD_DIR}/rq_table.dat"
              "${MAD_DIR}/sf_table.dat")

  source_group("Source Files" FILES ${MAD_SRC})
  source_group("Header Files" FILES ${MAD_HPP})
  source_group("Data Files" FILES ${MAD_DAT})

  add_library("mad" STATIC ${MAD_SRC} ${MAD_HPP} ${MAD_DAT})

  set_property(TARGET "mad" PROPERTY FOLDER "External Libraries")

  disable_project_warnings("mad")

  if(ENDIAN_BIG)
    set(WORDS_BIGENDIAN 1)
  endif()

  target_compile_definitions("mad" PRIVATE $<$<CONFIG:Debug>:DEBUG>)
  target_compile_definitions("mad" PRIVATE HAVE_CONFIG_H)

  if(MSVC)
    target_compile_definitions("mad" PRIVATE _CRT_SECURE_NO_WARNINGS)
    # TODO: Remove the need for this check since it's tied to 32-bit builds from
    # first glance.
    target_compile_definitions("mad" PRIVATE ASO_ZEROCHECK)
    target_compile_definitions("mad" PRIVATE $<$<CONFIG:Debug>:FPM_DEFAULT>)
    if(SM_WIN32_ARCH MATCHES "x64")
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:Release>:FPM_64BIT>)
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:MinSizeRel>:FPM_64BIT>)
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:RelWithDebInfo>:FPM_64BIT>)
    else()
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:Release>:FPM_INTEL>)
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:MinSizeRel>:FPM_INTEL>)
      target_compile_definitions("mad" PRIVATE $<$<CONFIG:RelWithDebInfo>:FPM_INTEL>)
    endif()
    # TODO: Provide a proper define for inline.
    target_compile_definitions("mad" PRIVATE inline=__inline)
  elseif(APPLE OR UNIX)
    target_compile_definitions("mad" PRIVATE FPM_64BIT=1)
  endif(MSVC)

  target_include_directories("mad" PUBLIC "${MAD_DIR}")

  configure_file("${SM_EXTERN_DIR}/config.mad.in.h" "${MAD_DIR}/config.h")
endif()
