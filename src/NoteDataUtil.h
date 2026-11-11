#ifndef NOTEDATAUTIL_H
#define NOTEDATAUTIL_H

#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"

class PlayerOptions;
struct RadarValues;
class NoteData;
class Song;
struct AttackArray;
class TimingData;

void PlaceAutoKeysound(NoteData& out, int row, TapNote tap);
int FindLongestOverlappingHoldNoteForAnyTrack(const NoteData& in, int row);
void LightTransformHelper(
    const NoteData& in, NoteData& out, const std::vector<int>& tracks);

/**
 * @brief Utility functions that deal with NoteData.
 *
 * Things should go in here if they can be (cleanly and efficiently)
 * implemented using only NoteData's primitives; this improves abstraction
 * and makes it much easier to change NoteData internally in the future. */
namespace NoteDataUtil {
NoteType GetSmallestNoteTypeForMeasure(const NoteData& nd, int measure_index);
NoteType GetSmallestNoteTypeInRange(
    const NoteData& nd, int start_index, int end_index);
void LoadFromSMNoteDataString(
    NoteData& out, const std::string& sm_note_data, bool composite);
void GetSMNoteDataString(
    const NoteData& in, std::string& notes_out, bool include_measure_comments);
void SplitCompositeNoteData(const NoteData& in, std::vector<NoteData>& out);
/**
 * @brief Similar to SplitCompositeNoteData, but will also separate couple note
 * data.
 *
 * @return true if the NoteData was split, otherwise false.
 */
bool SplitCompositeOrStackedNoteData(
    const NoteData& in, std::vector<NoteData>& out, StepsType steps_type);
void CombineCompositeNoteData(NoteData& out, const std::vector<NoteData>& in);
/**
 * @brief Autogenerate notes from one type to another.
 *
 * TODO: Look into a more intelligent way of doing so.
 * @param in The original NoteData.
 * @param out the new NoteData.
 * @param iNewNumTracks the number of tracks/columns of the new NoteData. */
void LoadTransformedSlidingWindow(
    const NoteData& in, NoteData& out, int new_num_tracks);
/**
 * @brief Autogenerate notes from one type to another.
 *
 * NOTE: This code assumes that there are more columns in the original type.
 * @param in The original NoteData.
 * @param out the new NoteData.
 * @param iNewNumTracks the number of tracks/columns of the new NoteData. */
void LoadOverlapped(const NoteData& in, NoteData& out, int new_num_tracks);
void LoadTransformedLights(
    const NoteData& in, NoteData& out, int new_num_tracks);
void LoadTransformedLightsFromTwo(
    const NoteData& marquee, const NoteData& bass, NoteData& out);
void InsertHoldTails(NoteData& inout);

void CalculateRadarValues(
    const NoteData& in, float song_seconds, RadarValues& out);
void CalculateRadarValues(
    const NoteData& in, float song_seconds, const TimingData* timing,
    RadarValues& out);

/**
 * @brief Remove all of the Hold notes.
 * @param inout the Notedata to be transformed.
 * @param iStartIndex the starting point for transforming.
 * @param iEndIndex the ending point for transforming. */
void RemoveHoldNotes(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void ChangeRollsToHolds(NoteData& in, int start_index, int end_index);
void ChangeHoldsToRolls(NoteData& in, int start_index, int end_index);
void RemoveSimultaneousNotes(
    NoteData& inout, int max_simultaneous, int start_index = 0,
    int end_index = kMaxNoteRow);
void RemoveJumps(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void RemoveHands(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void RemoveQuads(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
/**
 * @brief Remove all of a specific TapNote Type.
 * @param inout the Notedata to be transformed.
 * @param tn the TapNote Type to remove.
 * @param iStartIndex the starting point for transforming.
 * @param iEndIndex the ending point for transforming. */
void RemoveSpecificTapNotes(
    NoteData& inout, TapNoteType tn, int start_index = 0,
    int end_index = kMaxNoteRow);
/**
 * @brief Remove all of the mines from the chart.
 * @param inout the Notedata to be transformed.
 * @param iStartIndex the starting point for transforming.
 * @param iEndIndex the ending point for transforming. */
void RemoveMines(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
/**
 * @brief Remove all of the lifts from the chart.
 * @param inout the Notedata to be transformed.
 * @param iStartIndex the starting point for transforming.
 * @param iEndIndex the ending point for transforming. */
void RemoveLifts(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
/**
 * @brief Remove all of the fakes from the chart.
 * @param inout the Notedata to be transformed.
 * @param iStartIndex the starting point for transforming.
 * @param iEndIndex the ending point for transforming. */
void RemoveFakes(
    NoteData& inout, const TimingData& timing_data, int start_index = 0,
    int end_index = kMaxNoteRow);
void RemoveStretch(
    NoteData& inout, StepsType st, int start_index = 0,
    int end_index = kMaxNoteRow);
void RemoveAllButOneTap(NoteData& inout, int row);
/** @brief The types of transformations available for the NoteData. */
enum TrackMapping {
  left,  /**< The NoteData is arranged as if the player was facing to the left.
          */
  right, /**< The NoteData is arranged as if the player was facing to the right.
          */
  mirror,   /**< The NoteData is arranged as if facing a straight mirror. */
  lrmirror, /**< The NoteData is arranged so that the left and right columns are
               flipped. */
  udmirror, /**< The NoteData is arranged so that the up and down columns are
               flipped. */
  backwards, /**< The NoteData is arranged as if the player was facing
                backwards. This is NOT always the same as mirror. */
  shuffle,
  soft_shuffle,
  super_shuffle,
  hyper_shuffle,
  stomp,
  swap_up_down,
  NUM_TRACK_MAPPINGS
};
void Turn(
    NoteData& inout, StepsType st, TrackMapping tt, int start_index = 0,
    int end_index = kMaxNoteRow);
void Little(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Wide(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Big(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Quick(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void BMRize(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Skippy(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void InsertIntelligentTaps(
    NoteData& in, int window_size_rows, int insert_offset_rows,
    int window_stride_rows, bool skippy, int start_index = 0,
    int end_index = kMaxNoteRow);
void AddMines(
    NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Echo(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Stomp(
    NoteData& inout, StepsType st, int start_index = 0,
    int end_index = kMaxNoteRow);
void Planted(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Floored(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void Twister(NoteData& inout, int start_index = 0, int end_index = kMaxNoteRow);
void ConvertTapsToHolds(
    NoteData& inout, int simultaneous_holds, int start_index = 0,
    int end_index = kMaxNoteRow);

/**
 * @brief Convert all taps added via transforms into original style tap notes.
 * @param inout the NoteData to convert. */
void ConvertAdditionsToRegular(NoteData& inout);

void Backwards(NoteData& inout);
void SwapSides(NoteData& inout);
void CopyLeftToRight(NoteData& inout);
void CopyRightToLeft(NoteData& inout);
void ClearLeft(NoteData& inout);
void ClearRight(NoteData& inout);
void CollapseToOne(NoteData& inout);
void CollapseLeft(NoteData& inout);
void ShiftTracks(NoteData& inout, int shift_by);
void ShiftLeft(NoteData& inout);
void ShiftRight(NoteData& inout);
void SwapUpDown(NoteData& inout, StepsType st);
void ArbitraryRemap(NoteData& inout, int* mapping);

void SnapToNearestNoteType(
    NoteData& inout, NoteType nt1, NoteType nt2, int start_index,
    int end_index);

inline void SnapToNearestNoteType(
    NoteData& inout, NoteType nt, int start_index, int end_index) {
  SnapToNearestNoteType(inout, nt, NoteType_Invalid, start_index, end_index);
}

// True if no notes in row that aren't true in the mask
bool RowPassesValidMask(NoteData& inout, int row, const bool valid_mask[]);

void TransformNoteData(
    NoteData& nd, const TimingData& timing_data, const AttackArray& aa,
    StepsType st, Song* song);
void TransformNoteData(
    NoteData& nd, const TimingData& timing_data, const PlayerOptions& po,
    StepsType st, int start_index = 0, int end_index = kMaxNoteRow);
void ScaleRegion(
    NoteData& nd, float scale, int start_index = 0,
    int end_index = kMaxNoteRow);

void InsertRows(NoteData& nd, int start_index, int rows_to_shift);
void DeleteRows(NoteData& nd, int start_index, int rows_to_shift);

void RemoveAllTapsOfType(NoteData& in_out, TapNoteType type_to_remove);
void RemoveAllTapsExceptForType(NoteData& in_out, TapNoteType type_to_keep);

int GetMaxNonEmptyTrack(const NoteData& in);
bool GetNextEditorPosition(const NoteData& in, int& row_in_out);
bool GetPrevEditorPosition(const NoteData& in, int& row_in_out);

/** @brief Count the number of hold ticks that will fire, assuming that
 * tickholds are on.
 * @param td The TimingData from the relevant Steps. */
unsigned int GetTotalHoldTicks(NoteData* nd, const TimingData* td);
};  // namespace NoteDataUtil

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
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
