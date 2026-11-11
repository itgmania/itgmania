#include "RageUtil.h"

#include <json/json.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "LocalizedString.h"
#include "LuaManager.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageFileDriverDirectHelpers.h"
#include "RageLog.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageUtil/Regex.h"
#include "StdString.h"
#include "global.h"

const std::string kCustomSongPath = "/@mem/";

void UnicodeUpperLower(wchar_t*, size_t, const unsigned char*);

void fapproach(float& val, float other_val, float to_move) {
  ASSERT_M(to_move >= 0, ssprintf("to_move: %f < 0", to_move));
  if (val == other_val) {
    return;
  }
  float delta = other_val - val;
  float sign = delta / std::abs(delta);
  float f_to_move = sign * to_move;
  if (std::abs(f_to_move) > std::abs(delta)) {
    f_to_move = delta;  // snap
  }
  val += f_to_move;
}

/* Return a positive x mod y. */
float fmodfp(float x, float y) {
  x = std::fmod(x, y); /* x is [-y,y] */
  x += y;              /* x is [0,y*2] */
  x = std::fmod(x, y); /* x is [0,y] */
  return x;
}

/* https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2 */
int power_of_two(int v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;

  /* Always be sure to return a value of at least 1. In the event of any edge
   * cases, such as a zero or negative input, the returned value will be `1`. */
  v += (v == 0);
  return v;
}

bool IsAnInt(const std::string& s) {
  if (!s.size()) {
    return false;
  }

  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }

  return true;
}

bool IsHexVal(const std::string& s) {
  if (!s.size()) {
    return false;
  }

  for (size_t i = 0; i < s.size(); ++i) {
    if (!(s[i] >= '0' && s[i] <= '9') &&
        !(toupper(s[i]) >= 'A' && toupper(s[i]) <= 'F')) {
      return false;
    }
  }

  return true;
}

std::string BinaryToHex(const void* raw_data, size_t num_bytes) {
  const unsigned char* data = (const unsigned char*)raw_data;
  std::string s;
  for (size_t i = 0; i < num_bytes; i++) {
    unsigned val = data[i];
    s += ssprintf("%02x", val);
  }
  return s;
}

std::string BinaryToHex(const std::string& string) {
  return BinaryToHex(string.data(), string.size());
}

float HHMMSSToSeconds(const std::string& hhmmss) {
  std::vector<std::string> array_bits;
  split(hhmmss, ":", array_bits, false);

  while (array_bits.size() < 3) {
    array_bits.insert(array_bits.begin(), "0");  // pad missing bits
  }

  float seconds = 0;
  seconds += StringToInt(array_bits[0]) * 60 * 60;
  seconds += StringToInt(array_bits[1]) * 60;
  seconds += StringToFloat(array_bits[2]);

  return seconds;
}

std::string SecondsToHHMMSS(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  std::string s_return = ssprintf(
      "%02d:%02d:%02d", mins_display / 60, mins_display % 60, secs_display);
  return s_return;
}

std::string SecondsToMMSSMsMs(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  const int leftover_display =
      static_cast<int>((secs - mins_display * 60 - secs_display) * 100);
  std::string s_return = ssprintf(
      "%02d:%02d.%02d", mins_display, secs_display,
      std::min(99, leftover_display));
  return s_return;
}

std::string SecondsToMSSMsMs(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  const int leftover_display =
      static_cast<int>((secs - mins_display * 60 - secs_display) * 100);
  std::string s_return = ssprintf(
      "%01d:%02d.%02d", mins_display, secs_display,
      std::min(99, leftover_display));
  return s_return;
}

std::string MicrosecondsToMMSSMsMs(uint64_t usecs) {
  const uint64_t total_seconds = usecs / 1000000;
  const uint64_t mins_display = total_seconds / 60;
  const uint64_t secs_display = total_seconds % 60;
  const uint64_t leftover_display =
      (usecs % 1000000) / 10000;  // Adjusted for two decimal places
  std::string s_return = ssprintf(
      "%02" PRIu64 ":%02" PRIu64 ".%02" PRIu64, mins_display, secs_display,
      std::min<uint64_t>(99, leftover_display));
  return s_return;
}

std::string SecondsToMMSSMsMsMs(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  const int leftover_display =
      static_cast<int>((secs - mins_display * 60 - secs_display) * 1000);
  std::string s_return = ssprintf(
      "%02d:%02d.%03d", mins_display, secs_display,
      std::min(999, leftover_display));
  return s_return;
}

std::string MicrosecondsToMMSSMsMsMs(uint64_t usecs) {
  const uint64_t total_seconds = usecs / 1000000;
  const uint64_t mins_display = total_seconds / 60;
  const uint64_t secs_display = total_seconds % 60;
  const uint64_t leftover_display = (usecs % 1000000) / 1000;
  std::string s_return = ssprintf(
      "%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64, mins_display, secs_display,
      std::min<uint64_t>(999, leftover_display));
  return s_return;
}

std::string SecondsToMSS(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  std::string s_return = ssprintf("%01d:%02d", mins_display, secs_display);
  return s_return;
}

std::string SecondsToMMSS(float secs) {
  const int mins_display = static_cast<int>(secs / 60);
  const int secs_display = static_cast<int>(fmod(secs, 60));
  std::string s_return = ssprintf("%02d:%02d", mins_display, secs_display);
  return s_return;
}

std::string PrettyPercent(float numerator, float denominator) {
  return ssprintf("%0.2f%%", numerator / denominator * 100);
}

std::string Commify(int i_num) {
  std::string s_num = ssprintf("%d", i_num);
  return Commify(s_num);
}

std::string Commify(
    const std::string& num, const std::string& sep, const std::string& dot) {
  size_t num_start = 0;
  size_t num_end = num.size();
  size_t dot_pos = num.find(dot);
  size_t dash_pos = num.find('-');
  if (dot_pos != std::string::npos) {
    num_end = dot_pos;
  }
  if (dash_pos != std::string::npos) {
    num_start = dash_pos + 1;
  }
  size_t num_size = num_end - num_start;
  size_t commies = (num_size / 3) - (!(num_size % 3));
  if (commies < 1) {
    return num;
  }
  size_t commified_len = num.size() + (commies * sep.size());
  std::string ret;
  ret.resize(commified_len);
  size_t dest = 0;
  size_t next_comma = (num_size % 3) + (3 * (!(num_size % 3))) + num_start;
  for (size_t c = 0; c < num.size(); ++c) {
    if (c == next_comma && c < num_end) {
      for (size_t s = 0; s < sep.size(); ++s) {
        ret[dest] = sep[s];
        ++dest;
      }
      next_comma += 3;
    }
    ret[dest] = num[c];
    ++dest;
  }
  return ret;
}

static LocalizedString NUM_PREFIX("RageUtil", "NumPrefix");
static LocalizedString NUM_ST("RageUtil", "NumSt");
static LocalizedString NUM_ND("RageUtil", "NumNd");
static LocalizedString NUM_RD("RageUtil", "NumRd");
static LocalizedString NUM_TH("RageUtil", "NumTh");
std::string FormatNumberAndSuffix(int i) {
  std::string suffix;
  switch (i % 10) {
    case 1:
      suffix = NUM_ST;
      break;
    case 2:
      suffix = NUM_ND;
      break;
    case 3:
      suffix = NUM_RD;
      break;
    default:
      suffix = NUM_TH;
      break;
  }

  // "11th", "113th", etc.
  if (((i % 100) / 10) == 1) {
    suffix = NUM_TH;
  }

  return NUM_PREFIX.GetValue() + ssprintf("%i", i) + suffix;
}

std::string NormalizeDecimal(float num) {
  float mult = 1000.0f;
  float rounded = std::round(num * mult) / mult;
  char buf[64];
  const int len = snprintf(buf, sizeof(buf), "%.3f", rounded);
  return std::string(buf, len);
}

struct tm GetLocalTime() {
  const time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  return tm;
}

std::string ssprintf(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string ret = vssprintf(fmt, va);
  va_end(va);
  return ret;
}

std::string vssprintf(const char* format, va_list arg_list) {
  va_list tmp;
  va_copy(tmp, arg_list);
  int needed = std::vsnprintf(nullptr, 0, format, tmp);
  va_end(tmp);

  std::string ret;
  ret.resize(needed);
  std::vsnprintf(&ret.front(), needed + 1, format, arg_list);
  return ret;
}

std::string join(
    const std::string& deliminator, const std::vector<std::string>& source) {
  if (source.empty()) {
    return std::string();
  }

  std::string tmp;
  size_t final_size = 0;
  size_t delim_size = deliminator.size();
  for (size_t n = 0; n < source.size() - 1; ++n) {
    final_size += source[n].size() + delim_size;
  }
  final_size += source.back().size();
  tmp.reserve(final_size);

  for (unsigned num = 0; num < source.size() - 1; num++) {
    tmp += source[num];
    tmp += deliminator;
  }
  tmp += source.back();
  return tmp;
}

std::string join(
    const std::string& delimitor,
    std::vector<std::string>::const_iterator begin,
    std::vector<std::string>::const_iterator end) {
  if (begin == end) {
    return std::string();
  }

  std::string ret;
  size_t final_size = 0;
  size_t delim_size = delimitor.size();
  for (std::vector<std::string>::const_iterator curr = begin; curr != end;
       ++curr) {
    final_size += curr->size();
    if (curr != end) {
      final_size += delim_size;
    }
  }
  ret.reserve(final_size);

  while (begin != end) {
    ret += *begin;
    ++begin;
    if (begin != end) {
      ret += delimitor;
    }
  }

  return ret;
}

std::string SmEscape(
    const std::string& unescaped, const std::vector<char> chars_to_escape) {
  return SmEscape(unescaped.c_str(), unescaped.size(), chars_to_escape);
}

std::string SmEscape(
    const char* unescaped, int len, const std::vector<char> chars_to_escape) {
  std::string answer = "";
  for (int i = 0; i < len; ++i) {
    if (unescaped[i] == '/' && i + 1 < len && unescaped[i + 1] == '/') {
      answer += "\\/\\/";
      ++i;  // increment here so we skip both //s
      continue;
    }
    for (char escape_char : chars_to_escape) {
      if (unescaped[i] == escape_char) {
        answer += "\\";
        break;
      }
    }

    answer += unescaped[i];
  }
  return answer;
}

std::vector<std::string> SmEscape(
    const std::vector<std::string>& v_unescaped,
    const std::vector<char> chars_to_escape) {
  std::vector<std::string> escaped;
  for (std::string unescaped : v_unescaped) {
    escaped.push_back(SmEscape(unescaped, chars_to_escape));
  }
  return escaped;
}

std::string SmUnescape(const std::string& escaped) {
  std::string unescaped = escaped;
  Replace(unescaped, "\\\\", "||escaped-backslash||");
  Replace(unescaped, "\\", "");
  Replace(unescaped, "||escaped-backslash||", "\\");
  return unescaped;
}

template <class S>
static int DelimitorLength(const S& delimitor) {
  return delimitor.size();
}

static int DelimitorLength(char delimitor) { return 1; }

static int DelimitorLength(wchar_t delimitor) { return 1; }

template <class S, class C>
void do_split(
    const S& source, const C delimitor, std::vector<S>& add_it,
    const bool ignore_empty) {
  /* Short-circuit if the source is empty; we want to return an empty vector if
   * the string is empty, even if bIgnoreEmpty is true. */
  if (source.empty()) {
    return;
  }

  size_t startpos = 0;

  do {
    size_t pos;
    pos = source.find(delimitor, startpos);
    if (pos == source.npos) {
      pos = source.size();
    }

    if (pos > startpos || !ignore_empty) {
      /* Optimization: if we're copying the whole string, avoid substr; this
       * allows this copy to be refcounted, which is much faster. */
      if (startpos == 0 && pos - startpos == source.size()) {
        add_it.push_back(source);
      } else {
        const S add_r_string = source.substr(startpos, pos - startpos);
        add_it.push_back(add_r_string);
      }
    }

    startpos = pos + DelimitorLength(delimitor);
  } while (startpos <= source.size());
}

void split(
    const std::string& source, const std::string& delimitor,
    std::vector<std::string>& add_it, const bool ignore_empty) {
  if (delimitor.size() == 1) {
    do_split(source, delimitor[0], add_it, ignore_empty);
  } else {
    do_split(source, delimitor, add_it, ignore_empty);
  }
}

void split(
    const std::wstring& source, const std::wstring& delimitor,
    std::vector<std::wstring>& add_it, const bool ignore_empty) {
  if (delimitor.size() == 1) {
    do_split(source, delimitor[0], add_it, ignore_empty);
  } else {
    do_split(source, delimitor, add_it, ignore_empty);
  }
}

std::vector<std::string> split(
    const std::string& source, const char delimiter, const bool ignore_empty) {
  std::vector<std::string> result;
  std::string s_delimiter(
      1, delimiter);  // Create an std::string from the char delimiter
  split(source, s_delimiter, result, ignore_empty);
  return result;
}

/* Use:

std::string str="a,b,c";
int start = 0, size = -1;
for(;;)
{
        do_split( str, ",", start, size );
        if( start == str.size() )
                break;
        str[start] = 'Q';
}

*/

template <class S>
void do_split(
    const S& source, const S& delimitor, int& begin, int& size, int len,
    const bool ignore_empty) {
  if (size != -1) {
    // Start points to the beginning of the last delimiter. Move it up.
    begin += size + delimitor.size();
    begin = std::min(begin, len);
  }

  size = 0;

  if (ignore_empty) {
    // Skip delims.
    while (begin + delimitor.size() < source.size() &&
           !source.compare(begin, delimitor.size(), delimitor)) {
      ++begin;
    }
  }

  /* Where's the string function to find within a substring?
   * C++ strings apparently are missing that ... */
  size_t pos;
  if (delimitor.size() == 1) {
    pos = source.find(delimitor[0], begin);
  } else {
    pos = source.find(delimitor, begin);
  }
  if (pos == source.npos || (int)pos > len) {
    pos = len;
  }
  size = pos - begin;
}

void split(
    const std::string& source, const std::string& delimitor, int& begin,
    int& size, int len, const bool ignore_empty) {
  do_split(source, delimitor, begin, size, len, ignore_empty);
}

void split(
    const std::string& source, const std::string& delimitor, int& begin,
    int& size, const bool ignore_empty) {
  do_split(source, delimitor, begin, size, source.size(), ignore_empty);
}

/*
 * foo\fum\          -> "foo\fum\", "", ""
 * c:\foo\bar.txt    -> "c:\foo\", "bar", ".txt"
 * \\foo\fum         -> "\\foo\", "fum", ""
 */
void splitpath(
    const std::string& path, std::string& dir, std::string& filename,
    std::string& ext) {
  dir = filename = ext = "";

  std::vector<std::string> matches;

  /*
   * One level of escapes for the regex, one for C. Ew.
   * This is really:
   * ^(.*[\\/])?(.*)$
   */
  static Regex sep("^(.*[\\\\/])?(.*)$");
  bool check = sep.Compare(path, matches);
  ASSERT(check);

  dir = matches[0];
  const std::string base = matches[1];

  /* ^(.*)(\.[^\.]+)$ */
  static Regex split_ext("^(.*)(\\.[^\\.]+)$");
  if (split_ext.Compare(base, matches)) {
    filename = matches[0];
    ext = matches[1];
  } else {
    filename = base;
  }
}

std::string custom_songify_path(const std::string& path) {
  std::vector<std::string> parts;
  split(path, "/", parts, false);
  if (parts.size() < 2) {
    return kCustomSongPath + path;
  }
  return kCustomSongPath + parts[parts.size() - 2] + "/" +
         parts[parts.size() - 1];
}

/* "foo.bar", "baz" -> "foo.baz"
 * "foo", "baz" -> "foo.baz"
 * "foo.bar", "" -> "foo" */
std::string SetExtension(const std::string& path, const std::string& ext) {
  std::string dir, file_name, old_ext;
  splitpath(path, dir, file_name, old_ext);
  return dir + file_name + (ext.size() ? "." : "") + ext;
}

std::string GetExtension(const std::string& path) {
  size_t pos = path.rfind('.');
  if (pos == path.npos) {
    return std::string();
  }

  size_t slash = path.find('/', pos);
  if (slash != path.npos) {
    return std::string(); /* rare: path/dir.ext/fn */
  }

  return path.substr(pos + 1, path.size() - pos + 1);
}

std::string GetFileNameWithoutExtension(const std::string& path) {
  std::string throw_away, f_name;
  splitpath(path, throw_away, f_name, throw_away);
  return f_name;
}

void MakeValidFilename(std::string& s_name) {
  std::wstring ws_name = RStringToWstring(s_name);
  std::wstring invalid = L"/\\:*?\"<>|";
  for (unsigned i = 0; i < ws_name.size(); ++i) {
    wchar_t w = ws_name[i];
    if (w >= 32 && w < 126 && invalid.find_first_of(w) == invalid.npos) {
      continue;
    }

    if (w == L'"') {
      ws_name[i] = L'\'';
      continue;
    }

    /* We could replace with closest matches in ASCII: convert the character
     * to UTF-8 NFD (decomposed) (maybe NFKD?), and see if the first
     * character is ASCII. This is useless for non-Western languages,
     * since we'll replace the whole filename. */
    ws_name[i] = '_';
  }

  s_name = WStringToRString(ws_name);
}

bool FindFirstFilenameContaining(
    const std::vector<std::string>& filenames, std::string& out,
    const std::vector<std::string>& starts_with,
    const std::vector<std::string>& contains,
    const std::vector<std::string>& ends_with) {
  for (size_t i = 0; i < filenames.size(); ++i) {
    std::string lower = GetFileNameWithoutExtension(filenames[i]);
    MakeLower(lower);
    for (size_t s = 0; s < starts_with.size(); ++s) {
      if (!lower.compare(0, starts_with[s].size(), starts_with[s])) {
        out = filenames[i];
        return true;
      }
    }
    size_t lower_size = lower.size();
    for (size_t s = 0; s < ends_with.size(); ++s) {
      if (lower_size >= ends_with[s].size()) {
        size_t end_pos = lower_size - ends_with[s].size();
        if (!lower.compare(end_pos, std::string::npos, ends_with[s])) {
          out = filenames[i];
          return true;
        }
      }
    }
    for (size_t s = 0; s < contains.size(); ++s) {
      if (lower.find(contains[s]) != std::string::npos) {
        out = filenames[i];
        return true;
      }
    }
  }
  return false;
}

int g_argc = 0;
char** g_argv = nullptr;

void SetCommandlineArguments(int argc, char** argv) {
  g_argc = argc;
  g_argv = argv;
}

/* Search for the commandline argument given; eg. "test" searches for the
 * option "--test".  All commandline arguments are getopt_long style: --foo;
 * short arguments (-x) are not supported.  (These are not intended for
 * common, general use, so having short options isn't currently needed.)
 * If argument is non-nullptr, accept an argument. */
bool GetCommandlineArgument(
    const std::string& option, std::string* argument, int index) {
  const std::string optstr = "--" + option;

  for (int arg = 1; arg < g_argc; ++arg) {
    const std::string cur_argument = g_argv[arg];

    const size_t i = cur_argument.find("=");
    std::string cur_option = cur_argument.substr(0, i);
    if (CompareNoCase(cur_option, optstr)) {
      continue;  // no match
    }

    // Found it.
    if (index) {
      --index;
      continue;
    }

    if (argument) {
      if (i != std::string::npos) {
        *argument = cur_argument.substr(i + 1);
      } else {
        *argument = "";
      }
    }

    return true;
  }

  return false;
}

std::string GetCwd() {
  char buf[PATH_MAX];
  bool ret = DoGetCwd(buf, PATH_MAX) != nullptr;
  ASSERT(ret);
  return buf;
}

/*
 * Calculate a standard CRC32.  iCRC should be initialized to 0.
 * References:
 *   http://www.theorem.com/java/CRC32.java,
 *   http://www.faqs.org/rfcs/rfc1952.html
 */
void CRC32(unsigned int& crc, const void* void_buffer, size_t size) {
  static unsigned tab[256];
  static std::atomic<bool> initted = false;
  if (!initted.load(std::memory_order_acquire)) {
    const unsigned poly = 0xEDB88320;

    for (int i = 0; i < 256; ++i) {
      tab[i] = i;
      for (int j = 0; j < 8; ++j) {
        if (tab[i] & 1) {
          tab[i] = (tab[i] >> 1) ^ poly;
        } else {
          tab[i] >>= 1;
        }
      }
    }
    initted.store(true, std::memory_order_release);
  }

  crc ^= 0xFFFFFFFF;

  const char* buffer = (const char*)void_buffer;
  for (unsigned i = 0; i < size; ++i) {
    crc = (crc >> 8) ^ tab[(crc ^ buffer[i]) & 0xFF];
  }

  crc ^= 0xFFFFFFFF;
}

unsigned int GetHashForString(const std::string& s) {
  unsigned crc = 0;
  CRC32(crc, s.data(), s.size());
  return crc;
}

/* Return true if "dir" is empty or does not exist. */
bool DirectoryIsEmpty(const std::string& dir) {
  if (dir.empty()) {
    return true;
  }
  if (!DoesFileExist(dir)) {
    return true;
  }

  std::vector<std::string> file_names;
  GetDirListing(dir, file_names);
  return file_names.empty();
}

bool CompareRStringsAsc(const std::string& str1, const std::string& str2) {
  return CompareNoCase(str1, str2) < 0;
}

bool CompareRStringsDesc(const std::string& str1, const std::string& str2) {
  return CompareNoCase(str1, str2) > 0;
}

void SortRStringArray(
    std::vector<std::string>& array_r_strings, const bool sort_ascending) {
  sort(
      array_r_strings.begin(), array_r_strings.end(),
      sort_ascending ? CompareRStringsAsc : CompareRStringsDesc);
}

float calc_mean(const float* start, const float* end) {
  /* The Kahan summation algorithm is used here to prevent
   * situations where the low order bits may be lost.
   * https://en.wikipedia.org/wiki/Kahan_summation_algorithm */

  if (start == end) {
    return 0.0f;
  }

  float sum = 0.0f;
  float c = 0.0f;
  for (const float* p = start; p != end; ++p) {
    float y = *p - c;
    float t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }
  return sum / (end - start);
}

float calc_stddev(const float* start, const float* end, bool sample) {
  /* Calculate the mean. */
  float mean = calc_mean(start, end);

  /* Calculate stddev. */
  float dev = 0.0f;
  for (const float* i = start; i != end; ++i) {
    dev += (*i - mean) * (*i - mean);
  }
  dev /= std::distance(start, end) - (sample ? 1 : 0);
  dev = std::sqrt(dev);

  return dev;
}

bool CalcLeastSquares(
    const std::vector<std::pair<float, float>>& coordinates, float& slope,
    float& intercept, float& error) {
  if (coordinates.empty()) {
    return false;
  }
  float sum_xx = 0.0f, sum_xy = 0.0f, sum_x = 0.0f, sum_y = 0.0f;
  for (unsigned i = 0; i < coordinates.size(); ++i) {
    sum_xx += coordinates[i].first * coordinates[i].first;
    sum_xy += coordinates[i].first * coordinates[i].second;
    sum_x += coordinates[i].first;
    sum_y += coordinates[i].second;
  }
  const float denominator = coordinates.size() * sum_xx - sum_x * sum_x;
  slope = (coordinates.size() * sum_xy - sum_x * sum_y) / denominator;
  intercept = (sum_xx * sum_y - sum_x * sum_xy) / denominator;

  error = 0.0f;
  for (unsigned i = 0; i < coordinates.size(); ++i) {
    const float one_error =
        intercept + slope * coordinates[i].first - coordinates[i].second;
    error += one_error * one_error;
  }
  error /= coordinates.size();
  error = std::sqrt(error);
  return true;
}

void FilterHighErrorPoints(
    std::vector<std::pair<float, float>>& coordinates, float slope,
    float intercept, float cutoff) {
  unsigned int out = 0;
  for (unsigned int in = 0; in < coordinates.size(); ++in) {
    const float error =
        intercept + slope * coordinates[in].first - coordinates[in].second;
    if (std::abs(error) < cutoff) {
      coordinates[out] = coordinates[in];
      ++out;
    }
  }
  coordinates.resize(out);
}

void TrimLeft(std::string& str, const char* s) {
  int n = 0;
  while (n < int(str.size()) && strchr(s, str[n])) {
    n++;
  }

  str.erase(str.begin(), str.begin() + n);
}

void TrimRight(std::string& str, const char* s) {
  int n = str.size();
  while (n > 0 && strchr(s, str[n - 1])) {
    n--;
  }

  /* Delete from n to the end. If n == sStr.size(), nothing is deleted;
   * if n == 0, the whole string is erased. */
  str.erase(str.begin() + n, str.end());
}

void Trim(std::string& str, const char* s) {
  std::string::size_type b = 0, e = str.size();
  while (b < e && strchr(s, str[b])) {
    ++b;
  }
  while (b < e && strchr(s, str[e - 1])) {
    --e;
  }
  str.assign(str.substr(b, e - b));
}

void StripCrnl(std::string& s) {
  while (s.size() && (s[s.size() - 1] == '\r' || s[s.size() - 1] == '\n')) {
    s.erase(s.size() - 1);
  }
}

bool BeginsWith(const std::string& test_this, const std::string& beginning) {
  ASSERT(!beginning.empty());
  return test_this.compare(0, beginning.length(), beginning) == 0;
}

bool EndsWith(const std::string& test_this, const std::string& ending) {
  ASSERT(!ending.empty());
  if (test_this.size() < ending.size()) {
    return false;
  }
  return test_this.compare(
             test_this.length() - ending.length(), ending.length(), ending) ==
         0;
}

std::string URLEncode(const std::string& str) {
  std::string output;
  for (unsigned k = 0; k < str.size(); k++) {
    char t = str[k];
    if (t >= '!' && t <= 'z') {
      output += t;
    } else {
      output += "%" + ssprintf("%02X", t);
    }
  }
  return output;
}

// remove various version control-related files
static bool CVSOrSVN(const std::string& s) {
  return EqualsNoCase(Right(s, 3), "CVS") || Right(s, 4) == ".svn" ||
         EqualsNoCase(Right(s, 3), ".hg");
}

void StripCvsAndSvn(std::vector<std::string>& vs) { RemoveIf(vs, CVSOrSVN); }

static bool MacResourceFork(const std::string& s) {
  return EqualsNoCase(Left(s, 2), "._");
}

void StripMacResourceForks(std::vector<std::string>& vs) {
  RemoveIf(vs, MacResourceFork);
}

// path is a .redir pathname. Read it and return the real one.
std::string DerefRedir(const std::string& path) {
  std::string s_path = path;

  for (int i = 0; i < 100; i++) {
    if (GetExtension(s_path) != "redir") {
      return s_path;
    }

    std::string new_file_name;
    GetFileContents(s_path, new_file_name, true);

    // Empty is invalid.
    if (new_file_name == "") {
      return std::string();
    }

    std::string path2 = Dirname(s_path) + new_file_name;

    CollapsePath(path2);

    path2 += "*";

    std::vector<std::string> matches;
    GetDirListing(path2, matches, false, true);

    if (matches.empty()) {
      RageException::Throw(
          "The redirect \"%s\" references a file \"%s\" which doesn't exist.",
          s_path.c_str(), path2.c_str());
    } else if (matches.size() > 1) {
      RageException::Throw(
          "The redirect \"%s\" references a file \"%s\" with multiple matches.",
          s_path.c_str(), path2.c_str());
    }

    s_path = matches[0];
  }

  RageException::Throw("Circular redirect \"%s\".", s_path.c_str());
}

bool GetFileContents(const std::string& path, std::string& out, bool one_line) {
  // Don't warn if the file doesn't exist, but do warn if it exists and fails to
  // open.
  if (!IsAFile(path)) {
    return false;
  }

  RageFile file;
  if (!file.Open(path)) {
    LOG->Warn("GetFileContents(%s): %s", path.c_str(), file.GetError().c_str());
    return false;
  }

  // todo: figure out how to make this UTF-8 safe. -aj
  std::string data;
  int got;
  if (one_line) {
    got = file.GetLine(data);
  } else {
    got = file.Read(data, file.GetFileSize());
  }

  if (got == -1) {
    LOG->Warn("GetFileContents(%s): %s", path.c_str(), file.GetError().c_str());
    return false;
  }

  if (one_line) {
    StripCrnl(data);
  }

  out = data;
  return true;
}

bool GetFileContents(const std::string& s_file, std::vector<std::string>& out) {
  RageFile file;
  if (!file.Open(s_file)) {
    LOG->Warn(
        "GetFileContents(%s): %s", s_file.c_str(), file.GetError().c_str());
    return false;
  }

  std::string line;
  while (file.GetLine(line)) {
    out.push_back(line);
  }
  return true;
}

/* Given a UTF-8 byte, return the length of the codepoint (if a start code)
 * or 0 if it's a continuation byte. */
int utf8_get_char_len(char p) {
  if (!(p & 0x80)) {
    return 1; /* 0xxxxxxx - 1 */
  }
  if (!(p & 0x40)) {
    return 1; /* 10xxxxxx - continuation */
  }
  if (!(p & 0x20)) {
    return 2; /* 110xxxxx */
  }
  if (!(p & 0x10)) {
    return 3; /* 1110xxxx */
  }
  if (!(p & 0x08)) {
    return 4; /* 11110xxx */
  }
  if (!(p & 0x04)) {
    return 5; /* 111110xx */
  }
  if (!(p & 0x02)) {
    return 6; /* 1111110x */
  }
  return 1; /* 1111111x */
}

static inline bool is_utf8_continuation_byte(char c) {
  return (c & 0xC0) == 0x80;
}

/* Decode one codepoint at start; advance start and place the result in ch.
 * If the encoded string is invalid, false is returned. */
bool utf8_to_wchar_ec(const std::string& s, unsigned& start, wchar_t& ch) {
  if (start >= s.size()) {
    return false;
  }

  if (is_utf8_continuation_byte(s[start]) || /* misplaced continuation byte */
      (s[start] & 0xFE) == 0xFE)             /* 0xFE, 0xFF */
  {
    start += 1;
    return false;
  }

  int len = utf8_get_char_len(s[start]);

  const int first_byte_mask[] = {-1, 0x7F, 0x1F, 0x0F, 0x07, 0x03, 0x01};

  ch = wchar_t(s[start] & first_byte_mask[len]);

  for (int i = 1; i < len; ++i) {
    if (start + i >= s.size()) {
      /* We expected a continuation byte, but didn't get one. Return error, and
       * point start at the unexpected byte; it's probably a new sequence. */
      start += i;
      return false;
    }

    char byte = s[start + i];
    if (!is_utf8_continuation_byte(byte)) {
      /* We expected a continuation byte, but didn't get one. Return error, and
       * point start at the unexpected byte; it's probably a new sequence. */
      start += i;
      return false;
    }
    ch = (ch << 6) | (byte & 0x3F);
  }

  bool valid = true;
  {
    unsigned c1 = (unsigned)s[start] & 0xFF;
    unsigned c2 = (unsigned)s[start + 1] & 0xFF;
    int c = (c1 << 8) + c2;
    if ((c & 0xFE00) == 0xC000 || (c & 0xFFE0) == 0xE080 ||
        (c & 0xFFF0) == 0xF080 || (c & 0xFFF8) == 0xF880 ||
        (c & 0xFFFC) == 0xFC80) {
      valid = false;
    }
  }

  if (ch == 0xFFFE || ch == 0xFFFF) {
    valid = false;
  }

  start += len;
  return valid;
}

/* Like utf8_to_wchar_ec, but only does enough error checking to prevent
 * crashing. */
bool utf8_to_wchar(const char* s, size_t length, unsigned& start, wchar_t& ch) {
  if (start >= length) {
    return false;
  }

  int len = utf8_get_char_len(s[start]);

  if (start + len > length) {
    // We don't have room for enough continuation bytes. Return error.
    start += len;
    ch = L'?';
    return false;
  }

  switch (len) {
    case 1:
      ch = (s[start + 0] & 0x7F);
      break;
    case 2:
      ch = ((s[start + 0] & 0x1F) << 6) | (s[start + 1] & 0x3F);
      break;
    case 3:
      ch = ((s[start + 0] & 0x0F) << 12) | ((s[start + 1] & 0x3F) << 6) |
           (s[start + 2] & 0x3F);
      break;
    case 4:
      ch = ((s[start + 0] & 0x07) << 18) | ((s[start + 1] & 0x3F) << 12) |
           ((s[start + 2] & 0x3F) << 6) | (s[start + 3] & 0x3F);
      break;
    case 5:
      ch = ((s[start + 0] & 0x03) << 24) | ((s[start + 1] & 0x3F) << 18) |
           ((s[start + 2] & 0x3F) << 12) | ((s[start + 3] & 0x3F) << 6) |
           (s[start + 4] & 0x3F);
      break;

    case 6:
      ch = ((s[start + 0] & 0x01) << 30) | ((s[start + 1] & 0x3F) << 24) |
           ((s[start + 2] & 0x3F) << 18) | ((s[start + 3] & 0x3F) << 12) |
           ((s[start + 4] & 0x3F) << 6) | (s[start + 5] & 0x3F);
      break;
  }

  start += len;
  return true;
}

// UTF-8 encode ch and append to out.
void wchar_to_utf8(wchar_t ch, std::string& out) {
  if (ch < 0x80) {
    out.append(1, (char)ch);
    return;
  }

  int cbytes = 0;
  if (ch < 0x800) {
    cbytes = 1;
  } else if (ch < 0x10000) {
    cbytes = 2;
  } else if (ch < 0x200000) {
    cbytes = 3;
  } else if (ch < 0x4000000) {
    cbytes = 4;
  } else {
    cbytes = 5;
  }

  {
    int shift = cbytes * 6;
    const int init_masks[] = {0xC0, 0xE0, 0xF0, 0xF8, 0xFC};
    out.append(1, (char)(init_masks[cbytes - 1] | (ch >> shift)));
  }

  for (int i = 0; i < cbytes; ++i) {
    int shift = (cbytes - i - 1) * 6;
    out.append(1, (char)(0x80 | ((ch >> shift) & 0x3F)));
  }
}

wchar_t utf8_get_char(const std::string& s) {
  unsigned start = 0;
  wchar_t ret;
  if (!utf8_to_wchar_ec(s, start, ret)) {
    return kInvalidChar;
  }
  return ret;
}

bool utf8_is_valid(const std::string& s) {
  for (unsigned start = 0; start < s.size();) {
    wchar_t ch;
    if (!utf8_to_wchar_ec(s, start, ch)) {
      return false;
    }
  }
  return true;
}

/* Windows tends to drop garbage BOM characters at the start of UTF-8 text
 * files. Remove them. */
void utf8_remove_bom(std::string& line) {
  if (!line.compare(0, 3, "\xef\xbb\xbf")) {
    line.erase(0, 3);
  }
}

static int UnicodeDoUpper(
    char* p, size_t len, const unsigned char mapping[256]) {
  // Note: this has problems with certain accented characters. -aj
  wchar_t wc = L'\0';
  unsigned start = 0;
  if (!utf8_to_wchar(p, len, start, wc)) {
    return 1;
  }

  wchar_t upper = wc;
  if (wc < 256) {
    upper = mapping[wc];
  }
  if (upper != wc) {
    std::string out;
    wchar_to_utf8(upper, out);
    if (out.size() == start) {
      memcpy(p, out.data(), out.size());
    } else {
      WARN(ssprintf(
          "UnicodeDoUpper: invalid character at \"%s\"",
          std::string(p, len).c_str()));
    }
  }

  return start;
}

/* Fast in-place MakeUpper and MakeLower. This only replaces characters with
 * characters of the same UTF-8 length, so we never have to move the whole
 * string. This is optimized for strings that have no non-ASCII characters. */
void MakeUpper(char* p, size_t len) {
  char* start = p;
  char* end = p + len;
  while (p < end) {
    // Fast path:
    if (likely(!(*p & 0x80))) {
      if (unlikely(*p >= 'a' && *p <= 'z')) {
        *p += 'A' - 'a';
      }
      ++p;
      continue;
    }

    int remaining = len - (p - start);
    p += UnicodeDoUpper(p, remaining, upper_case);
  }
}

void UnicodeUpperLower(
    wchar_t* p, size_t len, const unsigned char mapping[256]) {
  wchar_t* end = p + len;
  while (p != end) {
// wchar_t can be signed or unsigned depending on the platform and the compiler.
// We use WCHAR_MIN to determine a valid condition that won't emit a type-limits
// warning.
#if WCHAR_MIN != 0
    if (*p >= 0 && *p < 256) {
#else
    if (*p < 256) {
#endif
      *p = mapping[*p];
    }
    ++p;
  }
}

void MakeUpper(wchar_t* p, size_t len) {
  UnicodeUpperLower(p, len, upper_case);
}

float StringToFloat(const std::string& string) {
  float out = std::strtof(string.c_str(), nullptr);
  if (!std::isfinite(out)) {
    out = 0.0f;
  }
  return out;
}

bool StringToFloat(const std::string& string, float& out) {
  char* end_ptr = nullptr;

  out = std::strtof(string.c_str(), &end_ptr);
  return string.size() && *end_ptr == '\0' && std::isfinite(out);
}

int StringToInt(const std::string& str, size_t* pos, int base, int except_val) {
  try {
    return std::stoi(str, pos, base);
  } catch (const std::invalid_argument& e) {
    LOG->Warn("stoi(%s): %s", str.c_str(), e.what());
  } catch (const std::out_of_range& e) {
    LOG->Warn("stoi(%s): %s", str.c_str(), e.what());
  }
  return except_val;
}

const wchar_t kInvalidChar = 0xFFFD; /* U+FFFD REPLACEMENT CHARACTER */

std::wstring RStringToWstring(const std::string& s) {
  std::wstring ret;
  ret.reserve(s.size());
  for (unsigned start = 0; start < s.size();) {
    char c = s[start];
    if (!(c & 0x80)) {
      // ASCII fast path
      ret += c;
      ++start;
      continue;
    }

    wchar_t ch = L'\0';
    if (!utf8_to_wchar(s.data(), s.size(), start, ch)) {
      ch = kInvalidChar;
    }
    ret += ch;
  }

  return ret;
}

std::string WStringToRString(const std::wstring& str) {
  std::string ret;

  for (unsigned i = 0; i < str.size(); ++i) {
    wchar_to_utf8(str[i], ret);
  }

  return ret;
}

std::string WcharToUTF8(wchar_t c) {
  std::string ret;
  wchar_to_utf8(c, ret);
  return ret;
}

// &a; -> a
void ReplaceEntityText(
    std::string& text, const std::map<std::string, std::string>& m) {
  std::string ret;

  size_t offset = 0;
  while (offset != text.size()) {
    size_t start = text.find('&', offset);
    if (start == text.npos) {
      // Optimization: if we didn't replace anything at all, do nothing.
      if (offset == 0) {
        return;
      }

      // Append the rest of the string.
      ret.append(text, offset, ret.npos);
      break;
    }

    // Append the text between iOffset and iStart.
    ret.append(text, offset, start - offset);
    offset += start - offset;

    // Optimization: stop early on "&", so "&&&&&&&&&&&" isn't n^2.
    size_t end = text.find_first_of("&;", start + 1);
    if (end == text.npos || text[end] == '&') {
      // & with no matching ;, or two & in a row. Append the & and continue.
      ret.append(text, start, 1);
      ++offset;
      continue;
    }

    std::string element = text.substr(start + 1, end - start - 1);
    MakeLower(element);

    std::map<std::string, std::string>::const_iterator it = m.find(element);
    if (it == m.end()) {
      ret.append(text, start, end - start + 1);
      offset = end + 1;
      continue;
    }

    const std::string& to = it->second;
    ret.append(to);
    offset = end + 1;
  }

  text = ret;
}

// abcd -> &a; &b; &c; &d;
void ReplaceEntityText(
    std::string& text, const std::map<char, std::string>& m) {
  std::string find;

  for (const std::pair<const char, std::string>& c : m) {
    find.append(1, c.first);
  }

  std::string ret;

  size_t offset = 0;
  while (offset != text.size()) {
    size_t start = text.find_first_of(find, offset);
    if (start == text.npos) {
      // Optimization: if we didn't replace anything at all, do nothing.
      if (offset == 0) {
        return;
      }

      // Append the rest of the string.
      ret.append(text, offset, ret.npos);
      break;
    }

    // Append the text between iOffset and iStart.
    ret.append(text, offset, start - offset);
    offset += start - offset;

    char element = text[start];

    std::map<char, std::string>::const_iterator it = m.find(element);
    ASSERT(it != m.end());

    const std::string& to = it->second;
    ret.append(1, '&');
    ret.append(to);
    ret.append(1, ';');
    ++offset;
  }

  text = ret;
}

// Replace &#nnnn; (decimal) and &xnnnn; (hex) with corresponding UTF-8
// characters.
void Replace_Unicode_Markers(std::string& text) {
  unsigned start = 0;
  while (start < text.size()) {
    // Look for &#digits;
    bool hex = false;
    size_t pos = text.find("&#", start);
    if (pos == text.npos) {
      hex = true;
      pos = text.find("&x", start);
    }

    if (pos == text.npos) {
      break;
    }
    start = pos + 1;

    unsigned p = pos;
    p += 2;

    // Found &# or &x. Is it followed by digits and a semicolon?
    if (p >= text.size()) {
      continue;
    }

    int num_digits = 0;
    while (p < text.size() && hex ? isxdigit(text[p]) : isdigit(text[p])) {
      p++;
      num_digits++;
    }

    if (!num_digits) {
      continue;  // must have at least one digit
    }
    if (p >= text.size() || text[p] != ';') {
      continue;
    }
    p++;

    unsigned int num;
    sscanf(text.c_str() + pos, hex ? "&x%x;" : "&#%u;", &num);
    if (num > 0xFFFF) {
      num = kInvalidChar;
    }

    text.replace(pos, p - pos, WcharToUTF8(wchar_t(num)));
  }
}

// Form a string to identify a wchar_t with ASCII.
std::string WcharDisplayText(wchar_t c) {
  std::string chr;
  chr = ssprintf("U+%4.4x", c);
  if (c < 128) {
    chr += ssprintf(" ('%c')", char(c));
  }
  return chr;
}

/* Return the last named component of dir:
 * a/b/c -> c
 * a/b/c/ -> c
 */
std::string Basename(const std::string& dir) {
  size_t end = dir.find_last_not_of("/\\");
  if (end == dir.npos) {
    return std::string();
  }

  size_t start = dir.find_last_of("/\\", end);
  if (start == dir.npos) {
    start = 0;
  } else {
    ++start;
  }

  return dir.substr(start, end - start + 1);
}

/* Return all but the last named component of dir:
 *
 * a/b/c -> a/b/
 * a/b/c/ -> a/b/
 * c/ -> ./
 * /foo -> /
 * / -> /
 */
std::string Dirname(const std::string& dir) {
  // Special case: "/" -> "/".
  if (dir.size() == 1 && dir[0] == '/') {
    return "/";
  }

  int pos = dir.size() - 1;
  // Skip trailing slashes.
  while (pos >= 0 && dir[pos] == '/') {
    --pos;
  }

  // Skip the last component.
  while (pos >= 0 && dir[pos] != '/') {
    --pos;
  }

  if (pos < 0) {
    return "./";
  }

  return dir.substr(0, pos + 1);
}

std::string Capitalize(const std::string& s) {
  if (s.empty()) {
    return std::string();
  }

  char* buf = const_cast<char*>(s.c_str());

  UnicodeDoUpper(buf, s.size(), upper_case);

  return buf;
}

unsigned char upper_case[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
    0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xC0, 0xC1, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xF7, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xFF,
};

void FixSlashesInPlace(std::string& path) {
  for (unsigned i = 0; i < path.size(); ++i) {
    if (path[i] == '\\') {
      path[i] = '/';
    }
  }
}

/* Keep trailing slashes, since that can be used to illustrate that a path
 * always represents a directory.
 *
 * foo/bar -> foo/bar
 * foo/bar/ -> foo/bar/
 * foo///bar/// -> foo/bar/
 * foo/bar/./baz -> foo/bar/baz
 * foo/bar/../baz -> foo/baz
 * ../foo -> ../foo
 * ../../foo -> ../../foo
 * ./foo -> foo (if bRemoveLeadingDot), ./foo (if !bRemoveLeadingDot)
 * ./ -> ./
 * ./// -> ./
 */

void CollapsePath(std::string& path, bool remove_leading_dot) {
  std::string out;
  out.reserve(path.size());

  size_t pos = 0;
  size_t next;
  for (; pos < path.size(); pos = next) {
    // Find the next slash.
    next = path.find('/', pos);
    if (next == std::string::npos) {
      next = path.size();
    } else {
      ++next;
    }

    /* Strip extra slashes, but don't remove slashes from the beginning of the
     * string. */
    if (next - pos == 1 && path[pos] == '/') {
      if (!out.empty()) {
        continue;
      }
    }

    // If this is a dot, skip it.
    if (next - pos == 2 && path[pos] == '.' && path[pos + 1] == '/') {
      if (remove_leading_dot || !out.empty()) {
        continue;
      }
    }

    // If this is two dots,
    if (next - pos == 3 && path[pos] == '.' && path[pos + 1] == '.' &&
        path[pos + 2] == '/') {
      /* If this is the first path element (nothing to delete),
       * or all we have is a slash, leave it. */
      if (out.empty() || (out.size() == 1 && out[0] == '/')) {
        out.append(path, pos, next - pos);
        continue;
      }

      // Search backwards for the previous path element.
      size_t prev = out.rfind('/', out.size() - 2);
      if (prev == std::string::npos) {
        prev = 0;
      } else {
        ++prev;
      }

      // If the previous element is also .., leave it.
      bool last_is_two_dots =
          (out.size() - prev == 3 && out[prev] == '.' && out[prev + 1] == '.');
      if (last_is_two_dots) {
        out.append(path, pos, next - pos);
        continue;
      }

      out.erase(prev);
      continue;
    }

    out.append(path, pos, next - pos);
  }

  out.swap(path);
}

namespace StringConversion {
template <>
bool FromString<int>(const std::string& value, int& out) {
  if (sscanf(value.c_str(), "%d", &out) == 1) {
    return true;
  }

  out = 0;
  return false;
}

template <>
bool FromString<unsigned>(const std::string& value, unsigned& out) {
  if (sscanf(value.c_str(), "%u", &out) == 1) {
    return true;
  }

  out = 0;
  return false;
}

template <>
bool FromString<float>(const std::string& value, float& out) {
  const char* endptr = value.data() + value.size();
  out = strtof(value.c_str(), (char**)&endptr);
  if (endptr != value.data() && std::isfinite(out)) {
    return true;
  }
  out = 0;
  return false;
}

template <>
bool FromString<bool>(const std::string& value, bool& out) {
  if (value.size() == 0) {
    return false;
  }

  out = StringToInt(value) != 0;
  return true;
}

template <>
std::string ToString<int>(const int& value) {
  return ssprintf("%i", value);
}

template <>
std::string ToString<unsigned>(const unsigned& value) {
  return ssprintf("%u", value);
}

template <>
std::string ToString<float>(const float& value) {
  return ssprintf("%f", value);
}

template <>
std::string ToString<bool>(const bool& value) {
  return ssprintf("%i", value);
}
}  // namespace StringConversion

bool FileCopy(const std::string& src_file, const std::string& dst_file) {
  if (!CompareNoCase(src_file, dst_file)) {
    LOG->Warn("Tried to copy \"%s\" over itself", src_file.c_str());
    return false;
  }

  RageFile in;
  if (!in.Open(src_file, RageFile::READ)) {
    return false;
  }

  RageFile out;
  if (!out.Open(dst_file, RageFile::WRITE)) {
    return false;
  }

  std::string error;
  if (!FileCopy(in, out, error)) {
    LOG->Warn(
        "FileCopy(%s,%s): %s", src_file.c_str(), dst_file.c_str(),
        error.c_str());
    return false;
  }

  return true;
}

bool FileCopy(
    RageFileBasic& in, RageFileBasic& out, std::string& error,
    bool* read_error) {
  for (;;) {
    std::string data;
    if (in.Read(data, 1024 * 32) == -1) {
      error = ssprintf("read error: %s", in.GetError().c_str());
      if (read_error != nullptr) {
        *read_error = true;
      }
      return false;
    }
    if (data.empty()) {
      break;
    }
    int i = out.Write(data);
    if (i == -1) {
      error = ssprintf("write error: %s", out.GetError().c_str());
      if (read_error != nullptr) {
        *read_error = false;
      }
      return false;
    }
  }

  if (out.Flush() == -1) {
    error = ssprintf("write error: %s", out.GetError().c_str());
    if (read_error != nullptr) {
      *read_error = false;
    }
    return false;
  }

  return true;
}

LuaFunction(SecondsToMSSMsMs, SecondsToMSSMsMs(FArg(1)))
    LuaFunction(SecondsToHHMMSS, SecondsToHHMMSS(FArg(1)))
        LuaFunction(SecondsToMMSSMsMs, SecondsToMMSSMsMs(FArg(1))) LuaFunction(
            SecondsToMMSSMsMsMs, SecondsToMMSSMsMsMs(FArg(1)))
            LuaFunction(SecondsToMSS, SecondsToMSS(FArg(1)))
                LuaFunction(SecondsToMMSS, SecondsToMMSS(FArg(1))) LuaFunction(
                    FormatNumberAndSuffix, FormatNumberAndSuffix(IArg(1)))
                    LuaFunction(Basename, Basename(SArg(1)))
                        LuaFunction(Lowercase, MakeLower(SArg(1)))
                            LuaFunction(Uppercase, MakeUpper(SArg(1)))
                                LuaFunction(
                                    mbstrlen,
                                    (int)RStringToWstring(SArg(1)).length())
                                    LuaFunction(URLEncode, URLEncode(SArg(1)));
LuaFunction(PrettyPercent, PrettyPercent(FArg(1), FArg(2)));
// LuaFunction( IsHexVal, IsHexVal( SArg(1) ) );
LuaFunction(lerp, lerp(FArg(1), FArg(2), FArg(3)));

int LuaFunc_BinaryToHex(lua_State* L);
int LuaFunc_BinaryToHex(lua_State* L) {
  size_t l;
  const char* s = luaL_checklstring(L, 1, &l);

  std::string hex = BinaryToHex(s, l);

  LuaHelpers::Push(L, hex);
  return 1;
}
LUAFUNC_REGISTER_COMMON(BinaryToHex);

int LuaFunc_commify(lua_State* L);
int LuaFunc_commify(lua_State* L) {
  std::string num = SArg(1);
  std::string sep = ",";
  std::string dot = ".";
  if (!lua_isnoneornil(L, 2)) {
    sep = lua_tostring(L, 2);
  }
  if (!lua_isnoneornil(L, 3)) {
    dot = lua_tostring(L, 3);
  }
  std::string ret = Commify(num, sep, dot);
  LuaHelpers::Push(L, ret);
  return 1;
}
LUAFUNC_REGISTER_COMMON(commify);

int LuaFunc_JsonEncode(lua_State* L);
int LuaFunc_JsonEncode(lua_State* L) {
  int argc = lua_gettop(L);
  bool minified = false;

  if (argc < 1 || argc > 2) {
    luaL_error(L, "JsonEncode must be called with one or two arguments");
  }

  if (argc == 2) {
    minified = lua_toboolean(L, 2);
  }

  std::function<Json::Value(int)> convert =
      [&L, &convert](int index) -> Json::Value {
    switch (lua_type(L, index)) {
      case LUA_TNIL:
        return Json::Value(Json::nullValue);
      case LUA_TBOOLEAN:
        return Json::Value(static_cast<bool>(lua_toboolean(L, index)));
      case LUA_TNUMBER: {
        double val = lua_tonumber(L, index);

        if (val == static_cast<Json::UInt>(val)) {
          return Json::Value(static_cast<Json::UInt>(val));
        } else if (val == static_cast<Json::Int>(val)) {
          return Json::Value(static_cast<Json::Int>(val));
        }
        return Json::Value(val);
      }
      case LUA_TSTRING: {
        size_t len;
        const char* s = lua_tolstring(L, index, &len);

        return Json::Value(std::string(s, len));
      }
      case LUA_TTABLE: {
        // if the index is relative to the top of the stack,
        // then calculate the absolute index, so we have a
        // stable reference
        if (index < 0) {
          index = lua_gettop(L) + index + 1;
        }

        size_t len = lua_objlen(L, index);

        if (len > 0) {
          // array
          Json::Value array(Json::arrayValue);
          array.resize(len);

          for (unsigned int i = 0; i < len; i++) {
            lua_rawgeti(L, index, i + 1);
            array[i] = convert(-1);
            lua_pop(L, 1);
          }

          return array;
        } else {
          // object
          Json::Value obj(Json::objectValue);

          lua_pushnil(L);
          while (lua_next(L, index) != 0) {
            if (!lua_isstring(L, -2)) {
              luaL_error(L, "object keys must be strings");
            }

            size_t keylen;
            const char* key = lua_tolstring(L, -2, &keylen);
            obj[std::string(key, keylen)] = convert(-1);
            lua_pop(L, 1);
          }

          if (obj.size() < 1) {
            return Json::Value(Json::arrayValue);
          }
          return obj;
        }
      }
    }

    int tp = lua_type(L, index);
    luaL_error(L, "%s is not JSON serializable", lua_typename(L, tp));
    return Json::Value(Json::nullValue); /* not reached */
  };

  Json::Value root = convert(1);

  std::string data;
  if (!minified) {
    Json::StyledWriter writer;
    data = writer.write(root);
  } else {
    Json::FastWriter writer;
    data = writer.write(root);
  }

  lua_pushlstring(L, data.c_str(), data.length());
  return 1;
}
LUAFUNC_REGISTER_COMMON(JsonEncode);

int LuaFunc_JsonDecode(lua_State* L);
int LuaFunc_JsonDecode(lua_State* L) {
  int argc = lua_gettop(L);

  if (argc < 1) {
    luaL_error(L, "JsonDecode requires an argument");
  }

  size_t datalen;
  const char* data = lua_tolstring(L, 1, &datalen);

  Json::Reader reader;
  Json::Value root;

  bool ok = reader.parse(std::string(data, datalen), root, true);
  if (!ok) {
    std::string error = reader.getFormattedErrorMessages();
    luaL_error(L, "failed to parse JSON: %s", error.c_str());
  }

  std::function<void(const Json::Value&)> convert =
      [&L, &convert](const Json::Value& val) {
        if (val.isNull()) {
          lua_pushnil(L);
        } else if (val.isInt() || val.isUInt() || val.isDouble()) {
          lua_pushnumber(L, val.asDouble());
        } else if (val.isString()) {
          std::string s = val.asString();
          lua_pushlstring(L, s.c_str(), s.length());
        } else if (val.isBool()) {
          lua_pushboolean(L, val.asBool());
        } else if (val.isArray()) {
          lua_createtable(L, val.size(), 0);
          for (unsigned int i = 0; i < val.size(); i++) {
            convert(val[i]);
            lua_rawseti(L, -2, i + 1);
          }
        } else if (val.isObject()) {
          lua_createtable(L, 0, val.size());
          for (const std::string& member : val.getMemberNames()) {
            lua_pushlstring(L, member.c_str(), member.length());
            convert(val[member]);
            lua_rawset(L, -3);
          }
        } else {
          luaL_error(L, "failed to parse JSON: invalid type");
        }
      };

  convert(root);
  return 1;
}
LUAFUNC_REGISTER_COMMON(JsonDecode);

void luafunc_approach_internal(
    lua_State* L, int valind, int goalind, int speedind, const float mult,
    int process_index);
void luafunc_approach_internal(
    lua_State* L, int valind, int goalind, int speedind, const float mult,
    int process_index) {
#define TONUMBER_NICE(dest, num_name, index)                           \
  if (!lua_isnumber(L, index)) {                                       \
    luaL_error(                                                        \
        L, "approach: " #num_name " for approach %d is not a number.", \
        process_index);                                                \
  }                                                                    \
  dest = lua_tonumber(L, index);
  float val = 0;
  float goal = 0;
  float speed = 0;
  TONUMBER_NICE(val, current, valind);
  TONUMBER_NICE(goal, goal, goalind);
  TONUMBER_NICE(speed, speed, speedind);
#undef TONUMBER_NICE
  if (speed < 0) {
    luaL_error(L, "approach: speed %d is negative.", process_index);
  }
  fapproach(val, goal, speed * mult);
  lua_pushnumber(L, val);
}

int LuaFunc_approach(lua_State* L);
int LuaFunc_approach(lua_State* L) {
  // Args:  current, goal, speed
  // Returns:  new_current
  luafunc_approach_internal(L, 1, 2, 3, 1.0f, 1);
  return 1;
}
LUAFUNC_REGISTER_COMMON(approach);

int LuaFunc_multiapproach(lua_State* L);
int LuaFunc_multiapproach(lua_State* L) {
  // Args:  {currents}, {goals}, {speeds}, speed_multiplier
  // speed_multiplier is optional, and is intended to be the delta time for
  // the frame, so that this can be used every frame and have the current
  // approach the goal at a framerate independent speed.
  // Returns:  {currents}
  // Modifies the values in {currents} in place.
  if (lua_gettop(L) < 3) {
    luaL_error(
        L,
        "multiapproach:  A table of current values, a table of goal values, "
        "and a table of speeds must be passed.");
  }
  size_t currents_len = lua_objlen(L, 1);
  size_t goals_len = lua_objlen(L, 2);
  size_t speeds_len = lua_objlen(L, 3);
  float mult = 1.0f;
  if (lua_isnumber(L, 4)) {
    mult = lua_tonumber(L, 4);
  }
  if (currents_len != goals_len || currents_len != speeds_len) {
    luaL_error(
        L,
        "multiapproach:  There must be the same number of current values, goal "
        "values, and speeds.");
  }
  if (!lua_istable(L, 1) || !lua_istable(L, 2) || !lua_istable(L, 3)) {
    luaL_error(
        L, "multiapproach:  current, goal, and speed must all be tables.");
  }
  for (size_t i = 1; i <= currents_len; ++i) {
    lua_rawgeti(L, 1, i);
    lua_rawgeti(L, 2, i);
    lua_rawgeti(L, 3, i);
    luafunc_approach_internal(L, -3, -2, -1, mult, i);
    lua_rawseti(L, 1, i);
    lua_pop(L, 3);
  }
  lua_pushvalue(L, 1);
  return 1;
}
LUAFUNC_REGISTER_COMMON(multiapproach);

int LuaFunc_get_music_file_length(lua_State* L);
int LuaFunc_get_music_file_length(lua_State* L) {
  // Args:  file_path
  // Returns:  The length of the music in seconds.
  std::string path = SArg(1);
  std::string error;
  RageSoundReader* sample = RageSoundReader_FileReader::OpenFile(path, error);
  if (sample == nullptr) {
    luaL_error(L, "The music file '%s' does not exist.", path.c_str());
  }
  lua_pushnumber(L, sample->GetLength() / 1000.0f);
  return 1;
}
LUAFUNC_REGISTER_COMMON(get_music_file_length);

/* stubs for deprecated network functions */
LuaFunction(CloseConnection, false);
LuaFunction(ConnectToServer, false);
LuaFunction(GetServerName, std::string(""));
LuaFunction(IsNetConnected, false);
LuaFunction(IsNetSMOnline, false);
LuaFunction(IsSMOnlineLoggedIn, false);
LuaFunction(ReportStyle, false);

/*
 * Copyright (c) 2001-2005 Chris Danford, Glenn Maynard
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
