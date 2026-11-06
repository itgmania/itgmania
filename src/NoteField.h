#ifndef NOTE_FIELD_H
#define NOTE_FIELD_H

#include <map>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "BitmapText.h"
#include "GameConstantsAndTypes.h"
#include "GhostArrowRow.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "NoteDisplay.h"
#include "PlayerNumber.h"
#include "Quad.h"
#include "RageTypes.h"
#include "ReceptorArrowRow.h"
#include "Sprite.h"

struct Attack;
class NoteData;
/** @brief An Actor that renders NoteData. */
class NoteField : public ActorFrame {
 public:
  NoteField();
  ~NoteField();
  virtual void Update(float fDeltaTime);
  virtual void DrawPrimitives();
  void CalcPixelsBeforeAndAfterTargets();
  void DrawBoardPrimitive();

  virtual void Init(
      const PlayerState* pPlayerState, float fYReverseOffsetPixels,
      bool use_states_zoom = true);
  virtual void Load(
      const NoteData* pNoteData, int iDrawDistanceAfterTargetsPixels,
      int iDrawDistanceBeforeTargetsPixels);
  virtual void Unload();

  void ensure_note_displays_have_skin();
  void InitColumnRenderers();

  virtual void HandleMessage(const Message& msg);

  // This is done automatically by Init(), but can be re-called explicitly if
  // the note skin changes.
  void CacheAllUsedNoteSkins();
  void FadeToFail();

  void Step(int col, TapNoteScore score, bool from_lua = false);
  void SetPressed(int col, bool from_lua = false);
  void DidTapNote(
      int col, TapNoteScore score, bool bright, bool from_lua = false);
  void DidHoldNote(
      int col, HoldNoteScore score, bool bright, bool from_lua = false);

  virtual void PushSelf(lua_State* L);

  // Allows the theme to modify the parameters to Step, SetPressed,
  // DidTapNote, and DidHoldNote before they pass on to the ghost arrows or
  // receptors. -Kyz
  LuaReference step_callback_;
  LuaReference set_pressed_callback_;
  LuaReference did_tap_note_callback_;
  LuaReference did_hold_note_callback_;

  const PlayerState* GetPlayerState() const { return player_state_; }

  int begin_marker_, end_marker_;  // only used with MODE_EDIT

  // m_ColumnRenderers belongs in the protected section, but it's here in
  // public so that the Lua API can access it. -Kyz
  std::vector<NoteColumnRenderer> column_renderers_;

  void SetBeatBars(bool active);
  bool GetBeatBars();
  void SetBeatBarsAlpha(
      float measure, float fourth, float eighth, float sixteenth);

  /** @brief Decode the song's audio into a peak buffer for waveform display.
   * Safe to call with an empty path, which just clears any loaded data. */
  void LoadWaveform(const std::string& sMusicPath);
  void UnloadWaveform();
  /** @brief Only show the waveform while actively editing steps, not while
   * playtesting/recording. */
  void SetShowWaveform(bool active) { show_waveform_ = active; }
  bool GetShowWaveform() const { return show_waveform_; }

 protected:
  void CacheNoteSkin(const std::string& sNoteSkin);
  void UncacheNoteSkin(const std::string& sNoteSkin);

  bool IsOnScreen(
      float fBeat, int iCol, int iDrawDistanceAfterTargetsPixels,
      int iDrawDistanceBeforeTargetsPixels) const;

  void DrawBoard(
      int iDrawDistanceAfterTargetsPixels,
      int iDrawDistanceBeforeTargetsPixels);

  enum BeatBarType { measure, beat, half_beat, quarter_beat };
  void DrawBeatBar(const float fBeat, BeatBarType type, int iMeasureIndex);
  void DrawMarkerBar(int fBeat);
  void DrawAreaHighlight(int iStartBeat, int iEndBeat);
  void DrawWaveform();
  void set_text_measure_number_for_draw(
      const float beat, const float side_sign, float x_offset,
      const float horiz_align, const RageColor& color, const RageColor& glow);
  void draw_timing_segment_text(
      const std::string& text, const float beat, const float side_sign,
      float x_offset, const float horiz_align, const RageColor& color,
      const RageColor& glow);
  void DrawAttackText(
      const float beat, const Attack& attack, const RageColor& glow);
  void DrawBGChangeText(
      const float beat, const std::string new_bg_name, const RageColor& glow);
  float GetWidth() const;

  const NoteData* note_data_;

  const PlayerState* player_state_;
  int draw_distance_after_targets_pixels_;   // this should be a negative number
  int draw_distance_before_targets_pixels_;  // this should be a positive number
  float y_reverse_offset_pixels_;

  // This exists so that the board can be drawn underneath combo/judge. -Kyz
  bool drawing_board_primitive_;

  // color arrows
  struct NoteDisplayCols {
    NoteDisplay* display_;
    ReceptorArrowRow receptor_arrow_row_;
    GhostArrowRow ghost_arrow_row_;
    NoteDisplayCols(int iNumCols) { display_ = new NoteDisplay[iNumCols]; }
    ~NoteDisplayCols() { delete[] display_; }
  };

  NoteFieldRenderArgs field_render_args_;

  /* All loaded note displays, mapped by their name. */
  std::map<std::string, NoteDisplayCols*> note_displays_;
  NoteDisplayCols* cur_display_;
  NoteDisplayCols* displays_[NUM_PlayerNumber];

  // decorations, mostly used in MODE_EDIT
  AutoActor spr_board_;
  float board_offset_pixels_;
  float current_beat_last_update_;        // -1 on first update
  float y_pos_current_beat_last_update_;  // -1 on first update

  Sprite spr_beat_bars_;  // 4 frames: Measure, 4th, 8th, 16th
  BitmapText text_measure_number_;
  Quad rect_marker_bar_;
  Quad rect_area_highlight_;

  bool show_beat_bars_;
  float bar_measure_alpha_;
  float bar4th_alpha_;
  float bar8th_alpha_;
  float bar16th_alpha_;

  // Song waveform, shown behind the beat bars while actively editing steps.
  bool show_waveform_;
  bool waveform_is_stereo_;
  int waveform_sample_rate_;
  std::vector<float> waveform_samples_l_;
  std::vector<float> waveform_samples_r_;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2004
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
