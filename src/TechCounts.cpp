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
	"HalfCrossovers",
	"FullCrossovers",
	"Footswitches",
	"UpFootswitches",
	"DownFootswitches",
	"Sideswitches",
	"Jacks",
	"Brackets",
	"Doublesteps"
};

XToString( TechCountsCategory );
XToLocalizedString( TechCountsCategory );
LuaFunction(TechCountsCategoryToLocalizedString, TechCountsCategoryToLocalizedString(Enum::Check<TechCountsCategory>(L, 1)) );
LuaXType( TechCountsCategory );

// 0.176 ~= 1/8th at 175bpm
// Anything slower isn't counted as a jack
const float JACK_CUTOFF = 0.176;
// 0.3 = 1/4th at 200bpm (or 3/16th at 150bpm)
// Anything slower isn't counted as a footswitch
const float FOOTSWITCH_CUTOFF = 0.3;
// 0.235 ~= 1/8th at 128bpm
// Anything slower isn't counted as a doublestep
const float DOUBLESTEP_CUTOFF = 0.235;

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

void TechCounts::CalculateTechCountsFromRows(const std::vector<StepParity::Row> &rows, StepParity::StageLayout & layout, TechCounts &out)
{
	for (unsigned long i = 1; i < rows.size(); i++)
	{
		const StepParity::Row &currentRow = rows[i];
		const StepParity::Row &previousRow = rows[i - 1];
			
		float elapsedTime = currentRow.second - previousRow.second;
		
		// Jacks are same arrow same foot
		// Doublestep is same foot on successive arrows
		// Brackets are jumps with one foot

		// Footswitch is different foot on the up or down arrow
		// Sideswitch is footswitch on left or right arrow
		// Crossovers are left foot on right arrow or vice versa

		// Check for jacks and doublesteps
		if(currentRow.noteCount == 1 && previousRow.noteCount == 1)
		{
			for (StepParity::Foot foot: StepParity::FEET)
			{
				if(currentRow.whereTheFeetAre[foot] == StepParity::INVALID_COLUMN || previousRow.whereTheFeetAre[foot] == StepParity::INVALID_COLUMN)
				{
					continue;
				}
				
				if(previousRow.whereTheFeetAre[foot] == currentRow.whereTheFeetAre[foot])
				{
					if(elapsedTime < JACK_CUTOFF)
					{
						out[TechCountsCategory_Jacks] += 1;
					}
				}
				else
				{
					if(elapsedTime < DOUBLESTEP_CUTOFF)
					{
						out[TechCountsCategory_Doublesteps] += 1;
					}
				}
			}
		}

		// Check for brackets
		if(currentRow.noteCount >= 2)
		{
			if(currentRow.whereTheFeetAre[StepParity::LEFT_HEEL] != StepParity::INVALID_COLUMN && currentRow.whereTheFeetAre[StepParity::LEFT_TOE] != StepParity::INVALID_COLUMN)
			{
				out[TechCountsCategory_Brackets] += 1;
			}

			if(currentRow.whereTheFeetAre[StepParity::RIGHT_HEEL] != StepParity::INVALID_COLUMN && currentRow.whereTheFeetAre[StepParity::RIGHT_TOE] != StepParity::INVALID_COLUMN)
			{
				out[TechCountsCategory_Brackets] += 1;
			}
		}

		// Check for up footswitches
		for(int c : layout.upArrows)
		{
			if(isFootswitch(c, currentRow, previousRow, elapsedTime))
			{
				out[TechCountsCategory_UpFootswitches] += 1;
				out[TechCountsCategory_Footswitches] += 1;
			}
		}
		// Check for down footswitches
		for(int c: layout.downArrows)
		{
			if(isFootswitch(c, currentRow, previousRow, elapsedTime))
			{
				out[TechCountsCategory_DownFootswitches] += 1;
				out[TechCountsCategory_Footswitches] += 1;
			}
		}
		
		// Check for sideswitches
		for(int c: layout.sideArrows)
		{
			if(isFootswitch(c, currentRow, previousRow, elapsedTime))
			{
				out[TechCountsCategory_Sideswitches] += 1;
			}
		}
		
		// Check for crossovers
		int leftHeel = currentRow.whereTheFeetAre[StepParity::LEFT_HEEL];
		int leftToe = currentRow.whereTheFeetAre[StepParity::LEFT_TOE];
		int rightHeel = currentRow.whereTheFeetAre[StepParity::RIGHT_HEEL];
		int rightToe = currentRow.whereTheFeetAre[StepParity::RIGHT_TOE];
		
		int previousLeftHeel = previousRow.whereTheFeetAre[StepParity::LEFT_HEEL];
		int previousLeftToe = previousRow.whereTheFeetAre[StepParity::LEFT_TOE];
		int previousRightHeel = previousRow.whereTheFeetAre[StepParity::RIGHT_HEEL];
		int previousRightToe = previousRow.whereTheFeetAre[StepParity::RIGHT_TOE];
		
		// Check for the following:
		// - We moved the right foot on this row,
		// - we moved the left foot on the previous row,
		// - we didn't move the right foot on the previous row
		// - Is the position of the right foot farther left than the left foot
		// - If so, check the row before the previous row for the following:
		//   - Was the right foot on a difference position
		//   - Was the right foot farther right than the left?
		//     - If so, then this was a full crossover (like RDL, starting on right foot)
		//     - otherwise, then this was probably a half crossover (like UDL, starting on right foot)
		if(rightHeel != StepParity::INVALID_COLUMN && previousLeftHeel != StepParity::INVALID_COLUMN && previousRightHeel == StepParity::INVALID_COLUMN)
		{
			StepParity::StagePoint leftPos = layout.averagePoint(previousLeftHeel, previousLeftToe);
			StepParity::StagePoint rightPos = layout.averagePoint(rightHeel, rightToe);
			
			if(rightPos.x < leftPos.x)
			{
				if(i > 1)
				{
					const StepParity::Row & previousPreviousRow = rows[i - 2];
					int previousPreviousRightHeel = previousPreviousRow.whereTheFeetAre[StepParity::RIGHT_HEEL];
					
					if(previousPreviousRightHeel != StepParity::INVALID_COLUMN && previousPreviousRightHeel != rightHeel)
					{
						StepParity::StagePoint previousPreviousRightPos = layout.columns[previousPreviousRightHeel];
						if(previousPreviousRightPos.x > leftPos.x)
						{
							out[TechCountsCategory_FullCrossovers] += 1;
						}
						else
						{
							out[TechCountsCategory_HalfCrossovers] += 1;
						}
						out[TechCountsCategory_Crossovers] += 1;
					}
				}
				else
				{
					out[TechCountsCategory_HalfCrossovers] += 1;
					out[TechCountsCategory_Crossovers] += 1;
				}
			}
		}
		// And check the same thing, starting with left foot
		else if(leftHeel != StepParity::INVALID_COLUMN && previousRightHeel != StepParity::INVALID_COLUMN && previousLeftHeel == StepParity::INVALID_COLUMN)
		{
			StepParity::StagePoint leftPos = layout.averagePoint(leftHeel, leftToe);
			StepParity::StagePoint rightPos = layout.averagePoint(previousRightHeel, previousRightToe);
			
			if(rightPos.x < leftPos.x)
			{
				if(i > 1)
				{
					const StepParity::Row & previousPreviousRow = rows[i - 2];
					int previousPreviousLeftHeel = previousPreviousRow.whereTheFeetAre[StepParity::LEFT_HEEL];
					if(previousPreviousLeftHeel != StepParity::INVALID_COLUMN && previousPreviousLeftHeel != leftHeel)
					{
						StepParity::StagePoint previousPreviousLeftPos = layout.columns[previousPreviousLeftHeel];
						if(rightPos.x > previousPreviousLeftPos.x)
						{
							out[TechCountsCategory_FullCrossovers] += 1;
						}
						else
						{
							out[TechCountsCategory_HalfCrossovers] += 1;
						}
						out[TechCountsCategory_Crossovers] += 1;
					}
				}
				else
				{
					out[TechCountsCategory_HalfCrossovers] += 1;
					out[TechCountsCategory_Crossovers] += 1;
				}
			}
		}
	}
}

bool TechCounts::isFootswitch(int c, const StepParity::Row & currentRow, const StepParity::Row & previousRow, float elapsedTime)
{
	if(currentRow.columns[c] == StepParity::NONE || previousRow.columns[c] == StepParity::NONE)
	{
		return false;
	}
	
	if(previousRow.columns[c] != currentRow.columns[c]
	   && StepParity::OTHER_PART_OF_FOOT[previousRow.columns[c]] != currentRow.columns[c]
	   && elapsedTime < FOOTSWITCH_CUTOFF)
	{
		return true;
	}
	return false;
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
