#include "global.h"
#include "StepParityCost.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"
	
using namespace StepParity;


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


float* StepParityCost::getActionCost(State * initialState, State * resultState, std::vector<Row>& rows, int rowIndex)
{
	Row &row = rows[rowIndex];
	int columnCount = row.columnCount;
	float elapsedTime = resultState->second - initialState->second;
    
    float* costs = new float[NUM_Cost];
    for(int i = 0; i < NUM_Cost; i++)
    {
        costs[i] = 0;
    }

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

    costs[COST_MINE] += calcMineCost( initialState, resultState, row, combinedColumns, columnCount);
    
	costs[COST_HOLDSWITCH] += calcHoldSwitchCost( initialState, resultState, row, combinedColumns, columnCount);
	costs[COST_BRACKETTAP] += calcBracketTapCost( initialState, resultState, row, leftHeel, leftToe, rightHeel, rightToe, elapsedTime, columnCount);
//	costs[COST_OTHER] += calcMovingFootWhileOtherIsntOnPadCost( initialState, resultState, columnCount);

	bool movedLeft =
	  resultState->didTheFootMove[LEFT_HEEL] ||
	  resultState->didTheFootMove[LEFT_TOE];
	
	bool movedRight =
	  resultState->didTheFootMove[RIGHT_HEEL] ||
	  resultState->didTheFootMove[RIGHT_TOE];

	// Note that this is checking whether the previous state was a jump, not whether the current state is
	bool didJump =
	  ((initialState->didTheFootMove[LEFT_HEEL] &&
		!initialState->isTheFootHolding[LEFT_HEEL]) ||
		(initialState->didTheFootMove[LEFT_TOE] &&
		  !initialState->isTheFootHolding[LEFT_TOE])) &&
	  ((initialState->didTheFootMove[RIGHT_HEEL] &&
		!initialState->isTheFootHolding[RIGHT_HEEL]) ||
		(initialState->didTheFootMove[RIGHT_TOE] &&
		  !initialState->isTheFootHolding[RIGHT_TOE]));

	// jacks don't matter if you did a jump before

	bool jackedLeft = didJackLeft(initialState, resultState, leftHeel, leftToe, movedLeft, didJump, columnCount);
	bool jackedRight = didJackRight(initialState, resultState, rightHeel, rightToe, movedRight, didJump, columnCount);

	// Doublestep weighting doesn't apply if you just did a jump or a jack

	costs[COST_BRACKETJACK] += calcBracketJackCost( initialState, resultState, rows, rowIndex, movedLeft, movedRight, jackedLeft, jackedRight, didJump, columnCount);
    costs[COST_DOUBLESTEP] += calcDoublestepCost(initialState, resultState, rows, rowIndex, movedLeft, movedRight, jackedLeft, jackedRight, didJump, columnCount);
//	costs[COST_JUMP] += calcJumpCost( row, movedLeft, movedRight, elapsedTime, columnCount);
	costs[COST_SLOW_BRACKET] += calcSlowBracketCost(row, movedLeft, movedRight, elapsedTime);
	costs[COST_TWISTED_FOOT] += calcTwistedFootCost(resultState);
	costs[COST_FACING] += calcFacingCosts( initialState, resultState, combinedColumns, columnCount);
    costs[COST_SPIN] += calcSpinCosts(initialState, resultState, combinedColumns, columnCount);
	costs[COST_FOOTSWITCH] += caclFootswitchCost( initialState, resultState, row, combinedColumns, elapsedTime, columnCount);
	costs[COST_SIDESWITCH] += calcSideswitchCost( initialState, resultState, columnCount);
	costs[COST_MISSED_FOOTSWITCH] += calcMissedFootswitchCost( row, jackedLeft, jackedRight, columnCount);
	costs[COST_JACK] += calcJackCost( movedLeft, movedRight, jackedLeft, jackedRight, elapsedTime, columnCount);
	costs[COST_DISTANCE] += calcBigMovementsQuicklyCost( initialState, resultState, elapsedTime, columnCount);
//    costs[COST_CROWDED_BRACKET] += calcCrowdedBracketCost(initialState, resultState, elapsedTime, columnCount);
    
    // I don't like that we're updating columns here like this.
    // We're basically updating columns with the final position of the feet
    // for the next iteration when this is initialState
	resultState->columns = combinedColumns;
    for(int i = 0; i < columnCount; i++)
    {
        if(combinedColumns[i] >= NONE)
        {
            resultState->whereTheFeetAre[combinedColumns[i]] = i;
        }
    }
    for(int i = 0; i < COST_TOTAL; i++)
    {
        costs[COST_TOTAL] += costs[i];
    }

    return costs;
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
		if (!resultState->didTheFootMove[initialState->columns[i]]) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  } else if (initialState->columns[i] == LEFT_TOE) {
		if (
		  !resultState->didTheFootMove[LEFT_TOE] &&
		  !resultState->didTheFootMove[LEFT_HEEL]
		) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  } else if (initialState->columns[i] == RIGHT_TOE) {
		if (
		  !resultState->didTheFootMove[RIGHT_TOE] &&
		  !resultState->didTheFootMove[RIGHT_HEEL]
		) {
		  combinedColumns[i] = initialState->columns[i];
		}
	  }
	}
}

// Calculate the cost of avoiding a mine before the current step
// If a mine occurred just before a step, add to the cost
// ex: 00M0
//     0010 <- add cost
//
//     00M0
//     0100 <- no cost
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

// Calculate a cost from having to switch feet in the middle of a hold.
// Multiply the HOLDSWITCH cost by the distance that the "intial" foot
// that was holding the note had to travel to it's new position.
// If the initial foot doesn't move anywhere, then don't mulitply it by anything.
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
		int previousFoot =initialState->whereTheFeetAre[combinedColumns[c]];
		cost +=
		  HOLDSWITCH *
		  (previousFoot == -1
			? 1
			: sqrt(
				layout.getDistanceSq(c, previousFoot)
			  ));
	  }
	}
	return cost;
}

// Calculate the cost of tapping a bracket during a hold note
//
// ex: 0200
//     0000
//     1000	<- maybe bracketable, if left heel is holding Down arrow
//     0300

float StepParityCost::calcBracketTapCost(State * initialState, State * resultState, Row &row, int leftHeel, int leftToe, int rightHeel, int rightToe, float elapsedTime, int columnCount)
{
	// Small penalty for trying to jack a bracket during a hold
	float cost = 0;
	if (leftHeel != -1 && leftToe != -1)
	{
		float jackPenalty = 1;
		if (
			initialState->didTheFootMove[LEFT_HEEL] ||
			initialState->didTheFootMove[LEFT_TOE])
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
		initialState->didTheFootMove[RIGHT_TOE] ||
		initialState->didTheFootMove[RIGHT_HEEL]
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

// Calculate a cost for moving the same foot while the other
// isn't on the pad.
//
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
						initialState->whereTheFeetAre[RIGHT_HEEL] != -1 ||
						initialState->whereTheFeetAre[RIGHT_TOE] != -1))
					cost += OTHER;
				break;
			case RIGHT_HEEL:
			case RIGHT_TOE:
				if (
					!(
						initialState->whereTheFeetAre[LEFT_HEEL] != -1 ||
						initialState->whereTheFeetAre[LEFT_TOE] != -1))
					cost += OTHER;
				break;
			default:
				break;
			}
		}
	}
	return cost;
}

float StepParityCost::calcBracketJackCost(State * initialState, State * resultState, std::vector<Row> & rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount)
{
	float cost = 0;
	if (
		movedLeft != movedRight &&
		(movedLeft || movedRight) &&
		isEmpty(resultState->holdFeet, columnCount) &&
		!didJump)
	{

	  if (
		jackedLeft &&
		resultState->didTheFootMove[LEFT_HEEL] &&
		resultState->didTheFootMove[LEFT_TOE]
	  ) {
		cost += BRACKETJACK;
	  }

	  if (
		jackedRight &&
		resultState->didTheFootMove[RIGHT_HEEL] &&
		resultState->didTheFootMove[RIGHT_TOE]
	  ) {
		cost += BRACKETJACK;
	  }
	}
	return cost;
}

float StepParityCost::calcDoublestepCost(State * initialState, State * resultState, std::vector<Row> & rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump, int columnCount)
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

// Jumps should be prioritized over brackets below a certain speed
float StepParityCost::calcSlowBracketCost(Row & row, bool movedLeft, bool movedRight, float elapsedTime)
{
	float cost = 0;
	if(elapsedTime > SLOW_BRACKET_THRESHOLD && movedLeft != movedRight &&
	   std::count_if(row.notes.begin(), row.notes.end(), [](StepParity::IntermediateNoteData note)
					 { return note.type != TapNoteType_Empty; }) >= 2)
	{
		float timediff = elapsedTime - SLOW_BRACKET_THRESHOLD;
		cost += timediff * SLOW_BRACKET;
	}
	return cost;
}

// Does this placement result in one of the feet being twisted around?
// This should probably be getting filtered out as an invalid positioning before
// we even get to calculating costs.
float StepParityCost::calcTwistedFootCost(State * resultState)
{
	float cost = 0;
	int leftHeel = resultState->whereTheFeetAre[LEFT_HEEL];
	int leftToe = resultState->whereTheFeetAre[LEFT_TOE];
	int rightHeel = resultState->whereTheFeetAre[RIGHT_HEEL];
	int rightToe = resultState->whereTheFeetAre[RIGHT_TOE];
	
	StagePoint leftPos = layout.averagePoint(leftHeel, leftToe);
	StagePoint rightPos = layout.averagePoint(rightHeel, rightToe);
	
	bool crossedOver = rightPos.x < leftPos.x;
	bool rightBackwards = rightHeel != -1 && rightToe != -1 ? layout.columns[rightToe].y < layout.columns[rightHeel].y : false;
	bool leftBackwards = leftHeel != -1 && leftToe != -1 ? layout.columns[leftToe].y < layout.columns[leftHeel].y : false;
	
	if(!crossedOver && (rightBackwards || leftBackwards))
	{
		cost += TWISTED_FOOT;
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

float StepParityCost::calcFacingCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> & combinedColumns, int columnCount)
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
		? layout.getXDifference(endLeftHeel, endRightHeel)
		: 0;
	float toeFacing =
	  endLeftToe != -1 && endRightToe != -1
		? layout.getXDifference(endLeftToe, endRightToe)
		: 0;
	float leftFacing =
	  endLeftHeel != -1 && endLeftToe != -1
		? layout.getYDifference(endLeftHeel, endLeftToe)
		: 0;
	float rightFacing =
	  endRightHeel != -1 && endRightToe != -1
		? layout.getYDifference(endRightHeel, endRightToe)
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

	return cost;
}

float StepParityCost::calcSpinCosts(State * initialState, State * resultState, std::vector<StepParity::Foot> & combinedColumns, int columnCount)
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
    
    // spin
    StagePoint previousLeftPos = layout.averagePoint(
     initialState->whereTheFeetAre[LEFT_HEEL],
     initialState->whereTheFeetAre[LEFT_TOE]
    );
    StagePoint previousRightPos = layout.averagePoint(
     initialState->whereTheFeetAre[RIGHT_HEEL],
     initialState->whereTheFeetAre[RIGHT_TOE]
    );
    StagePoint leftPos = layout.averagePoint(endLeftHeel, endLeftToe);
    StagePoint rightPos = layout.averagePoint(endRightHeel, endRightToe);

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

// Footswitches are harder to do when they get too slow.
// Notes with an elapsed time greater than this will incur a penalty
float StepParityCost::caclFootswitchCost(State * initialState, State * resultState, Row & row, std::vector<StepParity::Foot> & combinedColumns, float elapsedTime, int columnCount)
{
	float cost = 0;
	if (elapsedTime >= SLOW_FOOTSWITCH_THRESHOLD && elapsedTime < SLOW_FOOTSWITCH_IGNORE) {
		// footswitching has no penalty if there's a mine nearby
		if (
			std::all_of(row.mines.begin(), row.mines.end(), [](int mine)
						{ return mine == 0; }) &&
			std::all_of(row.fakeMines.begin(), row.fakeMines.end(), [](int mine)
						{ return mine == 0; }))
		{
			float timeScaled = elapsedTime - SLOW_FOOTSWITCH_THRESHOLD;

			for (int i = 0; i < columnCount; i++)
			{
				if (
					initialState->columns[i] == NONE ||
					resultState->columns[i] == NONE)
					continue;

				if (
					initialState->columns[i] != resultState->columns[i] &&
					initialState->columns[i] != OTHER_PART_OF_FOOT[resultState->columns[i]]
					)
				{
					cost += (timeScaled / (SLOW_FOOTSWITCH_THRESHOLD + timeScaled)) * FOOTSWITCH;
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
	for(auto c : layout.sideArrows)
	{
		if (
			initialState->columns[c] != resultState->columns[c] &&
			resultState->columns[c] != NONE &&
			initialState->columns[c] != NONE &&
			!resultState->didTheFootMove[initialState->columns[c]])
		{
			cost += SIDESWITCH;
		}
	}
	return cost;
}

// Jacks are harder to do the faster they are.
// Add a penalty when they get faster than 16ths at 150bpm (0.1 seconds)
float StepParityCost::calcJackCost(bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, float elapsedTime, int columnCount)
{
	float cost = 0;
	// weighting for jacking two notes too close to eachother
	if (elapsedTime < JACK_THRESHOLD && movedLeft != movedRight) {
	  float timeScaled = JACK_THRESHOLD - elapsedTime;
	  if (jackedLeft || jackedRight) {
		cost += (1 / timeScaled - 1 / JACK_THRESHOLD) * JACK;
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
		{
			continue;
		}
		
		int initialPosition = initialState->whereTheFeetAre[foot];
		if(initialPosition == -1)
		{
			continue;
		}
		
		int resultPosition = resultState->whereTheFeetAre[foot];
		
		
		// If we're bracketing something, and the toes are now where the heel
		// was, then we don't need to worry about it, we're not actually moving
		// the foot very far
		bool isBracketing = resultState->whereTheFeetAre[OTHER_PART_OF_FOOT[foot]] != -1;
		if(isBracketing && resultState->whereTheFeetAre[OTHER_PART_OF_FOOT[foot]] == initialPosition)
		{
			continue;
		}
		
		float dist = (sqrt(layout.getDistanceSq(initialPosition, resultPosition)) * DISTANCE) / elapsedTime;
		// Otherwise if we're still bracketing, this is probably a less drastic movement
		if(isBracketing)
		{
			dist = dist * 0.2;
		}
		cost += dist;
	}

	return cost;
}

// Are we trying to bracket a column that the other foot was just on,
// or are we trying to hit a note that the other foot was just bracketing?

float StepParityCost::calcCrowdedBracketCost(State * initialState, State * resultState, float elapsedTime, int columnCount)
{
    float cost = 0;
    
    bool resultLeftBracket = resultState->whereTheFeetAre[LEFT_HEEL] > -1 && resultState->whereTheFeetAre[LEFT_TOE] > -1;
    bool resultRightBracket = resultState->whereTheFeetAre[RIGHT_HEEL] > -1 && resultState->whereTheFeetAre[RIGHT_TOE] > -1;
    
    bool initialLeftBracket = initialState->whereTheFeetAre[LEFT_HEEL] > -1 && initialState->whereTheFeetAre[LEFT_TOE] > -1;
    bool initialRightBracket = initialState->whereTheFeetAre[RIGHT_HEEL] > -1 && initialState->whereTheFeetAre[RIGHT_TOE] > -1;
    
    // if we're trying to bracket with left foot, does it overlap the right foot
    // in previous state?
    if(
       (resultLeftBracket)
       && (
           initialState->columns[resultState->whereTheFeetAre[LEFT_HEEL]] == RIGHT_HEEL ||
           initialState->columns[resultState->whereTheFeetAre[LEFT_HEEL]] == RIGHT_TOE ||
           initialState->columns[resultState->whereTheFeetAre[LEFT_TOE]] == RIGHT_HEEL ||
           initialState->columns[resultState->whereTheFeetAre[LEFT_TOE]] == RIGHT_TOE
           )
       )
    {
        cost += CROWDED_BRACKET / elapsedTime;
    }
    else if(initialLeftBracket
        && (
            resultState->columns[initialState->whereTheFeetAre[LEFT_HEEL]] == RIGHT_HEEL ||
            resultState->columns[initialState->whereTheFeetAre[LEFT_HEEL]] == RIGHT_TOE ||
            resultState->columns[initialState->whereTheFeetAre[LEFT_TOE]] == RIGHT_HEEL ||
            resultState->columns[initialState->whereTheFeetAre[LEFT_TOE]] == RIGHT_TOE
            )
        )
    {
        cost += CROWDED_BRACKET / elapsedTime;
    }
    
    // and if we're trying to bracket with right foot, does it overlap the left ?
    if((resultRightBracket )
       && (
           initialState->columns[resultState->whereTheFeetAre[RIGHT_HEEL]] == LEFT_HEEL ||
           initialState->columns[resultState->whereTheFeetAre[RIGHT_HEEL]] == LEFT_TOE ||
           initialState->columns[resultState->whereTheFeetAre[RIGHT_TOE]] == LEFT_HEEL ||
           initialState->columns[resultState->whereTheFeetAre[RIGHT_TOE]] == LEFT_TOE
           )
       )
    {
        cost += CROWDED_BRACKET / elapsedTime;
    }
    else if( initialRightBracket
        && (
            resultState->columns[initialState->whereTheFeetAre[RIGHT_HEEL]] == LEFT_HEEL ||
            resultState->columns[initialState->whereTheFeetAre[RIGHT_HEEL]] == LEFT_TOE ||
            resultState->columns[initialState->whereTheFeetAre[RIGHT_TOE]] == LEFT_HEEL ||
            resultState->columns[initialState->whereTheFeetAre[RIGHT_TOE]] == LEFT_TOE
            )
        )
    {
        cost += CROWDED_BRACKET / elapsedTime;
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
		((initialState->didTheFootMove[LEFT_HEEL] &&
		  !initialState->isTheFootHolding[LEFT_HEEL]) ||
		 (initialState->didTheFootMove[LEFT_TOE] &&
		  !initialState->isTheFootHolding[LEFT_TOE])))
	{
		doublestepped = true;
	}
	  if (
		movedRight &&
		!jackedRight &&
		((initialState->didTheFootMove[RIGHT_HEEL] &&
		  !initialState->isTheFootHolding[RIGHT_HEEL]) ||
		  (initialState->didTheFootMove[RIGHT_TOE] &&
			!initialState->isTheFootHolding[RIGHT_TOE]))
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
			!resultState->isTheFootHolding[LEFT_HEEL] &&
			((initialState->didTheFootMove[LEFT_HEEL] &&
				!initialState->isTheFootHolding[LEFT_HEEL]) ||
				(initialState->didTheFootMove[LEFT_TOE] &&
				!initialState->isTheFootHolding[LEFT_TOE]))
				) {
				jackedLeft = true;
			}
			if (
				leftToe > -1 &&
			initialState->columns[leftToe] == LEFT_TOE &&
			!resultState->isTheFootHolding[LEFT_TOE] &&
			((initialState->didTheFootMove[LEFT_HEEL] &&
				!initialState->isTheFootHolding[LEFT_HEEL]) ||
				(initialState->didTheFootMove[LEFT_TOE] &&
				!initialState->isTheFootHolding[LEFT_TOE]))
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
		  !resultState->isTheFootHolding[RIGHT_HEEL] &&
		  ((initialState->didTheFootMove[RIGHT_HEEL] &&
			!initialState->isTheFootHolding[RIGHT_HEEL]) ||
			(initialState->didTheFootMove[RIGHT_TOE] &&
			  !initialState->isTheFootHolding[RIGHT_TOE]))
			) {
				jackedRight = true;
			}
		if ( rightToe > -1 &&
		  initialState->columns[rightToe] == RIGHT_TOE &&
		  !resultState->isTheFootHolding[RIGHT_TOE] &&
		  ((initialState->didTheFootMove[RIGHT_HEEL] &&
			!initialState->isTheFootHolding[RIGHT_HEEL]) ||
			(initialState->didTheFootMove[RIGHT_TOE] &&
			  !initialState->isTheFootHolding[RIGHT_TOE]))
			) {
				jackedRight = true;
			}
	}
	return jackedRight;
}
