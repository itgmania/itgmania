#ifndef RAGE_SURFACE_LOAD_STB_H
#define RAGE_SURFACE_LOAD_STB_H

#include <string>

#include "RageSurface_Load.h"

enum class RageSurfaceSTBFormat {
  BMP,
  GIF,
  JPEG,
  PNG,
};

RageSurfaceUtils::OpenResult RageSurface_Load_STB(
    const std::string& path, RageSurface*& ret, bool header_only,
    std::string& error, RageSurfaceSTBFormat format);

#endif