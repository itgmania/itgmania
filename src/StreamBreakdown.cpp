#include "StreamBreakdown.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

struct StreamSegment {
  int stream_start_;
  int stream_end_;
  bool is_break_;
};

std::vector<StreamSegment> GetStreamSegments(
    const std::vector<int>& notes_per_measure, int notes_threshold) {
  std::vector<StreamSegment> stream_segments;

  // Skip first break segment.
  size_t first_stream = 0;
  while (first_stream < notes_per_measure.size() &&
         notes_per_measure[first_stream] < notes_threshold) {
    ++first_stream;
  }
  if (first_stream == notes_per_measure.size()) {
    return stream_segments;
  }

  // Skip last break segment.
  size_t last_stream = notes_per_measure.size();
  while (notes_per_measure[last_stream - 1] < notes_threshold) {
    --last_stream;
  }

  size_t i = first_stream;
  while (i < last_stream) {
    const bool is_stream = notes_per_measure[i] >= notes_threshold;
    const size_t start = i;
    while (i < last_stream &&
           (notes_per_measure[i] >= notes_threshold) == is_stream) {
      ++i;
    }
    stream_segments.push_back(
        {static_cast<int>(start), static_cast<int>(i), !is_stream});
  }

  return stream_segments;
}

std::string GetBreakNotation(int segment_size) {
  if (segment_size <= 4) {
    return "-";
  }
  if (segment_size < 32) {
    return "/";
  }
  return " | ";
}

std::string GenerateBreakdownTextLevel0(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  for (const StreamSegment& segment : segments) {
    const int segment_size = segment.stream_end_ - segment.stream_start_;
    if (!segment.is_break_) {
      text += std::to_string(segment_size);
    } else if (segment_size == 1) {
      text += "-";
    } else {
      text += " (" + std::to_string(segment_size) + ") ";
    }
  }
  return text;
}

std::string GenerateBreakdownTextLevel1(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  for (const StreamSegment& segment : segments) {
    const int segment_size = segment.stream_end_ - segment.stream_start_;
    if (segment.is_break_) {
      text += GetBreakNotation(segment_size);
    } else {
      text += std::to_string(segment_size);
    }
  }
  return text;
}

std::string GenerateBreakdownTextLevel2(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  int segment_sum = 0;
  bool is_broken = false;

  for (const StreamSegment& segment : segments) {
    const int segment_size = segment.stream_end_ - segment.stream_start_;
    if (!segment.is_break_) {
      segment_sum += segment_size;
      continue;
    }
    if (segment_size == 1) {
      // Fold one measure breaks into the current group.
      ++segment_sum;
      is_broken = true;
      continue;
    }
    if (segment_sum != 0) {
      text += std::to_string(segment_sum);
      if (is_broken) {
        text += "*";
      }
    }
    text += GetBreakNotation(segment_size);
    segment_sum = 0;
    is_broken = false;
  }

  if (segment_sum != 0) {
    text += std::to_string(segment_sum);
    if (is_broken) {
      text += "*";
    }
  }
  return text;
}

}  // namespace

StreamBreakdown StreamBreakdown::Calculate(
    const std::vector<int>& notes_per_measure, int notes_threshold) {
  StreamBreakdown breakdown;
  if (notes_per_measure.empty()) {
    return breakdown;
  }

  const std::vector<StreamSegment> segments =
      GetStreamSegments(notes_per_measure, notes_threshold);

  for (const StreamSegment& segment : segments) {
    if (!segment.is_break_) {
      breakdown.total_stream_measures_ +=
          segment.stream_end_ - segment.stream_start_;
    }
  }
  breakdown.total_break_measures_ = static_cast<int>(notes_per_measure.size()) -
                                    breakdown.total_stream_measures_;

  breakdown.texts_[0] = GenerateBreakdownTextLevel0(segments);
  breakdown.texts_[1] = GenerateBreakdownTextLevel1(segments);
  breakdown.texts_[2] = GenerateBreakdownTextLevel2(segments);

  return breakdown;
}
