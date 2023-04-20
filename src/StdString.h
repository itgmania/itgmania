#ifndef STDSTRING_H
#define STDSTRING_H

// Standard headers needed
#include <string>			// basic_string
#include <algorithm>			// for_each, etc.

#if defined(WIN32)
#include <malloc.h>			// _alloca
#endif

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* In RageUtil: */
void MakeUpper( char *p, std::size_t iLen );
void MakeLower( char *p, std::size_t iLen );

/**
 * @brief Inline functions on which CStdString relies on.
 *
 * Usually for generic text mapping, we rely on preprocessor macro definitions
 * to map to string functions.  However the CStdString<> template cannot use
 * macro-based generic text mappings because its character types do not get
 * resolved until template processing which comes AFTER macro processing.  In
 * other words, UNICODE is of little help to us in the CStdString template.
 *
 * Therefore, to keep the CStdString declaration simple, we have these inline
 * functions.  The template calls them often.  Since they are inline (and NOT
 * exported when this is built as a DLL), they will probably be resolved away
 * to nothing.
 *
 * Without these functions, the CStdString<> template would probably have to broken
 * out into two, almost identical classes.  Either that or it would be a huge,
 * convoluted mess, with tons of "if" statements all over the place checking the
 * size of template parameter CT.
 *
 * In several cases, you will see two versions of each function.  One version is
 * the more portable, standard way of doing things, while the other is the
 * non-standard, but often significantly faster Visual C++ way.
 */
namespace StdString
{
/**
 * @brief Turn the character into its lowercase equivalent.
 * @param ch the character to convert.
 * @return the converted character.
 */
inline char sstolower(char ch)		{ return (ch >= 'A' && ch <= 'Z')? char(ch + 'a' - 'A'): ch; }

/**
 * @brief Assign one string to another.
 * @param sDst the destination string.
 * @param sSrc the source string.
 */
inline void	ssasn(std::string& sDst, const std::string& sSrc)
{
	if ( sDst.c_str() != sSrc.c_str() )
	{
		sDst.erase();
		sDst.assign(sSrc);
	}
}
/**
 * @brief Assign one string to another.
 * @param sDst the destination string.
 * @param pA the source string.
 */
inline void	ssasn(std::string& sDst, const char* pA)
{
		sDst.assign(pA);
}
#undef StrSizeType


// -----------------------------------------------------------------------------
// ssadd: string object concatenation -- add second argument to first
// -----------------------------------------------------------------------------
/**
 * @brief Concatenate one string with another.
 * @param sDst the original string.
 * @param sSrc the string being added.
 */
inline void	ssadd(std::string& sDst, const std::string& sSrc)
{
	sDst += sSrc;
}
/**
 * @brief Concatenate one string with another.
 * @param sDst the original string.
 * @param pA the string being added.
 */
inline void	ssadd(std::string& sDst, const char* pA)
{
	// If the string being added is our internal string or a part of our
	// internal string, then we must NOT do any reallocation without
	// first copying that string to another object (since we're using a
	// direct pointer)

	if ( pA >= sDst.c_str() && pA <= sDst.c_str()+sDst.length())
	{
		if ( sDst.capacity() <= sDst.size()+strlen(pA) )
			sDst.append(std::string(pA));
		else
			sDst.append(pA);
	}
	else
	{
		sDst.append(pA);
	}
}

// -----------------------------------------------------------------------------
// ssicmp: comparison (case insensitive )
// -----------------------------------------------------------------------------
/**
 * @brief Perform a case insensitive comparison of the two strings.
 * @param pA1 the first string.
 * @param pA2 the second string.
 * @return >0 if pA1 > pA2, <0 if pA1 < pA2, or 0 otherwise.
 */
inline int ssicmp(const char* pA1, const char* pA2)
{
	char f;
	char l;

	do
	{
		f = sstolower(*(pA1++));
		l = sstolower(*(pA2++));
	} while ( (f) && (f == l) );

	return (int)(f - l);
}

#if defined(WIN32)
#define vsnprintf _vsnprintf
#endif


/** @brief Our wrapper for std::string. */
class CStdString : public std::basic_string<char>
{
	// Typedefs for shorter names.  Using these names also appears to help
	// us avoid some ambiguities that otherwise arise on some platforms

	typedef typename std::basic_string<char>		MYBASE;	 // my base class
	typedef typename MYBASE::const_pointer		PCMYSTR;
	typedef typename MYBASE::pointer		PMYSTR;
	typedef typename MYBASE::iterator		MYITER;  // my iterator type
	typedef typename MYBASE::const_iterator		MYCITER; // you get the idea...
	typedef typename MYBASE::reverse_iterator	MYRITER;
	typedef typename MYBASE::size_type		MYSIZE;
	typedef typename MYBASE::value_type		MYVAL;
	typedef typename MYBASE::allocator_type		MYALLOC;
	typedef typename MYBASE::traits_type		MYTRAITS;

public:

	// CStdString inline constructors
	CStdString()
	{
	}

	CStdString(const CStdString& str) : MYBASE(str)
	{
	}

	CStdString(const std::string& str): MYBASE(str)
	{
	}

	CStdString(PCMYSTR pT, MYSIZE n) : MYBASE(pT, n)
	{
	}

	CStdString(const char* pA)
	{
		*this = pA;
	}

	CStdString(MYCITER first, MYCITER last)
		: MYBASE(first, last)
	{
	}

	CStdString(MYSIZE nSize, MYVAL ch, const MYALLOC& al=MYALLOC())
		: MYBASE(nSize, ch, al)
	{
	}

	// CStdString inline assignment operators
	CStdString& operator=(const CStdString& str)
	{
		ssasn(*this, str);
		return *this;
	}

	CStdString& operator=(const std::string& str)
	{
		ssasn(*this, str);
		return *this;
	}

	CStdString& operator=(const char* pA)
	{
		ssasn(*this, pA);
		return *this;
	}

	CStdString& operator=(char t)
	{
		this->assign(1, t);
		return *this;
	}

	// -------------------------------------------------------------------------
	// CStdString inline concatenation.
	// -------------------------------------------------------------------------
	CStdString& operator+=(const CStdString& str)
	{
		ssadd(*this, str);
		return *this;
	}

	CStdString& operator+=(const std::string& str)
	{
		ssadd(*this, str);
		return *this;
	}

	CStdString& operator+=(const char* pA)
	{
		ssadd(*this, pA);
		return *this;
	}

	CStdString& operator+=(char t)
	{
		this->append(1, t);
		return *this;
	}


	// addition operators -- global friend functions.
	friend	CStdString	operator+(const CStdString& str1, const CStdString& str2);
	friend	CStdString	operator+(const CStdString& str, char t);
	friend	CStdString	operator+(const CStdString& str, const char* sz);
	friend	CStdString	operator+(const char* pA, const CStdString& str);

	// -------------------------------------------------------------------------
	// Case changing functions
	// -------------------------------------------------------------------------
	CStdString& MakeUpper()
	{
		if ( !this->empty() )
			::MakeUpper(&MYBASE::operator[](0), this->size());

		return *this;
	}

	CStdString& MakeLower()
	{
		if ( !this->empty() )
			::MakeLower(&MYBASE::operator[](0), this->size());

		return *this;
	}


	// -------------------------------------------------------------------------
	// RString Facade Functions:
	//
	// The following methods are intended to allow you to use this class as a
	// drop-in replacement for CString.
	// -------------------------------------------------------------------------
	int CompareNoCase(PCMYSTR szThat)	const
	{
		return ssicmp(this->c_str(), szThat);
	}

	bool EqualsNoCase(PCMYSTR szThat)	const
	{
		return CompareNoCase(szThat) == 0;
	}

	int Replace(char chOld, char chNew)
	{
		int nReplaced	= 0;
		for ( MYITER iter=this->begin(); iter != this->end(); iter++ )
		{
			if ( *iter == chOld )
			{
				*iter = chNew;
				nReplaced++;
			}
		}
		return nReplaced;
	}

	int Replace(PCMYSTR szOld, PCMYSTR szNew)
	{
		int nReplaced		= 0;
		MYSIZE nIdx			= 0;
		MYSIZE nOldLen		= MYTRAITS::length(szOld);
		if ( 0 == nOldLen )
			return 0;

		static const char ch	= char(0);
		MYSIZE nNewLen		= MYTRAITS::length(szNew);
		PCMYSTR szRealNew	= szNew == 0 ? &ch : szNew;

		while ( (nIdx=this->find(szOld, nIdx)) != MYBASE::npos )
		{
			MYBASE::replace(this->begin()+nIdx, this->begin()+nIdx+nOldLen, szRealNew);
			nReplaced++;
			nIdx += nNewLen;
		}
		return nReplaced;
	}

	// Array-indexing operators.  Required because we defined an implicit cast
	// to operator const char* (Thanks to Julian Selman for pointing this out)
	char& operator[](int nIdx)
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const char& operator[](int nIdx) const
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	char& operator[](unsigned int nIdx)
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const char& operator[](unsigned int nIdx) const
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	char& operator[](long unsigned int nIdx){
	  return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const char& operator[](long unsigned int nIdx) const {
	  return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	char& operator[](long long unsigned int nIdx){
	  return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const char& operator[](long long unsigned int nIdx) const {
	  return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	operator const char*() const
	{
		return this->c_str();
	}
};


/**
 * @brief Another way to concatenate two strings together.
 * @param str1 the original string.
 * @param str2 the string to be added.
 * @return the longer string.
 */
inline
CStdString operator+(const  CStdString& str1, const  CStdString& str2)
{
	CStdString strRet(str1);
	strRet.append(str2);
	return strRet;
}
/**
 * @brief Another way to concatenate two strings together.
 * @param str the original string.
 * @param t the string to be added.
 * @return the longer string.
 */
inline
CStdString operator+(const  CStdString& str, char t)
{
	// this particular overload is needed for disabling reference counting
	// though it's only an issue from line 1 to line 2

	CStdString strRet(str);	// 1
	strRet.append(1, t);	// 2
	return strRet;
}
/**
 * @brief Another way to concatenate two strings together.
 * @param str the original string.
 * @param pA the string to be added.
 * @return the longer string.
 */
inline
CStdString operator+(const  CStdString& str, const char* pA)
{
	return CStdString(str) + CStdString(pA);
}
/**
 * @brief Another way to concatenate two strings together.
 * @param pA the original string.
 * @param str the string to be added.
 * @return the longer string.
 */
inline
CStdString operator+(const char* pA, const  CStdString& str)
{
	CStdString strRet(pA);
	strRet.append(str);
	return strRet;
}


#define StdStringLessNoCase		SSLNCA

struct StdStringLessNoCase
{
	inline
	bool operator()(const CStdString& sLeft, const CStdString& sRight) const
	{ return ssicmp(sLeft.c_str(), sRight.c_str()) < 0; }
};

}	// namespace StdString

#endif	// #ifndef STDSTRING_H
