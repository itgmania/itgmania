#include "global.h"
#include "StepParityCost.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"
	
using namespace StepParity;


template <typename T>
bool vectorIncludes(const std::vector<T>& vec, const T& value, int columnCount) {
	for (int i = 0; i < columnCount; i++)
	{
		if(vec[i] == value)
		{
			return true;
		}
	}
	return false;
}

template <typename T>
int indexOf(const std::vector<T>& vec, const T& value, int columnCount) {
	for (int i = 0; i < columnCount; i++)
	{
		if(vec[i] == value)
		{
			return i;
		}
	}
	return -1;
}

template <typename T>
bool isEmpty(const std::vector<T> & vec, int columnCount) {
	for (int i = 0; i < columnCount; i++)
	{
		if(static_cast<int>(vec[i]) != 0)
		{
			return false;
		}
	}
	return true;
}


float StepParityCost::getActionCost(State * initialState, State * resultState, std::vector<Row>& rows, int rowIndex)
{
	Row &row = rows[rowIndex];
	int columnCount = row.columnCount;
	float elapsedTime = resultState->second - initialState->second;
	float cost = 0;

	std::vector<StepParity::Foot> combinedColumns(columnCount, NONE);

	mergeInitialAndResultPosition(initialState, resultState, combinedColumns, columnCount);
	
	// Mine weighting
	int leftHeel = -1;
	int leftToe = -1;
	int rightHeel = -1;
	int rightToe = -1;

	for (int i = 0; i < columnCount; i++) {
	  switch (resultState->columns[i]) {
		case NONE:
		  break;
		case LEFT_HEEL:
		  leftHeel = i;
		  break;
		case LEFT_TOE:
		  leftToe = i;
		  break;
		case RIGHT_HEEL:
		  rightHeel = i;
		  break;
		case RIGHT_TOE:
		  rightToe = i;
		  break;
	  default:
		  break;
	  }
	}

	cost += calcMineCost( initialState, resultState, row, combinedColumns, columnCount);
	cost += calcHoldSwitchCost( initialState, resultState, row, combinedColumns, columnCount);
	cost += calcBracketTapCost( initialState, resultState, row, leftHeel, leftToe, rightHeel, rightToe, elapsedTime, columnCount);
	cost += calcMovingFootWhileOtherIsntOnPadCost( initialState, resultState, columnCount);

	bool movedLeft =
	  vectorIncludes(resultState->movedFeet, LEFT_HEEL, columnCount) ||
	  vectorIncludes(resultState->movedFeet, LEFT_TOE, columnCount);
	
	bool movedRight =
	  vectorIncludes(resultState->movedFeet, RIGHT_HEEL, columnCount) ||
	  vectorIncludes(resultState->movedFeet, RIGHT_TOE, columnCount);

	// Note that this is checking whether the previous state was a jump, not whether the current state is
	bool didJump =
	  ((vectorIncludes(initialState->movedFeet, LEFT_HEEL, columnCount) &&
		!vectorIncludes(initialState->holdFeet, LEFT_HEEL, columnCount)) ||
		(vectorIncludes(initialState->movedFeet, LEFT_TOE, columnCount) &&
		  !vectorIncludes(initialState->holdFeet, LEFT_TOE, columnCount))) &&
	  ((vectorIncludes(initialState->movedFeet, RIGHT_HEEL, columnCount) &&
		!vectorIncludes(initialState->holdFeet, RIGHT_HEEL, columnCount)) ||
		(vectorIncludes(initialState->movedFeet, RIGHT_TOE, columnCount) &&
		  !vectorIncludes(initialState->holdFeet, RIGHT_TOE, columnCount)));

	// jacks don't matter if you did a jump before

	bool jackedLeft = didJackLeft(initialState, resultState, leftHeel, leftToe, movedLeft, didJump, columnCount);
	bool jackedRight = didJackRight(initialState, resultState, rightHeel, rightToe, movedRight, didJump, columnCount);

	// Doublestep weighting doesn't apply if you just did a jump or a jack

	cost += calcJackCost( initialState, resultState, rows, rowIndex, movedLeft, movedRight, jackedLeft, jackedRight, didJump, columnCount);
	cost += calcJumpCost( row, movedLeft, movedRight, elapsedTime, columnCount);
	cost += calcFacingAndSpinCosts( initialState, resultState, combinedColumns, columnCount);
	cost += caclFootswitchCost( initialState, resultState, row, combinedColumns, elapsedTime, columnCount);
	cost += calcSideswitchCost( initialState, resultState, columnCount);
	cost += calcMissedFootswitchCost( row, jackedLeft, jackedRight, columnCount);

	// To do: small weighting for swapping heel with toe or toe with heel (both add up)
	// To do: huge weighting for having foot direction opposite of eachother (can't twist one leg 180 degrees)
	cost += calcJackedNotesTooCloseTogetherCost( movedLeft, movedRight, jackedLeft, jackedRight, elapsedTime, columnCount);
	
	// To do: weighting for moving a foot a far distance in a fast time
	cost += calcBigMovementsQuicklyCost( initialState, resultState, elapsedTime, columnCount);
	
	resultState->columns = combinedColumns;
	return cost;
}

// This merges the `columns` properties of initialState and resultState, which
// fully represents the player's position on the dance stage.
// For example:
// initialState.columns = [1,0,0,3]
// resultState.columns = [0,1,0,0]
// combinedColumns = [0,1,0,3]
// This eventually gets saved back to resultState
void StepParityCost::mergeInitialAndResultPosition(State * initialState, State * resultState, std::vector<StepParity::Foot> & combinedColumns, int columnCount)
{
	// Merge initial + result position
	for (int i = 0; i < columnCount; i++) {
	  // copy in data from resultState over the top which overrides it, as long as it's not nothing
	  if (resultState->columns[i] != NONE) {
		combinedColumns[i] = resultState->columns[i];
		continue;
	  }

	  // copy in data from initialState, if it wasn't moved
	  if (
		initialState->columns[i] == LEFT_HEEL ||
		initialState->columns[i] == RIGHT_HEEL
	  ) {
		if (!vectorIncludes(resultState->movedFeet, initialState->columns[i], columnCount)) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  } else if (initialState->columns[i] == LEFT_TOE) {
		if (
		  !vectorIncludes(resultState->movedFeet, LEFT_TOE, columnCount) &&
		  !vectorIncludes(resultState->movedFeet, LEFT_HEEL, columnCount)
		) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  } else if (initialState->columns[i] == RIGHT_TOE) {
		if (
		  !vectorIncludes(resultState->movedFeet, RIGHT_TOE, columnCount) &&
		  !vectorIncludes(resultState->movedFeet, RIGHT_HEEL, columnCount)
		) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  }
	}
}

float StepParityCost::calcMineCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot>& combinedColumns, int columnCount)
{
	float cost = 0;

	for (int i = 0; i < columnCount; i++) {
		if (combinedColumns[i] != NONE && row.mines[i] != 0) {
			cost += MINE;
			break;
	  	}
	}
	return cost;
}

float StepParityCost::calcHoldSwitchCost(State * initialState, State * resultState, Row &row, std::vector<StepParity::Foot> & combinedColumns, int columnCount)
{
	float cost = 0;
	for (int c = 0; c < columnCount; c++)
	{
		if (row.holds[c].type == TapNoteType_Empty)
			continue;
		if (
			((combinedColumns[c] == LEFT_HEEL ||
			  combinedColumns[c] == LEFT_TOE) &&
			 initialState->columns[c] != LEFT_TOE &&
			 initialState->columns[c] != LEFT_HEEL) ||
			((combinedColumns[c] == RIGHT_HEEL ||
			  combinedColumns[c] == RIGHT_TOE) &&
			 initialState->columns[c] != RIGHT_TOE &&
			 initialState->columns[c] != RIGHT_HEEL)) {
		int previousFoot =indexOf(initialState->columns, combinedColumns[c], columnCount);
		cost +=
		  HOLDSWITCH *
		  (previousFoot == -1
			? 1
			: sqrt(
				getDistanceSq(layout[c], layout[previousFoot])
			  ));
	  }
	}
	return cost;
}

float StepParityCost::calcBracketTapCost(State * initialState, State * resultState, Row &row, int leftHeel, int leftToe, int rightHeel, int rightToe, float elapsedTime, int columnCount)
{
	// Small penalty for trying to jack a bracket during a hold
	float cost = 0;
	if (leftHeel != -1 && leftToe != -1)
	{
		float jackPenalty = 1;
		if (
			vectorIncludes(initialState->movedFeet, LEFT_HEEL, columnCount) ||
			vectorIncludes(initialState->movedFeet, LEFT_TOE, columnCount))
			jackPenalty = 1 / elapsedTime;
		if (
			row.holds[leftHeel].type != TapNoteType_Empty &&
			row.holds[leftToe].type == TapNoteType_Empty) {
		cost += BRACKETTAP * jackPenalty;
	  }
	  if (
		row.holds[leftToe].type != TapNoteType_Empty &&
		row.holds[leftHeel].type == TapNoteType_Empty
	  ) {
		cost += BRACKETTAP * jackPenalty;
	  }
	}

	if (rightHeel != -1 && rightToe != -1) {
	  float jackPenalty = 1;
	  if (
		vectorIncludes(initialState->movedFeet, RIGHT_TOE, columnCount) ||
		vectorIncludes(initialState->movedFeet, RIGHT_HEEL, columnCount)
	  )
		jackPenalty = 1 / elapsedTime;

	  if (
		row.holds[rightHeel].type != TapNoteType_Empty &&
		row.holds[rightToe].type == TapNoteType_Empty
	  ) {
		cost += BRACKETTAP * jackPenalty;
	  }
	  if (
		row.holds[rightToe].type != TapNoteType_Empty &&
		row.holds[rightHeel].type == TapNoteType_Empty
	  ) {
		cost += BRACKETTAP * jackPenalty;
	  }
	}
	return cost;
}

float StepParityCost::calcMovingFootWhileOtherIsntOnPadCost(State * initialState, State * resultState, int columnCount)
{
	float cost = 0;
	// Weighting for moving a foot while the other isn't on the pad (so marked doublesteps are less bad than this)
	if (std::any_of(initialState->columns.begin(), initialState->columns.end(), [](Foot elem) { return elem != NONE;  }))
	{
		for (auto f : resultState->movedFeet)
		{
			switch (f)
			{
			case LEFT_HEEL:
			case LEFT_TOE:
				if (
					!(
						vectorIncludes(initialState->columns, RIGHT_HEEL, columnCount) ||
						vectorIncludes(initialState->columns, RIGHT_TOE, columnCount)))
					cost += 500;
				break;
			case RIGHT_HEEL:
			case RIGHT_TOE:
				if (
					!(
						vectorIncludes(initialState->columns, LEFT_HEEL, columnCount) ||
						vectorIncludes(initialState->columns, RIGHT_TOE, columnCount)))
					cost += 500;
				break;
			default:
				break;
			}
		}
	}
	return cost;
}

float StepParityCost::calcJackCost(State * initialState, State * resultState, std::vector<Row> & rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount)
{
	float cost = 0;
	if (
		movedLeft != movedRight &&
		(movedLeft || movedRight) &&
		isEmpty(resultState->holdFeet, columnCount) &&
		!didJump)
	{
		bool doublestepped = didDoubleStep(initialState, resultState, rows, rowIndex, movedLeft, jackedLeft, movedRight, jackedRight, columnCount);

	  if (doublestepped) {
		cost += DOUBLESTEP;
	  }

	  if (
		jackedLeft &&
		vectorIncludes(resultState->movedFeet, LEFT_HEEL, columnCount) &&
		vectorIncludes(resultState->movedFeet, LEFT_TOE, columnCount)
	  ) {
		cost += BRACKETJACK;
	  }

	  if (
		jackedRight &&
		vectorIncludes(resultState->movedFeet, RIGHT_HEEL, columnCount) &&
		vectorIncludes(resultState->movedFeet, RIGHT_TOE, columnCount)
	  ) {
		cost += BRACKETJACK;
	  }
	}
	return cost;
}

float StepParityCost::calcJumpCost(Row & row, bool movedLeft, bool movedRight, float elapsedTime, int columnCount)
{
	float cost = 0;
	if (
		movedLeft &&
		movedRight &&
		std::count_if(row.notes.begin(), row.notes.end(), [](StepParity::IntermediateNoteData note)
					  { return note.type != TapNoteType_Empty; }) >= 2)
	{
		cost += JUMP / elapsedTime;
	}

	return cost;
}

float StepParityCost::calcMissedFootswitchCost(Row & row, bool jackedLeft, bool jackedRight, int columnCount)
{
	float cost = 0;
	if (
		(jackedLeft || jackedRight) &&
		(std::any_of(row.mines.begin(), row.mines.end(), [](int mine)
					 { return mine != 0; }) ||
		 std::any_of(row.fakeMines.begin(), row.fakeMines.end(), [](int mine)
					 { return mine != 0; })))
	{
		cost += MISSED_FOOTSWITCH;
	}

	return cost;
}

float StepParityCost::calcFacingAndSpinCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> & combinedColumns, int columnCount)
{

	float cost = 0;

	float endLeftHeel = -1;
	float endLeftToe = -1;
	float endRightHeel = -1;
	float endRightToe = -1;

	for (int i = 0; i < columnCount; i++) {
	  switch (combinedColumns[i]) {
		case NONE:
		  break;
		case LEFT_HEEL:
		  endLeftHeel = i;
		  break;
		case LEFT_TOE:
		  endLeftToe = i;
		  break;
		case RIGHT_HEEL:
		  endRightHeel = i;
		  break;
		case RIGHT_TOE:
		  endRightToe = i;
		default:
		  break;
	  }
	}

	if (endLeftToe == -1) endLeftToe = endLeftHeel;
	if (endRightToe == -1) endRightToe = endRightHeel;

	// facing backwards gives a bit of bad weight (scaled heavily the further back you angle, so crossovers aren't Too bad; less bad than doublesteps)
	float heelFacing =
	  endLeftHeel != -1 && endRightHeel != -1
		? getXDifference(endLeftHeel, endRightHeel)
		: 0;
	float toeFacing =
	  endLeftToe != -1 && endRightToe != -1
		? getXDifference(endLeftToe, endRightToe)
		: 0;
	float leftFacing =
	  endLeftHeel != -1 && endLeftToe != -1
		? getYDifference(endLeftHeel, endLeftToe)
		: 0;
	float rightFacing =
	  endRightHeel != -1 && endRightToe != -1
		? getYDifference(endRightHeel, endRightToe)
		: 0;


	float heelFacingPenalty = pow(-1 * std::min(heelFacing, 0.0f), 1.8) * 100;
	float toesFacingPenalty = pow(-1 * std::min(toeFacing, 0.0f), 1.8) * 100;
	float leftFacingPenalty = pow(-1 * std::min(leftFacing, 0.0f), 1.8) * 100;
	float rightFacingPenalty = pow(-1 * std::min(rightFacing, 0.0f), 1.8) * 100;


	if (heelFacingPenalty > 0)
		cost += heelFacingPenalty * FACING;
	if (toesFacingPenalty > 0) 
		cost += toesFacingPenalty * FACING;
	if (leftFacingPenalty > 0) 
		cost += leftFacingPenalty * FACING;
	if (rightFacingPenalty > 0) 
		cost += rightFacingPenalty * FACING;

	// spin
	StagePoint previousLeftPos = averagePoint(
	 indexOf(initialState->columns, LEFT_HEEL, columnCount),
	 indexOf(initialState->columns, LEFT_TOE, columnCount)
	);
	StagePoint previousRightPos = averagePoint(
	 indexOf(initialState->columns, RIGHT_HEEL, columnCount),
	 indexOf(initialState->columns, RIGHT_TOE, columnCount)
	);
	StagePoint leftPos = averagePoint(endLeftHeel, endLeftToe);
	StagePoint rightPos = averagePoint(endRightHeel, endRightToe);

	if (
	  rightPos.x < leftPos.x &&
	  previousRightPos.x < previousLeftPos.x &&
	  rightPos.y < leftPos.y &&
	  previousRightPos.y > previousLeftPos.y
	) {
	  cost += SPIN;
	}
	if (
	  rightPos.x < leftPos.x &&
	  previousRightPos.x < previousLeftPos.x &&
	  rightPos.y > leftPos.y &&
	  previousRightPos.y < previousLeftPos.y
	) {
	  cost += SPIN;
	}

	return cost;
}

float StepParityCost::caclFootswitchCost(State * initialState, State * resultState, Row & row, std::vector<StepParity::Foot> & combinedColumns, float elapsedTime, int columnCount)
{
	float cost = 0;
	// ignore footswitch with 24 or less distance (8th note); penalise slower footswitches based on distance
	if (elapsedTime >= 0.25) {
		// footswitching has no penalty if there's a mine nearby
	if (
		std::all_of(row.mines.begin(), row.mines.end(), [](int mine)
					{ return mine == 0; }) &&
		std::all_of(row.fakeMines.begin(), row.fakeMines.end(), [](int mine)
					{ return mine == 0; }))
	{
		float timeScaled = elapsedTime - 0.25;

		for (int i = 0; i < columnCount; i++)
		{
			if (
				initialState->columns[i] == NONE ||
				resultState->columns[i] == NONE)
				continue;

			if (
				initialState->columns[i] != resultState->columns[i] &&
				!vectorIncludes(resultState->movedFeet, initialState->columns[i], columnCount))
			{
				cost += pow(timeScaled / 2.0f, 2) * FOOTSWITCH;
				break;
			}
		}
	  }
	}

	  return cost;
}

float StepParityCost::calcSideswitchCost(State * initialState, State * resultState, int columnCount)
{
	float cost = 0;
	if (
		initialState->columns[0] != resultState->columns[0] &&
		resultState->columns[0] != NONE &&
		initialState->columns[0] != NONE &&
		!vectorIncludes(resultState->movedFeet, initialState->columns[0], columnCount))
	{
		cost += SIDESWITCH;
	}

	if (
	  initialState->columns[3] != resultState->columns[3] &&
	  resultState->columns[3] != NONE &&
	  initialState->columns[3] != NONE &&
	  !vectorIncludes(resultState->movedFeet, initialState->columns[3], columnCount)
	) {
	  cost += SIDESWITCH;
	}
	return cost;
}

float StepParityCost::calcJackedNotesTooCloseTogetherCost(bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, float elapsedTime, int columnCount)
{
	float cost = 0;
	// weighting for jacking two notes too close to eachother
	if (elapsedTime <= 0.15 && movedLeft != movedRight) {
	  float timeScaled = 0.15 - elapsedTime;
	  if (jackedLeft || jackedRight) {
		cost += (1 / timeScaled - 1 / 0.15) * JACK;
	  }
	}

	return cost;
}

float StepParityCost::calcBigMovementsQuicklyCost(State * initialState, State * resultState, float elapsedTime, int columnCount)
{
	float cost = 0;
	for (StepParity::Foot foot : resultState->movedFeet)
	{
		if(foot == NONE)
			continue;
		int idxFoot = indexOf(initialState->columns, foot, columnCount);
		if (idxFoot == -1)
			continue;
		cost +=
			(sqrt(
				 getDistanceSq(
					 layout[idxFoot],
					 layout[indexOf(resultState->columns, foot, columnCount)])) *
			 DISTANCE) /
			elapsedTime;
	}

	return cost;
}



bool StepParityCost::didDoubleStep(State * initialState, State * resultState, std::vector<Row> & rows, int rowIndex, bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight, int columnCount)
{
	Row &row = rows[rowIndex];
	bool doublestepped = false;
	if (
		movedLeft &&
		!jackedLeft &&
		((vectorIncludes(initialState->movedFeet, LEFT_HEEL, columnCount) &&
		  !vectorIncludes(initialState->holdFeet, LEFT_HEEL, columnCount)) ||
		 (vectorIncludes(initialState->movedFeet, LEFT_TOE, columnCount) &&
		  !vectorIncludes(initialState->holdFeet, LEFT_TOE, columnCount))))
	{
		doublestepped = true;
	}
	  if (
		movedRight &&
		!jackedRight &&
		((vectorIncludes(initialState->movedFeet, RIGHT_HEEL, columnCount) &&
		  !vectorIncludes(initialState->holdFeet, RIGHT_HEEL, columnCount)) ||
		  (vectorIncludes(initialState->movedFeet, RIGHT_TOE, columnCount) &&
			!vectorIncludes(initialState->holdFeet, RIGHT_TOE, columnCount)))
	  )
		doublestepped = true;


	  if (rowIndex - 1 > -1)
	  {
	  	StepParity::Row &lastRow = rows[rowIndex - 1];
		for (StepParity::IntermediateNoteData hold: lastRow.holds) {
		  if (hold.type == TapNoteType_Empty) continue;
		  float endBeat = row.beat;
		  float startBeat = lastRow.beat;
		  // if a hold tail extends past the last row & ends in between, we can doublestep
		  if (
			hold.beat + hold.hold_length > startBeat &&
			hold.beat + hold.hold_length < endBeat
		  )
			doublestepped = false;
		  // if the hold tail extends past this row, we can doublestep
		  if (hold.beat + hold.hold_length >= endBeat) doublestepped = false;
		}
	  }
	  return doublestepped;
}

bool StepParityCost::didJackLeft(State * initialState, State * resultState, int leftHeel, int leftToe, bool movedLeft, bool didJump, int columnCount)
{
	bool jackedLeft = false;
	if(!didJump && movedLeft)
	{
		
			if ( leftHeel > -1 &&
			initialState->columns[leftHeel] == LEFT_HEEL &&
			!vectorIncludes(resultState->holdFeet, LEFT_HEEL, columnCount) &&
			((vectorIncludes(initialState->movedFeet, LEFT_HEEL, columnCount) &&
				!vectorIncludes(initialState->holdFeet, LEFT_HEEL, columnCount)) ||
				(vectorIncludes(initialState->movedFeet, LEFT_TOE, columnCount) &&
				!vectorIncludes(initialState->holdFeet, LEFT_TOE, columnCount)))
				) {
				jackedLeft = true;
			}
			if (
				leftToe > -1 &&
			initialState->columns[leftToe] == LEFT_TOE &&
			!vectorIncludes(resultState->holdFeet, LEFT_TOE, columnCount) &&
			((vectorIncludes(initialState->movedFeet, LEFT_HEEL, columnCount) &&
				!vectorIncludes(initialState->holdFeet, LEFT_HEEL, columnCount)) ||
				(vectorIncludes(initialState->movedFeet, LEFT_TOE, columnCount) &&
				!vectorIncludes(initialState->holdFeet, LEFT_TOE, columnCount)))
				){
					jackedLeft = true;
				}
		
	}
	return jackedLeft;
}

bool StepParityCost::didJackRight(State * initialState, State * resultState, int rightHeel, int rightToe, bool movedRight, bool didJump, int columnCount)
{
	bool jackedRight = false;
	if(!didJump && movedRight)
	{
		if ( rightHeel > -1 &&
		  initialState->columns[rightHeel] == RIGHT_HEEL &&
		  !vectorIncludes(resultState->holdFeet, RIGHT_HEEL, columnCount) &&
		  ((vectorIncludes(initialState->movedFeet, RIGHT_HEEL, columnCount) &&
			!vectorIncludes(initialState->holdFeet, RIGHT_HEEL, columnCount)) ||
			(vectorIncludes(initialState->movedFeet, RIGHT_TOE, columnCount) &&
			  !vectorIncludes(initialState->holdFeet, RIGHT_TOE, columnCount)))
			) {
				jackedRight = true;
			}
		if ( rightToe > -1 &&
		  initialState->columns[rightToe] == RIGHT_TOE &&
		  !vectorIncludes(resultState->holdFeet, RIGHT_TOE, columnCount) &&
		  ((vectorIncludes(initialState->movedFeet, RIGHT_HEEL, columnCount) &&
			!vectorIncludes(initialState->holdFeet, RIGHT_HEEL, columnCount)) ||
			(vectorIncludes(initialState->movedFeet, RIGHT_TOE, columnCount) &&
			  !vectorIncludes(initialState->holdFeet, RIGHT_TOE, columnCount)))
			) {
				jackedRight = true;
			}
	}
	return jackedRight;
}


float StepParityCost::getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2)
{
	return (p1.y - p2.y) * (p1.y - p2.y) + (p1.x - p2.x) * (p1.x - p2.x);
}

float StepParityCost::getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right)
{
	float x1 = right.x - left.x;
	float y1 = right.y - left.y;
	float x2 = 1;
	float y2 = 0;
	float dot = x1 * x2 + y1 * y2;
	float det = x1 * y2 - y1 * x2;
	return atan2f(det, dot);
}




float StepParityCost::getXDifference(int leftIndex, int rightIndex) {
	if (leftIndex == rightIndex) return 0;
	float dx = layout[rightIndex].x - layout[leftIndex].x;
	float dy = layout[rightIndex].y - layout[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dx /= distance;

	bool negative = dx <= 0;

	dx = pow(dx, 4);

	if (negative) dx = -dx;

	return dx;
  }

  float StepParityCost::getYDifference(int leftIndex, int rightIndex) {
	if (leftIndex == rightIndex) return 0;
	float dx = layout[rightIndex].x - layout[leftIndex].x;
	float dy = layout[rightIndex].y - layout[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dy /= distance;

	bool negative = dy <= 0;

	dy = pow(dy, 4);

	if (negative) dy = -dy;

	return dy;
  }

StagePoint StepParityCost::averagePoint(int leftIndex, int rightIndex) {
	if (leftIndex == -1 && rightIndex == -1) return { 0,0 };
	if (leftIndex == -1) return layout[rightIndex];
	if (rightIndex == -1) return layout[leftIndex];
	return {
	  (layout[leftIndex].x + layout[rightIndex].x) / 2.0f,
	  (layout[leftIndex].y + layout[rightIndex].y) / 2.0f,
	};
  }
