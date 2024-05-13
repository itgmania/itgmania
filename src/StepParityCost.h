#ifndef STEP_PARITY_COST_H
#define STEP_PARITY_COST_H

#include "GameConstantsAndTypes.h"
#include "StepParityDatastructs.h"

namespace StepParity
{
	
	const int DOUBLESTEP= 850;
	const int BRACKETJACK= 20;
	const int JACK= 30;
	const int JUMP= 30;
	const int BRACKETTAP= 400;
	const int HOLDSWITCH= 20;
	const int MINE= 10000;
	const int FOOTSWITCH= 5000;
	const int MISSED_FOOTSWITCH= 500;
	const int FACING= 2;
	const int DISTANCE= 6;
	const int SPIN= 1000;
	const int SIDESWITCH= 130;
	const int MOVING_SINGLE_FOOT = 500;
	
	class StepParityCost
	{
	private:
		StageLayout layout;

	public:
		StepParityCost(StageLayout _layout)
		{
			layout = _layout;
		}

		/// @brief Computes and returns a cost value for the player moving from initialState to resultState.
		/// @param initialState The starting position of the player
		/// @param resultState The end position of the player
		/// @param rows 
		/// @param rowIndex The index of the row represented by resultState
		/// @return The computed cost
		float getActionCost(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex);

	private:
		void mergeInitialAndResultPosition(State * initialState, State * resultState, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcMineCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcHoldSwitchCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float calcBracketTapCost(State * initialState, State * resultState, Row &row, int leftHeel, int leftToe, int rightHeel, int rightToe, float elapsedTime, int columnCount);
		float calcMovingFootWhileOtherIsntOnPadCost(State * initialState, State * resultState, int columnCount);
		float calcJackCost(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount);
		float calcJumpCost(Row &row, bool movedLeft, bool movedRight, float elapsedTime, int columnCount);
		float calcMissedFootswitchCost(Row &row, bool jackedLeft, bool jackedRight, int columnCount);
		float calcFacingAndSpinCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> &combinedColumns, int columnCount);
		float caclFootswitchCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> &combinedColumns, float elapsedTime, int columnCount);
		float calcSideswitchCost(State * initialState, State * resultState, int columnCount);
		float calcJackedNotesTooCloseTogetherCost(bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, float elapsedTime, int columnCount);
		float calcBigMovementsQuicklyCost(State * initialState, State * resultState, float elapsedTime, int columnCount);

		bool didDoubleStep(State * initialState, State * resultState, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight, int columnCount);
		bool didJackLeft(State * initialState, State * resultState, int leftHeel, int leftToe, bool movedLeft, bool didJump, int columnCount);
		bool didJackRight(State * initialState, State * resultState, int rightHeel, int rightToe, bool movedRight, bool didJump,int columnCount);

		float getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2);
		float getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right);

		float getXDifference(int leftIndex, int rightIndex);
		float getYDifference(int leftIndex, int rightIndex);
		StagePoint averagePoint(int leftIndex, int rightIndex);

	};
};

#endif

