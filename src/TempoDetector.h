#ifndef TempoDetector_H
#define TempoDetector_H

#include <atomic>
#include <string>
#include <thread>
#include <vector>

/** @brief A single BPM/offset estimate produced by TempoDetector. */
struct TempoResult {
  /** @brief The estimated tempo, in beats per minute. */
  double bpm;
  /**
   * @brief The estimated music offset, in seconds.
   *
   * This uses the same sign convention as TimingData::m_fBeat0OffsetInSeconds,
   * so it can be assigned to a TimingData directly. */
  double offset;
  /** @brief Relative confidence, normalized so all results sum to 1. */
  double fitness;
};

/**
 * @brief Estimates the BPM and offset of a song by analyzing its audio.
 *
 * This is a port of ArrowVortex's tempo detection (src/Editor/FindTempo.cpp),
 * which finds note onsets with aubio and then scores candidate beat intervals
 * by how well the onsets line up with them. The analysis runs on a background
 * thread; poll IsFinished() and read GetProgress() while waiting.
 */
class TempoDetector {
 public:
  /**
   * @brief Decode sMusicPath and begin detecting in the background.
   * @return nullptr if the music could not be read, in which case sError
   *         describes the problem. */
  static TempoDetector* Create(
      const std::string& sMusicPath, std::string& sError);

  /** @brief Stops the worker thread, waiting for it to notice if needed. */
  ~TempoDetector();

  /** @brief A description of the stage that is currently running. */
  std::string GetProgress() const;

  bool IsFinished() const { return finished_.load(); }

  /** @brief The estimates, best first. Only valid once IsFinished() is true. */
  const std::vector<TempoResult>& GetResults() const { return results_; }

 private:
  TempoDetector();
  void Run();

  std::vector<float> samples_;
  int sample_rate_;
  std::vector<TempoResult> results_;

  std::atomic<int> progress_;
  std::atomic<bool> terminate_;
  std::atomic<bool> finished_;
  std::thread thread_;
};

#endif

/**
 * @file
 * @section LICENSE
 * The tempo detection in TempoDetector.cpp is derived from ArrowVortex,
 * Copyright (C) Bram van de Wetering, licensed under the GNU General Public
 * License version 3 or later.
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
 */
