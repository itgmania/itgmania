#include "global.h"
#include "TechCounts.h"
#include "NoteData.h"
#include "RageLog.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "TimingData.h"
#include "GameState.h"
#include "RageTimer.h"


static const char *TechCountsCategoryNames[] = {
	"Crossovers",
	"Footswitches",
	"Sideswitches",
	"Jacks",
	"Brackets",
	"Doublesteps"
};

XToString( TechCountsCategory );
XToLocalizedString( TechCountsCategory );
LuaFunction(TechCountsCategoryToLocalizedString, TechCountsCategoryToLocalizedString(Enum::Check<TechCountsCategory>(L, 1)) );
LuaXType( TechCountsCategory );


// TechCounts methods

TechCounts::TechCounts()
{
	MakeUnknown();
}

void TechCounts::MakeUnknown()
{
	FOREACH_ENUM( TechCountsCategory, rc )
	{
		(*this)[rc] = TECHCOUNTS_VAL_UNKNOWN;
	}
}

void TechCounts::Zero()
{
	FOREACH_ENUM( TechCountsCategory, rc )
	{
		(*this)[rc] = 0;
	}
}

RString TechCounts::ToString( int iMaxValues ) const
{
	if( iMaxValues == -1 )
		iMaxValues = NUM_TechCountsCategory;
	iMaxValues = std::min( iMaxValues, (int)NUM_TechCountsCategory );

	std::vector<RString> asTechCounts;
	for( int r=0; r < iMaxValues; r++ )
	{
		asTechCounts.push_back(ssprintf("%.3f", (*this)[r]));
	}

	return join( ",",asTechCounts );
}

void TechCounts::FromString( RString sTechCounts )
{
	std::vector<RString> saValues;
	split( sTechCounts, ",", saValues, true );

	if( saValues.size() != NUM_TechCountsCategory )
	{
		MakeUnknown();
		return;
	}

	FOREACH_ENUM( RadarCategory, rc )
	{
		(*this)[rc] = StringToFloat(saValues[rc]);
	}

}

void TechCounts::CalculateTechCountsFromRows(const std::vector<StepParity::Row> &rows, TechCounts &out)
{
	// arrays to hold the column for each Foot enum.
	// A value of -1 means that Foot is not on any column
	int previousFootPlacement[StepParity::NUM_Foot];
	int currentFootPlacement[StepParity::NUM_Foot];

	for (int f = 0; f < StepParity::NUM_Foot; f++)
	{
		previousFootPlacement[f] = -1;
		currentFootPlacement[f] = -1;
	}

	// arrays to hold the foot placements for the current row and previos row.
	// They're basically just so we don't have to reference currentRow.notes[c].parity everywhere
	std::vector<StepParity::Foot> previousColumns(rows[0].columnCount, StepParity::NONE);
	std::vector<StepParity::Foot> currentColumns(rows[0].columnCount, StepParity::NONE);
	
	for (unsigned long i = 1; i < rows.size(); i++)
	{
		const StepParity::Row &currentRow = rows[i];
		int noteCount = 0;
		
		// copy the foot placement for the current row into currentColumns,
		// and count up how many notes there are in this row
		for (int c = 0; c < currentRow.columnCount; c++)
		{
			StepParity::Foot currFoot = currentRow.notes[c].parity;
			TapNoteType currType = currentRow.notes[c].type;

			// If this isn't either a tap or the beginning of a hold, skip it
			if(currType != TapNoteType_Tap && currType != TapNoteType_HoldHead)
			{
				continue;
			}

			currentFootPlacement[currFoot] = c;
			currentColumns[c] = currFoot;
			noteCount += 1;
		}

		/*
		Jacks are same arrow same foot
		Doublestep is same foot on successive arrows
		Brackets are jumps with one foot

		Footswitch is different foot on the up or down arrow
		Sideswitch is footswitch on left or right arrow
		Crossovers are left foot on right arrow or vice versa
		*/

		// check for jacks and doublesteps
		if(noteCount == 1)
		{
			for (StepParity::Foot foot: StepParity::FEET)
			{
				if(currentFootPlacement[foot] == -1 || previousFootPlacement[foot] == -1)
				{
					continue;
				}
				
				if(previousFootPlacement[foot] == currentFootPlacement[foot])
				{
					out[TechCountsCategory_Jacks] += 1;
				}
				else
				{
					out[TechCountsCategory_Doublesteps] += 1;
				}
			}
		}

		// check for brackets
		if(noteCount >= 2)
		{
			if(currentFootPlacement[StepParity::LEFT_HEEL] != -1 && currentFootPlacement[StepParity::LEFT_TOE] != -1)
			{
				out[TechCountsCategory_Brackets] += 1;
			}

			if(currentFootPlacement[StepParity::RIGHT_HEEL] != -1 && currentFootPlacement[StepParity::RIGHT_TOE] != -1)
			{
				out[TechCountsCategory_Brackets] += 1;
			}
		}

		// Check for footswitches, sideswitches, and crossovers
		for (int c = 0; c < currentRow.columnCount; c++)
		{
			if(currentColumns[c] == StepParity::NONE)
			{
				continue;
			}

			// this same column was stepped on in the previous row, but not by the same foot ==> footswitch or sideswitch
			if(previousColumns[c] != StepParity::NONE && previousColumns[c] != currentColumns[c])
			{
				// this is assuming only 4-panel single
				if(c == 0 || c == 3)
				{
					out[TechCountsCategory_Sideswitches] += 1;
				}
				else
				{
					out[TechCountsCategory_Footswitches] += 1;
				}
			}
			// if the right foot is pressing the left arrow, or the left foot is pressing the right ==> crossover
			else if(c == 0 && previousColumns[c] == StepParity::NONE &&
					(currentColumns[c] == StepParity::RIGHT_HEEL || currentColumns[c] == StepParity::RIGHT_TOE))
			{
				out[TechCountsCategory_Crossovers] += 1;
			}
			else if(c == 3 && previousColumns[c] == StepParity::NONE &&
					(currentColumns[c] == StepParity::LEFT_HEEL || currentColumns[c] == StepParity::LEFT_TOE))
			{
				out[TechCountsCategory_Crossovers] += 1;
			}
		}

		// Move the values from currentFootPlacement to previousFootPlacement,
		// and reset currentFootPlacement
		for (int f = 0; f < StepParity::NUM_Foot; f++)
		{
			previousFootPlacement[f] = currentFootPlacement[f];
			currentFootPlacement[f] = -1;
		}
		for (int c = 0; c < currentRow.columnCount; c++)
		{
			previousColumns[c] = currentColumns[c];
			currentColumns[c] = StepParity::NONE;
		}
	}
}

// lua start

class LunaTechCounts: public Luna<TechCounts>
{
public:


	static int GetValue( T* p, lua_State *L ) { lua_pushnumber( L, (*p)[Enum::Check<TechCountsCategory>(L, 1)] ); return 1; }

	LunaTechCounts()
	{
		ADD_METHOD( GetValue );
	}
};

LUA_REGISTER_CLASS( TechCounts )

/*
 * (c) 2023 Michael Votaw
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
