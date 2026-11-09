#include "TempoDetector.h"

#include <aubio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "RageLog.h"
#include "RageSoundReader_FileReader.h"
#include "global.h"

typedef double real;

namespace {

const real kMinimumBpm = 89.0;
const real kMaximumBpm = 205.0;
const int kIntervalDelta = 10;
const int kIntervalDownsample = 3;

// ArrowVortex analyzes at most ten minutes when nothing is selected.
const double kMaxAnalysisSeconds = 600.0;

struct Onset {
  int pos_;
  real strength_;
};

struct DetectorData {
  const float* samples_;
  int samplerate_;
  int num_frames_;
  std::atomic<bool>* terminate_;
  std::atomic<int>* progress_;
  std::vector<TempoResult> result_;
};

struct TempoSort {
  bool operator()(const TempoResult& a, const TempoResult& b) {
    return a.fitness_ > b.fitness_;
  }
};

// ============================================================================
// Onset detection

void FindOnsets(
    const float* samples, int samplerate, int num_frames,
    std::vector<Onset>& out) {
  static const int kWindowlen = 256;
  static const int kBufsize = kWindowlen * 4;
  static const char* method = "complex";

  aubio_onset_t* onset =
      new_aubio_onset(method, kBufsize, kWindowlen, samplerate);
  if (onset == nullptr) {
    return;
  }

  fvec_t* samplevec = new_fvec(kWindowlen);
  fvec_t* beatvec = new_fvec(2);
  for (int i = 0; i <= num_frames - kWindowlen; i += kWindowlen) {
    memcpy(samplevec->data, samples + i, sizeof(float) * kWindowlen);
    aubio_onset_do(onset, samplevec, beatvec);
    if (beatvec->data[0] > 0) {
      int pos = (int)aubio_onset_get_last(onset);
      if (pos >= 0) {
        Onset o = {pos, 1.0};
        out.push_back(o);
      }
    }
  }
  del_fvec(samplevec);
  del_fvec(beatvec);
  del_aubio_onset(onset);
}

// ============================================================================
// Least squares polynomial fitting

struct Matrix {
  Matrix(int rows, int cols)
      : data_(static_cast<size_t>(rows) * cols, 0.0),
        rows_(rows),
        cols_(cols) {}

  static Matrix Identity(int size) {
    Matrix result(size, size);
    for (int i = 0; i < size; ++i) {
      result(i, i) = 1.0;
    }
    return result;
  }

  real& operator()(int row, int col) {
    return data_[col + static_cast<size_t>(cols_) * row];
  }
  real operator()(int row, int col) const {
    return data_[col + static_cast<size_t>(cols_) * row];
  }

  Matrix operator*(const Matrix& other) const {
    Matrix result(rows_, other.cols_);
    for (int r = 0; r < rows_; ++r) {
      for (int ocol = 0; ocol < other.cols_; ++ocol) {
        for (int c = 0; c < cols_; ++c) {
          result(r, ocol) += (*this)(r, c) * other(c, ocol);
        }
      }
    }
    return result;
  }

  Matrix Transpose() const {
    Matrix result(cols_, rows_);
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < cols_; ++c) {
        result(c, r) += (*this)(r, c);
      }
    }
    return result;
  }

  std::vector<real> data_;
  int rows_;
  int cols_;
};

class Givens {
 public:
  Givens() : q_(1, 1), r_(1, 1), j_(2, 2) {}

  // Performs QR factorization using Givens rotations.
  void Decompose(const Matrix& matrix) {
    int rows = matrix.rows_;
    int cols = matrix.cols_;

    if (rows == cols) {
      cols--;
    } else if (rows < cols) {
      cols = rows - 1;
    }

    q_ = Matrix::Identity(rows);
    r_ = matrix;

    for (int j = 0; j < cols; ++j) {
      for (int i = j + 1; i < rows; ++i) {
        Rotation(r_(j, j), r_(i, j));
        PreMultiply(r_, j, i);
        PreMultiply(q_, j, i);
      }
    }

    q_ = q_.Transpose();
  }

  Matrix Solve(const Matrix& matrix) {
    Matrix qtm = q_.Transpose() * matrix;
    int cols = r_.cols_;
    Matrix s(1, cols);
    for (int i = cols - 1; i >= 0; --i) {
      s(0, i) = qtm(i, 0);
      for (int j = i + 1; j < cols; ++j) {
        s(0, i) -= s(0, j) * r_(i, j);
      }
      s(0, i) /= r_(i, i);
    }
    return s;
  }

 private:
  void Rotation(real a, real b) {
    real t, s, c;
    if (b == 0) {
      c = (a >= 0) ? 1.0 : -1.0;
      s = 0;
    } else if (a == 0) {
      c = 0;
      s = (b >= 0) ? -1.0 : 1.0;
    } else if (std::fabs(b) > std::fabs(a)) {
      t = a / b;
      s = -1 / std::sqrt(1 + t * t);
      c = -s * t;
    } else {
      t = b / a;
      c = 1 / std::sqrt(1 + t * t);
      s = -c * t;
    }
    j_(0, 0) = c;
    j_(0, 1) = -s;
    j_(1, 0) = s;
    j_(1, 1) = c;
  }

  void PreMultiply(Matrix& matrix, int i, int j) {
    int row_size = matrix.cols_;
    for (int row = 0; row < row_size; ++row) {
      real temp = matrix(i, row) * j_(0, 0) + matrix(j, row) * j_(0, 1);
      matrix(j, row) = matrix(i, row) * j_(1, 0) + matrix(j, row) * j_(1, 1);
      matrix(i, row) = temp;
    }
  }

  Matrix q_, r_, j_;
};

// Writes degree + 1 coefficients to outCoefs, skipping zeroed input values.
void PolyFit(
    int degree, real* out_coefs, const real* in_values, int num_non_zero_values,
    int offset_x) {
  ++degree;

  Matrix x_matrix(num_non_zero_values, degree);
  Matrix y_matrix(num_non_zero_values, 1);

  for (int row = 0, i = 0; row < num_non_zero_values; ++row, ++i) {
    while (in_values[i] == 0) {
      ++i;
    }
    y_matrix(row, 0) = in_values[i];
  }

  for (int row = 0, i = 0; row < num_non_zero_values; ++row, ++i) {
    while (in_values[i] == 0) {
      ++i;
    }
    real val = 1.0, x = static_cast<real>(offset_x + i);
    for (int col = 0; col < degree; ++col) {
      x_matrix(row, col) = val;
      val *= x;
    }
  }

  Matrix xt_matrix = x_matrix.Transpose();
  Matrix xtx_matrix = xt_matrix * x_matrix;
  Matrix xty_matrix = xt_matrix * y_matrix;

  Givens givens;
  givens.Decompose(xtx_matrix);
  Matrix coeff = givens.Solve(xty_matrix);

  for (int i = 0; i < degree; ++i) {
    out_coefs[i] = coeff.data_[i];
  }
}

// ============================================================================
// Audio processing

// Creates weights for a hamming window of length n.
void CreateHammingWindow(real* out, int n) {
  const real t = 6.2831853071795864 / static_cast<real>(n - 1);
  for (int i = 0; i < n; ++i) {
    out[i] = 0.54 - 0.46 * std::cos(static_cast<real>(i) * t);
  }
}

// Normalizes the given fitness value based on the given 3rd order poly
// coefficients and interval.
void NormalizeFitness(real& fitness, const real* coefs, real interval) {
  real x = interval, x2 = x * x, x3 = x2 * x;
  fitness -= coefs[0] + coefs[1] * x + coefs[2] * x2 + coefs[3] * x3;
}

// ============================================================================
// Gap confidence evaluation

struct GapData {
  GapData(int buffer_size, int downsample, int num_onsets, const Onset* onsets)
      : onsets_(onsets),
        wrapped_pos_(num_onsets > 0 ? num_onsets : 1, 0),
        wrapped_onsets_(buffer_size > 0 ? buffer_size : 1, 0.0),
        window_(2048 >> downsample, 0.0),
        buffer_size_(buffer_size),
        num_onsets_(num_onsets),
        window_size_(2048 >> downsample),
        downsample_(downsample) {
    CreateHammingWindow(&window_[0], window_size_);
  }

  const Onset* onsets_;
  std::vector<int> wrapped_pos_;
  std::vector<real> wrapped_onsets_;
  std::vector<real> window_;
  int buffer_size_, num_onsets_, window_size_, downsample_;
};

struct IntervalTester {
  IntervalTester(int samplerate, int num_onsets, const Onset* onsets)
      : samplerate_(samplerate), num_onsets_(num_onsets), onsets_(onsets) {
    min_interval_ = static_cast<int>(samplerate * 60.0 / kMaximumBpm + 0.5);
    max_interval_ = static_cast<int>(samplerate * 60.0 / kMinimumBpm + 0.5);
    num_intervals_ = max_interval_ - min_interval_;
    fitness_.assign(num_intervals_ > 0 ? num_intervals_ : 1, 0.0);
  }

  int min_interval_;
  int max_interval_;
  int num_intervals_;
  int samplerate_;
  int num_onsets_;
  const Onset* onsets_;
  std::vector<real> fitness_;
  real coefs_[4];
};

// Returns the confidence value that indicates how many onsets are close to the
// given gap position.
real GapConfidence(const GapData& gapdata, int gap_pos, int interval) {
  int window_size = gapdata.window_size_;
  int half_window_size = window_size / 2;
  const real* window = &gapdata.window_[0];
  const real* wrapped_onsets = &gapdata.wrapped_onsets_[0];
  real area = 0.0;

  int begin_onset = gap_pos - half_window_size;
  int end_onset = gap_pos + half_window_size;

  if (begin_onset < 0) {
    int wrapped_begin = begin_onset + interval;
    for (int i = wrapped_begin; i < interval; ++i) {
      int window_index = i - wrapped_begin;
      area += wrapped_onsets[i] * window[window_index];
    }
    begin_onset = 0;
  }
  if (end_onset > interval) {
    int wrapped_end = end_onset - interval;
    int index_offset = window_size - wrapped_end;
    for (int i = 0; i < wrapped_end; ++i) {
      int window_index = i + index_offset;
      area += wrapped_onsets[i] * window[window_index];
    }
    end_onset = interval;
  }
  for (int i = begin_onset; i < end_onset; ++i) {
    int window_index = i - begin_onset;
    area += wrapped_onsets[i] * window[window_index];
  }

  return area;
}

// Returns the confidence of the best gap value for the given interval.
real GetConfidenceForInterval(GapData& gapdata, int interval) {
  int downsample = gapdata.downsample_;
  int num_onsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrapped_pos = &gapdata.wrapped_pos_[0];
  real* wrapped_onsets = &gapdata.wrapped_onsets_[0];
  memset(wrapped_onsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  int reduced_interval = interval >> downsample;
  for (int i = 0; i < num_onsets; ++i) {
    int pos = (onsets[i].pos_ % interval) >> downsample;
    wrapped_pos[i] = pos;
    wrapped_onsets[pos] += onsets[i].strength_;
  }

  // Record the amount of support for each gap value.
  real highest_confidence = 0.0;
  for (int i = 0; i < num_onsets; ++i) {
    int pos = wrapped_pos[i];
    real confidence = GapConfidence(gapdata, pos, reduced_interval);
    int offbeat_pos = (pos + reduced_interval / 2) % reduced_interval;
    confidence += GapConfidence(gapdata, offbeat_pos, reduced_interval) * 0.5;

    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }

  return highest_confidence;
}

// Returns the confidence of the best gap value for the given BPM value.
real GetConfidenceForBPM(GapData& gapdata, IntervalTester& test, real bpm) {
  int num_onsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrapped_pos = &gapdata.wrapped_pos_[0];
  real* wrapped_onsets = &gapdata.wrapped_onsets_[0];
  memset(wrapped_onsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  real intervalf = test.samplerate_ * 60.0 / bpm;
  int interval = static_cast<int>(intervalf + 0.5);
  for (int i = 0; i < num_onsets; ++i) {
    int pos = static_cast<int>(
        std::fmod(static_cast<real>(onsets[i].pos_), intervalf));
    wrapped_pos[i] = pos;
    wrapped_onsets[pos] += onsets[i].strength_;
  }

  // Record the amount of support for each gap value.
  real highest_confidence = 0.0;
  for (int i = 0; i < num_onsets; ++i) {
    int pos = wrapped_pos[i];
    real confidence = GapConfidence(gapdata, pos, interval);
    int offbeat_pos = (pos + interval / 2) % interval;
    confidence += GapConfidence(gapdata, offbeat_pos, interval) * 0.5;

    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }

  // Normalize the confidence value.
  NormalizeFitness(highest_confidence, test.coefs_, intervalf);

  return highest_confidence;
}

// ============================================================================
// Interval testing

real IntervalToBPM(const IntervalTester& test, int i) {
  return (test.samplerate_ * 60.0) / (i + test.min_interval_);
}

void FillCoarseIntervals(IntervalTester& test, GapData& gapdata) {
  int num_coarse_intervals =
      (test.num_intervals_ + kIntervalDelta - 1) / kIntervalDelta;
  for (int i = 0; i < num_coarse_intervals; ++i) {
    int index = i * kIntervalDelta;
    int interval = test.min_interval_ + index;
    test.fitness_[index] =
        std::max(0.001, GetConfidenceForInterval(gapdata, interval));
  }
}

void FillIntervalRange(
    IntervalTester& test, GapData& gapdata, int begin, int end, int& begin_out,
    int& end_out) {
  begin = std::max(begin, 0);
  end = std::min(end, test.num_intervals_);
  for (int i = begin, interval = test.min_interval_ + begin; i < end;
       ++i, ++interval) {
    real* fit = &test.fitness_[i];
    if (*fit == 0) {
      *fit = GetConfidenceForInterval(gapdata, interval);
      NormalizeFitness(*fit, test.coefs_, static_cast<real>(interval));
      *fit = std::max(*fit, 0.1);
    }
  }
  begin_out = begin;
  end_out = end;
}

int FindBestInterval(const real* fitness, int begin, int end) {
  int best_interval = 0;
  real highest_fitness = 0.0;
  for (int i = begin; i < end; ++i) {
    if (fitness[i] > highest_fitness) {
      highest_fitness = fitness[i];
      best_interval = i;
    }
  }
  return best_interval;
}

// ============================================================================
// BPM testing

// Removes BPM values that are near-duplicates or multiples of a better BPM
// value.
void RemoveDuplicates(std::vector<TempoResult>& tempo) {
  for (int i = 0; i < (int)tempo.size(); ++i) {
    real bpm = tempo[i].bpm_, doubled = bpm * 2.0, halved = bpm * 0.5;
    for (int j = (int)tempo.size() - 1; j > i; --j) {
      real v = tempo[j].bpm_;
      if (std::min(
              std::min(std::fabs(v - bpm), std::fabs(v - doubled)),
              std::fabs(v - halved)) < 0.1) {
        tempo.erase(tempo.begin() + j);
      }
    }
  }
}

// Rounds BPM values that are close to integer values.
void RoundBPMValues(
    IntervalTester& test, GapData& gapdata, std::vector<TempoResult>& tempo) {
  for (TempoResult& t : tempo) {
    real round_bpm = std::round(t.bpm_);
    real diff = std::fabs(t.bpm_ - round_bpm);
    if (diff < 0.01) {
      t.bpm_ = round_bpm;
    } else if (diff < 0.05) {
      real old = GetConfidenceForBPM(gapdata, test, t.bpm_);
      real cur = GetConfidenceForBPM(gapdata, test, round_bpm);
      if (cur > old * 0.99) {
        t.bpm_ = round_bpm;
      }
    }
  }
}

// Finds likely BPM candidates based on the given note onset values.
void CalculateBPM(DetectorData* data, Onset* onsets, int num_onsets) {
  std::vector<TempoResult>& tempo = data->result_;

  // In order to determine the BPM, we need at least two onsets.
  if (num_onsets < 2) {
    return;
  }

  IntervalTester test(data->samplerate_, num_onsets, onsets);
  GapData* gapdata =
      new GapData(test.max_interval_, kIntervalDownsample, num_onsets, onsets);

  // Loop through every 10th possible BPM, later we will fill in those that
  // look interesting.
  FillCoarseIntervals(test, *gapdata);
  int num_coarse_intervals =
      (test.num_intervals_ + kIntervalDelta - 1) / kIntervalDelta;
  if (data->terminate_->load()) {
    delete gapdata;
    return;
  }
  data->progress_->store(2);

  // Determine the polynomial coefficients to approximate the fitness curve
  // and normalize the current fitness values.
  PolyFit(
      3, test.coefs_, &test.fitness_[0], num_coarse_intervals,
      test.min_interval_);
  real max_fitness = 0.001;
  for (int i = 0; i < test.num_intervals_; i += kIntervalDelta) {
    NormalizeFitness(
        test.fitness_[i], test.coefs_,
        static_cast<real>(test.min_interval_ + i));
    max_fitness = std::max(max_fitness, test.fitness_[i]);
  }

  // Refine the intervals around the best intervals.
  real fitness_threshold = max_fitness * 0.4;
  for (int i = 0; i < test.num_intervals_; i += kIntervalDelta) {
    if (test.fitness_[i] > fitness_threshold) {
      int begin = 0, end = 0;
      FillIntervalRange(
          test, *gapdata, i - kIntervalDelta, i + kIntervalDelta, begin, end);
      int best = FindBestInterval(&test.fitness_[0], begin, end);
      TempoResult result = {
          IntervalToBPM(test, best), 0.0, test.fitness_[best]};
      tempo.push_back(result);
    }
  }
  if (data->terminate_->load()) {
    delete gapdata;
    return;
  }
  data->progress_->store(3);

  // At this point we stop the downsampling and upgrade to a more precise gap
  // window.
  delete gapdata;
  gapdata = new GapData(test.max_interval_, 0, num_onsets, onsets);

  // Round BPM values to integers when possible, and remove weaker duplicates.
  std::stable_sort(tempo.begin(), tempo.end(), TempoSort());
  RemoveDuplicates(tempo);
  RoundBPMValues(test, *gapdata, tempo);

  // If the fitness of the first and second option is very close, we ask for a
  // second opinion.
  if (tempo.size() >= 2 && tempo[0].fitness_ / tempo[1].fitness_ < 1.05) {
    for (TempoResult& t : tempo) {
      t.fitness_ = GetConfidenceForBPM(*gapdata, test, t.bpm_);
    }
    std::stable_sort(tempo.begin(), tempo.end(), TempoSort());
  }

  // In all 300 test cases the correct BPM value was part of the top 3
  // choices, so it seems reasonable to discard anything below the top 3 as
  // irrelevant.
  if (tempo.size() > 3) {
    tempo.resize(3);
  }

  // Cleanup.
  delete gapdata;
}

// ============================================================================
// Offset testing

void ComputeSlopes(
    const float* samples, real* out, int num_frames, int samplerate) {
  memset(out, 0, sizeof(real) * num_frames);

  int wh = samplerate / 20;
  if (num_frames < wh * 2) {
    return;
  }

  // Initial sums of the left/right side of the window.
  real sum_l = 0, sum_r = 0;
  for (int i = 0, j = wh; i < wh; ++i, ++j) {
    sum_l += std::fabs(samples[i]);
    sum_r += std::fabs(samples[j]);
  }

  // Slide window over the samples.
  real scalar = 1.0 / static_cast<real>(wh);
  for (int i = wh, end = num_frames - wh; i < end; ++i) {
    // Determine slope value.
    out[i] = std::max(0.0, (sum_r - sum_l) * scalar);

    // Move window.
    real cur = std::fabs(samples[i]);
    sum_l -= std::fabs(samples[i - wh]);
    sum_l += cur;
    sum_r -= cur;
    sum_r += std::fabs(samples[i + wh]);
  }
}

// Returns the most promising offset for the given BPM value.
real GetBaseOffsetValue(GapData& gapdata, int samplerate, real bpm) {
  int num_onsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrapped_pos = &gapdata.wrapped_pos_[0];
  real* wrapped_onsets = &gapdata.wrapped_onsets_[0];
  memset(wrapped_onsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  real intervalf = samplerate * 60.0 / bpm;
  int interval = static_cast<int>(intervalf + 0.5);
  memset(wrapped_onsets, 0, sizeof(real) * interval);
  for (int i = 0; i < num_onsets; ++i) {
    int pos = static_cast<int>(
        std::fmod(static_cast<real>(onsets[i].pos_), intervalf));
    wrapped_pos[i] = pos;
    wrapped_onsets[pos] += 1.0;
  }

  // Record the amount of support for each gap value.
  real highest_confidence = 0.0;
  int offset_pos = 0;
  for (int i = 0; i < num_onsets; ++i) {
    int pos = wrapped_pos[i];
    real confidence = GapConfidence(gapdata, pos, interval);
    int offbeat_pos = (pos + interval / 2) % interval;
    confidence += GapConfidence(gapdata, offbeat_pos, interval) * 0.5;

    if (confidence > highest_confidence) {
      highest_confidence = confidence;
      offset_pos = pos;
    }
  }

  return static_cast<real>(offset_pos) / static_cast<real>(samplerate);
}

// Compares each offset to its corresponding offbeat value, and selects the most
// promising one.
real AdjustForOffbeats(
    DetectorData* data, const real* slopes, real offset, real bpm) {
  int samplerate = data->samplerate_;
  int num_frames = data->num_frames_;

  // Determine the offbeat sample position.
  real seconds_per_beat = 60.0 / bpm;
  real offbeat = offset + seconds_per_beat * 0.5;
  if (offbeat > seconds_per_beat) {
    offbeat -= seconds_per_beat;
  }

  // Calculate the support for both sample positions.
  real end = static_cast<real>(num_frames);
  real interval = seconds_per_beat * samplerate;
  real pos_a = offset * samplerate, sum_a = 0.0;
  real pos_b = offbeat * samplerate, sum_b = 0.0;
  for (; pos_a < end && pos_b < end; pos_a += interval, pos_b += interval) {
    sum_a += slopes[static_cast<int>(pos_a)];
    sum_b += slopes[static_cast<int>(pos_b)];
  }

  // Return the offset with the highest support.
  return (sum_a >= sum_b) ? offset : offbeat;
}

// Selects the best offset value for each of the BPM candidates.
void CalculateOffset(DetectorData* data, Onset* onsets, int num_onsets) {
  std::vector<TempoResult>& tempo = data->result_;
  int samplerate = data->samplerate_;

  if (tempo.empty()) {
    return;
  }

  // Create gapdata buffers for testing.
  real max_interval = 0.0;
  for (TempoResult& t : tempo) {
    max_interval = std::max(max_interval, samplerate * 60.0 / t.bpm_);
  }
  GapData gapdata(static_cast<int>(max_interval + 1.0), 1, num_onsets, onsets);

  // Fill in onset values for each BPM.
  for (TempoResult& t : tempo) {
    t.offset_ = GetBaseOffsetValue(gapdata, samplerate, t.bpm_);
  }

  // The slope representation only depends on the samples, so it is shared by
  // every candidate rather than rebuilt for each one.
  std::vector<real> slopes(data->num_frames_ > 0 ? data->num_frames_ : 1, 0.0);
  ComputeSlopes(data->samples_, &slopes[0], data->num_frames_, samplerate);

  // Test all onsets against their offbeat values, pick the best one.
  for (TempoResult& t : tempo) {
    t.offset_ = AdjustForOffbeats(data, &slopes[0], t.offset_, t.bpm_);
  }
}

// ============================================================================
// Audio decoding

short ToShort(float f) {
  int i = static_cast<int>(std::lrint(f * 32768.0f));
  return static_cast<short>(std::min(std::max(i, -32768), 32767));
}

bool DecodeMono(
    const std::string& music_path, std::vector<float>& samples_out,
    int& sample_rate_out, std::string& error) {
  RageSoundReader_FileReader* reader =
      RageSoundReader_FileReader::OpenFile(music_path, error, nullptr);
  if (reader == nullptr) {
    return false;
  }

  const int sample_rate = reader->GetSampleRate();
  const unsigned channels = reader->GetNumChannels();
  if (sample_rate <= 0 || channels == 0) {
    delete reader;
    error = "unsupported audio format";
    return false;
  }
  sample_rate_out = sample_rate;

  const size_t max_frames =
      static_cast<size_t>(kMaxAnalysisSeconds * sample_rate);
  const int buffer_frames = 4096;
  std::vector<float> buf(static_cast<size_t>(buffer_frames) * channels);

  while (samples_out.size() < max_frames) {
    int got = reader->Read(&buf[0], buffer_frames);
    if (got <= 0) {
      break;
    }
    for (int i = 0; i < got && samples_out.size() < max_frames; ++i) {
      const float l = buf[i * channels + 0];
      const float r = channels >= 2 ? buf[i * channels + 1] : l;
      // Matches ArrowVortex, which mixes down from 16-bit samples.
      samples_out.push_back(
          static_cast<float>(
              static_cast<int>(ToShort(l)) + static_cast<int>(ToShort(r))) /
          65536.0f);
    }
  }

  delete reader;
  return !samples_out.empty();
}

const char* progress_text[] = {"Looking for onsets",  "Scanning intervals",
                               "Refining intervals",  "Selecting BPM values",
                               "Calculating offsets", "Done"};

}  // namespace

TempoDetector::TempoDetector()
    : sample_rate_(0), progress_(0), terminate_(false), finished_(false) {}

TempoDetector::~TempoDetector() {
  terminate_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::string TempoDetector::GetProgress() const {
  const int num_texts =
      static_cast<int>(sizeof(progress_text) / sizeof(progress_text[0]));
  int i = progress_.load();
  i = std::min(std::max(i, 0), num_texts - 1);
  return progress_text[i];
}

TempoDetector* TempoDetector::Create(
    const std::string& music_path, std::string& error) {
  if (music_path.empty()) {
    error = "no music file";
    return nullptr;
  }

  TempoDetector* detector = new TempoDetector;
  if (!DecodeMono(
          music_path, detector->samples_, detector->sample_rate_, error)) {
    delete detector;
    return nullptr;
  }

  detector->thread_ = std::thread(&TempoDetector::Run, detector);
  return detector;
}

void TempoDetector::Run() {
  DetectorData data;
  data.samples_ = &samples_[0];
  data.samplerate_ = sample_rate_;
  data.num_frames_ = (int)samples_.size();
  data.terminate_ = &terminate_;
  data.progress_ = &progress_;

  // Run the aubio onset tracker to find note onsets.
  std::vector<Onset> onsets;
  FindOnsets(data.samples_, data.samplerate_, data.num_frames_, onsets);
  if (!terminate_.load()) {
    progress_.store(1);

    for (int i = 0; i < std::min((int)onsets.size(), 100); ++i) {
      int a = std::max(0, onsets[i].pos_ - 100);
      int b = std::min(data.num_frames_, onsets[i].pos_ + 100);
      float v = 0.0f;
      for (int j = a; j < b; ++j) {
        v += std::fabs(data.samples_[j]);
      }
      v /= static_cast<float>(std::max(1, b - a));
      onsets[i].strength_ = v;
    }

    // Find BPM values.
    CalculateBPM(
        &data, onsets.empty() ? nullptr : &onsets[0], (int)onsets.size());

    if (!terminate_.load()) {
      progress_.store(4);

      // Find offset values.
      CalculateOffset(
          &data, onsets.empty() ? nullptr : &onsets[0], (int)onsets.size());
      progress_.store(5);
    }
  }

  // ArrowVortex reports the fitness as a share of the total, and flips the
  // offset to the sign convention used by the simfile.
  double total_fitness = 0.0;
  for (TempoResult& t : data.result_) {
    total_fitness += t.fitness_;
  }
  for (TempoResult& t : data.result_) {
    t.fitness_ = total_fitness > 0.0 ? t.fitness_ / total_fitness : 0.0;
    t.offset_ = -t.offset_;
  }

  results_ = data.result_;
  finished_.store(true);
}

/*
 * Tempo detection derived from ArrowVortex, (c) Bram van de Wetering.
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
