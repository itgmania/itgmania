#include "TextBanner.h"

#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "Song.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(TextBanner);

void TextBanner::LoadFromNode(const XNode* pNode) {
  initted_ = true;

  ActorFrame::LoadFromNode(pNode);
}

void TextBanner::Load(std::string sMetricsGroup) {
  initted_ = true;

  text_title_.SetName("Title");
  text_title_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "text"));
  this->AddChild(&text_title_);

  text_sub_title_.SetName("Subtitle");
  text_sub_title_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "text"));
  this->AddChild(&text_sub_title_);

  text_artist_.SetName("Artist");
  text_artist_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "text"));
  this->AddChild(&text_artist_);

  AddCommand("AfterSet", THEME->GetMetricA(sMetricsGroup, "AfterSetCommand"));
  artist_prepend_string_ =
      THEME->GetMetric(sMetricsGroup, "ArtistPrependString");

  ActorUtil::LoadAllCommandsAndOnCommand(text_title_, sMetricsGroup);
  ActorUtil::LoadAllCommandsAndOnCommand(text_sub_title_, sMetricsGroup);
  ActorUtil::LoadAllCommandsAndOnCommand(text_artist_, sMetricsGroup);
}

TextBanner::TextBanner() { initted_ = false; }

TextBanner::TextBanner(const TextBanner& cpy)
    : ActorFrame(cpy),
      initted_(cpy.initted_),
      text_title_(cpy.text_title_),
      text_sub_title_(cpy.text_sub_title_),
      text_artist_(cpy.text_artist_),
      artist_prepend_string_(cpy.artist_prepend_string_) {
  this->AddChild(&text_title_);
  this->AddChild(&text_sub_title_);
  this->AddChild(&text_artist_);
}

void TextBanner::SetFromString(
    const std::string& sDisplayTitle, const std::string& sTranslitTitle,
    const std::string& sDisplaySubTitle, const std::string& sTranslitSubTitle,
    const std::string& sDisplayArtist, const std::string& sTranslitArtist) {
  ASSERT(initted_);

  text_title_.SetText(sDisplayTitle, sTranslitTitle);
  text_sub_title_.SetText(sDisplaySubTitle, sTranslitSubTitle);
  text_artist_.SetText(sDisplayArtist, sTranslitArtist);

  Message msg("AfterSet");
  this->PlayCommandNoRecurse(msg);
}

void TextBanner::SetFromSong(const Song* pSong) {
  if (pSong == nullptr) {
    SetFromString("", "", "", "", "", "");
    return;
  }
  SetFromString(
      pSong->GetDisplayMainTitle(), pSong->GetTranslitMainTitle(),
      pSong->GetDisplaySubTitle(), pSong->GetTranslitSubTitle(),
      artist_prepend_string_ + pSong->GetDisplayArtist(),
      artist_prepend_string_ + pSong->GetTranslitArtist());
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the TextBanner. */
class LunaTextBanner : public Luna<TextBanner> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetFromSong(T* p, lua_State* L) {
    Song* pSong = Luna<Song>::check(L, 1);
    p->SetFromSong(pSong);
    COMMON_RETURN_SELF;
  }
  static int SetFromString(T* p, lua_State* L) {
    std::string sDisplayTitle = SArg(1);
    std::string sTranslitTitle = SArg(2);
    std::string sDisplaySubTitle = SArg(3);
    std::string sTranslitSubTitle = SArg(4);
    std::string sDisplayArtist = SArg(5);
    std::string sTranslitArtist = SArg(6);
    p->SetFromString(
        sDisplayTitle, sTranslitTitle, sDisplaySubTitle, sTranslitSubTitle,
        sDisplayArtist, sTranslitArtist);
    COMMON_RETURN_SELF;
  }

  LunaTextBanner() {
    ADD_METHOD(Load);
    ADD_METHOD(SetFromSong);
    ADD_METHOD(SetFromString);
  }
};

LUA_REGISTER_DERIVED_CLASS(TextBanner, ActorFrame)
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
