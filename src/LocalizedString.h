#ifndef LOCALIZEDSTRING_H
#define LOCALIZEDSTRING_H

#include "global.h"

class ILocalizedStringImpl {
 public:
  virtual ~ILocalizedStringImpl() {}
  virtual void Load(const RString& group, const RString& name) = 0;
  virtual const RString& GetLocalized() const = 0;
};

// Get a String based on the user's natural language.
class LocalizedString {
 public:
  LocalizedString(const RString& group = "", const RString& name = "");
  LocalizedString(const LocalizedString& other);
  ~LocalizedString();
  void Load(const RString& group, const RString& name);
  operator const RString&() const { return GetValue(); }
  const RString& GetValue() const;

  typedef ILocalizedStringImpl* (*MakeLocalizer)();
  static void RegisterLocalizer(MakeLocalizer func);

 private:
  void CreateImpl();
  RString group_;
	RString name_;
  ILocalizedStringImpl* impl_;
  // Swallow up warnings. If they must be used, define them.
  LocalizedString& operator=(const LocalizedString& rhs);
};

#endif  // LOCALIZEDSTRING_H

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
