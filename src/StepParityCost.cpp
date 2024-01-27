#include "global.h"
#include "StepParityGenerator.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"

using namespace StepParity;

template <typename T>
bool setContains(const std::set<T>& aSet, const T& value) {
    return aSet.find(value) != aSet.end();
}

template <typename T>
bool vectorIncludes(const std::vector<T>& vec, const T& value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}

template <typename T>
int indexOf(const std::vector<T>& vec, const T& value) {
    auto it = std::find(vec.begin(), vec.end(), value);
    if (it != vec.end()) {
        return std::distance(vec.begin(), it);
    } else {
        return -1;
    }
}

float StepParityGenerator::getCachedCost(int rowIndex, RString cacheKey)
{
	if(costCache.find(rowIndex) == costCache.end() || costCache[rowIndex].find(cacheKey) == costCache[rowIndex].end() )
	{
		return -1;
	}

	return costCache[rowIndex][cacheKey];
}

void StepParityGenerator::getActionCost(Action *action, std::vector<Row>& rows, int rowIndex)
{
	Row row = rows[rowIndex];

	float elapsedTime = action->resultState->second - action->initialState->second;
	float cost = 0;

	std::vector<StepParity::Foot> combinedColumns(action->resultState->columns.size(), NONE);

	// Merge initial + result position
	for (unsigned long i = 0; i < action->resultState->columns.size(); i++) {
	  // copy in data from b over the top which overrides it, as long as it's not nothing
	  if (action->resultState->columns[i] != NONE) {
		combinedColumns[i] = action->resultState->columns[i];
		continue;
	  }

	  // copy in data from a first, if it wasn't moved
	  if (
		action->initialState->columns[i] == LEFT_HEEL ||
		action->initialState->columns[i] == RIGHT_HEEL
	  ) {
		if (!setContains(action->resultState->movedFeet, action->initialState->columns[i])) {
		  combinedColumns[i] = action->initialState->columns[i];
		}
	  } else if (action->initialState->columns[i] == LEFT_TOE) {
		if (
		  !setContains(action->resultState->movedFeet, LEFT_TOE) &&
		  !setContains(action->resultState->movedFeet, LEFT_HEEL)
		) {
		  combinedColumns[i] = action->initialState->columns[i];
		}
	  } else if (action->initialState->columns[i] == RIGHT_TOE) {
		if (
		  !setContains(action->resultState->movedFeet, RIGHT_TOE) &&
		  !setContains(action->resultState->movedFeet, RIGHT_HEEL)
		) {
		  combinedColumns[i] = action->initialState->columns[i];
		}
	  }
	}

	// std::vector<StepParity::Foot> cacheKeyEnums = action->initialState->columns;
	// cacheKeyEnums.insert(cacheKeyEnums.end(), action->resultState->columns.begin(), action->resultState->columns.end());
	// cacheKeyEnums.insert(cacheKeyEnums.end(), action->initialState->movedFeet.begin(), action->initialState->movedFeet.end());

	// std::vector<RString> cacheKeyStrings;
	// for(StepParity::Foot f: cacheKeyEnums)
	// {
	// 	cacheKeyStrings.push_back(std::to_string(static_cast<int>(f)));
	// }

	// RString cacheKey = join("|", cacheKeyStrings);

	// float cachedCost = getCachedCost(rowIndex, cacheKey);
	// if(cachedCost != -1)
	// {
	// 	cacheCounter++;
	// 	action->resultState->columns = combinedColumns;
	//   	action->cost = cachedCost;
	// 	return;
	// }
	
	// Mine weighting
	int leftHeel = -1;
	int leftToe = -1;
	int rightHeel = -1;
	int rightToe = -1;

	for (unsigned long i = 0; i < action->resultState->columns.size(); i++) {
	  switch (action->resultState->columns[i]) {
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
	  }
	  if (combinedColumns[i] != NONE && row.mines[i] != 0) {
		cost += MINE;
		break;
	  }
	}

	for (unsigned long c = 0; c < row.holds.size(); c++) {
	  if (row.holds[c].type == TapNoteType_Empty ) continue;
	  if (
		((combinedColumns[c] == LEFT_HEEL ||
		  combinedColumns[c] == LEFT_TOE) &&
		  action->initialState->columns[c] != LEFT_TOE &&
		  action->initialState->columns[c] != LEFT_HEEL) ||
		((combinedColumns[c] == RIGHT_HEEL ||
		  combinedColumns[c] == RIGHT_TOE) &&
		  action->initialState->columns[c] != RIGHT_TOE &&
		  action->initialState->columns[c] != RIGHT_HEEL)
	  ) {
		int previousFoot =indexOf(action->initialState->columns, combinedColumns[c]);
		cost +=
		  HOLDSWITCH *
		  (previousFoot == -1
			? 1
			: sqrt(
				getDistanceSq(layout[c], layout[previousFoot])
			  ));
	  }
	}

	// Small penalty for trying to jack a bracket during a hold
	if (leftHeel != -1 && leftToe != -1) {
	  float jackPenalty = 1;
	  if (
		setContains(action->initialState->movedFeet, LEFT_HEEL) ||
		setContains(action->initialState->movedFeet, LEFT_TOE)
	  )
		jackPenalty = 1 / elapsedTime;
	  if (
		row.holds[leftHeel].type != TapNoteType_Empty &&
		row.holds[leftToe].type == TapNoteType_Empty
	  ) {
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
		setContains(action->initialState->movedFeet, RIGHT_TOE) ||
		setContains(action->initialState->movedFeet, RIGHT_HEEL)
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

	// Weighting for moving a foot while the other isn't on the pad (so marked doublesteps are less bad than this)
	if (std::any_of(action->initialState->columns.begin(), action->initialState->columns.end(), [](Foot elem) { return elem != NONE;  }))
	{
		for (auto f : action->resultState->movedFeet)
		{
			switch (f)
			{
			case LEFT_HEEL:
			case LEFT_TOE:
				if (
					!(
						vectorIncludes(action->initialState->columns, RIGHT_HEEL) ||
						vectorIncludes(action->initialState->columns, RIGHT_TOE)))
					cost += 500;
				break;
			case RIGHT_HEEL:
			case RIGHT_TOE:
				if (
					!(
						vectorIncludes(action->initialState->columns, LEFT_HEEL) ||
						vectorIncludes(action->initialState->columns, RIGHT_TOE)))
					cost += 500;
				break;
			default:
				break;
			}
		}
	}

	bool movedLeft =
	  setContains(action->resultState->movedFeet, LEFT_HEEL) ||
	  setContains(action->resultState->movedFeet, LEFT_TOE);
	bool movedRight =
	  setContains(action->resultState->movedFeet, RIGHT_HEEL) ||
	  setContains(action->resultState->movedFeet, RIGHT_TOE);

	bool didJump =
	  ((setContains(action->initialState->movedFeet, LEFT_HEEL) &&
		!setContains(action->initialState->holdFeet, LEFT_HEEL)) ||
		(setContains(action->initialState->movedFeet, LEFT_TOE) &&
		  !setContains(action->initialState->holdFeet, LEFT_TOE))) &&
	  ((setContains(action->initialState->movedFeet, RIGHT_HEEL) &&
		!setContains(action->initialState->holdFeet, RIGHT_HEEL)) ||
		(setContains(action->initialState->movedFeet, RIGHT_TOE) &&
		  !setContains(action->initialState->holdFeet, RIGHT_TOE)));

	// jacks don't matter if you did a jump before

	bool jackedLeft = false;
	bool jackedRight = false;

	if (!didJump) {
	  if (leftHeel != -1 && movedLeft) {
		if (
		  action->initialState->columns[leftHeel] == LEFT_HEEL &&
		  !setContains(action->resultState->holdFeet, LEFT_HEEL) &&
		  ((setContains(action->initialState->movedFeet, LEFT_HEEL) &&
			!setContains(action->initialState->holdFeet, LEFT_HEEL)) ||
			(setContains(action->initialState->movedFeet, LEFT_TOE) &&
			  !setContains(action->initialState->holdFeet, LEFT_TOE)))
		)
		  jackedLeft = true;
		if (
		  action->initialState->columns[leftToe] == LEFT_TOE &&
		  !setContains(action->resultState->holdFeet, LEFT_TOE) &&
		  ((setContains(action->initialState->movedFeet, LEFT_HEEL) &&
			!setContains(action->initialState->holdFeet, LEFT_HEEL)) ||
			(setContains(action->initialState->movedFeet, LEFT_TOE) &&
			  !setContains(action->initialState->holdFeet, LEFT_TOE)))
		)
		  jackedLeft = true;
	  }

	  if (rightHeel != -1 && movedRight) {
		if (
		  action->initialState->columns[rightHeel] == RIGHT_HEEL &&
		  !setContains(action->resultState->holdFeet, RIGHT_HEEL) &&
		  ((setContains(action->initialState->movedFeet, RIGHT_HEEL) &&
			!setContains(action->initialState->holdFeet, RIGHT_HEEL)) ||
			(setContains(action->initialState->movedFeet, RIGHT_TOE) &&
			  !setContains(action->initialState->holdFeet, RIGHT_TOE)))
		)
		  jackedRight = true;
		if (
		  action->initialState->columns[rightToe] == RIGHT_TOE &&
		  !setContains(action->resultState->holdFeet, RIGHT_TOE) &&
		  ((setContains(action->initialState->movedFeet, RIGHT_HEEL) &&
			!setContains(action->initialState->holdFeet, RIGHT_HEEL)) ||
			(setContains(action->initialState->movedFeet, RIGHT_TOE) &&
			  !setContains(action->initialState->holdFeet, RIGHT_TOE)))
		)
		  jackedRight = true;
	  }
	}

	// Doublestep weighting doesn't apply if you just did a jump or a jack

	if (
	  movedLeft != movedRight &&
	  (movedLeft || movedRight) &&
	  action->resultState->holdFeet.size() == 0 &&
	  !didJump
	) {
	  bool doublestepped = false;

	  if (
		movedLeft &&
		!jackedLeft &&
		((setContains(action->initialState->movedFeet, LEFT_HEEL) &&
		  !setContains(action->initialState->holdFeet, LEFT_HEEL)) ||
		  (setContains(action->initialState->movedFeet, LEFT_TOE) &&
			!setContains(action->initialState->holdFeet, LEFT_TOE)))
	  ) {
		doublestepped = true;
	  }
	  if (
		movedRight &&
		!jackedRight &&
		((setContains(action->initialState->movedFeet, RIGHT_HEEL) &&
		  !setContains(action->initialState->holdFeet, RIGHT_HEEL)) ||
		  (setContains(action->initialState->movedFeet, RIGHT_TOE) &&
			!setContains(action->initialState->holdFeet, RIGHT_TOE)))
	  )
		doublestepped = true;


	  if (rowIndex - 1 > -1)
	  {
	  	StepParity::Row lastRow = rows[rowIndex - 1];
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

	  // Jack detection is wrong, it's detecting a jack even if another foot moved after it
	  /*if ((jackedLeft || jackedRight) && row_distance <= 12) {
					if (DoLogging||true) Console.WriteLine("[{0}->{1}] Penalty of 1000 for a fast jack given to {2} -> {3} with distance {4}", a.row, b.row, Stringify(a.panels), Stringify(newMovement.placement.panels), row_distance);
					newMovement.weighting += 1000;
				}*/

	  if (doublestepped) {
		cost += DOUBLESTEP;
	  }

	  if (
		jackedLeft &&
		setContains(action->resultState->movedFeet, LEFT_HEEL) &&
		setContains(action->resultState->movedFeet, LEFT_TOE)
	  ) {
		cost += BRACKETJACK;
	  }

	  if (
		jackedRight &&
		setContains(action->resultState->movedFeet, RIGHT_HEEL) &&
		setContains(action->resultState->movedFeet, RIGHT_TOE)
	  ) {
		cost += BRACKETJACK;
	  }
	}

	if (
		movedLeft &&
		movedRight &&
		std::count_if(row.notes.begin(), row.notes.end(), [](StepParity::IntermediateNoteData note)
					  { return note.type != TapNoteType_Empty; }) >= 2)
	{
		cost += JUMP / elapsedTime;
	}

	float endLeftHeel = -1;
	float endLeftToe = -1;
	float endRightHeel = -1;
	float endRightToe = -1;

	for (unsigned long i = 0; i < combinedColumns.size(); i++) {
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
	 indexOf(action->initialState->columns, LEFT_HEEL),
	 indexOf(action->initialState->columns, LEFT_TOE)
	);
	StagePoint previousRightPos = averagePoint(
	 indexOf(action->initialState->columns, RIGHT_HEEL),
	 indexOf(action->initialState->columns, RIGHT_TOE)
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

	// if (
	//   leftPos.y < rightPos.y &&
	//   previousLeftPos.y < previousRightPos.y &&
	//   rightPos.x > leftPos.x &&
	//   previousRightPos.x < previousLeftPos.x
	// ) {
	//   cost += SPIN
	// }

	// Footswitch penalty

	// ignore footswitch with 24 or less distance (8th note); penalise slower footswitches based on distance
	if (elapsedTime >= 0.25) {
	  // footswitching has no penalty if there's a mine nearby
	  if (
		  std::all_of(row.mines.begin(), row.mines.end(), [](int mine) { return mine == 0; }) &&
		  std::all_of(row.fakeMines.begin(), row.fakeMines.end(), [](int mine) { return mine == 0; })
		  )
	  {
		  float timeScaled = elapsedTime - 0.25;

		  for (unsigned long i = 0; i < combinedColumns.size(); i++)
		  {
			  if (
				  action->initialState->columns[i] == NONE ||
				  action->resultState->columns[i] == NONE)
				  continue;

			  if (
				  action->initialState->columns[i] != action->resultState->columns[i] &&
				  !setContains(action->resultState->movedFeet, action->initialState->columns[i]))
			  {
				  cost += pow(timeScaled / 2.0, 2) * FOOTSWITCH;
				  break;
			  }
		  }
	  }
	}

	if (
	  action->initialState->columns[0] != action->resultState->columns[0] &&
	  action->resultState->columns[0] != NONE &&
	  action->initialState->columns[0] != NONE &&
	  !setContains(action->resultState->movedFeet, action->initialState->columns[0])
	) {
	  cost += SIDESWITCH;
	}

	if (
	  action->initialState->columns[3] != action->resultState->columns[3] &&
	  action->resultState->columns[3] != NONE &&
	  action->initialState->columns[3] != NONE &&
	  !setContains(action->resultState->movedFeet, action->initialState->columns[3])
	) {
	  cost += SIDESWITCH;
	}

	// add penalty if jacked

	if (
		(jackedLeft || jackedRight) &&
		(std::any_of(row.mines.begin(), row.mines.end(), [](int mine)
					 { return mine != 0;  }) ||
		 std::any_of(row.fakeMines.begin(), row.fakeMines.end(), [](int mine)
					 { return mine != 0;  })))
	{
		cost += MISSED_FOOTSWITCH;
	}

	// To do: small weighting for swapping heel with toe or toe with heel (both add up)

	// To do: huge weighting for having foot direction opposite of eachother (can't twist one leg 180 degrees)

	// weighting for jacking two notes too close to eachother
	if (elapsedTime <= 0.15 && movedLeft != movedRight) {
	  float timeScaled = 0.15 - elapsedTime;
	  if (jackedLeft || jackedRight) {
		cost += (1 / timeScaled - 1 / 0.15) * JACK;
	  }
	}

	// To do: weighting for moving a foot a far distance in a fast time
	for (StepParity::Foot foot: action->resultState->movedFeet) {
	  int idxFoot =indexOf(action->initialState->columns, foot);
	  if (idxFoot == -1) continue;
	  cost +=
		(sqrt(
		  getDistanceSq(
			layout[idxFoot],
			layout[indexOf(action->resultState->columns, foot)]
		  )
		) *
		  DISTANCE) /
		elapsedTime;
	}

	action->cost = cost;
	action->resultState->columns = combinedColumns;

	if (costCache.find(rowIndex) == costCache.end())
	{
		costCache[rowIndex] = std::map<RString, float>();
	}
	exploreCounter++;
	// costCache[rowIndex][cacheKey] = cost;
}
