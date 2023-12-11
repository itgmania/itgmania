#include "global.h"

#include "CsvFile.h"

#include <vector>

#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"

CsvFile::CsvFile() {}

bool CsvFile::ReadFile(const RString& path) {
  path_ = path;
  CHECKPOINT_M(ssprintf("Reading '%s'", path_.c_str()));

  RageFile f;
  if (!f.Open(path_)) {
    LOG->Trace("Reading '%s' failed: %s", path_.c_str(), f.GetError().c_str());
    error_ = f.GetError();
    return 0;
  }

  return ReadFile(f);
}

bool CsvFile::ReadFile(RageFileBasic& file) {
  all_lines_.clear();

  // hi,"hi2,","""hi3"""

  for (;;) {
    RString line;
    switch (file.GetLine(line)) {
      case -1:
        error_ = file.GetError();
        return false;
      case 0:
        return true; /* eof */
    }

    utf8_remove_bom(line);

    std::vector<RString> values;

    while (!line.empty()) {
      if (line[0] == '\"')  // quoted value
      {
        line.erase(line.begin());  // eat open quote
        RString::size_type iEnd = 0;
        do {
          iEnd = line.find('\"', iEnd);
          if (iEnd == line.npos) {
            iEnd =
                line.size() - 1;  // didn't find an end.  Take the whole line.
            break;
          }

          if (line.size() > iEnd + 1 &&
              line[iEnd + 1] == '\"') {  // next char is also double quote
            iEnd = iEnd + 2;
          } else {
            break;
          }
        } while (true);

        RString value = line;
        value = value.Left(iEnd);
        values.push_back(value);

        line.erase(line.begin(), line.begin() + iEnd);

        if (!line.empty() && line[0] == '\"') {
          line.erase(line.begin());
        }
      } else {
        RString::size_type iEnd = line.find(',');
        if (iEnd == line.npos) {
          iEnd = line.size();  // didn't find an end.  Take the whole line
        }

        RString value = line;
        value = value.Left(iEnd);
        values.push_back(value);

        line.erase(line.begin(), line.begin() + iEnd);
      }

      if (!line.empty() && line[0] == ',') {
        line.erase(line.begin());
      }
    }

    all_lines_.push_back(values);
  }
}

bool CsvFile::WriteFile(const RString& path) const {
  RageFile file;
  if (!file.Open(path, RageFile::WRITE)) {
    LOG->Trace(
        "Writing '%s' failed: %s", path.c_str(), file.GetError().c_str());
    error_ = file.GetError();
    return false;
  }

  return CsvFile::WriteFile(file);
}

bool CsvFile::WriteFile(RageFileBasic& file) const {
  for (const StringVector& line : all_lines_) {
    RString cur_line;
    for (auto value = line.begin(); value != line.end(); ++value) {
      RString val = *value;
      val.Replace("\"", "\"\"");  // escape quotes to double-quotes
      cur_line += "\"" + val + "\"";
      if (value != line.end() - 1) {
        cur_line += ",";
      }
    }
    if (file.PutLine(cur_line) == -1) {
      error_ = file.GetError();
      return false;
    }
  }
  return true;
}

/*
 * (c) 2001-2004 Adam Clauss, Chris Danford
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
