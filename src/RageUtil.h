/** @brief RageUtil - Miscellaneous helper macros and functions. */

#ifndef RAGE_UTIL_H
#define RAGE_UTIL_H

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Compiler.h"
#include "Platform.h"
#include "global.h"

class RageUtil {
 public:
  // Safely delete pointers.
  template <typename T>
  inline static void SafeDelete(T*& p) noexcept {
    delete p;
    p = nullptr;
  }

  // Safely delete array pointers.
  template <typename T>
  inline static void SafeDeleteArray(T*& p) noexcept {
    delete[] p;
    p = nullptr;
  }
};

/** @brief Zero out the memory. */
#define ZERO(x) memset(&(x), 0, sizeof(x))
/** @brief Get the length of the array. */
#define ARRAYLEN(a) (sizeof(a) / sizeof((a)[0]))

extern const std::string kCustomSongPath;

/**
 * @brief Scales x so that l1 corresponds to l2 and h1 corresponds to h2.
 *
 * This does not modify x, so it MUST assign the result to something!
 * Do the multiply before the divide to that integer scales have more precision.
 *
 * One such example: SCALE(x, 0, 1, L, H); interpolate between L and H.
 */
#define SCALE(x, l1, h1, l2, h2) \
  (((x) - (l1)) * ((h2) - (l2)) / ((h1) - (l1)) + (l2))

template <typename T, typename U>
inline U lerp(T x, U l, U h) {
  return U(x * (h - l) + l);
}

template <typename T, typename U, typename V>
inline bool rage_clamp(T& x, U l, V h) {
  if (x > static_cast<T>(h)) {
    x = static_cast<T>(h);
    return true;
  } else if (x < static_cast<T>(l)) {
    x = static_cast<T>(l);
    return true;
  }
  return false;
}

template <class T>
inline bool enum_clamp(T& x, T l, T h) {
  if (x > h) {
    x = h;
    return true;
  } else if (x < l) {
    x = l;
    return true;
  }
  return false;
}

inline void wrap(int& x, int n) {
  if (x < 0) {
    x += ((-x / n) + 1) * n;
  }
  x %= n;
}
inline void wrap(unsigned& x, unsigned n) { x %= n; }
inline void wrap(float& x, float n) {
  if (x < 0) {
    x += std::trunc(((-x / n) + 1)) * n;
  }
  x = std::fmod(x, n);
}

inline float fracf(float f) { return f - std::trunc(f); }

template <class T>
void CircularShift(std::vector<T>& v, int dist) {
  for (int i = std::abs(dist); i > 0; i--) {
    if (dist > 0) {
      T t = v[0];
      v.erase(v.begin());
      v.push_back(t);
    } else {
      T t = v.back();
      v.erase(v.end() - 1);
      v.insert(v.begin(), t);
    }
  }
}

template <typename Type, typename Ret>
static Ret* CreateClass() {
  return new Type;
}

/*
 * Helper function to remove all objects from an STL container for which the
 * Predicate pred is true. If you want to remove all objects for which the
 * predicate returns false, wrap the predicate with not1().
 */
template <typename Container, typename Predicate>
inline void RemoveIf(Container& c, Predicate p) {
  c.erase(remove_if(c.begin(), c.end(), p), c.end());
}

/* Safely add an integer to an enum.
 *
 * This is illegal:
 *
 *  ((int&)val) += iAmt;
 *
 * It breaks aliasing rules; the compiler is allowed to assume that "val"
 * doesn't change (unless it's declared volatile), and in some cases, you'll end
 * up getting old values for "val" following the add.  (What's probably really
 * happening is that the memory location is being added to, but the value is
 * stored in a register, and breaking aliasing rules means the compiler doesn't
 * know that the register value is invalid.)
 */
template <typename T>
static inline void enum_add(T& val, int amt) {
  val = static_cast<T>(val + amt);
}

template <typename T>
static inline T enum_add2(T val, int amt) {
  return static_cast<T>(val + amt);
}

/* return f rounded to the nearest multiple of fRoundInterval */
inline float Quantize(const float f, const float round_interval) {
  return int((f + round_interval / 2) / round_interval) * round_interval;
}

inline int Quantize(const int i, const int round_interval) {
  return int((i + round_interval / 2) / round_interval) * round_interval;
}

/* return f truncated to the nearest multiple of fTruncInterval */
inline float ftruncf(const float f, const float trunc_interval) {
  return int((f) / trunc_interval) * trunc_interval;
}

/* Return i rounded up to the nearest multiple of iInterval. */
inline int QuantizeUp(int i, int interval) {
  return int((i + interval - 1) / interval) * interval;
}

inline float QuantizeUp(float i, float interval) {
  return std::ceil(i / interval) * interval;
}

/* Return i rounded down to the nearest multiple of iInterval. */
inline float QuantizeDown(float i, float interval) {
  return std::floor(i / interval) * interval;
}

// Move val toward other_val by to_move.
void fapproach(float& val, float other_val, float to_move);

/* Return a positive x mod y. */
float fmodfp(float x, float y);

int power_of_two(int v);
bool IsAnInt(const std::string& s);
bool IsHexVal(const std::string& s);
std::string BinaryToHex(const void* data, size_t num_bytes);
std::string BinaryToHex(const std::string& string);
float HHMMSSToSeconds(const std::string& hms);
std::string SecondsToHHMMSS(float secs);
std::string SecondsToMSSMsMs(float secs);
std::string SecondsToMMSSMsMs(float secs);
std::string SecondsToMMSSMsMsMs(float secs);
std::string MicrosecondsToMMSSMsMs(uint64_t usecs);
std::string MicrosecondsToMMSSMsMsMs(uint64_t usecs);
std::string SecondsToMSS(float secs);
std::string SecondsToMMSS(float secs);
std::string PrettyPercent(float numerator, float denominator);
std::string Commify(int num);
std::string Commify(
    const std::string& num, const std::string& sep = ",",
    const std::string& dot = ".");
std::string FormatNumberAndSuffix(int i);
/* Round num to 3 decimal places and return as string with 3 decimal places */
std::string NormalizeDecimal(float num);

struct tm GetLocalTime();

std::string ssprintf(const char* fmt, ...) PRINTF(1, 2);
std::string vssprintf(const char* fmt, va_list arg_list);

/*
 * Splits a Path into 4 parts (Directory, Drive, Filename, Extention).  Supports
 * UNC path names. If Path is a directory (eg. c:\games\stepmania"), append a
 * slash so the last element will end up in Dir, not FName:
 * "c:\games\stepmania\".
 * */
void splitpath(
    const std::string& path, std::string& dir, std::string& filename,
    std::string& ext);
std::string custom_songify_path(const std::string& path);

std::string SetExtension(const std::string& path, const std::string& ext);
std::string GetExtension(const std::string& path);
std::string GetFileNameWithoutExtension(const std::string& path);
void MakeValidFilename(std::string& name);

bool FindFirstFilenameContaining(
    const std::vector<std::string>& filenames, std::string& out,
    const std::vector<std::string>& starts_with,
    const std::vector<std::string>& contains,
    const std::vector<std::string>& ends_with);

extern const wchar_t kInvalidChar;

int utf8_get_char_len(char p);
bool utf8_to_wchar(const char* s, size_t length, unsigned& start, wchar_t& ch);
bool utf8_to_wchar_ec(const std::string& s, unsigned& start, wchar_t& ch);
void wchar_to_utf8(wchar_t ch, std::string& out);
wchar_t utf8_get_char(const std::string& s);
bool utf8_is_valid(const std::string& s);
void utf8_remove_bom(std::string& s);
void MakeUpper(char* p, size_t len);
void MakeUpper(wchar_t* p, size_t len);

// TODO: Have the three functions below be moved to better locations.
float StringToFloat(const std::string& string);
bool StringToFloat(const std::string& string, float& out);
// Better than IntToString because you can check for success.
template <class T>
inline bool operator>>(const std::string& lhs, T& rhs) {
  return !!(std::istringstream(lhs) >> rhs);
}

// Exception-safe wrappers around stoi and friends
// Additional argument exceptVal will be returned if the conversion couldn't be
// performed
int StringToInt(
    const std::string& str, size_t* pos = 0, int base = 10, int except_val = 0);

std::string WStringToRString(const std::wstring& string);
std::string WcharToUTF8(wchar_t c);
std::wstring RStringToWstring(const std::string& string);

// Splits a std::string into an std::vector<std::string> according the
// Delimitor.
void split(
    const std::string& source, const std::string& delimitor,
    std::vector<std::string>& add_it, const bool ignore_empty = true);
void split(
    const std::wstring& source, const std::wstring& delimitor,
    std::vector<std::wstring>& add_it, const bool ignore_empty = true);
std::vector<std::string> split(
    const std::string& source, const char delimiter,
    const bool ignore_empty = true);

/* In-place split. */
void split(
    const std::string& source, const std::string& delimitor, int& begin,
    int& size, const bool ignore_empty = true);

/* In-place split of partial string. */
void split(
    const std::string& source, const std::string& delimitor, int& begin,
    int& size, int len,
    const bool ignore_empty); /* no default to avoid ambiguity */

// Joins a std::vector<std::string> to create a std::string according the
// Deliminator.
std::string join(
    const std::string& delimitor, const std::vector<std::string>& source);
std::string join(
    const std::string& delimitor,
    std::vector<std::string>::const_iterator begin,
    std::vector<std::string>::const_iterator end);

// These methods escapes a string for saving in a .sm or .crs file
std::string SmEscape(
    const std::string& unescaped,
    const std::vector<char> chars_to_escape = {'\\', ':', ';'});
std::string SmEscape(
    const char* unescaped, int len,
    const std::vector<char> chars_to_escape = {'\\', ':', ';'});
// Escapes each element in a std::vector<std::string>, returns a new vector
std::vector<std::string> SmEscape(
    const std::vector<std::string>& unescaped,
    const std::vector<char> chars_to_escape = {'\\', ':', ';'});

std::string SmUnescape(const std::string& escaped);

std::string GetCwd();

void SetCommandlineArguments(int argc, char** argv);
bool GetCommandlineArgument(
    const std::string& option, std::string* argument = nullptr, int index = 0);
extern int g_argc;
extern char** g_argv;

void CRC32(unsigned int& crc, const void* buffer, size_t size);
unsigned int GetHashForString(const std::string& s);
unsigned int GetHashForFile(const std::string& path);
unsigned int GetHashForDirectory(
    const std::string& dir);  // a hash value that remains the same as long as
                              // nothing in the directory has changed
bool DirectoryIsEmpty(const std::string& path);

bool CompareRStringsAsc(const std::string& str1, const std::string& str2);
bool CompareRStringsDesc(const std::string& str1, const std::string& str2);
void SortRStringArray(
    std::vector<std::string>& add_to, const bool sort_ascending = true);

/* Find the mean and standard deviation of all numbers in [start,end). */
float calc_mean(const float* start, const float* end);
/* When bSample is true, it calculates the square root of an unbiased estimator
 * for the population variance. Note that this is not an unbiased estimator for
 * the population standard deviation but it is close and an unbiased estimator
 * is complicated (apparently). When the entire population is known, bSample
 * should be false to calculate the exact standard deviation. */
float calc_stddev(const float* start, const float* end, bool sample = false);

/*
 * Find the slope, intercept, and error of a linear least squares regression
 * of the points given.  Error is returned as the sqrt of the average squared
 * Y distance from the chosen line.
 * Returns true on success, false on failure.
 */
bool CalcLeastSquares(
    const std::vector<std::pair<float, float> >& coordinates, float& slope,
    float& intercept, float& error);

/*
 * This method throws away any points that are more than fCutoff away from
 * the line defined by fSlope and fIntercept.
 */
void FilterHighErrorPoints(
    std::vector<std::pair<float, float> >& coordinates, float slope,
    float intercept, float cutoff);

template <class T1, class T2>
int FindIndex(T1 begin, T1 end, const T2* p) {
  T1 iter = find(begin, end, p);
  if (iter == end) {
    return -1;
  }
  return iter - begin;
}

/* Useful for objects with no operator-, eg. map::iterator (more convenient than
 * advance). */
template <class T>
inline T Increment(T a) {
  ++a;
  return a;
}
template <class T>
inline T Decrement(T a) {
  --a;
  return a;
}

void TrimLeft(std::string& str, const char* trim = "\r\n\t ");
void TrimRight(std::string& str, const char* trim = "\r\n\t ");
void Trim(std::string& str, const char* trim = "\r\n\t ");
void StripCrnl(std::string& str);
bool BeginsWith(const std::string& test_this, const std::string& beginning);
bool EndsWith(const std::string& test_this, const std::string& ending);
std::string URLEncode(const std::string& str);

void StripCvsAndSvn(
    std::vector<std::string>&
        vs);  // Removes various versioning system metafolders.
void StripMacResourceForks(
    std::vector<std::string>& vs);  // Removes files starting with "._"

std::string DerefRedir(const std::string& path);
bool GetFileContents(
    const std::string& path, std::string& out, bool one_line = false);
bool GetFileContents(const std::string& file, std::vector<std::string>& out);

void ReplaceEntityText(
    std::string& text, const std::map<std::string, std::string>& m);
void ReplaceEntityText(std::string& text, const std::map<char, std::string>& m);
void Replace_Unicode_Markers(std::string& text);
std::string WcharDisplayText(wchar_t c);

std::string Basename(const std::string& dir);
std::string Dirname(const std::string& dir);
std::string Capitalize(const std::string& s);

extern unsigned char upper_case[256];

/* ASCII-only case insensitivity. */
struct char_traits_char_nocase : public std::char_traits<char> {
  static inline bool eq(char c1, char c2) {
    return upper_case[(unsigned char)c1] == upper_case[(unsigned char)c2];
  }

  static inline bool ne(char c1, char c2) {
    return upper_case[(unsigned char)c1] != upper_case[(unsigned char)c2];
  }

  static inline bool lt(char c1, char c2) {
    return upper_case[(unsigned char)c1] < upper_case[(unsigned char)c2];
  }

  static int compare(const char* s1, const char* s2, size_t n) {
    int ret = 0;
    while (n--) {
      ret = fasttoupper(*s1++) - fasttoupper(*s2++);
      if (ret != 0) {
        break;
      }
    }
    return ret;
  }

  static inline char fasttoupper(char a) {
    return upper_case[(unsigned char)a];
  }

  static const char* find(const char* s, int n, char a) {
    a = fasttoupper(a);
    while (n-- > 0 && fasttoupper(*s) != a) {
      ++s;
    }

    if (fasttoupper(*s) == a) {
      return s;
    }
    return nullptr;
  }
};
typedef std::basic_string<char, char_traits_char_nocase> istring;

/* Compatibility/convenience shortcuts. These are actually defined in
 * RageFileManager.h, but declared here since they're used in many places. */
void GetDirListing(
    const std::string& path, std::vector<std::string>& add_to,
    bool only_dirs = false, bool return_path_too = false);
void GetDirListingRecursive(
    const std::string& dir, const std::string& match,
    std::vector<std::string>& add_to);        /* returns path too */
bool DeleteRecursive(const std::string& dir); /* delete the dir and all
                                                  files/subdirs inside it */
bool DoesFileExist(const std::string& path);
bool IsAFile(const std::string& path);
bool IsADirectory(const std::string& path);

// call FixSlashesInPlace on any path that came from the user
void FixSlashesInPlace(std::string& path);
void CollapsePath(std::string& path, bool remove_leading_dot = false);

/** @brief Utilities for converting the RStrings. */
namespace StringConversion {
template <typename T>
bool FromString(const std::string& value, T& out);

template <typename T>
std::string ToString(const T& value);

template <>
inline bool FromString<std::string>(
    const std::string& value, std::string& out) {
  out = value;
  return true;
}
template <>
inline std::string ToString<std::string>(const std::string& value) {
  return value;
}
}  // namespace StringConversion

class RageFileBasic;
bool FileCopy(const std::string& src_file, const std::string& dst_file);
bool FileCopy(
    RageFileBasic& in, RageFileBasic& out, std::string& error,
    bool* read_error = nullptr);

template <class T>
void GetAsNotInBs(
    const std::vector<T>& as, const std::vector<T>& bs,
    std::vector<T>& difference) {
  std::vector<T> unmatched = bs;
  // Cannot use FOREACH_CONST here because std::vector<T>::const_iterator is an
  // implicit type.
  for (typename std::vector<T>::const_iterator a = as.begin(); a != as.end();
       ++a) {
    typename std::vector<T>::iterator iter =
        find(unmatched.begin(), unmatched.end(), *a);
    if (iter != unmatched.end()) {
      unmatched.erase(iter);
    } else {
      difference.push_back(*a);
    }
  }
}

template <class T>
void GetConnectsDisconnects(
    const std::vector<T>& before, const std::vector<T>& after,
    std::vector<T>& disconnects, std::vector<T>& connects) {
  GetAsNotInBs(before, after, disconnects);
  GetAsNotInBs(after, before, connects);
}

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2005
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
