/* SubscriptionManager - Object that accepts subscriptions. */

#ifndef SubscriptionManager_H
#define SubscriptionManager_H

#include <set>

// Since this class has only POD types and no constructor, there's no
// initialize order problem.
template <class T>
class SubscriptionManager {
 public:
  // TRICKY: If we make this a global instead of a global pointer,
  // then we'd have to be careful that the static constructors of all
  // subscribers are called before the collection constructor.  It's
  // impossible to enfore that in C++.  Instead, we'll allocate the
  // collection ourself on first use.  SubscriptionHandler itself is
  // a POD type, so a static SubscriptionHandler will always have
  // m_pSubscribers == nullptr (before any static constructors are called).
  std::set<T*>* subscribers_;

  // Use this to access m_pSubscribers, so you don't have to worry about
  // it being nullptr.
  std::set<T*>& Get() {
    if (subscribers_ == nullptr) {
      subscribers_ = new std::set<T*>;
    }
    return *subscribers_;
  }

  void Subscribe(T* p) {
    if (subscribers_ == nullptr) {
      subscribers_ = new std::set<T*>;
    }
#ifdef DEBUG
    typename std::set<T*>::iterator iter = subscribers_->find(p);
    ASSERT_M(iter == subscribers_->end(), "already subscribed");
#endif
    subscribers_->insert(p);
  }

  void Unsubscribe(T* p) {
    typename std::set<T*>::iterator iter = subscribers_->find(p);
    ASSERT(iter != subscribers_->end());
    subscribers_->erase(iter);
  }
};

#endif

/*
 * (c) 2004-2005 Chris Danford
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
