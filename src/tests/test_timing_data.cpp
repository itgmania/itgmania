#include <gtest/gtest.h>

#include "NoteTypes.h"
#include "TimingData.h"
#include "TimingSegments.h"

namespace {

constexpr float kEpsilon = 1e-4f;

void AddBpm(TimingData& td, float beat, float bpm) {
  td.AddSegment(BPMSegment(BeatToNoteRow(beat), bpm));
}

void AddStop(TimingData& td, float beat, float seconds) {
  td.AddSegment(StopSegment(BeatToNoteRow(beat), seconds));
}

void AddDelay(TimingData& td, float beat, float seconds) {
  td.AddSegment(DelaySegment(BeatToNoteRow(beat), seconds));
}

void AddWarp(TimingData& td, float beat, float length) {
  td.AddSegment(WarpSegment(BeatToNoteRow(beat), length));
}

void ExpectBeat(TimingData& td, float time, float beat) {
  EXPECT_NEAR(td.GetBeatFromElapsedTimeNoOffset(time), beat, kEpsilon)
      << "time " << time;
}

void ExpectTime(TimingData& td, float beat, float time) {
  EXPECT_NEAR(td.GetElapsedTimeFromBeatNoOffset(beat), time, kEpsilon)
      << "beat " << beat;
}

struct TimeBeatCase {
  float time;
  float beat;
};

struct BeatTimeCase {
  float beat;
  float time;
};

}  // namespace

TEST(TimingDataTest, ConstantBpm) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));

  ExpectBeat(td, 60.0f, 120.0f);
  ExpectBeat(td, -60.0f, -120.0f);
  ExpectBeat(td, 100000.0f, 200000.0f);
  ExpectBeat(td, -100000.0f, -200000.0f);

  ExpectTime(td, 60.0f, 30.0f);
  ExpectTime(td, -60.0f, -30.0f);
  ExpectTime(td, 100000.0f, 50000.0f);
  ExpectTime(td, -100000.0f, -50000.0f);

  EXPECT_FLOAT_EQ(td.GetBPMAtBeat(0.0f), 120.0f);
  EXPECT_FLOAT_EQ(td.GetBPMAtBeat(100000.0f), 120.0f);
}

TEST(TimingDataTest, BpmChange) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));
  AddBpm(td, 10.0f, 60.0f);

  EXPECT_FLOAT_EQ(td.GetBPMAtBeat(9.9f), 120.0f);
  EXPECT_FLOAT_EQ(td.GetBPMAtBeat(10.0f), 60.0f);

  ExpectBeat(td, 4.0f, 8.0f);
  ExpectBeat(td, 5.0f, 10.0f);
  ExpectBeat(td, 5.5f, 10.5f);

  ExpectTime(td, 9.0f, 4.5f);
  ExpectTime(td, 10.0f, 5.0f);
  ExpectTime(td, 11.0f, 6.0f);
}

TEST(TimingDataTest, Stops) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));
  AddBpm(td, 10.0f, 60.0f);
  AddStop(td, 5.0f, 2.0f);
  AddStop(td, 15.0f, 5.0f);

  const TimeBeatCase timeToBeat[] = {
      {1.0f, 2.0f},   {2.5f, 5.0f},   {3.0f, 5.0f},   {4.5f, 5.0f},
      {5.0f, 6.0f},   {7.0f, 10.0f},  {8.0f, 11.0f},  {12.0f, 15.0f},
      {14.0f, 15.0f}, {17.0f, 15.0f}, {18.0f, 16.0f},
  };
  for (const TimeBeatCase& c : timeToBeat) {
    SCOPED_TRACE(c.time);
    ExpectBeat(td, c.time, c.beat);
  }

  const BeatTimeCase beatToTime[] = {
      {1.0f, 0.5f},  {2.0f, 1.0f},  {5.0f, 2.5f},   {6.0f, 5.0f},
      {10.0f, 7.0f}, {12.0f, 9.0f}, {15.0f, 12.0f}, {16.0f, 18.0f},
  };
  for (const BeatTimeCase& c : beatToTime) {
    SCOPED_TRACE(c.beat);
    ExpectTime(td, c.beat, c.time);
  }

  TimingData::GetBeatArgs args;
  args.elapsed_time = 3.0f;
  td.GetBeatAndBPSFromElapsedTimeNoOffset(args);
  EXPECT_FLOAT_EQ(args.beat, 5.0f);
  EXPECT_FLOAT_EQ(args.bps_out, 2.0f);
  EXPECT_TRUE(args.freeze_out);
  EXPECT_FALSE(args.delay_out);
}

TEST(TimingDataTest, StopAtBeatZero) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));
  AddStop(td, 0.0f, 1.0f);

  ExpectBeat(td, -1.0f, -2.0f);
  ExpectBeat(td, 0.0f, 0.0f);
  ExpectBeat(td, 0.5f, 0.0f);
  ExpectBeat(td, 1.0f, 0.0f);
  ExpectBeat(td, 2.0f, 2.0f);

  ExpectTime(td, -1.0f, -0.5f);
  ExpectTime(td, 0.0f, 0.0f);
  ExpectTime(td, 1.0f, 1.5f);
  ExpectTime(td, 2.0f, 2.0f);

  TimingData::GetBeatArgs args;
  args.elapsed_time = 0.5f;
  td.GetBeatAndBPSFromElapsedTimeNoOffset(args);
  EXPECT_FLOAT_EQ(args.beat, 0.0f);
  EXPECT_TRUE(args.freeze_out);
  EXPECT_FALSE(args.delay_out);
}

TEST(TimingDataTest, DelayAndStopOnSameRow) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));
  AddDelay(td, 5.0f, 1.0f);
  AddStop(td, 5.0f, 2.0f);

  ExpectBeat(td, 2.0f, 4.0f);
  ExpectBeat(td, 2.5f, 5.0f);
  ExpectBeat(td, 3.0f, 5.0f);
  ExpectBeat(td, 3.5f, 5.0f);
  ExpectBeat(td, 4.0f, 5.0f);
  ExpectBeat(td, 5.5f, 5.0f);
  ExpectBeat(td, 6.0f, 6.0f);

  ExpectTime(td, 4.0f, 2.0f);
  ExpectTime(td, 5.0f, 3.5f);
  ExpectTime(td, 6.0f, 6.0f);

  TimingData::GetBeatArgs delayArgs;
  delayArgs.elapsed_time = 3.0f;
  td.GetBeatAndBPSFromElapsedTimeNoOffset(delayArgs);
  EXPECT_FLOAT_EQ(delayArgs.beat, 5.0f);
  EXPECT_FALSE(delayArgs.freeze_out);
  EXPECT_TRUE(delayArgs.delay_out);

  TimingData::GetBeatArgs stopArgs;
  stopArgs.elapsed_time = 4.0f;
  td.GetBeatAndBPSFromElapsedTimeNoOffset(stopArgs);
  EXPECT_FLOAT_EQ(stopArgs.beat, 5.0f);
  EXPECT_TRUE(stopArgs.freeze_out);
  EXPECT_FALSE(stopArgs.delay_out);
}

TEST(TimingDataTest, Warps) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));
  AddWarp(td, 2.0f, 2.0f);

  ExpectBeat(td, 0.5f, 1.0f);
  ExpectBeat(td, 0.75f, 1.5f);
  ExpectBeat(td, 1.0f, 4.0f);
  ExpectBeat(td, 1.25f, 4.5f);
  ExpectBeat(td, 1.5f, 5.0f);

  ExpectTime(td, 1.0f, 0.5f);
  ExpectTime(td, 1.5f, 0.75f);
  ExpectTime(td, 2.0f, 1.0f);
  ExpectTime(td, 3.0f, 1.0f);
  ExpectTime(td, 4.0f, 1.0f);
  ExpectTime(td, 5.0f, 1.5f);
  ExpectTime(td, 6.0f, 2.0f);

  TimingData::GetBeatArgs args;
  args.elapsed_time = 1.25f;
  td.GetBeatAndBPSFromElapsedTimeNoOffset(args);
  EXPECT_FLOAT_EQ(args.beat, 4.5f);
  EXPECT_EQ(args.warp_begin_out, BeatToNoteRow(2.0f));
  EXPECT_FLOAT_EQ(args.warp_dest_out, 4.0f);
}

TEST(TimingDataTest, LookupInvalidation) {
  TimingData td;
  td.AddSegment(BPMSegment(0, 120.0f));

  ExpectBeat(td, 6.0f, 12.0f);

  AddBpm(td, 10.0f, 60.0f);

  ExpectBeat(td, 6.0f, 11.0f);
  ExpectTime(td, 11.0f, 6.0f);
  ExpectBeat(td, 6.0f, 11.0f);
}

TEST(TimingDataTest, BeatZeroOffset) {
  TimingData td(0.5f);
  td.AddSegment(BPMSegment(0, 120.0f));

  ExpectTime(td, 0.0f, -0.5f);
  ExpectTime(td, 1.0f, 0.0f);
  ExpectTime(td, 2.0f, 0.5f);

  ExpectBeat(td, -0.5f, 0.0f);
  ExpectBeat(td, 0.0f, 1.0f);
  ExpectBeat(td, 0.5f, 2.0f);
}
