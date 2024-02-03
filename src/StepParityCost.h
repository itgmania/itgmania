#ifndef STEP_PARITY_COST_H
#define STEP_PARITY_COST_H

#include "GameConstantsAndTypes.h"
#include "StepParityDatastructs.h"

namespace StepParity
{
	enum WEIGHTS {
		DOUBLESTEP= 850,
		BRACKETJACK= 20,
		JACK= 30,
		JUMP= 30,
		BRACKETTAP= 400,
		HOLDSWITCH= 20,
		MINE= 10000,
		FOOTSWITCH= 5000,
		MISSED_FOOTSWITCH= 500,
		FACING= 2,
		DISTANCE= 6,
		SPIN= 1000,
		SIDESWITCH= 130,
	};

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

