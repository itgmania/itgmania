/** @brief RageUtil - Miscellaneous helper macros and functions. */

#ifndef RAGE_UTIL_H
#define RAGE_UTIL_H

#include "global.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

class RageFileDriver;

class RageUtil {
public:
	// Safely delete pointers.
	template <typename T>
	inline static void SafeDelete(T*& p) noexcept
	{
		delete p;
		p = nullptr;
	}

	// Safely delete array pointers.
	template <typename T>
	inline static void SafeDeleteArray(T*& p) noexcept
	{
		delete[] p;
		p = nullptr;
	}
};

/** @brief Zero out the memory. */
#define ZERO(x)	memset(&(x), 0, sizeof(x))
/** @brief Copy from a to b. */
#define COPY(a,b) do { ASSERT(sizeof(a)==sizeof(b)); memcpy(&(a), &(b), sizeof(a)); } while( false )
/** @brief Get the length of the array. */
#define ARRAYLEN(a) (sizeof(a) / sizeof((a)[0]))

extern const std::string CUSTOM_SONG_PATH;

/**
 * @brief Scales x so that l1 corresponds to l2 and h1 corresponds to h2.
 *
 * This does not modify x, so it MUST assign the result to something!
 * Do the multiply before the divide to that integer scales have more precision.
 *
 * One such example: SCALE(x, 0, 1, L, H); interpolate between L and H.
 */
#define SCALE(x, l1, h1, l2, h2)	(((x) - (l1)) * ((h2) - (l2)) / ((h1) - (l1)) + (l2))

template<typename T, typename U>
inline U lerp( T x, U l, U h )
{
	return U(x * (h - l) + l);
}

template<typename T, typename U, typename V>
inline bool rage_clamp(T& x, U l, V h)
{
	if(x > static_cast<T>(h)) { x= static_cast<T>(h); return true; }
	else if(x < static_cast<T>(l)) { x= static_cast<T>(l); return true; }
	return false;
}

template<class T>
inline bool enum_clamp( T &x, T l, T h )
{
	if (x > h)	{ x = h; return true; }
	else if (x < l) { x = l; return true; }
	return false;
}

inline void wrap( int &x, int n )
{
	if (x<0)
		x += ((-x/n)+1)*n;
	x %= n;
}
inline void wrap( unsigned &x, unsigned n )
{
	x %= n;
}
inline void wrap( float &x, float n )
{
	if (x<0)
		x += std::trunc(((-x/n)+1))*n;
	x = std::fmod(x,n);
}

inline float fracf( float f ) { return f - std::trunc(f); }

template<class T>
void CircularShift( std::vector<T> &v, int dist )
{
	for( int i = std::abs(dist); i>0; i-- )
	{
		if( dist > 0 )
		{
			T t = v[0];
			v.erase( v.begin() );
			v.push_back( t );
		}
		else
		{
			T t = v.back();
			v.erase( v.end()-1 );
			v.insert( v.begin(), t );
		}
	}
}

template<typename Type, typename Ret>
static Ret *CreateClass() { return new Type; }

/*
 * Helper function to remove all objects from an STL container for which the
 * Predicate pred is true. If you want to remove all objects for which the predicate
 * returns false, wrap the predicate with not1().
 */
template<typename Container, typename Predicate>
inline void RemoveIf( Container& c, Predicate p )
{
	c.erase( remove_if(c.begin(), c.end(), p), c.end() );
}
template<typename Container, typename Value>
inline void RemoveIfEqual( Container &c, const Value &v )
{
	c.erase( remove(c.begin(), c.end(), v), c.end() );
}

/* Safely add an integer to an enum.
 *
 * This is illegal:
 *
 *  ((int&)val) += iAmt;
 *
 * It breaks aliasing rules; the compiler is allowed to assume that "val" doesn't
 * change (unless it's declared volatile), and in some cases, you'll end up getting
 * old values for "val" following the add.  (What's probably really happening is
 * that the memory location is being added to, but the value is stored in a register,
 * and breaking aliasing rules means the compiler doesn't know that the register
 * value is invalid.)
 */
template<typename T>
static inline void enum_add( T &val, int iAmt )
{
	val = static_cast<T>( val + iAmt );
}

template<typename T>
static inline T enum_add2( T val, int iAmt )
{
	return static_cast<T>( val + iAmt );
}

template<typename T>
static inline T enum_cycle( T val, int iMax, int iAmt = 1 )
{
	int iVal = val + iAmt;
	iVal %= iMax;
	return static_cast<T>( iVal );
}

/* return f rounded to the nearest multiple of fRoundInterval */
inline float Quantize( const float f, const float fRoundInterval )
{
	return int( (f + fRoundInterval/2)/fRoundInterval ) * fRoundInterval;
}

inline int Quantize( const int i, const int iRoundInterval )
{
	return int( (i + iRoundInterval/2)/iRoundInterval ) * iRoundInterval;
}

/* return f truncated to the nearest multiple of fTruncInterval */
inline float ftruncf( const float f, const float fTruncInterval )
{
	return int( (f)/fTruncInterval ) * fTruncInterval;
}

/* Return i rounded up to the nearest multiple of iInterval. */
inline int QuantizeUp( int i, int iInterval )
{
	return int( (i+iInterval-1)/iInterval ) * iInterval;
}

inline float QuantizeUp( float i, float iInterval )
{
	return std::ceil( i/iInterval ) * iInterval;
}

/* Return i rounded down to the nearest multiple of iInterval. */
inline int QuantizeDown( int i, int iInterval )
{
	return int( (i-iInterval+1)/iInterval ) * iInterval;
}

inline float QuantizeDown( float i, float iInterval )
{
	return std::floor( i/iInterval ) * iInterval;
}

// Move val toward other_val by to_move.
void fapproach( float& val, float other_val, float to_move );

/* Return a positive x mod y. */
float fmodfp( float x, float y );

int power_of_two( int v );
bool IsAnInt( const std::string &s );
bool IsHexVal( const std::string &s );
std::string BinaryToHex( const void *pData_, size_t iNumBytes );
std::string BinaryToHex( const std::string &sString );
bool HexToBinary( const std::string &s, unsigned char *stringOut );
bool HexToBinary( const std::string &s, std::string *sOut );
float HHMMSSToSeconds( const std::string &sHMS );
std::string SecondsToHHMMSS( float fSecs );
std::string SecondsToMSSMsMs( float fSecs );
std::string SecondsToMMSSMsMs( float fSecs );
std::string SecondsToMMSSMsMsMs( float fSecs );
std::string MicrosecondsToMMSSMsMs(uint64_t usecs);
std::string MicrosecondsToMMSSMsMsMs(uint64_t usecs);
std::string SecondsToMSS( float fSecs );
std::string SecondsToMMSS( float fSecs );
std::string PrettyPercent( float fNumerator, float fDenominator );
inline std::string PrettyPercent( int fNumerator, int fDenominator ) { return PrettyPercent( float(fNumerator), float(fDenominator) ); }
std::string Commify( int iNum );
std::string Commify(const std::string& num, const std::string& sep= ",", const std::string& dot= ".");
std::string FormatNumberAndSuffix( int i );
/* Round num to 3 decimal places and return as string with 3 decimal places */
std::string NormalizeDecimal(float num);

struct tm GetLocalTime();

std::string ssprintf( const char *fmt, ...) PRINTF(1,2);
std::string vssprintf( const char *fmt, va_list argList );

/*
 * Splits a Path into 4 parts (Directory, Drive, Filename, Extention).  Supports UNC path names.
 * If Path is a directory (eg. c:\games\stepmania"), append a slash so the last
 * element will end up in Dir, not FName: "c:\games\stepmania\".
 * */
void splitpath( const std::string &Path, std::string &Dir, std::string &Filename, std::string &Ext );
std::string custom_songify_path(std::string const& path);

std::string SetExtension( const std::string &path, const std::string &ext );
std::string GetExtension( const std::string &sPath );
std::string GetFileNameWithoutExtension( const std::string &sPath );
void MakeValidFilename( std::string &sName );

bool FindFirstFilenameContaining(
	const std::vector<std::string>& filenames, std::string& out,
	const std::vector<std::string>& starts_with,
	const std::vector<std::string>& contains, const std::vector<std::string>& ends_with);

extern const wchar_t INVALID_CHAR;

int utf8_get_char_len( char p );
bool utf8_to_wchar( const char *s, size_t iLength, unsigned &start, wchar_t &ch );
bool utf8_to_wchar_ec( const std::string &s, unsigned &start, wchar_t &ch );
void wchar_to_utf8( wchar_t ch, std::string &out );
wchar_t utf8_get_char( const std::string &s );
bool utf8_is_valid( const std::string &s );
void utf8_remove_bom( std::string &s );
void MakeUpper( char *p, size_t iLen );
void MakeLower( char *p, size_t iLen );
void MakeUpper( wchar_t *p, size_t iLen );
void MakeLower( wchar_t *p, size_t iLen );

// TODO: Have the three functions below be moved to better locations.
float StringToFloat( const std::string &sString );
bool StringToFloat( const std::string &sString, float &fOut );
// Better than IntToString because you can check for success.
template<class T>
inline bool operator>>(const std::string& lhs, T& rhs)
{
	return !!(std::istringstream(lhs) >> rhs);
}

// Exception-safe wrappers around stoi and friends
// Additional argument exceptVal will be returned if the conversion couldn't be performed
int StringToInt( const std::string& str, size_t* pos = 0, int base = 10, int exceptVal = 0 );
long StringToLong( const std::string& str, size_t* pos = 0, int base = 10, long exceptVal = 0 );
long long StringToLLong( const std::string& str, size_t* pos = 0, int base = 10, long long exceptVal = 0 );

std::string WStringToRString( const std::wstring &sString );
std::string WcharToUTF8( wchar_t c );
std::wstring RStringToWstring( const std::string &sString );

// Splits a std::string into an std::vector<std::string> according the Delimitor.
void split( const std::string &sSource, const std::string &sDelimitor, std::vector<std::string>& asAddIt, const bool bIgnoreEmpty = true );
void split( const std::wstring &sSource, const std::wstring &sDelimitor, std::vector<std::wstring> &asAddIt, const bool bIgnoreEmpty = true );
std::vector<std::string> split( const std::string &sSource, const char delimiter, const bool bIgnoreEmpty = true );

/* In-place split. */
void split( const std::string &sSource, const std::string &sDelimitor, int &iBegin, int &iSize, const bool bIgnoreEmpty = true );
void split( const std::wstring &sSource, const std::wstring &sDelimitor, int &iBegin, int &iSize, const bool bIgnoreEmpty = true );

/* In-place split of partial string. */
void split( const std::string &sSource, const std::string &sDelimitor, int &iBegin, int &iSize, int iLen, const bool bIgnoreEmpty ); /* no default to avoid ambiguity */
void split( const std::wstring &sSource, const std::wstring &sDelimitor, int &iBegin, int &iSize, int iLen, const bool bIgnoreEmpty );

// Joins a std::vector<std::string> to create a std::string according the Deliminator.
std::string join( const std::string &sDelimitor, const std::vector<std::string>& sSource );
std::string join( const std::string &sDelimitor, std::vector<std::string>::const_iterator begin, std::vector<std::string>::const_iterator end );

// Joins a vector of numbers to a serialized string of numbers separated by Delimitor.
std::string serialize(const std::vector<float> & sSource, const std::string &sDelimitor, int precision);
std::string serialize(const std::vector<int> & sSource, const std::string &sDelimitor);

// These methods escapes a string for saving in a .sm or .crs file
std::string SmEscape(const std::string &sUnescaped, const std::vector<char> charsToEscape = {'\\', ':', ';'});
std::string SmEscape( const char *cUnescaped, int len, const std::vector<char> charsToEscape = {'\\', ':', ';'} );
// Escapes each element in a std::vector<std::string>, returns a new vector
std::vector<std::string> SmEscape(const std::vector<std::string> &vUnescaped, const std::vector<char> charsToEscape = {'\\', ':', ';'});

std::string SmUnescape( const std::string &sEscaped );

// These methods "escape" a string for .dwi by turning = into -, ] into I, etc.  That is "lossy".
std::string DwiEscape( const std::string &sUnescaped );
std::string DwiEscape( const char *cUnescaped, int len );

std::string GetCwd();

void SetCommandlineArguments( int argc, char **argv );
void GetCommandLineArguments( int &argc, char **&argv );
bool GetCommandlineArgument( const std::string &option, std::string *argument=nullptr, int iIndex=0 );
extern int g_argc;
extern char **g_argv;

void CRC32( unsigned int &iCRC, const void *pBuffer, size_t iSize );
unsigned int GetHashForString( const std::string &s );
unsigned int GetHashForFile( const std::string &sPath );
unsigned int GetHashForDirectory( const std::string &sDir );	// a hash value that remains the same as long as nothing in the directory has changed
bool DirectoryIsEmpty( const std::string &sPath );

bool CompareRStringsAsc( const std::string &sStr1, const std::string &sStr2 );
bool CompareRStringsDesc( const std::string &sStr1, const std::string &sStr2 );
void SortRStringArray( std::vector<std::string> &asAddTo, const bool bSortAscending = true );

/* Find the mean and standard deviation of all numbers in [start,end). */
float calc_mean( const float *pStart, const float *pEnd );
/* When bSample is true, it calculates the square root of an unbiased estimator for the population
 * variance. Note that this is not an unbiased estimator for the population standard deviation but
 * it is close and an unbiased estimator is complicated (apparently).
 * When the entire population is known, bSample should be false to calculate the exact standard
 * deviation. */
float calc_stddev( const float *pStart, const float *pEnd, bool bSample = false );

/*
 * Find the slope, intercept, and error of a linear least squares regression
 * of the points given.  Error is returned as the sqrt of the average squared
 * Y distance from the chosen line.
 * Returns true on success, false on failure.
 */
bool CalcLeastSquares( const std::vector<std::pair<float, float> > &vCoordinates,
                       float &fSlope, float &fIntercept, float &fError );

/*
 * This method throws away any points that are more than fCutoff away from
 * the line defined by fSlope and fIntercept.
 */
void FilterHighErrorPoints( std::vector<std::pair<float, float> > &vCoordinates,
                            float fSlope, float fIntercept, float fCutoff );

template<class T1, class T2>
int FindIndex( T1 begin, T1 end, const T2 *p )
{
	T1 iter = find( begin, end, p );
	if( iter == end )
		return -1;
	return iter - begin;
}

/* Useful for objects with no operator-, eg. map::iterator (more convenient than advance). */
template<class T>
inline T Increment( T a ) { ++a; return a; }
template<class T>
inline T Decrement( T a ) { --a; return a; }

void TrimLeft( std::string &sStr, const char *szTrim = "\r\n\t " );
void TrimRight( std::string &sStr, const char *szTrim = "\r\n\t " );
void Trim( std::string &sStr, const char *szTrim = "\r\n\t " );
void StripCrnl( std::string &sStr );
bool BeginsWith( const std::string &sTestThis, const std::string &sBeginning );
bool EndsWith( const std::string &sTestThis, const std::string &sEnding );
std::string URLEncode( const std::string &sStr );

void StripCvsAndSvn( std::vector<std::string> &vs ); // Removes various versioning system metafolders.
void StripMacResourceForks( std::vector<std::string> &vs ); // Removes files starting with "._"

std::string DerefRedir( const std::string &sPath );
bool GetFileContents( const std::string &sPath, std::string &sOut, bool bOneLine = false );
bool GetFileContents( const std::string &sFile, std::vector<std::string> &asOut );

void ReplaceEntityText( std::string &sText, const std::map<std::string,std::string> &m );
void ReplaceEntityText( std::string &sText, const std::map<char,std::string> &m );
void Replace_Unicode_Markers( std::string &Text );
std::string WcharDisplayText( wchar_t c );

std::string Basename( const std::string &dir );
std::string Dirname( const std::string &dir );
std::string Capitalize( const std::string &s );

#if defined(HAVE_UNISTD_H)
#include <unistd.h> /* correct place with correct definitions */
#endif

extern unsigned char g_UpperCase[256];
extern unsigned char g_LowerCase[256];

/* ASCII-only case insensitivity. */
struct char_traits_char_nocase: public std::char_traits<char>
{
	static inline bool eq( char c1, char c2 )
	{ return g_UpperCase[(unsigned char)c1] == g_UpperCase[(unsigned char)c2]; }

	static inline bool ne( char c1, char c2 )
	{ return g_UpperCase[(unsigned char)c1] != g_UpperCase[(unsigned char)c2]; }

	static inline bool lt( char c1, char c2 )
	{ return g_UpperCase[(unsigned char)c1] < g_UpperCase[(unsigned char)c2]; }

	static int compare( const char* s1, const char* s2, size_t n )
	{
		int ret = 0;
		while( n-- )
		{
			ret = fasttoupper(*s1++) - fasttoupper(*s2++);
			if( ret != 0 )
				break;
		}
		return ret;
	}

	static inline char fasttoupper(char a)
	{
		return g_UpperCase[(unsigned char)a];
	}

	static const char *find( const char* s, int n, char a )
	{
		a = fasttoupper(a);
		while( n-- > 0 && fasttoupper(*s) != a )
			++s;

		if(fasttoupper(*s) == a)
			return s;
		return nullptr;
	}
};
typedef std::basic_string<char,char_traits_char_nocase> istring;

/* Compatibility/convenience shortcuts. These are actually defined in RageFileManager.h, but
 * declared here since they're used in many places. */
void GetDirListing( const std::string &sPath, std::vector<std::string> &AddTo, bool bOnlyDirs=false, bool bReturnPathToo=false );
void GetDirListingRecursive( const std::string &sDir, const std::string &sMatch, std::vector<std::string> &AddTo );	/* returns path too */
void GetDirListingRecursive( RageFileDriver *prfd, const std::string &sDir, const std::string &sMatch, std::vector<std::string> &AddTo );	/* returns path too */
bool DeleteRecursive( const std::string &sDir );	/* delete the dir and all files/subdirs inside it */
bool DeleteRecursive( RageFileDriver *prfd, const std::string &sDir );	/* delete the dir and all files/subdirs inside it */
bool DoesFileExist( const std::string &sPath );
bool IsAFile( const std::string &sPath );
bool IsADirectory( const std::string &sPath );
int GetFileSizeInBytes( const std::string &sFilePath );

// call FixSlashesInPlace on any path that came from the user
void FixSlashesInPlace( std::string &sPath );
void CollapsePath( std::string &sPath, bool bRemoveLeadingDot=false );

/** @brief Utilities for converting the RStrings. */
namespace StringConversion
{
	template<typename T>
	bool FromString( const std::string &sValue, T &out );

	template<typename T>
	std::string ToString( const T &value );

	template<> inline bool FromString<std::string>( const std::string &sValue, std::string &out ) { out = sValue; return true; }
	template<> inline std::string ToString<std::string>( const std::string &value ) { return value; }
}

class RageFileBasic;
bool FileCopy( const std::string &sSrcFile, const std::string &sDstFile );
bool FileCopy( RageFileBasic &in, RageFileBasic &out, std::string &sError, bool *bReadError = nullptr );

template<class T>
void GetAsNotInBs( const std::vector<T> &as, const std::vector<T> &bs, std::vector<T> &difference )
{
	std::vector<T> bsUnmatched = bs;
	// Cannot use FOREACH_CONST here because std::vector<T>::const_iterator is an implicit type.
	for( typename std::vector<T>::const_iterator a = as.begin(); a != as.end(); ++a )
	{
		typename std::vector<T>::iterator iter = find( bsUnmatched.begin(), bsUnmatched.end(), *a );
		if( iter != bsUnmatched.end() )
			bsUnmatched.erase( iter );
		else
			difference.push_back( *a );
	}
}

template<class T>
void GetConnectsDisconnects( const std::vector<T> &before, const std::vector<T> &after, std::vector<T> &disconnects, std::vector<T> &connects )
{
	GetAsNotInBs( before, after, disconnects );
	GetAsNotInBs( after, before, connects );
}


#endif

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
