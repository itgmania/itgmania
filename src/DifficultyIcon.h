
#ifndef DIFFICULTY_ICON_H
#define DIFFICULTY_ICON_H

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "Sprite.h"
#include "Steps.h"
#include "Trail.h"

// Graphical representation of the difficulty class.
class DifficultyIcon : public Sprite {
 public:
  DifficultyIcon();
  virtual bool EarlyAbortDraw() const {
    return blank_ || Sprite::EarlyAbortDraw();
  }

  bool Load(RString file_path);
  virtual void Load(RageTextureID id) { Load(id.filename); }
  virtual void LoadFromNode(const XNode* node);
  virtual DifficultyIcon* Copy() const;

  void SetPlayer(PlayerNumber pn);
  void Unset();
  void SetFromSteps(PlayerNumber pn, const Steps* steps);
  void SetFromTrail(PlayerNumber pn, const Trail* trail);
  void SetFromDifficulty(Difficulty difficulty);

  // Lua
  void PushSelf(lua_State* L);

 protected:
  bool blank_;
  PlayerNumber player_number_;
};

#endif  // DIFFICULTY_ICON_H

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
