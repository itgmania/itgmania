#include "ModIcon.h"

#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "RageUtil.h"
#include "StdString.h"
#include "ThemeManager.h"

ModIcon::ModIcon() {}

ModIcon::ModIcon(const ModIcon& cpy)
    : ActorFrame(cpy),
      text_(cpy.text_),
      spr_filled_(cpy.spr_filled_),
      spr_empty_(cpy.spr_empty_),
      CROP_TEXT_TO_WIDTH(cpy.CROP_TEXT_TO_WIDTH),
      STOP_WORDS(cpy.STOP_WORDS),
      stop_words_(cpy.stop_words_) {
  this->RemoveAllChildren();
  this->AddChild(spr_filled_);
  this->AddChild(spr_empty_);
  this->AddChild(&text_);
}

void ModIcon::Load(std::string sMetricsGroup) {
  spr_filled_.Load(THEME->GetPathG(sMetricsGroup, "Filled"));
  spr_filled_->SetName("Filled");
  ActorUtil::LoadAllCommands(spr_filled_, sMetricsGroup);
  this->AddChild(spr_filled_);

  spr_empty_.Load(THEME->GetPathG(sMetricsGroup, "Empty"));
  spr_empty_->SetName("Empty");
  ActorUtil::LoadAllCommands(spr_empty_, sMetricsGroup);
  this->AddChild(spr_empty_);

  text_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "Text"));
  text_.SetName("Text");
  ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(text_, sMetricsGroup);
  this->AddChild(&text_);

  CROP_TEXT_TO_WIDTH.Load(sMetricsGroup, "CropTextToWidth");

  // stop words
  STOP_WORDS.Load(sMetricsGroup, "StopWords");
  stop_words_.clear();
  split(STOP_WORDS, ",", stop_words_);

  Set("");
}

void ModIcon::Set(const std::string& _sText) {
  std::string sText = _sText;

  for (unsigned i = 0; i < stop_words_.size(); i++) {
    if (EqualsNoCase(sText, stop_words_[i])) {
      sText = "";
    }
  }

  Replace(sText, " ", "\n");

  bool bVacant = (sText == "");
  spr_filled_->SetVisible(!bVacant);
  spr_empty_->SetVisible(bVacant);

  text_.SetText(sText);
  text_.CropToWidth(CROP_TEXT_TO_WIDTH);
}

/*
 * (c) 2002-2004 Chris Danford
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
