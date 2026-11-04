#include "ScreenSyncOverlay.h"

#include <string>
#include <vector>

#include "AdjustSync.h"
#include "GameConstantsAndTypes.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongOptions.h"
#include "ThemeManager.h"
#include "TimingSegments.h"
#include "global.h"

static bool IsGameplay() {
  return SCREENMAN && SCREENMAN->GetTopScreen() &&
         SCREENMAN->GetTopScreen()->GetScreenType() == gameplay;
}

REGISTER_SCREEN_CLASS(ScreenSyncOverlay);

void ScreenSyncOverlay::Init() {
  Screen::Init();

  overlay_.Load(THEME->GetPathB(name_, "overlay"));
  AddChild(overlay_);

  Update(0);
}

void ScreenSyncOverlay::Update(float fDeltaTime) {
  this->SetVisible(IsGameplay());
  if (!IsGameplay()) {
    HideHelp();
    return;
  }

  Screen::Update(fDeltaTime);

  UpdateText();
}

static LocalizedString AUTO_PLAY("ScreenSyncOverlay", "AutoPlay");
static LocalizedString AUTO_PLAY_CPU("ScreenSyncOverlay", "AutoPlayCPU");
static LocalizedString AUTO_SYNC_SONG("ScreenSyncOverlay", "AutoSync Song");
static LocalizedString AUTO_SYNC_MACHINE(
    "ScreenSyncOverlay", "AutoSync Machine");
static LocalizedString AUTO_SYNC_TEMPO("ScreenSyncOverlay", "AutoSync Tempo");
static LocalizedString OLD_OFFSET("ScreenSyncOverlay", "Old offset");
static LocalizedString NEW_OFFSET("ScreenSyncOverlay", "New offset");
static LocalizedString COLLECTING_SAMPLE(
    "ScreenSyncOverlay", "Collecting sample");
static LocalizedString STANDARD_DEVIATION(
    "ScreenSyncOverlay", "Standard deviation");
void ScreenSyncOverlay::UpdateText() {
  // Update Status
  std::vector<std::string> vs;

  PlayerController pc = GamePreferences::m_AutoPlay.Get();
  switch (pc) {
    case PC_HUMAN:
      break;
    case PC_AUTOPLAY:
      vs.push_back(AUTO_PLAY);
      break;
    case PC_CPU:
      vs.push_back(AUTO_PLAY_CPU);
      break;
    default:
      FAIL_M(ssprintf("Invalid PlayerController: %i", pc));
  }

  if (GAMESTATE->song_options_.GetCurrent().assist_clap_) {
    vs.push_back("Assist Clap");
  }

  AutosyncType type = GAMESTATE->song_options_.GetCurrent().autosync_type_;
  switch (type) {
    case AutosyncType_Off:
      break;
    case AutosyncType_Song:
      vs.push_back(AUTO_SYNC_SONG);
      break;
    case AutosyncType_Machine:
      vs.push_back(AUTO_SYNC_MACHINE);
      break;
    case AutosyncType_Tempo:
      vs.push_back(AUTO_SYNC_TEMPO);
      break;
    default:
      FAIL_M(ssprintf("Invalid autosync type: %i", type));
  }

  if (GAMESTATE->cur_song_ != nullptr &&
      !GAMESTATE->IsCourseMode())  // sync controls available
  {
    AdjustSync::GetSyncChangeTextGlobal(vs);
    AdjustSync::GetSyncChangeTextSong(vs);
  }

  Message set_status("SetStatus");
  set_status.SetParam("text", join("\n", vs));
  overlay_->HandleMessage(set_status);

  // Update SyncInfo
  bool visible =
      GAMESTATE->song_options_.GetCurrent().autosync_type_ != AutosyncType_Off;
  Message set_adjustments("SetAdjustments");
  set_adjustments.SetParam("visible", visible);
  if (visible) {
    float fNew;
    float fOld;
    if (GAMESTATE->song_options_.GetCurrent().autosync_type_ ==
        AutosyncType_Machine) {
      fNew = PREFSMAN->global_offset_seconds_;
      fOld = AdjustSync::global_offset_seconds_original_;
    } else {
      fNew = GAMESTATE->cur_song_->song_timing_.beat0_offset_in_seconds_;
      fOld = AdjustSync::timing_data_original_[0].beat0_offset_in_seconds_;
    }
    float fStdDev = AdjustSync::standard_deviation_;
    std::string s;
    s += OLD_OFFSET.GetValue() + ssprintf(": %0.3f\n", fOld);
    s += NEW_OFFSET.GetValue() + ssprintf(": %0.3f\n", fNew);
    s += STANDARD_DEVIATION.GetValue() + ssprintf(": %0.3f\n", fStdDev);
    s += COLLECTING_SAMPLE.GetValue() +
         ssprintf(
             ": %d / %d", AdjustSync::autosync_offset_sample_ + 1,
             AdjustSync::kOffsetSampleCount);
    set_adjustments.SetParam("text", s);
  } else {
    set_adjustments.SetParam("text", std::string(""));
  }
  overlay_->HandleMessage(set_adjustments);
}

static LocalizedString CANT_SYNC_WHILE_PLAYING_A_COURSE(
    "ScreenSyncOverlay", "Can't sync while playing a course.");
static LocalizedString SYNC_CHANGES_REVERTED(
    "ScreenSyncOverlay", "Sync changes reverted.");
bool ScreenSyncOverlay::Input(const InputEventPlus& input) {
  if (!IsGameplay()) {
    return Screen::Input(input);
  }

  if (input.device_i_.device_ != DEVICE_KEYBOARD) {
    return Screen::Input(input);
  }

  enum Action {
    RevertSyncChanges,
    ChangeSongBPM,
    ChangeGlobalOffset,
    ChangeSongOffset,
    Action_Invalid
  };
  Action a = Action_Invalid;

  bool bIncrease = true;
  switch (input.device_i_.button_) {
    case KEY_F4:
      a = RevertSyncChanges;
      break;

    case KEY_F9:
      bIncrease = false;
      [[fallthrough]];
    case KEY_F10:
      a = ChangeSongBPM;
      break;

    case KEY_F11:
      bIncrease = false;
      [[fallthrough]];
    case KEY_F12:
      if (INPUTFILTER->IsBeingPressed(
              DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT)) ||
          INPUTFILTER->IsBeingPressed(
              DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT))) {
        a = ChangeGlobalOffset;
      } else {
        a = ChangeSongOffset;
      }
      break;

    default:
      return Screen::Input(input);
  }

  if (GAMESTATE->IsCourseMode() && a != ChangeGlobalOffset) {
    SCREENMAN->SystemMessage(CANT_SYNC_WHILE_PLAYING_A_COURSE);
    return true;
  }

  switch (a) {
    case RevertSyncChanges:
      if (input.type_ != IET_FIRST_PRESS) {
        return false;
      }
      SCREENMAN->SystemMessage(SYNC_CHANGES_REVERTED);
      AdjustSync::RevertSyncChanges();
      break;
    case ChangeSongBPM: {
      float fDelta = bIncrease ? +0.02f : -0.02f;
      if (INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RALT)) ||
          INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LALT))) {
        fDelta /= 20;
      }
      switch (input.type_) {
        case IET_RELEASE:
          fDelta *= 0;
          break;
        case IET_REPEAT: {
          if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
            fDelta *= 0;
          } else {
            fDelta *= 10;
          }
          break;
        }
        default:
          break;
      }
      if (GAMESTATE->cur_song_ != nullptr) {
        TimingData& sTiming = GAMESTATE->cur_song_->song_timing_;
        BPMSegment* seg =
            sTiming.GetBPMSegmentAtBeat(GAMESTATE->position_.song_beat_);
        seg->SetBPS(seg->GetBPS() + fDelta);
        const std::vector<Steps*>& vpSteps =
            GAMESTATE->cur_song_->GetAllSteps();
        for (Steps* s : vpSteps) {
          TimingData& pTiming = s->timing_;
          // Empty means it inherits song timing,
          // which has already been updated.
          if (pTiming.empty()) {
            continue;
          }
          float second =
              sTiming.GetElapsedTimeFromBeat(GAMESTATE->position_.song_beat_);
          seg = pTiming.GetBPMSegmentAtBeat(
              pTiming.GetBeatFromElapsedTime(second));
          seg->SetBPS(seg->GetBPS() + fDelta);
        }
      }
    } break;
    case ChangeGlobalOffset:
    case ChangeSongOffset: {
      float fDelta = bIncrease ? +0.02f : -0.02f;
      if (INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RALT)) ||
          INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LALT))) {
        fDelta /= 20; /* 1ms */
      }
      switch (input.type_) {
        case IET_RELEASE:
          fDelta *= 0;
          break;
        case IET_REPEAT: {
          if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
            fDelta *= 0;
          } else {
            fDelta *= 10;
          }
        }
        default:
          break;
      }

      switch (a) {
        case ChangeGlobalOffset: {
          PREFSMAN->global_offset_seconds_.Set(
              PREFSMAN->global_offset_seconds_ + fDelta);
          break;
        }

        case ChangeSongOffset: {
          if (GAMESTATE->cur_song_ != nullptr) {
            GAMESTATE->cur_song_->song_timing_.beat0_offset_in_seconds_ +=
                fDelta;
            const std::vector<Steps*>& vpSteps =
                GAMESTATE->cur_song_->GetAllSteps();
            for (Steps* s : vpSteps) {
              // Empty means it inherits song timing,
              // which has already been updated.
              if (s->timing_.empty()) {
                continue;
              }
              s->timing_.beat0_offset_in_seconds_ += fDelta;
            }
          }
          break;
        }
        default:
          break;
      }
    } break;
    default:
      FAIL_M(ssprintf("Invalid sync action choice: %i", a));
  }

  ShowHelp();
  UpdateText();
  return true;
}

void ScreenSyncOverlay::ShowHelp() { overlay_->PlayCommand("Show"); }

void ScreenSyncOverlay::HideHelp() { overlay_->PlayCommand("Hide"); }

/*
 * (c) 2001-2005 Chris Danford
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
