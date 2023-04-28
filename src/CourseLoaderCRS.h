#ifndef COURSE_LOADER_CRS_H
#define COURSE_LOADER_CRS_H

#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "MsdFile.h"

// The Course Loader handles parsing a course from a .crs files.
namespace CourseLoaderCRS {

// Attempt to load a course file from a particular path.
bool LoadFromCRSFile(const RString& path, Course& out);
// Attempt to load the course information from the msd context.
bool LoadFromMsd(
    const RString& path, const MsdFile& msd, Course& out, bool bFromCache);
// Attempt to load the course file from the buffer.
bool LoadFromBuffer(const RString& path, const RString& buffer, Course& out);
// Attempt to load an edit course from the hard drive.
bool LoadEditFromFile(const RString& edit_file_path, ProfileSlot slot);
// Attempt to load an edit course from the buffer.
bool LoadEditFromBuffer(
    const RString& buffer, const RString& path, ProfileSlot slot);

}  // namespace CourseLoaderCRS

#endif  // COURSE_LOADER_CRS_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
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
