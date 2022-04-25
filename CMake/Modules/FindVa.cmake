# Find the Video Acceleration library.

# The following will be set:

# VA_INCLUDE_DIR VA_LIBRARY VA_FOUND
# VA_DRM_INCLUDE_DIR VA_DRM_LIBRARY VA_DRM_FOUND

find_path(VA_INCLUDE_DIR NAMES va/va.h)
find_path(VA_DRM_INCLUDE_DIR NAMES va/va_drm.h)

find_library(VA_LIBRARY NAMES "va" REQUIRED)
find_library(VA_DRM_LIBRARY NAMES "va-drm" REQUIRED)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VA DEFAULT_MSG VA_LIBRARY VA_INCLUDE_DIR)
find_package_handle_standard_args(VA_DRM DEFAULT_MSG VA_DRM_LIBRARY VA_DRM_INCLUDE_DIR)

mark_as_advanced(VA_LIBRARY VA_INCLUDE_DIR
		 VA_DRM_LIBRARY VA_DRM_INCLUDE_DIR)
