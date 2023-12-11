#ifndef BackgroundUtil_H
#define BackgroundUtil_H

#include "Song.h"
#include "XmlFile.h"

#include <vector>

extern const RString RANDOM_BACKGROUND_FILE;
extern const RString NO_SONG_BG_FILE;
extern const RString SONG_BACKGROUND_FILE;

extern const RString SBE_UpperLeft;
extern const RString SBE_Centered;
extern const RString SBE_StretchNormal;
extern const RString SBE_StretchNoLoop;
extern const RString SBE_StretchRewind;
extern const RString SBT_CrossFade;

// TODO(teejusb): Change this to a class.
struct BackgroundDef {
  bool operator<(const BackgroundDef& other) const;
  bool operator==(const BackgroundDef& other) const;
  bool IsEmpty() const { return file1_.empty() && file2_.empty(); }

  XNode* CreateNode() const;

  // Set up the BackgroundDef with default values.
  BackgroundDef()
      : effect_(""),
        file1_(""),
        file2_(""),
        color1_(""),
        color2_("") {}

  // Set up the BackgroundDef with some defined values.
  // effect is the intended effect.
  // f1 is the primary filename for the definition.
  // f2 is the secondary filename (optional).
  BackgroundDef(RString effect, RString f1, RString f2)
      : effect_(effect),
        file1_(f1),
        file2_(f2),
        color1_(""),
        color2_("") {}

  RString effect_;  // "" == automatically choose
  RString file1_;   // must not be ""
  RString file2_;   // may be ""
  RString color1_;  // "" == use default
  RString color2_;  // "" == use default
};

// TODO(teejusb): Change this to a class.
struct BackgroundChange {
 public:
  BackgroundChange()
      : background_def_(), start_beat_(-1), rate_(1), transition_("") {}

  BackgroundChange(
      float s, RString f1, RString f2 = RString(), float r = 1.f,
      RString e = SBE_Centered, RString t = RString())
      : background_def_(e, f1, f2), start_beat_(s), rate_(r), transition_(t) {}

  BackgroundDef background_def_;
  float start_beat_;
  float rate_;
  RString transition_;

  RString GetTextDescription() const;

  // Get the string representation of the change.
  RString ToString() const;
};

// Shared background-related routines.
namespace BackgroundUtil {

void AddBackgroundChange(
    std::vector<BackgroundChange>& background_changes, BackgroundChange seg);
void SortBackgroundChangesArray(
    std::vector<BackgroundChange>& background_changes);

void GetBackgroundEffects(
    const RString& name, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);
void GetBackgroundTransitions(
    const RString& name, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);

void GetSongBGAnimations(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);
void GetSongMovies(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);
void GetSongBitmaps(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);
void GetGlobalBGAnimations(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out);
void GetGlobalRandomMovies(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out,
    bool try_inside_of_song_group_and_genre_first = true,
    bool try_inside_of_song_group_first = true);

void BakeAllBackgroundChanges(Song* song);

};  // namespace BackgroundUtil

#endif  // BackgroundUtil_H

/*
 * (c) 2001-2004 Chris Danford, Ben Nordstrom
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
