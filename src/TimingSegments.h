#ifndef TIMING_SEGMENTS_H
#define TIMING_SEGMENTS_H

#include <cmath>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "NoteTypes.h"  // Converting rows to beats and vice~versa.

enum TimingSegmentType {
  SEGMENT_BPM,
  SEGMENT_STOP,
  SEGMENT_DELAY,
  SEGMENT_TIME_SIG,
  SEGMENT_WARP,
  SEGMENT_LABEL,
  SEGMENT_TICKCOUNT,
  SEGMENT_COMBO,
  SEGMENT_SPEED,
  SEGMENT_SCROLL,
  SEGMENT_FAKE,
  NUM_TimingSegmentType,
  TimingSegmentType_Invalid,
};

// XXX: dumb names
enum SegmentEffectType {
  SegmentEffectType_Row,         // takes effect on a single row
  SegmentEffectType_Range,       // takes effect for a definite amount of rows
  SegmentEffectType_Indefinite,  // takes effect until the next segment of its
                                 // type
  NUM_SegmentEffectType,
  SegmentEffectType_Invalid,
};

#define FOREACH_TimingSegmentType(tst) FOREACH_ENUM(TimingSegmentType, tst)

const std::string& TimingSegmentTypeToString(TimingSegmentType tst);

const int ROW_INVALID = -1;

/**
 * @brief The base timing segment for make glorious benefit wolfman
 * XXX: this should be an abstract class.
 */
struct TimingSegment {
  virtual TimingSegmentType GetType() const {
    return TimingSegmentType_Invalid;
  }
  virtual SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Invalid;
  }
  virtual TimingSegment* Copy() const = 0;

  virtual bool IsNotable() const = 0;

  // don't allow base TimingSegments to be instantiated directly
  TimingSegment(int iRow = ROW_INVALID) : start_row_(iRow) {}

  TimingSegment(const TimingSegment& other) : start_row_(other.GetRow()) {}

  // for our purposes, two floats within this level of error are equal
  static constexpr double kEpsilon = 1e-6;

  // A helper for testing equality of two floats within a level of error
  // epsilon.
  bool AreEqual(float f1, float f2) const {
    return std::abs(f1 - f2) < kEpsilon;
  }

  virtual ~TimingSegment() {}

  /**
   * @brief Scales itself.
   * @param start Starting row
   * @param length Length in rows
   * @param newLength The new length in rows
   */
  virtual void Scale(int start, int length, int newLength);

  int GetRow() const { return start_row_; }
  void SetRow(int iRow) { start_row_ = iRow; }

  float GetBeat() const { return NoteRowToBeat(start_row_); }

  virtual std::string ToString(int /* dec */) const {
    return std::to_string(GetBeat());
  }

  virtual std::vector<float> GetValues() const { return std::vector<float>(0); }

  bool operator<(const TimingSegment& other) const {
    return GetRow() < other.GetRow();
  }

  // overloads should not call this base version; derived classes
  // should only compare contents, and this compares position.
  virtual bool operator==(const TimingSegment& other) const {
    return GetRow() == other.GetRow();
  }

 private:
  /** @brief The row in which this segment activates. */
  int start_row_;
};

/**
 * @brief Identifies when a whole region of arrows is to be ignored.
 *
 * FakeSegments are similar to the Fake Tap Notes in that the contents
 * inside are neither for nor against the player. They can be useful for
 * mission modes, in conjunction with WarpSegments, or perhaps other
 * uses not thought up at the time of this comment. Unlike the Warp
 * Segments, these are not magically jumped over: instead, these are
 * drawn normally.
 *
 * These were inspired by the Pump It Up series. */
struct FakeSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_FAKE; }
  SegmentEffectType GetEffectType() const { return SegmentEffectType_Range; }

  TimingSegment* Copy() const { return new FakeSegment(*this); }

  bool IsNotable() const { return length_rows_ > 0; }

  FakeSegment() : TimingSegment(), length_rows_(-1) {}

  FakeSegment(int iStartRow, float fBeats)
      : TimingSegment(iStartRow), length_rows_(BeatToNoteRow(fBeats)) {}

  FakeSegment(const FakeSegment& other)
      : TimingSegment(other.GetRow()), length_rows_(other.GetLengthRows()) {}

  int GetLengthRows() const { return length_rows_; }
  float GetLengthBeats() const { return NoteRowToBeat(length_rows_); }
  float GetLength() const { return GetLengthBeats(); }  // compatibility

  void SetLength(float fBeats) { length_rows_ = BeatToNoteRow(fBeats); }

  void Scale(int start, int length, int newLength);

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetLength());
  }

  bool operator==(const FakeSegment& other) const {
    if (length_rows_ != other.length_rows_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const FakeSegment&>(other));
  }

 private:
  /** @brief The number of rows the FakeSegment is alive for. */
  int length_rows_;
};

/**
 * @brief Identifies when a song needs to warp to a new beat.
 *
 * A warp segment is used to replicate the effects of Negative BPMs without
 * abusing negative BPMs. Negative BPMs should be converted to warp segments.
 * WarpAt=WarpToRelative is the format, where both are in beats.
 * (Technically they're both rows though.) */
struct WarpSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_WARP; }
  SegmentEffectType GetEffectType() const { return SegmentEffectType_Range; }
  TimingSegment* Copy() const { return new WarpSegment(*this); }

  bool IsNotable() const { return length_rows_ > 0; }

  WarpSegment() : TimingSegment(), length_rows_(0) {}

  WarpSegment(const WarpSegment& other)
      : TimingSegment(other.GetRow()), length_rows_(other.GetLengthRows()) {}

  WarpSegment(int iStartRow, float fBeats)
      : TimingSegment(iStartRow), length_rows_(BeatToNoteRow(fBeats)) {}

  int GetLengthRows() const { return length_rows_; }
  float GetLengthBeats() const { return NoteRowToBeat(length_rows_); }
  float GetLength() const { return GetLengthBeats(); }  // compatibility

  void SetLength(float fBeats) { length_rows_ = BeatToNoteRow(fBeats); }

  void Scale(int start, int length, int newLength);
  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetLength());
  }

  bool operator==(const WarpSegment& other) const {
    if (length_rows_ != other.length_rows_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const WarpSegment&>(other));
  }

 private:
  /** @brief The number of rows the WarpSegment will warp past. */
  int length_rows_;
};

/**
 * @brief Identifies when a chart is to have a different tickcount value
 * for hold notes.
 *
 * A tickcount segment is used to better replicate the checkpoint hold
 * system used by various based video games. The number is used to
 * represent how many ticks can be counted in one beat.
 */

struct TickcountSegment : public TimingSegment {
  /** @brief The default amount of ticks per beat. */
  static const unsigned kDefaultTickCount = 4;

  TimingSegmentType GetType() const { return SEGMENT_TICKCOUNT; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new TickcountSegment(*this); }

  TickcountSegment(int iStartRow = ROW_INVALID, int iTicks = kDefaultTickCount)
      : TimingSegment(iStartRow), ticks_per_beat_(iTicks) {}

  TickcountSegment(const TickcountSegment& other)
      : TimingSegment(other.GetRow()), ticks_per_beat_(other.GetTicks()) {}

  int GetTicks() const { return ticks_per_beat_; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetTicks() * 1.f);
  }

  bool operator==(const TickcountSegment& other) const {
    if (ticks_per_beat_ != other.ticks_per_beat_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const TickcountSegment&>(other));
  }

 private:
  /** @brief The amount of hold checkpoints counted per beat */
  int ticks_per_beat_;
};

/**
 * @brief Identifies when a chart is to have a different combo multiplier value.
 *
 * Admitedly, this would primarily be used for mission mode style charts.
 * However, it can have its place during normal gameplay.
 */
struct ComboSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_COMBO; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new ComboSegment(*this); }

  ComboSegment(int iStartRow = ROW_INVALID, int iCombo = 1, int iMissCombo = 1)
      : TimingSegment(iStartRow), combo_(iCombo), miss_combo_(iMissCombo) {}

  ComboSegment(const ComboSegment& other)
      : TimingSegment(other.GetRow()),
        combo_(other.GetCombo()),
        miss_combo_(other.GetMissCombo()) {}

  int GetCombo() const { return combo_; }
  int GetMissCombo() const { return miss_combo_; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const;

  bool operator==(const ComboSegment& other) const {
    if (combo_ != other.combo_) {
      return false;
    }
    if (miss_combo_ != other.miss_combo_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const ComboSegment&>(other));
  }

 private:
  /** @brief The amount the combo increases at this point. */
  int combo_;

  /** @brief The amount of miss combos given at this point. */
  int miss_combo_;
};

/**
 * @brief Identifies when a chart is entering a different section.
 *
 * This is meant for helping to identify different sections of a chart
 * versus relying on measures and beats alone.
 */
struct LabelSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_LABEL; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new LabelSegment(*this); }

  LabelSegment(
      int iStartRow = ROW_INVALID, const std::string& sLabel = std::string())
      : TimingSegment(iStartRow), label_(sLabel) {}

  LabelSegment(const LabelSegment& other)
      : TimingSegment(other.GetRow()), label_(other.GetLabel()) {}

  const std::string& GetLabel() const { return label_; }

  std::string ToString(int dec) const;
  // Use the default definition for GetValues because the value for a
  // LabelSegment is not a float or set of floats. TimingSegmentSetToLuaTable in
  // TimingData.cpp has a special case for labels to handle this.

  bool operator==(const LabelSegment& other) const {
    if (label_ != other.label_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const LabelSegment&>(other));
  }

 private:
  /** @brief The label/section name for this point. */
  std::string label_;
};

/**
 * @brief Identifies when a song changes its BPM.
 */
struct BPMSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_BPM; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new BPMSegment(*this); }

  // note that this takes a BPM, not a BPS (compatibility)
  BPMSegment(int iStartRow = ROW_INVALID, float fBPM = 0.0f)
      : TimingSegment(iStartRow) {
    SetBPM(fBPM);
  }

  BPMSegment(const BPMSegment& other)
      : TimingSegment(other.GetRow()), bps_(other.GetBPS()) {}

  float GetBPS() const { return bps_; }
  float GetBPM() const { return bps_ * 60.0f; }

  void SetBPS(float fBPS) { bps_ = fBPS; }
  void SetBPM(float fBPM) { bps_ = fBPM / 60.0f; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetBPM());
  }

  bool operator==(const BPMSegment& other) const {
    return AreEqual(bps_, other.bps_);
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const BPMSegment&>(other));
  }

 private:
  /** @brief The number of beats per second within this BPMSegment. */
  float bps_;
};

/**
 * @brief Identifies when a song changes its time signature.
 *
 * This only supports simple time signatures. The upper number
 * (called the numerator here, though this isn't properly a
 * fraction) is the number of beats per measure. The lower number
 * (denominator here) is the note value representing one beat. */
struct TimeSignatureSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_TIME_SIG; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new TimeSignatureSegment(*this); }

  TimeSignatureSegment(
      int iStartRow = ROW_INVALID, int iNum = 4, int iDenom = 4)
      : TimingSegment(iStartRow), numerator_(iNum), denominator_(iDenom) {}

  TimeSignatureSegment(const TimeSignatureSegment& other)
      : TimingSegment(other.GetRow()),
        numerator_(other.GetNum()),
        denominator_(other.GetDen()) {}

  int GetNum() const { return numerator_; }

  int GetDen() const { return denominator_; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const;

  /**
   * @brief Retrieve the number of note rows per measure within the
   * TimeSignatureSegment.
   *
   * With BeatToNoteRow(1) rows per beat, then we should have
   * BeatToNoteRow(1)*m_iNumerator beats per measure. But if we assume that
   * every BeatToNoteRow(1) rows is a quarter note, and we want the beats to be
   * 1/m_iDenominator notes, then we should have BeatToNoteRow(1)*4 is rows per
   * whole note and thus BeatToNoteRow(1)*4/m_iDenominator is rows per beat.
   * Multiplying by m_iNumerator gives rows per measure.
   * @returns the number of note rows per measure.
   */
  int GetNoteRowsPerMeasure() const {
    return BeatToNoteRow(1) * 4 * numerator_ / denominator_;
  }

  bool operator==(const TimeSignatureSegment& other) const {
    if (numerator_ != other.numerator_) {
      return false;
    }
    if (denominator_ != other.denominator_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const TimeSignatureSegment&>(other));
  }

 private:
  int numerator_, denominator_;
};

/**
 * @brief Identifies when the arrow scroll changes.
 *
 * SpeedSegments take a Player's scrolling BPM (Step's BPM * speed mod),
 * and then multiplies it with the percentage value. No matter the player's
 * speed mod, the ratio will be the same. Unlike forced attacks, these
 * cannot be turned off at a set time: reset it by setting the percentage
 * back to 1.
 *
 * These were inspired by the Pump It Up series. */
struct SpeedSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_SPEED; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new SpeedSegment(*this); }

  /** @brief The type of unit used for segment scaling. */
  enum BaseUnit { UNIT_BEATS, UNIT_SECONDS };

  SpeedSegment(
      int iStartRow = ROW_INVALID, float fRatio = 1.0f, float fDelay = 0.0f,
      BaseUnit unit = UNIT_BEATS)
      : TimingSegment(iStartRow), ratio_(fRatio), delay_(fDelay), unit_(unit) {}

  SpeedSegment(const SpeedSegment& other)
      : TimingSegment(other.GetRow()),
        ratio_(other.GetRatio()),
        delay_(other.GetDelay()),
        unit_(other.GetUnit()) {}

  float GetRatio() const { return ratio_; }

  float GetDelay() const { return delay_; }
  void SetDelay(float fDelay) { delay_ = fDelay; }

  BaseUnit GetUnit() const { return unit_; }

  void Scale(int start, int length, int newLength);

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const;

  bool operator==(const SpeedSegment& other) const {
    if (!AreEqual(ratio_, other.ratio_)) {
      return false;
    }
    if (!AreEqual(delay_, other.delay_)) {
      return false;
    }
    if (unit_ != other.unit_) {
      return false;
    }
    return true;
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const SpeedSegment&>(other));
  }

 private:
  /** @brief The percentage by which the Player's BPM is multiplied. */
  float ratio_;

  /**
   * @brief The number of beats or seconds to wait before applying.
   * A value of 0 means this is immediate. */
  float delay_;

  /** @brief The mode that this segment uses for the math.  */
  BaseUnit unit_;
};

/**
 * @brief Identifies when the chart scroll changes.
 *
 * ScrollSegments adjusts the scrolling speed of the note field.
 * Unlike forced attacks, these cannot be turned off at a set time:
 * reset it by setting the precentage back to 1.
 *
 * These were inspired by the Pump It Up series. */
struct ScrollSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_SCROLL; }
  SegmentEffectType GetEffectType() const {
    return SegmentEffectType_Indefinite;
  }

  bool IsNotable() const {
    return true;
  }  // indefinite segments are always true

  TimingSegment* Copy() const { return new ScrollSegment(*this); }

  ScrollSegment(int iStartRow = ROW_INVALID, float fRatio = 1.0f)
      : TimingSegment(iStartRow), ratio_(fRatio) {}

  ScrollSegment(const ScrollSegment& other)
      : TimingSegment(other.GetRow()), ratio_(other.GetRatio()) {}

  float GetRatio() const { return ratio_; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetRatio());
  }

  bool operator==(const ScrollSegment& other) const {
    return AreEqual(ratio_, other.ratio_);
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const ScrollSegment&>(other));
  }

 private:
  /** @brief The percentage by which the chart's scroll rate is multiplied. */
  float ratio_;
};

/**
 * @brief Identifies when a song has a stop, DDR/ITG style.
 */
struct StopSegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_STOP; }
  SegmentEffectType GetEffectType() const { return SegmentEffectType_Row; }

  bool IsNotable() const { return seconds_ > 0; }

  TimingSegment* Copy() const { return new StopSegment(*this); }

  StopSegment(int iStartRow = ROW_INVALID, float fSeconds = 0.0f)
      : TimingSegment(iStartRow), seconds_(fSeconds) {}

  StopSegment(const StopSegment& other)
      : TimingSegment(other.GetRow()), seconds_(other.GetPause()) {}

  float GetPause() const { return seconds_; }
  void SetPause(float fSeconds) { seconds_ = fSeconds; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetPause());
  }

  bool operator==(const StopSegment& other) const {
    return AreEqual(seconds_, other.seconds_);
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const StopSegment&>(other));
  }

 private:
  /** @brief The number of seconds to pause at the segment's row. */
  float seconds_;
};

/**
 * @brief Identifies when a song has a delay, or pump style stop.
 */
struct DelaySegment : public TimingSegment {
  TimingSegmentType GetType() const { return SEGMENT_DELAY; }
  SegmentEffectType GetEffectType() const { return SegmentEffectType_Row; }

  bool IsNotable() const { return seconds_ > 0; }

  TimingSegment* Copy() const { return new DelaySegment(*this); }

  DelaySegment(int iStartRow = ROW_INVALID, float fSeconds = 0)
      : TimingSegment(iStartRow), seconds_(fSeconds) {}

  DelaySegment(const DelaySegment& other)
      : TimingSegment(other.GetRow()), seconds_(other.GetPause()) {}

  float GetPause() const { return seconds_; }
  void SetPause(float fSeconds) { seconds_ = fSeconds; }

  std::string ToString(int dec) const;
  std::vector<float> GetValues() const {
    return std::vector<float>(1, GetPause());
  }

  bool operator==(const DelaySegment& other) const {
    return AreEqual(seconds_, other.seconds_);
  }

  bool operator==(const TimingSegment& other) const {
    if (GetType() != other.GetType()) {
      return false;
    }

    return operator==(static_cast<const DelaySegment&>(other));
  }

 private:
  /** @brief The number of seconds to pause at the segment's row. */
  float seconds_;
};

#endif

/**
 * @file
 * @author Jason Felds (c) 2011
 * @section LICENSE
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
