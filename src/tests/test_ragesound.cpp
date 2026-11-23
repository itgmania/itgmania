#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "RageSoundMixBuffer.h"
#include "RageSoundReader.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_PitchChange.h"
#include "RageSoundReader_Resample_Good.h"
#include "RageSoundReader_ThreadedBuffer.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
const int kRates[] = {8000, 11025, 22050, 32000, 44100, 48000, 96000};

/*-- -- -- -- -- -- -- -- -
      Synthetic sources
 -- -- -- -- -- -- -- -- -*/

// Every sample encodes its own frame index, so any dropped, repeated or
// shifted frame is detected exactly:   value(frame, ch) = frame + 0.25 * ch
// (exact in float for frame < 2^24).
class FrameIdReader : public RageSoundReader {
 public:
  FrameIdReader(int rate, int channels, int total_frames, int max_chunk = 0)
      : rate_(rate),
        channels_(channels),
        total_(total_frames),
        max_chunk_(max_chunk),
        pos_(0) {}

  static float Value(int frame, int ch) {
    return static_cast<float>(frame) + 0.25f * static_cast<float>(ch);
  }

  int GetLength() const override {
    return static_cast<int>(static_cast<int64_t>(total_) * 1000 / rate_);
  }
  int SetPosition(int frame) override {
    if (frame < 0) {
      frame = 0;
    }
    pos_ = std::min(frame, total_);
    return frame < total_ ? 1 : 0;  // 0 == positioned at EOF
  }
  int Read(float* buf, int frames) override {
    if (pos_ >= total_) {
      return END_OF_FILE;
    }
    int n = std::min(frames, total_ - pos_);
    if (max_chunk_ > 0) {
      n = std::min(n, max_chunk_);
    }
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < channels_; ++c) {
        *buf++ = Value(pos_ + i, c);
      }
    }
    pos_ += n;
    return n;
  }
  RageSoundReader* Copy() const override { return new FrameIdReader(*this); }
  int GetSampleRate() const override { return rate_; }
  unsigned GetNumChannels() const override { return channels_; }
  int GetNextSourceFrame() const override { return pos_; }
  float GetStreamToSourceRatio() const override { return 1.0f; }
  std::string GetError() const override { return ""; }

 private:
  int rate_, channels_, total_, max_chunk_, pos_;
};

// Per-channel generator: sine (or DC when freq == 0) at a given amplitude.
struct ChannelSpec {
  double freq_hz;  // 0 => DC
  double amp;
};

class ToneReader : public RageSoundReader {
 public:
  ToneReader(int rate, int total_frames, std::vector<ChannelSpec> channels)
      : rate_(rate), total_(total_frames), ch_(std::move(channels)), pos_(0) {}

  int GetLength() const override {
    return static_cast<int>(static_cast<int64_t>(total_) * 1000 / rate_);
  }
  int SetPosition(int frame) override {
    pos_ = std::clamp(frame, 0, total_);
    return frame < total_ ? 1 : 0;
  }
  int Read(float* buf, int frames) override {
    if (pos_ >= total_) {
      return END_OF_FILE;
    }
    const int n = std::min(frames, total_ - pos_);
    for (int i = 0; i < n; ++i) {
      for (const ChannelSpec& c : ch_) {
        const double t = static_cast<double>(pos_ + i) / rate_;
        *buf++ = static_cast<float>(
            c.freq_hz == 0 ? c.amp : c.amp * std::sin(2 * kPi * c.freq_hz * t));
      }
    }
    pos_ += n;
    return n;
  }
  RageSoundReader* Copy() const override { return new ToneReader(*this); }
  int GetSampleRate() const override { return rate_; }
  unsigned GetNumChannels() const override {
    return static_cast<unsigned>(ch_.size());
  }
  int GetNextSourceFrame() const override { return pos_; }
  float GetStreamToSourceRatio() const override { return 1.0f; }
  std::string GetError() const override { return ""; }

 private:
  int rate_, total_;
  std::vector<ChannelSpec> ch_;
  int pos_;
};

/*-- -- -- -- -- -- -- -- -
       Helper methods
 -- -- -- -- -- -- -- -- -*/

// Read exactly up to `want` frames (or EOF), retrying through WOULD_BLOCK and
// "try again" zero returns. Returns frames read; sets *hit_eof.
//
// Each individual Read() is capped at max_request frames. Filters such as
// Resample_Good alloca() a buffer sized by the request, so one huge Read can
// overflow a 1 MB (Windows) stack; the game itself only reads small blocks.
int ReadFrames(
    RageSoundReader& r, float* out, int want, bool* hit_eof = nullptr,
    int timeout_ms = 5000, int max_request = 4096) {
  const int ch = static_cast<int>(r.GetNumChannels());
  int got = 0;
  if (hit_eof) {
    *hit_eof = false;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (got < want) {
    const int n = r.Read(
        out + static_cast<size_t>(got) * ch, std::min(want - got, max_request));
    if (n > 0) {
      got += n;
    } else if (n == RageSoundReader::END_OF_FILE) {
      if (hit_eof) {
        *hit_eof = true;
      }
      break;
    } else if (
        n == RageSoundReader::WOULD_BLOCK || n == 0 ||
        n == RageSoundReader::STREAM_LOOPED) {
      if (std::chrono::steady_clock::now() > deadline) {
        ADD_FAILURE() << "timed out waiting for data";
        break;
      }
      if (n == RageSoundReader::WOULD_BLOCK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } else {
      ADD_FAILURE() << "Read returned error " << n;
      break;
    }
  }
  return got;
}

std::vector<float> ReadAll(RageSoundReader& r, int max_frames) {
  const int ch = static_cast<int>(r.GetNumChannels());
  std::vector<float> out(static_cast<size_t>(max_frames) * ch);
  const int got = ReadFrames(r, out.data(), max_frames);
  out.resize(static_cast<size_t>(got) * ch);
  return out;
}

// Verifies out[] holds FrameIdReader values for frames [first, first+n).
::testing::AssertionResult FramesMatch(
    const float* out, int channels, int first, int n) {
  for (int i = 0; i < n; ++i) {
    for (int c = 0; c < channels; ++c) {
      const float expect = FrameIdReader::Value(first + i, c);
      const float got = out[static_cast<size_t>(i) * channels + c];
      if (got != expect) {
        return ::testing::AssertionFailure()
               << "frame " << (first + i) << " ch " << c << ": expected "
               << expect << ", got " << got << " (stream index " << i << ")";
      }
    }
  }
  return ::testing::AssertionSuccess();
}

// Zero-crossing frequency estimate over [begin, end) of a mono signal.
double EstimateFrequency(
    const std::vector<float>& mono, size_t begin, size_t end, int rate) {
  int crossings = 0;
  for (size_t i = begin + 1; i < end; ++i) {
    if ((mono[i - 1] < 0) != (mono[i] < 0)) {
      ++crossings;
    }
  }
  const double seconds = static_cast<double>(end - begin) / rate;
  return crossings / (2.0 * seconds);
}

/*-- -- -- -- -- -- -- -- -
        Test naming
 -- -- -- -- -- -- -- -- -*/
// Every suite is instantiated with the "RageSound" prefix, so the whole file
// groups as RageSound/<Suite>.<Test>/<sample rate>.

std::string RateName(const ::testing::TestParamInfo<int>& info) {
  return std::to_string(info.param) + "Hz";
}

struct RatePair {
  int from, to;
};

std::string PairName(const ::testing::TestParamInfo<RatePair>& info) {
  return std::to_string(info.param.from) + "to" +
         std::to_string(info.param.to) + "Hz";
}

// For suites with a single variant (no sample-rate dimension).
std::string SingleVariantName(const ::testing::TestParamInfo<int>&) {
  return "Default";
}

// Fixtures for suites without a sample-rate dimension.
class MixBufferBasicTest : public ::testing::TestWithParam<int> {};
INSTANTIATE_TEST_SUITE_P(
    RageSound, MixBufferBasicTest, ::testing::Values(0), SingleVariantName);
class ResampleMiscTest : public ::testing::TestWithParam<int> {};
INSTANTIATE_TEST_SUITE_P(
    RageSound, ResampleMiscTest, ::testing::Values(0), SingleVariantName);
class PanTest : public ::testing::TestWithParam<int> {};
INSTANTIATE_TEST_SUITE_P(
    RageSound, PanTest, ::testing::Values(0), SingleVariantName);
class PitchChangeTest : public ::testing::TestWithParam<int> {};
INSTANTIATE_TEST_SUITE_P(
    RageSound, PitchChangeTest, ::testing::Values(0), SingleVariantName);

/*-- -- -- -- -- -- -- -- -
RageSoundReader_ThreadedBuffer
 -- -- -- -- -- -- -- -- -*/

class ThreadedBufferTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
    RageSound, ThreadedBufferTest, ::testing::ValuesIn(kRates), RateName);

TEST_P(ThreadedBufferTest, MetadataPassesThrough) {
  const int rate = GetParam();
  for (int ch = 1; ch <= 2; ++ch) {
    FrameIdReader* src = new FrameIdReader(rate, ch, rate / 2);
    const int expected_len = src->GetLength();
    RageSoundReader_ThreadedBuffer tb(src);
    EXPECT_EQ(tb.GetSampleRate(), rate);
    EXPECT_EQ(tb.GetNumChannels(), static_cast<unsigned>(ch));
    EXPECT_EQ(tb.GetLength(), expected_len);
    EXPECT_EQ(tb.GetNextSourceFrame(), 0);
    EXPECT_FLOAT_EQ(tb.GetStreamToSourceRatio(), 1.0f);
  }
}

TEST_P(ThreadedBufferTest, EveryFrameInOrderNoOffByOne) {
  const int rate = GetParam();
  const int total = rate;  // 1 second, spans many 4096-frame blocks
  for (int ch = 1; ch <= 2; ++ch) {
    SCOPED_TRACE(::testing::Message() << rate << " Hz, " << ch << " ch");
    RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, ch, total));
    bool eof = false;
    std::vector<float> buf(static_cast<size_t>(total + 100) * ch);
    const int got = ReadFrames(tb, buf.data(), total + 100, &eof);
    EXPECT_EQ(got, total) << "frame count off";
    EXPECT_TRUE(eof);
    EXPECT_TRUE(FramesMatch(buf.data(), ch, 0, got));
    EXPECT_EQ(tb.GetNextSourceFrame(), total);
    EXPECT_EQ(tb.Read(buf.data(), 16), RageSoundReader::END_OF_FILE);
  }
}

TEST_P(ThreadedBufferTest, VariousReadSizesStayContiguous) {
  const int rate = GetParam();
  const int total = rate / 2 + 7;  // deliberately not block-aligned
  const int sizes[] = {1, 2, 3, 7, 64, 1000, 4095, 4096, 4097, 9999};
  for (int size : sizes) {
    SCOPED_TRACE(::testing::Message() << "read size " << size);
    RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 2, total));
    std::vector<float> buf(static_cast<size_t>(size) * 2);
    int consumed = 0;
    // Cap the number of iterations for the tiny sizes.
    const int max_iters = (size < 64) ? 2000 : 1 << 30;
    for (int it = 0; it < max_iters && consumed < total; ++it) {
      const int want = std::min(size, total - consumed);
      bool eof = false;
      // Allow the whole requested size in one Read(): this test is about
      // read sizes.
      const int got = ReadFrames(tb, buf.data(), want, &eof, 5000, size);
      ASSERT_GT(got, 0);
      ASSERT_TRUE(FramesMatch(buf.data(), 2, consumed, got));
      consumed += got;
      // Position polled after every read must equal frames consumed.
      ASSERT_EQ(tb.GetNextSourceFrame(), consumed);
    }
  }
}

TEST_P(ThreadedBufferTest, ShortSourceReadsDoNotLoseOrDuplicateFrames) {
  const int rate = GetParam();
  const int total = rate / 2;
  // The source hands back awkward chunk sizes; blocks must still join up.
  for (int chunk : {1000, 333, 4097}) {
    SCOPED_TRACE(::testing::Message() << "source chunk " << chunk);
    RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 2, total, chunk));
    std::vector<float> all = ReadAll(tb, total + 50);
    ASSERT_EQ(static_cast<int>(all.size()), total * 2);
    EXPECT_TRUE(FramesMatch(all.data(), 2, 0, total));
  }
}

TEST_P(ThreadedBufferTest, SetPositionSeeksExactly) {
  const int rate = GetParam();
  const int total = rate;
  const int targets[] = {0,    1,        4095,         4096,
                         4097, rate / 2, total - 5000, total - 1};
  for (int target : targets) {
    SCOPED_TRACE(::testing::Message() << "seek to " << target);
    RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 2, total));
    // Read a little first so the buffer holds data from the old position.
    std::vector<float> junk(2048 * 2);
    ReadFrames(tb, junk.data(), 2048);

    ASSERT_EQ(tb.SetPosition(target), 1);
    EXPECT_EQ(tb.GetNextSourceFrame(), target);

    const int want = std::min(3000, total - target);
    std::vector<float> buf(static_cast<size_t>(want) * 2);
    bool eof = false;
    const int got = ReadFrames(tb, buf.data(), want, &eof);
    EXPECT_EQ(got, want);
    EXPECT_TRUE(FramesMatch(buf.data(), 2, target, got));
    EXPECT_EQ(tb.GetNextSourceFrame(), target + got);
  }
}

TEST_P(ThreadedBufferTest, SeekBackwardAndRereadMatches) {
  const int rate = GetParam();
  const int total = rate / 2;
  RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 1, total));
  std::vector<float> a(total), b(total);
  ASSERT_EQ(ReadFrames(tb, a.data(), total), total);
  ASSERT_EQ(tb.SetPosition(0), 1);
  EXPECT_EQ(tb.GetNextSourceFrame(), 0);
  ASSERT_EQ(ReadFrames(tb, b.data(), total), total);
  EXPECT_EQ(a, b);
  EXPECT_TRUE(FramesMatch(b.data(), 1, 0, total));
}

TEST_P(ThreadedBufferTest, SetPositionPastEndReportsEof) {
  const int rate = GetParam();
  const int total = rate / 4;
  RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 1, total));
  EXPECT_EQ(tb.SetPosition(total), 0);
  float f;
  EXPECT_EQ(ReadFrames(tb, &f, 1), 0);
}

TEST_P(ThreadedBufferTest, CopyContinuesFromSamePosition) {
  const int rate = GetParam();
  const int total = rate / 2;
  RageSoundReader_ThreadedBuffer tb(new FrameIdReader(rate, 2, total));
  std::vector<float> head(1500 * 2);
  ASSERT_EQ(ReadFrames(tb, head.data(), 1500), 1500);

  std::unique_ptr<RageSoundReader_ThreadedBuffer> copy(tb.Copy());
  EXPECT_EQ(copy->GetNextSourceFrame(), 1500);

  const int rest = total - 1500;
  std::vector<float> a(static_cast<size_t>(rest) * 2),
      b(static_cast<size_t>(rest) * 2);
  ASSERT_EQ(ReadFrames(tb, a.data(), rest), rest);
  ASSERT_EQ(ReadFrames(*copy, b.data(), rest), rest);
  EXPECT_TRUE(FramesMatch(a.data(), 2, 1500, rest));
  EXPECT_EQ(a, b);
}

/*-- -- -- -- -- -- -- -- -
RageSoundReader_Resample_Good
 -- -- -- -- -- -- -- -- -*/

class ResampleTest : public ::testing::TestWithParam<RatePair> {};

INSTANTIATE_TEST_SUITE_P(
    RageSound, ResampleTest,
    ::testing::Values(
        RatePair{44100, 48000}, RatePair{48000, 44100}, RatePair{22050, 44100},
        RatePair{44100, 22050}, RatePair{8000, 48000}, RatePair{48000, 8000},
        RatePair{96000, 44100}, RatePair{44100, 96000}, RatePair{11025, 32000}),
    PairName);

TEST_P(ResampleTest, ReportsTargetRate) {
  const RatePair p = GetParam();
  RageSoundReader_Resample_Good rs(
      new ToneReader(p.from, p.from, {{1000.0, 0.5}}), p.to);
  EXPECT_EQ(rs.GetSampleRate(), p.to);
}

// One second in must be one second out: the ideal output is
// in_frames * to / from, give or take one frame of rounding.
TEST_P(ResampleTest, OutputLengthMatchesIdeal) {
  const RatePair p = GetParam();
  const int in_frames = p.from;  // 1 second
  RageSoundReader_Resample_Good rs(
      new ToneReader(p.from, in_frames, {{1000.0, 0.5}}), p.to);

  const int expected =
      static_cast<int>(static_cast<int64_t>(in_frames) * p.to / p.from);
  std::vector<float> out = ReadAll(rs, expected + 2000);
  const int got = static_cast<int>(out.size());
  EXPECT_NEAR(got, expected, 1)
      << p.from << " -> " << p.to << ": short by " << (expected - got)
      << " frames (" << (expected - got) * 1000.0 / p.to << " ms)";
}

// Content must also line up in time: a tone whose positive zero crossings
// fall at exact multiples of its period in the source must have them at the
// same times after resampling.
TEST_P(ResampleTest, ContentIsTimeAlignedWithSource) {
  const RatePair p = GetParam();
  const double freq = std::min(500.0, 0.05 * std::min(p.from, p.to));
  RageSoundReader_Resample_Good rs(
      new ToneReader(p.from, p.from, {{freq, 0.5}}), p.to);
  std::vector<float> out = ReadAll(rs, p.to + 2000);
  ASSERT_GT(out.size(), static_cast<size_t>(p.to / 2));

  double sum = 0;
  int count = 0;
  for (size_t i = out.size() / 10; i + 1 < out.size() - out.size() / 10; ++i) {
    if (out[i] < 0 && out[i + 1] >= 0) {
      const double frac = -out[i] / (out[i + 1] - out[i]);
      const double t = (static_cast<double>(i) + frac) / p.to;
      sum += t - std::round(t * freq) / freq;
      ++count;
    }
  }
  ASSERT_GT(count, 10);
  const double mean_error_s = sum / count;
  const double tolerance_s = 1.0 / std::min(p.from, p.to);  // one frame
  EXPECT_NEAR(mean_error_s, 0.0, tolerance_s)
      << p.from << " -> " << p.to << ": content is "
      << (mean_error_s < 0 ? "early" : "late") << " by "
      << std::fabs(mean_error_s) * 1e6 << " us ("
      << std::fabs(mean_error_s) * p.from << " source frames)";
}

TEST_P(ResampleTest, PreservesFrequencyAndAmplitude) {
  const RatePair p = GetParam();
  const double amp = 0.5;
  // Keep the tone well inside the passband of the lower rate; the short
  // filter rolls off noticeably near Nyquist.
  const double freq = std::min(1000.0, 0.1 * std::min(p.from, p.to));

  RageSoundReader_Resample_Good rs(
      new ToneReader(p.from, p.from, {{freq, amp}}), p.to);
  std::vector<float> out = ReadAll(rs, p.to + 2000);
  ASSERT_GT(out.size(), static_cast<size_t>(p.to / 2));

  const size_t skip = p.to / 10;  // ignore filter warm-up
  const size_t end = out.size() - p.to / 10;
  EXPECT_NEAR(EstimateFrequency(out, skip, end, p.to), freq, freq * 0.01);

  float peak = 0;
  for (size_t i = skip; i < end; ++i) {
    peak = std::max(peak, std::fabs(out[i]));
  }
  EXPECT_NEAR(peak, amp, amp * 0.05);
}

TEST_P(ResampleTest, DcLevelAndChannelsStayIndependent) {
  const RatePair p = GetParam();
  RageSoundReader_Resample_Good rs(
      new ToneReader(p.from, p.from / 2, {{0, 0.25}, {0, -0.5}}), p.to);
  ASSERT_EQ(rs.GetNumChannels(), 2u);
  std::vector<float> out = ReadAll(rs, p.to);
  ASSERT_GT(out.size(), 2u * 200);
  // Past the warm-up, left stays +0.25 and right stays -0.5 (not swapped,
  // not mixed).
  const size_t frames = out.size() / 2;
  for (size_t f = 100; f < frames - 100; ++f) {
    ASSERT_NEAR(out[f * 2], 0.25f, 0.01f) << "left, frame " << f;
    ASSERT_NEAR(out[f * 2 + 1], -0.5f, 0.01f) << "right, frame " << f;
  }
}

TEST_P(ResampleMiscTest, SameRateIsBitExactPassthrough) {
  RageSoundReader_Resample_Good rs(new FrameIdReader(44100, 2, 20000), 44100);
  std::vector<float> out = ReadAll(rs, 20000);
  ASSERT_EQ(out.size(), 40000u);
  EXPECT_TRUE(FramesMatch(out.data(), 2, 0, 20000));
  EXPECT_EQ(rs.GetNextSourceFrame(), 20000);
}

TEST_P(ResampleMiscTest, NextSourceFrameTracksOutputPosition) {
  // After reading N output frames, the reported source position (expressed in
  // output-rate frames) must match N within the filter latency.
  for (RatePair p :
       {RatePair{44100, 48000}, RatePair{48000, 44100},
        RatePair{22050, 44100}}) {
    SCOPED_TRACE(::testing::Message() << p.from << " -> " << p.to);
    RageSoundReader_Resample_Good rs(
        new FrameIdReader(p.from, 1, p.from * 2), p.to);
    std::vector<float> buf(20000);
    const int n = ReadFrames(rs, buf.data(), 20000);
    ASSERT_EQ(n, 20000);
    EXPECT_NEAR(rs.GetNextSourceFrame(), n, 16);
  }
}

TEST_P(ResampleMiscTest, SetPositionScalesToSourceRate) {
  RageSoundReader_Resample_Good rs(new FrameIdReader(22050, 1, 22050), 44100);
  // Destination frame 20000 is source frame 10000.
  ASSERT_EQ(rs.SetPosition(20000), 1);
  EXPECT_NEAR(rs.GetNextSourceFrame(), 20000, 1);
  std::vector<float> out(4000);
  ASSERT_EQ(ReadFrames(rs, out.data(), 4000), 4000);
  // The first resampled frames approximate the source ramp near frame 10000.
  EXPECT_NEAR(out[2000], 10000 + (2000 - 8) / 2.0, 200);
}

TEST_P(ResampleMiscTest, VerifyConsumptionAndRatioWithoutRateMod) {
  constexpr int kSourceRate =
      100000;  // yes we did actually find a 100000Hz sample rate OGG simfile
  constexpr int kTargetRate = 48000;
  RageSoundReader_Resample_Good rs(
      new FrameIdReader(kSourceRate, 1, kSourceRate * 4), kTargetRate);
  EXPECT_EQ(rs.GetSampleRate(), kTargetRate);

  rs.SetRate(1.0f);
  EXPECT_EQ(rs.GetRate(), 1.0f);
  EXPECT_EQ(rs.GetStreamToSourceRatio(), 1.0f);

  // Let the converter's internal lookahead fill before measuring.
  std::vector<float> warmup(4096);
  ASSERT_EQ(ReadFrames(rs, warmup.data(), 4096), 4096);
  const int pos_before = rs.GetNextSourceFrame();

  std::vector<float> out(10000);
  ASSERT_EQ(ReadFrames(rs, out.data(), 10000), 10000);
  const int pos_after = rs.GetNextSourceFrame();

  // We can tolerate being a single frame off
  EXPECT_NEAR(pos_after - pos_before, 10000, 1);

  // pos_after is reported in target-rate frames but out.back() is in
  // source-rate frames so we need to measure it on the same basis
  const double next_source_frame =
      out.back() + static_cast<double>(kSourceRate) / kTargetRate;
  const double expected_reported =
      next_source_frame * kTargetRate / kSourceRate;
  EXPECT_NEAR(pos_after, expected_reported, 0.1);
}

TEST_P(ResampleMiscTest, VerifyConsumptionAndRatioWithRateMod) {
  RageSoundReader_Resample_Good rs(
      new FrameIdReader(44100, 1, 44100 * 4), 44100);
  rs.SetRate(2.0f);
  EXPECT_NEAR(rs.GetRate(), 2.0f, 0.01f);
  EXPECT_NEAR(rs.GetStreamToSourceRatio(), 2.0f, 0.01f);

  std::vector<float> out(10000);
  ASSERT_EQ(ReadFrames(rs, out.data(), 10000), 10000);
  // Twice the speed: 10000 output frames consume ~20000 source frames.
  const int src = static_cast<int>(rs.GetNextSourceFrame() / 1);
  EXPECT_NEAR(src, 20000, 64);
}

/*-- -- -- -- -- -- -- -- -
     RageSoundReader_Pan
 -- -- -- -- -- -- -- -- -*/

TEST_P(PanTest, ExpectedChannelMath) {
  struct Case {
    float pan;
    float in_l, in_r;
    float out_l, out_r;
  };
  // RageSoundUtil::Pan: left = L*(1-|p|) [+ R in the swapped direction],
  // right = L*(0.5 -> 0) + R*(0.5 -> 1) as |p| goes 0 -> 1; negative pans
  // mirror it.
  const Case cases[] = {
      {0.0f, 0.8f, 0.2f, 0.8f, 0.2f},    // untouched
      {1.0f, 0.8f, 0.2f, 0.0f, 0.2f},    // hard right: only right input
      {-1.0f, 0.8f, 0.2f, 0.2f, 0.0f},   // hard left: only right input
      {0.5f, 0.8f, 0.2f, 0.4f, 0.35f},   // 0.8*0.5 ; 0.8*0.25 + 0.2*0.75
      {-0.5f, 0.8f, 0.2f, 0.35f, 0.4f},  // mirror image
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(::testing::Message() << "pan " << c.pan);
    RageSoundReader_Pan pan(
        new ToneReader(44100, 1000, {{0, c.in_l}, {0, c.in_r}}));
    ASSERT_TRUE(pan.SetProperty("Pan", c.pan));
    ASSERT_EQ(pan.GetNumChannels(), 2u);
    std::vector<float> out = ReadAll(pan, 1000);
    ASSERT_EQ(out.size(), 2000u);
    for (size_t f = 0; f < 1000; ++f) {
      ASSERT_NEAR(out[f * 2], c.out_l, 1e-5f) << "frame " << f;
      ASSERT_NEAR(out[f * 2 + 1], c.out_r, 1e-5f) << "frame " << f;
    }
  }

  // A mono source is widened to stereo, both channels equal at pan 0.
  RageSoundReader_Pan mono(new ToneReader(44100, 500, {{0, 0.6}}));
  EXPECT_EQ(mono.GetNumChannels(), 2u);
  std::vector<float> out(500 * 2);
  ASSERT_EQ(ReadFrames(mono, out.data(), 500), 500);
  for (size_t f = 0; f < 500; ++f) {
    ASSERT_NEAR(out[f * 2], 0.6f, 1e-6f);
    ASSERT_NEAR(out[f * 2 + 1], 0.6f, 1e-6f);
  }
}

/*-- -- -- -- -- -- -- -- -
 RageSoundReader_PitchChange
 -- -- -- -- -- -- -- -- -*/

// Assumes the RateModPreservesPitch preference is at its default (true), so
// "Pitch" changes frequency while "Speed" changes duration.

// Renders a mono tone through PitchChange in small blocks and returns the
// output. Plain function (no lambda); returns false on any read problem.
bool RenderPitchChange(
    int rate, int total_frames, double tone_hz, float pitch, float speed,
    std::vector<float>* out) {
  RageSoundReader_PitchChange pc(
      new ToneReader(rate, total_frames, {{tone_hz, 0.5}}));
  pc.SetProperty("Pitch", pitch);
  pc.SetProperty("Speed", speed);

  out->clear();
  float block[2048];
  int idle_reads = 0;
  // Bounded loop: at most 4x the input length worth of blocks.
  const int max_blocks = (total_frames / 2048 + 1) * 4;
  for (int i = 0; i < max_blocks; ++i) {
    const int n = pc.Read(block, 2048);
    if (n == RageSoundReader::END_OF_FILE) {
      return true;
    }
    if (n < 0 && n != RageSoundReader::STREAM_LOOPED) {
      return false;
    }
    if (n <= 0) {
      // 0 means "try again" (e.g. a ratio change was just applied).
      if (++idle_reads > 1000) {
        return false;
      }
      continue;
    }
    idle_reads = 0;
    out->insert(out->end(), block, block + n);
  }
  return true;
}

TEST_P(PitchChangeTest, DoublePitchDoublesFrequencyKeepsDuration) {
  const int rate = 44100;
  const double base_freq = 440.0;
  const int total = rate * 4;

  std::vector<float> neutral;
  std::vector<float> up;
  ASSERT_TRUE(RenderPitchChange(rate, total, base_freq, 1.0f, 1.0f, &neutral));
  ASSERT_TRUE(RenderPitchChange(rate, total, base_freq, 2.0f, 1.0f, &up));
  ASSERT_GT(neutral.size(), static_cast<size_t>(rate * 3));
  ASSERT_GT(up.size(), static_cast<size_t>(rate * 3));

  const size_t skip = rate / 2;
  const size_t tail = rate / 2;
  const double f_neutral =
      EstimateFrequency(neutral, skip, neutral.size() - tail, rate);
  const double f_up = EstimateFrequency(up, skip, up.size() - tail, rate);
  EXPECT_NEAR(f_neutral, base_freq, base_freq * 0.03);
  EXPECT_NEAR(f_up, 2 * base_freq, 2 * base_freq * 0.03);

  // Pitch shifting must not change the playing time (within 5%).
  EXPECT_NEAR(
      static_cast<double>(up.size()), static_cast<double>(neutral.size()),
      static_cast<double>(neutral.size()) * 0.05);
}

/*-- -- -- -- -- -- -- -- -
     RageSoundMixBuffer
 -- -- -- -- -- -- -- -- -*/

class MixBufferTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
    RageSound, MixBufferTest, ::testing::ValuesIn(kRates), RateName);

TEST_P(MixBufferBasicTest, WriteReadRoundTripAndClears) {
  RageSoundMixBuffer mix;
  const float in[6] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f};
  mix.write(in, 6);
  EXPECT_EQ(mix.size(), 6u);
  float out[6] = {};
  mix.read(out);
  for (int i = 0; i < 6; ++i) {
    EXPECT_FLOAT_EQ(out[i], in[i]);
  }
  EXPECT_EQ(mix.size(), 0u);  // read() empties the buffer
}

TEST_P(MixBufferBasicTest, ZeroLengthWriteDoesNothing) {
  RageSoundMixBuffer mix;
  const float x = 1.0f;
  mix.write(&x, 0);
  EXPECT_EQ(mix.size(), 0u);
}

TEST_P(MixBufferBasicTest, WritesAccumulate) {
  RageSoundMixBuffer mix;
  const float a[4] = {0.1f, 0.2f, 0.3f, 0.4f};
  const float b[4] = {0.4f, 0.3f, 0.2f, 0.1f};
  mix.write(a, 4);
  mix.write(b, 4);
  EXPECT_EQ(mix.size(), 4u);
  float out[4];
  mix.read(out);
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(out[i], 0.5f, 1e-6f);
  }
}

TEST_P(MixBufferBasicTest, ClampsFloatOutput) {
  RageSoundMixBuffer mix;
  const float in[4] = {1.5f, -1.5f, 0.5f, -0.5f};
  mix.write(in, 4);
  float out[4];
  mix.read(out);
  EXPECT_FLOAT_EQ(out[0], 1.0f);
  EXPECT_FLOAT_EQ(out[1], -1.0f);
  EXPECT_FLOAT_EQ(out[2], 0.5f);
  EXPECT_FLOAT_EQ(out[3], -0.5f);
}

TEST_P(MixBufferBasicTest, Int16ConversionRoundsAndClamps) {
  RageSoundMixBuffer mix;
  const float in[7] = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, -0.5f};
  mix.write(in, 7);
  int16_t out[7];
  mix.read(out);
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[1], 32767);
  EXPECT_EQ(out[2], -32767);  // scale is INT16_MAX in both directions
  EXPECT_EQ(out[3], 32767);   // clipped
  EXPECT_EQ(out[4], -32767);
  EXPECT_EQ(out[5], 16384);  // round(16383.5), not truncated to 16383
  EXPECT_EQ(out[6], -16384);
  EXPECT_EQ(mix.size(), 0u);
}

TEST_P(MixBufferBasicTest, StrideWriteInterleavesMonoIntoStereo) {
  RageSoundMixBuffer mix;
  const float mono[3] = {1.0f, 2.0f, 3.0f};
  // Left channel, then right channel offset by one sample.
  mix.write(mono, 3, 1, 2);
  EXPECT_EQ(mix.size(), 5u);  // 3 samples at stride 2 use 5 slots, not 6
  mix.SetWriteOffset(1);
  mix.write(mono, 3, 1, 2);
  EXPECT_EQ(mix.size(), 6u);
  mix.SetWriteOffset(0);
  std::vector<float> out(6);
  // Values exceed 1.0 so compare through the deinterlacer (no clamp).
  float l[3], r[3];
  float* bufs[2] = {l, r};
  mix.read_deinterlace(bufs, 2);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FLOAT_EQ(l[i], mono[i]);
    EXPECT_FLOAT_EQ(r[i], mono[i]);
  }
}

TEST_P(MixBufferBasicTest, SourceStrideSkipsSamples) {
  RageSoundMixBuffer mix;
  const float stereo[6] = {1, 10, 2, 20, 3, 30};
  mix.write(stereo, 3, 2, 1);  // left channel only
  float l[3], r[3];
  float* bufs[1] = {l};
  mix.read_deinterlace(bufs, 1);
  EXPECT_FLOAT_EQ(l[0], 1);
  EXPECT_FLOAT_EQ(l[1], 2);
  EXPECT_FLOAT_EQ(l[2], 3);
  (void)r;
}

TEST_P(MixBufferBasicTest, DeinterlaceSplitsChannels) {
  RageSoundMixBuffer mix;
  const float inter[8] = {1, 10, 2, 20, 3, 30, 4, 40};
  mix.write(inter, 8);
  float l[4], r[4];
  float* bufs[2] = {l, r};
  mix.read_deinterlace(bufs, 2);
  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(l[i], static_cast<float>(i + 1));
    EXPECT_FLOAT_EQ(r[i], static_cast<float>((i + 1) * 10));
  }
  EXPECT_EQ(mix.size(), 0u);
}

TEST_P(MixBufferTest, WriteOffsetLandsOnTheExactFrame) {
  const int rate = GetParam();
  for (int ch = 1; ch <= 2; ++ch) {
    // Start times in ms; the frame index is how a mixer would convert them.
    for (double start_ms : {0.0, 1.0, 10.0, 250.0, 999.0}) {
      SCOPED_TRACE(
          ::testing::Message()
          << rate << " Hz, " << ch << " ch, start " << start_ms << " ms");
      const int start_frame =
          static_cast<int>(std::llround(start_ms * rate / 1000.0));
      const int len_frames = 480;
      std::vector<float> src(static_cast<size_t>(len_frames) * ch);
      for (int f = 0; f < len_frames; ++f) {
        for (int c = 0; c < ch; ++c) {
          src[static_cast<size_t>(f) * ch + c] =
              0.001f * static_cast<float>(f + 1);  // never zero
        }
      }

      RageSoundMixBuffer mix;
      mix.SetWriteOffset(start_frame * ch);
      mix.write(src.data(), static_cast<unsigned>(src.size()));
      ASSERT_EQ(mix.size(), static_cast<size_t>(start_frame + len_frames) * ch);

      std::vector<float> out(mix.size());
      mix.read(out.data());
      for (int f = 0; f < start_frame + len_frames; ++f) {
        for (int c = 0; c < ch; ++c) {
          const float v = out[static_cast<size_t>(f) * ch + c];
          if (f < start_frame) {
            ASSERT_EQ(v, 0.0f) << "frame " << f << " should be silence";
          } else {
            ASSERT_FLOAT_EQ(v, 0.001f * static_cast<float>(f - start_frame + 1))
                << "frame " << f;
          }
        }
      }
    }
  }
}

TEST_P(MixBufferTest, OverlappingSoundsSumAtCorrectFrames) {
  const int rate = GetParam();
  const int ch = 2;
  const int a_start = rate / 100;            // 10 ms
  const int b_start = a_start + rate / 200;  // 5 ms after A
  const int len = rate / 50;                 // 20 ms
  std::vector<float> a(static_cast<size_t>(len) * ch, 0.125f);
  std::vector<float> b(static_cast<size_t>(len) * ch, 0.25f);

  RageSoundMixBuffer mix;
  mix.SetWriteOffset(a_start * ch);
  mix.write(a.data(), static_cast<unsigned>(a.size()));
  mix.SetWriteOffset(b_start * ch);
  mix.write(b.data(), static_cast<unsigned>(b.size()));

  const int end_frame = std::max(a_start + len, b_start + len);
  ASSERT_EQ(mix.size(), static_cast<size_t>(end_frame) * ch);
  std::vector<float> out(mix.size());
  mix.read(out.data());

  for (int f = 0; f < end_frame; ++f) {
    float expect = 0.0f;
    if (f >= a_start && f < a_start + len) {
      expect += 0.125f;
    }
    if (f >= b_start && f < b_start + len) {
      expect += 0.25f;
    }
    for (int c = 0; c < ch; ++c) {
      ASSERT_FLOAT_EQ(out[static_cast<size_t>(f) * ch + c], expect)
          << "frame " << f << " ch " << c;
    }
  }
}

TEST_P(MixBufferTest, ConsecutiveBlocksJoinWithoutGapOrOverlap) {
  // A mixer writes a stream block by block; the frame after block N must be
  // the first frame of block N+1.
  const int rate = GetParam();
  const int ch = 2;
  const int block = rate / 100;  // 10 ms blocks
  const int blocks = 5;
  RageSoundMixBuffer mix;
  int next_frame = 0;
  std::vector<float> stream;
  for (int b = 0; b < blocks; ++b) {
    std::vector<float> src(static_cast<size_t>(block) * ch);
    for (int f = 0; f < block; ++f) {
      for (int c = 0; c < ch; ++c) {
        src[static_cast<size_t>(f) * ch + c] =
            0.00001f * static_cast<float>(next_frame + f + 1);
      }
    }
    mix.SetWriteOffset(next_frame * ch);
    mix.write(src.data(), static_cast<unsigned>(src.size()));
    next_frame += block;
  }
  ASSERT_EQ(mix.size(), static_cast<size_t>(blocks) * block * ch);
  std::vector<float> out(mix.size());
  mix.read(out.data());
  for (int f = 0; f < blocks * block; ++f) {
    for (int c = 0; c < ch; ++c) {
      ASSERT_FLOAT_EQ(
          out[static_cast<size_t>(f) * ch + c],
          0.00001f * static_cast<float>(f + 1))
          << "frame " << f;
    }
  }
}

TEST_P(MixBufferTest, TimestampToFrameRoundTrip) {
  // 1 s of audio: the frame at t seconds is exactly t * rate, never off by
  // one, for several timestamps.
  const int rate = GetParam();
  RageSoundMixBuffer mix;
  std::vector<float> ramp(static_cast<size_t>(rate));
  for (int f = 0; f < rate; ++f) {
    ramp[f] = 1e-6f * static_cast<float>(f);  // value encodes the frame
  }
  mix.write(ramp.data(), static_cast<unsigned>(ramp.size()));
  ASSERT_EQ(mix.size(), static_cast<size_t>(rate));
  std::vector<float> out(mix.size());
  mix.read(out.data());
  for (double t : {0.0, 0.001, 0.01, 0.1, 0.25, 0.5, 0.999}) {
    const int frame = static_cast<int>(std::llround(t * rate));
    ASSERT_LT(frame, rate);
    EXPECT_FLOAT_EQ(out[frame], 1e-6f * static_cast<float>(frame))
        << "t=" << t << " s";
    EXPECT_NEAR(static_cast<double>(frame) / rate, t, 0.5 / rate + 1e-12);
  }
}

}  // namespace
