/* TODO (sukibaby):  remove this entirely; implement smart pointers directly */

#ifndef RAGE_UTIL_AUTO_PTR_H
#define RAGE_UTIL_AUTO_PTR_H

#include <memory> // shared_ptr, unique_ptr
#include <utility> // swap

/*
 * This is a simple copy-on-write refcounted smart pointer.  Once constructed, all read-only
 * access to the object is made without extra copying.  If you need read-write access, you
 * can get a pointer with Get(), which will cause the object to deep-copy.  (Don't free
 * the resulting pointer.)
 *
 * Note that there are no non-const operator* or operator-> overloads, because that would
 * cause all const access by code with non-const permissions to deep-copy.  For example,
 *
 *   AutoPtrCopyOnWrite<int> a( new int(1) );
 *   AutoPtrCopyOnWrite<int> b( a );
 *   printf( "%i\n", *a );
 *
 * If we have a non-const operator*, this *a will use it (even though it only needs const
 * access), and will copy the underlying object wastefully.  g++ std::string has this behavior,
 * which is why it's important to qualify strings as "const" when const access is desired,
 * but that's brittle, so let's make all potential deep-copying explicit.
 */

template<class T>
class AutoPtrCopyOnWrite
{
public:
	AutoPtrCopyOnWrite() : m_pPtr(nullptr) {}

	explicit AutoPtrCopyOnWrite(T* p) : m_pPtr(p, [](T* ptr) { delete ptr; }) {}

	AutoPtrCopyOnWrite(const AutoPtrCopyOnWrite& rhs) : m_pPtr(rhs.m_pPtr) {}

	AutoPtrCopyOnWrite& operator=(const AutoPtrCopyOnWrite& rhs)
	{
		if (this != &rhs)
		{
			m_pPtr = rhs.m_pPtr;
		}
		return *this;
	}

	T *Get()
	{
		if (!m_pPtr.unique())
		{
			m_pPtr = std::make_shared<T>(*m_pPtr);
		}
		return m_pPtr.get();
	}

	int GetReferenceCount() const { return m_pPtr.use_count(); }

	const T& operator*() const { return *m_pPtr; }
	const T* operator->() const { return m_pPtr.get(); }

private:
	std::shared_ptr<T> m_pPtr;
};

template<class T>
inline void swap( AutoPtrCopyOnWrite<T> &a, AutoPtrCopyOnWrite<T> &b )
{
	std::swap(a.m_pPtr, b.m_pPtr);
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
template<class T>
struct HiddenPtrTraits
{
	static T *Copy( const T *pCopy );
	static void Delete( T *p );
};

#define REGISTER_CLASS_TRAITS(T, CopyExpr) \
	template<> T *HiddenPtrTraits<T>::Copy( const T *pCopy ) { return CopyExpr; } \
	template<> void HiddenPtrTraits<T>::Delete( T *p ) { delete p; }

template<class T>
class HiddenPtr
{
public:
	const T& operator*() const { return *m_pPtr; }
	const T* operator->() const { return m_pPtr.get(); }
	T& operator*() { return *m_pPtr; }
	T* operator->() { return m_pPtr.get(); }

	explicit HiddenPtr(T* p = nullptr) : m_pPtr(p, HiddenPtrTraits<T>::Delete) {}

	HiddenPtr(const HiddenPtr<T>& cpy) : m_pPtr(nullptr, HiddenPtrTraits<T>::Delete)
	{
		if (cpy.m_pPtr)
		{
			m_pPtr.reset(HiddenPtrTraits<T>::Copy(cpy.m_pPtr.get()));
		}
	}

	HiddenPtr& operator=(T* p)
	{
		m_pPtr.reset(p);
		return *this;
	}

	HiddenPtr& operator=(const HiddenPtr& cpy)
	{
		if (this != &cpy)
		{
			if (cpy.m_pPtr)
			{
				m_pPtr.reset(HiddenPtrTraits<T>::Copy(cpy.m_pPtr.get()));
			}
			else
			{
				m_pPtr.reset();
			}
		}
		return *this;
	}

	bool isNull() const { return !m_pPtr; }
	const std::unique_ptr<T, void(*)(T*)>& getPtr() const { return m_pPtr; }

private:
	std::unique_ptr<T, void(*)(T*)> m_pPtr;

	// swap function needs to be a friend to access m_pPtr
	template<class U>
	friend void swap(HiddenPtr<U>& a, HiddenPtr<U>& b);
};

template<class T>
inline void swap( HiddenPtr<T> &a, HiddenPtr<T> &b )
{
	std::swap(a.m_pPtr, b.m_pPtr);
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
