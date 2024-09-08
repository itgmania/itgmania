#ifndef STEP_PARITY_COST_H
#define STEP_PARITY_COST_H

#include "GameConstantsAndTypes.h"
#include "StepParityDatastructs.h"

namespace StepParity
{
	
   
	const int DOUBLESTEP= 850;
	const int BRACKETJACK= 20;
	const int JACK= 30;
	const int JUMP= 0;
	const int SLOW_BRACKET=300;
	const int TWISTED_FOOT=100000;
	const int BRACKETTAP= 400;
	const int HOLDSWITCH= 55;
	const int MINE= 10000;
	const int FOOTSWITCH= 325;
	const int MISSED_FOOTSWITCH= 500;
	const int FACING= 2;
	const int DISTANCE= 6;
	const int SPIN= 1000;
	const int SIDESWITCH= 130;
    const int CROWDED_BRACKET = 0;
    const int OTHER = 0;
    
	// 0.1 = 1/16th note at 150bpm. Jacks quicker than this are harder.
	// Above this speed, footswitches should be prioritized
	const float JACK_THRESHOLD = 0.1;
	// 0.15 = 1/8th at 200bpm, or 3/16th at 150bpm. Below this speed, jumps should be prioritized
	const float SLOW_BRACKET_THRESHOLD = 0.15;
	
	// 0.2 = 1/8th at 150bpm. Footswitches slower than this are harder.
	// Below this speed, jacks should be prioritized
	const float SLOW_FOOTSWITCH_THRESHOLD = 0.2;
	// 0.4 = 1/4th at 150bpm. Once a footswitch gets slow enough, though,
	// just ignore it.
	const float SLOW_FOOTSWITCH_IGNORE = 0.4;
	
	class StepParityCost
	{
	private:
		StageLayout layout;

	public:
		StepParityCost(const StageLayout& _layout): layout(_layout){
		}

		/// @brief Computes and returns a cost value for the player moving from initialState to resultState.
		/// @param initialState The starting position of the player
		/// @param resultState The end position of the player
		/// @param rows 
		/// @param rowIndex The index of the row represented by resultState
		/// @return The computed cost
		float* getActionCost(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex);

	private:
		void mergeInitialAndResultPosition(State * initialState, State * resultState, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcMineCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcHoldSwitchCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcBracketTapCost(State * initialState, State * resultState, Row &row, int leftHeel, int leftToe, int rightHeel, int rightToe, float elapsedTime, int columnCount);
		float calcMovingFootWhileOtherIsntOnPadCost(State * initialState, State * resultState, int columnCount);
		float calcBracketJackCost(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount);
        float calcDoublestepCost(State * initialState, State * resultState, std::vector<Row> & rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount);
		float calcJumpCost(Row &row, bool movedLeft, bool movedRight, float elapsedTime, int columnCount);
		float calcSlowBracketCost(Row & row, bool movedLeft, bool movedRight, float elapsedTime);
		float calcTwistedFootCost(State * resultState);
		float calcMissedFootswitchCost(Row &row, bool jackedLeft, bool jackedRight, int columnCount);
		float calcFacingCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
        float calcSpinCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> & combinedColumns, int columnCount);
		float caclFootswitchCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, float elapsedTime, int columnCount);
		float calcSideswitchCost(State * initialState, State * resultState, int columnCount);
		float calcJackCost(bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, float elapsedTime, int columnCount);
		float calcBigMovementsQuicklyCost(State * initialState, State * resultState, float elapsedTime, int columnCount);
        float calcCrowdedBracketCost(State * initialState, State * resultState, float elapsedTime, int columnCount);
        
		bool didDoubleStep(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight, int columnCount);
		bool didJackLeft(State * initialState, State * resultState, int leftHeel, int leftToe, bool movedLeft, bool didJump, int columnCount);
		bool didJackRight(State * initialState, State * resultState, int rightHeel, int rightToe, bool movedRight, bool didJump,int columnCount);
	};
};

#endif

