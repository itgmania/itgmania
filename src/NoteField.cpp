#include "NoteField.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ArrowEffects.h"
#include "Attack.h"
#include "BackgroundUtil.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteDisplay.h"
#include "NoteSkinManager.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSoundReader_FileReader.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "Song.h"
#include "Sprite.h"
#include "StdString.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

float FindFirstDisplayedBeat(
    const PlayerState* pPlayerState, int iDrawDistanceAfterTargetsPixels);
float FindLastDisplayedBeat(
    const PlayerState* pPlayerState, int iDrawDistanceBeforeTargetsPixels);

static ThemeMetric<bool> SHOW_BOARD("NoteField", "ShowBoard");
static ThemeMetric<bool> SHOW_BEAT_BARS("NoteField", "ShowBeatBars");
static ThemeMetric<float> FADE_BEFORE_TARGETS_PERCENT(
    "NoteField", "FadeBeforeTargetsPercent");
static ThemeMetric<float> BAR_MEASURE_ALPHA("NoteField", "BarMeasureAlpha");
static ThemeMetric<float> BAR_4TH_ALPHA("NoteField", "Bar4thAlpha");
static ThemeMetric<float> BAR_8TH_ALPHA("NoteField", "Bar8thAlpha");
static ThemeMetric<float> BAR_16TH_ALPHA("NoteField", "Bar16thAlpha");
static ThemeMetric<float> FADE_FAIL_TIME("NoteField", "FadeFailTime");
static ThemeMetric<RageColor> WAVEFORM_COLOR("NoteField", "WaveformColor");
static ThemeMetric<bool> SHOW_WAVEFORM("NoteField", "ShowWaveform");
static ThemeMetric<float> WAVEFORM_WIDTH_PERCENT(
    "NoteField", "WaveformWidthPercent");
static ThemeMetric<float> WAVEFORM_GAIN("NoteField", "WaveformGain");
static ThemeMetric<float> WAVEFORM_POINTS_PER_PIXEL(
    "NoteField", "WaveformPointsPerPixel");

NoteField::NoteField() {
  note_data_ = nullptr;
  cur_display_ = nullptr;
  drawing_board_primitive_ = false;
  show_beat_bars_ = SHOW_BEAT_BARS;
  bar_measure_alpha_ = BAR_MEASURE_ALPHA;
  bar4th_alpha_ = BAR_4TH_ALPHA;
  bar8th_alpha_ = BAR_8TH_ALPHA;
  bar16th_alpha_ = BAR_16TH_ALPHA;
  show_waveform_ = SHOW_WAVEFORM;
  waveform_is_stereo_ = false;
  waveform_sample_rate_ = 0;

  text_measure_number_.LoadFromFont(
      THEME->GetPathF("NoteField", "MeasureNumber"));
  text_measure_number_.SetZoom(1.0f);
  text_measure_number_.SetShadowLength(2);
  text_measure_number_.SetWrapWidthPixels(300);

  rect_marker_bar_.SetEffectDiffuseShift(
      2, RageColor(1, 1, 1, 0.5f), RageColor(0.5f, 0.5f, 0.5f, 0.5f));

  spr_board_.Load(THEME->GetPathG("NoteField", "board"));
  spr_board_->SetName("Board");
  spr_board_->PlayCommand("On");
  this->AddChild(spr_board_);

  board_offset_pixels_ = 0;
  current_beat_last_update_ = -1;
  y_pos_current_beat_last_update_ = -1;
  this->SubscribeToMessage(Message_CurrentSongChanged);

  spr_beat_bars_.Load(THEME->GetPathG("NoteField", "bars"));
  spr_beat_bars_.StopAnimating();

  // I decided to do it this way because I don't want to dig through
  // ScreenEdit to change all the places it touches the markers. -Kyz
  field_render_args_.selection_begin_marker_ = &begin_marker_;
  field_render_args_.selection_end_marker_ = &end_marker_;
  begin_marker_ = end_marker_ = -1;

  field_render_args_.fail_fade_ = -1;

  step_callback_.SetFromNil();
  set_pressed_callback_.SetFromNil();
  did_tap_note_callback_.SetFromNil();
  did_hold_note_callback_.SetFromNil();
}

NoteField::~NoteField() { Unload(); }

void NoteField::Unload() {
  for (std::map<std::string, NoteDisplayCols*>::iterator it =
           note_displays_.begin();
       it != note_displays_.end(); ++it) {
    delete it->second;
  }
  note_displays_.clear();
  cur_display_ = nullptr;
  memset(displays_, 0, sizeof(displays_));
}

void NoteField::SetBeatBars(bool active) { show_beat_bars_ = active; }

bool NoteField::GetBeatBars() { return show_beat_bars_; }

void NoteField::UnloadWaveform() {
  waveform_samples_l_.clear();
  waveform_samples_r_.clear();
  waveform_is_stereo_ = false;
  waveform_sample_rate_ = 0;
}

void NoteField::LoadWaveform(const std::string& sMusicPath) {
  UnloadWaveform();
  if (sMusicPath.empty()) {
    return;
  }

  std::string sError;
  RageSoundReader_FileReader* pReader =
      RageSoundReader_FileReader::OpenFile(sMusicPath, sError, nullptr);
  if (pReader == nullptr) {
    LOG->Warn(
        "NoteField: couldn't load waveform for \"%s\": %s", sMusicPath.c_str(),
        sError.c_str());
    return;
  }

  const int iSampleRate = pReader->GetSampleRate();
  const unsigned iChannels = pReader->GetNumChannels();
  if (iSampleRate <= 0 || iChannels == 0) {
    delete pReader;
    return;
  }
  waveform_is_stereo_ = iChannels >= 2;

  waveform_sample_rate_ = iSampleRate;
  const size_t iExpectedFrames = static_cast<size_t>(std::max(
      0, (int)((static_cast<int64_t>(pReader->GetLength()) * iSampleRate) /
               1000)));
  waveform_samples_l_.reserve(iExpectedFrames);
  waveform_samples_r_.reserve(iExpectedFrames);

  const int iBufferFrames = 4096;
  std::vector<float> buf(static_cast<size_t>(iBufferFrames) * iChannels);

  while (true) {
    int iGot = pReader->Read(&buf[0], iBufferFrames);
    if (iGot <= 0) {
      break;
    }
    for (int i = 0; i < iGot; ++i) {
      const float fSampleL = buf[i * iChannels + 0];
      const float fSampleR = iChannels >= 2 ? buf[i * iChannels + 1] : fSampleL;
      waveform_samples_l_.push_back(fSampleL);
      waveform_samples_r_.push_back(fSampleR);
    }
  }

  delete pReader;
}

void NoteField::SetBeatBarsAlpha(
    float measure, float fourth, float eighth, float sixteenth) {
  bar_measure_alpha_ = measure;
  bar4th_alpha_ = fourth;
  bar8th_alpha_ = eighth;
  bar16th_alpha_ = sixteenth;
}

void NoteField::CacheNoteSkin(const std::string& sNoteSkin_) {
  std::string sNoteSkinLower = sNoteSkin_;
  MakeLower(sNoteSkinLower);

  if (note_displays_.find(sNoteSkinLower) != note_displays_.end()) {
    return;
  }

  LockNoteSkin l(sNoteSkinLower);

  LOG->Trace("NoteField::CacheNoteSkin: cache %s", sNoteSkinLower.c_str());
  NoteDisplayCols* nd = new NoteDisplayCols(
      GAMESTATE->GetCurrentStyle(player_state_->player_number_)
          ->cols_per_player_);

  for (int c = 0; c < GAMESTATE->GetCurrentStyle(player_state_->player_number_)
                          ->cols_per_player_;
       c++) {
    nd->display_[c].Load(c, player_state_, y_reverse_offset_pixels_);
  }
  nd->receptor_arrow_row_.Load(player_state_, y_reverse_offset_pixels_);
  nd->ghost_arrow_row_.Load(player_state_, y_reverse_offset_pixels_);

  note_displays_[sNoteSkinLower] = nd;
}

void NoteField::UncacheNoteSkin(const std::string& sNoteSkin_) {
  std::string sNoteSkinLower = sNoteSkin_;
  MakeLower(sNoteSkinLower);

  LOG->Trace("NoteField::CacheNoteSkin: release %s", sNoteSkinLower.c_str());
  ASSERT_M(
      note_displays_.find(sNoteSkinLower) != note_displays_.end(),
      sNoteSkinLower);
  delete note_displays_[sNoteSkinLower];
  note_displays_.erase(sNoteSkinLower);
}

void NoteField::CacheAllUsedNoteSkins() {
  /* Cache all note skins that we might need for the whole song, course or
   * battle play, so we don't have to load them later (such as between course
   * songs). */
  std::vector<std::string> asSkinsLower;
  GAMESTATE->GetAllUsedNoteSkins(asSkinsLower);
  asSkinsLower.push_back(player_state_->player_options_.GetStage().note_skin_);
  for (std::string& s : asSkinsLower) {
    NOTESKIN->ValidateNoteSkinName(s);
    MakeLower(s);
  }

  for (unsigned i = 0; i < asSkinsLower.size(); ++i) {
    CacheNoteSkin(asSkinsLower[i]);
  }

  /* If we're changing note skins in the editor, we can have old note skins
   * lying around.  Remove them so they don't accumulate. */
  std::set<std::string> setNoteSkinsToUnload;
  for (std::pair<const std::string&, NoteDisplayCols*> d : note_displays_) {
    bool unused = find(asSkinsLower.begin(), asSkinsLower.end(), d.first) ==
                  asSkinsLower.end();
    if (unused) {
      setNoteSkinsToUnload.insert(d.first);
    }
  }
  for (const std::string& skin : setNoteSkinsToUnload) {
    UncacheNoteSkin(skin);
  }

  std::string sCurrentNoteSkinLower =
      player_state_->player_options_.GetCurrent().note_skin_;
  NOTESKIN->ValidateNoteSkinName(sCurrentNoteSkinLower);
  MakeLower(sCurrentNoteSkinLower);

  std::map<std::string, NoteDisplayCols*>::iterator it =
      note_displays_.find(sCurrentNoteSkinLower);
  ASSERT_M(it != note_displays_.end(), sCurrentNoteSkinLower);
  cur_display_ = it->second;
  memset(displays_, 0, sizeof(displays_));

  FOREACH_EnabledPlayer(pn) {
    std::string sNoteSkinLower =
        GAMESTATE->player_state_[pn]->player_options_.GetCurrent().note_skin_;
    NOTESKIN->ValidateNoteSkinName(sNoteSkinLower);
    MakeLower(sNoteSkinLower);
    it = note_displays_.find(sNoteSkinLower);
    ASSERT_M(it != note_displays_.end(), sNoteSkinLower);
    displays_[pn] = it->second;
  }

  InitColumnRenderers();
}

void NoteField::Init(
    const PlayerState* pPlayerState, float fYReverseOffsetPixels,
    bool use_states_zoom) {
  player_state_ = pPlayerState;
  y_reverse_offset_pixels_ = fYReverseOffsetPixels;
  CacheAllUsedNoteSkins();
  // Design change:  Instead of having a flag in the style that toggles a
  // fixed zoom that is only applied to the columns, ScreenGameplay now
  // calculates a zoom factor to apply to the notefield and puts it in the
  // PlayerState. -Kyz
  // use_states_zoom flag exists because edit mode has to set its own special
  // zoom factor. -Kyz
  if (use_states_zoom) {
    SetZoom(pPlayerState->notefield_zoom_);
  }
  // Pass the player state info down to children so that they can set
  // per-player things.  For example, if a screen filter is in the notefield
  // board, this tells it what player it's for. -Kyz
  Message msg("PlayerStateSet");
  msg.SetParam("PlayerNumber", pPlayerState->player_number_);
  HandleMessage(msg);
}

void NoteField::Load(
    const NoteData* pNoteData, int iDrawDistanceAfterTargetsPixels,
    int iDrawDistanceBeforeTargetsPixels) {
  ASSERT(pNoteData != nullptr);
  note_data_ = pNoteData;
  draw_distance_after_targets_pixels_ = iDrawDistanceAfterTargetsPixels;
  draw_distance_before_targets_pixels_ = iDrawDistanceBeforeTargetsPixels;
  ASSERT(
      draw_distance_before_targets_pixels_ >=
      draw_distance_after_targets_pixels_);

  field_render_args_.fail_fade_ = -1;

  // int i1 = m_pNoteData->GetNumTracks();
  // int i2 =
  // GAMESTATE->GetCurrentStyle(m_pPlayerState->m_PlayerNumber)->m_iColsPerPlayer;
  ASSERT_M(
      note_data_->GetNumTracks() ==
          GAMESTATE->GetCurrentStyle(player_state_->player_number_)
              ->cols_per_player_,
      ssprintf(
          "NumTracks %d = ColsPerPlayer %d", note_data_->GetNumTracks(),
          GAMESTATE->GetCurrentStyle(player_state_->player_number_)
              ->cols_per_player_));

  // If we're in routine mode, the noteskin is forcibly set to the routine
  // noteskin metrics (which is bad in its own way).  The noteskin set in the
  // options is ignored and probably already set anyway. -Kyz
  if (GAMESTATE->GetCurrentStyle(player_state_->player_number_)->style_type_ !=
      StyleType_TwoPlayersSharedSides) {
    ensure_note_displays_have_skin();
  }
  InitColumnRenderers();
}

void NoteField::ensure_note_displays_have_skin() {
  // The NoteSkin may have changed at the beginning of a new course song.
  std::string sNoteSkinLower =
      player_state_->player_options_.GetCurrent().note_skin_;

  /* XXX: Combination of good idea and bad idea to ensure courses load
   * regardless of noteskin content. This may take a while to fix. */
  NoteDisplayCols* badIdea = cur_display_;

  if (sNoteSkinLower.empty()) {
    sNoteSkinLower = player_state_->player_options_.GetPreferred().note_skin_;

    if (sNoteSkinLower.empty()) {
      sNoteSkinLower = "default";
    }
    note_displays_.insert(
        std::pair<std::string, NoteDisplayCols*>(sNoteSkinLower, badIdea));
  }

  MakeLower(sNoteSkinLower);
  std::map<std::string, NoteDisplayCols*>::iterator it =
      note_displays_.find(sNoteSkinLower);
  ASSERT_M(
      it != note_displays_.end(),
      ssprintf(
          "iterator != note_displays_.end() [sNoteSkinLower = %s]",
          sNoteSkinLower.c_str()));
  memset(displays_, 0, sizeof(displays_));
  FOREACH_EnabledPlayer(pn) {
    sNoteSkinLower =
        GAMESTATE->player_state_[pn]->player_options_.GetCurrent().note_skin_;

    // XXX: Re-setup sNoteSkinLower. Unsure if inserting the skin again is
    // needed.
    if (sNoteSkinLower.empty()) {
      sNoteSkinLower = GAMESTATE->player_state_[pn]
                           ->player_options_.GetPreferred()
                           .note_skin_;

      if (sNoteSkinLower.empty()) {
        sNoteSkinLower = "default";
      }
      note_displays_.insert(
          std::pair<std::string, NoteDisplayCols*>(sNoteSkinLower, badIdea));
    }

    MakeLower(sNoteSkinLower);
    it = note_displays_.find(sNoteSkinLower);
    ASSERT_M(it != note_displays_.end(), sNoteSkinLower);
    displays_[pn] = it->second;
  }
}

void NoteField::InitColumnRenderers() {
  field_render_args_.player_state_ = player_state_;
  field_render_args_.reverse_offset_pixels_ = y_reverse_offset_pixels_;
  field_render_args_.receptor_row_ = &(cur_display_->receptor_arrow_row_);
  field_render_args_.ghost_row_ = &(cur_display_->ghost_arrow_row_);
  field_render_args_.note_data_ = note_data_;
  column_renderers_.resize(
      GAMESTATE->GetCurrentStyle(player_state_->player_number_)
          ->cols_per_player_);
  // Check if we're currently in twoplayersharedsides mode if so, we need to
  // hide the receptor row of player 2
  if (GAMESTATE->GetCurrentStyle(player_state_->player_number_)->style_type_ ==
      StyleType_TwoPlayersSharedSides) {
    // Check if the player state player number is player 2
    if (player_state_->player_number_ == PLAYER_2) {
      cur_display_->receptor_arrow_row_.SetDrawOrder(0);
      cur_display_->receptor_arrow_row_.SetVisible(false);
    }
  }
  for (size_t ncr = 0; ncr < column_renderers_.size(); ++ncr) {
    FOREACH_EnabledPlayer(pn) {
      column_renderers_[ncr].displays_[pn] = &(displays_[pn]->display_[ncr]);
    }
    column_renderers_[ncr].displays_[PLAYER_INVALID] =
        &(cur_display_->display_[ncr]);
    column_renderers_[ncr].column_ = ncr;
    column_renderers_[ncr].column_render_args_.column_ = ncr;
    column_renderers_[ncr].field_render_args_ = &field_render_args_;
  }
  cur_display_->receptor_arrow_row_.SetColumnRenderers(column_renderers_);
  cur_display_->ghost_arrow_row_.SetColumnRenderers(column_renderers_);
}

void NoteField::Update(float fDeltaTime) {
  if (first_update_) {
    cur_display_->receptor_arrow_row_.PlayCommand("On");
  }

  ActorFrame::Update(fDeltaTime);
  ArrowEffects::SetCurrentOptions(&player_state_->player_options_.GetCurrent());

  for (size_t c = 0; c < column_renderers_.size(); ++c) {
    column_renderers_[c].Update(fDeltaTime);
  }

  // update m_fBoardOffsetPixels, m_fCurrentBeatLastUpdate,
  // m_fYPosCurrentBeatLastUpdate
  const float fCurrentBeat = player_state_->GetDisplayedPosition().song_beat_;
  bool bTweeningOn = spr_board_->GetCurrentDiffuseAlpha() >= 0.98 &&
                     spr_board_->GetCurrentDiffuseAlpha() < 1.00;  // HACK
  if (!bTweeningOn && current_beat_last_update_ != -1) {
    const float fYOffsetLast =
        ArrowEffects::GetYOffset(player_state_, 0, current_beat_last_update_);
    const float fYPosLast = ArrowEffects::GetYPos(
        player_state_, 0, fYOffsetLast, y_reverse_offset_pixels_);
    const float fPixelDifference = fYPosLast - y_pos_current_beat_last_update_;

    // LOG->Trace( "speed = %f, %f, %f, %f, %f, %f", fSpeed, fYOffsetAtCurrent,
    // fYOffsetAtNext, fSecondsAtCurrent, fSecondsAtNext, fPixelDifference,
    // fSecondsDifference );

    board_offset_pixels_ += fPixelDifference;
    wrap(board_offset_pixels_, spr_board_->GetUnzoomedHeight());
  }
  current_beat_last_update_ = fCurrentBeat;
  const float fYOffsetCurrent =
      ArrowEffects::GetYOffset(player_state_, 0, current_beat_last_update_);
  y_pos_current_beat_last_update_ = ArrowEffects::GetYPos(
      player_state_, 0, fYOffsetCurrent, y_reverse_offset_pixels_);

  rect_marker_bar_.Update(fDeltaTime);

  NoteDisplayCols* cur = cur_display_;

  cur->receptor_arrow_row_.Update(fDeltaTime);
  cur->ghost_arrow_row_.Update(fDeltaTime);

  if (field_render_args_.fail_fade_ >= 0) {
    field_render_args_.fail_fade_ = std::min(
        field_render_args_.fail_fade_ + fDeltaTime / FADE_FAIL_TIME, 1.0f);
  }

  // Update fade to failed
  cur_display_->receptor_arrow_row_.SetFadeToFailPercent(
      field_render_args_.fail_fade_);

  NoteDisplay::Update(fDeltaTime);
  /* Update all NoteDisplays. Hack: We need to call this once per frame, not
   * once per player. */
  // TODO: Remove use of PlayerNumber.

  PlayerNumber pn = player_state_->player_number_;
  if (pn == GAMESTATE->GetMasterPlayerNumber()) {
    NoteDisplay::Update(fDeltaTime);
  }
}

float NoteField::GetWidth() const {
  const Style* pStyle =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  float fMinX, fMaxX;
  // TODO: Remove use of PlayerNumber.
  pStyle->GetMinAndMaxColX(player_state_->player_number_, fMinX, fMaxX);

  const float fYZoom = ArrowEffects::GetZoom(player_state_, 0, 0);
  return (fMaxX - fMinX + ARROW_SIZE) * fYZoom;
}

void NoteField::DrawBeatBar(
    const float fBeat, BeatBarType type, int iMeasureIndex) {
  bool bIsMeasure = type == measure;

  const float fYOffset = ArrowEffects::GetYOffset(player_state_, 0, fBeat);
  const float fYPos = ArrowEffects::GetYPos(
      player_state_, 0, fYOffset, y_reverse_offset_pixels_);

  float fAlpha;
  int iState;

  if (bIsMeasure) {
    fAlpha = bar_measure_alpha_;
    iState = 0;
  } else {
    const PlayerOptions& curr_ops = player_state_->player_options_.GetCurrent();
    float fScrollSpeed = curr_ops.scroll_speed_;
    if (curr_ops.time_spacing_ > 0) {
      fScrollSpeed = 4;
    } else if (curr_ops.max_scroll_bpm_ != 0) {
      fScrollSpeed = curr_ops.max_scroll_bpm_ / player_state_->read_bpm_;
    }
    switch (type) {
      DEFAULT_FAIL(type);
      case measure:  // handled above
      case beat:
        fAlpha = bar4th_alpha_;
        iState = 1;
        break;
      case half_beat:
        fAlpha = SCALE(fScrollSpeed, 1.0f, 2.0f, 0.0f, bar8th_alpha_);
        iState = 2;
        break;
      case quarter_beat:
        fAlpha = SCALE(fScrollSpeed, 2.0f, 4.0f, 0.0f, bar16th_alpha_);
        iState = 3;
        break;
    }
    rage_clamp(fAlpha, 0, 1);
  }

  float fWidth = GetWidth();
  float fFrameWidth = spr_beat_bars_.GetUnzoomedWidth();

  spr_beat_bars_.SetX(0);
  spr_beat_bars_.SetY(fYPos);
  spr_beat_bars_.SetDiffuse(RageColor(1, 1, 1, fAlpha));
  spr_beat_bars_.SetState(iState);
  spr_beat_bars_.SetCustomTextureRect(RectF(
      0, SCALE(iState, 0.f, 4.f, 0.f, 1.f), fWidth / fFrameWidth,
      SCALE(iState + 1, 0.f, 4.f, 0.f, 1.f)));
  spr_beat_bars_.SetZoomX(fWidth / spr_beat_bars_.GetUnzoomedWidth());
  spr_beat_bars_.Draw();

  if (GAMESTATE->IsEditing() && bIsMeasure) {
    int iMeasureNoDisplay = iMeasureIndex;

    text_measure_number_.SetDiffuse(RageColor(1, 1, 1, 1));
    text_measure_number_.SetGlow(RageColor(1, 1, 1, 0));
    text_measure_number_.SetHorizAlign(align_right);
    text_measure_number_.SetText(ssprintf("%d", iMeasureNoDisplay));
    text_measure_number_.SetXY(-fWidth / 2, fYPos);
    text_measure_number_.Draw();
  }
}

void NoteField::DrawBoard(
    int iDrawDistanceAfterTargetsPixels, int iDrawDistanceBeforeTargetsPixels) {
  // todo: make this an AutoActor instead? -aj
  Sprite* pSprite = dynamic_cast<Sprite*>((Actor*)spr_board_);
  if (pSprite == nullptr) {
    spr_board_->Draw();
  } else {
    // Draw the board centered on fYPosAt0 so that the board doesn't slide as
    // the draw distance changes with modifiers.
    const float fYPosAt0 =
        ArrowEffects::GetYPos(player_state_, 0, 0, y_reverse_offset_pixels_);

    RectF rect = *pSprite->GetCurrentTextureCoordRect();
    const float fBoardGraphicHeightPixels = pSprite->GetUnzoomedHeight();
    float fTexCoordOffset = board_offset_pixels_ / fBoardGraphicHeightPixels;

    // top half
    const float fHeight =
        iDrawDistanceBeforeTargetsPixels - iDrawDistanceAfterTargetsPixels;
    const float fY =
        fYPosAt0 -
        ((iDrawDistanceBeforeTargetsPixels + iDrawDistanceAfterTargetsPixels) /
         2.0f);

    pSprite->ZoomToHeight(fHeight);
    pSprite->SetY(fY);

    // handle tex coord offset and fade
    rect.top_ = -fTexCoordOffset -
                (iDrawDistanceBeforeTargetsPixels / fBoardGraphicHeightPixels);
    rect.bottom_ = -fTexCoordOffset + (-iDrawDistanceAfterTargetsPixels /
                                       fBoardGraphicHeightPixels);
    pSprite->SetCustomTextureRect(rect);
    float fFadeTop =
        FADE_BEFORE_TARGETS_PERCENT * iDrawDistanceBeforeTargetsPixels /
        (iDrawDistanceBeforeTargetsPixels - iDrawDistanceAfterTargetsPixels);
    pSprite->SetFadeTop(fFadeTop);
    pSprite->Draw();
  }
}

void NoteField::DrawMarkerBar(int iBeat) {
  float fBeat = NoteRowToBeat(iBeat);
  const float fYOffset = ArrowEffects::GetYOffset(player_state_, 0, fBeat);
  const float fYPos = ArrowEffects::GetYPos(
      player_state_, 0, fYOffset, y_reverse_offset_pixels_);

  rect_marker_bar_.StretchTo(RectF(
      -GetWidth() / 2, fYPos - ARROW_SIZE / 2, GetWidth() / 2,
      fYPos + ARROW_SIZE / 2));
  rect_marker_bar_.Draw();
}

static ThemeMetric<RageColor> AREA_HIGHLIGHT_COLOR(
    "NoteField", "AreaHighlightColor");
void NoteField::DrawAreaHighlight(int iStartBeat, int iEndBeat) {
  float fStartBeat = NoteRowToBeat(iStartBeat);
  float fEndBeat = NoteRowToBeat(iEndBeat);
  float fDrawDistanceAfterTargetsPixels =
      ArrowEffects::GetYOffset(player_state_, 0, fStartBeat);
  float fYStartPos = ArrowEffects::GetYPos(
      player_state_, 0, fDrawDistanceAfterTargetsPixels,
      y_reverse_offset_pixels_);
  float fDrawDistanceBeforeTargetsPixels =
      ArrowEffects::GetYOffset(player_state_, 0, fEndBeat);
  float fYEndPos = ArrowEffects::GetYPos(
      player_state_, 0, fDrawDistanceBeforeTargetsPixels,
      y_reverse_offset_pixels_);

  // The caller should have clamped these to reasonable values
  ASSERT(fYStartPos > -1000);
  ASSERT(fYEndPos < +5000);

  rect_area_highlight_.StretchTo(
      RectF(-GetWidth() / 2, fYStartPos, GetWidth() / 2, fYEndPos));
  rect_area_highlight_.SetDiffuse(AREA_HIGHLIGHT_COLOR);
  rect_area_highlight_.Draw();
}

// todo: add DrawWarpAreaBG? -aj

void NoteField::DrawWaveform() {
  if (waveform_samples_l_.empty() || waveform_sample_rate_ <= 0) {
    return;
  }

  const TimingData& timing = player_state_->GetDisplayedTiming();

  // Use the *NoOffset timing calls here: notes/beat bars are positioned
  // purely from beat rows, with no adjustment for the user's Global Offset
  // preference (an audio/video sync calibration value). Using the
  // Global-Offset-aware calls would shift the whole waveform relative to
  // the notes by that preference's value.
  const float fFirstBeat = NoteRowToBeat(field_render_args_.first_row_);
  const float fLastBeat = NoteRowToBeat(field_render_args_.last_row_);
  const float fFirstTime = timing.GetElapsedTimeFromBeatNoOffset(fFirstBeat);
  const float fLastTime = timing.GetElapsedTimeFromBeatNoOffset(fLastBeat);

  int iFirstSample =
      (int)std::floor(std::min(fFirstTime, fLastTime) * waveform_sample_rate_) -
      1;
  int iLastSample =
      (int)std::ceil(std::max(fFirstTime, fLastTime) * waveform_sample_rate_) +
      1;
  iFirstSample = std::max(iFirstSample, 0);
  iLastSample = std::min(iLastSample, (int)waveform_samples_l_.size() - 1);
  if (iFirstSample + 1 > iLastSample) {
    return;
  }

  const float fHalfWidth = GetWidth() * 0.5f * WAVEFORM_WIDTH_PERCENT;
  const RageColor baseColor = WAVEFORM_COLOR;
  const int iVisibleSamples = iLastSample - iFirstSample + 1;
  const int iVisiblePixels = std::max(
      128, (int)(field_render_args_.draw_pixels_before_targets_ -
                 field_render_args_.draw_pixels_after_targets_));
  // Adapt detail to zoom: keep multiple waveform points per screen pixel
  // before decimating so the shape remains granular like AV.
  const float fPointsPerPixel =
      std::max(1.0f, (float)WAVEFORM_POINTS_PER_PIXEL);
  const int iTargetPoints =
      std::max(1, (int)(iVisiblePixels * fPointsPerPixel));
  const int iStep = iVisibleSamples > iTargetPoints
                        ? (iVisibleSamples + iTargetPoints - 1) / iTargetPoints
                        : 1;

  std::vector<RageSpriteVertex> vStripL;
  std::vector<RageSpriteVertex> vStripR;
  vStripL.reserve((iVisibleSamples / iStep + 2) * 2);
  vStripR.reserve((iVisibleSamples / iStep + 2) * 2);

  auto accumulate_window = [&](int begin, int end, bool left_channel,
                               float& min_out, float& max_out) {
    min_out = +1.f;
    max_out = -1.f;
    for (int k = begin; k < end; ++k) {
      const std::vector<float>& samples =
          left_channel ? waveform_samples_l_ : waveform_samples_r_;
      min_out = std::min(min_out, samples[k]);
      max_out = std::max(max_out, samples[k]);
    }
    if (max_out < min_out) {
      min_out = max_out = 0.f;
    }
  };

  auto build_channel_strip = [&](bool left_channel,
                                 std::vector<RageSpriteVertex>& strip,
                                 RageColor color) {
    for (int i = iFirstSample; i <= iLastSample; i += iStep) {
      const int end = std::min(i + iStep, iLastSample + 1);
      float fMin = 0.f;
      float fMax = 0.f;
      accumulate_window(i, end, left_channel, fMin, fMax);

      fMin *= WAVEFORM_GAIN;
      fMax *= WAVEFORM_GAIN;
      rage_clamp(fMin, -1.f, 1.f);
      rage_clamp(fMax, -1.f, 1.f);

      const float fTime = ((i + end) * 0.5f) / waveform_sample_rate_;
      const float fBeat = timing.GetBeatFromElapsedTimeNoOffset(fTime);
      const float fYOffset = ArrowEffects::GetYOffset(player_state_, 0, fBeat);
      const float fY = ArrowEffects::GetYPos(
          player_state_, 0, fYOffset, y_reverse_offset_pixels_);

      const float fMagnitude =
          fHalfWidth * std::max(std::abs(fMin), std::abs(fMax));
      const float xL = -fMagnitude;
      const float xR = fMagnitude;

      RageSpriteVertex vLeft, vRight;
      vLeft.p_ = RageVector3(xL, fY, 0);
      vRight.p_ = RageVector3(xR, fY, 0);
      vLeft.c_ = color;
      vRight.c_ = color;
      vLeft.t_ = RageVector2(0, 0);
      vRight.t_ = RageVector2(0, 0);
      strip.push_back(vLeft);
      strip.push_back(vRight);
    }
  };

  RageColor channelColorL = baseColor;
  RageColor channelColorR = baseColor;
  // Overlapping both channels creates richer fine detail than mono merge.
  channelColorL.a_ *= waveform_is_stereo_ ? 0.65f : 1.0f;
  channelColorR.a_ *= waveform_is_stereo_ ? 0.65f : 1.0f;

  build_channel_strip(true, vStripL, channelColorL);
  if (waveform_is_stereo_) {
    build_channel_strip(false, vStripR, channelColorR);
  }

  if (vStripL.size() < 4 && vStripR.size() < 4) {
    return;
  }

  this->SetGlobalRenderStates();
  DISPLAY->ClearAllTextures();
  this->SetTextureRenderStates();
  if (vStripL.size() >= 4) {
    DISPLAY->DrawQuadStrip(&vStripL[0], vStripL.size());
  }
  if (vStripR.size() >= 4) {
    DISPLAY->DrawQuadStrip(&vStripR[0], vStripR.size());
  }
}

static ThemeMetric<RageColor> BPM_COLOR("NoteField", "BPMColor");
static ThemeMetric<RageColor> STOP_COLOR("NoteField", "StopColor");
static ThemeMetric<RageColor> DELAY_COLOR("NoteField", "DelayColor");
static ThemeMetric<RageColor> WARP_COLOR("NoteField", "WarpColor");
static ThemeMetric<RageColor> TIME_SIG_COLOR("NoteField", "TimeSignatureColor");
static ThemeMetric<RageColor> TICKCOUNT_COLOR("NoteField", "TickcountColor");
static ThemeMetric<RageColor> COMBO_COLOR("NoteField", "ComboColor");
static ThemeMetric<RageColor> LABEL_COLOR("NoteField", "LabelColor");
static ThemeMetric<RageColor> SPEED_COLOR("NoteField", "SpeedColor");
static ThemeMetric<RageColor> SCROLL_COLOR("NoteField", "ScrollColor");
static ThemeMetric<RageColor> FAKE_COLOR("NoteField", "FakeColor");
static ThemeMetric<bool> BPM_IS_LEFT_SIDE("NoteField", "BPMIsLeftSide");
static ThemeMetric<bool> STOP_IS_LEFT_SIDE("NoteField", "StopIsLeftSide");
static ThemeMetric<bool> DELAY_IS_LEFT_SIDE("NoteField", "DelayIsLeftSide");
static ThemeMetric<bool> WARP_IS_LEFT_SIDE("NoteField", "WarpIsLeftSide");
static ThemeMetric<bool> TIME_SIG_IS_LEFT_SIDE(
    "NoteField", "TimeSignatureIsLeftSide");
static ThemeMetric<bool> TICKCOUNT_IS_LEFT_SIDE(
    "NoteField", "TickcountIsLeftSide");
static ThemeMetric<bool> COMBO_IS_LEFT_SIDE("NoteField", "ComboIsLeftSide");
static ThemeMetric<bool> LABEL_IS_LEFT_SIDE("NoteField", "LabelIsLeftSide");
static ThemeMetric<bool> SPEED_IS_LEFT_SIDE("NoteField", "SpeedIsLeftSide");
static ThemeMetric<bool> SCROLL_IS_LEFT_SIDE("NoteField", "ScrollIsLeftSide");
static ThemeMetric<bool> FAKE_IS_LEFT_SIDE("NoteField", "FakeIsLeftSide");
static ThemeMetric<float> BPM_OFFSETX("NoteField", "BPMOffsetX");
static ThemeMetric<float> STOP_OFFSETX("NoteField", "StopOffsetX");
static ThemeMetric<float> DELAY_OFFSETX("NoteField", "DelayOffsetX");
static ThemeMetric<float> WARP_OFFSETX("NoteField", "WarpOffsetX");
static ThemeMetric<float> TIME_SIG_OFFSETX("NoteField", "TimeSignatureOffsetX");
static ThemeMetric<float> TICKCOUNT_OFFSETX("NoteField", "TickcountOffsetX");
static ThemeMetric<float> COMBO_OFFSETX("NoteField", "ComboOffsetX");
static ThemeMetric<float> LABEL_OFFSETX("NoteField", "LabelOffsetX");
static ThemeMetric<float> SPEED_OFFSETX("NoteField", "SpeedOffsetX");
static ThemeMetric<float> SCROLL_OFFSETX("NoteField", "ScrollOffsetX");
static ThemeMetric<float> FAKE_OFFSETX("NoteField", "FakeOffsetX");

void NoteField::set_text_measure_number_for_draw(
    const float beat, const float side_sign, float x_offset,
    const float horiz_align, const RageColor& color, const RageColor& glow) {
  const float y_offset = ArrowEffects::GetYOffset(player_state_, 0, beat);
  const float y_pos = ArrowEffects::GetYPos(
      player_state_, 0, y_offset, y_reverse_offset_pixels_);
  const float zoom = ArrowEffects::GetZoom(player_state_, y_offset, 0);
  const float x_base = GetWidth() * .5f;
  x_offset *= zoom;

  text_measure_number_.SetZoom(zoom);
  text_measure_number_.SetHorizAlign(horiz_align);
  text_measure_number_.SetDiffuse(color);
  text_measure_number_.SetGlow(glow);
  text_measure_number_.SetXY((x_offset + x_base) * side_sign, y_pos);
}

void NoteField::draw_timing_segment_text(
    const std::string& text, const float beat, const float side_sign,
    float x_offset, const float horiz_align, const RageColor& color,
    const RageColor& glow) {
  set_text_measure_number_for_draw(
      beat, side_sign, x_offset, horiz_align, color, glow);
  text_measure_number_.SetText(text);
  text_measure_number_.Draw();
}

void NoteField::DrawAttackText(
    const float beat, const Attack& attack, const RageColor& glow) {
  set_text_measure_number_for_draw(
      beat, 1, 10, align_left, RageColor(0, 0.8f, 0.8f, 1), glow);
  text_measure_number_.SetText(attack.GetTextDescription());
  text_measure_number_.Draw();
}

void NoteField::DrawBGChangeText(
    const float beat, const std::string new_bg_name, const RageColor& glow) {
  set_text_measure_number_for_draw(
      beat, 1, 0, align_left, RageColor(0, 1, 0, 1), glow);
  text_measure_number_.SetText(new_bg_name);
  text_measure_number_.Draw();
}

static CacheNoteStat GetNumNotesFromBeginning(
    const PlayerState* pPlayerState, float beat) {
  // XXX: I realized that I have copied and pasted my binary search code 3 times
  // already.
  //      how can we abstract this?
  const std::vector<CacheNoteStat>& data = pPlayerState->cache_note_stat_;
  int max = data.size() - 1;
  int l = 0, r = max;
  while (l <= r) {
    int m = (l + r) / 2;
    if ((m == 0 || data[m].beat_ <= beat) &&
        (m == max || beat < data[m + 1].beat_)) {
      return data[m];
    } else if (data[m].beat_ <= beat) {
      l = m + 1;
    } else {
      r = m - 1;
    }
  }
  CacheNoteStat dummy = {0, 0, 0};
  return dummy;
}

static int GetNumNotesRange(
    const PlayerState* pPlayerState, float fLow, float fHigh) {
  CacheNoteStat low = GetNumNotesFromBeginning(pPlayerState, fLow);
  CacheNoteStat high = GetNumNotesFromBeginning(pPlayerState, fHigh);
  return high.notes_upper_ - low.notes_lower_;
}

float FindFirstDisplayedBeat(
    const PlayerState* pPlayerState, int iDrawDistanceAfterTargetsPixels) {
  float fLow = 0, fHigh = pPlayerState->GetDisplayedPosition().song_beat_;

  bool bHasCache = pPlayerState->cache_note_stat_.size() > 0;

  if (!bHasCache) {
    fLow = fHigh - 4.0f;
  }

  const int NUM_ITERATIONS = 24;
  const int MAX_NOTES_AFTER = 64;

  float fFirstBeatToDraw = fLow;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    float fMid = (fLow + fHigh) / 2.0f;

    bool bIsPastPeakYOffset;
    float fPeakYOffset;
    float fYOffset = ArrowEffects::GetYOffset(
        pPlayerState, 0, fMid, fPeakYOffset, bIsPastPeakYOffset, true);

    if (fYOffset < iDrawDistanceAfterTargetsPixels ||
        (bHasCache && GetNumNotesRange(
                          pPlayerState, fMid,
                          pPlayerState->GetDisplayedPosition().song_beat_) >
                          MAX_NOTES_AFTER))  // off screen / too many notes
    {
      fFirstBeatToDraw = fMid;  // move towards fSongBeat
      fLow = fMid;
    } else  // on screen, move away!!
    {
      fHigh = fMid;
    }
  }

  return fFirstBeatToDraw;
}

float FindLastDisplayedBeat(
    const PlayerState* pPlayerState, int iDrawDistanceBeforeTargetsPixels) {
  // Probe for last note to draw. Worst case is 0.25x + boost.
  // Adjust search distance so that notes don't pop onto the screen.
  float fSearchDistance = 10;
  float fLastBeatToDraw =
      pPlayerState->GetDisplayedPosition().song_beat_ + fSearchDistance;
  float fSpeedMultiplier =
      pPlayerState->GetDisplayedTiming().GetDisplayedSpeedPercent(
          pPlayerState->GetDisplayedPosition().song_beat_visible_,
          pPlayerState->GetDisplayedPosition().music_seconds_visible_);

  const int NUM_ITERATIONS = 20;

  bool bBoomerang;
  {
    const float* fAccels = pPlayerState->player_options_.GetCurrent().accels_;
    bBoomerang = (fAccels[PlayerOptions::ACCEL_BOOMERANG] != 0);
  }

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    bool bIsPastPeakYOffset;
    float fPeakYOffset;
    float fYOffset = ArrowEffects::GetYOffset(
        pPlayerState, 0, fLastBeatToDraw, fPeakYOffset, bIsPastPeakYOffset,
        true);

    if (bBoomerang && !bIsPastPeakYOffset) {
      fLastBeatToDraw += fSearchDistance;
    } else if (fYOffset > iDrawDistanceBeforeTargetsPixels) {  // off screen
      fLastBeatToDraw -= fSearchDistance;
    } else {  // on screen
      fLastBeatToDraw += fSearchDistance;
    }

    fSearchDistance /= 2;
  }

  if (fSpeedMultiplier < 0.75) {
    fLastBeatToDraw = std::min(
        fLastBeatToDraw, pPlayerState->GetDisplayedPosition().song_beat_ + 16);
  }

  return fLastBeatToDraw;
}

bool NoteField::IsOnScreen(
    float fBeat, int iCol, int iDrawDistanceAfterTargetsPixels,
    int iDrawDistanceBeforeTargetsPixels) const {
  // IMPORTANT:  Do not modify this function without also modifying the
  // version that is in NoteDisplay.cpp or coming up with a good way to
  // merge them. -Kyz
  // TRICKY: If boomerang is on, then ones in the range
  // [iFirstRowToDraw,iLastRowToDraw] aren't necessarily visible.
  // Test to see if this beat is visible before drawing.
  float fYOffset = ArrowEffects::GetYOffset(player_state_, iCol, fBeat);
  if (fYOffset > iDrawDistanceBeforeTargetsPixels) {  // off screen
    return false;
  }
  if (fYOffset < iDrawDistanceAfterTargetsPixels) {  // off screen
    return false;
  }

  return true;
}

void NoteField::CalcPixelsBeforeAndAfterTargets() {
  const PlayerOptions& curr_options =
      player_state_->player_options_.GetCurrent();
  // Adjust draw range depending on some effects
  field_render_args_.draw_pixels_after_targets_ =
      draw_distance_after_targets_pixels_ *
      (1.f + curr_options.draw_size_back_);
  // HACK: If boomerang and centered are on, then we want to draw much
  // earlier so that the notes don't pop on screen.
  float centered_times_boomerang =
      curr_options.scrolls_[PlayerOptions::SCROLL_CENTERED] *
      curr_options.accels_[PlayerOptions::ACCEL_BOOMERANG];
  field_render_args_.draw_pixels_after_targets_ +=
      int(SCALE(centered_times_boomerang, 0.f, 1.f, 0.f, -SCREEN_HEIGHT / 2));
  field_render_args_.draw_pixels_before_targets_ =
      draw_distance_before_targets_pixels_ * (1.f + curr_options.draw_size_);

  float draw_scale = 1;
  draw_scale *= 1 + 0.5f * std::abs(curr_options.perspective_tilt_);
  draw_scale *= 1 + std::abs(curr_options.effects_[PlayerOptions::EFFECT_MINI]);

  field_render_args_.draw_pixels_after_targets_ =
      (int)(field_render_args_.draw_pixels_after_targets_ * draw_scale);
  field_render_args_.draw_pixels_before_targets_ =
      (int)(field_render_args_.draw_pixels_before_targets_ * draw_scale);
}

void NoteField::DrawPrimitives() {
  // LOG->Trace( "NoteField::DrawPrimitives()" );

  // This should be filled in on the first update.
  ASSERT(cur_display_ != nullptr);

  // ArrowEffects::Update call moved because having it happen once per
  // NoteField (which means twice in two player) seemed wasteful. -Kyz

  if (drawing_board_primitive_) {
    CalcPixelsBeforeAndAfterTargets();
    DrawBoard(
        field_render_args_.draw_pixels_after_targets_,
        field_render_args_.draw_pixels_before_targets_);
    return;
  }
  // Some might prefer an else block, instead of returning from the if, but I
  // don't want to bump the indent on the entire remaining section. -Kyz
  ArrowEffects::SetCurrentOptions(&player_state_->player_options_.GetCurrent());

  CalcPixelsBeforeAndAfterTargets();
  NoteDisplayCols* cur = cur_display_;
  // Probe for first and last notes on the screen
  float first_beat_to_draw = FindFirstDisplayedBeat(
      player_state_, field_render_args_.draw_pixels_after_targets_);
  float last_beat_to_draw = FindLastDisplayedBeat(
      player_state_, field_render_args_.draw_pixels_before_targets_);

  player_state_->last_drawn_beat_ = last_beat_to_draw;

  field_render_args_.first_row_ = BeatToNoteRow(first_beat_to_draw);
  field_render_args_.last_row_ = BeatToNoteRow(last_beat_to_draw);

  // LOG->Trace( "start = %f.1, end = %f.1", first_beat_to_draw-fSongBeat,
  // last_beat_to_draw-fSongBeat ); LOG->Trace( "Drawing elements %d through
  // %d", m_FieldRenderArgs.first_row, m_FieldRenderArgs.last_row );

#define IS_ON_SCREEN(fBeat)                                         \
  (first_beat_to_draw <= (fBeat) && (fBeat) <= last_beat_to_draw && \
   IsOnScreen(                                                      \
       fBeat, 0, field_render_args_.draw_pixels_after_targets_,     \
       field_render_args_.draw_pixels_before_targets_))

  // Draw Receptors
  {
    cur->receptor_arrow_row_.Draw();
  }

  const TimingData* pTiming = &player_state_->GetDisplayedTiming();
  const std::vector<TimingSegment*>* segs[NUM_TimingSegmentType];

  FOREACH_TimingSegmentType(tst) segs[tst] = &(pTiming->GetTimingSegments(tst));

  // In TwoPlayersSharedSides (routine/couples), Restrict beat bar drawing to
  // the master player so only one copy is rendered otherwise one renders over
  // the other player
  const Style* shared_style =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  const bool suppress_shared_overlays =
      shared_style != nullptr &&
      shared_style->style_type_ == StyleType_TwoPlayersSharedSides &&
      player_state_->player_number_ != GAMESTATE->GetMasterPlayerNumber();

  // Draw the song waveform behind the beat bars.
  if (show_waveform_ && pTiming != nullptr && !suppress_shared_overlays) {
    DrawWaveform();
  }

  // Draw beat bars
  if ((GAMESTATE->IsEditing() || show_beat_bars_) && pTiming != nullptr &&
      !suppress_shared_overlays) {
    const std::vector<TimingSegment*>& tSigs = *segs[SEGMENT_TIME_SIG];
    int iMeasureIndex = 0;
    for (size_t i = 0; i < tSigs.size(); i++) {
      const TimeSignatureSegment* ts = ToTimeSignature(tSigs[i]);
      int iSegmentEndRow = (i + 1 == tSigs.size())
                               ? field_render_args_.last_row_
                               : tSigs[i + 1]->GetRow();

      // beat bars every 16th note
      int iDrawBeatBarsEveryRows = BeatToNoteRow(((float)ts->GetDen()) / 4) / 4;

      // In 4/4, every 16th beat bar is a measure
      int iMeasureBarFrequency = ts->GetNum() * 4;
      int iBeatBarsDrawn = 0;

      for (int j = ts->GetRow(); j < iSegmentEndRow;
           j += iDrawBeatBarsEveryRows) {
        bool bMeasureBar = iBeatBarsDrawn % iMeasureBarFrequency == 0;
        BeatBarType type = quarter_beat;
        if (bMeasureBar) {
          type = measure;
        } else if (iBeatBarsDrawn % 4 == 0) {
          type = beat;
        } else if (iBeatBarsDrawn % 2 == 0) {
          type = half_beat;
        }
        float fBeat = NoteRowToBeat(j);

        if (IS_ON_SCREEN(fBeat)) {
          DrawBeatBar(fBeat, type, iMeasureIndex);
        }

        iBeatBarsDrawn++;
        if (bMeasureBar) {
          iMeasureIndex++;
        }
      }
    }
  }

  if (GAMESTATE->IsEditing() && pTiming != nullptr) {
    ASSERT(GAMESTATE->cur_song_ != nullptr);

    const TimingData& timing = *pTiming;

    // Create an oscillating / pulsing glow effect.
    // Converts a counter to radians and uses the cosine for a cyclic
    // appearance.
    static uint_fast16_t iGlowCounter;
    static constexpr float fCyclical = 2.0f * 3.14159265f / 360.0f;
    iGlowCounter = (iGlowCounter + 1) % 360;
    float phase = iGlowCounter * fCyclical;
    float glow = std::cos(phase) * 0.5f + 0.5f;
    const RageColor text_glow = RageColor(1.0f, 1.0f, 1.0f, glow);

    float horiz_align = align_right;
    float side_sign = 1;
#define draw_all_segments(str_exp, name, caps_name)                       \
  horiz_align = caps_name##_IS_LEFT_SIDE ? align_right : align_left;      \
  side_sign = caps_name##_IS_LEFT_SIDE ? -1 : 1;                          \
  for (size_t i = 0; i < segs[SEGMENT_##caps_name]->size(); ++i) {        \
    const name##Segment* seg = To##name((*segs[SEGMENT_##caps_name])[i]); \
    if (seg->GetRow() >= field_render_args_.first_row_ &&                 \
        seg->GetRow() <= field_render_args_.last_row_ &&                  \
        IS_ON_SCREEN(seg->GetBeat())) {                                   \
      draw_timing_segment_text(                                           \
          str_exp, seg->GetBeat(), side_sign, caps_name##_OFFSETX,        \
          horiz_align, caps_name##_COLOR, text_glow);                     \
    }                                                                     \
  }

    draw_all_segments(std::to_string(seg->GetRatio()), Scroll, SCROLL);
    draw_all_segments(std::to_string(seg->GetBPM()), BPM, BPM);
    draw_all_segments(std::to_string(seg->GetPause()), Stop, STOP);
    draw_all_segments(std::to_string(seg->GetPause()), Delay, DELAY);
    draw_all_segments(std::to_string(seg->GetLength()), Warp, WARP);
    draw_all_segments(
        ssprintf("%d\n--\n%d", seg->GetNum(), seg->GetDen()), TimeSignature,
        TIME_SIG);
    draw_all_segments(ssprintf("%d", seg->GetTicks()), Tickcount, TICKCOUNT);
    draw_all_segments(
        ssprintf("%d/%d", seg->GetCombo(), seg->GetMissCombo()), Combo, COMBO);
    draw_all_segments(seg->GetLabel(), Label, LABEL);
    draw_all_segments(
        ssprintf(
            "%s\n%s\n%s", std::to_string(seg->GetRatio()).c_str(),
            (seg->GetUnit() == 1 ? "S" : "B"),
            std::to_string(seg->GetDelay()).c_str()),
        Speed, SPEED);
    draw_all_segments(std::to_string(seg->GetLength()), Fake, FAKE);
#undef draw_all_segments

    // Course mods text
    const Course* pCourse = GAMESTATE->cur_course_;
    if (pCourse) {
      ASSERT_M(
          GAMESTATE->edit_course_entry_index_ >= 0 &&
              GAMESTATE->edit_course_entry_index_ <
                  (int)pCourse->entries_.size(),
          ssprintf("%i", GAMESTATE->edit_course_entry_index_.Get()));
      const CourseEntry& ce =
          pCourse->entries_[GAMESTATE->edit_course_entry_index_];
      for (const Attack& a : ce.attacks_) {
        float fSecond = a.start_second_;
        float fBeat = timing.GetBeatFromElapsedTime(fSecond);

        if (BeatToNoteRow(fBeat) >= field_render_args_.first_row_ &&
            BeatToNoteRow(fBeat) <= field_render_args_.last_row_ &&
            IS_ON_SCREEN(fBeat)) {
          DrawAttackText(fBeat, a, text_glow);
        }
      }
    } else {
      auto draw_attacks = [&](const AttackArray& attacks) {
        for (const Attack& a : attacks) {
          float fBeat = timing.GetBeatFromElapsedTime(a.start_second_);
          if (BeatToNoteRow(fBeat) >= field_render_args_.first_row_ &&
              BeatToNoteRow(fBeat) <= field_render_args_.last_row_ &&
              IS_ON_SCREEN(fBeat)) {
            this->DrawAttackText(fBeat, a, text_glow);
          }
        }
      };

      draw_attacks(GAMESTATE->cur_song_->attacks_);
      if (GAMESTATE->is_using_step_timing_) {
        draw_attacks(GAMESTATE->cur_steps_[PLAYER_1]->GetStepsAttacks());
      }
    }

    if (!GAMESTATE->is_using_step_timing_) {
      // BGChange text
      EditMode mode = GAMESTATE->edit_mode_;
      switch (mode) {
        case EditMode_Home:
        case EditMode_CourseMods:
        case EditMode_Practice:
          break;
        case EditMode_Full: {
          std::vector<BackgroundChange>::iterator iter[NUM_BackgroundLayer];
          FOREACH_BackgroundLayer(j) iter[j] =
              GAMESTATE->cur_song_->GetBackgroundChanges(j).begin();

          for (;;) {
            float fLowestBeat = FLT_MAX;
            std::vector<BackgroundLayer> viLowestIndex;

            FOREACH_BackgroundLayer(j) {
              if (iter[j] ==
                  GAMESTATE->cur_song_->GetBackgroundChanges(j).end()) {
                continue;
              }
              float fBeat = iter[j]->start_beat_;
              if (fBeat < fLowestBeat) {
                fLowestBeat = fBeat;
                viLowestIndex.clear();
                viLowestIndex.push_back(j);
              } else if (fBeat == fLowestBeat) {
                viLowestIndex.push_back(j);
              }
            }

            if (viLowestIndex.empty()) {
              FOREACH_BackgroundLayer(j) {
                ASSERT(
                    iter[j] ==
                    GAMESTATE->cur_song_->GetBackgroundChanges(j).end());
              }
              break;
            }

            if (IS_ON_SCREEN(fLowestBeat)) {
              std::vector<std::string> vsBGChanges;
              for (const BackgroundLayer& bl : viLowestIndex) {
                ASSERT(
                    iter[bl] !=
                    GAMESTATE->cur_song_->GetBackgroundChanges(bl).end());
                const BackgroundChange& change = *iter[bl];
                std::string s = change.GetTextDescription();
                if (bl != 0) {
                  s = ssprintf("%d: ", bl) + s;
                }
                vsBGChanges.push_back(s);
              }
              DrawBGChangeText(fLowestBeat, join("\n", vsBGChanges), text_glow);
            }
            for (const BackgroundLayer& bl : viLowestIndex) {
              iter[bl]++;
            }
          }
        } break;
        default:
          FAIL_M(ssprintf("Invalid edit mode: %i", mode));
      }
    }

    // Draw marker bars
    if (begin_marker_ != -1 && end_marker_ != -1) {
      int iBegin = begin_marker_;
      int iEnd = end_marker_;
      rage_clamp(
          iBegin, field_render_args_.first_row_, field_render_args_.last_row_);
      rage_clamp(
          iEnd, field_render_args_.first_row_, field_render_args_.last_row_);
      DrawAreaHighlight(iBegin, iEnd);
    } else if (begin_marker_ != -1) {
      if (begin_marker_ >= field_render_args_.first_row_ &&
          begin_marker_ <= field_render_args_.last_row_) {
        DrawMarkerBar(begin_marker_);
      }
    } else if (end_marker_ != -1) {
      if (end_marker_ >= field_render_args_.first_row_ &&
          end_marker_ <= field_render_args_.last_row_) {
        DrawMarkerBar(end_marker_);
      }
    }
  }

  // Optimization is very important here because there are so many arrows to
  // draw. Draw the arrows in order of column. This minimizes texture switches
  // and lets us draw in big batches.

  const Style* pStyle =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  ASSERT_M(
      note_data_->GetNumTracks() ==
          GAMESTATE->GetCurrentStyle(player_state_->player_number_)
              ->cols_per_player_,
      ssprintf(
          "NumTracks %d != ColsPerPlayer %d", note_data_->GetNumTracks(),
          GAMESTATE->GetCurrentStyle(player_state_->player_number_)
              ->cols_per_player_));

  if (*field_render_args_.selection_begin_marker_ != -1 &&
      *field_render_args_.selection_end_marker_ != -1) {
    field_render_args_.selection_glow_ =
        SCALE(std::cos(RageTimer::GetTimeSinceStart() * 2), -1, 1, 0.1f, 0.3f);
  }
  field_render_args_.fade_before_targets_ = FADE_BEFORE_TARGETS_PERCENT;

  for (int j = 0; j < note_data_->GetNumTracks(); j++)  // for each arrow column
  {
    const int c = pStyle->column_draw_order_[j];
    column_renderers_[c].Draw();
  }

  cur->ghost_arrow_row_.Draw();
}

void NoteField::DrawBoardPrimitive() {
  if (!SHOW_BOARD) {
    return;
  }
  drawing_board_primitive_ = true;
  Draw();
  drawing_board_primitive_ = false;
}

void NoteField::FadeToFail() {
  field_render_args_.fail_fade_ = std::max(
      0.0f,
      field_render_args_
          .fail_fade_);  // this will slowly increase every Update()
                         // don't fade all over again if this is called twice
}

// A few functions and macros to take care of processing the callback
// return values, since the code would be identical in all of them. -Kyz

#define OPEN_CALLBACK_BLOCK(member_name)   \
  if (!from_lua && !member_name.IsNil()) { \
    Lua* L = LUA->Get();                   \
    member_name.PushSelf(L);

#define OPEN_RUN_BLOCK(arg_count)                 \
  std::string error = "Error running callback: "; \
  if (LuaHelpers::RunScriptOnStack(L, error, arg_count, arg_count, true)) {
#define CLOSE_RUN_AND_CALLBACK_BLOCKS \
  }                                   \
  lua_settop(L, 0);                   \
  LUA->Release(L);                    \
  }
#define PUSH_COLUMN lua_pushnumber(L, col + 1)

static void get_returned_column(Lua* L, PlayerNumber pn, int index, int& col) {
  if (lua_isnumber(L, index)) {
    // 1-indexed columns in lua
    int tmpcol = lua_tonumber(L, index) - 1;
    if (tmpcol < 0 ||
        tmpcol >= GAMESTATE->GetCurrentStyle(pn)->cols_per_player_) {
      LuaHelpers::ReportScriptErrorFmt(
          "Column returned by callback must be between 1 and %d "
          "(GAMESTATE:GetCurrentStyle():ColumnsPerPlayer()).",
          GAMESTATE->GetCurrentStyle(pn)->cols_per_player_);
    } else {
      col = tmpcol;
    }
  }
}

// Templated so it can be used for TNS and HNS. -Kyz
template <class T>
static void get_returned_score(Lua* L, int index, T& score) {
  T maybe_score = Enum::Check<T>(L, index, true, true);
  if (maybe_score != EnumTraits<T>::invalid_) {
    score = maybe_score;
  }
}

static void get_returned_bright(Lua* L, int index, bool& bright) {
  if (lua_isboolean(L, index)) {
    bright = lua_toboolean(L, index);
  }
}

void NoteField::Step(int col, TapNoteScore score, bool from_lua) {
  OPEN_CALLBACK_BLOCK(step_callback_);
  PUSH_COLUMN;
  Enum::Push(L, score);
  OPEN_RUN_BLOCK(2);
  get_returned_column(L, player_state_->player_number_, 1, col);
  get_returned_score(L, 2, score);
  CLOSE_RUN_AND_CALLBACK_BLOCKS;
  cur_display_->receptor_arrow_row_.Step(col, score);
}
void NoteField::SetPressed(int col, bool from_lua) {
  OPEN_CALLBACK_BLOCK(set_pressed_callback_);
  PUSH_COLUMN;
  OPEN_RUN_BLOCK(1);
  get_returned_column(L, player_state_->player_number_, 1, col);
  CLOSE_RUN_AND_CALLBACK_BLOCKS;
  cur_display_->receptor_arrow_row_.SetPressed(col);
}
void NoteField::DidTapNote(
    int col, TapNoteScore score, bool bright, bool from_lua) {
  OPEN_CALLBACK_BLOCK(did_tap_note_callback_);
  PUSH_COLUMN;
  Enum::Push(L, score);
  lua_pushboolean(L, bright);
  OPEN_RUN_BLOCK(3);
  get_returned_column(L, player_state_->player_number_, 1, col);
  get_returned_score(L, 2, score);
  get_returned_bright(L, 3, bright);
  CLOSE_RUN_AND_CALLBACK_BLOCKS;
  cur_display_->ghost_arrow_row_.DidTapNote(col, score, bright);
}
void NoteField::DidHoldNote(
    int col, HoldNoteScore score, bool bright, bool from_lua) {
  OPEN_CALLBACK_BLOCK(did_hold_note_callback_);
  PUSH_COLUMN;
  Enum::Push(L, score);
  lua_pushboolean(L, bright);
  OPEN_RUN_BLOCK(3);
  get_returned_column(L, player_state_->player_number_, 1, col);
  get_returned_score(L, 2, score);
  get_returned_bright(L, 3, bright);
  CLOSE_RUN_AND_CALLBACK_BLOCKS;
  cur_display_->ghost_arrow_row_.DidHoldNote(col, score, bright);
}

#undef OPEN_CALLBACK_BLOCK
#undef OPEN_RUN_BLOCK
#undef CLOSE_RUN_AND_CALLBACK_BLOCKS
#undef PUSH_COLUMN

void NoteField::HandleMessage(const Message& msg) {
  if (msg == Message_CurrentSongChanged) {
    current_beat_last_update_ = -1;
    y_pos_current_beat_last_update_ = -1;
  }

  ActorFrame::HandleMessage(msg);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Notefield. */
class LunaNoteField : public Luna<NoteField> {
 public:
#define SET_CALLBACK_GENERIC(callback_name, member_name)                \
  static int callback_name(T* p, lua_State* L) {                        \
    if (lua_isnoneornil(L, 1)) {                                        \
      p->member_name.SetFromNil();                                      \
    } else if (lua_isfunction(L, 1)) {                                  \
      p->member_name.SetFromStack(L);                                   \
    } else {                                                            \
      luaL_error(                                                       \
          L, #callback_name                                             \
          "Callback argument must be nil (to clear the callback) or a " \
          "function (to set the callback).");                           \
    }                                                                   \
    return 0;                                                           \
  }
  SET_CALLBACK_GENERIC(set_step_callback, step_callback_);
  SET_CALLBACK_GENERIC(set_set_pressed_callback, set_pressed_callback_);
  SET_CALLBACK_GENERIC(set_did_tap_note_callback, did_tap_note_callback_);
  SET_CALLBACK_GENERIC(set_did_hold_note_callback, did_hold_note_callback_);
#undef SET_CALLBACK_GENERIC

  static int check_column(lua_State* L, int index, PlayerNumber pn) {
    // 1-indexed columns in lua
    int col = IArg(1) - 1;
    if (col < 0 || col >= GAMESTATE->GetCurrentStyle(pn)->cols_per_player_) {
      luaL_error(
          L,
          "Column must be between 1 and %d "
          "(GAMESTATE:GetCurrentStyle(pn):ColumnsPerPlayer()).",
          GAMESTATE->GetCurrentStyle(pn)->cols_per_player_);
    }
    return col;
  }

  static int step(T* p, lua_State* L) {
    int col = check_column(L, 1, p->GetPlayerState()->player_number_);
    TapNoteScore tns = Enum::Check<TapNoteScore>(L, 2);
    p->Step(col, tns, true);
    return 0;
  }

  static int set_pressed(T* p, lua_State* L) {
    int col = check_column(L, 1, p->GetPlayerState()->player_number_);
    p->SetPressed(col, true);
    return 0;
  }

  static int did_tap_note(T* p, lua_State* L) {
    int col = check_column(L, 1, p->GetPlayerState()->player_number_);
    TapNoteScore tns = Enum::Check<TapNoteScore>(L, 2);
    bool bright = BArg(3);
    p->DidTapNote(col, tns, bright, true);
    return 0;
  }

  static int did_hold_note(T* p, lua_State* L) {
    int col = check_column(L, 1, p->GetPlayerState()->player_number_);
    HoldNoteScore hns = Enum::Check<HoldNoteScore>(L, 2);
    bool bright = BArg(3);
    p->DidHoldNote(col, hns, bright, true);
    return 0;
  }

  static int get_column_actors(T* p, lua_State* L) {
    lua_createtable(L, p->column_renderers_.size(), 0);
    for (size_t i = 0; i < p->column_renderers_.size(); ++i) {
      p->column_renderers_[i].PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  static int GetBeatBars(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetBeatBars());
    return 1;
  };

  static int SetBeatBars(T* p, lua_State* L) {
    p->SetBeatBars(BArg(1));
    return 0;
  }

  static int SetBeatBarsAlpha(T* p, lua_State* L) {
    p->SetBeatBarsAlpha(FArg(1), FArg(2), FArg(3), FArg(4));
    return 0;
  }

  LunaNoteField() {
    ADD_METHOD(set_step_callback);
    ADD_METHOD(set_set_pressed_callback);
    ADD_METHOD(set_did_tap_note_callback);
    ADD_METHOD(set_did_hold_note_callback);
    ADD_METHOD(step);
    ADD_METHOD(set_pressed);
    ADD_METHOD(did_tap_note);
    ADD_METHOD(did_hold_note);
    ADD_METHOD(get_column_actors);
    ADD_METHOD(GetBeatBars);
    ADD_METHOD(SetBeatBars);
    ADD_METHOD(SetBeatBarsAlpha);
  }
};

LUA_REGISTER_DERIVED_CLASS(NoteField, ActorFrame)
// lua end

/*
 * (c) 2001-2004 Chris Danford
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
