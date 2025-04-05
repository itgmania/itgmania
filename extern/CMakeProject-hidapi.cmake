set(HIDAPI_HPP "hidapi/hidapi/hidapi.h")

if(WIN32)
    set(HIDAPI_SRC "hidapi/windows/hid.c" "hidapi/windows/hidapi_descriptor_reconstruct.c")
	list(APPEND HIDAPI_HPP 
			"hidapi/windows/hidapi_cfgmgr32.h"
			"hidapi/windows/hidapi_descriptor_reconstruct.h"
			"hidapi/windows/hidapi_hidclass.h"
			"hidapi/windows/hidapi_hidpi.h"
			"hidapi/windows/hidapi_hidsdi.h"
			"hidapi/windows/hidapi_winapi.h")
elseif(APPLE)
	set(HIDAPI_SRC "hidapi/mac/hid.c")
	list(APPEND HIDAPI_HPP 
			"hidapi/mac/hidapi_darwin.h")
else() #Unix
	set(HIDAPI_SRC "hidapi/linux/hid.c")
endif()

source_group("" FILES ${HIDAPI_SRC} ${HIDAPI_HPP})

add_library("hidapi" STATIC ${HIDAPI_SRC} ${HIDAPI_HPP})

set_property(TARGET "hidapi" PROPERTY FOLDER "External Libraries")

if(MSVC)
  target_compile_definitions("hidapi" PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories("hidapi" PUBLIC "hidapi/hidapi")