#ifndef PLAYER_H
#define PLAYER_H

#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "AttackDisplay.h"
#include "AutoActor.h"
#include "GameConstantsAndTypes.h"
#include "HoldJudgment.h"
#include "NoteData.h"
#include "NoteDataWithScoring.h"
#include "RageSound.h"
#include "ScreenMessage.h"
#include "ThemeMetric.h"
#include "TimingData.h"

class ScoreDisplay;
class LifeMeter;
class CombinedLifeMeter;
class ScoreKeeper;
class RageTimer;
class NoteField;
class PlayerStageStats;
class JudgedRows;

// todo: replace these with a Message and MESSAGEMAN? -aj
AutoScreenMessage(SM_100Combo);
AutoScreenMessage(SM_200Combo);
AutoScreenMessage(SM_300Combo);
AutoScreenMessage(SM_400Combo);
AutoScreenMessage(SM_500Combo);
AutoScreenMessage(SM_600Combo);
AutoScreenMessage(SM_700Combo);
AutoScreenMessage(SM_800Combo);
AutoScreenMessage(SM_900Combo);
AutoScreenMessage(SM_1000Combo);
AutoScreenMessage(SM_ComboStopped);
AutoScreenMessage(SM_ComboContinuing);

/** @brief Accepts input, knocks down TapNotes that were stepped on, and keeps
 * score for the player. */
class Player : public ActorFrame {
 public:
  // The passed in NoteData isn't touched until Load() is called.
  Player(NoteData& nd, bool bVisibleParts = true);
  ~Player();

  virtual void Update(float fDeltaTime);
  virtual void DrawPrimitives();
  // PushPlayerMatrix and PopPlayerMatrix are separate functions because
  // they need to be used twice so that the notefield board can rendered
  // underneath the combo and judgment.  They're not embedded in
  // PlayerMatrixPusher so that some nutjob can later decide to expose them
  // to lua. -Kyz
  void PushPlayerMatrix(float x, float skew, float center_y);
  void PopPlayerMatrix();

  // This exists so that the board can be drawn underneath combo/judge. -Kyz
  void DrawNoteFieldBoard();

  // Here's a fun construct for people that haven't seen it before:
  // This object does some task when it's created, then cleans up when it's
  // destroyed.  That way, you stick it inside a block, and can't forget the
  // cleanup. -Kyz
  struct PlayerNoteFieldPositioner {
    PlayerNoteFieldPositioner(
        Player* p, float x, float tilt, float skew, float mini, float center_y,
        bool reverse);
    ~PlayerNoteFieldPositioner();
    Player* player_;
    float original_y_;
    float y_offset_;
  };

  struct TrackRowTapNote {
    int track_;
    int row_;
    TapNote* tn_;
  };
  void UpdateHoldNotes(
      int iSongRow, float fDeltaTime, std::vector<TrackRowTapNote>& vTN);

  void Init(
      const std::string& sType, PlayerState* pPlayerState,
      PlayerStageStats* pPlayerStageStats, LifeMeter* pLM,
      CombinedLifeMeter* pCombinedLM, ScoreDisplay* pScoreDisplay,
      ScoreDisplay* pSecondaryScoreDisplay, ScoreKeeper* pPrimaryScoreKeeper,
      ScoreKeeper* pSecondaryScoreKeeper);
  void Load();
  void CrossedRows(int iLastRowCrossed, const RageTimer& now);
  bool IsOniDead() const;

  /**
   * @brief Retrieve the Player's TimingData.
   *
   * This is primarily for a lua hook.
   * @return the TimingData in question. */
  TimingData GetPlayerTimingData() const { return *(this->timing_); }

  void DoTapScoreNone();

  void Step(int col, int row, const RageTimer& tm, bool bHeld, bool bRelease);

  void FadeToFail();
  void CacheAllUsedNoteSkins();
  void ApplyWaitingTransforms();
  void SetPaused(bool bPaused) { paused_ = bPaused; }

  static float GetMaxStepDistanceSeconds();
  static float GetWindowSeconds(TimingWindow tw);
  const NoteData& GetNoteData() const { return note_data_; }
  bool HasVisibleParts() const { return note_field_ != nullptr; }

  void SetActorWithJudgmentPosition(Actor* pActor) {
    actor_with_judgment_position_ = pActor;
  }
  void SetActorWithComboPosition(Actor* pActor) {
    actor_with_combo_position_ = pActor;
  }

  // OITG bug:  Actor::SetZoom only sets X and Y.  When mini is applied to
  // the notefield with SetZoom, it does not affect the range of bumpy.
  // m_oitg_zoom_mode provides compatibility with that bug.  Only used in
  // defective mode. -Kyz
  bool oitg_zoom_mode_;

  // Lua
  virtual void PushSelf(lua_State* L);

  PlayerState* GetPlayerState() { return this->player_state_; }
  void ChangeLife(float delta);
  void SetLife(float value);
  bool inside_lua_set_life_;

 protected:
  void UpdateTapNotesMissedOlderThan(float fMissIfOlderThanThisBeat);
  void UpdateJudgedRows();
  void FlashGhostRow(int iRow);
  void HandleTapRowScore(unsigned row);
  void HandleHoldScore(const TapNote& tn);
  void HandleHoldCheckpoint(
      int iRow, int iNumHoldsHeldThisRow, int iNumHoldsMissedThisRow,
      const std::vector<int>& viColsWithHold);
  void DrawTapJudgments();
  void DrawHoldJudgments();
  void SendComboMessages(unsigned int iOldCombo, unsigned int iOldMissCombo);
  void PlayKeysound(const TapNote& tn, TapNoteScore score);

  void SetMineJudgment(TapNoteScore tns, int iTrack);
  void SetJudgment(int iRow, int iFirstTrack, const TapNote& tn) {
    SetJudgment(
        iRow, iFirstTrack, tn, tn.result_.tns_, tn.result_.tap_note_offset_);
  }
  void SetJudgment(
      int iRow, int iFirstTrack, const TapNote& tn, TapNoteScore tns,
      float fTapNoteOffset);  // -1 if no track as in TNS_Miss
  void SetHoldJudgment(TapNote& tn, int iTrack);
  void SetCombo(unsigned int iCombo, unsigned int iMisses);
  void IncrementComboOrMissCombo(bool bComboOrMissCombo);
  void IncrementCombo() { IncrementComboOrMissCombo(true); };
  void IncrementMissCombo() { IncrementComboOrMissCombo(false); };

  void ChangeLife(TapNoteScore tns);
  void ChangeLife(HoldNoteScore hns, TapNoteScore tns);
  void ChangeLifeRecord();

  int GetClosestNoteDirectional(
      int col, int iStartRow, int iMaxRowsAhead, bool bAllowGraded,
      bool bForward) const;
  int GetClosestNote(
      int col, int iNoteRow, int iMaxRowsAhead, int iMaxRowsBehind,
      bool bAllowGraded) const;
  std::string ApplyRandomAttack();

  inline void HideNote(int col, int row) {
    NoteData::iterator iter = note_data_.FindTapNote(col, row);
    if (iter != note_data_.end(col)) {
      iter->second.result_.hidden_ = true;
    }
  }

  bool loaded_;

  /** @brief The player's present state. */
  PlayerState* player_state_;
  /** @brief The player's present stage stats. */
  PlayerStageStats* player_stage_stats_;
  TimingData* timing_;
  float note_field_height_;

  bool paused_;
  bool delay_;

  NoteData& note_data_;
  NoteField* note_field_;

  std::vector<HoldJudgment*> hold_judgment_;

  AutoActor spr_judgment_;
  AutoActor spr_combo_;
  Actor* actor_with_judgment_position_;
  Actor* actor_with_combo_position_;

  AttackDisplay* attack_display_;

  LifeMeter* life_meter_;
  CombinedLifeMeter* combined_life_meter_;
  ScoreDisplay* score_display_;
  ScoreDisplay* secondary_score_display_;
  ScoreKeeper* primary_score_keeper_;
  ScoreKeeper* secondary_score_keeper_;

  int first_uncrossed_row_;  // used by hold checkpoints logic
  NoteData::all_tracks_iterator* iter_needs_tap_judging_;
  NoteData::all_tracks_iterator* iter_needs_hold_judging_;
  NoteData::all_tracks_iterator* iter_uncrossed_rows_;
  NoteData::all_tracks_iterator* iter_unjudged_rows_;
  NoteData::all_tracks_iterator* iter_unjudged_mine_rows_;
  unsigned int last_seen_combo_;
  bool seen_combo_yet_;
  JudgedRows* judged_rows_;

  RageSound sound_mine_;
  RageSound sound_attack_launch_;
  RageSound sound_attack_ending_;

  float active_random_attack_start_;

  std::vector<RageSound> keysounds_;

  ThemeMetric<float> GRAY_ARROWS_Y_STANDARD;
  ThemeMetric<float> GRAY_ARROWS_Y_REVERSE;
  ThemeMetric2D<float> ATTACK_DISPLAY_X;
  ThemeMetric<float> HOLD_JUDGMENT_Y_STANDARD;
  ThemeMetric<float> HOLD_JUDGMENT_Y_REVERSE;
  ThemeMetric<int> BRIGHT_GHOST_COMBO_THRESHOLD;
  ThemeMetric<bool> TAP_JUDGMENTS_UNDER_FIELD;
  ThemeMetric<bool> HOLD_JUDGMENTS_UNDER_FIELD;
  ThemeMetric<bool> COMBO_UNDER_FIELD;
  ThemeMetric<int> DRAW_DISTANCE_AFTER_TARGET_PIXELS;
  ThemeMetric<int> DRAW_DISTANCE_BEFORE_TARGET_PIXELS;

#define NUM_REVERSE 2
#define NUM_CENTERED 2
  TweenState judgment_[NUM_REVERSE][NUM_CENTERED];
  TweenState combo_[NUM_REVERSE][NUM_CENTERED];

  bool send_judgment_and_combo_messages_;
  bool tick_holds_;
  // This exists so that the board can be drawn underneath combo/judge. -Kyz
  bool drawing_notefield_board_;
};

class PlayerPlus {
  Player* player_;
  NoteData note_data_;

 public:
  PlayerPlus() { player_ = new Player(note_data_); }
  ~PlayerPlus() { delete player_; }
  void Load(const NoteData& nd) {
    note_data_ = nd;
    player_->Load();
  }
  Player* operator->() { return player_; }
  const Player* operator->() const { return player_; }
  operator Player*() { return player_; }
  operator const Player*() const { return player_; }
};

#endif

/*
 * (c) 2001-2006 Chris Danford, Steve Checkoway
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
