#include "ScreenTestSound.h"

#include <string>
#include <vector>

#include "InputEventPlus.h"
#include "InputFilter.h"
#include "RageInputDevice.h"
#include "RageSoundManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ThemeManager.h"

REGISTER_SCREEN_CLASS(ScreenTestSound);

void ScreenTestSound::Init() {
  Screen::Init();

  this->AddChild(&heeeeeeeeelp_);

  heeeeeeeeelp_.SetXY(450, 400);
  heeeeeeeeelp_.LoadFromFont(THEME->GetPathF("Common", "normal"));
  heeeeeeeeelp_.SetZoom(.5f);
  heeeeeeeeelp_.SetText(
      "p  Play\n"
      "s  Stop\n"
      "l  Set looping\n"
      "a  Set autostop\n"
      "c  Set continue");

  for (int i = 0; i < nsounds; ++i) {
    this->AddChild(&s_[i].txt_);
    s_[i].txt_.LoadFromFont(THEME->GetPathF("Common", "normal"));
    s_[i].txt_.SetZoom(.5f);
  }

  s_[0].txt_.SetXY(150, 100);
  s_[1].txt_.SetXY(450, 100);
  s_[2].txt_.SetXY(150, 250);
  s_[3].txt_.SetXY(450, 250);
  s_[4].txt_.SetXY(150, 400);

  // howl says that "oga" is preferred over "ogg" as the extension;
  // i personally don't give a shit until I see what the vorbis group
  // has to say about it.
  s_[0].s_.Load("Themes/default/Sounds/_common menu music (loop).ogg");
  s_[1].s_.Load("Themes/default/Sounds/ScreenTitleMenu change.ogg");
  s_[2].s_.Load("Themes/default/Sounds/ScreenEvaluation try extra1.ogg");
  s_[3].s_.Load("Themes/default/Sounds/ScreenGameplay oni die.ogg");
  s_[4].s_.Load("Themes/default/Sounds/Common back.ogg");

  // s[0].s.SetStartSeconds(45);
  // s[0].s.SetPositionSeconds();
  //  s[4].s.SetLengthSeconds(1);
  RageSoundParams p;
  p.stop_mode_ = RageSoundParams::M_STOP;
  // p.m_fRate = 1.20f;
  for (int i = 0; i < nsounds; ++i) {
    s_[i].s_.SetParams(p);
  }

  // s[0].s.SetStopMode(RageSound::M_LOOP);
  // s[0].s.Play(false);

  selected_ = 0;
  for (int i = 0; i < nsounds; ++i) {
    UpdateText(i);
  }
}

ScreenTestSound::~ScreenTestSound() {
  for (int i = 0; i < nsounds; ++i) {
    /* Delete copied sounds. */
    std::vector<RageSound*>& snds = sound_copies_[i];
    for (unsigned j = 0; j < snds.size(); ++j) {
      delete snds[j];
    }
  }
}

void ScreenTestSound::UpdateText(int n) {
  std::string fn = Basename(s_[n].s_.GetLoadedFilePath());

  std::vector<RageSound*>& snds = sound_copies_[n];

  std::string pos;
  for (unsigned p = 0; p < snds.size(); ++p) {
    if (p) {
      pos += ", ";
    }
    pos += ssprintf("%.3f", snds[p]->GetPositionSeconds());
  }

  s_[n].txt_.SetText(ssprintf(
      "%i: %s\n"
      "%s\n"
      "%s\n"
      "(%s)\n"
      "%s",
      n + 1, fn.c_str(), s_[n].s_.IsPlaying() ? "Playing" : "Stopped",
      s_[n].s_.GetParams().stop_mode_ == RageSoundParams::M_STOP
          ? "Stop when finished"
      : s_[n].s_.GetParams().stop_mode_ == RageSoundParams::M_CONTINUE
          ? "Continue until stopped"
          : "Loop",
      pos.size() ? pos.c_str() : "none playing",
      selected_ == n ? "^^^^^^" : ""));
}

void ScreenTestSound::Update(float f) {
  Screen::Update(f);

  for (int i = 0; i < nsounds; ++i) {
    UpdateText(i);

    /* Delete copied sounds that have finished playing. */
    std::vector<RageSound*>& snds = sound_copies_[i];
    for (unsigned j = 0; j < snds.size(); ++j) {
      if (snds[j]->IsPlaying()) {
        continue;
      }
      delete snds[j];
      snds.erase(snds.begin() + j);
      --j;
    }
  }
}

bool ScreenTestSound::Input(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;  // ignore
  }

  switch (input.device_i_.device_) {
    case DEVICE_KEYBOARD:
      switch (input.device_i_.button_) {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
          selected_ = input.device_i_.button_ - '0' - 1;
          break;
        case 'p': {
          /* We want to be able to read the position of copied sounds; if we let
           * RageSound copy itself, then the copy will be owned by
           * RageSoundManager and we won't be allowed to touch it.  Copy it
           * ourself. */
          RageSound* pCopy = new RageSound(s_[selected_].s_);
          sound_copies_[selected_].push_back(pCopy);
          pCopy->Play(false);
          break;
        }
        case 's': {
          for (int i = 0; i < nsounds; ++i) {
            /* Stop copied sounds. */
            std::vector<RageSound*>& snds = sound_copies_[i];
            for (unsigned j = 0; j < snds.size(); ++j) {
              snds[j]->Stop();
            }
          }
          break;
        }
        case 'l': {
          RageSoundParams p = s_[selected_].s_.GetParams();
          p.stop_mode_ = RageSoundParams::M_LOOP;
          s_[selected_].s_.SetParams(p);
          break;
        }
        case 'a': {
          RageSoundParams p = s_[selected_].s_.GetParams();
          p.stop_mode_ = RageSoundParams::M_STOP;
          s_[selected_].s_.SetParams(p);
          break;
        }
        case 'c': {
          RageSoundParams p = s_[selected_].s_.GetParams();
          p.stop_mode_ = RageSoundParams::M_CONTINUE;
          s_[selected_].s_.SetParams(p);
          break;
        }
          /*			case KEY_LEFT:
                                          obj.SetX(obj.GetX() - 10);
                                          break;
                                  case KEY_RIGHT:
                                          obj.SetX(obj.GetX() + 10);
                                          break;
                                  case KEY_UP:
                                          obj.SetY(obj.GetY() - 10);
                                          break;
                                  case KEY_DOWN:
                                          obj.SetY(obj.GetY() + 10);
                                          break;
          */
        default:
          break;
      }
    default:
      break;
  }

  return false;
}

/*
 * (c) 2003 Glenn Maynard
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
