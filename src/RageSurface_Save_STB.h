#ifndef RAGE_SURFACE_SAVE_STB_H
#define RAGE_SURFACE_SAVE_STB_H

#include <string>

struct RageSurface;
class RageFile;

enum class RageSurfaceSTBWriteFormat {
  BMP,
  JPEG,
  PNG,
};

bool RageSurface_Save_STB(
    RageSurface* surface, RageFile& f, std::string& error,
    RageSurfaceSTBWriteFormat format, int quality = 100);

#endif