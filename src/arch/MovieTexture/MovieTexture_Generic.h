#ifndef RAGE_MOVIE_TEXTURE_GENERIC_H
#define RAGE_MOVIE_TEXTURE_GENERIC_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "MovieTexture.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTypes.h"

class FFMpeg_Helper;
struct RageSurface;
struct RageTextureLock;
class RageTextureRenderTarget;
class Sprite;

enum MovieDecoderPixelFormatYCbCr {
  PixelFormatYCbCr_YUYV422,
  NUM_PixelFormatYCbCr,
  PixelFormatYCbCr_Invalid
};

class MovieDecoder {
 public:
  virtual ~MovieDecoder() {}

  virtual std::string Open(std::string file) = 0;
  virtual void Close() = 0;
  virtual void Rewind() = 0;
  virtual void Rollover() = 0;

  // Decode the next frame.
  // Return 1 on success, 0 on EOF, -1 on fatal error, -2 on cancel.
  virtual int DecodeFrame() = 0;
  virtual int DecodeMovie() = 0;

  // Returns true if the frame we want to display has been decoded already.
  virtual bool IsCurrentFrameReady() = 0;

  /*
   * Get the currently-decoded frame.
   */
  virtual int GetFrame(RageSurface* surface_out) = 0;

  /* Return the dimensions of the image, in pixels (before aspect ratio
   * adjustments). */
  virtual int GetWidth() const = 0;
  virtual int GetHeight() const = 0;

  /* Return the aspect ratio of a pixel in the image.  Usually 1. */
  virtual float GetSourceAspectRatio() const { return 1.0f; }

  /*
   * Create a surface acceptable to pass to GetFrame.  This should be
   * a surface which is realtime-compatible with DISPLAY, and should
   * attempt to obey bPreferHighColor.  The given size will usually be
   * the next power of two higher than GetWidth/GetHeight, but on systems
   * with limited texture resolution, may be smaller.
   *
   * If DISPLAY supports the EffectMode_YUYV422 blend mode, this may be
   * a packed-pixel YUV surface.  UYVY maps to RGBA, respectively.  If
   * used, set fmtout.
   */
  virtual RageSurface* CreateCompatibleSurface(
      int iTextureWidth, int iTextureHeight, bool bPreferHighColor,
      MovieDecoderPixelFormatYCbCr& fmtout) = 0;

  /* The following functions return information about the current frame,
   * decoded by the last successful call to GetFrame, and will never be
   * called before that. */

  /* Get the timestamp, in seconds, when the current frame should be
   * displayed.  The first frame will always be 0. */
  virtual float GetTimestamp() const = 0;

  // Cancels the decoding of the movie.
  virtual void Cancel() = 0;

  // Sets the looping property on the decoder.
  virtual void SetLooping(bool loop) = 0;

  // Returns true if the the final frame was displayed.
  virtual bool EndOfMovie() = 0;
};

class MovieTexture_Generic : public RageMovieTexture {
 public:
  MovieTexture_Generic(
      RageTextureID ID, std::unique_ptr<MovieDecoder> pDecoder);
  ~MovieTexture_Generic() override;
  std::string Init();

  /* only called by RageTextureManager::InvalidateTextures */
  void Invalidate() override;

  void Reload() override;

  void SetPosition(float seconds) override;

  // UpdateMovie tells the MovieTexture to update the displayed frame based
  // on fSeconds passed in. (e.g., 5.9 input means show the frame that should
  // be displayed 5.9 seconds into the movie).
  void UpdateMovie(float seconds) override;
  void SetPlaybackRate(float rate) override { rate_ = rate; }
  void SetLooping(bool looping = true) override {
    loop_ = looping;
    if (decoder_) {
      decoder_->SetLooping(looping);
    }
  }
  uintptr_t GetTexHandle() const override;

  static EffectMode GetEffectMode(MovieDecoderPixelFormatYCbCr fmt);

 private:
  std::unique_ptr<MovieDecoder> decoder_;

  std::unique_ptr<std::thread> decoding_thread_;

  /* Current playback rate multiplier (1.0 = normal speed). */
  float rate_;
  /* True if the video is set to loop seamlessly upon reaching EOF. */
  bool loop_;
  /* True if non-looping playback reached the end of the movie. */
  bool finished_ = false;
  /* True once the first frame has been uploaded to the texture, ensuring
   * banners and song wheel previews display immediately without delay. */
  bool first_frame_displayed_ = false;

  // If true, halts all decoding and display due to a fatal decode error.
  bool failure_ = false;

  uintptr_t texture_handle_;
  std::unique_ptr<RageTextureRenderTarget> render_target_;
  std::unique_ptr<RageTexture> intermediate_texture_;
  std::unique_ptr<Sprite> sprite_;

  RageSurface* surface_;
  RageTextureLock* texture_lock_;

  /* The engine playback clock (in seconds) tracking elapsed movie display time.
   */
  float clock_;

  void UpdateFrame();

  void CreateTexture();
  void DestroyTexture();

  float CheckFrameTime();
};

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
