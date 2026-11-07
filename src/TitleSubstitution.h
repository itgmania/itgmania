#ifndef TITLE_SUBSTITUTION_H
#define TITLE_SUBSTITUTION_H

#include <string>
#include <vector>

/** @brief The different fields to potentially translate. */
struct TitleFields {
  void SaveToStrings(
      std::string& title, std::string& subtitle, std::string& artist,
      std::string& title_translit, std::string& subtitle_translit,
      std::string& artist_translit) const {
    title = title_;
    subtitle = subtitle_;
    artist = artist_;
    title_translit = title_translit_;
    subtitle_translit = subtitle_translit_;
    artist_translit = artist_translit_;
  }

  void LoadFromStrings(
      std::string title, std::string subtitle, std::string artist,
      std::string title_translit, std::string subtitle_translit,
      std::string artist_translit) {
    title_ = title;
    subtitle_ = subtitle;
    artist_ = artist;
    title_translit_ = title_translit;
    subtitle_translit_ = subtitle_translit;
    artist_translit_ = artist_translit;
  }
  std::string title_, subtitle_, artist_;
  std::string title_translit_, subtitle_translit_, artist_translit_;
};
struct TitleTrans;
/** @brief Automatic translation for Song titles. */
class TitleSubst {
  std::vector<TitleTrans*> ttab_;

  void AddTrans(const TitleTrans& tr);

 public:
  TitleSubst(const std::string& section);
  ~TitleSubst();

  void Load(const std::string& filename, const std::string& section);

  void Subst(TitleFields& tf);
};

#endif

/**
 * @file
 * @author Glenn Maynard (c) 2003-2004
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
