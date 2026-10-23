#ifndef STREAM_BREAKDOWN_H
#define STREAM_BREAKDOWN_H

#include <array>
#include <string>
#include <vector>

/**
 * @brief Stream and break information derived from the notes-per-measure data.
 *
 * The breakdown text uses the following notation:
 *   "-" = Break between [0, 4] measures
 *   "/" = Break between [5, 32) measures
 *   "|" = Break greater than or equal to 32 measures
 */
struct StreamBreakdown {
  static constexpr int kNotesThreshold = 16;
  static constexpr int kNumCachedMinimizationLevels = 3;

  /** @brief Cached breakdown text for minimization levels 0 through 2. */
  std::array<std::string, kNumCachedMinimizationLevels> texts;

  int totalStreamMeasures = 0;
  int totalBreakMeasures = 0;

  /**
   * @brief Calculate the breakdown from a chart's notes-per-measure data.
   *
   * If notesPerMeasure is empty or contains no measures meeting the stream
   * threshold, all of the texts will be empty. */
  static StreamBreakdown Calculate(
      const std::vector<int>& notesPerMeasure,
      int notesThreshold = kNotesThreshold);
};

#endif
