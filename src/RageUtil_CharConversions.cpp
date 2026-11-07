#include "RageUtil_CharConversions.h"

#include <string>
#include <vector>

#include "RageException.h"
#include "RageLog.h"
#include "RageUtil.h"

#if defined(_WIN32)

#include <windows.h>

#include "archutils/Win32/ErrorStrings.h"

/* Convert from the given codepage to UTF-8.  Return true if successful. */
static bool CodePageConvert(std::string& text, int code_page) {
  int size = MultiByteToWideChar(
      code_page, MB_ERR_INVALID_CHARS, text.data(), text.size(), nullptr, 0);
  if (size == 0) {
    LOG->Trace("%s\n", werr_ssprintf(GetLastError(), "err: ").c_str());
    return false; /* error */
  }

  std::wstring out;
  out.append(size, ' ');
  /* Nonportable: */
  size = MultiByteToWideChar(
      code_page, MB_ERR_INVALID_CHARS, text.data(), text.size(),
      (wchar_t*)out.data(), size);
  ASSERT(size != 0);

  text = WStringToRString(out);
  return true;
}

static bool AttemptEnglishConversion(std::string& text) {
  return CodePageConvert(text, 1252);
}
static bool AttemptKoreanConversion(std::string& text) {
  return CodePageConvert(text, 949);
}
static bool AttemptJapaneseConversion(std::string& text) {
  return CodePageConvert(text, 932);
}

#elif defined(MACOSX)
#include <CoreFoundation/CoreFoundation.h>

#include <cstddef>

static bool ConvertFromCP(std::string& text, int code_page) {
  CFStringEncoding encoding =
      CFStringConvertWindowsCodepageToEncoding(code_page);

  if (encoding == kCFStringEncodingInvalidId) {
    return false;
  }

  CFStringRef old =
      CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), encoding);

  if (old == nullptr) {
    return false;
  }
  const size_t size = CFStringGetMaximumSizeForEncoding(
      CFStringGetLength(old), kCFStringEncodingUTF8);

  char* buf = new char[size + 1];
  buf[0] = '\0';
  bool result = CFStringGetCString(old, buf, size, kCFStringEncodingUTF8);
  text = buf;
  delete[] buf;
  CFRelease(old);
  return result;
}

static bool AttemptEnglishConversion(std::string& text) {
  return ConvertFromCP(text, 1252);
}
static bool AttemptKoreanConversion(std::string& text) {
  return ConvertFromCP(text, 949);
}
static bool AttemptJapaneseConversion(std::string& text) {
  return ConvertFromCP(text, 932);
}

#else

/* No converters are available, so all fail--we only accept UTF-8. */
static bool AttemptEnglishConversion(std::string& text) { return false; }
static bool AttemptKoreanConversion(std::string& text) { return false; }
static bool AttemptJapaneseConversion(std::string& text) { return false; }

#endif

bool ConvertString(std::string& str, const std::string& encodings) {
  if (str.empty()) {
    return true;
  }

  std::vector<std::string> lst;
  split(encodings, ",", lst);

  for (unsigned i = 0; i < lst.size(); ++i) {
    if (lst[i] == "utf-8") {
      /* Is the string already valid utf-8? */
      if (utf8_is_valid(str)) {
        return true;
      }
      continue;
    }
    if (lst[i] == "english") {
      if (AttemptEnglishConversion(str)) {
        return true;
      }
      continue;
    }

    if (lst[i] == "japanese") {
      if (AttemptJapaneseConversion(str)) {
        return true;
      }
      continue;
    }

    if (lst[i] == "korean") {
      if (AttemptKoreanConversion(str)) {
        return true;
      }
      continue;
    }

    RageException::Throw(
        "Unexpected conversion string \"%s\" (string \"%s\").", lst[i].c_str(),
        str.c_str());
  }

  return false;
}

/* Written by Glenn Maynard.  In the public domain; there are so many
 * simple conversion interfaces that restricting them is silly. */
