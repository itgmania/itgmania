#include "RageSurface_Save_STB.h"

#include <cstdint>
#include <string>

#include "RageFile.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageUtil/Endian.h"
#include "stb_image_write.h"

namespace {
struct StbWriteState {
  RageFile* file;
  std::string* error;
  bool io_failed;
};

void StbWriteCallback(void* context, void* data, int size) {
  StbWriteState* state = static_cast<StbWriteState*>(context);
  if (state->io_failed || size <= 0) {
    return;
  }

  const int wrote = state->file->Write(data, static_cast<size_t>(size));
  if (wrote == size) {
    return;
  }

  state->io_failed = true;
  if (!state->error->empty()) {
    return;
  }

  const std::string file_error = state->file->GetError();
  if (!file_error.empty()) {
    *state->error = file_error;
  } else {
    *state->error = "Image write failed";
  }
}

const char* GetFormatName(RageSurfaceSTBWriteFormat format) {
  switch (format) {
    case RageSurfaceSTBWriteFormat::BMP:
      return "BMP";
    case RageSurfaceSTBWriteFormat::JPEG:
      return "JPEG";
    case RageSurfaceSTBWriteFormat::PNG:
      return "PNG";
  }

  return "image";
}

bool WriteSurface(
    RageSurface* surface, RageFile& f, std::string& error,
    RageSurfaceSTBWriteFormat format, int quality) {
  StbWriteState state{&f, &error, false};
  stbi_flip_vertically_on_write(0);

  int result = 0;
  switch (format) {
    case RageSurfaceSTBWriteFormat::BMP:
      result = stbi_write_bmp_to_func(
          StbWriteCallback, &state, surface->w, surface->h, 3, surface->pixels);
      break;
    case RageSurfaceSTBWriteFormat::JPEG:
      result = stbi_write_jpg_to_func(
          StbWriteCallback, &state, surface->w, surface->h, 3, surface->pixels,
          quality);
      break;
    case RageSurfaceSTBWriteFormat::PNG: {
      const int previous_level = stbi_write_png_compression_level;
      stbi_write_png_compression_level = 1;
      result = stbi_write_png_to_func(
          StbWriteCallback, &state, surface->w, surface->h, 3, surface->pixels,
          surface->pitch);
      stbi_write_png_compression_level = previous_level;
      break;
    }
  }

  if (state.io_failed) {
    return false;
  }

  if (result == 0) {
    if (error.empty()) {
      error = std::string("Failed to save ") + GetFormatName(format);
    }
    return false;
  }

  if (f.Flush() == -1) {
    if (error.empty()) {
      error = f.GetError();
    }
    return false;
  }

  return true;
}
}  // namespace

bool RageSurface_Save_STB(
    RageSurface* surface, RageFile& f, std::string& error,
    RageSurfaceSTBWriteFormat format, int quality) {
  RageSurface* converted_surface = CreateSurface(
      surface->w, surface->h, 24, Swap24BE(0xFF0000), Swap24BE(0x00FF00),
      Swap24BE(0x0000FF), 0);
  RageSurfaceUtils::CopySurface(surface, converted_surface);

  const bool wrote = WriteSurface(converted_surface, f, error, format, quality);
  delete converted_surface;
  return wrote;
}