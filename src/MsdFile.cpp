/*
 * The original MSD format is simply:
 *
 * #PARAM0:PARAM1:PARAM2:PARAM3;
 * #NEXTPARAM0:PARAM1:PARAM2:PARAM3;
 *
 * (The first field is typically an identifier, but doesn't have to be.)
 *
 * The semicolon is not optional, though if we hit a # on a new line, eg:
 * #VALUE:PARAM1
 * #VALUE2:PARAM2
 * we'll recover.
 */

#include "MsdFile.h"

#include <string>

#include "RageFile.h"

void MsdFile::AddParam(const char* buf, int len) {
  values_.back().params_.push_back(std::string(buf, len));
}

void MsdFile::AddValue() /* (no extra charge) */
{
  values_.push_back(value_t());
  values_.back().params_.reserve(32);
}

void MsdFile::ReadBuf(const char* buf, int len, bool unescape) {
  values_.reserve(64);

  bool reading_value = false;
  int i = 0;
  char* processed = new char[len];
  int processed_len = -1;
  while (i < len) {
    if (i + 1 < len && buf[i] == '/' && buf[i + 1] == '/') {
      /* Skip a comment entirely; don't copy the comment to the value/parameter
       */
      do {
        i++;
      } while (i < len && buf[i] != '\n');

      continue;
    }

    if (reading_value && buf[i] == '#') {
      /* Unfortunately, many of these files are missing ;'s.
       * If we get a # when we thought we were inside a value, assume we
       * missed the ;.  Back up and end the value. */
      // Make sure this # is the first non-whitespace character on the line.
      bool first_char = true;
      int j = processed_len;
      while (j > 0 && processed[j - 1] != '\r' && processed[j - 1] != '\n') {
        if (processed[j - 1] == ' ' || processed[j - 1] == '\t') {
          --j;
          continue;
        }

        first_char = false;
        break;
      }

      if (!first_char) {
        /* We're not the first char on a line.  Treat it as if it were a normal
         * character. */
        processed[processed_len++] = buf[i++];
        continue;
      }

      /* Skip newlines and whitespace before adding the value. */
      processed_len = j;
      while (processed_len > 0 && (processed[processed_len - 1] == '\r' ||
                                   processed[processed_len - 1] == '\n' ||
                                   processed[processed_len - 1] == ' ' ||
                                   processed[processed_len - 1] == '\t')) {
        --processed_len;
      }

      AddParam(processed, processed_len);
      processed_len = 0;
      reading_value = false;
    }

    /* # starts a new value. */
    if (!reading_value && buf[i] == '#') {
      AddValue();
      reading_value = true;
    }

    if (!reading_value) {
      if (unescape && buf[i] == '\\') {
        i += 2;
      } else {
        ++i;
      }
      continue; /* nothing else is meaningful outside of a value */
    }

    /* : and ; end the current param, if any. */
    if (processed_len != -1 && (buf[i] == ':' || buf[i] == ';')) {
      AddParam(processed, processed_len);
    }

    /* # and : begin new params. */
    if (buf[i] == '#' || buf[i] == ':') {
      ++i;
      processed_len = 0;
      continue;
    }

    /* ; ends the current value. */
    if (buf[i] == ';') {
      reading_value = false;
      ++i;
      continue;
    }

    /* We've gone through all the control characters.  All that is left is
     * either an escaped character, ie \#, \\, \:, etc., or a regular character.
     */
    if (buf[i] == '\\' && i < len) {
      // If we're escaping the next character, skip the '\\'
      if (unescape) {
        ++i;
      }
      // Otherwise, add the '\\' to cProcessed here, so that
      // whatever character is coming next stays escaped in
      // the resulting value/parameter string
      // (and most importantly, it doesn't get parsed as a control character)
      // on the next iteration
      else {
        processed[processed_len++] = buf[i++];
      }
    }

    if (i < len) {
      processed[processed_len++] = buf[i++];
    }
  }

  /* Add any unterminated value at the very end. */
  if (reading_value) {
    AddParam(processed, processed_len);
  }

  delete[] processed;
}

// returns true if successful, false otherwise
bool MsdFile::ReadFile(std::string new_path, bool unescape) {
  error_ = "";

  RageFile f;
  /* Open a file. */
  if (!f.Open(new_path)) {
    error_ = f.GetError();
    return false;
  }

  // allocate a string to hold the file
  std::string file_string;
  file_string.reserve(f.GetFileSize());

  int bytes_read = f.Read(file_string);
  if (bytes_read == -1) {
    error_ = f.GetError();
    return false;
  }

  ReadBuf(file_string.c_str(), bytes_read, unescape);

  return true;
}

void MsdFile::ReadFromString(const std::string& string, bool unescape) {
  ReadBuf(string.c_str(), string.size(), unescape);
}

std::string MsdFile::GetParam(unsigned val, unsigned par) const {
  if (val >= GetNumValues() || par >= GetNumParams(val)) {
    return std::string();
  }

  return values_[val].params_[par];
}

/*
 * (c) 2001-2006 Chris Danford, Glenn Maynard
 *
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
