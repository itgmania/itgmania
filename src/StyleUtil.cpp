#include "StyleUtil.h"

#include "Game.h"
#include "GameManager.h"
#include "Style.h"
#include "XmlFile.h"
#include "global.h"

void StyleID::FromStyle(const Style* p) {
  if (p) {
    game_ = GAMEMAN->GetGameForStyle(p)->name_;
    style_ = p->name_;
  } else {
    game_ = "";
    style_ = "";
  }
}

XNode* StyleID::CreateNode() const {
  XNode* node = new XNode("Style");

  node->AppendAttr("Game", game_);
  node->AppendAttr("Style", style_);

  return node;
}

void StyleID::LoadFromNode(const XNode* node) {
  Unset();
  ASSERT(node->GetName() == "Style");

  game_ = "";
  node->GetAttrValue("Game", game_);

  style_ = "";
  node->GetAttrValue("Style", style_);
}

bool StyleID::IsValid() const { return !game_.empty() && !style_.empty(); }

bool StyleID::operator<(const StyleID& rhs) const {
#define COMP(a)               \
  if (a < rhs.a) return true; \
  if (a > rhs.a) return false;
  COMP(game_);
  COMP(style_);
#undef COMP
  return false;
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
