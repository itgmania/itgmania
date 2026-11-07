/* AutoPtrCopyOnWrite - Simple smart pointer template. */

#ifndef RAGE_UTIL_AUTO_PTR_H
#define RAGE_UTIL_AUTO_PTR_H

#include <utility>

/*
 * This is a simple copy-on-write refcounted smart pointer.  Once constructed,
 * all read-only access to the object is made without extra copying.  If you
 * need read-write access, you can get a pointer with Get(), which will cause
 * the object to deep-copy.  (Don't free the resulting pointer.)
 *
 * Note that there are no non-const operator* or operator-> overloads, because
 * that would cause all const access by code with non-const permissions to
 * deep-copy.  For example,
 *
 *   AutoPtrCopyOnWrite<int> a( new int(1) );
 *   AutoPtrCopyOnWrite<int> b( a );
 *   printf( "%i\n", *a );
 *
 * If we have a non-const operator*, this *a will use it (even though it only
 * needs const access), and will copy the underlying object wastefully.  g++
 * std::string has this behavior, which is why it's important to qualify strings
 * as "const" when const access is desired, but that's brittle, so let's make
 * all potential deep-copying explicit.
 */

template <class T>
class AutoPtrCopyOnWrite {
 public:
  /* This constructor only exists to make us work with STL containers. */
  inline AutoPtrCopyOnWrite() : ptr_(nullptr), ref_count_(new int(1)) {}

  explicit inline AutoPtrCopyOnWrite(T* p) : ptr_(p), ref_count_(new int(1)) {}

  inline AutoPtrCopyOnWrite(const AutoPtrCopyOnWrite& rhs)
      : ptr_(rhs.ptr_), ref_count_(rhs.ref_count_) {
    ++(*ref_count_);
  }

  void Swap(AutoPtrCopyOnWrite<T>& rhs) {
    std::swap(ptr_, rhs.ptr_);
    std::swap(ref_count_, rhs.ref_count_);
  }

  inline AutoPtrCopyOnWrite<T>& operator=(const AutoPtrCopyOnWrite& rhs) {
    AutoPtrCopyOnWrite<T> obj(rhs);
    this->Swap(obj);
    return *this;
  }

  ~AutoPtrCopyOnWrite() {
    --(*ref_count_);
    if (*ref_count_ == 0) {
      delete ptr_;
      delete ref_count_;
    }
  }

  /* Get a non-const pointer.  This will deep-copy the object if necessary. */
  T* Get() {
    if (*ref_count_ > 1) {
      --*ref_count_;
      ptr_ = new T(*ptr_);
      ref_count_ = new int(1);
    }

    return ptr_;
  }

  int GetReferenceCount() const { return *ref_count_; }

  const T& operator*() const { return *ptr_; }
  const T* operator->() const { return ptr_; }

 private:
  T* ptr_;
  int* ref_count_;
};

template <class T>
inline void swap(AutoPtrCopyOnWrite<T>& a, AutoPtrCopyOnWrite<T>& b) {
  a.Swap(b);
}

/*
 * This smart pointer template is used to safely hide implementations from
 * headers, to reduce dependencies.  This is the same as declaring a pointer
 * to a class, and allocating/deallocating it in the implementation: only
 * the implementation needs to include that class.  This makes copying
 * and deletion automatic, so you don't need to include a copy ctor or
 * remember to delete it.
 *
 * There's one subtlety: in order to copy or delete an object, we need its
 * definition.  This is intended to avoid pulling in the definition.  So,
 * we use a traits class to hide it.  Use REGISTER_CLASS_TRAITS for each
 * class used with this template.
 *
 * Concepts from http://www.gotw.ca/gotw/062.htm.
 */
template <class T>
struct HiddenPtrTraits {
  static T* Copy(const T* copy);
  static void Delete(T* p);
};
#define REGISTER_CLASS_TRAITS(T, CopyExpr)      \
  template <>                                   \
  T* HiddenPtrTraits<T>::Copy(const T* pCopy) { \
    return CopyExpr;                            \
  }                                             \
  template <>                                   \
  void HiddenPtrTraits<T>::Delete(T* p) {       \
    delete p;                                   \
  }

template <class T>
class HiddenPtr {
 public:
  const T& operator*() const { return *ptr_; }
  const T* operator->() const { return ptr_; }
  T& operator*() { return *ptr_; }
  T* operator->() { return ptr_; }

  explicit HiddenPtr(T* p = nullptr) : ptr_(p) {}

  HiddenPtr(const HiddenPtr<T>& cpy) : ptr_(nullptr) {
    if (cpy.ptr_ != nullptr) {
      ptr_ = HiddenPtrTraits<T>::Copy(cpy.ptr_);
    }
  }

#if 0  // broken VC6
	template<class U>
	HiddenPtr( const HiddenPtr<U> &cpy )
	{
		if( cpy.ptr_ == nullptr )
			ptr_ = nullptr;
		else
			ptr_ = HiddenPtrTraits<U>::Copy( cpy.ptr_ );
	}
#endif

  ~HiddenPtr() { HiddenPtrTraits<T>::Delete(ptr_); }
  void Swap(HiddenPtr<T>& rhs) { std::swap(ptr_, rhs.ptr_); }

  HiddenPtr<T>& operator=(T* p) {
    HiddenPtr<T> t(p);
    Swap(t);
    return *this;
  }

  HiddenPtr<T>& operator=(const HiddenPtr& cpy) {
    HiddenPtr<T> t(cpy);
    Swap(t);
    return *this;
  }

#if 0  // broken VC6
	template<class U>
	HiddenPtr<T> &operator=( const HiddenPtr<U> &cpy )
	{
		HiddenPtr<T> t( cpy );
		Swap( t );
		return *this;
	}
#endif

  bool isNull() const { return ptr_ == nullptr; }

 private:
  T* ptr_;

#if 0  // broken VC6
	template<class U>
	friend class HiddenPtr;
#endif
};

template <class T>
inline void swap(HiddenPtr<T>& a, HiddenPtr<T>& b) {
  a.Swap(b);
}

#endif

/*
 * (c) 2005 Glenn Maynard
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
