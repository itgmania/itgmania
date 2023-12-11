#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "ActorFrame.h"
#include "BackgroundUtil.h"
#include "PlayerNumber.h"
#include "Quad.h"

#include <vector>


class DancingCharacters;
class Song;
class BackgroundImpl;

// The Background that is behind the notes while playing.
class Background : public ActorFrame {
 public:
  Background();
  ~Background();
  void Init();

  virtual void LoadFromSong(const Song* song);
  virtual void Unload();

  void FadeToActualBrightness();
  void SetBrightness(float brightness);  // overrides pref and Cover

  // NOTE(Kyz): One more piece of the puzzle that puts the notefield board above
	// the bg and under everything else. disable_draw_ exists so that
  // ScreenGameplay can draw the background manually, and still have it as a
  // child.
  bool disable_draw_;
  virtual bool EarlyAbortDraw() const { return disable_draw_; }

  // Retrieve whatever dancing characters are in use.
  DancingCharacters* GetDancingCharacters();

  void GetLoadedBackgroundChanges(
      std::vector<BackgroundChange>** background_changes_out);

 protected:
  BackgroundImpl* background_impl_;
};

#endif  // BACKGROUND_H

/**
 * @file
 * @author Chris Danford, Ben Nordstrom (c) 2001-2004
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
