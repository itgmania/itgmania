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
    const std::vector<int>& notesPerMeasure, int notesThreshold) {
  std::vector<StreamSegment> streamSegments;

  // Skip first break segment.
  size_t firstStream = 0;
  while (firstStream < notesPerMeasure.size() &&
         notesPerMeasure[firstStream] < notesThreshold) {
    ++firstStream;
  }
  if (firstStream == notesPerMeasure.size()) {
    return streamSegments;
  }

  // Skip last break segment.
  size_t lastStream = notesPerMeasure.size();
  while (notesPerMeasure[lastStream - 1] < notesThreshold) {
    --lastStream;
  }

  size_t i = firstStream;
  while (i < lastStream) {
    const bool isStream = notesPerMeasure[i] >= notesThreshold;
    const size_t start = i;
    while (i < lastStream &&
           (notesPerMeasure[i] >= notesThreshold) == isStream) {
      ++i;
    }
    streamSegments.push_back(
        {static_cast<int>(start), static_cast<int>(i), !isStream});
  }

  return streamSegments;
}

std::string GetBreakNotation(int segmentSize) {
  if (segmentSize <= 4) {
    return "-";
  }
  if (segmentSize < 32) {
    return "/";
  }
  return " | ";
}

std::string GenerateBreakdownTextLevel0(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  for (const StreamSegment& segment : segments) {
    const int segmentSize = segment.stream_end_ - segment.stream_start_;
    if (!segment.is_break_) {
      text += std::to_string(segmentSize);
    } else if (segmentSize == 1) {
      text += "-";
    } else {
      text += " (" + std::to_string(segmentSize) + ") ";
    }
  }
  return text;
}

std::string GenerateBreakdownTextLevel1(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  for (const StreamSegment& segment : segments) {
    const int segmentSize = segment.stream_end_ - segment.stream_start_;
    if (segment.is_break_) {
      text += GetBreakNotation(segmentSize);
    } else {
      text += std::to_string(segmentSize);
    }
  }
  return text;
}

std::string GenerateBreakdownTextLevel2(
    const std::vector<StreamSegment>& segments) {
  std::string text;
  int segmentSum = 0;
  bool isBroken = false;

  for (const StreamSegment& segment : segments) {
    const int segmentSize = segment.stream_end_ - segment.stream_start_;
    if (!segment.is_break_) {
      segmentSum += segmentSize;
      continue;
    }
    if (segmentSize == 1) {
      // Fold one measure breaks into the current group.
      ++segmentSum;
      isBroken = true;
      continue;
    }
    if (segmentSum != 0) {
      text += std::to_string(segmentSum);
      if (isBroken) {
        text += "*";
      }
    }
    text += GetBreakNotation(segmentSize);
    segmentSum = 0;
    isBroken = false;
  }

  if (segmentSum != 0) {
    text += std::to_string(segmentSum);
    if (isBroken) {
      text += "*";
    }
  }
  return text;
}

}  // namespace

StreamBreakdown StreamBreakdown::Calculate(
    const std::vector<int>& notesPerMeasure, int notesThreshold) {
  StreamBreakdown breakdown;
  if (notesPerMeasure.empty()) {
    return breakdown;
  }

  const std::vector<StreamSegment> segments =
      GetStreamSegments(notesPerMeasure, notesThreshold);

  for (const StreamSegment& segment : segments) {
    if (!segment.is_break_) {
      breakdown.total_stream_measures_ +=
          segment.stream_end_ - segment.stream_start_;
    }
  }
  breakdown.total_break_measures_ = static_cast<int>(notesPerMeasure.size()) -
                                    breakdown.total_stream_measures_;

  breakdown.texts_[0] = GenerateBreakdownTextLevel0(segments);
  breakdown.texts_[1] = GenerateBreakdownTextLevel1(segments);
  breakdown.texts_[2] = GenerateBreakdownTextLevel2(segments);

  return breakdown;
}
