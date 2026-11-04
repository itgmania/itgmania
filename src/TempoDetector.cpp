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

const real MinimumBPM = 89.0;
const real MaximumBPM = 205.0;
const int IntervalDelta = 10;
const int IntervalDownsample = 3;

// ArrowVortex analyzes at most ten minutes when nothing is selected.
const double MaxAnalysisSeconds = 600.0;

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
    const float* samples, int samplerate, int numFrames,
    std::vector<Onset>& out) {
  static const int windowlen = 256;
  static const int bufsize = windowlen * 4;
  static const char* method = "complex";

  aubio_onset_t* onset =
      new_aubio_onset(method, bufsize, windowlen, samplerate);
  if (onset == nullptr) {
    return;
  }

  fvec_t* samplevec = new_fvec(windowlen);
  fvec_t* beatvec = new_fvec(2);
  for (int i = 0; i <= numFrames - windowlen; i += windowlen) {
    memcpy(samplevec->data, samples + i, sizeof(float) * windowlen);
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
  Matrix(int nRows, int nCols)
      : data_(static_cast<size_t>(nRows) * nCols, 0.0),
        rows_(nRows),
        cols_(nCols) {}

  static Matrix Identity(int nSize) {
    Matrix result(nSize, nSize);
    for (int i = 0; i < nSize; ++i) {
      result(i, i) = 1.0;
    }
    return result;
  }

  real& operator()(int nRow, int nCol) {
    return data_[nCol + static_cast<size_t>(cols_) * nRow];
  }
  real operator()(int nRow, int nCol) const {
    return data_[nCol + static_cast<size_t>(cols_) * nRow];
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
    int nRows = matrix.rows_;
    int nCols = matrix.cols_;

    if (nRows == nCols) {
      nCols--;
    } else if (nRows < nCols) {
      nCols = nRows - 1;
    }

    q_ = Matrix::Identity(nRows);
    r_ = matrix;

    for (int j = 0; j < nCols; ++j) {
      for (int i = j + 1; i < nRows; ++i) {
        Rotation(r_(j, j), r_(i, j));
        PreMultiply(r_, j, i);
        PreMultiply(q_, j, i);
      }
    }

    q_ = q_.Transpose();
  }

  Matrix Solve(const Matrix& matrix) {
    Matrix qtm = q_.Transpose() * matrix;
    int nCols = r_.cols_;
    Matrix s(1, nCols);
    for (int i = nCols - 1; i >= 0; --i) {
      s(0, i) = qtm(i, 0);
      for (int j = i + 1; j < nCols; ++j) {
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
    int nRowSize = matrix.cols_;
    for (int nRow = 0; nRow < nRowSize; ++nRow) {
      real temp = matrix(i, nRow) * j_(0, 0) + matrix(j, nRow) * j_(0, 1);
      matrix(j, nRow) = matrix(i, nRow) * j_(1, 0) + matrix(j, nRow) * j_(1, 1);
      matrix(i, nRow) = temp;
    }
  }

  Matrix q_, r_, j_;
};

// Writes degree + 1 coefficients to outCoefs, skipping zeroed input values.
void PolyFit(
    int degree, real* outCoefs, const real* inValues, int numNonZeroValues,
    int offsetX) {
  ++degree;

  Matrix xMatrix(numNonZeroValues, degree);
  Matrix yMatrix(numNonZeroValues, 1);

  for (int nRow = 0, i = 0; nRow < numNonZeroValues; ++nRow, ++i) {
    while (inValues[i] == 0) {
      ++i;
    }
    yMatrix(nRow, 0) = inValues[i];
  }

  for (int nRow = 0, i = 0; nRow < numNonZeroValues; ++nRow, ++i) {
    while (inValues[i] == 0) {
      ++i;
    }
    real nVal = 1.0, x = static_cast<real>(offsetX + i);
    for (int nCol = 0; nCol < degree; ++nCol) {
      xMatrix(nRow, nCol) = nVal;
      nVal *= x;
    }
  }

  Matrix xtMatrix = xMatrix.Transpose();
  Matrix xtxMatrix = xtMatrix * xMatrix;
  Matrix xtyMatrix = xtMatrix * yMatrix;

  Givens givens;
  givens.Decompose(xtxMatrix);
  Matrix coeff = givens.Solve(xtyMatrix);

  for (int i = 0; i < degree; ++i) {
    outCoefs[i] = coeff.data_[i];
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
  GapData(int bufferSize, int downsample, int numOnsets, const Onset* onsets)
      : onsets_(onsets),
        wrapped_pos_(numOnsets > 0 ? numOnsets : 1, 0),
        wrapped_onsets_(bufferSize > 0 ? bufferSize : 1, 0.0),
        window_(2048 >> downsample, 0.0),
        buffer_size_(bufferSize),
        num_onsets_(numOnsets),
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
  IntervalTester(int samplerate, int numOnsets, const Onset* onsets)
      : samplerate_(samplerate), num_onsets_(numOnsets), onsets_(onsets) {
    min_interval_ = static_cast<int>(samplerate * 60.0 / MaximumBPM + 0.5);
    max_interval_ = static_cast<int>(samplerate * 60.0 / MinimumBPM + 0.5);
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
real GapConfidence(const GapData& gapdata, int gapPos, int interval) {
  int windowSize = gapdata.window_size_;
  int halfWindowSize = windowSize / 2;
  const real* window = &gapdata.window_[0];
  const real* wrappedOnsets = &gapdata.wrapped_onsets_[0];
  real area = 0.0;

  int beginOnset = gapPos - halfWindowSize;
  int endOnset = gapPos + halfWindowSize;

  if (beginOnset < 0) {
    int wrappedBegin = beginOnset + interval;
    for (int i = wrappedBegin; i < interval; ++i) {
      int windowIndex = i - wrappedBegin;
      area += wrappedOnsets[i] * window[windowIndex];
    }
    beginOnset = 0;
  }
  if (endOnset > interval) {
    int wrappedEnd = endOnset - interval;
    int indexOffset = windowSize - wrappedEnd;
    for (int i = 0; i < wrappedEnd; ++i) {
      int windowIndex = i + indexOffset;
      area += wrappedOnsets[i] * window[windowIndex];
    }
    endOnset = interval;
  }
  for (int i = beginOnset; i < endOnset; ++i) {
    int windowIndex = i - beginOnset;
    area += wrappedOnsets[i] * window[windowIndex];
  }

  return area;
}

// Returns the confidence of the best gap value for the given interval.
real GetConfidenceForInterval(GapData& gapdata, int interval) {
  int downsample = gapdata.downsample_;
  int numOnsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrappedPos = &gapdata.wrapped_pos_[0];
  real* wrappedOnsets = &gapdata.wrapped_onsets_[0];
  memset(wrappedOnsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  int reducedInterval = interval >> downsample;
  for (int i = 0; i < numOnsets; ++i) {
    int pos = (onsets[i].pos_ % interval) >> downsample;
    wrappedPos[i] = pos;
    wrappedOnsets[pos] += onsets[i].strength_;
  }

  // Record the amount of support for each gap value.
  real highestConfidence = 0.0;
  for (int i = 0; i < numOnsets; ++i) {
    int pos = wrappedPos[i];
    real confidence = GapConfidence(gapdata, pos, reducedInterval);
    int offbeatPos = (pos + reducedInterval / 2) % reducedInterval;
    confidence += GapConfidence(gapdata, offbeatPos, reducedInterval) * 0.5;

    if (confidence > highestConfidence) {
      highestConfidence = confidence;
    }
  }

  return highestConfidence;
}

// Returns the confidence of the best gap value for the given BPM value.
real GetConfidenceForBPM(GapData& gapdata, IntervalTester& test, real bpm) {
  int numOnsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrappedPos = &gapdata.wrapped_pos_[0];
  real* wrappedOnsets = &gapdata.wrapped_onsets_[0];
  memset(wrappedOnsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  real intervalf = test.samplerate_ * 60.0 / bpm;
  int interval = static_cast<int>(intervalf + 0.5);
  for (int i = 0; i < numOnsets; ++i) {
    int pos = static_cast<int>(
        std::fmod(static_cast<real>(onsets[i].pos_), intervalf));
    wrappedPos[i] = pos;
    wrappedOnsets[pos] += onsets[i].strength_;
  }

  // Record the amount of support for each gap value.
  real highestConfidence = 0.0;
  for (int i = 0; i < numOnsets; ++i) {
    int pos = wrappedPos[i];
    real confidence = GapConfidence(gapdata, pos, interval);
    int offbeatPos = (pos + interval / 2) % interval;
    confidence += GapConfidence(gapdata, offbeatPos, interval) * 0.5;

    if (confidence > highestConfidence) {
      highestConfidence = confidence;
    }
  }

  // Normalize the confidence value.
  NormalizeFitness(highestConfidence, test.coefs_, intervalf);

  return highestConfidence;
}

// ============================================================================
// Interval testing

real IntervalToBPM(const IntervalTester& test, int i) {
  return (test.samplerate_ * 60.0) / (i + test.min_interval_);
}

void FillCoarseIntervals(IntervalTester& test, GapData& gapdata) {
  int numCoarseIntervals =
      (test.num_intervals_ + IntervalDelta - 1) / IntervalDelta;
  for (int i = 0; i < numCoarseIntervals; ++i) {
    int index = i * IntervalDelta;
    int interval = test.min_interval_ + index;
    test.fitness_[index] =
        std::max(0.001, GetConfidenceForInterval(gapdata, interval));
  }
}

void FillIntervalRange(
    IntervalTester& test, GapData& gapdata, int begin, int end, int& beginOut,
    int& endOut) {
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
  beginOut = begin;
  endOut = end;
}

int FindBestInterval(const real* fitness, int begin, int end) {
  int bestInterval = 0;
  real highestFitness = 0.0;
  for (int i = begin; i < end; ++i) {
    if (fitness[i] > highestFitness) {
      highestFitness = fitness[i];
      bestInterval = i;
    }
  }
  return bestInterval;
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
    real roundBPM = std::round(t.bpm_);
    real diff = std::fabs(t.bpm_ - roundBPM);
    if (diff < 0.01) {
      t.bpm_ = roundBPM;
    } else if (diff < 0.05) {
      real old = GetConfidenceForBPM(gapdata, test, t.bpm_);
      real cur = GetConfidenceForBPM(gapdata, test, roundBPM);
      if (cur > old * 0.99) {
        t.bpm_ = roundBPM;
      }
    }
  }
}

// Finds likely BPM candidates based on the given note onset values.
void CalculateBPM(DetectorData* data, Onset* onsets, int numOnsets) {
  std::vector<TempoResult>& tempo = data->result_;

  // In order to determine the BPM, we need at least two onsets.
  if (numOnsets < 2) {
    return;
  }

  IntervalTester test(data->samplerate_, numOnsets, onsets);
  GapData* gapdata =
      new GapData(test.max_interval_, IntervalDownsample, numOnsets, onsets);

  // Loop through every 10th possible BPM, later we will fill in those that
  // look interesting.
  FillCoarseIntervals(test, *gapdata);
  int numCoarseIntervals =
      (test.num_intervals_ + IntervalDelta - 1) / IntervalDelta;
  if (data->terminate_->load()) {
    delete gapdata;
    return;
  }
  data->progress_->store(2);

  // Determine the polynomial coefficients to approximate the fitness curve
  // and normalize the current fitness values.
  PolyFit(
      3, test.coefs_, &test.fitness_[0], numCoarseIntervals,
      test.min_interval_);
  real maxFitness = 0.001;
  for (int i = 0; i < test.num_intervals_; i += IntervalDelta) {
    NormalizeFitness(
        test.fitness_[i], test.coefs_,
        static_cast<real>(test.min_interval_ + i));
    maxFitness = std::max(maxFitness, test.fitness_[i]);
  }

  // Refine the intervals around the best intervals.
  real fitnessThreshold = maxFitness * 0.4;
  for (int i = 0; i < test.num_intervals_; i += IntervalDelta) {
    if (test.fitness_[i] > fitnessThreshold) {
      int begin = 0, end = 0;
      FillIntervalRange(
          test, *gapdata, i - IntervalDelta, i + IntervalDelta, begin, end);
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
  gapdata = new GapData(test.max_interval_, 0, numOnsets, onsets);

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
    const float* samples, real* out, int numFrames, int samplerate) {
  memset(out, 0, sizeof(real) * numFrames);

  int wh = samplerate / 20;
  if (numFrames < wh * 2) {
    return;
  }

  // Initial sums of the left/right side of the window.
  real sumL = 0, sumR = 0;
  for (int i = 0, j = wh; i < wh; ++i, ++j) {
    sumL += std::fabs(samples[i]);
    sumR += std::fabs(samples[j]);
  }

  // Slide window over the samples.
  real scalar = 1.0 / static_cast<real>(wh);
  for (int i = wh, end = numFrames - wh; i < end; ++i) {
    // Determine slope value.
    out[i] = std::max(0.0, (sumR - sumL) * scalar);

    // Move window.
    real cur = std::fabs(samples[i]);
    sumL -= std::fabs(samples[i - wh]);
    sumL += cur;
    sumR -= cur;
    sumR += std::fabs(samples[i + wh]);
  }
}

// Returns the most promising offset for the given BPM value.
real GetBaseOffsetValue(GapData& gapdata, int samplerate, real bpm) {
  int numOnsets = gapdata.num_onsets_;
  const Onset* onsets = gapdata.onsets_;

  int* wrappedPos = &gapdata.wrapped_pos_[0];
  real* wrappedOnsets = &gapdata.wrapped_onsets_[0];
  memset(wrappedOnsets, 0, sizeof(real) * gapdata.buffer_size_);

  // Make a histogram of onset strengths for every position in the interval.
  real intervalf = samplerate * 60.0 / bpm;
  int interval = static_cast<int>(intervalf + 0.5);
  memset(wrappedOnsets, 0, sizeof(real) * interval);
  for (int i = 0; i < numOnsets; ++i) {
    int pos = static_cast<int>(
        std::fmod(static_cast<real>(onsets[i].pos_), intervalf));
    wrappedPos[i] = pos;
    wrappedOnsets[pos] += 1.0;
  }

  // Record the amount of support for each gap value.
  real highestConfidence = 0.0;
  int offsetPos = 0;
  for (int i = 0; i < numOnsets; ++i) {
    int pos = wrappedPos[i];
    real confidence = GapConfidence(gapdata, pos, interval);
    int offbeatPos = (pos + interval / 2) % interval;
    confidence += GapConfidence(gapdata, offbeatPos, interval) * 0.5;

    if (confidence > highestConfidence) {
      highestConfidence = confidence;
      offsetPos = pos;
    }
  }

  return static_cast<real>(offsetPos) / static_cast<real>(samplerate);
}

// Compares each offset to its corresponding offbeat value, and selects the most
// promising one.
real AdjustForOffbeats(
    DetectorData* data, const real* slopes, real offset, real bpm) {
  int samplerate = data->samplerate_;
  int numFrames = data->num_frames_;

  // Determine the offbeat sample position.
  real secondsPerBeat = 60.0 / bpm;
  real offbeat = offset + secondsPerBeat * 0.5;
  if (offbeat > secondsPerBeat) {
    offbeat -= secondsPerBeat;
  }

  // Calculate the support for both sample positions.
  real end = static_cast<real>(numFrames);
  real interval = secondsPerBeat * samplerate;
  real posA = offset * samplerate, sumA = 0.0;
  real posB = offbeat * samplerate, sumB = 0.0;
  for (; posA < end && posB < end; posA += interval, posB += interval) {
    sumA += slopes[static_cast<int>(posA)];
    sumB += slopes[static_cast<int>(posB)];
  }

  // Return the offset with the highest support.
  return (sumA >= sumB) ? offset : offbeat;
}

// Selects the best offset value for each of the BPM candidates.
void CalculateOffset(DetectorData* data, Onset* onsets, int numOnsets) {
  std::vector<TempoResult>& tempo = data->result_;
  int samplerate = data->samplerate_;

  if (tempo.empty()) {
    return;
  }

  // Create gapdata buffers for testing.
  real maxInterval = 0.0;
  for (TempoResult& t : tempo) {
    maxInterval = std::max(maxInterval, samplerate * 60.0 / t.bpm_);
  }
  GapData gapdata(static_cast<int>(maxInterval + 1.0), 1, numOnsets, onsets);

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
    const std::string& sMusicPath, std::vector<float>& samplesOut,
    int& iSampleRateOut, std::string& sError) {
  RageSoundReader_FileReader* pReader =
      RageSoundReader_FileReader::OpenFile(sMusicPath, sError, nullptr);
  if (pReader == nullptr) {
    return false;
  }

  const int iSampleRate = pReader->GetSampleRate();
  const unsigned iChannels = pReader->GetNumChannels();
  if (iSampleRate <= 0 || iChannels == 0) {
    delete pReader;
    sError = "unsupported audio format";
    return false;
  }
  iSampleRateOut = iSampleRate;

  const size_t iMaxFrames =
      static_cast<size_t>(MaxAnalysisSeconds * iSampleRate);
  const int iBufferFrames = 4096;
  std::vector<float> buf(static_cast<size_t>(iBufferFrames) * iChannels);

  while (samplesOut.size() < iMaxFrames) {
    int iGot = pReader->Read(&buf[0], iBufferFrames);
    if (iGot <= 0) {
      break;
    }
    for (int i = 0; i < iGot && samplesOut.size() < iMaxFrames; ++i) {
      const float fL = buf[i * iChannels + 0];
      const float fR = iChannels >= 2 ? buf[i * iChannels + 1] : fL;
      // Matches ArrowVortex, which mixes down from 16-bit samples.
      samplesOut.push_back(
          static_cast<float>(
              static_cast<int>(ToShort(fL)) + static_cast<int>(ToShort(fR))) /
          65536.0f);
    }
  }

  delete pReader;
  return !samplesOut.empty();
}

const char* sProgressText[] = {"Looking for onsets",  "Scanning intervals",
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
  const int iNumTexts =
      static_cast<int>(sizeof(sProgressText) / sizeof(sProgressText[0]));
  int i = progress_.load();
  i = std::min(std::max(i, 0), iNumTexts - 1);
  return sProgressText[i];
}

TempoDetector* TempoDetector::Create(
    const std::string& sMusicPath, std::string& sError) {
  if (sMusicPath.empty()) {
    sError = "no music file";
    return nullptr;
  }

  TempoDetector* pDetector = new TempoDetector;
  if (!DecodeMono(
          sMusicPath, pDetector->samples_, pDetector->sample_rate_, sError)) {
    delete pDetector;
    return nullptr;
  }

  pDetector->thread_ = std::thread(&TempoDetector::Run, pDetector);
  return pDetector;
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
  double fTotalFitness = 0.0;
  for (TempoResult& t : data.result_) {
    fTotalFitness += t.fitness_;
  }
  for (TempoResult& t : data.result_) {
    t.fitness_ = fTotalFitness > 0.0 ? t.fitness_ / fTotalFitness : 0.0;
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
