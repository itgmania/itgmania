/* MAD is available from: http://www.underbit.com/products/mad/ */

#include "RageSoundReader_MP3.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "RageFileBasic.h"
#include "RageLog.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageUtil.h"
#include "global.h"
#include "mad.h"

// ID3 code from libid3:
enum tagtype {
  TAGTYPE_NONE = 0,
  TAGTYPE_ID3V1,
  TAGTYPE_ID3V2,
  TAGTYPE_ID3V2_FOOTER
};

static const int ID3_TAG_FLAG_FOOTERPRESENT = 0x10;

static tagtype tagtype(const unsigned char* data, id3_length_t length) {
  if (length >= 3 && data[0] == 'T' && data[1] == 'A' && data[2] == 'G') {
    return TAGTYPE_ID3V1;
  }

  if (length >= 10 &&
      ((data[0] == 'I' && data[1] == 'D' && data[2] == '3') ||
       (data[0] == '3' && data[1] == 'D' && data[2] == 'I')) &&
      data[3] < 0xff && data[4] < 0xff && data[6] < 0x80 && data[7] < 0x80 &&
      data[8] < 0x80 && data[9] < 0x80) {
    return data[0] == 'I' ? TAGTYPE_ID3V2 : TAGTYPE_ID3V2_FOOTER;
  }

  return TAGTYPE_NONE;
}

static unsigned long id3_parse_uint(
    const unsigned char** ptr, unsigned int bytes) {
  unsigned long value = 0;

  ASSERT(bytes >= 1 && bytes <= 4);

  switch (bytes) {
    case 4:
      value = (value << 8) | *(*ptr)++;
      [[fallthrough]];
    case 3:
      value = (value << 8) | *(*ptr)++;
      [[fallthrough]];
    case 2:
      value = (value << 8) | *(*ptr)++;
      [[fallthrough]];
    case 1:
      value = (value << 8) | *(*ptr)++;
  }

  return value;
}

static unsigned long id3_parse_syncsafe(
    const unsigned char** ptr, unsigned int bytes) {
  unsigned long value = 0;

  ASSERT(bytes == 4 || bytes == 5);

  switch (bytes) {
    case 5:
      value = (value << 4) | (*(*ptr)++ & 0x0f);
      [[fallthrough]];
    case 4:
      value = (value << 7) | (*(*ptr)++ & 0x7f);
      value = (value << 7) | (*(*ptr)++ & 0x7f);
      value = (value << 7) | (*(*ptr)++ & 0x7f);
      value = (value << 7) | (*(*ptr)++ & 0x7f);
  }

  return value;
}

static void parse_header(
    const unsigned char** ptr, unsigned int* version, int* flags,
    id3_length_t* size) {
  *ptr += 3;

  *version = id3_parse_uint(ptr, 2);
  *flags = id3_parse_uint(ptr, 1);
  *size = id3_parse_syncsafe(ptr, 4);
}

/*
 * NAME:	tag->query()
 * DESCRIPTION:	if a tag begins at the given location, return its size
 */
signed long id3_tag_query(const unsigned char* data, id3_length_t length) {
  unsigned int version;
  int flags;
  id3_length_t size;

  switch (tagtype(data, length)) {
    case TAGTYPE_ID3V1:
      return 128;

    case TAGTYPE_ID3V2:
      parse_header(&data, &version, &flags, &size);

      if (flags & ID3_TAG_FLAG_FOOTERPRESENT) {
        size += 10;
      }

      return 10 + size;

    case TAGTYPE_ID3V2_FOOTER:
      parse_header(&data, &version, &flags, &size);
      return -(int)size - 10;

    case TAGTYPE_NONE:
      break;
  }

  return 0;
}

/* XING code ripped out of madplay (header)
 *
 * mad - MPEG audio decoder
 * Copyright (C) 2000-2001 Robert Leslie
 */
struct xing {
  long flags_;             /* valid fields (see below) */
  unsigned long frames_;   /* total number of frames */
  unsigned long bytes_;    /* total number of bytes */
  unsigned char toc_[100]; /* 100-point seek table */
  long scale_;             /* ?? */
  enum { XING, INFO } type_;
};

enum {
  XING_FRAMES = 0x00000001L,
  XING_BYTES = 0x00000002L,
  XING_TOC = 0x00000004L,
  XING_SCALE = 0x00000008L
};

void xing_init(struct xing* xing);
int xing_parse(struct xing* xing, struct mad_bitptr ptr, unsigned int bitlen);

/* end XING header */

/* a -= b */
static void mad_timer_sub(mad_timer_t* a, mad_timer_t b) {
  /* a = a - b -> a = a + -b */
  mad_timer_negate(&b);
  mad_timer_add(a, b);
}

/* internal->decoder_private field */
struct madlib_t {
  madlib_t() {
    outpos_ = 0;
    outleft_ = 0;
    memset(inbuf_, 0, sizeof(inbuf_));
    memset(outbuf_, 0, sizeof(outbuf_));
    memset(&stream_, 0, sizeof(stream_));
    memset(&frame_, 0, sizeof(frame_));
    memset(&synth_, 0, sizeof(synth_));
    timer_ = mad_timer_zero;
    timer_accurate_ = false;
    inbuf_filepos_ = 0;
    filesize_ = 0;
    header_bytes_ = 0;
    first_frame_ = 0;
    has_xing_ = 0;
    memset(&xingtag_, 0, sizeof(xingtag_));
    length_ = 0;
    framelength_ = mad_timer_zero;
    bitrate_ = 0;
  }

  uint8_t inbuf_[16384];
  float outbuf_[8192];
  int outpos_;
  unsigned outleft_;

  struct mad_stream stream_;
  struct mad_frame frame_;
  struct mad_synth synth_;
  /* Timestamp of the next frame. */
  mad_timer_t timer_;

  /* Whether Timer is trusted; this is false after doing a quick seek. */
  int timer_accurate_;

  /*
   * Frame index of each percentage of the file.  This is constructed
   * as we read the file, so we can quickly seek back later.
   */
  struct mad_timer_compare_lt {
    bool operator()(
        const mad_timer_t& timer1, const mad_timer_t& timer2) const {
      return mad_timer_compare(timer1, timer2) < 0;
    }
  };

  typedef std::map<mad_timer_t, int, mad_timer_compare_lt> tocmap_t;
  tocmap_t tocmap_;

  /* Position in the file of inbuf: */
  int inbuf_filepos_;

  /* File size. */
  int filesize_;

  /* Number of bytes of header data at the beginning; used for seeking. */
  int header_bytes_;

  /* Whether we've decoded the first frame of real audio yet.  If this
   * is false, we're somewhere before the first frame; we might be in
   * the middle of headers. */
  int first_frame_;

  /* This data is filled in when the first frame is decoded. */
  int has_xing_; /* whether xingtag is valid */
  struct xing xingtag_;

  /* If has_xing is true, this is filled in based on the xing header.
   * If it's false, this is filled in based on the size of the file
   * and the first frame. */
  int length_;
  mad_timer_t framelength_;

  /* If we have a Xing tag, this is the average bitrate; otherwise it's
   * the bitrate of the first frame. */
  int bitrate_;
};

static float scale(mad_fixed_t sample) {
  return float(double(sample) / (1 << MAD_F_FRACBITS));
}

static int get_this_frame_byte(const madlib_t* mad) {
  int ret = mad->inbuf_filepos_;

  /* If we have a frame, adjust. */
  if (mad->stream_.this_frame != nullptr) {
    ret += mad->stream_.this_frame - mad->inbuf_;
  }

  return ret;
}

/* Called on the first frame decoded.  Returns true if this frame
 * should be ignored. */
bool RageSoundReader_MP3::handle_first_frame() {
  bool ret = false;

  /* Check for a XING tag. */
  xing_init(&mad_->xingtag_);
  if (xing_parse(
          &mad_->xingtag_, mad_->stream_.anc_ptr, mad_->stream_.anc_bitlen) ==
      0) {
    /*
     * "Info" tags are written by some tools.  They're just Xing tags, but for
     * CBR files.
     *
     * However, DWI's decoder, BASS, doesn't understand this, and treats it as a
     * corrupt frame, outputting a frame of silence.  Let's ignore the tag, so
     * it'll be treated as an invalid frame, so we match DWI sync.
     *
     * The information in it isn't very useful to us.  The TOC is less accurate
     * then computing it ourself (low resolution).  The file length computation
     * might be wrong, if the tag is incorrect, and we can compute that
     * accurately ourself for CBR files.
     */
    if (mad_->xingtag_.type_ == xing::INFO) {
      return false;
    }

    mad_->header_bytes_ =
        std::max(mad_->header_bytes_, get_this_frame_byte(mad_));

    mad_->has_xing_ = true;

    mad_timer_t tm = mad_->frame_.header.duration;
    /* XXX: does this include the Xing header itself? */
    mad_timer_multiply(&tm, mad_->xingtag_.frames_);
    mad_->length_ = mad_timer_count(tm, MAD_UNITS_MILLISECONDS);

    /* XXX: an id3v2 footer tag would throw this off a little. This also assumes
     * the Xing tag is the last header; it always is, I think. */
    int bytes = mad_->filesize_ - mad_->header_bytes_;
    mad_->bitrate_ = (int)(bytes * 8 / (mad_->length_ / 1000.f));

    if (mad_->xingtag_.type_ == xing::XING) {
      ret = 1;
    }
  }

  /* If there's no Xing tag, mad->length will be filled in by _open. */

  return ret;
}

int RageSoundReader_MP3::fill_buffer() {
  /* Need more data. */
  int inbytes = 0;
  if (mad_->stream_.next_frame != nullptr) {
    /* Pull out remaining data from the last buffer. */
    inbytes = mad_->stream_.bufend - mad_->stream_.next_frame;
    memmove(mad_->inbuf_, mad_->stream_.next_frame, inbytes);
    mad_->inbuf_filepos_ += mad_->stream_.next_frame - mad_->inbuf_;
  }

  const bool bWasAtEOF = file_->AtEOF();

  int rc = file_->Read(
      mad_->inbuf_ + inbytes,
      sizeof(mad_->inbuf_) - inbytes - MAD_BUFFER_GUARD);
  if (rc < 0) {
    SetError(file_->GetError());
    return -1;
  }

  if (file_->AtEOF() && !bWasAtEOF) {
    /* We just reached EOF.  Append MAD_BUFFER_GUARD bytes of NULs to the
     * buffer, to ensure that the last frame is flushed. */
    memset(mad_->inbuf_ + inbytes + rc, 0, MAD_BUFFER_GUARD);
    rc += MAD_BUFFER_GUARD;
  }

  if (rc == 0) {
    return 0;
  }

  inbytes += rc;
  mad_stream_buffer(&mad_->stream_, mad_->inbuf_, inbytes);
  return rc;
}

void fill_frame_index_cache(madlib_t* mad) {
  int pos;

  /* Only update the frame cache if our timer is consistent. */
  if (!mad->timer_accurate_) {
    return;
  }

  /* the frame we just decoded: */
  pos = get_this_frame_byte(mad);

  /* Fill in the TOC percent. */
  if (!mad->tocmap_.empty()) {
    /* Don't add an entry if one already exists near this time. */
    madlib_t::tocmap_t::iterator it = mad->tocmap_.upper_bound(mad->timer_);
    if (it != mad->tocmap_.begin()) {
      --it;
      int iDiffSeconds = mad->timer_.seconds - it->first.seconds;
      if (iDiffSeconds < 5) {
        return;
      }
    }
  }

  mad->tocmap_[mad->timer_] = pos;
}

/* Handle first-stage decoding: extracting the MP3 frame data. */
int RageSoundReader_MP3::do_mad_frame_decode(bool headers_only) {
  int bytes_read = 0;

  for (;;) {
    int ret;

    /* Always actually decode the first packet, so we cleanly parse Xing tags.
     */
    if (headers_only && !mad_->first_frame_) {
      ret = mad_header_decode(&mad_->frame_.header, &mad_->stream_);
    } else {
      ret = mad_frame_decode(&mad_->frame_, &mad_->stream_);
    }

    if (ret == -1 && (mad_->stream_.error == MAD_ERROR_BUFLEN ||
                      mad_->stream_.error == MAD_ERROR_BUFPTR)) {
      if (bytes_read > 25000) {
        /* We've read this much without actually getting a frame; error. */
        SetError("Can't find data");
        return -1;
      }

      ret = fill_buffer();
      if (ret <= 0) {
        return ret;
      }
      bytes_read += ret;

      continue;
    }

    if (ret == -1 && mad_->stream_.error == MAD_ERROR_LOSTSYNC) {
      /* This might be an ID3V2 tag. */
      const int tagsize = id3_tag_query(
          mad_->stream_.this_frame,
          mad_->stream_.bufend - mad_->stream_.this_frame);

      if (tagsize) {
        mad_stream_skip(&mad_->stream_, tagsize);

        /* Don't count the tagsize against the max-read-per-call figure. */
        bytes_read -= tagsize;

        continue;
      }
    }

    if (ret == -1 && mad_->stream_.error == MAD_ERROR_BADDATAPTR) {
      /*
       * Something's corrupt.  One cause of this is cutting an MP3 in the middle
       * without reencoding; the first two frames will reference data from
       * previous frames that have been removed.  The frame is valid--we can get
       * a header from it, we just can't synth useful data.
       *
       * BASS pretends the bad frames are silent.  Emulate that, for
       * compatibility.
       */
      ret = 0; /* pretend success */
    }

    if (!ret) {
      /* OK. */
      if (mad_->first_frame_) {
        /* We're at the beginning.  Is this a Xing tag? */
        if (handle_first_frame()) {
          /* The first frame contained a header. Continue searching. */
          continue;
        }

        /* We've decoded the first frame of data.
         *
         * We want mad->Timer to represent the timestamp of the first sample of
         * the currently decoded frame.  Don't increment mad->Timer on the first
         * frame, or it'll be the time of the *next* frame.  (All frames have
         * the same duration.) */
        mad_->first_frame_ = false;
        mad_->timer_ = mad_timer_zero;
        mad_->header_bytes_ = get_this_frame_byte(mad_);
      } else {
        mad_timer_add(&mad_->timer_, mad_->frame_.header.duration);
      }

      fill_frame_index_cache(mad_);

      return 1;
    }

    if (mad_->stream_.error == MAD_ERROR_BADCRC) {
      /* XXX untested */
      mad_frame_mute(&mad_->frame_);
      mad_synth_mute(&mad_->synth_);

      continue;
    }

    if (!MAD_RECOVERABLE(mad_->stream_.error)) {
      /* We've received an unrecoverable error. */
      SetError(mad_stream_errorstr(&mad_->stream_));
      return -1;
    }
  }
}

void RageSoundReader_MP3::synth_output() {
  mad_->outleft_ = mad_->outpos_ = 0;

  if (MAD_NCHANNELS(&mad_->frame_.header) != this->channels_) {
    /* This frame contains a different number of channels than the first.
     * I've never actually seen this--it's just to prevent exploding if
     * it happens--and we could continue on easily enough, so if it happens,
     * just continue. */
    return;
  }

  mad_synth_frame(&mad_->synth_, &mad_->frame_);
  for (int i = 0; i < mad_->synth_.pcm.length; i++) {
    for (int chan = 0; chan < this->channels_; ++chan) {
      float Sample = scale(mad_->synth_.pcm.samples[chan][i]);
      mad_->outbuf_[mad_->outleft_] = Sample;
      ++mad_->outleft_;
    }
  }
}

/* Seek to a byte in the file.  If you're going to put the file position
 * back when you're done, and not going to read any data, you don't have
 * to use this. */
int RageSoundReader_MP3::seek_stream_to_byte(int byte) {
  if (file_->Seek(byte) == -1) {
    SetError(strerror(errno));
    return 0;
  }

  mad_frame_mute(&mad_->frame_);
  mad_synth_mute(&mad_->synth_);
  mad_stream_finish(&mad_->stream_);
  mad_stream_init(&mad_->stream_);

  mad_->outleft_ = mad_->outpos_ = 0;

  mad_->inbuf_filepos_ = byte;

  /* If the position is <= the position of the first audio sample, then
   * we're at the beginning. */
  if (!mad_->tocmap_.empty()) {
    mad_->first_frame_ = (byte <= mad_->tocmap_.begin()->second);
  }

  return 1;
}

RageSoundReader_MP3::RageSoundReader_MP3() {
  mad_ = new madlib_t;
  accurate_sync_ = false;

  mad_stream_init(&mad_->stream_);
  mad_frame_init(&mad_->frame_);
  mad_synth_init(&mad_->synth_);

  mad_frame_mute(&mad_->frame_);
  mad_timer_reset(&mad_->timer_);
  mad_->length_ = -1;
  mad_->inbuf_filepos_ = 0;
  mad_->header_bytes_ = 0;
  mad_->has_xing_ = false;
  mad_->timer_accurate_ = 1;
  mad_->bitrate_ = -1;
  mad_->first_frame_ = true;
}

RageSoundReader_MP3::~RageSoundReader_MP3() {
  mad_synth_finish(&mad->Synth);
  mad_frame_finish(&mad_->frame_);
  mad_stream_finish(&mad_->stream_);

  delete mad_;
}

RageSoundReader_FileReader::OpenResult RageSoundReader_MP3::Open(
    RageFileBasic* pFile) {
  file_ = pFile;

  mad_->filesize_ = file_->GetFileSize();
  ASSERT(mad_->filesize_ != -1);

  /* Make sure we can decode at least one frame.  This will also fill in header
   * info. */
  mad_->outpos_ = 0;

  int ret = do_mad_frame_decode();
  switch (ret) {
    case 0:
      SetError("Failed to read any data at all");
      return OPEN_UNKNOWN_FILE_FORMAT;
    case -1:
      SetError(GetError() + " (not an MP3 stream?)");
      return OPEN_UNKNOWN_FILE_FORMAT;
  }

  /* Store the bitrate of the frame we just got. */
  if (mad_->bitrate_ == -1) { /* might have been set by a Xing tag */
    mad_->bitrate_ = mad_->frame_.header.bitrate;
  }

  sample_rate_ = mad_->frame_.header.samplerate;
  mad_->framelength_ = mad_->frame_.header.duration;
  this->channels_ = MAD_NCHANNELS(&mad_->frame_.header);

  /* Since we've already decoded a frame, just synth it instead of rewinding
   * the stream. */
  synth_output();

  if (mad_->length_ == -1) {
    /* If vbr and !xing, this is just an estimate. */
    int bps = mad_->bitrate_ / 8;
    float secs = (float)(mad_->filesize_ - mad_->header_bytes_) / bps;
    mad_->length_ = (int)(secs * 1000.f);
  }

  return OPEN_OK;
}

RageSoundReader_MP3* RageSoundReader_MP3::Copy() const {
  RageSoundReader_MP3* ret = new RageSoundReader_MP3;

  ret->file_ = file_->Copy();
  ret->file_->Seek(0);
  ret->accurate_sync_ = accurate_sync_;
  ret->mad_->filesize_ = mad_->filesize_;
  ret->mad_->bitrate_ = mad_->bitrate_;
  ret->sample_rate_ = sample_rate_;
  ret->mad_->framelength_ = mad_->framelength_;
  ret->channels_ = channels_;
  ret->mad_->length_ = mad_->length_;

  //	int n = ret->do_mad_frame_decode();
  //	ASSERT( n > 0 );
  //	ret->synth_output();

  return ret;
}

int RageSoundReader_MP3::Read(float* buf, int iFrames) {
  int iFramesWritten = 0;

  while (iFrames > 0) {
    if (mad_->outleft_ > 0) {
      int iFramesToCopy =
          std::min(iFrames, int(mad_->outleft_ / GetNumChannels()));
      const int iSamplesToCopy = iFramesToCopy * GetNumChannels();
      const int iBytesToCopy = iSamplesToCopy * sizeof(float);

      memcpy(buf, mad_->outbuf_ + mad_->outpos_, iBytesToCopy);

      buf += iSamplesToCopy;
      iFrames -= iFramesToCopy;
      iFramesWritten += iFramesToCopy;
      mad_->outpos_ += iSamplesToCopy;
      mad_->outleft_ -= iSamplesToCopy;
      continue;
    }

    /* Decode more from the MP3 stream. */
    int ret = do_mad_frame_decode();
    if (ret == 0) {
      return END_OF_FILE;
    }
    if (ret == -1) {
      return ERROR;
    }

    synth_output();
  }

  return iFramesWritten;
}

bool RageSoundReader_MP3::MADLIB_rewind() {
  file_->Seek(0);

  mad_frame_mute(&mad_->frame_);
  mad_synth_mute(&mad_->synth_);
  mad_timer_reset(&mad_->timer_);
  mad_->outpos_ = mad_->outleft_ = 0;

  mad_stream_finish(&mad_->stream_);
  mad_stream_init(&mad_->stream_);
  mad_stream_buffer(&mad_->stream_, nullptr, 0);
  mad_->inbuf_filepos_ = 0;

  /* Be careful.  We need to leave header_bytes alone, so if we try to
   * SetPosition_estimate immediately after this, we still know the header size.
   * However, we need to set first_frame to true, since the first frame is
   * handled specially in do_mad_frame_decode; if we don't set it, then we'll be
   * desynced by a frame after an accurate seek. */
  //	mad->header_bytes = 0;
  mad_->first_frame_ = true;
  mad_->stream_.this_frame = nullptr;

  return true;
}

/* Methods of seeking:
 *
 * 1. We can seek from any position to any higher position by decoding headers.
 *    (SetPosition_hard)
 *
 * 2. We can jump based on the bitrate.  This is fast, but not accurate.
 *    (SetPosition_estimate)
 *
 * 3. MADLIB_rewind will rewind the stream to the beginning.  This is always
 * accurate.
 *
 * SetPosition_estimate will leave the position behind the actual requested
 * position; combine them with MADLIB_rewind or SetPosition_hard to catch up.
 */

int RageSoundReader_MP3::SetPosition_hard(int iFrame) {
  mad_timer_t desired;
  mad_timer_set(&desired, 0, iFrame, mad_->frame_.header.samplerate);

  /* This seek doesn't change the accuracy of our timer. */

  /* If we're already exactly at the requested position, OK. */
  if (mad_timer_compare(mad_->timer_, desired) == 0) {
    return 1;
  }

  /* We always come in here with data synthed.  Be careful not to synth the
   * same frame twice. */
  bool synthed = true;

  /* If we're already past the requested position, rewind. */
  if (mad_timer_compare(mad_->timer_, desired) > 0) {
    if (!MADLIB_rewind()) {
      return -1;
    }
    if (do_mad_frame_decode() <= 0) {
      return -1;
    }
    synthed = false;
  }

  /* Decode frames until the current frame contains the desired offset. */
  for (;;) {
    /* If desired < next_frame_timer, this frame contains the position.  Since
     * we've already decoded the frame, synth it, too. */
    mad_timer_t next_frame_timer = mad_->timer_;
    mad_timer_add(&next_frame_timer, mad_->framelength_);

    if (mad_timer_compare(desired, next_frame_timer) < 0) {
      if (!synthed) {
        synth_output();
      } else {
        mad_->outleft_ += mad_->outpos_;
        mad_->outpos_ = 0;
      }

      /* We just synthed data starting at mad->Timer, containing the desired
       * offset. Skip (desired - mad->Timer) worth of frames in the output to
       * line up. */
      mad_timer_t skip = desired;
      mad_timer_sub(&skip, mad_->timer_);

      int samples = mad_timer_count(skip, (mad_units)sample_rate_);

      /* Skip 'samples' samples. */
      mad_->outpos_ = samples * this->channels_;
      mad_->outleft_ -= samples * this->channels_;
      return 1;
    }

    /* Otherwise, if the desired time will be in the *next* decode, then synth
     * this one, too. */
    mad_timer_t next_next_frame_timer = next_frame_timer;
    mad_timer_add(&next_next_frame_timer, mad_->framelength_);

    if (mad_timer_compare(desired, next_next_frame_timer) < 0 && !synthed) {
      synth_output();
      synthed = true;
    }

    int ret = do_mad_frame_decode();
    if (ret <= 0) {
      mad_->outleft_ = mad_->outpos_ = 0;
      return ret; /* it set the error */
    }

    synthed = false;
  }
}

/* Do a seek based on the bitrate. */
int RageSoundReader_MP3::SetPosition_estimate(int iFrame) {
  /* This doesn't leave us accurate. */
  mad_->timer_accurate_ = 0;

  mad_timer_t seekamt;
  mad_timer_set(&seekamt, 0, iFrame, mad_->frame_.header.samplerate);
  {
    /* We're going to skip ahead two samples below, so seek earlier than
     * we were asked to. */
    mad_timer_t back_len = mad_->framelength_;
    mad_timer_multiply(&back_len, -2);
    mad_timer_add(&seekamt, back_len);
    if (mad_timer_compare(seekamt, mad_timer_zero) < 0) {
      seekamt = mad_timer_zero;
    }
  }

  int seekpos = mad_timer_count(seekamt, MAD_UNITS_MILLISECONDS) *
                (mad_->bitrate_ / 8 / 1000);
  seekpos += mad_->header_bytes_;
  seek_stream_to_byte(seekpos);

  /* We've jumped across the file, so the decoder is currently desynced.
   * Just decode a few frames. */
  for (int i = 0; i < 2; ++i) {
    int ret = do_mad_frame_decode();
    if (ret <= 0) {
      return ret;
    }
  }

  /* Throw out one synth. */
  synth_output();
  mad_->outleft_ = 0;

  /* Find out where we really seeked to. */
  int ms = (get_this_frame_byte(mad_) - mad_->header_bytes_) /
           (mad_->bitrate_ / 8 / 1000);
  mad_timer_set(&mad_->timer_, 0, ms, 1000);

  return 1;
}

int RageSoundReader_MP3::SetPosition(int iFrame) {
  /* Rewinding is always fast and accurate, and SetPosition_estimate is bad at
   * 0. */
  if (!iFrame) {
    MADLIB_rewind();
    return 1; /* ok */
  }

  /* We can do a fast jump in VBR with Xing with more accuracy than without
   * Xing. */
  if (mad_->has_xing_) {
    return SetPosition_hard(iFrame);
  }

  LOG->Trace("MP3 decoder had to revert to guessing the song position");
  /* Guess.  This is only remotely accurate when we're not VBR, but also
   * do it if we have no Xing tag. */
  return SetPosition_estimate(iFrame);
}

bool RageSoundReader_MP3::SetProperty(
    const std::string& sProperty, float fValue) {
  if (sProperty == "AccurateSync") {
    accurate_sync_ = (fValue > 0.001f);
    return true;
  }

  return RageSoundReader_FileReader::SetProperty(sProperty, fValue);
}

int RageSoundReader_MP3::GetNextSourceFrame() const {
  int iFrame =
      mad_timer_count(mad_->timer_, mad_units(mad_->frame_.header.samplerate));
  iFrame += mad_->outpos_ / this->channels_;
  return iFrame;
}

int RageSoundReader_MP3::GetLengthInternal(bool fast) {
  if (mad_->has_xing_ && mad_->length_ != -1) {
    return mad_->length_; /* should be accurate */
  }

  /* Check to see if a frame in the middle of the file is the same
   * bitrate as the first frame.  If it is, assume the file is really CBR. */
  seek_stream_to_byte(mad_->filesize_ / 2);

  /* XXX use mad_header_decode and check more than one frame */
  if (mad_->length_ != -1 && do_mad_frame_decode() &&
      mad_->bitrate_ == (int)mad_->frame_.header.bitrate) {
    return mad_->length_;
  }

  if (!MADLIB_rewind()) {
    return 0;
  }

  /* Worst-case: vbr && !xing.  We've made a guess at the length, but let's
   * actually scan the size, since the guess is probably wrong. */
  if (fast) {
    SetError("Can't estimate file length");
    return -1;
  }

  MADLIB_rewind();
  for (;;) {
    int ret = do_mad_frame_decode(true);
    if (ret == -1) {
      return -1; /* it set the error */
    }
    if (ret == 0) /* EOF */
    {
      break;
    }
  }

  /* mad->Timer is the timestamp of the current frame; find the timestamp of
   * the very end. */
  mad_timer_t end = mad_->timer_;
  mad_timer_add(&end, mad_->framelength_);

  /* Count milliseconds. */
  return mad_timer_count(end, MAD_UNITS_MILLISECONDS);
}

int RageSoundReader_MP3::GetLengthConst(bool fast) const {
  RageSoundReader_MP3* pCopy = this->Copy();

  int iLength = pCopy->GetLengthInternal(fast);

  delete pCopy;
  return iLength;
}

/* begin XING code ripped out of madplay */

/*
 * NAME:	xing->init()
 * DESCRIPTION:	initialize Xing structure
 */
void xing_init(struct xing* xing) { xing->flags_ = 0; }

/*
 * NAME:	xing->parse()
 * DESCRIPTION:	parse a Xing VBR header
 */
int xing_parse(struct xing* xing, struct mad_bitptr ptr, unsigned int bitlen) {
  const unsigned XING_MAGIC = (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g');
  const unsigned INFO_MAGIC = (('I' << 24) | ('n' << 16) | ('f' << 8) | 'o');
  unsigned data;
  if (bitlen < 64) {
    goto fail;
  }
  data = mad_bit_read(&ptr, 32);

  if (data == XING_MAGIC) {
    xing->type_ = xing::XING;
  } else if (data == INFO_MAGIC) {
    xing->type_ = xing::INFO;
  } else {
    goto fail;
  }

  xing->flags_ = mad_bit_read(&ptr, 32);
  bitlen -= 64;

  if (xing->flags_ & XING_FRAMES) {
    if (bitlen < 32) {
      goto fail;
    }

    xing->frames_ = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  if (xing->flags_ & XING_BYTES) {
    if (bitlen < 32) {
      goto fail;
    }

    xing->bytes_ = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  if (xing->flags_ & XING_TOC) {
    if (bitlen < 800) {
      goto fail;
    }

    for (int i = 0; i < 100; ++i) {
      xing->toc_[i] = (unsigned char)mad_bit_read(&ptr, 8);
    }

    bitlen -= 800;
  }

  if (xing->flags_ & XING_SCALE) {
    if (bitlen < 32) {
      goto fail;
    }

    xing->scale_ = mad_bit_read(&ptr, 32);
    bitlen -= 32;
  }

  return 0;

fail:
  xing->flags_ = 0;
  return -1;
}

/* end XING code ripped out of madplay */

/*
 * Copyright (c) 2003-2004 Glenn Maynard
 * Copyright (c) 2000-2003 Robert Leslie (Xing code from madplay)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
