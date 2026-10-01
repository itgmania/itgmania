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
static bool CodePageConvert(std::string& sText, int iCodePage) {
  int iSize = MultiByteToWideChar(
      iCodePage, MB_ERR_INVALID_CHARS, sText.data(), sText.size(), nullptr, 0);
  if (iSize == 0) {
    LOG->Trace("%s\n", werr_ssprintf(GetLastError(), "err: ").c_str());
    return false; /* error */
  }

  std::wstring sOut;
  sOut.append(iSize, ' ');
  /* Nonportable: */
  iSize = MultiByteToWideChar(
      iCodePage, MB_ERR_INVALID_CHARS, sText.data(), sText.size(),
      (wchar_t*)sOut.data(), iSize);
  ASSERT(iSize != 0);

  sText = WStringToRString(sOut);
  return true;
}

static bool AttemptEnglishConversion(std::string& sText) {
  return CodePageConvert(sText, 1252);
}
static bool AttemptKoreanConversion(std::string& sText) {
  return CodePageConvert(sText, 949);
}
static bool AttemptJapaneseConversion(std::string& sText) {
  return CodePageConvert(sText, 932);
}

#elif defined(MACOSX)
#include <CoreFoundation/CoreFoundation.h>

#include <cstddef>

static bool ConvertFromCP(std::string& sText, int iCodePage) {
  CFStringEncoding encoding =
      CFStringConvertWindowsCodepageToEncoding(iCodePage);

  if (encoding == kCFStringEncodingInvalidId) {
    return false;
  }

  CFStringRef old =
      CFStringCreateWithCString(kCFAllocatorDefault, sText.c_str(), encoding);

  if (old == nullptr) {
    return false;
  }
  const size_t size = CFStringGetMaximumSizeForEncoding(
      CFStringGetLength(old), kCFStringEncodingUTF8);

  char* buf = new char[size + 1];
  buf[0] = '\0';
  bool result = CFStringGetCString(old, buf, size, kCFStringEncodingUTF8);
  sText = buf;
  delete[] buf;
  CFRelease(old);
  return result;
}

static bool AttemptEnglishConversion(std::string& sText) {
  return ConvertFromCP(sText, 1252);
}
static bool AttemptKoreanConversion(std::string& sText) {
  return ConvertFromCP(sText, 949);
}
static bool AttemptJapaneseConversion(std::string& sText) {
  return ConvertFromCP(sText, 932);
}

#else

/* No converters are available, so all fail--we only accept UTF-8. */
static bool AttemptEnglishConversion(std::string& sText) { return false; }
static bool AttemptKoreanConversion(std::string& sText) { return false; }
static bool AttemptJapaneseConversion(std::string& sText) { return false; }

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
