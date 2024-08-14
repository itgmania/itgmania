/*

	StdString.h (for RString)
	
	I've optimized StdString.h (RString) for the ITGmania engine by reducing its
	unneeded dependencies, and moving many functions into StdString.cpp.
	
	The original comments are located at the end of the file.
	
	2024 sukibaby
	
*/

#if defined(_MSC_VER)
	#pragma component(browser, off, references, "CStdString")
	#pragma warning (push)
	#pragma warning (disable : 4290) // C++ Exception Specification ignored
	#pragma warning (disable : 4127) // Conditional expression is constant
	#pragma warning (disable : 4097) // typedef name used as synonym for class name
	#pragma warning (disable : 4512) // assignment operator could not be generated
#endif

#ifndef STDSTRING_H
#define STDSTRING_H

#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>

typedef const char* PCSTR;
typedef char* PSTR;
typedef const wchar_t* PCWSTR;
typedef wchar_t* PWSTR;

/* In RageUtil: */
void MakeUpper( char *p, std::size_t iLen );
void MakeLower( char *p, std::size_t iLen );
void MakeUpper( wchar_t *p, std::size_t iLen );
void MakeLower( wchar_t *p, std::size_t iLen );

namespace StdString
{
typedef std::string::size_type SS_SIZETYPE;
typedef std::string::pointer SS_PTRTYPE;

// Function declarations
void ssasn(std::string& sDst, const std::string& sSrc) noexcept;
void ssasn(std::wstring& sDst, const std::wstring& sSrc) noexcept;
void ssasn(std::string& sDst, PCSTR pA) noexcept;
void ssasn(std::wstring& sDst, PCWSTR pA) noexcept;
void ssadd(std::string& sDst, const std::string& sSrc) noexcept;
void ssadd(std::wstring& sDst, const std::wstring& sSrc) noexcept;
void ssadd(std::string& sDst, PCSTR pA) noexcept;
void ssadd(std::wstring& sDst, PCWSTR pA) noexcept;
char sstoupper(char ch) noexcept;
char sstolower(char ch) noexcept;
wchar_t sstoupper(wchar_t ch) noexcept;
wchar_t sstolower(wchar_t ch) noexcept;

template<typename CT>
inline int ssicmp(const CT* pA1, const CT* pA2) noexcept
{
	CT f;
	CT l;

	do
	{
		f = sstolower(*(pA1++));
		l = sstolower(*(pA2++));
	} while ((f) && (f == l));

	return static_cast<int>(f - l);
}

inline void sslwr(char *pT, std::size_t nLen) noexcept
{
	MakeLower(pT, nLen);
}

inline void ssupr(char *pT, std::size_t nLen) noexcept
{
	MakeUpper(pT, nLen);
}

inline void sslwr(wchar_t *pT, std::size_t nLen) noexcept
{
	MakeLower(pT, nLen);
}

inline void ssupr(wchar_t *pT, std::size_t nLen) noexcept
{
	MakeUpper(pT, nLen);
}

#if defined(WIN32)
#define vsnprintf _vsnprintf
#endif

template<typename CT>
class CStdStr;

template<typename CT>
inline
CStdStr<CT> operator+(const CStdStr<CT>& str1, const CStdStr<CT>& str2) noexcept
{
	CStdStr<CT> strRet(str1);
	strRet.append(str2);
	return strRet;
}

template<typename CT>
inline
CStdStr<CT> operator+(const CStdStr<CT>& str, CT t) noexcept
{
	CStdStr<CT> strRet(str);
	strRet.append(1, t);
	return strRet;
}

template<typename CT>
inline
CStdStr<CT> operator+(const CStdStr<CT>& str, const CT* pA) noexcept
{
	return CStdStr<CT>(str) + CStdStr<CT>(pA);
}

template<typename CT>
inline
CStdStr<CT> operator+(const CT* pA, const CStdStr<CT>& str) noexcept
{
	CStdStr<CT> strRet(pA);
	strRet.append(str);
	return strRet;
}

template<typename CT>
class CStdStr : public std::basic_string<CT>
{
	typedef typename std::basic_string<CT> MYBASE;
	typedef CStdStr<CT> MYTYPE;
	typedef typename MYBASE::const_pointer PCMYSTR;
	typedef typename MYBASE::pointer PMYSTR;
	typedef typename MYBASE::iterator MYITER;
	typedef typename MYBASE::const_iterator MYCITER;
	typedef typename MYBASE::reverse_iterator MYRITER;
	typedef typename MYBASE::size_type MYSIZE;
	typedef typename MYBASE::value_type MYVAL;
	typedef typename MYBASE::allocator_type MYALLOC;
	typedef typename MYBASE::traits_type MYTRAITS;

public:
	CStdStr() {}

	CStdStr(const MYTYPE& str) : MYBASE(str) {}

	CStdStr(const std::basic_string<CT>& str) : MYBASE(str) {}

	CStdStr(PCMYSTR pT, MYSIZE n) : MYBASE(pT, n) {}

	CStdStr(const CT* pA)
	{
		*this = pA;
	}

	CStdStr(MYCITER first, MYCITER last) : MYBASE(first, last) {}

	CStdStr(MYSIZE nSize, MYVAL ch, const MYALLOC& al = MYALLOC()) : MYBASE(nSize, ch, al) {}

	MYTYPE& operator=(const MYTYPE& str) noexcept
	{
		ssasn(*this, str);
		return *this;
	}

	MYTYPE& operator=(const std::basic_string<CT>& str) noexcept
	{
		ssasn(*this, str);
		return *this;
	}

	MYTYPE& operator=(const CT* pA) noexcept
	{
		ssasn(*this, pA);
		return *this;
	}

	MYTYPE& operator=(CT t) noexcept
	{
		this->assign(1, t);
		return *this;
	}

	MYTYPE& operator+=(const MYTYPE& str) noexcept
	{
		ssadd(*this, str);
		return *this;
	}

	MYTYPE& operator+=(const std::basic_string<CT>& str) noexcept
	{
		ssadd(*this, str);
		return *this;
	}

	MYTYPE& operator+=(const CT* pA) noexcept
	{
		ssadd(*this, pA);
		return *this;
	}

	MYTYPE& operator+=(CT t) noexcept
	{
		this->append(1, t);
		return *this;
	}

	friend MYTYPE operator+ <>(const MYTYPE& str1, const MYTYPE& str2) noexcept;
	friend MYTYPE operator+ <>(const MYTYPE& str, CT t) noexcept;
	friend MYTYPE operator+ <>(const MYTYPE& str, const CT* sz) noexcept;
	friend MYTYPE operator+ <>(const CT* pA, const MYTYPE& str) noexcept;

	MYTYPE& MakeUpper() noexcept
	{
		if (!this->empty())
			ssupr(GetBuffer(), this->size());

		return *this;
	}

	MYTYPE& MakeLower() noexcept
	{
		if (!this->empty())
			sslwr(GetBuffer(), this->size());

		return *this;
	}

	CT* GetBuffer(int nMinLen = -1) noexcept
	{
		if (static_cast<int>(this->size()) < nMinLen)
			this->resize(static_cast<MYSIZE>(nMinLen));

		return this->empty() ? const_cast<CT*>(this->data()) : &(this->at(0));
	}

	void ReleaseBuffer(int nNewLen = -1) noexcept
	{
		this->resize(static_cast<MYSIZE>(nNewLen > -1 ? nNewLen : MYTRAITS::length(this->c_str())));
	}

	int CompareNoCase(PCMYSTR szThat) const noexcept
	{
		return ssicmp(this->c_str(), szThat);
	}

	bool EqualsNoCase(PCMYSTR szThat) const noexcept
	{
		return CompareNoCase(szThat) == 0;
	}

	MYTYPE Left(int nCount) const noexcept
	{
		nCount = std::max(0, std::min(nCount, static_cast<int>(this->size())));
		return this->substr(0, static_cast<MYSIZE>(nCount));
	}

	int Replace(CT chOld, CT chNew) noexcept
	{
		int nReplaced = 0;
		for (MYITER iter = this->begin(); iter != this->end(); iter++)
		{
			if (*iter == chOld)
			{
				*iter = chNew;
				nReplaced++;
			}
		}
		return nReplaced;
	}

	int Replace(PCMYSTR szOld, PCMYSTR szNew) noexcept
	{
		int nReplaced = 0;
		MYSIZE nIdx = 0;
		MYSIZE nOldLen = MYTRAITS::length(szOld);
		if (0 == nOldLen)
			return 0;

		static const CT ch = CT(0);
		MYSIZE nNewLen = MYTRAITS::length(szNew);
		PCMYSTR szRealNew = szNew == 0 ? &ch : szNew;

		while ((nIdx = this->find(szOld, nIdx)) != MYBASE::npos)
		{
			MYBASE::replace(this->begin() + nIdx, this->begin() + nIdx + nOldLen, szRealNew);
			nReplaced++;
			nIdx += nNewLen;
		}
		return nReplaced;
	}

	MYTYPE Right(int nCount) const noexcept
	{
		nCount = std::max(0, std::min(nCount, static_cast<int>(this->size())));
		return this->substr(this->size() - static_cast<MYSIZE>(nCount));
	}

	CT& operator[](int nIdx) noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const CT& operator[](int nIdx) const noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	CT& operator[](unsigned int nIdx) noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const CT& operator[](unsigned int nIdx) const noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	CT& operator[](long unsigned int nIdx) noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const CT& operator[](long unsigned int nIdx) const noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	CT& operator[](long long unsigned int nIdx) noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

	const CT& operator[](long long unsigned int nIdx) const noexcept
	{
		return MYBASE::operator[](static_cast<MYSIZE>(nIdx));
	}

#ifndef SS_NO_IMPLICIT_CASTS
	operator const CT*() const noexcept
	{
		return this->c_str();
	}
#endif
};

typedef CStdStr<char> CStdStringA;
typedef CStdStr<wchar_t> CStdStringW;
#define CStdString CStdStringA

#define StdStringLessNoCase SSLNCA
#define StdStringEqualsNoCase SSENCA

struct StdStringLessNoCase
{
	inline
	bool operator()(const CStdStringA& sLeft, const CStdStringA& sRight) const noexcept
	{
		return ssicmp(sLeft.c_str(), sRight.c_str()) < 0;
	}

	inline
	bool operator()(const CStdStringW& sLeft, const CStdStringW& sRight) const noexcept
	{
		return ssicmp(sLeft.c_str(), sRight.c_str()) < 0;
	}
};

struct StdStringEqualsNoCase
{
	inline
	bool operator()(const CStdStringA& sLeft, const CStdStringA& sRight) const noexcept
	{
		return ssicmp(sLeft.c_str(), sRight.c_str()) == 0;
	}

	inline
	bool operator()(const CStdStringW& sLeft, const CStdStringW& sRight) const noexcept
	{
		return ssicmp(sLeft.c_str(), sRight.c_str()) == 0;
	}
};

}

#if defined(_MSC_VER)
	#pragma warning (pop)
#endif

#endif 

/* Original comments:

// =============================================================================
//  FILE:  StdString.h
//  AUTHOR:	Joe O'Leary (with outside help noted in comments)
//  REMARKS:
//		This header file declares the CStdStr template.  This template derives
//		the Standard C++ Library basic_string<> template and add to it the
//		the following conveniences:
//			- The full MFC RString set of functions (including implicit cast)
//			- writing to/reading from COM IStream interfaces
//			- Functional objects for use in STL algorithms
//
//		From this template, we intstantiate two classes:  CStdStringA and
//		CStdStringW.  The name "CStdString" is just a #define of one of these,
//		based upone the _UNICODE macro setting
//
//		This header also declares our own version of the MFC/ATL UNICODE-MBCS
//		conversion macros.  Our version looks exactly like the Microsoft's to
//		facilitate portability.
//
//	NOTE:
//		If you you use this in an MFC or ATL build, you should include either
//		afx.h or atlbase.h first, as appropriate.
//
//	PEOPLE WHO HAVE CONTRIBUTED TO THIS CLASS:
//
//		Several people have helped me iron out problems and othewise improve
//		this class.  OK, this is a long list but in my own defense, this code
//		has undergone two major rewrites.  Many of the improvements became
//		necessary after I rewrote the code as a template.  Others helped me
//		improve the RString facade.
//
//		Anyway, these people are (in chronological order):
//
//			- Pete the Plumber (???)
//			- Julian Selman
//			- Chris (of Melbsys)
//			- Dave Plummer
//			- John C Sipos
//			- Chris Sells
//			- Nigel Nunn
//			- Fan Xia
//			- Matthew Williams
//			- Carl Engman
//			- Mark Zeren
//			- Craig Watson
//			- Rich Zuris
//			- Karim Ratib
//			- Chris Conti
//			- Baptiste Lepilleur
//			- Greg Pickles
//			- Jim Cline
//			- Jeff Kohn
//			- Todd Heckel
//			- Ullrich Pollähne
//			- Joe Vitaterna
//			- Joe Woodbury
//			- Aaron (no last name)
//			- Joldakowski (???)
//			- Scott Hathaway
//			- Eric Nitzche
//			- Pablo Presedo
//			- Farrokh Nejadlotfi
//			- Jason Mills
//			- Igor Kholodov
//			- Mike Crusader
//			- John James
//			- Wang Haifeng
//			- Tim Dowty
//          - Arnt Witteveen
*
 * @file
 * @author Joseph M. O'Leary (c) 1999
 * @section LICENSE
 * COPYRIGHT:
 *	1999 Joseph M. O'Leary.  This code is free.  Use it anywhere you want.
 *	Rewrite it, restructure it, whatever.  Please don't blame me if it makes
 *	your $30 billion dollar satellite explode in orbit.  If you redistribute
 *	it in any form, I'd appreciate it if you would leave this notice here.
 *
 *	If you find any bugs, please let me know:
 *
 *			jmoleary@earthlink.net
 *			http://home.earthlink.net/~jmoleary
*/
