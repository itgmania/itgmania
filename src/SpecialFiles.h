#ifndef SpecialFiles_H
#define SpecialFiles_H

#include <string>

/** @brief The listing of the special files and directories in use. */
namespace SpecialFiles {
extern const std::string kPackagesDir;
extern const std::string kKeymapsPath;
/** @brief Edit Mode keymaps are separate from standard keymaps because
 * it should not change with the gametype, and to avoid possible
 * interference with the normal keymaps system. -Kyz */
extern const std::string kEditModeKeymapsPath;
extern const std::string kPreferencesIniPath;
/** @brief The directory that contains the themes. */
extern const std::string kThemesDir;
/** @brief The directory that contains the different languages. */
extern const std::string kLanguagesSubdir;
/** @brief The base language for most users of this program. */
extern const std::string kBaseLanguage;
extern const std::string kMetricsFile;
extern const std::string kCacheDir;
extern const std::string kBaseThemeName;
extern const std::string kDefaultsIniPath;
extern const std::string kStaticIniPath;
extern const std::string kTypeTxtFile;
extern const std::string kCaBundlePath;
/** @brief The default Songs directory. */
extern const std::string kSongsDir;
/** @brief The default courses directory. */
extern const std::string kCoursesDir;
/** @brief The default noteskins directory. */
extern const std::string kNoteskinsDir;

extern const std::string kCoinsIni;

}  // namespace SpecialFiles

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2005
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
