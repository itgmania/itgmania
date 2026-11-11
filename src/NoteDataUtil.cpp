#include "NoteDataUtil.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Attack.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "Style.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

// TODO: Remove these constants that aren't time signature-aware
static const int kBeatsPerMeasure = 4;
static const int kRowsPerMeasure = kRowsPerBeat * kBeatsPerMeasure;

NoteType NoteDataUtil::GetSmallestNoteTypeForMeasure(
    const NoteData& nd, int measure_index) {
  const int measure_start_index = measure_index * kRowsPerMeasure;
  const int measure_end_index = (measure_index + 1) * kRowsPerMeasure;

  return NoteDataUtil::GetSmallestNoteTypeInRange(
      nd, measure_start_index, measure_end_index);
}

NoteType NoteDataUtil::GetSmallestNoteTypeInRange(
    const NoteData& n, int start_index, int end_index) {
  std::vector<int> rows;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(n, i, start_index, end_index) {
    rows.push_back(i);
  }

  // probe to find the smallest note type
  FOREACH_ENUM(NoteType, nt) {
    float beat_spacing = NoteTypeToBeat(nt);
    int row_spacing = std::lrint(beat_spacing * kRowsPerBeat);

    bool found_smaller_note = false;
    // for each index in this measure
    for (int i : rows) {
      if (i % row_spacing == 0) {
        continue;  // skip
      }

      if (!n.IsRowEmpty(i)) {
        found_smaller_note = true;
        break;
      }
    }

    if (found_smaller_note) {
      continue;  // searching the next NoteType
    } else {
      return nt;  // stop searching. We found the smallest NoteType
    }
  }
  return NoteType_Invalid;  // well-formed notes created in the editor should
                            // never get here
}

static void LoadFromSMNoteDataStringWithPlayer(
    NoteData& out, const std::string& sm_note_data, int start, int len,
    PlayerNumber pn, int num_tracks) {
  /* Don't allocate memory for the entire string, nor per measure. Instead, use
   * the in-place partial string split twice. By maintaining begin and end
   * pointers to each measure line we can perform this without copying the
   * string at all. */
  int size = -1;
  const int end = start + len;
  std::vector<std::pair<const char*, const char*>> measure_lines;
  for (unsigned m = 0; true; ++m) {
    /* XXX Ignoring empty seems wrong for measures. It means that ",,," is
     * treated as
     * "," where I would expect most people would want 2 empty measures.
     * ",\n,\n," would do as I would expect. */
    split(
        sm_note_data, ",", start, size, end,
        true);  // Ignore empty is important.
    if (start == end) {
      break;
    }
    // Partial string split.
    int measure_line_start = start, measure_line_size = -1;
    const int measure_end = start + size;

    measure_lines.clear();
    for (;;) {
      // Ignore empty is clearly important here.
      split(
          sm_note_data, "\n", measure_line_start, measure_line_size,
          measure_end, true);
      if (measure_line_start == measure_end) {
        break;
      }
      // std::string &line = sSMNoteData.substr( measureLineStart,
      // measureLineSize );
      const char* begin_line = sm_note_data.data() + measure_line_start;
      const char* end_line = begin_line + measure_line_size;

      while (begin_line < end_line && strchr("\r\n\t ", *begin_line)) {
        ++begin_line;
      }
      while (end_line > begin_line && strchr("\r\n\t ", *(end_line - 1))) {
        --end_line;
      }
      if (begin_line < end_line) {  // nonempty
        measure_lines.push_back(
            std::pair<const char*, const char*>(begin_line, end_line));
      }
    }

    for (unsigned l = 0; l < measure_lines.size(); l++) {
      const char* p = measure_lines[l].first;
      const char* const begin_line = p;
      const char* const end_line = measure_lines[l].second;

      const float percent_into_measure = l / (float)measure_lines.size();
      const float beat = (m + percent_into_measure) * kBeatsPerMeasure;
      const int index = BeatToNoteRow(beat);

      int track = 0;
      while (track < num_tracks && p < end_line) {
        TapNote tn;
        char ch = *p;

        switch (ch) {
          case '0':
            tn = tap_empty;
            break;
          case '1':
            tn = tap_original_tap;
            break;
          case '2':
          case '4':
            // case 'N': // minefield
            tn = ch == '2' ? tap_original_hold_head : tap_original_roll_head;
            /*
            // upcoming code for minefields -aj
            switch(ch)
            {
            case '2': tn = TAP_ORIGINAL_HOLD_HEAD; break;
            case '4': tn = TAP_ORIGINAL_ROLL_HEAD; break;
            case 'N': tn = TAP_ORIGINAL_MINE_HEAD; break;
            }
            */

            /* Set the hold note to have infinite length. We'll clamp
             * it when we hit the tail. */
            tn.duration_ = kMaxNoteRow;
            break;
          case '3': {
            // This is the end of a hold. Search for the beginning.
            int head_row;
            if (!out.IsHoldNoteAtRow(track, index, &head_row)) {
              int n = intptr_t(end_line) - intptr_t(begin_line);
              LOG->Warn("Unmatched 3 in \"%.*s\"", n, begin_line);
            } else {
              out.FindTapNote(track, head_row)->second.duration_ =
                  index - head_row;
            }

            // This won't write tn, but keep parsing normally anyway.
            break;
          }
          //				case 'm':
          // Don't be loose with the definition.  Use only 'M' since
          // that's what we've been writing to disk.  -Chris
          case 'M':
            tn = tap_original_mine;
            break;
          // case 'A': tn = TAP_ORIGINAL_ATTACK;			break;
          case 'K':
            tn = tap_original_auto_keysound;
            break;
          case 'L':
            tn = tap_original_lift;
            break;
          case 'F':
            tn = tap_original_fake;
            break;
          // case 'I': tn = TAP_ORIGINAL_ITEM;			break;
          default:
            /* Invalid data. We don't want to assert, since there might
             * simply be invalid data in an .SM, and we don't want to die
             * due to invalid data. We should probably check for this when
             * we load SM data for the first time ... */
            // FAIL_M("Invalid data in SM");
            tn = tap_empty;
            break;
        }

        p++;
        // We won't scan past the end of the line so these are safe to do.
#if 0
				// look for optional attack info (e.g. "{tipsy,50% drunk:15.2}")
				if( *p == '{' )
				{
					p++;

					char szModifiers[256] = "";
					float fDurationSeconds = 0;
					if( sscanf( p, "%255[^:]:%f}", szModifiers, &fDurationSeconds ) == 2 )	// not fatal if this fails due to malformed data
					{
						tn.type = TapNoteType_Attack;
						tn.attack_modifiers_ = szModifiers;
		 				tn.attack_duration_seconds_ = fDurationSeconds;
					}

					// skip past the '}'
					while( p < endLine )
					{
						if( *(p++) == '}' )
							break;
					}
				}
#endif

        // look for optional keysound index (e.g. "[123]")
        if (*p == '[') {
          p++;
          int keysound_index = 0;
          if (1 ==
              sscanf(p, "%d]", &keysound_index)) {  // not fatal if this fails
                                                    // due to malformed data
            tn.keysound_index_ = keysound_index;
          }

          // skip past the ']'
          while (p < end_line) {
            if (*(p++) == ']') {
              break;
            }
          }
        }

#if 0
				// look for optional item name (e.g. "<potion>"),
				// where the name in the <> is a Lua function defined elsewhere
				// (Data/ItemTypes.lua, perhaps?) -aj
				if( *p == '<' )
				{
					p++;

					// skip past the '>'
					while( p < endLine )
					{
						if( *(p++) == '>' )
							break;
					}
				}
#endif

        /* Optimization: if we pass TAP_EMPTY, NoteData will do a search
         * to remove anything in this position.  We know that there's nothing
         * there, so avoid the search. */
        if (tn.type_ != TapNoteType_Empty && ch != '3') {
          tn.pn_ = pn;
          out.SetTapNote(track, index, tn);
        }

        track++;
      }
    }
  }

  // Make sure we don't have any hold notes that didn't find a tail.
  for (int t = 0; t < out.GetNumTracks(); t++) {
    NoteData::iterator begin = out.begin(t);
    while (begin != out.end(t)) {
      const TapNote& tn = begin->second;
      if (tn.type_ == TapNoteType_HoldHead && tn.duration_ == kMaxNoteRow) {
        LOG->UserLog(
            "", "",
            "While loading .sm/.ssc note data, there was an unmatched 2 at "
            "beat %f",
            NoteRowToBeat(begin->first));
        begin = out.RemoveTapNote(t, begin);
      } else {
        ++begin;
      }
    }
  }
  out.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::LoadFromSMNoteDataString(
    NoteData& out, const std::string& raw_sm_note_data, bool composite) {
  // Load note data
  std::string sm_note_data;
  std::string::size_type index_comment_start = 0;
  std::string::size_type index_comment_end = 0;
  std::string::size_type orig_size = raw_sm_note_data.size();
  const char* p = raw_sm_note_data.data();

  sm_note_data.reserve(orig_size);
  while ((index_comment_start = raw_sm_note_data.find(
              "//", index_comment_end)) != std::string::npos) {
    sm_note_data.append(p, index_comment_start - index_comment_end);
    p += index_comment_start - index_comment_end;
    index_comment_end = raw_sm_note_data.find("\n", index_comment_start);
    index_comment_end =
        (index_comment_end == std::string::npos ? orig_size
                                                : index_comment_end + 1);
    p += index_comment_end - index_comment_start;
  }
  sm_note_data.append(p, orig_size - index_comment_end);

  // Clear notes, but keep the same number of tracks.
  int num_tracks = out.GetNumTracks();
  out.Init();
  out.SetNumTracks(num_tracks);

  if (!composite) {
    LoadFromSMNoteDataStringWithPlayer(
        out, sm_note_data, 0, sm_note_data.size(), kPlayerInvalid, num_tracks);
    return;
  }

  int start = 0, size = -1;

  std::vector<NoteData> parts;
  FOREACH_PlayerNumber(pn) {
    // Split in place.
    split(sm_note_data, "&", start, size, false);
    if (unsigned(start) == sm_note_data.size()) {
      break;
    }
    parts.push_back(NoteData());
    NoteData& nd = parts.back();

    nd.SetNumTracks(num_tracks);
    LoadFromSMNoteDataStringWithPlayer(
        nd, sm_note_data, start, size, pn, num_tracks);
  }
  CombineCompositeNoteData(out, parts);
  out.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::InsertHoldTails(NoteData& inout) {
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    /* Collect the tails to insert in a first pass. We can't insert while
     * iterating, since inserting into the track invalidates our iterators. */
    std::vector<std::pair<int, TapNote>> tails;
    for (NoteData::iterator it = inout.begin(t); it != inout.end(t); ++it) {
      int row = it->first;
      const TapNote& tn = it->second;
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      TapNote tail = tn;
      tail.type_ = TapNoteType_HoldTail;

      /* If iDuration is 0, we'd end up overwriting the head with the tail.
       * Empty hold notes aren't valid. */
      ASSERT(tn.duration_ != 0);

      tails.push_back({row + tn.duration_, tail});
    }

    for (const std::pair<int, TapNote>& tail : tails) {
      inout.SetTapNote(t, tail.first, tail.second);
    }
  }
}

void NoteDataUtil::GetSMNoteDataString(
    const NoteData& in, std::string& ret, bool include_measure_comments) {
  // Get note data
  std::vector<NoteData> parts;
  float last_beat = -1.0f;

  SplitCompositeNoteData(in, parts);

  for (NoteData& nd : parts) {
    InsertHoldTails(nd);
    last_beat = std::max(last_beat, nd.GetLastBeat());
  }

  int last_measure = int(last_beat / kBeatsPerMeasure);

  ret = "";
  int part_num = 0;
  for (const NoteData& nd : parts) {
    if (part_num++ != 0) {
      ret.append("&\n");
    }
    // Rows are visited in increasing order, so walk each track forward
    // instead of searching for every row/track cell.
    std::vector<NoteData::const_iterator> cursors;
    for (int t = 0; t < nd.GetNumTracks(); ++t) {
      cursors.push_back(nd.begin(t));
    }
    for (int m = 0; m <= last_measure; ++m)  // foreach measure
    {
      if (m) {
        ret.append(1, ',');
      }
      if (include_measure_comments) {
        ret += ssprintf("  // measure %d\n", m);
      }

      NoteType nt = GetSmallestNoteTypeForMeasure(nd, m);
      int row_spacing;
      if (nt == NoteType_Invalid) {
        row_spacing = 1;
      } else {
        row_spacing = std::lrint(NoteTypeToBeat(nt) * kRowsPerBeat);
      }
      // (verify first)
      // iRowSpacing = BeatToNoteRow( NoteTypeToBeat(nt) );

      const int measure_start_row = m * kRowsPerMeasure;
      const int measure_last_row = (m + 1) * kRowsPerMeasure - 1;

      for (int r = measure_start_row; r <= measure_last_row; r += row_spacing) {
        for (int t = 0; t < nd.GetNumTracks(); ++t) {
          NoteData::const_iterator& it = cursors[t];
          while (it != nd.end(t) && it->first < r) {
            ++it;
          }
          const TapNote& tn =
              (it != nd.end(t) && it->first == r) ? it->second : tap_empty;
          char c;
          switch (tn.type_) {
            case TapNoteType_Empty:
              c = '0';
              break;
            case TapNoteType_Tap:
              c = '1';
              break;
            case TapNoteType_HoldHead:
              switch (tn.sub_type_) {
                case TapNoteSubType_Hold:
                  c = '2';
                  break;
                case TapNoteSubType_Roll:
                  c = '4';
                  break;
                // case TapNoteSubType_Mine:	c = 'N'; break;
                default:
                  FAIL_M(
                      ssprintf("Invalid tap note subtype: %i", tn.sub_type_));
              }
              break;
            case TapNoteType_HoldTail:
              c = '3';
              break;
            case TapNoteType_Mine:
              c = 'M';
              break;
            case TapNoteType_Attack:
              c = 'A';
              break;
            case TapNoteType_AutoKeysound:
              c = 'K';
              break;
            case TapNoteType_Lift:
              c = 'L';
              break;
            case TapNoteType_Fake:
              c = 'F';
              break;
            default:
              c = '\0';
              FAIL_M(ssprintf("Invalid tap note type: %i", tn.type_));
          }
          ret.push_back(c);

          if (tn.type_ == TapNoteType_Attack) {
            ret.append(ssprintf(
                "{%s:%.2f}", tn.attack_modifiers_.c_str(),
                tn.attack_duration_seconds_));
          }
          // hey maybe if we have TapNoteType_Item we can do things here.
          if (tn.keysound_index_ >= 0) {
            ret.append(ssprintf("[%d]", tn.keysound_index_));
          }
        }

        ret.push_back('\n');
      }
    }
  }
}

bool NoteDataUtil::SplitCompositeOrStackedNoteData(
    const NoteData& in, std::vector<NoteData>& out, StepsType steps_type) {
  if (in.IsComposite()) {
    SplitCompositeNoteData(in, out);
    return true;
  } else if (
      GAMEMAN->GetStepsTypeInfo(steps_type).steps_type_category_ ==
      StepsTypeCategory::StepsTypeCategory_Couple) {
    // XXX: Assumption that couple will always have an even number of notes.
    const int tracks = in.GetNumTracks() / 2;
    out.resize(2);
    out[PLAYER_1] = in;
    out[PLAYER_1].SetNumTracks(tracks);
    out[PLAYER_2] = in;
    NoteDataUtil::ShiftTracks(out[PLAYER_2], tracks);
    out[PLAYER_2].SetNumTracks(tracks);
    return true;
  } else {
    return false;
  }
}

void NoteDataUtil::SplitCompositeNoteData(
    const NoteData& in, std::vector<NoteData>& out) {
  if (!in.IsComposite()) {
    out.push_back(in);
    return;
  }

  FOREACH_PlayerNumber(pn) {
    out.push_back(NoteData());
    out.back().SetNumTracks(in.GetNumTracks());
  }

  for (int t = 0; t < in.GetNumTracks(); ++t) {
    for (NoteData::const_iterator iter = in.begin(t); iter != in.end(t);
         ++iter) {
      int row = iter->first;
      TapNote tn = iter->second;
      /*
       XXX: This code is (hopefully) a temporary hack to make sure that
       routine charts don't have any notes without players assigned to them.
       I suspect this is due to a related bug that these problems were
       occuring to begin with, but at this time, I am unsure how to deal with
       it. Hopefully this hack can be removed soon. -- Jason "Wolfman2000" Felds
       */
      const Style* cur_style = GAMESTATE->GetCurrentStyle(kPlayerInvalid);
      if ((cur_style == nullptr ||
           cur_style->style_type_ == StyleType_TwoPlayersSharedSides) &&
          int(tn.pn_) > NUM_PlayerNumber) {
        tn.pn_ = PLAYER_1;
      }
      unsigned index = int(tn.pn_);

      ASSERT_M(
          index < NUM_PlayerNumber,
          ssprintf(
              "We have a note not assigned to a player. The note in question "
              "is on beat %f, column %i.",
              NoteRowToBeat(row), t + 1));
      tn.pn_ = kPlayerInvalid;
      out[index].SetTapNote(t, row, tn);
    }
  }
}

void NoteDataUtil::CombineCompositeNoteData(
    NoteData& out, const std::vector<NoteData>& in) {
  for (const NoteData& nd : in) {
    const int max_tracks = std::min(out.GetNumTracks(), nd.GetNumTracks());

    for (int track = 0; track < max_tracks; ++track) {
      for (NoteData::const_iterator i = nd.begin(track); i != nd.end(track);
           ++i) {
        int row = i->first;
        if (out.IsHoldNoteAtRow(track, i->first)) {
          continue;
        }
        if (i->second.type_ == TapNoteType_HoldHead) {
          out.AddHoldNote(track, row, row + i->second.duration_, i->second);
        } else {
          out.SetTapNote(track, row, i->second);
        }
      }
    }
  }
  out.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::LoadTransformedSlidingWindow(
    const NoteData& in, NoteData& out, int new_num_tracks) {
  // reset all notes
  out.Init();

  if (in.GetNumTracks() > new_num_tracks) {
    // Use a different algorithm for reducing tracks.
    LoadOverlapped(in, out, new_num_tracks);
    return;
  }

  out.SetNumTracks(new_num_tracks);

  if (in.GetNumTracks() == 0) {
    return;  // nothing to do and don't AV below
  }

  int cur_track_offset = 0;
  int track_offset_min = 0;
  int track_offset_max = std::abs(new_num_tracks - in.GetNumTracks());
  int offset_increasing = true;

  int last_measure = 0;
  int measures_since_change = 0;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS(in, r) {
    const int measure = r / kRowsPerMeasure;
    if (measure != last_measure) {
      ++measures_since_change;
    }

    if (measure != last_measure &&
        measures_since_change >=
            4)  // adjust sliding window every 4 measures at most
    {
      // See if there is a hold crossing the beginning of this measure
      bool hold_crosses_this_measure = false;

      for (int t = 0; t < in.GetNumTracks(); t++) {
        if (in.IsHoldNoteAtRow(t, r - 1) && in.IsHoldNoteAtRow(t, r)) {
          hold_crosses_this_measure = true;
          break;
        }
      }

      // adjust offset
      if (!hold_crosses_this_measure) {
        measures_since_change = 0;
        cur_track_offset += offset_increasing ? 1 : -1;
        if (cur_track_offset == track_offset_min ||
            cur_track_offset == track_offset_max) {
          offset_increasing ^= true;
        }
        rage_clamp(cur_track_offset, track_offset_min, track_offset_max);
      }
    }

    last_measure = measure;

    // copy notes in this measure
    for (int t = 0; t < in.GetNumTracks(); t++) {
      int old_track = t;
      int new_track = (old_track + cur_track_offset) % new_num_tracks;
      TapNote tn = in.GetTapNote(old_track, r);
      tn.pn_ = kPlayerInvalid;
      out.SetTapNote(new_track, r, tn);
    }
  }
  out.RevalidateATIs(std::vector<int>(), false);
}

void PlaceAutoKeysound(NoteData& out, int row, TapNote tap) {
  int empty_track = -1;
  int empty_row = row;
  int new_num_tracks = out.GetNumTracks();
  bool found_empty_track = false;
  int rows_to_look[3] = {0, -1, 1};

  for (int j = 0; j < 3; j++) {
    int r = rows_to_look[j] + row;
    if (r < 0) {
      continue;
    }
    for (int i = 0; i < new_num_tracks; ++i) {
      if (out.GetTapNote(i, r) == tap_empty && !out.IsHoldNoteAtRow(i, r)) {
        empty_track = i;
        empty_row = r;
        found_empty_track = true;
        break;
      }
    }
    if (found_empty_track) {
      break;
    }
  }

  if (empty_track != -1) {
    tap.type_ = TapNoteType_AutoKeysound;
    out.SetTapNote(empty_track, empty_row, tap);
  }
}

void NoteDataUtil::LoadOverlapped(
    const NoteData& in, NoteData& out, int new_num_tracks) {
  out.SetNumTracks(new_num_tracks);

  /* Keep track of the last source track that put a tap into each destination
   * track, and the row of that tap. Then, if two rows are trying to put taps
   * into the same row within the shift threshold, shift the newcomer source
   * row. */
  int last_source_track[kMaxNoteTracks];
  int last_source_row[kMaxNoteTracks];
  int dest_row[kMaxNoteTracks];

  for (int tr = 0; tr < kMaxNoteTracks; ++tr) {
    last_source_track[tr] = -1;
    last_source_row[tr] = -kMaxNoteRow;
    dest_row[tr] = tr;
    wrap(dest_row[tr], new_num_tracks);
  }

  const int shift_threshold = BeatToNoteRow(1);

  FOREACH_NONEMPTY_ROW_ALL_TRACKS(in, row) {
    for (int track_from = 0; track_from < in.GetNumTracks(); ++track_from) {
      TapNote from = in.GetTapNote(track_from, row);
      if (from.type_ == TapNoteType_Empty ||
          from.type_ == TapNoteType_AutoKeysound) {
        continue;
      }
      from.pn_ = kPlayerInvalid;

      // If this is a hold note, find the end.
      int end_index = row;
      if (from.type_ == TapNoteType_HoldHead) {
        end_index = row + from.duration_;
      }

      int& track_to = dest_row[track_from];
      if (last_source_track[track_to] != track_from) {
        if (end_index - last_source_row[track_to] < shift_threshold) {
          /* This destination track is in use by a different source
           * track. Use the least-recently-used track. */
          for (int dest_track = 0; dest_track < new_num_tracks; ++dest_track) {
            if (last_source_row[dest_track] < last_source_row[track_to]) {
              track_to = dest_track;
            }
          }
        }

        // If it's still in use, then we just don't have an available track.
        if (end_index - last_source_row[track_to] < shift_threshold) {
          // If it has a keysound, put it in autokeysound track.
          if (from.keysound_index_ >= 0) {
            TapNote tap = from;
            PlaceAutoKeysound(out, row, tap);
          }
          continue;
        }
      }

      last_source_track[track_to] = track_from;
      last_source_row[track_to] = end_index;
      out.SetTapNote(track_to, row, from);
      if (from.type_ == TapNoteType_HoldHead) {
        const TapNote& tail = in.GetTapNote(track_from, end_index);
        out.SetTapNote(track_to, end_index, tail);
      }
    }

    // find empty track for autokeysounds in 2 next rows, so you can hear most
    // autokeysounds
    for (int track_from = 0; track_from < in.GetNumTracks(); ++track_from) {
      const TapNote& from = in.GetTapNote(track_from, row);
      if (from.type_ != TapNoteType_AutoKeysound) {
        continue;
      }

      PlaceAutoKeysound(out, row, from);
    }
  }
  out.RevalidateATIs(std::vector<int>(), false);
}

int FindLongestOverlappingHoldNoteForAnyTrack(const NoteData& in, int row) {
  int max_tail_row = -1;
  for (int t = 0; t < in.GetNumTracks(); t++) {
    const TapNote& tn = in.GetTapNote(t, row);
    if (tn.type_ == TapNoteType_HoldHead) {
      max_tail_row = std::max(max_tail_row, row + tn.duration_);
    }
  }

  return max_tail_row;
}

// For every row in "in" with a tap or hold on any track, enable the specified
// tracks in "out".
void LightTransformHelper(
    const NoteData& in, NoteData& out, const std::vector<int>& tracks) {
  for (unsigned i = 0; i < tracks.size(); ++i) {
    ASSERT_M(
        tracks[i] < out.GetNumTracks(),
        ssprintf("%i, %i", tracks[i], out.GetNumTracks()));
  }

  FOREACH_NONEMPTY_ROW_ALL_TRACKS(in, r) {
    /* If any row starts a hold note, find the end of the hold note, and keep
     * searching until we've extended to the end of the latest overlapping hold
     * note. */
    int hold_start = r;
    int hold_end = -1;
    for (;;) {
      int max_tail_row = FindLongestOverlappingHoldNoteForAnyTrack(in, r);
      if (max_tail_row == -1) {
        break;
      }
      hold_end = max_tail_row;
      r = max_tail_row;
    }

    if (hold_end != -1) {
      // If we found a hold note, add it to all tracks.
      for (unsigned i = 0; i < tracks.size(); ++i) {
        int t = tracks[i];
        out.AddHoldNote(t, hold_start, hold_end, tap_original_hold_head);
      }
      continue;
    }

    if (in.IsRowEmpty(r)) {
      continue;
    }

    // Enable every track in the output.
    for (unsigned i = 0; i < tracks.size(); ++i) {
      int t = tracks[i];
      out.SetTapNote(t, r, tap_original_tap);
    }
  }
}

// For every track enabled in "in", enable all tracks in "out".
void NoteDataUtil::LoadTransformedLights(
    const NoteData& in, NoteData& out, int new_num_tracks) {
  // make a new NoteData that is a copy of the input.
  NoteData bass;
  bass.Init();
  bass.CopyAll(in);

  // if the user desires,
  // copy from the marquee data, but slim down the notes.
  // this makes it look more bass-ish and less like the original chart.
  if (PREFSMAN->lights_simplify_bass_) {
    RemoveHoldNotes(bass);
    Little(bass);
  }

  LoadTransformedLightsFromTwo(in, bass, out);

  // old code which will make all lights blink on every note.
  /*
  // reset all notes
  out.Init();

  out.SetNumTracks( iNewNumTracks );

  std::vector<int> aiTracks;
  for( int i = 0; i < out.GetNumTracks(); ++i )
          aiTracks.push_back( i );

  LightTransformHelper( in, out, aiTracks );
  */
}

// This transform is specific to StepsType_lights_cabinet.
#include "LightsManager.h"  // for LIGHT_*
void NoteDataUtil::LoadTransformedLightsFromTwo(
    const NoteData& marquee, const NoteData& bass, NoteData& out) {
  ASSERT(marquee.GetNumTracks() >= 4);
  ASSERT(bass.GetNumTracks() >= 1);

  /* For each track in "marquee", enable a track in the marquee lights.
   * This will reinit out. */
  {
    NoteData transformed_marquee;
    transformed_marquee.CopyAll(marquee);
    Wide(transformed_marquee);

    const int original_track_to_take_from[NUM_CabinetLight] = {0, 1,  2,
                                                               3, -1, -1};
    out.LoadTransformed(
        transformed_marquee, NUM_CabinetLight, original_track_to_take_from);
  }

  // For each track in "bass", enable the bass lights.
  {
    std::vector<int> tracks;
    tracks.push_back(LIGHT_BASS_LEFT);
    tracks.push_back(LIGHT_BASS_RIGHT);
    LightTransformHelper(bass, out, tracks);
  }

  // Delete all mines.
  NoteDataUtil::RemoveMines(out);
}

struct recent_note {
  int row_;
  int track_;
  recent_note() : row_(0), track_(0) {}
  recent_note(int r, int t) : row_(r), track_(t) {}
};

// CalculateRadarValues has to delay some stuff until a row ends, but can
// only detect a row ending when it hits the next note.  There isn't a note
// after the last row, so it also has to do the delayed stuff after exiting
// its loop.  So this state structure exists to be passed to a function that
// can be called from both places to do the work.  If this were Lua,
// DoRowEndRadarCalc would be a nested function. -Kyz
struct crv_state {
  bool judgable_;
  // hold_ends tracks where currently active holds will end, which is used
  // to count the number of hands. -Kyz
  std::vector<int> hold_ends_;
  // num_holds_on_curr_row saves us the work of tracking where holds started
  // just to keep a jump of two holds from counting as a hand.
  int num_holds_on_curr_row_;
  int num_notes_on_curr_row_;

  crv_state()
      : judgable_(false),
        num_holds_on_curr_row_(0),
        num_notes_on_curr_row_(0) {}
};

static void DoRowEndRadarCalc(crv_state& state, RadarValues& out) {
  if (state.judgable_) {
    if (state.num_notes_on_curr_row_ +
            (state.hold_ends_.size() - state.num_holds_on_curr_row_) >=
        3) {
      ++out[RadarCategory_Hands];
    }
  }
}

void NoteDataUtil::CalculateRadarValues(
    const NoteData& in, float song_seconds, RadarValues& out) {
  TimingData* timing = GAMESTATE->GetProcessedTimingData();
  CalculateRadarValues(in, song_seconds, timing, out);
}

void NoteDataUtil::CalculateRadarValues(
    const NoteData& in, float song_seconds, const TimingData* timing,
    RadarValues& out) {
  // Anybody editing this function should also examine
  // NoteDataWithScoring::GetActualRadarValues to make sure it handles things
  // the same way.
  out.Zero();
  int curr_row = -1;
  // recent_notes is used to calculate the voltage.  Each element is the row
  // and track number of a tap note.  When the pair at the beginning is too
  // old, it's deleted.  This provides a way to have a rolling window
  // that scans for the peak step density. -Kyz
  std::vector<recent_note> recent_notes;
  NoteData::all_tracks_const_iterator curr_note =
      in.GetTapNoteRangeAllTracks(0, kMaxNoteRow);

  // total_taps exists because the stream calculation needs GetNumTapNotes,
  // but TapsAndHolds + Jumps + Hands would be inaccurate. -Kyz
  float total_taps = 0;
  const float voltage_window_beats = 8.0f;
  const int voltage_window = BeatToNoteRow(voltage_window_beats);
  size_t max_notes_in_voltage_window = 0;
  int num_chaos_rows = 0;
  crv_state state;

  while (!curr_note.IsAtEnd()) {
    if (curr_note.Row() != curr_row) {
      DoRowEndRadarCalc(state, out);
      curr_row = curr_note.Row();
      state.num_notes_on_curr_row_ = 0;
      state.num_holds_on_curr_row_ = 0;
      state.judgable_ = timing->IsJudgableAtRow(curr_row);
      for (size_t n = 0; n < state.hold_ends_.size(); ++n) {
        if (state.hold_ends_[n] < curr_row) {
          state.hold_ends_.erase(state.hold_ends_.begin() + n);
          --n;
        }
      }
      for (size_t n = 0; n < recent_notes.size(); ++n) {
        if (recent_notes[n].row_ < curr_row - voltage_window) {
          recent_notes.erase(recent_notes.begin() + n);
          --n;
        } else {
          // recent_notes is kept sorted, so reaching the first note that
          // isn't old enough to remove means we're finished. -Kyz
          break;
        }
      }
      // GetChaosRadarValue did not care about whether a row is judgable.
      // So chaos is checked here. -Kyz
      if (GetNoteType(curr_row) >= NOTE_TYPE_12TH) {
        ++num_chaos_rows;
      }
    }
    if (state.judgable_) {
      switch (curr_note->type_) {
        case TapNoteType_Tap:
        case TapNoteType_HoldHead:
          // Lifts have to be counted with taps for them to be added to max dp
          // correctly. -Kyz
        case TapNoteType_Lift:
          // HoldTails and Attacks are counted by IsTap.  But it doesn't
          // make sense to count HoldTails as hittable notes. -Kyz
        case TapNoteType_Attack:
          ++out[RadarCategory_Notes];
          ++state.num_notes_on_curr_row_;
          ++total_taps;
          recent_notes.push_back(recent_note(curr_row, curr_note.Track()));
          max_notes_in_voltage_window =
              std::max(recent_notes.size(), max_notes_in_voltage_window);
          // If there is one hold active, and one tap on this row, it does
          // not count as a jump.  Hands do need to count the number of
          // holds active though. -Kyz
          switch (state.num_notes_on_curr_row_) {
            case 1:
              ++out[RadarCategory_TapsAndHolds];
              break;
            case 2:
              ++out[RadarCategory_Jumps];
              break;
            default:
              break;
          }
          if (curr_note->type_ == TapNoteType_HoldHead) {
            state.hold_ends_.push_back(curr_row + curr_note->duration_);
            ++state.num_holds_on_curr_row_;
            switch (curr_note->sub_type_) {
              case TapNoteSubType_Hold:
                ++out[RadarCategory_Holds];
                break;
              case TapNoteSubType_Roll:
                ++out[RadarCategory_Rolls];
                break;
              default:
                break;
            }
          } else if (curr_note->type_ == TapNoteType_Lift) {
            ++out[RadarCategory_Lifts];
          }
          break;
        case TapNoteType_Mine:
          ++out[RadarCategory_Mines];
          break;
        case TapNoteType_Fake:
          ++out[RadarCategory_Fakes];
          break;
        default:
          break;
      }
    } else {
      ++out[RadarCategory_Fakes];
    }
    ++curr_note;
  }
  DoRowEndRadarCalc(state, out);

  // Walking the notes complete, now assign any values that remain. -Kyz
  if (song_seconds > 0.0f) {
    out[RadarCategory_Stream] = (total_taps / song_seconds) / 7.0f;
    // As seen in GetVoltageRadarValue:  Don't use the timing data, just
    // pretend the beats are evenly spaced. -Kyz
    float avg_bps = in.GetLastBeat() / song_seconds;
    out[RadarCategory_Voltage] =
        ((max_notes_in_voltage_window / voltage_window_beats) * avg_bps) /
        10.0f;
    out[RadarCategory_Air] = out[RadarCategory_Jumps] / song_seconds;
    out[RadarCategory_Freeze] = out[RadarCategory_Holds] / song_seconds;
    out[RadarCategory_Chaos] = num_chaos_rows / song_seconds * .5f;
  }
  // Sorry, there's not an assert here anymore for making sure all fields
  // are set.  There's a comment in the RadarCategory enum to direct
  // attention here when adding new categories. -Kyz
}

void NoteDataUtil::RemoveHoldNotes(
    NoteData& in, int start_index, int end_index) {
  // turn all the HoldNotes into TapNotes
  for (int t = 0; t < in.GetNumTracks(); ++t) {
    NoteData::TrackMap::iterator begin, end;
    in.GetTapNoteRangeInclusive(t, start_index, end_index, begin, end);
    for (; begin != end; ++begin) {
      if (begin->second.type_ != TapNoteType_HoldHead ||
          begin->second.sub_type_ != TapNoteSubType_Hold) {
        continue;
      }
      begin->second.type_ = TapNoteType_Tap;
    }
  }
  in.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::ChangeRollsToHolds(
    NoteData& in, int start_index, int end_index) {
  for (int t = 0; t < in.GetNumTracks(); ++t) {
    NoteData::TrackMap::iterator begin, end;
    in.GetTapNoteRangeInclusive(t, start_index, end_index, begin, end);
    for (; begin != end; ++begin) {
      if (begin->second.type_ != TapNoteType_HoldHead ||
          begin->second.sub_type_ != TapNoteSubType_Roll) {
        continue;
      }
      begin->second.sub_type_ = TapNoteSubType_Hold;
    }
  }
  in.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::ChangeHoldsToRolls(
    NoteData& in, int start_index, int end_index) {
  for (int t = 0; t < in.GetNumTracks(); ++t) {
    NoteData::TrackMap::iterator begin, end;
    in.GetTapNoteRangeInclusive(t, start_index, end_index, begin, end);
    for (; begin != end; ++begin) {
      if (begin->second.type_ != TapNoteType_HoldHead ||
          begin->second.sub_type_ != TapNoteSubType_Hold) {
        continue;
      }
      begin->second.sub_type_ = TapNoteSubType_Roll;
    }
  }
  in.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveSimultaneousNotes(
    NoteData& in, int max_simultaneous, int start_index, int end_index) {
  // Remove tap and hold notes so no more than iMaxSimultaneous buttons are
  // being held at any given time.  Never touch data outside of the range given;
  // if many hold notes are overlapping iStartIndex, and we'd have to change
  // those holds to obey iMaxSimultaneous, just do the best we can without doing
  // so.
  if (in.IsComposite()) {
    // Do this per part.
    std::vector<NoteData> parts;

    SplitCompositeNoteData(in, parts);
    for (NoteData& nd : parts) {
      RemoveSimultaneousNotes(nd, max_simultaneous, start_index, end_index);
    }
    in.Init();
    in.SetNumTracks(parts.front().GetNumTracks());
    CombineCompositeNoteData(in, parts);
  }
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(in, r, start_index, end_index) {
    std::set<int> tracks_held;
    in.GetTracksHeldAtRow(r, tracks_held);

    // remove the first tap note or the first hold note that starts on this row
    int total_tracks_pressed =
        in.GetNumTracksWithTapOrHoldHead(r) + tracks_held.size();
    int tracks_to_remove = std::max(0, total_tracks_pressed - max_simultaneous);
    for (int t = 0; tracks_to_remove > 0 && t < in.GetNumTracks(); t++) {
      const TapNote& tn = in.GetTapNote(t, r);
      if (tn.type_ == TapNoteType_Tap || tn.type_ == TapNoteType_HoldHead) {
        in.SetTapNote(t, r, tap_empty);
        tracks_to_remove--;
      }
    }
  }
  in.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveJumps(
    NoteData& inout, int start_index, int end_index) {
  RemoveSimultaneousNotes(inout, 1, start_index, end_index);
}

void NoteDataUtil::RemoveHands(
    NoteData& inout, int start_index, int end_index) {
  RemoveSimultaneousNotes(inout, 2, start_index, end_index);
}

void NoteDataUtil::RemoveQuads(
    NoteData& inout, int start_index, int end_index) {
  RemoveSimultaneousNotes(inout, 3, start_index, end_index);
}

void NoteDataUtil::RemoveSpecificTapNotes(
    NoteData& inout, TapNoteType tn, int start_index, int end_index) {
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(inout, t, r, start_index, end_index) {
      if (inout.GetTapNote(t, r).type_ == tn) {
        inout.SetTapNote(t, r, tap_empty);
      }
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveMines(
    NoteData& inout, int start_index, int end_index) {
  RemoveSpecificTapNotes(inout, TapNoteType_Mine, start_index, end_index);
}

void NoteDataUtil::RemoveLifts(
    NoteData& inout, int start_index, int end_index) {
  RemoveSpecificTapNotes(inout, TapNoteType_Lift, start_index, end_index);
}

void NoteDataUtil::RemoveFakes(
    NoteData& inout, const TimingData& timing_data, int start_index,
    int end_index) {
  RemoveSpecificTapNotes(inout, TapNoteType_Fake, start_index, end_index);
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(inout, t, r, start_index, end_index) {
      if (!timing_data.IsJudgableAtRow(r)) {
        inout.SetTapNote(t, r, tap_empty);
      }
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveAllButOneTap(NoteData& inout, int row) {
  if (row < 0) {
    return;
  }

  int track;
  for (track = 0; track < inout.GetNumTracks(); ++track) {
    if (inout.GetTapNote(track, row).type_ == TapNoteType_Tap) {
      break;
    }
  }

  track++;

  for (; track < inout.GetNumTracks(); ++track) {
    NoteData::iterator iter = inout.FindTapNote(track, row);
    if (iter != inout.end(track) && iter->second.type_ == TapNoteType_Tap) {
      inout.RemoveTapNote(track, iter);
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

// TODO: Perform appropriate matrix calculations for everything instead.
static void GetTrackMapping(
    StepsType st, NoteDataUtil::TrackMapping tt, int num_tracks,
    int* take_from_track) {
  // Identity transform for cases not handled below.
  for (int t = 0; t < kMaxNoteTracks; ++t) {
    take_from_track[t] = t;
  }

  switch (tt) {
    case NoteDataUtil::lrmirror:
      switch (st) {
        case StepsType_dance_single:
          take_from_track[0] = 3;
          take_from_track[3] = 0;
          break;
        case StepsType_dance_double:
          take_from_track[0] = 7;
          take_from_track[1] = 5;
          take_from_track[2] = 6;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          take_from_track[5] = 1;
          take_from_track[6] = 2;
          take_from_track[7] = 0;
          break;
        case StepsType_dance_solo:
          take_from_track[0] = 5;
          take_from_track[1] = 4;
          take_from_track[4] = 1;
          take_from_track[5] = 0;
          break;
        case StepsType_pump_single:
          take_from_track[0] = 4;
          take_from_track[1] = 3;
          take_from_track[3] = 1;
          take_from_track[4] = 0;
          break;
        case StepsType_pump_halfdouble:
          take_from_track[0] = 5;
          take_from_track[1] = 4;
          take_from_track[2] = 3;
          take_from_track[3] = 2;
          take_from_track[4] = 1;
          take_from_track[5] = 0;
          break;
        case StepsType_pump_double:
          take_from_track[0] = 9;
          take_from_track[1] = 8;
          take_from_track[2] = 7;
          take_from_track[3] = 6;
          take_from_track[4] = 5;
          take_from_track[5] = 4;
          take_from_track[6] = 3;
          take_from_track[7] = 2;
          take_from_track[8] = 1;
          take_from_track[9] = 0;
          break;
        case StepsType_pump_couple:
          take_from_track[0] = 9;
          take_from_track[1] = 8;
          take_from_track[2] = 7;
          take_from_track[3] = 6;
          take_from_track[4] = 5;
          take_from_track[5] = 4;
          take_from_track[6] = 3;
          take_from_track[7] = 2;
          take_from_track[8] = 1;
          take_from_track[9] = 0;
          break;
        default:
          break;
      }
      break;
    case NoteDataUtil::udmirror:
      switch (st) {
        case StepsType_dance_single:
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          break;
        case StepsType_dance_double:
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          break;
        case StepsType_dance_solo:
          take_from_track[2] = 3;
          take_from_track[3] = 2;
          break;
        case StepsType_pump_single:
          take_from_track[0] = 1;
          take_from_track[1] = 0;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          break;
        case StepsType_pump_halfdouble:
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          break;
        case StepsType_pump_double:
          take_from_track[0] = 1;
          take_from_track[1] = 0;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          take_from_track[8] = 9;
          take_from_track[9] = 8;
          break;
        case StepsType_pump_couple:
          take_from_track[0] = 1;
          take_from_track[1] = 0;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          take_from_track[8] = 9;
          take_from_track[9] = 8;
          break;
        default:
          break;
      }
      break;
    case NoteDataUtil::left:
    case NoteDataUtil::right:
      // Is there a way to do this without handling each StepsType? -Chris
      switch (st) {
        case StepsType_dance_single:
        case StepsType_dance_double:
        case StepsType_dance_couple:
        case StepsType_dance_routine:
          take_from_track[0] = 2;
          take_from_track[1] = 0;
          take_from_track[2] = 3;
          take_from_track[3] = 1;
          take_from_track[4] = 6;
          take_from_track[5] = 4;
          take_from_track[6] = 7;
          take_from_track[7] = 5;
          break;
        case StepsType_dance_solo:
          take_from_track[0] = 5;
          take_from_track[1] = 4;
          take_from_track[2] = 0;
          take_from_track[3] = 3;
          take_from_track[4] = 1;
          take_from_track[5] = 2;
          break;
        case StepsType_pump_single:
        case StepsType_pump_couple:
          take_from_track[0] = 1;
          take_from_track[1] = 3;
          take_from_track[2] = 2;
          take_from_track[3] = 4;
          take_from_track[4] = 0;
          take_from_track[5] = 6;
          take_from_track[6] = 8;
          take_from_track[7] = 7;
          take_from_track[8] = 9;
          take_from_track[9] = 5;
          break;
        case StepsType_pump_halfdouble:
          take_from_track[0] = 2;
          take_from_track[1] = 0;
          take_from_track[2] = 1;
          take_from_track[3] = 3;
          take_from_track[4] = 4;
          take_from_track[5] = 5;
          break;
        case StepsType_pump_double:
          take_from_track[0] = 8;
          take_from_track[1] = 9;
          take_from_track[2] = 7;
          take_from_track[3] = 5;
          take_from_track[4] = 6;
          take_from_track[5] = 3;
          take_from_track[6] = 4;
          take_from_track[7] = 2;
          take_from_track[8] = 0;
          take_from_track[9] = 1;
          break;
        default:
          break;
      }

      if (tt == NoteDataUtil::right) {
        // Invert.
        int track[kMaxNoteTracks];
        memcpy(track, take_from_track, sizeof(track));
        for (int t = 0; t < kMaxNoteTracks; ++t) {
          const int to = track[t];
          take_from_track[to] = t;
        }
      }

      break;
    case NoteDataUtil::backwards: {
      // If a Pump game type, treat differently. Otherwise, send to mirror.
      bool needs_backwards = true;
      switch (st) {
        case StepsType_pump_single:
        case StepsType_pump_couple: {
          take_from_track[0] = 3;
          take_from_track[1] = 4;
          take_from_track[2] = 2;
          take_from_track[3] = 0;
          take_from_track[4] = 1;
          take_from_track[5] = 8;
          take_from_track[6] = 9;
          take_from_track[7] = 2;
          take_from_track[8] = 5;
          take_from_track[9] = 6;
          break;
        }
        case StepsType_pump_double:
        case StepsType_pump_routine: {
          take_from_track[0] = 8;
          take_from_track[1] = 9;
          take_from_track[2] = 7;
          take_from_track[3] = 5;
          take_from_track[4] = 6;
          take_from_track[5] = 3;
          take_from_track[6] = 4;
          take_from_track[7] = 2;
          take_from_track[8] = 0;
          take_from_track[9] = 1;
          break;
        }
        case StepsType_pump_halfdouble: {
          take_from_track[0] = 5;
          take_from_track[1] = 3;
          take_from_track[2] = 4;
          take_from_track[3] = 1;
          take_from_track[4] = 2;
          take_from_track[5] = 0;
          break;
        }
        default:
          needs_backwards = false;
      }
      if (needs_backwards) {
        break;
      }
      [[fallthrough]];
    }
    case NoteDataUtil::mirror: {
      for (int t = 0; t < num_tracks; t++) {
        take_from_track[t] = num_tracks - t - 1;
      }
      break;
    }
    case NoteDataUtil::shuffle:
    case NoteDataUtil::super_shuffle:  // use shuffle code to mix up HoldNotes
                                       // without creating impossible patterns
    {
      // TRICKY: Shuffle so that both player get the same shuffle mapping
      // in the same round. This is already achieved in beat mode.
      int orig[kMaxNoteTracks];
      memcpy(orig, take_from_track, sizeof(orig));

      int shuffle_seed = GAMESTATE->stage_seed_;
      do {
        RandomGen rnd(shuffle_seed);
        std::shuffle(&take_from_track[0], &take_from_track[num_tracks], rnd);
        shuffle_seed++;
      } while (!memcmp(
          orig, take_from_track,
          sizeof(orig)));  // shuffle again if shuffle managed to shuffle them
                           // in the same order
    } break;
    case NoteDataUtil::soft_shuffle: {
      // XXX: this is still pretty much a stub.

      // soft shuffle, as described at
      // http://www.stepmania.com/forums/showthread.php?t=19469

      /* one of the following at random:
       *
       * 0. No columns changed
       * 1. Left and right columns swapped
       * 2. Down and up columns swapped
       * 3. Mirror (left and right swapped, down and up swapped)
       * ----------------------------------------------------------------
       * To extend it to handle all game types, it would pick each axis
       * of symmetry the game type has and either flip it or not flip it.
       *
       * For instance, PIU singles has four axes:
       * horizontal, vertical,
       * diagonally top left to bottom right,
       * diagonally bottom left to top right.
       * (above text from forums) */

      // TRICKY: Shuffle so that both player get the same shuffle mapping
      // in the same round.

      int shuffle_seed = GAMESTATE->stage_seed_;
      RandomGen rnd(shuffle_seed);
      int rand_choice = (rnd() % 4);

      // XXX: cases 1 and 2 only implemented for dance_*
      switch (rand_choice) {
        case 1:  // left and right mirror
        case 2:  // up and down mirror
          switch (st) {
            case StepsType_dance_single:
              if (rand_choice == 1) {
                // left and right
                take_from_track[0] = 3;
                take_from_track[3] = 0;
              }
              if (rand_choice == 2) {
                // up and down
                take_from_track[1] = 2;
                take_from_track[2] = 1;
              }
              break;
            case StepsType_dance_double:
            case StepsType_dance_couple:
            case StepsType_dance_routine:
              if (rand_choice == 1) {
                // left and right
                take_from_track[0] = 3;
                take_from_track[3] = 0;
                take_from_track[4] = 7;
                take_from_track[7] = 4;
              }
              if (rand_choice == 2) {
                // up and down
                take_from_track[1] = 2;
                take_from_track[2] = 1;
                take_from_track[5] = 6;
                take_from_track[6] = 5;
              }
              break;
            // here be dragons (unchanged code)
            case StepsType_dance_solo:
              take_from_track[0] = 5;
              take_from_track[1] = 4;
              take_from_track[2] = 0;
              take_from_track[3] = 3;
              take_from_track[4] = 1;
              take_from_track[5] = 2;
              break;
            case StepsType_pump_single:
            case StepsType_pump_couple:
              take_from_track[0] = 3;
              take_from_track[1] = 4;
              take_from_track[2] = 2;
              take_from_track[3] = 0;
              take_from_track[4] = 1;
              take_from_track[5] = 8;
              take_from_track[6] = 9;
              take_from_track[7] = 7;
              take_from_track[8] = 5;
              take_from_track[9] = 6;
              break;
            case StepsType_pump_halfdouble:
              take_from_track[0] = 2;
              take_from_track[1] = 0;
              take_from_track[2] = 1;
              take_from_track[3] = 3;
              take_from_track[4] = 4;
              take_from_track[5] = 5;
              break;
            case StepsType_pump_double:
              take_from_track[0] = 8;
              take_from_track[1] = 9;
              take_from_track[2] = 7;
              take_from_track[3] = 5;
              take_from_track[4] = 6;
              take_from_track[5] = 3;
              take_from_track[6] = 4;
              take_from_track[7] = 2;
              take_from_track[8] = 0;
              take_from_track[9] = 1;
              break;
            default:
              break;
          }
          break;
        case 3:  // full mirror
          GetTrackMapping(
              st, NoteDataUtil::mirror, num_tracks, take_from_track);
          break;
        case 0:
        default:
          // case 0 and default are set by identity matrix above
          break;
      }
    } break;
    case NoteDataUtil::stomp:
      switch (st) {
        case StepsType_dance_single:
        case StepsType_dance_couple:
          take_from_track[0] = 3;
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          take_from_track[3] = 0;
          take_from_track[4] = 7;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          take_from_track[7] = 4;
          break;
        case StepsType_dance_double:
        case StepsType_dance_routine:
          take_from_track[0] = 1;
          take_from_track[1] = 0;
          take_from_track[2] = 3;
          take_from_track[3] = 2;
          take_from_track[4] = 5;
          take_from_track[5] = 4;
          take_from_track[6] = 7;
          take_from_track[7] = 6;
          break;
        default:
          break;
      }
      break;
    case NoteDataUtil::swap_up_down:
      switch (st) {
        case StepsType_dance_single:
        case StepsType_dance_double:
        case StepsType_dance_couple:
        case StepsType_dance_routine:
          take_from_track[0] = 0;
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          take_from_track[3] = 3;
          take_from_track[4] = 4;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          take_from_track[7] = 7;
          break;
        case StepsType_pump_single:
        case StepsType_pump_double:
        case StepsType_pump_couple:
        case StepsType_pump_routine:
          take_from_track[0] = 1;
          take_from_track[1] = 0;
          take_from_track[2] = 2;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          take_from_track[5] = 6;
          take_from_track[6] = 5;
          take_from_track[7] = 7;
          take_from_track[8] = 9;
          take_from_track[9] = 8;
          break;
        case StepsType_pump_halfdouble:
          take_from_track[0] = 0;
          take_from_track[1] = 2;
          take_from_track[2] = 1;
          take_from_track[3] = 4;
          take_from_track[4] = 3;
          take_from_track[5] = 5;
          break;
        default:
          break;
      }
      break;
    default:
      ASSERT(0);
  }
}

static void HyperShuffleNotes(NoteData& inout, int start_index, int end_index) {
  int num_tracks = inout.GetNumTracks();

  std::vector<int> hold_end_rows(num_tracks, -1);

  // A "free track" is a track that is not part of a hold and is therefore
  // free to be shuffled into.
  int free_tracks = num_tracks;

  // If this is the first row, there cannot be any active holds yet.
  if (start_index != 0) {
    // Search for any hold notes that might be present at the first row.
    // Once the loop below gets going, it keeps track of hold notes itself.
    for (int track = 0; track < num_tracks; track++) {
      int target_row = start_index;

      if (inout.GetPrevTapNoteRowForTrack(track, target_row)) {
        const TapNote& tn = inout.GetTapNote(track, target_row);

        // Check for a hold that ends on or after this row.
        if (tn.type_ == TapNoteType_HoldHead) {
          int hold_end_row = target_row + tn.duration_;

          if (hold_end_row >= start_index) {
            hold_end_rows[track] = hold_end_row;
            free_tracks--;
          }
        }
      }
    }
  }

  // Stores the list of tracks that can have notes placed into them.
  // This will be shuffled so that notes can be placed in different tracks.
  std::vector<int> target_tracks;
  // Stores the list of tap notes that need to be placed.
  std::vector<TapNote> target_taps;
  target_tracks.reserve(num_tracks);
  target_taps.reserve(num_tracks);

  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    target_tracks.clear();
    target_taps.clear();
    int actual_taps = 0;

    for (int track = 0; track < num_tracks; track++) {
      int hold_end_row = hold_end_rows[track];

      // Check if this track is actually still occupied by a hold.
      if (hold_end_row != -1) {
        if (r > hold_end_row) {
          hold_end_row = -1;
          hold_end_rows[track] = hold_end_row;
          free_tracks++;
        }
      }

      const TapNote tn1 = inout.GetTapNote(track, r);
      switch (tn1.type_) {
        case TapNoteType_HoldTail:
          ASSERT_M(
              0,
              ssprintf(
                  "Saw a HoldTail during HyperShuffleNotes (row %d, track %d)",
                  r, track));

          // If asserts are off, treat this HoldTail as if it were an empty
          // note. If they aren't off, this code can't be reached.
          inout.SetTapNote(track, r, tap_empty);

          [[fallthrough]];
        case TapNoteType_Empty:
          // Empty tap notes don't get directly placed in the shuffle table.
          // Instead, if they aren't in a hold, they get added to the list
          // of selectable tracks. Later on, new empty tap notes will be
          // created to pad the list of taps out to the required number.
          if (hold_end_row == -1) {
            target_tracks.push_back(track);
          }
          break;
        case TapNoteType_AutoKeysound:
        case TapNoteType_Tap:
        case TapNoteType_Lift:
        case TapNoteType_Mine:
        case TapNoteType_Attack:
        case TapNoteType_Fake:
        case TapNoteType_HoldHead:
          target_taps.push_back(tn1);
          actual_taps++;

          if (hold_end_row == -1) {
            target_tracks.push_back(track);
          } else {
            // This track won't be part of the shuffle table, so if it
            // is not cleared now it will not get cleared later.
            inout.SetTapNote(track, r, tap_empty);
          }
          break;
        case TapNoteType_Invalid:
        default:
          ASSERT(0);
      }
    }

    // Do the padding described above.
    for (int i = actual_taps; i < free_tracks; i++) {
      target_taps.push_back(tap_empty);
    }

    std::shuffle(
        target_tracks.begin(), target_tracks.end(), random_number_generator);

    // Go through the tracks in their shuffled order and drop tap notes.
    for (size_t i = 0; i < target_tracks.size(); i++) {
      const int target_track = target_tracks[i];
      const TapNote current_tn = target_taps[i];
      inout.SetTapNote(target_track, r, current_tn);

      // If it's a hold, mark this track occupied by a hold until its end.
      if (current_tn.type_ == TapNoteType_HoldHead) {
        ASSERT_M(
            hold_end_rows[target_track] == -1,
            ssprintf(
                "Tried to insert a hold into another hold (row %d, track %d)",
                r, target_track));
        free_tracks--;
        hold_end_rows[target_track] = r + current_tn.duration_;
      }
    }
  }
}

static void SuperShuffleTaps(NoteData& inout, int start_index, int end_index) {
  /*
   * We already did the normal shuffling code above, which did a good job
   * of shuffling HoldNotes without creating impossible patterns.
   * Now, go in and shuffle the TapNotes per-row.
   *
   * This is only called by NoteDataUtil::Turn.
   */
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    for (int t1 = 0; t1 < inout.GetNumTracks(); t1++) {
      const TapNote& tn1 = inout.GetTapNote(t1, r);
      switch (tn1.type_) {
        case TapNoteType_Empty:
        case TapNoteType_HoldHead:
        case TapNoteType_HoldTail:
        case TapNoteType_AutoKeysound:
          continue;  // skip
        case TapNoteType_Tap:
        case TapNoteType_Mine:
        case TapNoteType_Attack:
        case TapNoteType_Lift:
        case TapNoteType_Fake:
          break;  // shuffle this
          DEFAULT_FAIL(tn1.type_);
      }

      DEBUG_ASSERT_M(
          !inout.IsHoldNoteAtRow(t1, r),
          ssprintf(
              "There is a tap.type = %d inside of a hold at row %d", tn1.type_,
              r));

      // Probe for a spot to swap with.
      std::set<int> tried_tracks;
      for (int i = 0; i < 4; i++)  // probe max 4 times
      {
        int t2 = RandomInt(inout.GetNumTracks());
        if (tried_tracks.find(t2) !=
            tried_tracks.end()) {  // already tried this track
          continue;                // skip
        }
        tried_tracks.insert(t2);

        // swapping with ourself is a no-op
        if (t1 == t2) {
          break;  // done swapping
        }

        const TapNote& tn2 = inout.GetTapNote(t2, r);
        switch (tn2.type_) {
          case TapNoteType_HoldHead:
          case TapNoteType_HoldTail:
          case TapNoteType_AutoKeysound:
            continue;  // don't swap with these
          case TapNoteType_Empty:
          case TapNoteType_Tap:
          case TapNoteType_Mine:
          case TapNoteType_Attack:
          case TapNoteType_Lift:
          case TapNoteType_Fake:
            break;  // ok to swap with this
            DEFAULT_FAIL(tn2.type_);
        }

        // don't swap into the middle of a hold note
        if (inout.IsHoldNoteAtRow(t2, r)) {
          continue;
        }

        // do the swap
        const TapNote temp = tn1;
        inout.SetTapNote(t1, r, tn2);
        inout.SetTapNote(t2, r, temp);

        break;  // done swapping
      }
    }
  }
}

void NoteDataUtil::Turn(
    NoteData& inout, StepsType st, TrackMapping tt, int start_index,
    int end_index) {
  NoteData temp_note_data;

  if (tt == hyper_shuffle) {
    // HyperShuffle doesn't require a column transformation beforehand, so
    // there's no point in wasting time doing that.
    temp_note_data.CopyAll(inout);
    HyperShuffleNotes(temp_note_data, start_index, end_index);
  } else {
    // Calculate how the columns should be rearranged, and then do it.
    int take_from_track[kMaxNoteTracks];  // New track "t" will take from old
                                          // track iTakeFromTrack[t]
    GetTrackMapping(st, tt, inout.GetNumTracks(), take_from_track);

    temp_note_data.LoadTransformed(
        inout, inout.GetNumTracks(), take_from_track);

    // SuperShuffle doesn't affect holds, it instead relies on a regular shuffle
    // to do them first. Then it SuperShuffles everything else.
    if (tt == super_shuffle) {
      SuperShuffleTaps(temp_note_data, start_index, end_index);
    }
  }

  inout.CopyAll(temp_note_data);
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Backwards(NoteData& inout) {
  NoteData out;
  out.SetNumTracks(inout.GetNumTracks());

  int max_row = inout.GetLastRow();
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(inout, t, r, 0, max_row) {
      int row_earlier = r;
      int row_later = max_row - r;

      const TapNote& earlier = inout.GetTapNote(t, row_earlier);
      if (earlier.type_ == TapNoteType_HoldHead) {
        row_later -= earlier.duration_;
      }

      out.SetTapNote(t, row_later, earlier);
    }
  }

  inout.swap(out);
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::SwapSides(NoteData& inout) {
  int original_track_to_take_from[kMaxNoteTracks];
  for (int t = 0; t < inout.GetNumTracks() / 2; ++t) {
    int track_earlier = t;
    int track_later = t + inout.GetNumTracks() / 2 + inout.GetNumTracks() % 2;
    original_track_to_take_from[track_earlier] = track_later;
    original_track_to_take_from[track_later] = track_earlier;
  }

  NoteData orig(inout);
  inout.LoadTransformed(orig, orig.GetNumTracks(), original_track_to_take_from);
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Little(NoteData& inout, int start_index, int end_index) {
  // filter out all non-quarter notes
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(inout, t, i, start_index, end_index) {
      if (i % kRowsPerBeat == 0) {
        continue;
      }
      inout.SetTapNote(t, i, tap_empty);
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

// Make all quarter notes into jumps.
void NoteDataUtil::Wide(NoteData& inout, int start_index, int end_index) {
  /* Start on an even beat. */
  start_index = Quantize(start_index, BeatToNoteRow(2.0f));

  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, i, start_index, end_index) {
    if (i % BeatToNoteRow(2.0f) != 0) {
      continue;  // even beats only
    }

    bool hold_note_at_beat = false;
    for (int t = 0; !hold_note_at_beat && t < inout.GetNumTracks(); ++t) {
      if (inout.IsHoldNoteAtRow(t, i)) {
        hold_note_at_beat = true;
      }
    }
    if (hold_note_at_beat) {
      continue;  // skip.  Don't place during holds
    }

    if (inout.GetNumTracksWithTap(i) != 1) {
      continue;  // skip
    }

    bool space_around_is_empty =
        true;  // no other notes with a 1/8th of this row
    FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
        inout, j, i - kRowsPerBeat / 2 + 1, i + kRowsPerBeat / 2)
    if (j != i && inout.GetNumTapNonEmptyTracks(j) > 0) {
      space_around_is_empty = false;
      break;
    }

    if (!space_around_is_empty) {
      continue;  // skip
    }

    // add a note determinitsitcally
    int beat = std::lrint(NoteRowToBeat(i));
    int track_of_note = inout.GetFirstTrackWithTap(i);
    int track_to_add =
        track_of_note + (beat % 5) -
        2;  // won't be more than 2 tracks away from the existing note
    rage_clamp(track_to_add, 0, inout.GetNumTracks() - 1);
    if (track_to_add == track_of_note) {
      track_to_add++;
    }
    rage_clamp(track_to_add, 0, inout.GetNumTracks() - 1);
    if (track_to_add == track_of_note) {
      track_to_add--;
    }
    rage_clamp(track_to_add, 0, inout.GetNumTracks() - 1);

    if (inout.GetTapNote(track_to_add, i).type_ != TapNoteType_Empty &&
        inout.GetTapNote(track_to_add, i).type_ != TapNoteType_Fake) {
      track_to_add = (track_to_add + 1) % inout.GetNumTracks();
    }
    inout.SetTapNote(track_to_add, i, tap_addition_tap);
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Big(NoteData& inout, int start_index, int end_index) {
  // add 8ths between 4ths
  InsertIntelligentTaps(
      inout, BeatToNoteRow(1.0f), BeatToNoteRow(0.5f), BeatToNoteRow(1.0f),
      false, start_index, end_index);
}

void NoteDataUtil::Quick(NoteData& inout, int start_index, int end_index) {
  // add 16ths between 8ths
  InsertIntelligentTaps(
      inout, BeatToNoteRow(0.5f), BeatToNoteRow(0.25f), BeatToNoteRow(1.0f),
      false, start_index, end_index);
}

// Due to popular request by people annoyed with the "new" implementation of
// Quick, we now have this BMR-izer for your steps.  Use with caution.
void NoteDataUtil::BMRize(NoteData& inout, int start_index, int end_index) {
  Big(inout, start_index, end_index);
  Quick(inout, start_index, end_index);
}

void NoteDataUtil::Skippy(NoteData& inout, int start_index, int end_index) {
  // add 16ths between 4ths
  InsertIntelligentTaps(
      inout, BeatToNoteRow(1.0f), BeatToNoteRow(0.75f), BeatToNoteRow(1.0f),
      true, start_index, end_index);
}

void NoteDataUtil::InsertIntelligentTaps(
    NoteData& inout, int window_size_rows, int insert_offset_rows,
    int window_stride_rows, bool skippy, int start_index, int end_index) {
  ASSERT(insert_offset_rows <= window_size_rows);
  ASSERT(window_size_rows <= window_stride_rows);

  bool require_note_at_beginning_of_window = !skippy;
  bool require_note_at_end_of_window = true;

  /* Start on a multiple of fBeatInterval. */
  start_index = Quantize(start_index, window_stride_rows);

  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, i, start_index, end_index) {
    // Insert a beat in the middle of every fBeatInterval.
    if (i % window_stride_rows != 0) {
      continue;  // even beats only
    }

    int row_earlier = i;
    int row_later = i + window_size_rows;
    int row_to_add = i + insert_offset_rows;
    // following two lines have been changed because the behavior of treating
    // hold-heads as different from taps doesn't feel right, and because we need
    // to check against TAP_ADDITION with the BMRize mod.
    if (require_note_at_beginning_of_window) {
      if (inout.GetNumTapNonEmptyTracks(row_earlier) != 1 ||
          inout.GetNumTracksWithTapOrHoldHead(row_earlier) != 1) {
        continue;
      }
    }
    if (require_note_at_end_of_window) {
      if (inout.GetNumTapNonEmptyTracks(row_later) != 1 ||
          inout.GetNumTracksWithTapOrHoldHead(row_later) != 1) {
        continue;
      }
    }
    // there is a 4th and 8th note surrounding iRowBetween

    // don't insert a new note if there's already one within this interval
    bool note_in_middle = false;
    for (int t = 0; t < inout.GetNumTracks(); ++t) {
      if (inout.IsHoldNoteAtRow(t, row_earlier + 1)) {
        note_in_middle = true;
      }
    }
    FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
        inout, j, row_earlier + 1, row_later - 1)
    note_in_middle = true;
    if (note_in_middle) {
      continue;
    }

    // add a note deterministically somewhere on a track different from the two
    // surrounding notes
    int track_of_note_earlier = -1;
    bool earlier_has_non_empty_track =
        inout.GetTapFirstNonEmptyTrack(row_earlier, track_of_note_earlier);
    int track_of_note_later = -1;
    inout.GetTapFirstNonEmptyTrack(row_later, track_of_note_later);
    int track_of_note_to_add = 0;
    if (skippy &&
        track_of_note_earlier !=
            track_of_note_later &&  // Don't make skips on the same note
        earlier_has_non_empty_track) {
      track_of_note_to_add = track_of_note_earlier;
    } else if (std::abs(track_of_note_earlier - track_of_note_later) >= 2) {
      // try to choose a track between the earlier and later notes
      track_of_note_to_add =
          std::min(track_of_note_earlier, track_of_note_later) + 1;
    } else if (std::min(track_of_note_earlier, track_of_note_later) - 1 >= 0) {
      // try to choose a track just to the left
      track_of_note_to_add =
          std::min(track_of_note_earlier, track_of_note_later) - 1;
    } else if (
        std::max(track_of_note_earlier, track_of_note_later) + 1 <
        inout.GetNumTracks()) {
      // try to choose a track just to the right
      track_of_note_to_add =
          std::max(track_of_note_earlier, track_of_note_later) + 1;
    }

    inout.SetTapNote(track_of_note_to_add, row_to_add, tap_addition_tap);
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::AddMines(NoteData& inout, int start_index, int end_index) {
  // Change whole rows at a time to be tap notes.  Otherwise, it causes
  // major problems for our scoring system. -Chris

  int row_count = 0;
  int place_every_rows = 6;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    row_count++;

    // place every 6 or 7 rows
    // XXX: What is "6 or 7" derived from?  Can we calculate that in a way
    // that won't break if ROWS_PER_MEASURE changes?
    if (row_count >= place_every_rows) {
      for (int t = 0; t < inout.GetNumTracks(); t++) {
        if (inout.GetTapNote(t, r).type_ == TapNoteType_Tap) {
          inout.SetTapNote(t, r, tap_addition_mine);
        }
      }

      row_count = 0;
      if (place_every_rows == 6) {
        place_every_rows = 7;
      } else {
        place_every_rows = 6;
      }
    }
  }

  // Place mines right after hold so player must lift their foot.
  for (int track = 0; track < inout.GetNumTracks(); ++track) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(
        inout, track, r, start_index, end_index) {
      const TapNote& tn = inout.GetTapNote(track, r);
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      int mine_row = r + tn.duration_ + BeatToNoteRow(0.5f);
      if (mine_row < start_index || mine_row > end_index) {
        continue;
      }

      // Only place a mines if there's not another step nearby
      int mine_range_begin = mine_row - BeatToNoteRow(0.5f) + 1;
      int mine_range_end = mine_row + BeatToNoteRow(0.5f) - 1;
      if (!inout.IsRangeEmpty(track, mine_range_begin, mine_range_end)) {
        continue;
      }

      // Add a mine right after the hold end.
      inout.SetTapNote(track, mine_row, tap_addition_mine);

      // Convert all notes in this row to mines.
      for (int t = 0; t < inout.GetNumTracks(); t++) {
        if (inout.GetTapNote(t, mine_row).type_ == TapNoteType_Tap) {
          inout.SetTapNote(t, mine_row, tap_addition_mine);
        }
      }

      row_count = 0;
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Echo(NoteData& inout, int start_index, int end_index) {
  // add 8th note tap "echos" after all taps
  int echo_track = -1;

  const int rows_per_interval = BeatToNoteRow(0.5f);
  start_index = Quantize(start_index, rows_per_interval);

  /* Clamp iEndIndex to the last real tap note.  Otherwise, we'll keep adding
   * echos of our echos all the way up to MAX_TAP_ROW. */
  end_index = std::min(end_index, inout.GetLastRow()) + 1;

  // window is one beat wide and slides 1/2 a beat at a time
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    if (r % rows_per_interval != 0) {
      continue;  // 8th notes only
    }

    const int row_window_begin = r;
    const int row_window_end = r + rows_per_interval * 2;

    const int first_tap_in_row = inout.GetFirstTrackWithTap(row_window_begin);
    if (first_tap_in_row != -1) {
      echo_track = first_tap_in_row;
    }

    if (echo_track == -1) {
      continue;  // don't lay
    }

    // don't insert a new note if there's already a tap within this interval
    bool tap_in_middle = false;
    FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
        inout, r2, row_window_begin + 1, row_window_end - 1)
    tap_in_middle = true;
    if (tap_in_middle) {
      continue;  // don't lay
    }

    const int row_echo = r + rows_per_interval;
    {
      std::set<int> tracks;
      inout.GetTracksHeldAtRow(row_echo, tracks);

      // don't lay if holding 2 already
      if (tracks.size() >= 2) {
        continue;  // don't lay
      }

      // don't lay echos on top of a HoldNote
      if (find(tracks.begin(), tracks.end(), echo_track) != tracks.end()) {
        continue;  // don't lay
      }
    }

    inout.SetTapNote(echo_track, row_echo, tap_addition_tap);
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Planted(NoteData& inout, int start_index, int end_index) {
  ConvertTapsToHolds(inout, 1, start_index, end_index);
}
void NoteDataUtil::Floored(NoteData& inout, int start_index, int end_index) {
  ConvertTapsToHolds(inout, 2, start_index, end_index);
}
void NoteDataUtil::Twister(NoteData& inout, int start_index, int end_index) {
  ConvertTapsToHolds(inout, 3, start_index, end_index);
}
void NoteDataUtil::ConvertTapsToHolds(
    NoteData& inout, int simultaneous_holds, int start_index, int end_index) {
  // Convert all taps to freezes.
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    int track_added_this_row = 0;
    for (int t = 0; t < inout.GetNumTracks(); t++) {
      if (track_added_this_row > simultaneous_holds) {
        break;
      }

      if (inout.GetTapNote(t, r).type_ == TapNoteType_Tap) {
        // Find the ending row for this hold
        int taps_left = simultaneous_holds;

        int r2 = r + 1;
        bool add_hold = true;
        FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
            inout, next_row, r + 1, end_index) {
          r2 = next_row;

          // If there are two taps in a row on the same track,
          // don't convert the earlier one to a hold.
          if (inout.GetTapNote(t, r2).type_ != TapNoteType_Empty) {
            add_hold = false;
            break;
          }

          std::set<int> tracks_down;
          inout.GetTracksHeldAtRow(r2, tracks_down);
          inout.GetTapNonEmptyTracks(r2, tracks_down);
          taps_left -= tracks_down.size();
          if (taps_left == 0) {
            break;  // we found the ending row for this hold
          } else if (taps_left < 0) {
            add_hold = false;
            break;
          }
        }

        if (!add_hold) {
          continue;
        }

        // If the steps end in a tap, convert that tap
        // to a hold that lasts for at least one beat.
        if (r2 == r + 1) {
          r2 = r + BeatToNoteRow(1);
        }

        inout.AddHoldNote(t, r, r2, tap_original_hold_head);
        track_added_this_row++;
      }
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::Stomp(
    NoteData& inout, StepsType st, int start_index, int end_index) {
  // Make all non jumps with ample space around them into jumps.
  int track_mapping[kMaxNoteTracks];
  GetTrackMapping(st, stomp, inout.GetNumTracks(), track_mapping);

  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    if (inout.GetNumTracksWithTap(r) != 1) {
      continue;  // skip
    }

    for (int t = 0; t < inout.GetNumTracks(); t++) {
      if (inout.GetTapNote(t, r).type_ ==
          TapNoteType_Tap)  // there is a tap here
      {
        // Look to see if there is enough empty space on either side of the note
        // to turn this into a jump.
        int row_window_begin = r - BeatToNoteRow(0.5f);
        int row_window_end = r + BeatToNoteRow(0.5f);

        bool tap_in_middle = false;
        FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
            inout, r2, row_window_begin + 1, row_window_end - 1)
        if (inout.IsThereATapAtRow(r2) &&
            r2 != r)  // don't count the note we're looking around
        {
          tap_in_middle = true;
          break;
        }
        if (tap_in_middle) {
          continue;
        }

        // don't convert to jump if there's a hold here
        int num_tracks_held = inout.GetNumTracksHeldAtRow(r);
        if (num_tracks_held >= 1) {
          continue;
        }

        int opposite_track = track_mapping[t];
        inout.SetTapNote(opposite_track, r, tap_addition_tap);
      }
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::SnapToNearestNoteType(
    NoteData& inout, NoteType nt1, NoteType nt2, int start_index,
    int end_index) {
  // nt2 is optional and should be NoteType_Invalid if it is not used

  float snap_interval1 = NoteTypeToBeat(nt1);
  float snap_interval2 =
      10000;  // nothing will ever snap to this.  That's what we want!
  if (nt2 != NoteType_Invalid) {
    snap_interval2 = NoteTypeToBeat(nt2);
  }

  // iterate over all TapNotes in the interval and snap them
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
      inout, old_index, start_index, end_index) {
    int new_index1 = Quantize(old_index, BeatToNoteRow(snap_interval1));
    int new_index2 = Quantize(old_index, BeatToNoteRow(snap_interval2));

    bool new_beat1_is_closer =
        std::abs(new_index1 - old_index) < std::abs(new_index2 - old_index);
    int new_index = new_beat1_is_closer ? new_index1 : new_index2;

    for (int c = 0; c < inout.GetNumTracks(); c++) {
      TapNote tn_new = inout.GetTapNote(c, old_index);
      if (tn_new.type_ == TapNoteType_Empty) {
        continue;
      }

      inout.SetTapNote(c, old_index, tap_empty);

      if (tn_new.type_ == TapNoteType_Tap &&
          inout.IsHoldNoteAtRow(c, new_index)) {
        continue;  // HoldNotes override TapNotes
      }

      if (tn_new.type_ == TapNoteType_HoldHead) {
        /* Quantize the duration.  If the result is empty, just discard the
         * hold. */
        tn_new.duration_ =
            Quantize(tn_new.duration_, BeatToNoteRow(snap_interval1));
        if (tn_new.duration_ == 0) {
          continue;
        }

        /* We might be moving a hold note downwards, or extending its duration
         * downwards.  Make sure there isn't anything else in the new range. */
        inout.ClearRangeForTrack(
            new_index, new_index + tn_new.duration_ + 1, c);
      }

      inout.SetTapNote(c, new_index, tn_new);
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::CopyLeftToRight(NoteData& inout) {
  /* XXX
  inout.ConvertHoldNotesTo4s();
  for( int t=0; t<inout.GetNumTracks()/2; t++ )
  {
          FOREACH_NONEMPTY_ROW_IN_TRACK( inout, t, r )
          {
                  int iTrackEarlier = t;
                  int iTrackLater = inout.GetNumTracks()-1-t;

                  const TapNote &tnEarlier = inout.GetTapNote(iTrackEarlier, r);
                  inout.SetTapNote(iTrackLater, r, tnEarlier);
          }
  }
  inout.Convert4sToHoldNotes();
*/
}

void NoteDataUtil::CopyRightToLeft(NoteData& inout) {
  /* XXX
  inout.ConvertHoldNotesTo4s();
  for( int t=0; t<inout.GetNumTracks()/2; t++ )
  {
          FOREACH_NONEMPTY_ROW_IN_TRACK( inout, t, r )
          {
                  int iTrackEarlier = t;
                  int iTrackLater = inout.GetNumTracks()-1-t;

                  TapNote tnLater = inout.GetTapNote(iTrackLater, r);
                  inout.SetTapNote(iTrackEarlier, r, tnLater);
          }
  }
  inout.Convert4sToHoldNotes();
*/
}

void NoteDataUtil::ClearLeft(NoteData& inout) {
  for (int t = 0; t < inout.GetNumTracks() / 2; t++) {
    inout.ClearRangeForTrack(0, kMaxNoteRow, t);
  }
}

void NoteDataUtil::ClearRight(NoteData& inout) {
  for (int t = (inout.GetNumTracks() + 1) / 2; t < inout.GetNumTracks(); t++) {
    inout.ClearRangeForTrack(0, kMaxNoteRow, t);
  }
}

void NoteDataUtil::CollapseToOne(NoteData& inout) {
  FOREACH_NONEMPTY_ROW_ALL_TRACKS(inout, r)
  for (int t = 1; t < inout.GetNumTracks(); t++) {
    NoteData::iterator iter = inout.FindTapNote(t, r);
    if (iter == inout.end(t)) {
      continue;
    }
    inout.SetTapNote(0, r, iter->second);
    inout.RemoveTapNote(t, iter);
  }
}

void NoteDataUtil::CollapseLeft(NoteData& inout) {
  FOREACH_NONEMPTY_ROW_ALL_TRACKS(inout, r) {
    int num_tracks_filled = 0;
    for (int t = 0; t < inout.GetNumTracks(); t++) {
      if (inout.GetTapNote(t, r).type_ != TapNoteType_Empty) {
        TapNote tn = inout.GetTapNote(t, r);
        inout.SetTapNote(t, r, tap_empty);
        if (num_tracks_filled < inout.GetNumTracks()) {
          inout.SetTapNote(num_tracks_filled, r, tn);
          ++num_tracks_filled;
        }
      }
    }
  }
}

void NoteDataUtil::ShiftTracks(NoteData& inout, int shift_by) {
  int original_track_to_take_from[kMaxNoteTracks];
  for (int i = 0; i < inout.GetNumTracks(); ++i) {
    int from = i - shift_by;
    wrap(from, inout.GetNumTracks());
    original_track_to_take_from[i] = from;
  }

  NoteData orig(inout);
  inout.LoadTransformed(orig, orig.GetNumTracks(), original_track_to_take_from);
}

void NoteDataUtil::ShiftLeft(NoteData& inout) { ShiftTracks(inout, -1); }

void NoteDataUtil::ShiftRight(NoteData& inout) { ShiftTracks(inout, +1); }

void NoteDataUtil::SwapUpDown(NoteData& inout, StepsType st) {
  int take_from[kMaxNoteTracks];
  GetTrackMapping(
      st, NoteDataUtil::swap_up_down, inout.GetNumTracks(), take_from);
  NoteData temp_nd;
  temp_nd.LoadTransformed(inout, inout.GetNumTracks(), take_from);
  inout.CopyAll(temp_nd);
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::ArbitraryRemap(NoteData& inout, int* mapping) {
  NoteData temp_nd;
  temp_nd.LoadTransformed(inout, inout.GetNumTracks(), mapping);
  inout.CopyAll(temp_nd);
  inout.RevalidateATIs(std::vector<int>(), false);
}

struct ValidRow {
  StepsType st_;
  bool valid_mask_[kMaxNoteTracks];
};
#define T true
#define f false
const ValidRow kValidRows[] = {
    {StepsType_dance_double, {T, T, T, T, f, f, f, f}},
    {StepsType_dance_double, {f, T, T, T, T, f, f, f}},
    {StepsType_dance_double, {f, f, f, T, T, T, T, f}},
    {StepsType_dance_double, {f, f, f, f, T, T, T, T}},
    {StepsType_pump_double, {T, T, T, T, T, f, f, f, f, f}},
    {StepsType_pump_double, {f, f, T, T, T, T, T, T, f, f}},
    {StepsType_pump_double, {f, f, f, f, f, T, T, T, T, T}},
};
#undef T
#undef f

void NoteDataUtil::RemoveStretch(
    NoteData& inout, StepsType st, int start_index, int end_index) {
  std::vector<const ValidRow*> valid_rows_to_check;
  for (unsigned i = 0; i < ARRAYLEN(kValidRows); i++) {
    if (kValidRows[i].st_ == st) {
      valid_rows_to_check.push_back(&kValidRows[i]);
    }
  }

  // bail early if there's nothing to validate against
  if (valid_rows_to_check.empty()) {
    return;
  }

  // each row must pass at least one valid mask
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(inout, r, start_index, end_index) {
    // only check rows with jumps
    if (inout.GetNumTapNonEmptyTracks(r) < 2) {
      continue;
    }

    bool passed_one_mask = false;
    for (unsigned i = 0; i < valid_rows_to_check.size(); i++) {
      const ValidRow& vr = *valid_rows_to_check[i];
      if (NoteDataUtil::RowPassesValidMask(inout, r, vr.valid_mask_)) {
        passed_one_mask = true;
        break;
      }
    }

    if (!passed_one_mask) {
      RemoveAllButOneTap(inout, r);
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

bool NoteDataUtil::RowPassesValidMask(
    NoteData& inout, int row, const bool valid_mask[]) {
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    if (!valid_mask[t] && inout.GetTapNote(t, row).type_ != TapNoteType_Empty) {
      return false;
    }
  }

  return true;
}

void NoteDataUtil::ConvertAdditionsToRegular(NoteData& inout) {
  for (int t = 0; t < inout.GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK(inout, t, r)
    if (inout.GetTapNote(t, r).source_ == TapNoteSource_Addition) {
      TapNote tn = inout.GetTapNote(t, r);
      tn.source_ = TapNoteSource_Original;
      inout.SetTapNote(t, r, tn);
    }
  }
  inout.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::TransformNoteData(
    NoteData& nd, const TimingData& timing_data, const AttackArray& aa,
    StepsType st, Song* song) {
  for (const Attack& a : aa) {
    PlayerOptions po;
    po.FromString(a.modifiers_);
    if (po.ContainsTransformOrTurn()) {
      float start_beat, end_beat;
      a.GetAttackBeats(song, start_beat, end_beat);

      NoteDataUtil::TransformNoteData(
          nd, timing_data, po, st, BeatToNoteRow(start_beat),
          BeatToNoteRow(end_beat));
    }
  }
}

void NoteDataUtil::TransformNoteData(
    NoteData& nd, const TimingData& timing_data, const PlayerOptions& po,
    StepsType st, int start_index, int end_index) {
  // Apply remove transforms before others so that we don't go removing
  // notes we just inserted.  Apply TRANSFORM_NOROLLS before TRANSFORM_NOHOLDS,
  // since NOROLLS creates holds.
  if (po.transforms_[PlayerOptions::TRANSFORM_LITTLE]) {
    NoteDataUtil::Little(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOROLLS]) {
    NoteDataUtil::ChangeRollsToHolds(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOHOLDS]) {
    NoteDataUtil::RemoveHoldNotes(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOMINES]) {
    NoteDataUtil::RemoveMines(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOJUMPS]) {
    NoteDataUtil::RemoveJumps(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOLIFTS]) {
    NoteDataUtil::RemoveLifts(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOFAKES]) {
    NoteDataUtil::RemoveFakes(nd, timing_data, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOHANDS]) {
    NoteDataUtil::RemoveHands(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOQUADS]) {
    NoteDataUtil::RemoveQuads(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_NOSTRETCH]) {
    NoteDataUtil::RemoveStretch(nd, st, start_index, end_index);
  }

  // Apply inserts.
  if (po.transforms_[PlayerOptions::TRANSFORM_BIG]) {
    NoteDataUtil::Big(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_QUICK]) {
    NoteDataUtil::Quick(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_BMRIZE]) {
    NoteDataUtil::BMRize(nd, start_index, end_index);
  }

  // Skippy will still add taps to places that the other
  // AddIntelligentTaps above won't.
  if (po.transforms_[PlayerOptions::TRANSFORM_SKIPPY]) {
    NoteDataUtil::Skippy(nd, start_index, end_index);
  }

  // These aren't affects by the above inserts very much.
  if (po.transforms_[PlayerOptions::TRANSFORM_MINES]) {
    NoteDataUtil::AddMines(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_ECHO]) {
    NoteDataUtil::Echo(nd, start_index, end_index);
  }

  // Jump-adding transforms aren't much affected by additional taps.
  if (po.transforms_[PlayerOptions::TRANSFORM_WIDE]) {
    NoteDataUtil::Wide(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_STOMP]) {
    NoteDataUtil::Stomp(nd, st, start_index, end_index);
  }

  // Transforms that add holds go last.  If they went first, most tap-adding
  // transforms wouldn't do anything because tap-adding transforms skip areas
  // where there's a hold.
  if (po.transforms_[PlayerOptions::TRANSFORM_PLANTED]) {
    NoteDataUtil::Planted(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_FLOORED]) {
    NoteDataUtil::Floored(nd, start_index, end_index);
  }
  if (po.transforms_[PlayerOptions::TRANSFORM_TWISTER]) {
    NoteDataUtil::Twister(nd, start_index, end_index);
  }

  // Do this here to turn any added holds into rolls
  if (po.transforms_[PlayerOptions::TRANSFORM_HOLDROLLS]) {
    NoteDataUtil::ChangeHoldsToRolls(nd, start_index, end_index);
  }

  // Apply turns and shuffles last so that they affect inserts.
  if (po.turns_[PlayerOptions::TURN_MIRROR]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::mirror, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_LRMIRROR]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::lrmirror, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_UDMIRROR]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::udmirror, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_BACKWARDS]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::backwards, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_LEFT]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::left, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_RIGHT]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::right, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_SHUFFLE]) {
    NoteDataUtil::Turn(nd, st, NoteDataUtil::shuffle, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_SOFT_SHUFFLE]) {
    NoteDataUtil::Turn(
        nd, st, NoteDataUtil::soft_shuffle, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_SUPER_SHUFFLE]) {
    NoteDataUtil::Turn(
        nd, st, NoteDataUtil::super_shuffle, start_index, end_index);
  }
  if (po.turns_[PlayerOptions::TURN_HYPER_SHUFFLE]) {
    NoteDataUtil::Turn(
        nd, st, NoteDataUtil::hyper_shuffle, start_index, end_index);
  }

  nd.RevalidateATIs(std::vector<int>(), false);
}

/* XXX: move this to an appropriate place, same place as NoteRowToBeat perhaps?
 */
static inline int GetScaledRow(
    float scale, int start_index, int end_index, int row) {
  if (row < start_index) {
    return row;
  } else if (row > end_index) {
    return row + std::lrint((end_index - start_index) * (scale - 1));
  } else {
    return std::lrint((row - start_index) * scale) + start_index;
  }
}

void NoteDataUtil::ScaleRegion(
    NoteData& nd, float scale, int start_index, int end_index) {
  ASSERT(scale > 0);
  ASSERT(start_index < end_index);
  ASSERT(start_index >= 0);

  NoteData out;
  out.SetNumTracks(nd.GetNumTracks());

  for (int t = 0; t < nd.GetNumTracks(); t++) {
    for (NoteData::const_iterator iter = nd.begin(t); iter != nd.end(t);
         ++iter) {
      TapNote tn = iter->second;
      int new_row = GetScaledRow(scale, start_index, end_index, iter->first);
      int new_duration =
          GetScaledRow(
              scale, start_index, end_index, iter->first + tn.duration_) -
          new_row;
      tn.duration_ = new_duration;
      out.SetTapNote(t, new_row, tn);
    }
  }

  nd.swap(out);
  nd.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::InsertRows(NoteData& nd, int start_index, int rows_to_add) {
  ASSERT(rows_to_add >= 0);

  NoteData temp;
  temp.SetNumTracks(nd.GetNumTracks());
  temp.CopyRange(nd, start_index, kMaxNoteRow);
  nd.ClearRange(start_index, kMaxNoteRow);
  nd.CopyRange(temp, 0, kMaxNoteRow, start_index + rows_to_add);
  nd.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::DeleteRows(
    NoteData& nd, int start_index, int rows_to_delete) {
  ASSERT(rows_to_delete >= 0);

  NoteData temp;
  temp.SetNumTracks(nd.GetNumTracks());
  temp.CopyRange(nd, start_index + rows_to_delete, kMaxNoteRow);
  nd.ClearRange(start_index, kMaxNoteRow);
  nd.CopyRange(temp, 0, kMaxNoteRow, start_index);
  nd.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveAllTapsOfType(
    NoteData& in_out, TapNoteType type_to_remove) {
  /* Be very careful when deleting the tap notes. Erasing elements from maps
   * using iterators invalidates only the iterator that is being erased. To that
   * end, increment the iterator before deleting the elment of the map.
   */
  for (int t = 0; t < in_out.GetNumTracks(); t++) {
    for (NoteData::iterator iter = in_out.begin(t); iter != in_out.end(t);) {
      if (iter->second.type_ == type_to_remove) {
        iter = in_out.RemoveTapNote(t, iter);
      } else {
        ++iter;
      }
    }
  }
  in_out.RevalidateATIs(std::vector<int>(), false);
}

void NoteDataUtil::RemoveAllTapsExceptForType(
    NoteData& in_out, TapNoteType type_to_keep) {
  /* Same as in RemoveAllTapsOfType(). */
  for (int t = 0; t < in_out.GetNumTracks(); t++) {
    for (NoteData::iterator iter = in_out.begin(t); iter != in_out.end(t);) {
      if (iter->second.type_ != type_to_keep) {
        iter = in_out.RemoveTapNote(t, iter);
      } else {
        ++iter;
      }
    }
  }
  in_out.RevalidateATIs(std::vector<int>(), false);
}

int NoteDataUtil::GetMaxNonEmptyTrack(const NoteData& in) {
  for (int t = in.GetNumTracks() - 1; t >= 0; t--) {
    if (!in.IsTrackEmpty(t)) {
      return t;
    }
  }
  return -1;
}

/* Find the next row that either starts a TapNote, or ends a previous one. */
bool NoteDataUtil::GetNextEditorPosition(const NoteData& in, int& row_in_out) {
  int original_row = row_in_out;
  bool any_have_next_note = in.GetNextTapNoteRowForAllTracks(row_in_out);

  int closest_next_row = row_in_out;
  if (!any_have_next_note) {
    closest_next_row = kMaxNoteRow;
  }

  for (int t = 0; t < in.GetNumTracks(); t++) {
    int head_row;
    if (!in.IsHoldHeadOrBodyAtRow(t, original_row, &head_row)) {
      continue;
    }

    const TapNote& tn = in.GetTapNote(t, head_row);
    int end_row = head_row + tn.duration_;
    if (end_row == original_row) {
      continue;
    }

    any_have_next_note = true;
    ASSERT(end_row < kMaxNoteRow);
    closest_next_row = std::min(closest_next_row, end_row);
  }

  if (!any_have_next_note) {
    return false;
  }

  row_in_out = closest_next_row;
  return true;
}

bool NoteDataUtil::GetPrevEditorPosition(const NoteData& in, int& row_in_out) {
  int original_row = row_in_out;
  bool any_have_prev_note = in.GetPrevTapNoteRowForAllTracks(row_in_out);

  int closest_prev_row = row_in_out;
  for (int t = 0; t < in.GetNumTracks(); t++) {
    int head_row = original_row;
    if (!in.GetPrevTapNoteRowForTrack(t, head_row)) {
      continue;
    }

    const TapNote& tn = in.GetTapNote(t, head_row);
    if (tn.type_ != TapNoteType_HoldHead) {
      continue;
    }

    int end_row = head_row + tn.duration_;
    if (end_row >= original_row) {
      continue;
    }

    any_have_prev_note = true;
    ASSERT(end_row < kMaxNoteRow);
    closest_prev_row = std::max(closest_prev_row, end_row);
  }

  if (!any_have_prev_note) {
    return false;
  }

  row_in_out = closest_prev_row;
  return true;
}

unsigned int NoteDataUtil::GetTotalHoldTicks(
    NoteData* nd, const TimingData* td) {
  unsigned int ret = 0;
  // Last row must be included. -- Matt
  int end = nd->GetLastRow() + 1;
  std::vector<TimingSegment*> segments =
      td->GetTimingSegments(SEGMENT_TICKCOUNT);
  // We start with the LAST TimingSegment and work our way backwards.
  // This way we can continually update end instead of having to lookup when
  // the next segment starts.
  for (int i = segments.size() - 1; i >= 0; i--) {
    TickcountSegment* ts = (TickcountSegment*)segments[i];
    if (ts->GetTicks() > 0) {
      // Jump to each point where holds would tick and add the number of holds
      // there to ret.
      for (int j = ts->GetRow(); j < end; j += kRowsPerBeat / ts->GetTicks()) {
        // 1 tick per row.
        if (nd->GetNumTracksHeldAtRow(j) > 0) {
          ret++;
        }
      }
    }
    end = ts->GetRow();
  }
  return ret;
}

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
