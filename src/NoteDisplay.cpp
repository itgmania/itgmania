#include "NoteDisplay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Actor.h"
#include "ArrowEffects.h"
#include "CubicSpline.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameState.h"
#include "GhostArrowRow.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteSkinManager.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageTexture.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ReceptorArrowRow.h"
#include "Sprite.h"
#include "Style.h"
#include "Tween.h"
#include "global.h"

static Preference<bool> g_bRenderEarlierNotesOnTop(
    "RenderEarlierNotesOnTop", false);

static const double PI_180 = PI / 180.0;
static const double PI_180R = 180.0 / PI;

const std::string& NoteNotePartToString(NotePart i);
/** @brief A foreach loop going through the different NoteParts. */
#define FOREACH_NotePart(i) FOREACH_ENUM(NotePart, i)

static const char* NotePartNames[] = {
    "TapNote",  "TapMine",    "TapLift",  "TapFake",       "HoldHead",
    "HoldTail", "HoldTopCap", "HoldBody", "HoldBottomCap",
};
XToString(NotePart);
LuaXType(NotePart);

static const char* NoteColorTypeNames[] = {
    "Denominator", "Progress", "ProgressAlternate"};
XToString(NoteColorType);
StringToX(NoteColorType);
LuaXType(NoteColorType);

static const char* NoteColumnSplineModeNames[] = {
    "Disabled",
    "Offset",
    "Position",
};
XToString(NoteColumnSplineMode);
StringToX(NoteColumnSplineMode);
LuaXType(NoteColumnSplineMode);

static bool IsVectorZero(const RageVector2& v) {
  return v.x_ == 0 && v.y_ == 0;
}

// cache
struct NoteMetricCache_t {
  bool draw_hold_head_for_taps_on_same_row_;
  bool draw_roll_head_for_taps_on_same_row_;
  bool tap_hold_roll_on_row_means_hold_;
  float animation_length_[NUM_NotePart];
  bool animation_is_vivid_[NUM_NotePart];
  RageVector2 addition_texture_coord_offset_[NUM_NotePart];
  RageVector2 note_color_texture_coord_spacing_[NUM_NotePart];

  int note_color_count_[NUM_NotePart];
  NoteColorType note_color_type_[NUM_NotePart];

  // For animation based on beats or seconds -DaisuMaster
  bool animation_based_on_beats_;
  bool hold_head_is_above_wavy_parts_;
  bool hold_tail_is_above_wavy_parts_;
  int start_drawing_hold_body_offset_from_head_;
  int stop_drawing_hold_body_offset_from_tail_;
  float hold_let_go_gray_percent_;
  bool flip_head_and_tail_when_reverse_;
  bool flip_hold_body_when_reverse_;
  bool top_hold_anchor_when_reverse_;
  bool hold_active_is_add_layer_;

  void Load(const std::string& sButton);
};

void NoteMetricCache_t::Load(const std::string& sButton) {
  draw_hold_head_for_taps_on_same_row_ =
      NOTESKIN->GetMetricB(sButton, "DrawHoldHeadForTapsOnSameRow");
  draw_roll_head_for_taps_on_same_row_ =
      NOTESKIN->GetMetricB(sButton, "DrawRollHeadForTapsOnSameRow");
  tap_hold_roll_on_row_means_hold_ =
      NOTESKIN->GetMetricB(sButton, "TapHoldRollOnRowMeansHold");
  FOREACH_NotePart(p) {
    const std::string& s = NotePartToString(p);
    animation_length_[p] = NOTESKIN->GetMetricF(sButton, s + "AnimationLength");
    animation_is_vivid_[p] =
        NOTESKIN->GetMetricB(sButton, s + "AnimationIsVivid");
    addition_texture_coord_offset_[p].x_ =
        NOTESKIN->GetMetricF(sButton, s + "AdditionTextureCoordOffsetX");
    addition_texture_coord_offset_[p].y_ =
        NOTESKIN->GetMetricF(sButton, s + "AdditionTextureCoordOffsetY");
    note_color_texture_coord_spacing_[p].x_ =
        NOTESKIN->GetMetricF(sButton, s + "NoteColorTextureCoordSpacingX");
    note_color_texture_coord_spacing_[p].y_ =
        NOTESKIN->GetMetricF(sButton, s + "NoteColorTextureCoordSpacingY");
    note_color_count_[p] = NOTESKIN->GetMetricI(sButton, s + "NoteColorCount");

    std::string ct = NOTESKIN->GetMetric(sButton, s + "NoteColorType");
    note_color_type_[p] = StringToNoteColorType(ct);
  }
  // I was here -DaisuMaster
  animation_based_on_beats_ =
      NOTESKIN->GetMetricB(sButton, "AnimationIsBeatBased");
  hold_head_is_above_wavy_parts_ =
      NOTESKIN->GetMetricB(sButton, "HoldHeadIsAboveWavyParts");
  hold_tail_is_above_wavy_parts_ =
      NOTESKIN->GetMetricB(sButton, "HoldTailIsAboveWavyParts");
  start_drawing_hold_body_offset_from_head_ =
      NOTESKIN->GetMetricI(sButton, "StartDrawingHoldBodyOffsetFromHead");
  stop_drawing_hold_body_offset_from_tail_ =
      NOTESKIN->GetMetricI(sButton, "StopDrawingHoldBodyOffsetFromTail");
  hold_let_go_gray_percent_ =
      NOTESKIN->GetMetricF(sButton, "HoldLetGoGrayPercent");
  flip_head_and_tail_when_reverse_ =
      NOTESKIN->GetMetricB(sButton, "FlipHeadAndTailWhenReverse");
  flip_hold_body_when_reverse_ =
      NOTESKIN->GetMetricB(sButton, "FlipHoldBodyWhenReverse");
  top_hold_anchor_when_reverse_ =
      NOTESKIN->GetMetricB(sButton, "TopHoldAnchorWhenReverse");
  hold_active_is_add_layer_ =
      NOTESKIN->GetMetricB(sButton, "HoldActiveIsAddLayer");
}

struct NoteSkinAndPath {
  NoteSkinAndPath(
      const std::string sNoteSkin_, const std::string sPath_,
      const PlayerNumber pn_, const GameController gc_)
      : note_skin_(sNoteSkin_), path_(sPath_), pn_(pn_), gc_(gc_) {}
  std::string note_skin_;
  std::string path_;
  PlayerNumber pn_;
  GameController gc_;
  bool operator<(const NoteSkinAndPath& other) const {
    int cmp = strcmp(note_skin_.c_str(), other.note_skin_.c_str());

    if (cmp < 0) {
      return true;
    } else if (cmp == 0) {
      if (path_ < other.path_) {
        return true;
      } else if (path_ == other.path_) {
        if (pn_ < other.pn_) {
          return true;
        } else if (pn_ == other.pn_) {
          return gc_ < other.gc_;
        } else {
          return false;
        }
      } else {
        return false;
      }
    } else {
      return false;
    }
  }
};

struct NoteResource {
  NoteResource(const NoteSkinAndPath& nsap) : nsap_(nsap) {
    ref_count_ = 0;
    actor_ = nullptr;
  }

  ~NoteResource() { delete actor_; }

  const NoteSkinAndPath
      nsap_;  // should be refcounted along with g_NoteResource[]
  int ref_count_;
  Actor* actor_;  // todo: AutoActor me? -aj
};

static std::map<std::string, std::map<NoteSkinAndPath, NoteResource*>>
    g_NoteResource;

static NoteResource* MakeNoteResource(
    const std::string& sButton, const std::string& sElement, PlayerNumber pn,
    GameController gc, bool bSpriteOnly, std::string sColor) {
  if (PREFSMAN->fast_note_rendering_) {
    sColor = "4th";
  }
  std::string sElementAndType =
      ssprintf("%s, %s", sButton.c_str(), sElement.c_str());
  NoteSkinAndPath nsap(NOTESKIN->GetCurrentNoteSkin(), sElementAndType, pn, gc);

  std::map<NoteSkinAndPath, NoteResource*>::iterator it =
      g_NoteResource[sColor].find(nsap);
  if (it == g_NoteResource[sColor].end()) {
    NoteResource* pRes = new NoteResource(nsap);

    NOTESKIN->SetPlayerNumber(pn);
    NOTESKIN->SetGameController(gc);

    pRes->actor_ =
        NOTESKIN->LoadActor(sButton, sElement, nullptr, bSpriteOnly, sColor);
    ASSERT(pRes->actor_ != nullptr);

    g_NoteResource[sColor][nsap] = pRes;
    it = g_NoteResource[sColor].find(nsap);
  }

  NoteResource* pRet = it->second;
  ++pRet->ref_count_;
  return pRet;
}

static void DeleteNoteResource(NoteResource* pRes) {
  ASSERT(pRes != nullptr);

  ASSERT_M(pRes->ref_count_ > 0, ssprintf("RefCount %i > 0", pRes->ref_count_));
  --pRes->ref_count_;
  if (pRes->ref_count_) {
    return;
  }

  for (auto& it : g_NoteResource) {
    it.second.erase(pRes->nsap_);
  }
  delete pRes;
}

/* NoteColorActor */

NoteColorActor::NoteColorActor() { p_.clear(); }

NoteColorActor::~NoteColorActor() {
  for (auto& it : p_) {
    if (it.second) {
      DeleteNoteResource(it.second);
    }
  }
  p_.clear();
}

void NoteColorActor::Load(
    const std::string& sButton, const std::string& sElement, PlayerNumber pn,
    GameController gc, const std::string& sColor) {
  p_[sColor] = MakeNoteResource(sButton, sElement, pn, gc, false, sColor);
}

Actor* NoteColorActor::Get(const std::string& sColor) {
  return p_[sColor]->actor_;
}

/* NoteColorSprite */

NoteColorSprite::NoteColorSprite() { p_.clear(); }

NoteColorSprite::~NoteColorSprite() {
  if (!p_.empty()) {
    for (auto& it : p_) {
      if (it.second) {
        DeleteNoteResource(it.second);
      }
    }
    p_.clear();
  }
}

void NoteColorSprite::Load(
    const std::string& sButton, const std::string& sElement, PlayerNumber pn,
    GameController gc, const std::string& sColor) {
  p_[sColor] = MakeNoteResource(sButton, sElement, pn, gc, true, sColor);
}

Sprite* NoteColorSprite::Get(const std::string& sColor) {
  return dynamic_cast<Sprite*>(p_[sColor]->actor_);
}

static const char* HoldTypeNames[] = {
    "Hold",
    "Roll",
    //"Minefield",
};
XToString(HoldType);

static const char* ActiveTypeNames[] = {
    "Active",
    "Inactive",
};
XToString(ActiveType);

float NCSplineHandler::BeatToTValue(float song_beat, float note_beat) const {
  float relative_beat = note_beat;
  // This allows someone to do something really fancy like having a spline
  // that extends the length of the song.  Think of arrows tracing a path
  // as the song progresses. -Kyz
  if (subtract_song_beat_from_curr_) {
    relative_beat -= song_beat;
    return (relative_beat / beats_per_t_) - receptor_t_;
  }
  return relative_beat / beats_per_t_;
}

void NCSplineHandler::EvalForBeat(
    float song_beat, float note_beat, RageVector3& ret) const {
  float t_value = BeatToTValue(song_beat, note_beat);
  spline_.evaluate(t_value, ret);
}

void NCSplineHandler::EvalDerivForBeat(
    float song_beat, float note_beat, RageVector3& ret) const {
  float t_value = BeatToTValue(song_beat, note_beat);
  spline_.evaluate_derivative(t_value, ret);
}

void NCSplineHandler::EvalForReceptor(float song_beat, RageVector3& ret) const {
  float t_value = receptor_t_;
  if (!subtract_song_beat_from_curr_) {
    t_value = song_beat / beats_per_t_;
  }
  spline_.evaluate(t_value, ret);
}

void NCSplineHandler::MakeWeightedAverage(
    NCSplineHandler& out, const NCSplineHandler& from,
    const NCSplineHandler& to, float between) {
#define BOOLS_FROM_CLOSEST(closest)        \
  out.spline_mode_ = closest.spline_mode_; \
  out.subtract_song_beat_from_curr_ = closest.subtract_song_beat_from_curr_;
  if (between >= 0.5f) {
    BOOLS_FROM_CLOSEST(to);
  } else {
    BOOLS_FROM_CLOSEST(from);
  }
#undef BOOLS_FROM_CLOSEST
  CubicSplineN::weighted_average(
      out.spline_, from.spline_, to.spline_, between);
}

void NoteColumnRenderArgs::spae_pos_for_beat(
    const PlayerState* player_state, float beat, float y_offset,
    float y_reverse_offset, RageVector3& sp_pos, RageVector3& ae_pos) const {
  switch (pos_handler_->spline_mode_) {
    case NCSM_Disabled:
      ArrowEffects::GetXYZPos(
          player_state, column_, y_offset, y_reverse_offset, ae_pos);
      break;
    case NCSM_Offset:
      ArrowEffects::GetXYZPos(
          player_state, column_, y_offset, y_reverse_offset, ae_pos);
      pos_handler_->EvalForBeat(song_beat_, beat, sp_pos);
      break;
    case NCSM_Position:
      pos_handler_->EvalForBeat(song_beat_, beat, sp_pos);
      break;
    default:
      break;
  }
}
void NoteColumnRenderArgs::spae_zoom_for_beat(
    const PlayerState* state, float beat, RageVector3& sp_zoom,
    RageVector3& ae_zoom, int col_num, float y_offset) const {
  switch (zoom_handler_->spline_mode_) {
    case NCSM_Disabled:
      ae_zoom.x_ = ae_zoom.y_ = ae_zoom.z_ =
          ArrowEffects::GetZoom(state, y_offset, col_num);
      break;
    case NCSM_Offset:
      ae_zoom.x_ = ae_zoom.y_ = ae_zoom.z_ =
          ArrowEffects::GetZoom(state, y_offset, col_num);
      zoom_handler_->EvalForBeat(song_beat_, beat, sp_zoom);
      break;
    case NCSM_Position:
      zoom_handler_->EvalForBeat(song_beat_, beat, sp_zoom);
      break;
    default:
      break;
  }
}
void NoteColumnRenderArgs::SetPRZForActor(
    Actor* actor, const RageVector3& sp_pos, const RageVector3& ae_pos,
    const RageVector3& sp_rot, const RageVector3& ae_rot,
    const RageVector3& sp_zoom, const RageVector3& ae_zoom) const {
  actor->SetX(sp_pos.x_ + ae_pos.x_);
  actor->SetY(sp_pos.y_ + ae_pos.y_);
  actor->SetZ(sp_pos.z_ + ae_pos.z_);
  actor->SetRotationX(sp_rot.x_ * PI_180R + ae_rot.x_);
  actor->SetRotationY(sp_rot.y_ * PI_180R + ae_rot.y_);
  actor->SetRotationZ(sp_rot.z_ * PI_180R + ae_rot.z_);
  actor->SetZoomX(sp_zoom.x_ + ae_zoom.x_);
  actor->SetZoomY(sp_zoom.y_ + ae_zoom.y_);
  actor->SetZoomZ(sp_zoom.z_ + ae_zoom.z_);
}

NoteDisplay::NoteDisplay() { cache_ = new NoteMetricCache_t; }

NoteDisplay::~NoteDisplay() { delete cache_; }

void NoteDisplay::Load(
    int iColNum, const PlayerState* pPlayerState, float fYReverseOffsetPixels) {
  player_state_ = pPlayerState;
  y_reverse_offset_pixels_ = fYReverseOffsetPixels;

  const PlayerNumber pn = player_state_->player_number_;
  std::vector<GameInput> GameI;
  GAMESTATE->GetCurrentStyle(pPlayerState->player_number_)
      ->StyleInputToGameInput(iColNum, pn, GameI);

  const std::string& sButton =
      GAMESTATE->GetCurrentStyle(pPlayerState->player_number_)
          ->ColToButtonName(iColNum);

  cache_->Load(sButton);
  std::vector<std::string> Colors = {"4th",  "8th",  "12th", "16th", "24th",
                                     "32nd", "48th", "64th", "192nd"};
  for (const std::string& color : Colors) {
    // "normal" note types
    tap_note_.Load(sButton, "Tap Note", pn, GameI[0].controller_, color);
    // m_TapAdd.Load(		sButton, "Tap Addition", pn, GameI.controller );
    tap_mine_.Load(sButton, "Tap Mine", pn, GameI[0].controller_, color);
    tap_lift_.Load(sButton, "Tap Lift", pn, GameI[0].controller_, color);
    tap_fake_.Load(sButton, "Tap Fake", pn, GameI[0].controller_, color);

    // hold types
    FOREACH_HoldType(ht) {
      FOREACH_ActiveType(at) {
        hold_head_[ht][at].Load(
            sButton, HoldTypeToString(ht) + " Head " + ActiveTypeToString(at),
            pn, GameI[0].controller_, color);
        hold_top_cap_[ht][at].Load(
            sButton, HoldTypeToString(ht) + " Topcap " + ActiveTypeToString(at),
            pn, GameI[0].controller_, color);
        hold_body_[ht][at].Load(
            sButton, HoldTypeToString(ht) + " Body " + ActiveTypeToString(at),
            pn, GameI[0].controller_, color);
        hold_bottom_cap_[ht][at].Load(
            sButton,
            HoldTypeToString(ht) + " Bottomcap " + ActiveTypeToString(at), pn,
            GameI[0].controller_, color);
        hold_tail_[ht][at].Load(
            sButton, HoldTypeToString(ht) + " Tail " + ActiveTypeToString(at),
            pn, GameI[0].controller_, color);
      }
    }
  }
}

inline float NoteRowToVisibleBeat(const PlayerState* pPlayerState, int iRow) {
  return NoteRowToBeat(iRow);
}

bool NoteDisplay::IsOnScreen(
    float fBeat, int iCol, int iDrawDistanceAfterTargetsPixels,
    int iDrawDistanceBeforeTargetsPixels) const {
  // IMPORTANT:  Do not modify this function without also modifying the
  // version that is in NoteField.cpp or coming up with a good way to
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

bool NoteDisplay::DrawHoldsInRange(
    const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args,
    const std::vector<NoteData::TrackMap::const_iterator>& tap_set) {
  bool any_upcoming = false;
  for (std::vector<NoteData::TrackMap::const_iterator>::const_iterator tapit =
           tap_set.begin();
       tapit != tap_set.end(); ++tapit) {
    const TapNote& tn = (*tapit)->second;
    const HoldNoteResult& result = tn.hold_result_;
    int start_row = (*tapit)->first;
    int end_row = start_row + tn.duration_;

    // TRICKY: If boomerang is on, then all notes in the range
    // [first_row,last_row] aren't necessarily visible.
    // Test every note to make sure it's on screen before drawing
    float throw_away;
    bool start_past_peak = false;
    bool end_past_peak = false;
    float start_y = ArrowEffects::GetYOffset(
        player_state_, column_args.column_,
        NoteRowToVisibleBeat(player_state_, start_row), throw_away,
        start_past_peak);
    float end_y = ArrowEffects::GetYOffset(
        player_state_, column_args.column_,
        NoteRowToVisibleBeat(player_state_, end_row), throw_away,
        end_past_peak);
    bool tail_visible = field_args.draw_pixels_after_targets_ <= end_y &&
                        end_y <= field_args.draw_pixels_before_targets_;
    bool head_visible = field_args.draw_pixels_after_targets_ <= start_y &&
                        start_y <= field_args.draw_pixels_before_targets_;
    bool straddling_visible =
        start_y <= field_args.draw_pixels_after_targets_ &&
        field_args.draw_pixels_before_targets_ <= end_y;
    bool straddling_peak = start_past_peak && !end_past_peak;
    if (!(tail_visible || head_visible || straddling_visible ||
          straddling_peak)) {
      // LOG->Trace( "skip drawing this hold." );
      continue;  // skip
    }

    bool is_addition = (tn.source_ == TapNoteSource_Addition);
    const bool hold_ghost_showing =
        tn.hold_result_.active_ && tn.hold_result_.life_ > 0;
    const bool is_holding = tn.hold_result_.held_;
    if (hold_ghost_showing) {
      field_args.ghost_row_->SetHoldShowing(column_args.column_, tn);
    }

    ASSERT_M(
        NoteRowToBeat(start_row) > -2000,
        ssprintf("%i %i %i", start_row, end_row, column_args.column_));

    bool in_selection_range = false;
    if (*field_args.selection_begin_marker_ != -1 &&
        *field_args.selection_end_marker_ != -1) {
      in_selection_range =
          (*field_args.selection_begin_marker_ <= start_row &&
           end_row < *field_args.selection_end_marker_);
    }

    DrawHold(
        tn, field_args, column_args, start_row, is_holding, result, is_addition,
        in_selection_range ? field_args.selection_glow_
                           : field_args.fail_fade_);

    bool note_upcoming = NoteRowToBeat(start_row) >
                         player_state_->GetDisplayedPosition().song_beat_;
    any_upcoming |= note_upcoming;
  }
  return any_upcoming;
}

bool NoteDisplay::DrawTapsInRange(
    const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args,
    const std::vector<NoteData::TrackMap::const_iterator>& tap_set) {
  bool any_upcoming = false;

  auto loop_body = [this, &field_args, &column_args, &any_upcoming](
                       const NoteData::TrackMap::const_iterator& tapit) {
    int tap_row = tapit->first;
    const TapNote& tn = tapit->second;

    // TRICKY: If boomerang is on, then all notes in the range
    // [first_row,last_row] aren't necessarily visible.
    // Test every note to make sure it's on screen before drawing.
    if (!IsOnScreen(
            NoteRowToBeat(tap_row), column_args.column_,
            field_args.draw_pixels_after_targets_,
            field_args.draw_pixels_before_targets_)) {
      return;  // skip
    }

    // Hm, this assert used to pass the first and last rows to draw, when it
    // was in NoteField, but those aren't available here.
    // Well, anyone who has to investigate hitting it can use a debugger to
    // discover the values, hopefully. -Kyz
    ASSERT_M(
        NoteRowToBeat(tap_row) > -2000,
        ssprintf(
            "Invalid tap_row: %i, %f %f", tap_row,
            player_state_->GetDisplayedPosition().song_beat_,
            player_state_->GetDisplayedPosition().music_seconds_));

    // See if there is a hold step that begins on this index.
    // Only do this if the noteskin cares.
    bool hold_begins_on_this_beat = false;
    if (DrawHoldHeadForTapsOnSameRow()) {
      for (int c2 = 0; c2 < field_args.note_data_->GetNumTracks(); ++c2) {
        const TapNote& tmp = field_args.note_data_->GetTapNote(c2, tap_row);
        if (tmp.type_ == TapNoteType_HoldHead &&
            tmp.sub_type_ == TapNoteSubType_Hold) {
          hold_begins_on_this_beat = true;
          break;
        }
      }
    }

    // do the same for a roll.
    bool roll_begins_on_this_beat = false;
    if (DrawRollHeadForTapsOnSameRow()) {
      for (int c2 = 0; c2 < field_args.note_data_->GetNumTracks(); ++c2) {
        const TapNote& tmp = field_args.note_data_->GetTapNote(c2, tap_row);
        if (tmp.type_ == TapNoteType_HoldHead &&
            tmp.sub_type_ == TapNoteSubType_Roll) {
          roll_begins_on_this_beat = true;
          break;
        }
      }
    }

    bool in_selection_range = false;
    if (*field_args.selection_begin_marker_ != -1 &&
        *field_args.selection_end_marker_ != -1) {
      in_selection_range = *field_args.selection_begin_marker_ <= tap_row &&
                           tap_row < *field_args.selection_end_marker_;
    }

    bool is_addition = (tn.source_ == TapNoteSource_Addition);
    DrawTap(
        tn, field_args, column_args,
        NoteRowToVisibleBeat(player_state_, tap_row), hold_begins_on_this_beat,
        roll_begins_on_this_beat, is_addition,
        in_selection_range ? field_args.selection_glow_
                           : field_args.fail_fade_);

    any_upcoming |= NoteRowToBeat(tap_row) >
                    player_state_->GetDisplayedPosition().song_beat_;

    if (!PREFSMAN->fast_note_rendering_) {
      DISPLAY->ClearZBuffer();
    }
  };

  if (g_bRenderEarlierNotesOnTop.Get()) {
    // draw notes from closest to furthest
    std::for_each(tap_set.rbegin(), tap_set.rend(), loop_body);
  } else {
    // draw notes from furthest to closest
    std::for_each(tap_set.begin(), tap_set.end(), loop_body);
  }

  return any_upcoming;
}

bool NoteDisplay::DrawHoldHeadForTapsOnSameRow() const {
  return cache_->draw_hold_head_for_taps_on_same_row_;
}

bool NoteDisplay::DrawRollHeadForTapsOnSameRow() const {
  return cache_->draw_roll_head_for_taps_on_same_row_;
}

void NoteDisplay::Update(float fDeltaTime) {
  /* This function is static: it's called once per game loop, not once per
   * NoteDisplay.  Update each cached item exactly once. */
  std::map<NoteSkinAndPath, NoteResource*>::iterator it;
  for (auto& color_map : g_NoteResource) {
    for (it = color_map.second.begin(); it != color_map.second.end(); ++it) {
      NoteResource* pRes = it->second;
      pRes->actor_->Update(fDeltaTime);
    }
  }
}

void NoteDisplay::SetActiveFrame(
    float fNoteBeat, Actor& actorToSet, float fAnimationLength, bool bVivid) {
  /* -inf ... inf */
  float fBeatOrSecond = cache_->animation_based_on_beats_
                            ? player_state_->position_.song_beat_
                            : player_state_->position_.music_seconds_;
  /* -len ... +len */
  float fPercentIntoAnimation = std::fmod(fBeatOrSecond, fAnimationLength);
  /* -1 ... 1 */
  fPercentIntoAnimation /= fAnimationLength;

  if (bVivid) {
    float fNoteBeatFraction = std::fmod(fNoteBeat, 1.0f);

    const float fInterval = 1.f / fAnimationLength;
    fPercentIntoAnimation += QuantizeDown(fNoteBeatFraction, fInterval);

    // just in case somehow we're majorly negative with the subtraction
    wrap(fPercentIntoAnimation, 1.f);
  } else {
    /* 0 ... 1, wrapped */
    if (fPercentIntoAnimation < 0) {
      fPercentIntoAnimation += 1.0f;
    }
  }

  float fLengthSeconds = actorToSet.GetAnimationLengthSeconds();
  actorToSet.SetSecondsIntoAnimation(fPercentIntoAnimation * fLengthSeconds);
}

Actor* NoteDisplay::GetTapActor(
    NoteColorActor& nca, NotePart part, float fNoteBeat) {
  const std::string& sColor = NoteTypeToString(BeatToNoteType(fNoteBeat));
  Actor* pActorOut = nca.Get(sColor);

  SetActiveFrame(
      fNoteBeat, *pActorOut, cache_->animation_length_[part],
      cache_->animation_is_vivid_[part]);
  return pActorOut;
}

Actor* NoteDisplay::GetHoldActor(
    NoteColorActor nca[NUM_HoldType][NUM_ActiveType], NotePart part,
    float fNoteBeat, bool bIsRoll, bool bIsBeingHeld) {
  return GetTapActor(
      nca[bIsRoll ? roll : hold][bIsBeingHeld ? active : inactive], part,
      fNoteBeat);
}

Sprite* NoteDisplay::GetHoldSprite(
    NoteColorSprite ncs[NUM_HoldType][NUM_ActiveType], NotePart part,
    float fNoteBeat, bool bIsRoll, bool bIsBeingHeld) {
  const std::string& sColor = NoteTypeToString(BeatToNoteType(fNoteBeat));
  Sprite* pSpriteOut =
      ncs[bIsRoll ? roll : hold][bIsBeingHeld ? active : inactive].Get(sColor);

  SetActiveFrame(
      fNoteBeat, *pSpriteOut, cache_->animation_length_[part],
      cache_->animation_is_vivid_[part]);
  return pSpriteOut;
}

static float ArrowGetAlphaOrGlow(
    bool bGlow, const PlayerState* pPlayerState, int iCol, float fYOffset,
    float fPercentFadeToFail, float fYReverseOffsetPixels,
    float fDrawDistanceBeforeTargetsPixels, float fFadeInPercentOfDrawFar) {
  if (bGlow) {
    return ArrowEffects::GetGlow(
        pPlayerState, iCol, fYOffset, fPercentFadeToFail, fYReverseOffsetPixels,
        fDrawDistanceBeforeTargetsPixels, fFadeInPercentOfDrawFar);
  } else {
    return ArrowEffects::GetAlpha(
        pPlayerState, iCol, fYOffset, fPercentFadeToFail, fYReverseOffsetPixels,
        fDrawDistanceBeforeTargetsPixels, fFadeInPercentOfDrawFar);
  }
}

struct StripBuffer {
  enum { size = 512 };
  RageSpriteVertex* buf_;
  RageSpriteVertex* v_;
  StripBuffer() {
    buf_ = (RageSpriteVertex*)malloc(size * sizeof(RageSpriteVertex));
    Init();
  }
  ~StripBuffer() { free(buf_); }

  void Init() { v_ = buf_; }
  void Draw() { DISPLAY->DrawSymmetricQuadStrip(buf_, v_ - buf_); }
  int Used() const { return v_ - buf_; }
  int Free() const { return size - Used(); }
};

enum hold_part_type {
  hpt_top,
  hpt_body,
  hpt_bottom,
};

void NoteDisplay::DrawHoldPart(
    std::vector<Sprite*>& vpSpr, const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args,
    const draw_hold_part_args& part_args, bool glow, int part_type) {
  ASSERT(!vpSpr.empty());

  float ae_zoom = ArrowEffects::GetZoom(player_state_, 0, column_args.column_);
  Sprite* pSprite = vpSpr.front();

  // draw manually in small segments
  RectF rect = *pSprite->GetCurrentTextureCoordRect();
  if (part_args.flip_texture_vertically_) {
    std::swap(rect.top_, rect.bottom_);
  }
  const float fFrameWidth = pSprite->GetUnzoomedWidth();
  const float unzoomed_frame_height = pSprite->GetUnzoomedHeight();

  /* Only draw the section that's within the range specified.  If a hold note is
   * very long, don't process or draw the part outside of the range.  Don't
   * change part_args.y_top or part_args.y_bottom; they need to be left alone to
   * calculate texture coordinates. */
  // If hold body, draw texture to the outside screen.(fix by A.C)
  float y_start_pos = (part_type == hpt_body)
                          ? part_args.y_top_
                          : std::max(part_args.y_top_, part_args.y_start_pos_);
  if (part_args.y_top_ < part_args.y_start_pos_ - unzoomed_frame_height) {
    y_start_pos =
        std::fmod(
            (y_start_pos - part_args.y_start_pos_), unzoomed_frame_height) +
        part_args.y_start_pos_;
  }
  float y_end_pos = std::min(part_args.y_bottom_, part_args.y_end_pos_);
  const float color_scale = glow ? 1 : part_args.color_scale_;
  if (part_type == hpt_body) {
    // Overshoot to cover up the seam between body and bottom.
    // It's not fully gone, but the bg doesn't peak through.
    y_end_pos += 1;
  }

  // top to bottom
  bool bAllAreTransparent = true;
  bool last_vert_set = false;
  bool first_vert_set = true;
  float add_to_tex_coord = 0;

  // The caps should always use the full texture.
  if (part_type == hpt_body) {
    if (!part_args.anchor_to_top_) {
      float tex_coord_bottom = SCALE(
          part_args.y_bottom_ - part_args.y_top_, 0, unzoomed_frame_height,
          rect.top_, rect.bottom_);
      float want_tex_coord_bottom = std::ceil(tex_coord_bottom - 0.0001f);
      add_to_tex_coord = want_tex_coord_bottom - tex_coord_bottom;
    }

    if (part_args.wrapping_) {
      /* For very large hold notes, shift the texture coordinates to be near 0,
       * so we don't send very large values to the renderer. */
      const float fDistFromTop = y_start_pos - part_args.y_top_;
      float fTexCoordTop = SCALE(
          fDistFromTop, 0, unzoomed_frame_height, rect.top_, rect.bottom_);
      fTexCoordTop += add_to_tex_coord;
      add_to_tex_coord -= std::floor(fTexCoordTop);
    }
  }
  // The bottom caps mysteriously hate me and their texture coords need to be
  // shifted by one pixel or there is a seam. -Kyz
  if (part_type == hpt_bottom) {
    if (!part_args.anchor_to_top_) {
      float offset =
          unzoomed_frame_height - (y_end_pos - y_start_pos) / ae_zoom;
      // ロングノート本体の長さがunzoomed_frame_height→0のときに、add_to_tex_coordを0→1にすればOK
      // つまり、offsetを0→unzoomed_frame_heightにすると理想通りの表示になる
      // -A.C Shift texture coord to fit hold length If hold length is less than
      // bottomcap frame height. (translated by hanubeki)
      if (offset > 0) {
        add_to_tex_coord =
            SCALE(offset, 0.0f, unzoomed_frame_height, 0.0f, 1.0f);
      } else {
        add_to_tex_coord = 0.0f;
      }
    }
  }

  DISPLAY->ClearAllTextures();

  const float fTexCoordLeft = rect.left_;
  const float fTexCoordRight = rect.right_;
  const float fTexCoordCenter = (fTexCoordLeft + fTexCoordRight) / 2;

  // pos_z_vec will be used later to orient the hold.  Read below. -Kyz
  static const RageVector3 pos_z_vec(0.0f, 0.0f, 1.0f);
  static const RageVector3 pos_y_vec(0.0f, 1.0f, 0.0f);
  StripBuffer queue;
  for (float fY = y_start_pos; !last_vert_set; fY += part_args.y_step_) {
    if (fY >= y_end_pos) {
      fY = y_end_pos;
      last_vert_set = true;
    }

    const float fYOffset = ArrowEffects::GetYOffsetFromYPos(
        column_args.column_, fY, y_reverse_offset_pixels_);

    ae_zoom =
        ArrowEffects::GetZoom(player_state_, fYOffset, column_args.column_);

    float cur_beat = part_args.top_beat_;
    if (part_args.top_beat_ != part_args.bottom_beat_) {
      cur_beat = SCALE(
          fY, part_args.y_top_, part_args.y_bottom_, part_args.top_beat_,
          part_args.bottom_beat_);
    }

    // Fun times ahead with vector math.  If the notes are being moved by the
    // position spline, the vectors used to position the edges of the strip
    // need to be adjusted or the hold will vanish when the notes move
    // horizontally.
    // To accomplish this, we use the derivative at the current point from
    // AE and the position spline.  That gives us the forward vector for the
    // strip, pointing to where the next center vert will be. (step 1)
    // The vectors pointing left and right to the edges of the strip are
    // obtained from the cross product of the forward vector and pos_z_vec.
    // (unless the forward vec is too close to pos_z_vec or -pos_z_vec, in
    // which case pos_y_vec is used)  The result of a cross product is a
    // vector perpendicular to both, so forward crossed with pos_z_vec gives
    // us the left vector.  Right is of course -left. (step 2)
    // After that step, the left and right vectors need to be rotated around
    // the forward vector axis by the y rotation value, to allow the hold to
    // twist. (step 3)
    // Steps will be labeled where they occur below. -Kyz
    // TODO:  Figure out whether it's worth the time investment to figure out
    // a way to skip the complex vector handling if the spline is disabled.

    RageVector3 sp_pos;
    RageVector3 sp_pos_forward;
    RageVector3 sp_rot;
    RageVector3 sp_zoom;
    RageVector3 ae_pos;
    RageVector3 ae_rot;

    // (step 1 of vector handling, part 1)
    // ArrowEffects only contributes to the Y component of the vector to
    // maintain the old behavior of how holds are drawn when they wave back
    // and forth. -Kyz
    RageVector3 render_forward(0.0f, 1.0f, 0.0f);
    column_args.spae_pos_for_beat(
        player_state_, cur_beat, fYOffset, y_reverse_offset_pixels_, sp_pos,
        ae_pos);
    // fX and fZ are sp_pos.x + ae_pos.x and sp_pos.z + ae_pos.z. -Kyz
    // fY is the actual y position that should be used, not whatever spae
    // fetched from ArrowEffects. -Kyz
    switch (column_args.pos_handler_->spline_mode_) {
      case NCSM_Disabled:
        ae_pos.y_ = fY + ArrowEffects::GetMoveY(column_args.column_);
        break;
      case NCSM_Offset:
        ae_pos.y_ = fY + ArrowEffects::GetMoveY(column_args.column_);
        column_args.pos_handler_->EvalDerivForBeat(
            column_args.song_beat_, cur_beat, sp_pos_forward);
        RageVec3Normalize(&sp_pos_forward, &sp_pos_forward);
        break;
      case NCSM_Position:
        ae_pos.y_ = 0.0f;
        render_forward.y_ = 0.0f;
        column_args.pos_handler_->EvalDerivForBeat(
            column_args.song_beat_, cur_beat, sp_pos_forward);
        RageVec3Normalize(&sp_pos_forward, &sp_pos_forward);
        break;
      default:
        break;
    }

    render_forward.x_ += sp_pos_forward.x_;
    render_forward.y_ += sp_pos_forward.y_;
    render_forward.z_ += sp_pos_forward.z_;
    // Normalize the vector so it'll be easy to test when determining whether
    // to use pos_z_vec or pos_y_vec for the cross product in step 2.
    RageVec3Normalize(&render_forward, &render_forward);

    // Holds are only affected by the x axis of the zoom spline because they
    // are flat sprites. -Kyz
    float render_width = fFrameWidth;
    switch (column_args.zoom_handler_->spline_mode_) {
      case NCSM_Disabled:
        render_width = fFrameWidth * ae_zoom;
        break;
      case NCSM_Offset:
        column_args.zoom_handler_->EvalForBeat(
            column_args.song_beat_, cur_beat, sp_zoom);
        render_width = fFrameWidth * (ae_zoom + sp_zoom.x_);
        break;
      case NCSM_Position:
        column_args.zoom_handler_->EvalForBeat(
            column_args.song_beat_, cur_beat, sp_zoom);
        render_width = fFrameWidth * sp_zoom.x_;
        break;
      default:
        break;
    }

    const float fFrameWidthScale = ArrowEffects::GetFrameWidthScale(
        player_state_, fYOffset, part_args.overlapped_time_);
    const float fScaledFrameWidth = render_width * fFrameWidthScale;

    // Can't use the same code as for taps because hold bodies can only rotate
    // around the y axis. -Kyz
    switch (column_args.rot_handler_->spline_mode_) {
      case NCSM_Disabled:
        // XXX: Actor rotations use degrees, Math uses radians. Convert here.
        ae_rot.y_ = ArrowEffects::GetRotationY(
                        player_state_, fYOffset, column_args.column_) *
                    -PI_180;
        break;
      case NCSM_Offset:
        ae_rot.y_ = ArrowEffects::GetRotationY(
                        player_state_, fYOffset, column_args.column_) *
                    -PI_180;
        column_args.rot_handler_->EvalForBeat(
            column_args.song_beat_, cur_beat, sp_rot);
        break;
      case NCSM_Position:
        column_args.rot_handler_->EvalForBeat(
            column_args.song_beat_, cur_beat, sp_rot);
        break;
      default:
        break;
    }

    RageVector3 center_vert(
        sp_pos.x_ + ae_pos.x_, sp_pos.y_ + ae_pos.y_, sp_pos.z_ + ae_pos.z_);

    // Special case for hold caps, which have the same top and bottom beat.
    if (part_args.top_beat_ == part_args.bottom_beat_ && !first_vert_set) {
      center_vert.x_ += render_forward.x_;
      center_vert.y_ += render_forward.y_;
      center_vert.z_ += render_forward.z_;
    }

    const float render_roty = (sp_rot.y_ + ae_rot.y_);

    // (step 2 of vector handling)
    RageVector3 render_left;
    if (std::abs(render_forward.z_) > 0.9f)  // 0.9 arbitrariliy picked.
    {
      RageVec3Cross(&render_left, &pos_y_vec, &render_forward);
    } else {
      RageVec3Cross(&render_left, &pos_z_vec, &render_forward);
    }
    RageAARotate(&render_left, &render_forward, render_roty);
    const float half_width = fScaledFrameWidth * .5f;
    render_left.x_ *= half_width;
    render_left.y_ *= half_width;
    render_left.z_ *= half_width;

    const RageVector3 left_vert(
        center_vert.x_ + render_left.x_, center_vert.y_ + render_left.y_,
        center_vert.z_ + render_left.z_);
    const RageVector3 right_vert(
        center_vert.x_ - render_left.x_, center_vert.y_ - render_left.y_,
        center_vert.z_ - render_left.z_);

    // Hack: because some mods mess with the zoom, we need to compensate
    // accordingly, or else hold ends don't look right.
    const float fPulseInnerAdj = ArrowEffects::GetPulseInner();
    const float fVariableZoom =
        ArrowEffects::GetZoomVariable(fYOffset, column_args.column_, 1) /
        fPulseInnerAdj;

    const float fDistFromTop = (fY - y_start_pos) / ae_zoom;
    float fTexCoordTop = SCALE(
        fDistFromTop, 0, unzoomed_frame_height, rect.top_,
        rect.bottom_ * fVariableZoom);
    fTexCoordTop += add_to_tex_coord;

    const float fAlpha = ArrowGetAlphaOrGlow(
        glow, player_state_, column_args.column_, fYOffset,
        part_args.percent_fade_to_fail_, y_reverse_offset_pixels_,
        field_args.draw_pixels_before_targets_,
        field_args.fade_before_targets_);
    const RageColor color = RageColor(
        column_args.diffuse_.r_ * color_scale,
        column_args.diffuse_.g_ * color_scale,
        column_args.diffuse_.b_ * color_scale,
        column_args.diffuse_.a_ * fAlpha);

    if (fAlpha > 0) {
      bAllAreTransparent = false;
    }

    queue.v_[0].p_ = left_vert;
    queue.v_[0].c_ = color;
    queue.v_[0].t_ = RageVector2(fTexCoordLeft, fTexCoordTop);
    queue.v_[1].p_ = center_vert;
    queue.v_[1].c_ = color;
    queue.v_[1].t_ = RageVector2(fTexCoordCenter, fTexCoordTop);
    queue.v_[2].p_ = right_vert;
    queue.v_[2].c_ = color;
    queue.v_[2].t_ = RageVector2(fTexCoordRight, fTexCoordTop);
    queue.v_ += 3;

    if (queue.Free() < 3 || last_vert_set) {
      /* The queue is full.  Render it, clear the buffer, and move back a step
       * to start off the strip again. */
      if (!bAllAreTransparent) {
        int i = 0;
        for (Sprite* spr : vpSpr) {
          RageTexture* pTexture = spr->GetTexture();
          DISPLAY->SetTexture(TextureUnit_1, pTexture->GetTexHandle());
          DISPLAY->SetBlendMode((i++ == 0) ? BLEND_NORMAL : BLEND_ADD);
          DISPLAY->SetCullMode(CULL_NONE);
          DISPLAY->SetTextureWrapping(TextureUnit_1, part_args.wrapping_);
          queue.Draw();
        }
      }
      queue.Init();
      bAllAreTransparent = true;
      fY -= part_args.y_step_;
    }
    first_vert_set = false;
  }
}

void NoteDisplay::DrawHoldBodyInternal(
    std::vector<Sprite*>& sprite_top, std::vector<Sprite*>& sprite_body,
    std::vector<Sprite*>& sprite_bottom, const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args, draw_hold_part_args& part_args,
    const float head_minus_top, const float tail_plus_bottom,
    const float y_head, const float y_tail, const float top_beat,
    const float bottom_beat, bool glow) {
  // Draw the top cap
  part_args.y_top_ = head_minus_top;
  part_args.y_bottom_ = y_head;
  part_args.top_beat_ = top_beat;
  part_args.bottom_beat_ = top_beat;
  part_args.wrapping_ = false;
  // Negative scrolls can move the opposite end past this cap. Clip it just
  // like the bottom cap below, so reverse holds don't leave a floating tail.
  const float original_y_end_pos = part_args.y_end_pos_;
  part_args.y_end_pos_ = std::fmin(part_args.y_end_pos_, y_tail);
  DrawHoldPart(sprite_top, field_args, column_args, part_args, glow, hpt_top);
  part_args.y_end_pos_ = original_y_end_pos;
  // Draw the body
  part_args.y_top_ = y_head;
  part_args.y_bottom_ = y_tail;
  part_args.bottom_beat_ = bottom_beat;
  part_args.wrapping_ = true;
  DrawHoldPart(sprite_body, field_args, column_args, part_args, glow, hpt_body);
  // Draw the bottom cap
  float overlap_hack = 1.0f;
  part_args.y_top_ = y_tail + overlap_hack;
  part_args.y_bottom_ = tail_plus_bottom + overlap_hack;
  part_args.top_beat_ = bottom_beat;
  part_args.y_start_pos_ = std::fmax(part_args.y_start_pos_, y_head);
  part_args.wrapping_ = false;
  DrawHoldPart(
      sprite_bottom, field_args, column_args, part_args, glow, hpt_bottom);
}

void NoteDisplay::DrawHoldBody(
    const TapNote& tn, const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args, float beat, bool being_held,
    float y_head, float y_tail, float percent_fade_to_fail, float color_scale,
    float top_beat, float bottom_beat) {
  draw_hold_part_args part_args;
  part_args.percent_fade_to_fail_ = percent_fade_to_fail;
  part_args.color_scale_ = color_scale;
  part_args.overlapped_time_ = tn.hold_result_.overlapped_time_;
  std::vector<Sprite*> vpSprTop;
  Sprite* pSpriteTop = GetHoldSprite(
      hold_top_cap_, NotePart_HoldTopCap, beat,
      tn.sub_type_ == TapNoteSubType_Roll,
      being_held && !cache_->hold_active_is_add_layer_);
  vpSprTop.push_back(pSpriteTop);

  std::vector<Sprite*> vpSprBody;
  Sprite* pSpriteBody = GetHoldSprite(
      hold_body_, NotePart_HoldBody, beat, tn.sub_type_ == TapNoteSubType_Roll,
      being_held && !cache_->hold_active_is_add_layer_);
  vpSprBody.push_back(pSpriteBody);

  std::vector<Sprite*> vpSprBottom;
  Sprite* pSpriteBottom = GetHoldSprite(
      hold_bottom_cap_, NotePart_HoldBottomCap, beat,
      tn.sub_type_ == TapNoteSubType_Roll,
      being_held && !cache_->hold_active_is_add_layer_);
  vpSprBottom.push_back(pSpriteBottom);

  if (being_held && cache_->hold_active_is_add_layer_) {
    Sprite* pSprTop = GetHoldSprite(
        hold_top_cap_, NotePart_HoldTopCap, beat,
        tn.sub_type_ == TapNoteSubType_Roll, true);
    vpSprTop.push_back(pSprTop);
    Sprite* pSprBody = GetHoldSprite(
        hold_body_, NotePart_HoldBody, beat,
        tn.sub_type_ == TapNoteSubType_Roll, true);
    vpSprBody.push_back(pSprBody);
    Sprite* pSprBottom = GetHoldSprite(
        hold_bottom_cap_, NotePart_HoldBottomCap, beat,
        tn.sub_type_ == TapNoteSubType_Roll, true);
    vpSprBottom.push_back(pSprBottom);
  }

  const bool reverse =
      player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
          column_args.column_) > 0.5f;
  part_args.flip_texture_vertically_ =
      reverse && cache_->flip_hold_body_when_reverse_;
  if (part_args.flip_texture_vertically_) {
    std::swap(vpSprTop, vpSprBottom);
    std::swap(pSpriteTop, pSpriteBottom);
  }

  const bool bWavyPartsNeedZBuffer = ArrowEffects::NeedZBuffer();
  DISPLAY->SetZTestMode(
      bWavyPartsNeedZBuffer ? ZTEST_WRITE_ON_PASS : ZTEST_OFF);
  DISPLAY->SetZWrite(bWavyPartsNeedZBuffer);

  // Hack: Z effects need a finer grain step.
  part_args.y_step_ =
      bWavyPartsNeedZBuffer ? 4 : 16;  // use small steps only if wavy

  if (part_args.flip_texture_vertically_) {
    y_head -= cache_->stop_drawing_hold_body_offset_from_tail_;
    y_tail -= cache_->start_drawing_hold_body_offset_from_head_;
  } else {
    y_head += cache_->start_drawing_hold_body_offset_from_head_;
    y_tail += cache_->stop_drawing_hold_body_offset_from_tail_;
  }

  float ae_zoom = ArrowEffects::GetZoom(player_state_, 0, column_args.column_);
  const float frame_height_top = pSpriteTop->GetUnzoomedHeight() * ae_zoom;
  const float frame_height_bottom =
      pSpriteBottom->GetUnzoomedHeight() * ae_zoom;

  part_args.y_start_pos_ = ArrowEffects::GetYPos(
      player_state_, column_args.column_, field_args.draw_pixels_after_targets_,
      y_reverse_offset_pixels_);
  part_args.y_end_pos_ = ArrowEffects::GetYPos(
      player_state_, column_args.column_,
      field_args.draw_pixels_before_targets_, y_reverse_offset_pixels_);
  if (reverse) {
    std::swap(part_args.y_start_pos_, part_args.y_end_pos_);
  }
  // So that part_args.y_start_pos can be changed when drawing the bottom.
  const float original_y_start_pos = part_args.y_start_pos_;
  const float head_minus_top = y_head - frame_height_top;
  const float tail_plus_bottom = y_tail + frame_height_bottom;

  part_args.anchor_to_top_ = reverse && cache_->top_hold_anchor_when_reverse_;

  DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);
  DrawHoldBodyInternal(
      vpSprTop, vpSprBody, vpSprBottom, field_args, column_args, part_args,
      head_minus_top, tail_plus_bottom, y_head, y_tail, top_beat, bottom_beat,
      false);
  part_args.y_start_pos_ = original_y_start_pos;
  DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Glow);
  DrawHoldBodyInternal(
      vpSprTop, vpSprBody, vpSprBottom, field_args, column_args, part_args,
      head_minus_top, tail_plus_bottom, y_head, y_tail, top_beat, bottom_beat,
      true);
}

void NoteDisplay::DrawHold(
    const TapNote& tn, const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args, int iRow, bool bIsBeingHeld,
    const HoldNoteResult& Result, bool bIsAddition, float fPercentFadeToFail) {
  int iEndRow = iRow + tn.duration_;
  float top_beat = NoteRowToVisibleBeat(player_state_, iRow);
  float bottom_beat = NoteRowToVisibleBeat(player_state_, iEndRow);
  if (bIsBeingHeld) {
    top_beat = column_args.song_beat_;
  }

  // bDrawGlowOnly is a little hacky.  We need to draw the diffuse part and the
  // glow part one pass at a time to minimize state changes

  bool bReverse =
      player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
          column_args.column_) > 0.5f;
  float fStartBeat =
      NoteRowToBeat(std::max(tn.hold_result_.last_held_row_, iRow));
  float fThrowAway = 0;

  // HACK: If life > 0, don't set YOffset to 0 so that it doesn't jiggle around
  // the receptor.
  bool bStartIsPastPeak = true;
  float fStartYOffset = 0;
  if (tn.hold_result_.active_ && tn.hold_result_.life_ > 0)
    ;  // use the default values filled in above
  else {
    fStartYOffset = ArrowEffects::GetYOffset(
        player_state_, column_args.column_, fStartBeat, fThrowAway,
        bStartIsPastPeak);
  }

  float fEndPeakYOffset = 0;
  bool bEndIsPastPeak = false;
  float fEndYOffset = ArrowEffects::GetYOffset(
      player_state_, column_args.column_, NoteRowToBeat(iEndRow),
      fEndPeakYOffset, bEndIsPastPeak);

  // In boomerang, the arrows reverse direction at Y offset value
  // fPeakAtYOffset. If fPeakAtYOffset lies inside of the hold we're drawing,
  // then the we want to draw the tail at that max Y offset, or else the hold
  // will appear to magically grow as the tail approaches the max Y offset.
  if (bStartIsPastPeak && !bEndIsPastPeak) {
    fEndYOffset = fEndPeakYOffset;  // use the calculated PeakYOffset so that
                                    // long holds don't appear to grow
  }

  // Swap in reverse, so fStartYOffset is always the offset higher on the
  // screen.
  if (bReverse) {
    std::swap(fStartYOffset, fEndYOffset);
  }

  const float fYHead = ArrowEffects::GetYPos(
      player_state_, column_args.column_, fStartYOffset,
      y_reverse_offset_pixels_);
  const float fYTail = ArrowEffects::GetYPos(
      player_state_, column_args.column_, fEndYOffset,
      y_reverse_offset_pixels_);

  const float fColorScale = SCALE(
      tn.hold_result_.life_, 0.0f, 1.0f, cache_->hold_let_go_gray_percent_,
      1.0f);

  bool bFlipHeadAndTail = bReverse && cache_->flip_head_and_tail_when_reverse_;

  /* The body and caps should have no overlap, so their order doesn't matter.
   * Draw the head last, so it appears on top. */
  float fBeat = NoteRowToBeat(iRow);
  // Side note:  I don't know why these two checks were commented out and I
  // didn't bother to update them when rewriting the arguments that are
  // passed to the note drawing functions. -Kyz
  /*
  if( !cache->m_bHoldHeadIsAboveWavyParts )
  {
          Actor *pActor = GetHoldActor( m_HoldHead, NotePart_HoldHead,
  NoteRowToBeat(iRow), tn.subType == TapNoteSubType_Roll, bIsBeingHeld );
          DrawActor( tn, pActor, NotePart_HoldHead, iCol, bFlipHeadAndTail ?
  fEndYOffset : fStartYOffset, fBeat, bIsAddition, fPercentFadeToFail,
  fReverseOffsetPixels, fColorScale, fDrawDistanceAfterTargetsPixels,
  fDrawDistanceBeforeTargetsPixels, fFadeInPercentOfDrawFar );
  }
  if( !cache->m_bHoldTailIsAboveWavyParts )
  {
          Actor *pActor = GetHoldActor( m_HoldTail, NotePart_HoldTail,
  NoteRowToBeat(iRow), tn.subType == TapNoteSubType_Roll, bIsBeingHeld );
          DrawActor( tn, pActor, NotePart_HoldTail, iCol, bFlipHeadAndTail ?
  fStartYOffset : fEndYOffset, fBeat, bIsAddition, fPercentFadeToFail,
  fReverseOffsetPixels, fColorScale, fDrawDistanceAfterTargetsPixels,
  fDrawDistanceBeforeTargetsPixels, fFadeInPercentOfDrawFar );
  }
  */

  DrawHoldBody(
      tn, field_args, column_args, fBeat, bIsBeingHeld, fYHead, fYTail,
      fPercentFadeToFail, fColorScale, top_beat, bottom_beat);

  /* These set the texture mode themselves. */
  // this part was modified in pumpmania, where it flips the draw order
  // of the head and tail. Perhaps make this a theme/noteskin metric? -aj
  if (cache_->hold_tail_is_above_wavy_parts_) {
    Actor* pActor = GetHoldActor(
        hold_tail_, NotePart_HoldTail, NoteRowToBeat(iRow),
        tn.sub_type_ == TapNoteSubType_Roll, bIsBeingHeld);
    DrawActor(
        tn, pActor, NotePart_HoldTail, field_args, column_args,
        bFlipHeadAndTail ? fStartYOffset : fEndYOffset, fBeat, bIsAddition,
        fPercentFadeToFail, fColorScale, false);
  }
  if (cache_->hold_head_is_above_wavy_parts_) {
    Actor* pActor = GetHoldActor(
        hold_head_, NotePart_HoldHead, NoteRowToBeat(iRow),
        tn.sub_type_ == TapNoteSubType_Roll, bIsBeingHeld);
    DrawActor(
        tn, pActor, NotePart_HoldHead, field_args, column_args,
        bFlipHeadAndTail ? fEndYOffset : fStartYOffset, fBeat, bIsAddition,
        fPercentFadeToFail, fColorScale, bIsBeingHeld);
  }
}

void NoteDisplay::DrawActor(
    const TapNote& tn, Actor* pActor, NotePart part,
    const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args, float fYOffset, float fBeat,
    bool bIsAddition, float fPercentFadeToFail, float fColorScale,
    bool is_being_held) {
  if (tn.type_ == TapNoteType_AutoKeysound && !GAMESTATE->in_step_editor_) {
    return;
  }
  if (fYOffset < field_args.draw_pixels_after_targets_ ||
      fYOffset > field_args.draw_pixels_before_targets_) {
    return;
  }
  float spline_beat = fBeat;
  if (is_being_held) {
    spline_beat = column_args.song_beat_;
  }

  const float fAlpha = ArrowEffects::GetAlpha(
      player_state_, column_args.column_, fYOffset, fPercentFadeToFail,
      y_reverse_offset_pixels_, field_args.draw_pixels_before_targets_,
      field_args.fade_before_targets_);
  const float fGlow = ArrowEffects::GetGlow(
      player_state_, column_args.column_, fYOffset, fPercentFadeToFail,
      y_reverse_offset_pixels_, field_args.draw_pixels_before_targets_,
      field_args.fade_before_targets_);
  const RageColor diffuse = RageColor(
      column_args.diffuse_.r_ * fColorScale,
      column_args.diffuse_.g_ * fColorScale,
      column_args.diffuse_.b_ * fColorScale, column_args.diffuse_.a_ * fAlpha);
  const RageColor glow = RageColor(1, 1, 1, fGlow);
  // We can't actually use the glow color from the effect on the colum actor
  // because it's used by the stealth modifier. -Kyz
  /*
  const RageColor glow	= RageColor(
          column_args.glow.r * fColorScale,
          column_args.glow.g * fColorScale,
          column_args.glow.b * fColorScale,
          column_args.glow.a * fGlow);
  */

  bool bIsHoldHead = tn.type_ == TapNoteType_HoldHead;
  bool bIsHoldCap = bIsHoldHead || tn.type_ == TapNoteType_HoldTail;

  // So, thie call to GetBrightness does nothing because fColorScale is not
  // used after this point.  If you read GetBrightness, it looks like it's
  // meant to fade the note to black, so a note that is one beat past the
  // receptors is black.  However, I looked through the github history and
  // it's been down here, disabled, since at least SM5 beta 1a.  I don't
  // know if we should bring that behavior back now. -Kyz
  if (tn.type_ != TapNoteType_HoldHead) {
    fColorScale *= ArrowEffects::GetBrightness(player_state_, fBeat);
  }

  // same logical structure as in UpdateReceptorGhostStuff, I just haven't
  // figured out a good way to combine them. -Kyz
  RageVector3 sp_pos;
  RageVector3 sp_rot;
  RageVector3 sp_zoom;
  RageVector3 ae_pos;
  RageVector3 ae_rot;
  RageVector3 ae_zoom;
  column_args.spae_pos_for_beat(
      player_state_, spline_beat, fYOffset, y_reverse_offset_pixels_, sp_pos,
      ae_pos);

  switch (column_args.rot_handler_->spline_mode_) {
    case NCSM_Disabled:
      ae_rot.x_ = ArrowEffects::GetRotationX(
          player_state_, fYOffset, bIsHoldCap, column_args.column_);
      ae_rot.y_ = ArrowEffects::GetRotationY(
          player_state_, fYOffset, column_args.column_);
      ae_rot.z_ = ArrowEffects::GetRotationZ(
          player_state_, fBeat, bIsHoldHead, column_args.column_);
      break;
    case NCSM_Offset:
      ae_rot.x_ = ArrowEffects::GetRotationX(
          player_state_, fYOffset, bIsHoldCap, column_args.column_);
      ae_rot.y_ = ArrowEffects::GetRotationY(
          player_state_, fYOffset, column_args.column_);
      ae_rot.z_ = ArrowEffects::GetRotationZ(
          player_state_, fBeat, bIsHoldHead, column_args.column_);
      column_args.rot_handler_->EvalForBeat(
          column_args.song_beat_, spline_beat, sp_rot);
      break;
    case NCSM_Position:
      column_args.rot_handler_->EvalForBeat(
          column_args.song_beat_, spline_beat, sp_rot);
      break;
    default:
      break;
  }
  column_args.spae_zoom_for_beat(
      player_state_, spline_beat, sp_zoom, ae_zoom, column_args.column_,
      fYOffset);
  column_args.SetPRZForActor(
      pActor, sp_pos, ae_pos, sp_rot, ae_rot, sp_zoom, ae_zoom);
  // [AJ] this two lines (and how they're handled) piss off many people:
  pActor->SetDiffuse(diffuse);
  pActor->SetGlow(glow);

  bool bNeedsTranslate =
      (bIsAddition &&
       !IsVectorZero(cache_->addition_texture_coord_offset_[part])) ||
      !IsVectorZero(cache_->note_color_texture_coord_spacing_[part]);
  if (bNeedsTranslate) {
    DISPLAY->TexturePushMatrix();
    float color = 0.0f;
    // this is only used for ProgressAlternate but must be declared here
    float fScaledBeat = 0.0f;
    switch (cache_->note_color_type_[part]) {
      case NoteColorType_Denominator:
        color = float(BeatToNoteType(fBeat));
        color = std::clamp(
            color, 0.0f, (float)(cache_->note_color_count_[part] - 1));
        break;
      case NoteColorType_Progress:
        color = std::fmod(
            std::ceil(fBeat * cache_->note_color_count_[part]),
            (float)cache_->note_color_count_[part]);
        break;
      case NoteColorType_ProgressAlternate:
        fScaledBeat = fBeat * cache_->note_color_count_[part];
        if (fScaledBeat - int64_t(fScaledBeat) == 0.0f) {
          // we're on a boundary, so move to the previous frame.
          // doing it this way ensures that fScaledBeat is never negative so
          // std::fmod works.
          fScaledBeat += cache_->note_color_count_[part] - 1;
        }
        color = std::fmod(
            std::ceil(fScaledBeat), (float)cache_->note_color_count_[part]);
        break;
      default:
        FAIL_M(ssprintf(
            "Invalid NoteColorType: %i", cache_->note_color_type_[part]));
    }
    DISPLAY->TextureTranslate(
        (bIsAddition ? cache_->addition_texture_coord_offset_[part]
                     : RageVector2(0, 0)) +
        cache_->note_color_texture_coord_spacing_[part] * color);
  }

  pActor->Draw();

  if (bNeedsTranslate) {
    DISPLAY->TexturePopMatrix();
  }
}

void NoteDisplay::DrawTap(
    const TapNote& tn, const NoteFieldRenderArgs& field_args,
    const NoteColumnRenderArgs& column_args, float fBeat,
    bool bOnSameRowAsHoldStart, bool bOnSameRowAsRollStart, bool bIsAddition,
    float fPercentFadeToFail) {
  Actor* pActor = nullptr;
  NotePart part = NotePart_Tap;
  /*
  if( tn.source == TapNoteSource_Addition )
  {
          pActor = GetTapActor( m_TapAddition, NotePart_Addition, fBeat );
          part = NotePart_Addition;
  }
  */
  if (tn.type_ == TapNoteType_Lift) {
    pActor = GetTapActor(tap_lift_, NotePart_Lift, fBeat);
    part = NotePart_Lift;
  } else if (tn.type_ == TapNoteType_Mine) {
    pActor = GetTapActor(tap_mine_, NotePart_Mine, fBeat);
    part = NotePart_Mine;
  } else if (tn.type_ == TapNoteType_Fake) {
    pActor = GetTapActor(tap_fake_, NotePart_Fake, fBeat);
    part = NotePart_Fake;
  }
  // TODO: Simplify all of the below.
  else if (bOnSameRowAsHoldStart && bOnSameRowAsRollStart) {
    if (cache_->draw_hold_head_for_taps_on_same_row_ &&
        cache_->draw_roll_head_for_taps_on_same_row_) {
      if (cache_->tap_hold_roll_on_row_means_hold_)  // another new metric?
      {
        pActor =
            GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, false, false);
      } else {
        pActor =
            GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, true, false);
      }
    } else if (cache_->draw_hold_head_for_taps_on_same_row_) {
      pActor = GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, false, false);
    } else if (cache_->draw_roll_head_for_taps_on_same_row_) {
      pActor = GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, true, false);
    }
  }

  else if (
      bOnSameRowAsHoldStart && cache_->draw_hold_head_for_taps_on_same_row_) {
    pActor = GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, false, false);
  }

  else if (
      bOnSameRowAsRollStart && cache_->draw_roll_head_for_taps_on_same_row_) {
    pActor = GetHoldActor(hold_head_, NotePart_HoldHead, fBeat, true, false);
  }

  else {
    pActor = GetTapActor(tap_note_, NotePart_Tap, fBeat);
  }

  if (tn.type_ == TapNoteType_Attack) {
    Message msg("SetAttack");
    msg.SetParam("Modifiers", tn.attack_modifiers_);
    pActor->HandleMessage(msg);
  }

  const float fYOffset =
      ArrowEffects::GetYOffset(player_state_, column_args.column_, fBeat);
  // this is the line that forces the (1,1,1,x) part of the noteskin diffuse -aj
  DrawActor(
      tn, pActor, part, field_args, column_args, fYOffset, fBeat, bIsAddition,
      fPercentFadeToFail, 1.0f, false);

  if (tn.type_ == TapNoteType_Attack) {
    pActor->PlayCommand("UnsetAttack");
  }
}

void NoteColumnRenderer::UpdateReceptorGhostStuff(Actor* receptor) const {
  const PlayerState* player_state = field_render_args_->player_state_;
  float song_beat = player_state->GetDisplayedPosition().song_beat_visible_;
  // sp_* will be filled with the settings from the splines.
  // ae_* will be filled with the settings from ArrowEffects.
  // The two together will be applied to the actor.
  // sp_* will be zeroes in NCSM_Disabled, and ae_* will be zeroes in
  // NCSM_Position, so the setting step won't have to check the mode. -Kyz
  // sp_* are sized by the spline evaluate function.
  RageVector3 sp_pos;
  RageVector3 sp_rot;
  RageVector3 sp_zoom;
  RageVector3 ae_pos;
  RageVector3 ae_rot;
  RageVector3 ae_zoom;
  switch (ncr_current_.pos_handler_.spline_mode_) {
    case NCSM_Disabled:
      ArrowEffects::GetXYZPos(
          player_state, column_, 0, field_render_args_->reverse_offset_pixels_,
          ae_pos);
      break;
    case NCSM_Offset:
      ArrowEffects::GetXYZPos(
          player_state, column_, 0, field_render_args_->reverse_offset_pixels_,
          ae_pos);
      ncr_current_.pos_handler_.EvalForReceptor(song_beat, sp_pos);
      break;
    case NCSM_Position:
      ncr_current_.pos_handler_.EvalForReceptor(song_beat, sp_pos);
      break;
    default:
      break;
  }
  switch (ncr_current_.rot_handler_.spline_mode_) {
    case NCSM_Disabled:
      ae_rot.z_ = ArrowEffects::ReceptorGetRotationZ(player_state, column_);
      ae_rot.x_ = ArrowEffects::ReceptorGetRotationX(player_state, column_);
      ae_rot.y_ = ArrowEffects::ReceptorGetRotationY(player_state, column_);
      break;
    case NCSM_Offset:
      ae_rot.z_ = ArrowEffects::ReceptorGetRotationZ(player_state, column_);
      ae_rot.x_ = ArrowEffects::ReceptorGetRotationX(player_state, column_);
      ae_rot.y_ = ArrowEffects::ReceptorGetRotationY(player_state, column_);
      ncr_current_.rot_handler_.EvalForReceptor(song_beat, sp_rot);
      break;
    case NCSM_Position:
      ncr_current_.rot_handler_.EvalForReceptor(song_beat, sp_rot);
      break;
    default:
      break;
  }
  switch (ncr_current_.zoom_handler_.spline_mode_) {
    case NCSM_Disabled:
      ae_zoom.x_ = ae_zoom.y_ = ae_zoom.z_ =
          ArrowEffects::GetZoom(player_state, 0, column_);
      break;
    case NCSM_Offset:
      ae_zoom.x_ = ae_zoom.y_ = ae_zoom.z_ =
          ArrowEffects::GetZoom(player_state, 0, column_);
      ncr_current_.zoom_handler_.EvalForReceptor(song_beat, sp_zoom);
      break;
    case NCSM_Position:
      ncr_current_.zoom_handler_.EvalForReceptor(song_beat, sp_zoom);
      break;
    default:
      break;
  }
  column_render_args_.SetPRZForActor(
      receptor, sp_pos, ae_pos, sp_rot, ae_rot, sp_zoom, ae_zoom);
  receptor->SetInternalDiffuse(column_render_args_.diffuse_);
  receptor->SetInternalGlow(column_render_args_.glow_);
}

void NoteColumnRenderer::DrawPrimitives() {
  column_render_args_.song_beat_ =
      field_render_args_->player_state_->GetDisplayedPosition()
          .song_beat_visible_;
  column_render_args_.pos_handler_ = &ncr_current_.pos_handler_;
  column_render_args_.rot_handler_ = &ncr_current_.rot_handler_;
  column_render_args_.zoom_handler_ = &ncr_current_.zoom_handler_;
  column_render_args_.diffuse_ = temp_state_->diffuse_[0];
  column_render_args_.glow_ = temp_state_->glow_;
  bool any_upcoming = false;
  // Build lists of holds and taps for each player number, then pass those
  // lists to the displays to draw.
  // The vector in the NUM_PlayerNumber slot should stay empty, not worth
  // optimizing it out. -Kyz
  std::vector<std::vector<NoteData::TrackMap::const_iterator>> holds(
      PLAYER_INVALID + 1);
  std::vector<std::vector<NoteData::TrackMap::const_iterator>> taps(
      PLAYER_INVALID + 1);
  NoteData::TrackMap::const_iterator begin, end;
  field_render_args_->note_data_->GetTapNoteRangeInclusive(
      column_, field_render_args_->first_row_,
      field_render_args_->last_row_ + 1, begin, end);
  for (; begin != end; ++begin) {
    const TapNote& tn = begin->second;
    switch (tn.type_) {
      case TapNoteType_Empty:
        continue;
      case TapNoteType_Tap:
      case TapNoteType_HoldTail:
      case TapNoteType_Mine:
      case TapNoteType_Lift:
      case TapNoteType_Attack:
      case TapNoteType_AutoKeysound:
      case TapNoteType_Fake:
        if (!tn.result_.hidden_) {
          taps[tn.pn_].push_back(begin);
        }
        break;
      case TapNoteType_HoldHead:
        if (tn.hold_result_.hns_ != HNS_Held) {
          holds[tn.pn_].push_back(begin);
        }
        break;
      default:
        break;
    }
  }
#define DTS_INNER(pn, tap_set, draw_func, disp)                 \
  if (!tap_set[pn].empty()) {                                   \
    any_upcoming |= disp->draw_func(                            \
        *field_render_args_, column_render_args_, tap_set[pn]); \
  }
#define DRAW_TAP_SET(tap_set, draw_func) \
  FOREACH_PlayerNumber(pn) { DTS_INNER(pn, tap_set, draw_func, displays_[pn]); }

  DRAW_TAP_SET(holds, DrawHoldsInRange);
  DTS_INNER(PLAYER_INVALID, holds, DrawHoldsInRange, displays_[PLAYER_INVALID]);
  DRAW_TAP_SET(taps, DrawTapsInRange);
  DTS_INNER(PLAYER_INVALID, taps, DrawTapsInRange, displays_[PLAYER_INVALID]);
#undef DTS_INNER
#undef DRAW_TAP_SET
  field_render_args_->receptor_row_->SetNoteUpcoming(column_, any_upcoming);
}

void NoteColumnRenderer::SetCurrentTweenStart() { ncr_start_ = ncr_current_; }

void NoteColumnRenderer::EraseHeadTween() {
  ncr_current_ = ncr_tweens_[0];
  ncr_tweens_.erase(ncr_tweens_.begin());
}

void NoteColumnRenderer::UpdatePercentThroughTween(float between) {
  NCR_TweenState::MakeWeightedAverage(
      ncr_current_, ncr_start_, ncr_tweens_[0], between);
}

void NoteColumnRenderer::BeginTweening(float time, ITween* interp) {
  Actor::BeginTweening(time, interp);
  if (!ncr_tweens_.empty()) {
    ncr_tweens_.push_back(ncr_tweens_.back());
  } else {
    ncr_tweens_.push_back(ncr_current_);
  }
}

void NoteColumnRenderer::StopTweening() {
  ncr_tweens_.clear();
  Actor::StopTweening();
}

void NoteColumnRenderer::FinishTweening() {
  if (!ncr_tweens_.empty()) {
    ncr_current_ = NCR_DestTweenState();
  }
  Actor::FinishTweening();
}

NoteColumnRenderer::NCR_TweenState::NCR_TweenState() {
  rot_handler_.spline_.set_spatial_extent(0, PI * 2.0f);
  rot_handler_.spline_.set_spatial_extent(1, PI * 2.0f);
  rot_handler_.spline_.set_spatial_extent(2, PI * 2.0f);
}

void NoteColumnRenderer::NCR_TweenState::MakeWeightedAverage(
    NCR_TweenState& out, const NCR_TweenState& from, const NCR_TweenState& to,
    float between) {
#define WEIGHT_FOR_ME(me) \
  NCSplineHandler::MakeWeightedAverage(out.me, from.me, to.me, between);
  WEIGHT_FOR_ME(pos_handler_);
  WEIGHT_FOR_ME(rot_handler_);
  WEIGHT_FOR_ME(zoom_handler_);
#undef WEIGHT_FOR_ME
}

#include "LuaBinding.h"

struct LunaNCSplineHandler : Luna<NCSplineHandler> {
  static int get_spline(T* p, lua_State* L) {
    p->spline_.PushSelf(L);
    return 1;
  }
  DEFINE_METHOD(get_receptor_t, receptor_t_);
  DEFINE_METHOD(get_beats_per_t, beats_per_t_);
#define SET_T(member, name)             \
  static int name(T* p, lua_State* L) { \
    p->member = FArg(1);                \
    COMMON_RETURN_SELF;                 \
  }
#define SET_B(member, name)             \
  static int name(T* p, lua_State* L) { \
    p->member = BArg(1);                \
    COMMON_RETURN_SELF;                 \
  }
  SET_T(receptor_t_, set_receptor_t);
  SET_T(beats_per_t_, set_beats_per_t);
  SET_B(subtract_song_beat_from_curr_, set_subtract_song_beat);
#undef SET_T
#undef SET_B
  static int set_spline_mode(T* p, lua_State* L) {
    p->spline_mode_ = Enum::Check<NoteColumnSplineMode>(L, 1);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(get_spline_mode, spline_mode_);
  DEFINE_METHOD(get_subtract_song_beat, subtract_song_beat_from_curr_);

  LunaNCSplineHandler() {
    ADD_METHOD(get_spline);
    ADD_METHOD(get_beats_per_t);
    ADD_METHOD(set_beats_per_t);
    ADD_METHOD(get_receptor_t);
    ADD_METHOD(set_receptor_t);
    ADD_METHOD(get_spline_mode);
    ADD_METHOD(set_spline_mode);
    ADD_METHOD(get_subtract_song_beat);
    ADD_METHOD(set_subtract_song_beat);
  }
};

LUA_REGISTER_CLASS(NCSplineHandler);

struct LunaNoteColumnRenderer : Luna<NoteColumnRenderer> {
#define GET_HANDLER(member, name)       \
  static int name(T* p, lua_State* L) { \
    p->member->PushSelf(L);             \
    return 1;                           \
  }
  GET_HANDLER(GetPosHandler(), get_pos_handler);
  GET_HANDLER(GetRotHandler(), get_rot_handler);
  GET_HANDLER(GetZoomHandler(), get_zoom_handler);
#undef GET_HANDLER

  LunaNoteColumnRenderer() {
    ADD_METHOD(get_pos_handler);
    ADD_METHOD(get_rot_handler);
    ADD_METHOD(get_zoom_handler);
  }
};

LUA_REGISTER_DERIVED_CLASS(NoteColumnRenderer, Actor)

/*
 * NoteColumnRenderer and associated spline stuff (c) Eric Reese 2014-2015
 * (c) 2001-2006 Brian Bugh, Ben Nordstrom, Chris Danford, Steve Checkoway
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
