#include "NotesLoader.h"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "NotesLoaderDWI.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSMA.h"
#include "NotesLoaderSSC.h"
#include "StdString.h"

void NotesLoader::GetMainAndSubTitlesFromFullTitle(
    const std::string& full_title, std::string& main_title_out,
    std::string& sub_title_out) {
  static const std::string_view kLeftSeps[] = {"\t", " -", " ~", " (", " ["};
  size_t full_title_size = full_title.size();

  for (const auto& sep : kLeftSeps) {
    size_t begin_index = full_title.find(sep);
    if (begin_index != std::string::npos) {
      main_title_out = Left(full_title, static_cast<int>(begin_index));
      sub_title_out = full_title.substr(
          begin_index + sep.size(), full_title_size - begin_index - sep.size());
      return;
    }
  }
  main_title_out = full_title;
  sub_title_out = "";
}

bool NotesLoader::LoadFromDir(
    const std::string& path, Song& out,
    std::set<std::string>& blacklisted_images, bool load_autosave) {
  std::vector<std::string> list;

  blacklisted_images.clear();
  SSCLoader loader_ssc;
  loader_ssc.GetApplicableFiles(path, list, load_autosave);
  if (!list.empty()) {
    if (!loader_ssc.LoadFromDir(path, out, load_autosave)) {
      return false;
    }
    return true;
  }
  SMALoader loader_sma;
  loader_sma.GetApplicableFiles(path, list);
  if (!list.empty()) {
    return loader_sma.LoadFromDir(path, out);
  }
  SMLoader loader_sm;
  loader_sm.GetApplicableFiles(path, list);
  if (!list.empty()) {
    return loader_sm.LoadFromDir(path, out);
  }
  DWILoader::GetApplicableFiles(path, list);
  if (!list.empty()) {
    return DWILoader::LoadFromDir(path, out, blacklisted_images);
  }
  return false;
}

/*
 * (c) 2001-2004,2007 Chris Danford, Glenn Maynard, Steve Checkoway
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
