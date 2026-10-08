#include "MovieTexture_FFMpeg.h"

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "RageDisplay.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageTextureID.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "RageUtil/Endian.h"
#include "arch/MovieTexture/MovieTexture.h"
#include "arch/MovieTexture/MovieTexture_Generic.h"
#include "global.h"

static void FixLilEndian() {
  if constexpr (!Endian::little) {
    return;
  }

  static bool Initialized = false;
  if (Initialized) {
    return;
  }
  Initialized = true;

  for (int i = 0; i < AVPixelFormats[i].bpp; ++i) {
    AVPixelFormat_t& pf = AVPixelFormats[i];

    if (!pf.bByteSwapOnLittleEndian) {
      continue;
    }

    for (int mask = 0; mask < 4; ++mask) {
      int m = pf.masks[mask];
      switch (pf.bpp) {
        case 24:
          m = Swap24(m);
          break;
        case 32:
          m = Swap32(m);
          break;
        default:
          FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp));
      }
      pf.masks[mask] = m;
    }
  }
}

static int FindCompatibleAVFormat(bool bHighColor) {
  if (DISPLAY == nullptr) {
    return -1;
  }
  for (int i = 0; AVPixelFormats[i].bpp; ++i) {
    AVPixelFormat_t& fmt = AVPixelFormats[i];
    if (fmt.YUV != PixelFormatYCbCr_Invalid) {
      EffectMode em = MovieTexture_Generic::GetEffectMode(fmt.YUV);
      if (!DISPLAY->IsEffectModeSupported(em)) {
        continue;
      }
    } else if (fmt.bHighColor != bHighColor) {
      continue;
    }

    RagePixelFormat pixfmt = DISPLAY->FindPixelFormat(
        fmt.bpp, fmt.masks[0], fmt.masks[1], fmt.masks[2], fmt.masks[3],
        true /* realtime */
    );

    if (pixfmt == RagePixelFormat_Invalid) {
      continue;
    }

    return i;
  }

  return -1;
}

RageSurface* RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface(
    int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
    int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout) {
  FixLilEndian();

  int iAVTexfmtIndex = FindCompatibleAVFormat(bPreferHighColor);
  if (iAVTexfmtIndex == -1) {
    iAVTexfmtIndex = FindCompatibleAVFormat(!bPreferHighColor);
  }

  if (iAVTexfmtIndex == -1) {
    /* No dice.  Use the first avcodec format of the preferred bit depth,
     * and let the display system convert. */
    for (iAVTexfmtIndex = 0; AVPixelFormats[iAVTexfmtIndex].bpp;
         ++iAVTexfmtIndex) {
      if (AVPixelFormats[iAVTexfmtIndex].bHighColor == bPreferHighColor) {
        break;
      }
    }
    ASSERT(AVPixelFormats[iAVTexfmtIndex].bpp != 0);
  }

  const AVPixelFormat_t* pfd = &AVPixelFormats[iAVTexfmtIndex];
  iAVTexfmt = pfd->pf;
  fmtout = pfd->YUV;

  LOG->Trace(
      "Texture pixel format: %i %i (%ibpp, %08x %08x %08x %08x)", iAVTexfmt,
      fmtout, pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2],
      pfd->masks[3]);

  if (pfd->YUV == PixelFormatYCbCr_YUYV422) {
    iTextureWidth /= 2;
  }

  return CreateSurface(
      iTextureWidth, iTextureHeight, pfd->bpp, pfd->masks[0], pfd->masks[1],
      pfd->masks[2], pfd->masks[3]);
}

MovieDecoder_FFMpeg::MovieDecoder_FFMpeg() {
  FixLilEndian();
  Init();
}

MovieDecoder_FFMpeg::~MovieDecoder_FFMpeg() {
  Cancel();
  Close();
}

void MovieDecoder_FFMpeg::Init() {
  av_format_context_ = nullptr;
  av_stream_ = nullptr;
  av_sws_context_ = nullptr;
  av_stream_codec_ = nullptr;
  av_io_context_ = nullptr;
  av_buffer_ = nullptr;
  timestamp_offset_ = 0.0f;
  last_pts_ = 0.0f;
  last_duration_ = 0.0f;
  first_pts_seen_ = false;
  target_width_ = 0;
  target_height_ = 0;
  target_pitch_ = 0;
  cancel_ = false;
  looping_ = false;
  reset_ = false;
  end_of_movie_ = false;
}

void MovieDecoder_FFMpeg::Cancel() {
  cancel_ = true;
  can_produce_cv_.notify_all();
  can_consume_cv_.notify_all();
}

void MovieDecoder_FFMpeg::Rewind() {
  reset_ = true;
  can_produce_cv_.notify_all();
  can_consume_cv_.notify_all();
}

void MovieDecoder_FFMpeg::Rollover() {
  // Looping is seamlessly handled inside DecodeMovie()
}

float MovieDecoder_FFMpeg::GetTimestamp() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (ready_queue_.empty()) {
    return last_pts_;
  }
  return ready_queue_.front()->pts;
}

bool MovieDecoder_FFMpeg::IsCurrentFrameReady() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return !ready_queue_.empty();
}

bool MovieDecoder_FFMpeg::EndOfMovie() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return end_of_movie_ && ready_queue_.empty();
}

// Seek back to the beginning of the video stream and reset decoder buffers.
void MovieDecoder_FFMpeg::SeekToStart() {
  if (av_format_context_ && av_stream_) {
    // Reset custom AVIO flags so subsequent av_read_frame calls don't
    // immediately return AVERROR_EOF
    if (av_format_context_->pb) {
      av_format_context_->pb->eof_reached = 0;
      av_format_context_->pb->error = 0;
      avcodec::avio_flush(av_format_context_->pb);
    }
    int64_t start_pts =
        (av_stream_->start_time != AV_NOPTS_VALUE) ? av_stream_->start_time : 0;
    int ret = avcodec::av_seek_frame(
        av_format_context_, av_stream_->index, start_pts,
        AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME);
    if (ret < 0) {
      // Fallback: attempt seeking to absolute timestamp 0
      avcodec::av_seek_frame(
          av_format_context_, av_stream_->index, 0, AVSEEK_FLAG_BACKWARD);
    }
    if (av_stream_codec_) {
      avcodec::avcodec_flush_buffers(av_stream_codec_);
    }
  }
  first_pts_seen_ = false;
  timestamp_offset_ = 0.0f;
  last_pts_ = 0.0f;
  last_duration_ = 0.0f;
}

void MovieDecoder_FFMpeg::HandleReset() {
  reset_ = false;
  SeekToStart();
  std::lock_guard<std::mutex> lock(queue_mutex_);
  while (!ready_queue_.empty()) {
    if (frame_pool_.size() < kMaxPoolFrames) {
      frame_pool_.push_back(std::move(ready_queue_.front()));
    }
    ready_queue_.pop_front();
  }
  end_of_movie_ = false;
  can_produce_cv_.notify_all();
}

// Computes the presentation timestamp (PTS) in seconds.
// Prefers best_effort_timestamp / pts over pkt_dts to handle out-of-order
// B-frames accurately.
float MovieDecoder_FFMpeg::CalculatePTS(avcodec::AVFrame* frame) {
  int64_t pts = frame->best_effort_timestamp;
  if (pts == AV_NOPTS_VALUE) {
    pts = frame->pts;
  }
  if (pts == AV_NOPTS_VALUE) {
    pts = frame->pkt_dts;
  }

  float seconds = 0.0f;
  if (pts != AV_NOPTS_VALUE && av_stream_ && av_stream_->time_base.den > 0) {
    seconds = static_cast<float>(pts * av_q2d(av_stream_->time_base));
  } else {
    seconds = last_pts_ + last_duration_;
  }

  if (!first_pts_seen_) {
    first_pts_seen_ = true;
    timestamp_offset_ = seconds;
  }

  seconds -= timestamp_offset_;
  if (seconds < 0.0f) {
    seconds = 0.0f;
  }

  // Guarantee strictly monotonic presentation timestamps. If legacy containers
  // (e.g. AVI files with B-frames) report backward timestamp anomalies, advance
  // smoothly using frame duration.
  if (first_pts_seen_ && seconds < last_pts_) {
    seconds = last_pts_ + last_duration_;
  }

  last_pts_ = seconds;
  return seconds;
}

float MovieDecoder_FFMpeg::CalculateDuration(avcodec::AVFrame* frame) {
  float duration = 0.0f;
  if (frame->pkt_duration > 0 && av_stream_ && av_stream_->time_base.den > 0) {
    duration =
        static_cast<float>(frame->pkt_duration * av_q2d(av_stream_->time_base));
  } else if (
      av_stream_ && av_stream_->avg_frame_rate.num > 0 &&
      av_stream_->avg_frame_rate.den > 0) {
    duration = static_cast<float>(av_q2d(av_inv_q(av_stream_->avg_frame_rate)));
  } else {
    duration = 1.0f / 30.0f;
  }
  last_duration_ = duration;
  return duration;
}

// Converts pixel format and scales the frame on the worker thread, completely
// avoiding sws_scale execution on the main engine render thread.
void MovieDecoder_FFMpeg::ConvertFrame(
    avcodec::AVFrame* raw_frame, ConvertedFrame& out_frame) {
  int dst_width = GetWidth();
  int dst_height = GetHeight();

  if (av_sws_context_ == nullptr && dst_width > 0 && dst_height > 0) {
    av_sws_context_ = avcodec::sws_getCachedContext(
        av_sws_context_, GetWidth(), GetHeight(), av_stream_codec_->pix_fmt,
        dst_width, dst_height, av_pixel_format_, kSwsFlags, nullptr, nullptr,
        nullptr);
  }

  size_t required_size = static_cast<size_t>(
      target_pitch_ > 0 ? target_pitch_ * target_height_ : 0);
  if (out_frame.data.size() < required_size) {
    out_frame.data.resize(required_size);
  }
  out_frame.width = target_width_;
  out_frame.height = target_height_;
  out_frame.pitch = target_pitch_;

  uint8_t* dst_data[4] = {out_frame.data.data(), nullptr, nullptr, nullptr};
  int dst_linesize[4] = {target_pitch_, 0, 0, 0};

  if (av_sws_context_ != nullptr && !out_frame.data.empty()) {
    avcodec::sws_scale(
        av_sws_context_, raw_frame->data, raw_frame->linesize, 0, GetHeight(),
        dst_data, dst_linesize);
  }
}

int MovieDecoder_FFMpeg::ReadAndDecodeNextFrame(
    avcodec::AVPacket* packet, avcodec::AVFrame* raw_frame) {
  while (!cancel_) {
    if (reset_) {
      return -3;
    }

    int ret = avcodec::avcodec_receive_frame(av_stream_codec_, raw_frame);
    if (ret == 0) {
      return 0;
    }
    if (ret == AVERROR_EOF) {
      return AVERROR_EOF;
    }
    if (ret != AVERROR(EAGAIN)) {
      LOG->Warn("avcodec_receive_frame error: %d", ret);
      return ret;
    }

    bool packet_sent = false;
    while (!packet_sent && !cancel_ && !reset_) {
      avcodec::av_packet_unref(packet);
      ret = avcodec::av_read_frame(av_format_context_, packet);
      if (ret < 0) {
        avcodec::avcodec_send_packet(av_stream_codec_, nullptr);
        packet_sent = true;
        break;
      }

      if (packet->stream_index == av_stream_->index) {
        ret = avcodec::avcodec_send_packet(av_stream_codec_, packet);
        avcodec::av_packet_unref(packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
          LOG->Warn("avcodec_send_packet error: %d", ret);
        }
        packet_sent = true;
        break;
      }
    }

    if (reset_) {
      return -3;
    }
  }
  return -2;
}

int MovieDecoder_FFMpeg::DecodeMovie() {
  avcodec::AVPacket* packet = avcodec::av_packet_alloc();
  avcodec::AVFrame* raw_frame = avcodec::av_frame_alloc();

  while (!cancel_) {
    if (reset_) {
      HandleReset();
    }

    std::unique_ptr<ConvertedFrame> frame_buf;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      can_produce_cv_.wait(lock, [this]() {
        return cancel_ || reset_ || ready_queue_.size() < kMaxQueueFrames;
      });

      if (cancel_) {
        break;
      }
      if (reset_) {
        continue;
      }

      if (!frame_pool_.empty()) {
        frame_buf = std::move(frame_pool_.back());
        frame_pool_.pop_back();
      }
    }

    if (!frame_buf) {
      frame_buf = std::make_unique<ConvertedFrame>();
    }

    int ret = ReadAndDecodeNextFrame(packet, raw_frame);
    if (ret < 0) {
      if (ret == -3) {
        HandleReset();
        continue;
      }
      if (ret == AVERROR_EOF) {
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          if (!ready_queue_.empty()) {
            ready_queue_.back()->is_last_frame = true;
          } else {
            end_of_movie_ = true;
          }
        }
        can_consume_cv_.notify_all();

        if (looping_) {
          SeekToStart();
          continue;
        } else {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          end_of_movie_ = true;
          break;
        }
      }
      if (ret == -2 || cancel_) {
        break;
      }
      LOG->Warn("DecodeMovie: decoding error: %d", ret);
      break;
    }

    float frame_pts = CalculatePTS(raw_frame);
    float frame_duration = CalculateDuration(raw_frame);

    ConvertFrame(raw_frame, *frame_buf);
    frame_buf->pts = frame_pts;
    frame_buf->duration = frame_duration;
    frame_buf->is_last_frame = false;

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      ready_queue_.push_back(std::move(frame_buf));
    }
    can_consume_cv_.notify_all();
  }

  avcodec::av_frame_free(&raw_frame);
  avcodec::av_packet_free(&packet);
  return cancel_ ? -2 : 0;
}

int MovieDecoder_FFMpeg::GetFrame(RageSurface* surface_out) {
  std::unique_ptr<ConvertedFrame> frame;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (ready_queue_.empty()) {
      return -1;
    }
    frame = std::move(ready_queue_.front());
    ready_queue_.pop_front();
  }
  can_produce_cv_.notify_one();

  if (surface_out && surface_out->pixels && !frame->data.empty()) {
    const uint8_t* src = frame->data.data();
    uint8_t* dst = static_cast<uint8_t*>(surface_out->pixels);
    int bytes_per_row = std::min(frame->pitch, surface_out->pitch);
    int rows = std::min(frame->height, surface_out->h);
    for (int y = 0; y < rows; ++y) {
      memcpy(
          dst + y * surface_out->pitch, src + y * frame->pitch, bytes_per_row);
    }
  }

  bool was_last = frame->is_last_frame;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (frame_pool_.size() < kMaxPoolFrames) {
      frame_pool_.push_back(std::move(frame));
    }
  }

  return was_last ? 1 : 0;
}

static std::string averr_ssprintf(int err, const char* fmt, ...) {
  ASSERT(err < 0);

  va_list va;
  va_start(va, fmt);
  std::string s = vssprintf(fmt, va);
  va_end(va);

  size_t errbuf_size = 512;
  char* errbuf = new char[errbuf_size];
  avcodec::av_strerror(err, errbuf, errbuf_size);
  std::string Error = ssprintf("%i: %s", err, errbuf);
  delete[] errbuf;

  return s + " (" + Error + ")";
}

static int AVIORageFile_ReadPacket(void* opaque, uint8_t* buf, int buf_size) {
  RageFile* f = (RageFile*)opaque;
  int n = f->Read(buf, buf_size);
  if (n == 0) {
    return AVERROR_EOF;
  }
  return n;
}

static int64_t AVIORageFile_Seek(void* opaque, int64_t offset, int whence) {
  RageFile* f = (RageFile*)opaque;
  if (whence == AVSEEK_SIZE) {
    return f->GetFileSize();
  }

  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
    LOG->Trace("Error: unsupported seek whence: %d", whence);
    return -1;
  }

  return f->Seek((int)offset, whence);
}

std::string MovieDecoder_FFMpeg::Open(std::string file) {
  av_format_context_ = avcodec::avformat_alloc_context();
  if (!av_format_context_) {
    return "AVCodec: Couldn't allocate context";
  }

  RageFile* f = new RageFile;

  if (!f->Open(file, RageFile::READ)) {
    std::string errorMessage = f->GetError();
    std::string error = ssprintf(
        "MovieDecoder_FFMpeg: Error opening \"%s\": %s", file.c_str(),
        errorMessage.c_str());
    delete f;
    return error;
  }

  av_buffer_ =
      static_cast<unsigned char*>(avcodec::av_malloc(kFFMpegBufferSize));
  av_io_context_ = avcodec::avio_alloc_context(
      av_buffer_, kFFMpegBufferSize, 0, f, AVIORageFile_ReadPacket, nullptr,
      AVIORageFile_Seek);

  av_format_context_->pb = av_io_context_;
  int ret = avcodec::avformat_open_input(
      &av_format_context_, file.c_str(), nullptr, nullptr);
  if (ret < 0) {
    return std::string(
        averr_ssprintf(ret, "AVCodec: Couldn't open \"%s\"", file.c_str()));
  }

  ret = avcodec::avformat_find_stream_info(av_format_context_, nullptr);
  if (ret < 0) {
    return std::string(averr_ssprintf(
        ret, "AVCodec (%s): Couldn't find codec parameters", file.c_str()));
  }

  int stream_idx = avcodec::av_find_best_stream(
      av_format_context_, avcodec::AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_idx < 0 ||
      static_cast<unsigned int>(stream_idx) >= av_format_context_->nb_streams ||
      av_format_context_->streams[stream_idx] == nullptr) {
    return "Couldn't find any video streams";
  }
  av_stream_ = av_format_context_->streams[stream_idx];
  av_stream_codec_ = avcodec::avcodec_alloc_context3(nullptr);
  if (avcodec::avcodec_parameters_to_context(
          av_stream_codec_, av_stream_->codecpar) < 0) {
    return ssprintf("Could not get context from parameters");
  }

  if (av_stream_codec_->codec_id == avcodec::AV_CODEC_ID_NONE) {
    return ssprintf("Unsupported codec %08x", av_stream_codec_->codec_tag);
  }

  std::string sError = OpenCodec();
  if (!sError.empty()) {
    return ssprintf("AVCodec (%s): %s", file.c_str(), sError.c_str());
  }

  LOG->Trace("Bitrate: %i", static_cast<int>(av_stream_codec_->bit_rate));
  LOG->Trace(
      "Codec pixel format: %s",
      avcodec::av_get_pix_fmt_name(av_stream_codec_->pix_fmt));

  return std::string();
}

std::string MovieDecoder_FFMpeg::OpenCodec() {
  ASSERT(av_stream_ != nullptr);
  if (av_stream_codec_->codec) {
    avcodec::avcodec_close(av_stream_codec_);
  }

  const avcodec::AVCodec* pCodec =
      avcodec::avcodec_find_decoder(av_stream_codec_->codec_id);
  if (pCodec == nullptr) {
    return ssprintf("Couldn't find decoder %i", av_stream_codec_->codec_id);
  }

  av_stream_codec_->workaround_bugs = 1;
  av_stream_codec_->idct_algo = FF_IDCT_AUTO;
  av_stream_codec_->error_concealment = 3;

  LOG->Trace("Opening codec %s", pCodec->name);

  int ret = avcodec::avcodec_open2(av_stream_codec_, pCodec, nullptr);
  if (ret < 0) {
    return std::string(
        averr_ssprintf(ret, "Couldn't open codec \"%s\"", pCodec->name));
  }
  ASSERT(av_stream_codec_->codec != nullptr);

  return std::string();
}

// Safely cleans up all FFmpeg contexts and custom I/O allocations.
// Note: avio_context_free() frees the internal AVIO buffer, avoiding
// double-free issues. av_format_context_->pb is nulled before
// avformat_close_input to prevent premature buffer deallocation.
void MovieDecoder_FFMpeg::Close() {
  if (av_sws_context_) {
    avcodec::sws_freeContext(av_sws_context_);
    av_sws_context_ = nullptr;
  }
  if (av_stream_codec_) {
    // avcodec_free_context automatically closes the codec context if open
    avcodec::avcodec_free_context(&av_stream_codec_);
    av_stream_codec_ = nullptr;
  }
  if (av_format_context_) {
    av_format_context_->pb = nullptr;
    avcodec::avformat_close_input(&av_format_context_);
    av_format_context_ = nullptr;
  }
  if (av_io_context_ != nullptr) {
    RageFile* file = static_cast<RageFile*>(av_io_context_->opaque);
    if (file != nullptr) {
      file->Close();
      delete file;
    }
    avcodec::avio_context_free(&av_io_context_);
    av_io_context_ = nullptr;
    av_buffer_ = nullptr;
  }
  if (av_buffer_ != nullptr) {
    avcodec::av_freep(&av_buffer_);
    av_buffer_ = nullptr;
  }
  av_stream_ = nullptr;

  std::lock_guard<std::mutex> lock(queue_mutex_);
  ready_queue_.clear();
  frame_pool_.clear();
}

RageSurface* MovieDecoder_FFMpeg::CreateCompatibleSurface(
    int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
    MovieDecoderPixelFormatYCbCr& fmtout) {
  RageSurface* pSurf =
      RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface(
          iTextureWidth, iTextureHeight, bPreferHighColor,
          *ConvertValue<int>(&av_pixel_format_), fmtout);

  if (pSurf != nullptr) {
    target_width_ = pSurf->w;
    target_height_ = pSurf->h;
    target_pitch_ = pSurf->pitch;
  }

  return pSurf;
}

MovieTexture_FFMpeg::MovieTexture_FFMpeg(RageTextureID ID)
    : MovieTexture_Generic(ID, std::make_unique<MovieDecoder_FFMpeg>()) {}

RageMovieTexture* RageMovieTextureDriver_FFMpeg::Create(
    RageTextureID ID, std::string& sError) {
  MovieTexture_FFMpeg* pRet = new MovieTexture_FFMpeg(ID);
  sError = pRet->Init();
  if (!sError.empty()) {
    RageUtil::SafeDelete(pRet);
  }
  return pRet;
}

REGISTER_MOVIE_TEXTURE_CLASS(FFMpeg);

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
