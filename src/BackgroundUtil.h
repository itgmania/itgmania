#ifndef BackgroundUtil_H
#define BackgroundUtil_H

#include <string>
#include <vector>

class Song;
class XNode;

extern const std::string RANDOM_BACKGROUND_FILE;
extern const std::string NO_SONG_BG_FILE;
extern const std::string SONG_BACKGROUND_FILE;

extern const std::string SBE_UpperLeft;
extern const std::string SBE_Centered;
extern const std::string SBE_StretchNormal;
extern const std::string SBE_StretchNoLoop;
extern const std::string SBE_StretchRewind;
extern const std::string SBT_CrossFade;

struct BackgroundDef {
  bool operator<(const BackgroundDef& other) const;
  bool operator==(const BackgroundDef& other) const;
  bool IsEmpty() const { return file1_.empty() && file2_.empty(); }
  std::string effect_;  // "" == automatically choose
  std::string file1_;   // must not be ""
  std::string file2_;   // may be ""
  std::string color1_;  // "" == use default
  std::string color2_;  // "" == use default

  XNode* CreateNode() const;

  /** @brief Set up the BackgroundDef with default values. */
  BackgroundDef()
      : effect_(""), file1_(""), file2_(""), color1_(""), color2_("") {}

  /**
   * @brief Set up the BackgroundDef with some defined values.
   * @param effect the intended effect.
   * @param f1 the primary filename for the definition.
   * @param f2 the secondary filename (optional). */
  BackgroundDef(std::string effect, std::string f1, std::string f2)
      : effect_(effect), file1_(f1), file2_(f2), color1_(""), color2_("") {}
};

struct BackgroundChange {
  BackgroundChange() : def_(), start_beat_(-1), rate_(1), transition_("") {}

  BackgroundChange(
      float s, std::string f1, std::string f2 = std::string(), float r = 1.f,
      std::string e = SBE_Centered, std::string t = std::string())
      : def_(e, f1, f2), start_beat_(s), rate_(r), transition_(t) {}

  BackgroundDef def_;
  float start_beat_;
  float rate_;
  std::string transition_;

  std::string GetTextDescription() const;

  /**
   * @brief Get the string representation of the change.
   * @return the string representation. */
  std::string ToString() const;
};
/** @brief Shared background-related routines. */
namespace BackgroundUtil {
void AddBackgroundChange(
    std::vector<BackgroundChange>& background_changes, BackgroundChange seg);
void SortBackgroundChangesArray(
    std::vector<BackgroundChange>& background_changes);

void GetBackgroundEffects(
    const std::string& name, std::vector<std::string>& paths_out,
    std::vector<std::string>& names_out);
void GetBackgroundTransitions(
    const std::string& name, std::vector<std::string>& paths_out,
    std::vector<std::string>& names_out);

void GetSongBGAnimations(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out);
void GetSongMovies(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out);
void GetSongBitmaps(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out);
void GetGlobalBGAnimations(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out);
void GetGlobalRandomMovies(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out,
    bool try_inside_of_song_group_and_genre_first = true,
    bool try_inside_of_song_group_first = true);

};  // namespace BackgroundUtil

#endif

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
