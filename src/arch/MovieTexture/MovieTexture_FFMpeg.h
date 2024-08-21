/* MovieTexture_FFMpeg - FFMpeg movie renderer. */

#ifndef RAGE_MOVIE_TEXTURE_FFMPEG_H
#define RAGE_MOVIE_TEXTURE_FFMPEG_H

#include "MovieTexture_Generic.h"

#include <cstdint>
#include <mutex>

struct RageSurface;

namespace avcodec
{
	extern "C"
	{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
	}
};

#define STEPMANIA_FFMPEG_BUFFER_SIZE 4096
static const int kSwsFlags = SWS_BICUBIC; // XXX: Reasonable default?

struct FrameHolder {
	avcodec::AVFrame* frame = avcodec::av_frame_alloc();
	bool displayed = false;
	int packet_num = -1; // Used as a sanity check during display.
	std::mutex lock; // Protects the frame as it's being initialized.
	~FrameHolder() {
		if (frame != nullptr) {
			avcodec::av_frame_free(&frame);
		}
	}
};

struct PacketHolder {
	avcodec::AVPacket* packet = avcodec::av_packet_alloc();
	float frame_timestamp = 0;
	float frame_delay = 0;
	bool decoded = false;
	std::mutex lock; // Protects the packet as it's being initialized.

	PacketHolder() = default;

	~PacketHolder() {
		if (packet != nullptr) {
			avcodec::av_packet_free(&packet);
		}
	}
};

class MovieTexture_FFMpeg : public MovieTexture_Generic
{
public:
	MovieTexture_FFMpeg(RageTextureID ID);

	static RageSurface* AVCodecCreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class RageMovieTextureDriver_FFMpeg : public RageMovieTextureDriver
{
public:
	virtual RageMovieTexture* Create(RageTextureID ID, RString& sError);
	static RageSurface* AVCodecCreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout);
};

class MovieDecoder_FFMpeg : public MovieDecoder
{
public:
	MovieDecoder_FFMpeg();
	~MovieDecoder_FFMpeg();

	RString Open(RString sFile);
	void Close();

	// Rewind sends the reset signal to DecodeMovie. See DecodeMovie
	// and HandleReset for more information.
	void Rewind();

	// Like rewind, but handles the case that a looping video reached the end,
	// and the next frame to display is the first one of the movie.
	void Rollover();

	// This draws a frame from the buffer onto the provided RageSurface.
	// Returns 1 if the last frame of the movie, -1 if there's an issue
	// with the frame and we should skip.
	int GetFrame(RageSurface* pOut);

	// Handles the next packet in decoding.
	int HandleNextPacket();

	// Decode a single frame.
	// Return -2 on cancel
	//        -1 on error
	//         0 on success
	//         1 on success and end_of_file_ set
	int DecodeFrame();

	// Decode the entire movie.
	// Works via a sliding window between packet_buffer_ and frame_buffer_.
	// The frame_buffer_, when full, will not reuse a FrameHolder until the
	// frame at frame_buffer_position_ has been displayed.
	//
	// Returns 0 on success, -1 on fatal error, -2 on cancel. Looping movies
	// never exit until destruction.
	int DecodeMovie();
	bool IsCurrentFrameReady();

	int GetWidth() const { return av_stream_codec_->width; }
	int GetHeight() const { return av_stream_codec_->height; }

	RageSurface* CreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, MovieDecoderPixelFormatYCbCr& fmtout);

	float GetTimestamp() const;

	// Cancel decoding.
	void Cancel() { cancel_ = true; };

	// Called by the MovieTexture to tell the decoder if the movie loops.
	void SetLooping(bool loop) { looping_ = loop; }

	// Are we displaying the last frame?
	bool LastFrame() { return display_frame_num_ == (total_frames_ - 1); }

	// The signal if the final frame of the movie was just displayed.
	bool EndOfMovie() { return end_of_movie_; }

private:
	void Init();
	RString OpenCodec();

	// Read a packet and send it to our frame data buffer.
	// Returns -2 on cancel, -1 on error, 0 on EOF, 1 on OK.
	int SendPacketToBuffer();

	// Send the packet at packet_buffer_position_ to the frame buffer
	// at the next open position.
	// Returns -2 on cancel, -1 on error, 0 if the packet is finished.
	int DecodePacketToFrame();
	void HandleReset();

	avcodec::AVStream* av_stream_;
	avcodec::AVPixelFormat av_pixel_format_;	/* pixel format of output surface */
	avcodec::SwsContext* av_sws_context_;
	avcodec::AVCodecContext* av_stream_codec_;
	avcodec::AVFormatContext* av_format_context_;
	int total_frames_; // Total number of frames in the movie.

	unsigned char* av_buffer_;
	avcodec::AVIOContext* av_io_context_;

	// The movie's buffers. This uses a sliding window from FrameBuffer to
	// PacketBuffer. AVPackets are small enough that keeping the whole movie's
	// packets in memory is trivial, but AVFrames can quickly overwhelm RAM.
	// Therefore, the FrameBuffer represents a sliding window along the PacketBuffer.
	std::vector<std::unique_ptr<PacketHolder>> packet_buffer_;
	std::vector<std::unique_ptr<FrameHolder>> frame_buffer_;
	int frame_buffer_position_ = 0;
	int packet_buffer_position_ = 0;

	// Offset for the frame_buffer_ when a looping movie goes back to
	// the zeroeth frame. next_offset_ is written when the zeroeth frame
	// is decoded, and when the last frame is displayed, it is applied to
	// offset_.
	int offset_ = 0;
	int next_offset_ = 0;

	// display_frame_num_ will often be the start of the sliding window,
	// or the oldest Frame that is currently decoded.
	int display_frame_num_ = 0;

	// 0 = no EOF
	// 1 = EOF while decoding
	int end_of_file_;

	// The various flags used to signal the decoding thread to do something.
	bool cancel_ = false;
	bool looping_ = false;
	bool reset_ = false;
	bool end_of_movie_ = false;
};

static struct AVPixelFormat_t
{
	int bpp;
	std::uint32_t masks[4];
	avcodec::AVPixelFormat pf;
	bool bHighColor;
	bool bByteSwapOnLittleEndian;
	MovieDecoderPixelFormatYCbCr YUV;
} AVPixelFormats[] = {
	{
		32,
		{ 0xFF000000,
		  0x00FF0000,
		  0x0000FF00,
		  0x000000FF },
		avcodec::AV_PIX_FMT_YUYV422,
		false, /* N/A */
		true,
		PixelFormatYCbCr_YUYV422,
	},
	{
		32,
		{ 0x0000FF00,
		  0x00FF0000,
		  0xFF000000,
		  0x000000FF },
		avcodec::AV_PIX_FMT_BGRA,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		32,
		{ 0x00FF0000,
		  0x0000FF00,
		  0x000000FF,
		  0xFF000000 },
		avcodec::AV_PIX_FMT_ARGB,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		24,
		{ 0xFF0000,
		  0x00FF00,
		  0x0000FF,
		  0x000000 },
		avcodec::AV_PIX_FMT_RGB24,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		24,
		{ 0x0000FF,
		  0x00FF00,
		  0xFF0000,
		  0x000000 },
		avcodec::AV_PIX_FMT_BGR24,
		true,
		true,
		PixelFormatYCbCr_Invalid,
	},
	{
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x0000 },
		avcodec::AV_PIX_FMT_RGB555,
		false,
		false,
		PixelFormatYCbCr_Invalid,
	},
	{ 0, { 0,0,0,0 }, avcodec::AV_PIX_FMT_NB, true, false, PixelFormatYCbCr_Invalid }
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
