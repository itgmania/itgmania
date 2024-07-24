#ifndef TECH_COUNTS_H
#define TECH_COUNTS_H

#include "GameConstantsAndTypes.h"
#include "StepParityGenerator.h"
class NoteData;


/** @brief Unknown radar values are given a default value. */
#define TECHCOUNTS_VAL_UNKNOWN -1

enum TechCountsCategory
{
	TechCountsCategory_Crossovers = 0,
	TechCountsCategory_Footswitches,
	TechCountsCategory_Sideswitches,
	TechCountsCategory_Jacks,
	TechCountsCategory_Brackets,
	TechCountsCategory_Doublesteps,
	NUM_TechCountsCategory,
	TechCountsCategory_Invalid
};

const RString& TechCountsCategoryToString( TechCountsCategory cat );
/**
 * @brief Turn the radar category into a proper localized string.
 * @param cat the radar category.
 * @return the localized string version of the radar category.
 */
const RString& TechCountsCategoryToLocalizedString( TechCountsCategory cat );
LuaDeclareType( TechCountsCategory );

struct lua_State;

/** @brief Technical statistics */
struct TechCounts
{
private:
	float m_Values[NUM_TechCountsCategory];
public:

	float operator[](TechCountsCategory cat) const { return m_Values[cat]; }
	float& operator[](TechCountsCategory cat) { return m_Values[cat]; }
	float operator[](int cat) const { return m_Values[cat]; }
	float& operator[](int cat) { return m_Values[cat]; }
	TechCounts();
	void MakeUnknown();
	void Zero();

	TechCounts& operator+=( const TechCounts& other )
	{
		FOREACH_ENUM( TechCountsCategory, tc )
		{
			(*this)[tc] += other[tc];
		}
		return *this;
	}

	bool operator==( const TechCounts& other ) const
	{
		FOREACH_ENUM( TechCountsCategory, tc )
		{
			if((*this)[tc] != other[tc])
			{
				return false;
			}
		}
		return true;
	}

	bool operator!=( const TechCounts& other ) const
	{
		return !operator==( other );
	}

	RString ToString( int iMaxValues = -1 ) const; // default = all
	void FromString( RString sValues );

	void PushSelf( lua_State *L );
	static void CalculateTechCountsFromRows(const std::vector<StepParity::Row> &rows, TechCounts &out);
};

#endif

/**
 * @file
 * @author Michael Votaw (c) 2023
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
