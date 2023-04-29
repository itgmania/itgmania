#ifndef DisplayResolutions_H
#define DisplayResolutions_H

#include <set>

// The dimensions of the program.
class DisplayResolution {
 public:
  // The width of the program.
  int width_;
  // The height of the program.
  int height_;
  // Is this display stretched/used for widescreen?
  bool is_stretched_;

  // Determine if one DisplayResolution is less than the other.
  bool operator<(const DisplayResolution& other) const {

// A quick way to compare the two DisplayResolutions.
#define COMPARE(x) \
  if (x != other.x) return x < other.x;
    COMPARE(width_);
    COMPARE(height_);
    COMPARE(is_stretched_);
#undef COMPARE
    return false;
  }
};

// The collection of DisplayResolutions available within the program.
typedef std::set<DisplayResolution> DisplayResolutions;

#endif  // DisplayResolutions_H

/**
 * @file
 * @author Chris Danford (c) 2001-2005
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
