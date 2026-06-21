#include "RageSurface_Load_STB.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "RageFile.h"
#include "RageSurface.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "stb_image.h"

namespace {
const char* GetFormatName(RageSurfaceSTBFormat format) {
  switch (format) {
    case RageSurfaceSTBFormat::BMP:
      return "BMP";
    case RageSurfaceSTBFormat::GIF:
      return "GIF";
    case RageSurfaceSTBFormat::JPEG:
      return "JPEG";
    case RageSurfaceSTBFormat::PNG:
      return "PNG";
  }

  return "image";
}

int GetSignatureSize(RageSurfaceSTBFormat format) {
  switch (format) {
    case RageSurfaceSTBFormat::BMP:
      return 2;
    case RageSurfaceSTBFormat::GIF:
      return 6;
    case RageSurfaceSTBFormat::JPEG:
      return 3;
    case RageSurfaceSTBFormat::PNG:
      return 8;
  }

  return 0;
}

bool SignatureMatches(
    RageSurfaceSTBFormat format, const unsigned char* header, int bytes_read) {
  switch (format) {
    case RageSurfaceSTBFormat::BMP:
      return bytes_read >= 2 && header[0] == 'B' && header[1] == 'M';
    case RageSurfaceSTBFormat::GIF:
      return bytes_read >= 6 && (!std::memcmp(header, "GIF87a", 6) ||
                                 !std::memcmp(header, "GIF89a", 6));
    case RageSurfaceSTBFormat::JPEG:
      return bytes_read >= 3 && header[0] == 0xFF && header[1] == 0xD8 &&
             header[2] == 0xFF;
    case RageSurfaceSTBFormat::PNG: {
      static const unsigned char kPngSignature[8] = {0x89, 'P',  'N',  'G',
                                                     0x0D, 0x0A, 0x1A, 0x0A};
      return bytes_read >= 8 && !std::memcmp(header, kPngSignature, 8);
    }
  }

  return false;
}

bool RewindFile(RageFile& file, std::string& error) {
  file.ClearError();

  const int ret = file.Seek(0);
  if (ret == -1) {
    error = file.GetError();
    return false;
  }
  if (ret != 0) {
    error = "Unexpected seek failure";
    return false;
  }

  return true;
}

RageSurfaceUtils::OpenResult ProbeFileFormat(
    RageFile& file, RageSurfaceSTBFormat format, std::string& error) {
  unsigned char header[8] = {};
  const int expected = GetSignatureSize(format);
  const int got = file.Read(header, expected);
  if (got == -1) {
    error = file.GetError();
    return RageSurfaceUtils::OPEN_FATAL_ERROR;
  }

  if (!RewindFile(file, error)) {
    return RageSurfaceUtils::OPEN_FATAL_ERROR;
  }

  if (!SignatureMatches(format, header, got)) {
    error = ssprintf("not a %s", GetFormatName(format));
    return RageSurfaceUtils::OPEN_UNKNOWN_FILE_FORMAT;
  }

  return RageSurfaceUtils::OPEN_OK;
}

struct StbIoState {
  RageFile* file;
  bool io_failed;
};

int StbRead(void* user, char* data, int size) {
  StbIoState* state = static_cast<StbIoState*>(user);
  const int got = state->file->Read(data, static_cast<size_t>(size));
  if (got == -1) {
    state->io_failed = true;
    return 0;
  }
  return got;
}

void StbSkip(void* user, int size) {
  StbIoState* state = static_cast<StbIoState*>(user);
  if (size == 0) {
    return;
  }

  const int pos = state->file->Tell();
  if (pos == -1) {
    state->io_failed = true;
    return;
  }

  const int target = pos + size;
  const int ret = state->file->Seek(target);
  if (ret != target) {
    state->io_failed = true;
  }
}

int StbEof(void* user) {
  StbIoState* state = static_cast<StbIoState*>(user);
  return state->io_failed || state->file->AtEOF();
}

const stbi_io_callbacks kStbCallbacks = {StbRead, StbSkip, StbEof};

RageSurface* CreateHeaderOnlySurface(int width, int height) {
  return CreateSurfaceFrom(width, height, 32, 0, 0, 0, 0, nullptr, width * 4);
}

void InitializeGrayscalePalette(RageSurface* img) {
  for (int i = 0; i < 256; ++i) {
    RageSurfaceColor color;
    color.r = static_cast<uint8_t>(i);
    color.g = static_cast<uint8_t>(i);
    color.b = static_cast<uint8_t>(i);
    color.a = 0xFF;
    img->fmt.palette->colors[i] = color;
  }
}

RageSurface* CreateSurfaceFromStbPixels(
    const stbi_uc* pixels, int width, int height, int channels) {
  RageSurface* img = nullptr;

  switch (channels) {
    case 1:
      img = CreateSurface(width, height, 8, 0, 0, 0, 0);
      InitializeGrayscalePalette(img);
      break;
    case 2:
      img = CreateSurface(
          width, height, 32, Swap32BE(0xFF000000), Swap32BE(0x00FF0000),
          Swap32BE(0x0000FF00), Swap32BE(0x000000FF));
      break;
    case 3:
      img = CreateSurface(
          width, height, 24, Swap24BE(0xFF0000), Swap24BE(0x00FF00),
          Swap24BE(0x0000FF), 0);
      break;
    case 4:
      img = CreateSurface(
          width, height, 32, Swap32BE(0xFF000000), Swap32BE(0x00FF0000),
          Swap32BE(0x0000FF00), Swap32BE(0x000000FF));
      break;
    default:
      return nullptr;
  }

  if (channels == 2) {
    for (int y = 0; y < height; ++y) {
      const stbi_uc* src = pixels + static_cast<size_t>(y) * width * 2;
      uint8_t* dst = img->pixels + static_cast<size_t>(y) * img->pitch;
      for (int x = 0; x < width; ++x) {
        const uint8_t gray = src[x * 2 + 0];
        dst[x * 4 + 0] = gray;
        dst[x * 4 + 1] = gray;
        dst[x * 4 + 2] = gray;
        dst[x * 4 + 3] = src[x * 2 + 1];
      }
    }
    return img;
  }

  const size_t row_bytes = static_cast<size_t>(width) * channels;
  for (int y = 0; y < height; ++y) {
    std::memcpy(
        img->pixels + static_cast<size_t>(y) * img->pitch,
        pixels + static_cast<size_t>(y) * row_bytes, row_bytes);
  }

  return img;
}

std::string GetLoadError(
    const StbIoState& state, RageFile& file, RageSurfaceSTBFormat format) {
  if (state.io_failed) {
    const std::string file_error = file.GetError();
    if (!file_error.empty()) {
      return file_error;
    }
  }

  const char* reason = stbi_failure_reason();
  if (reason != nullptr && reason[0] != 0) {
    return ssprintf("Error loading %s: %s", GetFormatName(format), reason);
  }

  return ssprintf("Error loading %s", GetFormatName(format));
}
}  // namespace

RageSurfaceUtils::OpenResult RageSurface_Load_STB(
    const std::string& sPath, RageSurface*& ret, bool bHeaderOnly,
    std::string& error, RageSurfaceSTBFormat format) {
  RageFile file;
  if (!file.Open(sPath)) {
    error = file.GetError();
    return RageSurfaceUtils::OPEN_FATAL_ERROR;
  }

  ret = nullptr;

  const RageSurfaceUtils::OpenResult probe_result =
      ProbeFileFormat(file, format, error);
  if (probe_result != RageSurfaceUtils::OPEN_OK) {
    return probe_result;
  }

  file.ClearError();

  StbIoState state{&file, false};
  int width = 0;
  int height = 0;
  int channels = 0;

  if (bHeaderOnly) {
    if (!stbi_info_from_callbacks(
            &kStbCallbacks, &state, &width, &height, &channels)) {
      error = GetLoadError(state, file, format);
      return RageSurfaceUtils::OPEN_FATAL_ERROR;
    }

    ret = CreateHeaderOnlySurface(width, height);
    return RageSurfaceUtils::OPEN_OK;
  }

  stbi_uc* pixels = stbi_load_from_callbacks(
      &kStbCallbacks, &state, &width, &height, &channels, 0);
  if (pixels == nullptr) {
    error = GetLoadError(state, file, format);
    return RageSurfaceUtils::OPEN_FATAL_ERROR;
  }

  ret = CreateSurfaceFromStbPixels(pixels, width, height, channels);
  stbi_image_free(pixels);

  if (ret == nullptr) {
    error = ssprintf(
        "Unsupported %s channel count %i", GetFormatName(format), channels);
    return RageSurfaceUtils::OPEN_FATAL_ERROR;
  }

  return RageSurfaceUtils::OPEN_OK;
}