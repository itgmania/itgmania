#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

#include "RageTimer.h"

// Deviation thresholds are defined as such...
//   <= 1 ms - fine.
//   >  1 ms gets FLAGGED. (we hope for far less than this)
//   >  5 ms is deemed NOTICEABLE. A sudden clock deviation of this much is
//   extremely noticeable, but this value is more of a target across long
//   uptimes (some people leave the game open for several days etc). > 10 ms is
//   an EXTREME fault  (never acceptable, will 100% be noticed whether it's
//   happened instaneously or gradually over many hours)
// Anything above 1 ms fails the test, with the severity named in the message.
//
// For context, RageTimer has two jobs, one is to provide a game uptime clock.
// We take a timestamp at game launch and then afterwards take a timestamp of
// the current point in time and then subtract that timestamp, so we know how
// long the game has been open for. The other is to handle RageTimer objects,
// which are typically used to calculate timing intervals or timing deltas with
// a value as a floating point in seconds. Since the real clock portion is based
// on std::chrono::steady_clock timestamps, it cannot be fast-forwarded. So, the
// live clock is tested for real (immediate response, short sleeps), the long
// checkpoints (5 min ... 256 h) are tested by feeding synthetic durations
// through the same conversion pipeline and through RageTimer's timestamp
// arithmetic at those same magnitues.

namespace {

constexpr double kFlagS = 0.001;
constexpr double kNoticeableS = 0.005;
constexpr double kExtremeS = 0.010;

const char* Severity(double deviation) {
  const double d = std::fabs(deviation);
  if (d > kExtremeS) {
    return "EXTREME FAULT (>10ms)";
  }
  if (d > kNoticeableS) {
    return "NOTICEABLE (>5ms)";
  }
  if (d > kFlagS) {
    return "FLAGGED (>1ms)";
  }
  return "ok";
}

::testing::AssertionResult WithinSyncBudget(double expected, double actual) {
  const double dev = actual - expected;
  if (std::fabs(dev) <= kFlagS) {
    return ::testing::AssertionSuccess();
  }
  std::ostringstream os;
  os.precision(9);
  os << Severity(dev) << ": expected " << expected << " s, got " << actual
     << " s, deviation " << dev * 1000.0 << " ms";
  return ::testing::AssertionFailure() << os.str();
}

struct Checkpoint {
  const char* name;
  uint64_t seconds;
};

constexpr uint64_t kMin = 60;
constexpr uint64_t kHour = 3600;

// Make sure no funny business happens right after hour roll-overs, so test 5
// minutes after each hour too
const Checkpoint kCheckpoints[] = {
    {"immediate(10s)", 10},
    {"5m", 5 * kMin},
    {"15m", 15 * kMin},
    {"30m", 30 * kMin},
    {"45m", 45 * kMin},
    {"1h", 1 * kHour},
    {"1h05m", 1 * kHour + 5 * kMin},
    {"1h15m", 1 * kHour + 15 * kMin},
    {"1h30m", 1 * kHour + 30 * kMin},
    {"1h45m", 1 * kHour + 45 * kMin},
    {"2h", 2 * kHour},
    {"2h05m", 2 * kHour + 5 * kMin},
    {"2h15m", 2 * kHour + 15 * kMin},
    {"2h30m", 2 * kHour + 30 * kMin},
    {"2h45m", 2 * kHour + 45 * kMin},
    {"3h", 3 * kHour},
    {"3h05m", 3 * kHour + 5 * kMin},
    {"4h", 4 * kHour},
    {"4h05m", 3 * kHour + 5 * kMin},
    {"5h", 5 * kHour},
    {"5h05m", 3 * kHour + 5 * kMin},
    {"6h", 6 * kHour},
    {"6h05m", 3 * kHour + 5 * kMin},
    {"24h", 24 * kHour},
    {"48h", 48 * kHour},
    {"256h", 256 * kHour},  // close to two weeks
    {"500h", 500 * kHour},
    {"1000h", 1000 * kHour},
    {"10000h", 10000 * kHour},
};

// The same three conversions RageTimer.cpp applies to
// (steady_clock::now() - start), applied to a synthetic duration.
template <typename Dur>
double ToDouble(Dur d) {
  return std::chrono::duration<double>(d).count();
}
template <typename Dur>
int ToSeconds(Dur d) {
  return std::chrono::duration_cast<std::chrono::seconds>(d).count();
}
template <typename Dur>
uint64_t ToMicros(Dur d) {
  return std::chrono::duration_cast<
             std::chrono::duration<uint64_t, std::micro>>(d)
      .count();
}

// Time-since-start: live clock
TEST(RageTimerSinceStart, ImmediateResponseIsWithinOneSecondOfStart) {
  const double t = RageTimer::GetTimeSinceStart();
  EXPECT_GE(t, 0.0);
  EXPECT_LT(t, 1.0) << "Process start -> first test took over a second.";
  EXPECT_EQ(RageTimer::GetTimeSinceStartSeconds(), 0);
  EXPECT_LT(RageTimer::GetTimeSinceStartMicroseconds(), 1000000u);
}

TEST(RageTimerSinceStart, IsMonotonic) {
  double prev = RageTimer::GetTimeSinceStart();
  for (int i = 0; i < 10000; ++i) {
    const double cur = RageTimer::GetTimeSinceStart();
    ASSERT_GE(cur, prev);
    prev = cur;
  }
}

TEST(RageTimerSinceStart, TracksWallTimeAcrossSleep) {
  const double a = RageTimer::GetTimeSinceStart();
  const auto w0 = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const double b = RageTimer::GetTimeSinceStart();
  const auto w1 = std::chrono::steady_clock::now();
  EXPECT_TRUE(
      WithinSyncBudget(std::chrono::duration<double>(w1 - w0).count(), b - a));
  EXPECT_GE(b - a, 0.199);
}

// Time-since-start: long checkpoints (synthetic durations)

TEST(RageTimerSinceStart, CheckpointConversionsAreExact) {
  for (const Checkpoint& c : kCheckpoints) {
    SCOPED_TRACE(c.name);
    const double expected = static_cast<double>(c.seconds);
    // A sub-millisecond offset should, in theory, make sure fractions survive
    // at every scale.
    const auto d =
        std::chrono::seconds(c.seconds) + std::chrono::nanoseconds(123456789);
    const double expected_frac = expected + 0.123456789;

    EXPECT_TRUE(WithinSyncBudget(expected_frac, ToDouble(d)));
    EXPECT_EQ(ToSeconds(d), static_cast<int>(c.seconds));
    EXPECT_EQ(ToMicros(d), c.seconds * 1000000ULL + 123456u);

    // Whole-second durations should be exact in all three.
    const auto w = std::chrono::seconds(c.seconds);
    EXPECT_DOUBLE_EQ(ToDouble(w), expected);
    EXPECT_EQ(ToMicros(w), c.seconds * 1000000ULL);
  }
}

TEST(RageTimerSinceStart, CheckpointValuesFitReturnTypes) {
  // 256h = 921,600 s; microseconds = 9.2e11, far beyond 32 bits.
  const auto d = std::chrono::hours(256);
  EXPECT_GT(ToMicros(d), static_cast<uint64_t>(UINT32_MAX));
  EXPECT_EQ(ToSeconds(d), 921600);
  // int seconds stays valid for decades; guard against a narrowing change.
  EXPECT_LT(ToSeconds(std::chrono::hours(24 * 365 * 10)), INT32_MAX);
}

// Whether the three variants agree
// (context: StepMania traditionally uses just GetTimeSinceStart which returned
// a float. SM5 added a function to return the microseconds time since start as
// an unsigned 64 bit integer. We now have one that returns a double in seconds,
// one that returns microseconds as 64 bit unsigned, and one that returns an int
// in seconds (since a lot of places had been using the floating point
// GetTimeSinceStart, but casting to an int).

namespace {
void ExpectVariantsAgree() {
  // Bracket the double read between two microsecond reads, and the seconds
  // read between two double reads, retrying if a read straddled nothing odd.
  const uint64_t us0 = RageTimer::GetTimeSinceStartMicroseconds();
  const double d = RageTimer::GetTimeSinceStart();
  const int s = RageTimer::GetTimeSinceStartSeconds();
  const uint64_t us1 = RageTimer::GetTimeSinceStartMicroseconds();

  EXPECT_GE(d * 1e6, static_cast<double>(us0) - 1.0);
  EXPECT_LE(d * 1e6, static_cast<double>(us1) + 1.0);
  // Seconds is a truncation of the same clock, bracketed by the micro reads.
  EXPECT_GE(static_cast<uint64_t>(s), us0 / 1000000ULL);
  EXPECT_LE(static_cast<uint64_t>(s), us1 / 1000000ULL);
  // And int(d) is what callers who cast get; must not exceed Seconds+0.
  EXPECT_LE(static_cast<int>(d), s);
}
}  // namespace

TEST(RageTimerSinceStart, ThreeVariantsAgree) {
  ExpectVariantsAgree();
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  ExpectVariantsAgree();
}

TEST(RageTimerSinceStart, ThreeVariantsAgreeAcrossSecondBoundary) {
  const uint64_t start_s =
      RageTimer::GetTimeSinceStartMicroseconds() / 1000000ULL;
  // Poll until the whole-second counter rolls over, checking along the way.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (RageTimer::GetTimeSinceStartMicroseconds() / 1000000ULL == start_s &&
         std::chrono::steady_clock::now() < deadline) {
    ExpectVariantsAgree();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (int i = 0; i < 20; ++i) {
    ExpectVariantsAgree();
  }
}

TEST(RageTimerSinceStart, ThreeVariantsAgreeOnSyntheticCheckpoints) {
  for (const Checkpoint& c : kCheckpoints) {
    SCOPED_TRACE(c.name);
    const auto d =
        std::chrono::seconds(c.seconds) + std::chrono::microseconds(999999);
    EXPECT_EQ(ToSeconds(d), static_cast<int>(c.seconds));  // truncates
    EXPECT_EQ(ToMicros(d), c.seconds * 1000000ULL + 999999u);
    EXPECT_TRUE(
        WithinSyncBudget(static_cast<double>(ToMicros(d)) / 1e6, ToDouble(d)));
    EXPECT_EQ(static_cast<int>(ToDouble(d)), ToSeconds(d));
  }
}  // This is the end of tests for RageTimer's "time-since-start" functions.

// RageTimer calculations.
// RageTimers are internally just two uint64_t, one storing the seconds value
// and one storing the microseconds value. Operations on RageTimers return a
// float. A float representing a time in seconds can be retrieved from a
// RageTimer object. Common uses are measuring small intervals of time.

TEST(RageTimerCalc, ZeroAndTouch) {
  RageTimer t(0, 0);
  EXPECT_TRUE(t.IsZero());
  EXPECT_TRUE(RageZeroTimer.IsZero());

  t.Touch();
  EXPECT_FALSE(t.IsZero());
  EXPECT_LT(t.Ago(), 0.001f);

  t.SetZero();
  EXPECT_TRUE(t.IsZero());
  EXPECT_EQ(t.secs_, 0u);
  EXPECT_EQ(t.us_, 0u);

  EXPECT_FALSE(RageTimer(0, 1).IsZero());
  EXPECT_FALSE(RageTimer(1, 0).IsZero());
}

TEST(RageTimerCalc, DefaultConstructorTouches) {
  const RageTimer t;
  EXPECT_FALSE(t.IsZero());
  EXPECT_LT(t.us_, 1000000u);
}

TEST(RageTimerCalc, TouchAdvances) {
  RageTimer t;
  const RageTimer first = t;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  t.Touch();
  EXPECT_TRUE(first < t);
  EXPECT_GE(t - first, 0.019f);
  EXPECT_LT(t.us_, 1000000u);
}

TEST(RageTimerCalc, PlusMinusOperators) {
  const RageTimer base(100, 250000);

  RageTimer p = base + 5.25f;
  EXPECT_EQ(p.secs_, 105u);
  EXPECT_EQ(p.us_, 500000u);

  RageTimer m = base - 0.5f;
  EXPECT_EQ(m.secs_, 99u);
  EXPECT_EQ(m.us_, 750000u);

  RageTimer a = base;
  a += 1.5f;
  EXPECT_EQ(a.secs_, 101u);
  EXPECT_EQ(a.us_, 750000u);
  a -= 1.5f;
  EXPECT_EQ(a.secs_, base.secs_);
  EXPECT_EQ(a.us_, base.us_);

  // Zero is the identity.
  const RageTimer z = base + 0.0f;
  EXPECT_EQ(z.secs_, base.secs_);
  EXPECT_EQ(z.us_, base.us_);
}

TEST(RageTimerCalc, SumCarriesMicroseconds) {
  const RageTimer r = RageTimer(10, 750000) + 0.5f;
  EXPECT_EQ(r.secs_, 11u);
  EXPECT_EQ(r.us_, 250000u);

  const RageTimer r2 = RageTimer(10, 999999) + 1.0f;
  EXPECT_EQ(r2.secs_, 11u);
  EXPECT_EQ(r2.us_, 999999u);
}

TEST(RageTimerCalc, SumNegativeBorrowsSeconds) {
  // The comments in Sum: -1.25 -> secs=-2, us=.75
  const RageTimer r = RageTimer(5, 100000) + -0.25f;
  EXPECT_EQ(r.secs_, 4u);
  EXPECT_EQ(r.us_, 850000u);

  const RageTimer r2 = RageTimer(5, 500000) + -1.25f;
  EXPECT_EQ(r2.secs_, 4u);
  EXPECT_EQ(r2.us_, 250000u);

  const RageTimer r3 = RageTimer(5, 250000) + -1.25f;
  EXPECT_EQ(r3.secs_, 4u);
  EXPECT_EQ(r3.us_, 0u);
}

TEST(RageTimerCalc, DifferenceBasics) {
  EXPECT_FLOAT_EQ(RageTimer(10, 500000) - RageTimer(10, 500000), 0.0f);
  EXPECT_FLOAT_EQ(RageTimer(12, 500000) - RageTimer(10, 250000), 2.25f);
  // Microsecond borrow.
  EXPECT_FLOAT_EQ(RageTimer(12, 100000) - RageTimer(10, 600000), 1.5f);
  // Negative result (lhs earlier than rhs).
  EXPECT_FLOAT_EQ(RageTimer(10, 0) - RageTimer(12, 500000), -2.5f);
}

TEST(RageTimerCalc, LessThan) {
  EXPECT_TRUE(RageTimer(1, 5) < RageTimer(2, 0));
  EXPECT_TRUE(RageTimer(1, 5) < RageTimer(1, 6));
  EXPECT_FALSE(RageTimer(1, 6) < RageTimer(1, 5));
  EXPECT_FALSE(RageTimer(2, 0) < RageTimer(1, 999999));
  EXPECT_FALSE(RageTimer(3, 3) < RageTimer(3, 3));
}

TEST(RageTimerCalc, AgoMatchesKnownOffset) {
  RageTimer t;
  t -= 3.5f;
  EXPECT_TRUE(WithinSyncBudget(3.5, t.Ago()));

  RageTimer future;
  future += 2.0f;
  EXPECT_TRUE(WithinSyncBudget(-2.0, future.Ago()));
}

TEST(RageTimerCalc, HalfIsMidpointToNow) {
  RageTimer t;
  t -= 10.0f;
  const RageTimer h = t.Half();
  EXPECT_TRUE(WithinSyncBudget(5.0, h.Ago()));
  EXPECT_TRUE(WithinSyncBudget(5.0, h - t));
  EXPECT_TRUE(t < h);

  // Half of "now" is ~now.
  const RageTimer now;
  EXPECT_LT(std::fabs(now.Half().Ago()), 0.001f);
}

TEST(RageTimerCalc, GetDeltaTime) {
  RageTimer t;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const float dt = t.GetDeltaTime();
  EXPECT_GE(dt, 0.049f);
  EXPECT_LT(dt, 0.25f);  // scheduler slack only; not a clock-accuracy bound

  // GetDeltaTime re-touches, so an immediate second call is near zero.
  const float dt2 = t.GetDeltaTime();
  EXPECT_GE(dt2, 0.0f);
  EXPECT_LT(dt2, 0.01f);
  EXPECT_LT(t.Ago(), 0.01f);
}

TEST(RageTimerCalc, GetDeltaTimeIsAccumulative) {
  RageTimer t;
  float total = 0;
  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    total += t.GetDeltaTime();
  }
  EXPECT_GE(total, 0.049f);
  EXPECT_LT(total, 0.25f);
}

// Sum / Difference at large magnitudes (through the public operators)
// These are RageTimer's two private methods. Since RageTimers are stored as
// separated seconds and microseconds, these functions provide the needed
// functionalities to properaly add or subtract these values

TEST(RageTimerSumDifference, CheckpointsSmallDeltaIsPrecise) {
  // Timestamps at checkpoint magnitudes; the delta stays small so it is
  // representable in float. Seconds and microseconds are kept separate, so
  // the absolute magnitude must not cost any precision.
  for (const Checkpoint& c : kCheckpoints) {
    SCOPED_TRACE(c.name);
    const RageTimer base(c.seconds, 250000);

    const RageTimer later = base + 0.25f;
    EXPECT_EQ(later.secs_, c.seconds);
    EXPECT_EQ(later.us_, 500000u);
    EXPECT_TRUE(WithinSyncBudget(0.25, later - base));

    const RageTimer carry = base + 0.75f;
    EXPECT_EQ(carry.secs_, c.seconds + 1);
    EXPECT_EQ(carry.us_, 0u);

    const RageTimer earlier = base - 0.5f;
    EXPECT_EQ(earlier.secs_, c.seconds - 1);
    EXPECT_EQ(earlier.us_, 750000u);
    EXPECT_TRUE(WithinSyncBudget(-0.5, earlier - base));
    EXPECT_TRUE(WithinSyncBudget(0.0, base - base));
  }
}

TEST(RageTimerSumDifference, CheckpointsWholeSecondOffsets) {
  // Whole-second float values are exact up to 2^24, covering 256h (921,600).
  for (const Checkpoint& c : kCheckpoints) {
    SCOPED_TRACE(c.name);
    const RageTimer zero(1000, 0);
    const RageTimer r = zero + static_cast<float>(c.seconds);
    EXPECT_EQ(r.secs_, 1000 + c.seconds);
    EXPECT_EQ(r.us_, 0u);
    EXPECT_TRUE(WithinSyncBudget(static_cast<double>(c.seconds), r - zero));
    EXPECT_TRUE(WithinSyncBudget(-static_cast<double>(c.seconds), zero - r));

    const RageTimer back = r - static_cast<float>(c.seconds);
    EXPECT_EQ(back.secs_, zero.secs_);
    EXPECT_EQ(back.us_, zero.us_);
  }
}

TEST(RageTimerSumDifference, RoundTripAtCheckpoints) {
  for (const Checkpoint& c : kCheckpoints) {
    SCOPED_TRACE(c.name);
    RageTimer t(c.seconds, 123000);
    for (int i = 0; i < 1000; ++i) {
      t += 0.5f;
    }
    for (int i = 0; i < 1000; ++i) {
      t -= 0.5f;
    }
    EXPECT_EQ(t.secs_, c.seconds);
    EXPECT_EQ(t.us_, 123000u);
  }
}

TEST(RageTimerSumDifference, SmallFramePeriodsDoNotDriftOverSixHours) {
  // Simulate a 6h run at ~16.667ms frames: accumulate the timestamp with
  // operator+= and compare with the exact value. Each step is quantised to
  // whole microseconds, so the error budget is bounded by 1us/step; this
  // catches truncation bugs that would show up as a *sudden or steady* sync
  // drift (budget: 5ms noticeable limit over 6h, 1ms flag).
  const float step = 0.015625f;  // 1/64 s, exact in float and in microseconds
  const int steps = 64 * 3600;   // 1 hour of frames (kept fast), x6 below
  RageTimer t(0, 0);
  for (int hour = 1; hour <= 6; ++hour) {
    for (int i = 0; i < steps; ++i) {
      t += step;
    }
    const double expected = hour * 3600.0;
    EXPECT_TRUE(WithinSyncBudget(expected, t - RageTimer(0, 0)))
        << "after " << hour << "h";
  }
}

TEST(RageTimerSumDifference, FloatReturnResolutionAtLongDurations) {
  // Documenting a hard limit rather than a bug: operator- returns float, so
  // a *difference* of many hours cannot hold sub-ms precision (float has 24
  // bits; at 256h the spacing is ~62ms). Timestamps stay precise (see the
  // tests above); only long *durations* returned as float lose resolution.
  const RageTimer a(256 * kHour + 0, 1);
  const RageTimer b(0, 0);
  const float d = a - b;
  const double exact = 256.0 * 3600.0 + 1e-6;
  const double dev = std::fabs(static_cast<double>(d) - exact);
  EXPECT_LE(dev, 0.0625);  // float spacing at ~9.2e5 is 0.0625
}

}  // namespace
