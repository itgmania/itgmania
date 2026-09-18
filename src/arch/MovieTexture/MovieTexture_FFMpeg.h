/* MovieTexture_FFMpeg - FFMpeg movie renderer. */

#ifndef RAGE_MOVIE_TEXTURE_FFMPEG_H
#define RAGE_MOVIE_TEXTURE_FFMPEG_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "MovieTexture_Generic.h"

struct RageSurface;

namespace avcodec {
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
};  // namespace avcodec

// Size of buffer for custom FFmpeg AVIO context (128 KB minimizes I/O
// overhead).
constexpr size_t kFFMpegBufferSize = 131072;
constexpr int kSwsFlags = SWS_BILINEAR;

// Holds a decoded, color-converted, and scaled video frame ready for OpenGL
// presentation. Frames are pre-allocated and recycled via frame_pool_ to avoid
// heap allocations in steady state.
struct ConvertedFrame {
  std::vector<uint8_t> data;
  float pts = 0.0f;
  float duration = 0.0f;
  int width = 0;
  int height = 0;
  int pitch = 0;
  bool is_last_frame = false;
};

class MovieTexture_FFMpeg : public MovieTexture_Generic {
 public:
  MovieTexture_FFMpeg(RageTextureID ID);

  static RageSurface* AVCodecCreateCompatibleSurface(
      int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
      int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class RageMovieTextureDriver_FFMpeg : public RageMovieTextureDriver {
 public:
  RageMovieTexture* Create(RageTextureID ID, std::string& sError) override;
  static RageSurface* AVCodecCreateCompatibleSurface(
      int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
      int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class MovieDecoder_FFMpeg : public MovieDecoder {
 public:
  MovieDecoder_FFMpeg();
  ~MovieDecoder_FFMpeg() override;

  std::string Open(std::string file) override;
  void Close() override;

  void Rewind() override;
  void Rollover() override;

  int GetFrame(RageSurface* surface_out) override;

  int DecodeFrame() override { return 0; }

  int DecodeMovie() override;
  bool IsCurrentFrameReady() override;

  int GetWidth() const override {
    return av_stream_codec_ ? av_stream_codec_->width : 0;
  }
  int GetHeight() const override {
    return av_stream_codec_ ? av_stream_codec_->height : 0;
  }

  RageSurface* CreateCompatibleSurface(
      int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
      MovieDecoderPixelFormatYCbCr& fmtout) override;

  float GetTimestamp() const override;

  void Cancel() override;
  void SetLooping(bool loop) override { looping_ = loop; }
  bool EndOfMovie() override;

 private:
  void Init();
  std::string OpenCodec();

  int ReadAndDecodeNextFrame(
      avcodec::AVPacket* packet, avcodec::AVFrame* raw_frame);
  float CalculatePTS(avcodec::AVFrame* frame);
  float CalculateDuration(avcodec::AVFrame* frame);
  void ConvertFrame(avcodec::AVFrame* raw_frame, ConvertedFrame& out_frame);
  void SeekToStart();
  void HandleReset();

  avcodec::AVStream* av_stream_ = nullptr;
  avcodec::AVPixelFormat av_pixel_format_ = avcodec::AV_PIX_FMT_NONE;
  avcodec::SwsContext* av_sws_context_ = nullptr;
  avcodec::AVCodecContext* av_stream_codec_ = nullptr;
  avcodec::AVFormatContext* av_format_context_ = nullptr;

  unsigned char* av_buffer_ = nullptr;
  avcodec::AVIOContext* av_io_context_ = nullptr;

  int target_width_ = 0;
  int target_height_ = 0;
  int target_pitch_ = 0;

  static constexpr size_t kMaxQueueFrames = 4;
  static constexpr size_t kMaxPoolFrames = 6;
  mutable std::mutex queue_mutex_;
  std::condition_variable can_produce_cv_;
  std::condition_variable can_consume_cv_;

  // Complete, ready frames to display.
  std::deque<std::unique_ptr<ConvertedFrame>> ready_queue_;
  // Frames that are ready to be overwritten. Kept in this pool to avoid
  // constant memory reallocation.
  std::vector<std::unique_ptr<ConvertedFrame>> frame_pool_;

  float timestamp_offset_ = 0.0f;
  float last_pts_ = 0.0f;
  float last_duration_ = 0.0f;
  bool first_pts_seen_ = false;

  std::atomic<bool> cancel_{false};
  std::atomic<bool> looping_{false};
  std::atomic<bool> reset_{false};
  std::atomic<bool> end_of_movie_{false};
};

static struct AVPixelFormat_t {
  int bpp;
  uint32_t masks[4];
  avcodec::AVPixelFormat pf;
  bool bHighColor;
  bool bByteSwapOnLittleEndian;
  MovieDecoderPixelFormatYCbCr YUV;
} AVPixelFormats[] = {
    {
        32,
        {0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF},
        avcodec::AV_PIX_FMT_YUYV422,
        false, /* N/A */
        true,
        PixelFormatYCbCr_YUYV422,
    },
    {
        32,
        {0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF},
        avcodec::AV_PIX_FMT_BGRA,
        true,
        true,
        PixelFormatYCbCr_Invalid,
    },
    {
        32,
        {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000},
        avcodec::AV_PIX_FMT_ARGB,
        true,
        true,
        PixelFormatYCbCr_Invalid,
    },
    {
        24,
        {0xFF0000, 0x00FF00, 0x0000FF, 0x000000},
        avcodec::AV_PIX_FMT_RGB24,
        true,
        true,
        PixelFormatYCbCr_Invalid,
    },
    {
        24,
        {0x0000FF, 0x00FF00, 0xFF0000, 0x000000},
        avcodec::AV_PIX_FMT_BGR24,
        true,
        true,
        PixelFormatYCbCr_Invalid,
    },
    {
        16,
        {0x7C00, 0x03E0, 0x001F, 0x0000},
        avcodec::AV_PIX_FMT_RGB555,
        false,
        false,
        PixelFormatYCbCr_Invalid,
    },
    {0,
     {0, 0, 0, 0},
     avcodec::AV_PIX_FMT_NB,
     true,
     false,
     PixelFormatYCbCr_Invalid}};

#endif

/*
 * (c) 2003-2005 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
