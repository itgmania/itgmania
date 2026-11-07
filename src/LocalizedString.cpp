#include "LocalizedString.h"

#include <string>

#include "RageUtil.h"
#include "SubscriptionManager.h"

static SubscriptionManager<LocalizedString> m_Subscribers;

class LocalizedStringImplDefault : public ILocalizedStringImpl {
 public:
  static ILocalizedStringImpl* Create() {
    return new LocalizedStringImplDefault;
  }

  void Load(const std::string& group, const std::string& name) {
    value_ = name;
  }

  const std::string& GetLocalized() const { return value_; }

 private:
  std::string value_;
};

static LocalizedString::MakeLocalizer g_pMakeLocalizedStringImpl =
    LocalizedStringImplDefault::Create;

void LocalizedString::RegisterLocalizer(MakeLocalizer func) {
  g_pMakeLocalizedStringImpl = func;
  for (LocalizedString* pLoc : *m_Subscribers.subscribers_) {
    pLoc->CreateImpl();
  }
}

LocalizedString::LocalizedString(
    const std::string& group, const std::string& name) {
  m_Subscribers.Subscribe(this);

  group_ = group;
  name_ = name;
  impl_ = nullptr;

  CreateImpl();
}

LocalizedString::LocalizedString(const LocalizedString& other) {
  m_Subscribers.Subscribe(this);

  group_ = other.group_;
  name_ = other.name_;
  impl_ = nullptr;

  CreateImpl();
}

LocalizedString::~LocalizedString() {
  m_Subscribers.Unsubscribe(this);

  RageUtil::SafeDelete(impl_);
}

void LocalizedString::CreateImpl() {
  RageUtil::SafeDelete(impl_);
  impl_ = g_pMakeLocalizedStringImpl();
  impl_->Load(group_, name_);
}

void LocalizedString::Load(const std::string& group, const std::string& name) {
  group_ = group;
  name_ = name;
  CreateImpl();
}

const std::string& LocalizedString::GetValue() const {
  return impl_->GetLocalized();
}

/*
 * Copyright (c) 2001-2005 Chris Danford
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
