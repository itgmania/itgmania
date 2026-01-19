#include "global.h"
#include "StepParityDatastructs.h"

using namespace StepParity;


bool State::operator==(const State &other) const
{
   return columns == other.columns &&
	combinedColumns == other.combinedColumns &&
   movedFeet == other.movedFeet &&
   holdFeet == other.holdFeet;
}

// StageLayout
bool StageLayout::bracketCheck(int column1, int column2) const
{
	float dist =getDistance(column1, column2);
	return (dist * dist) <= 2;
}

bool StageLayout::isSideArrow(int column) const
{
	return std::find(sideArrows.begin(), sideArrows.end(), column) != sideArrows.end();
}

bool StageLayout::isUpArrow(int column) const
{
	return std::find(upArrows.begin(), upArrows.end(), column) != upArrows.end();
}

bool StageLayout::isDownArrow(int column) const
{
	return std::find(downArrows.begin(), downArrows.end(), column) != downArrows.end();
}

float StageLayout::getDistanceSq(int c1, int c2) const
{
	return getDistanceSq(columns[c1], columns[c2]);
}

float StageLayout::getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2) const
{
	return (p1.y - p2.y) * (p1.y - p2.y) + (p1.x - p2.x) * (p1.x - p2.x);
}

float StageLayout::getDistance(int leftIndex, int rightIndex) const
{
	if(leftIndex == INVALID_COLUMN || rightIndex == INVALID_COLUMN)
	{
		return 0;
	}
	int idx = leftIndex * columnCount + rightIndex;
	return distances[idx];
}

float StageLayout::getXDifference(int leftIndex, int rightIndex) const
{
	if (leftIndex == rightIndex) return 0;
	float dx = columns[rightIndex].x - columns[leftIndex].x;
	float dy = columns[rightIndex].y - columns[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dx /= distance;

	bool negative = dx <= 0;

	dx = pow(dx, 4);

	if (negative) dx = -dx;

	return dx;
}

float StageLayout::getYDifference(int leftIndex, int rightIndex) const
{
	if (leftIndex == rightIndex) return 0;
	float dx = columns[rightIndex].x - columns[leftIndex].x;
	float dy = columns[rightIndex].y - columns[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dy /= distance;

	bool negative = dy <= 0;

	dy = pow(dy, 4);

	if (negative) dy = -dy;

	return dy;
}

float StageLayout::getXFacingPenalty(int leftIndex, int rightIndex) const
{
	if(leftIndex == INVALID_COLUMN || rightIndex == INVALID_COLUMN)
	{
		return 0;
	}
	
	int idx = leftIndex * columnCount + rightIndex;
	return facingXPenalties[idx];
}

float StageLayout::getYFacingPenalty(int leftIndex, int rightIndex) const
{
	if(leftIndex == INVALID_COLUMN || rightIndex == INVALID_COLUMN)
	{
		return 0;
	}
	
	int idx = leftIndex * columnCount + rightIndex;
	return facingYPenalties[idx];
}

float facing_penalty(float v) {
	float base = -1 * std::min(v, 0.0f);
	float result = pow(base, 1.8) * 100;
	return result;
}

void StageLayout::preCalculateStuff()
{
	avgPoints.resize(columnCount * columnCount);
	distances.resize(columnCount * columnCount);
	facingXPenalties.resize(columnCount * columnCount);
	facingYPenalties.resize(columnCount * columnCount);
	
	for(int left = 0; left < columnCount; left++)
	{
		for(int right = 0; right < columnCount; right++)
		{
			int idx = left * columnCount + right;
			
			StagePoint avgPoint = {
				(columns[left].x + columns[right].x) / 2.0f,
				(columns[left].y + columns[right].y) / 2.0f,
			};
			avgPoints[idx] = avgPoint;
			
			float dx = columns[left].x - columns[right].x;
			float dy = columns[left].y - columns[right].y;
			float distSq = (dx * dx) + (dy * dy);
			float dist = sqrt(distSq);
			distances[idx] = dist;
			
			if(dist == 0)
			{
				facingXPenalties[idx] = 0;
				facingYPenalties[idx] = 0;
			}
			else
			{
				float normalized_dx = dx / dist;
				float normalized_dy = dy / dist;
				float xm = getXDifference(left, right);
				float ym = getYDifference(left, right);
				
				facingXPenalties[idx] = std::max(0.0f, facing_penalty(xm));
				facingYPenalties[idx] = std::max(0.0f, facing_penalty(ym));
			}
		}
	}
}

// Counts the number of ones in the binary representation
// of the given `x`.
inline int popcount(unsigned int x) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_popcount(x);
#elif defined(_MSC_VER)
	#include <intrin.h>
	return __popcnt(x);
#else
	// Fallback implementation
	int count = 0;
	while (x) {
		x &= x - 1;  // Clear the lowest set bit
		count++;
	}
	return count;
#endif
}

void StageLayout::preGeneratePermutations()
{
	for(unsigned int i = 0; i < pow(2, columnCount); i++)
	{
		int bits = popcount(i);
		if(bits > 4)
		{
			continue;
		}
		FootPlacement blankColumns(columnCount, NONE);
		
		std::vector<FootPlacement> placements = PermuteFootPlacements(i, blankColumns, 0);
		permuteCache[i] = std::move(placements);
	}
}

std::vector<FootPlacement> StageLayout::PermuteFootPlacements(unsigned int mask, FootPlacement columns, unsigned long column)
{
	// MV: This is re-defined here because I was running into
	// issues with StepParity::FEET being empty within this context?
	// I don't understand C++ header initialization stuff
	std::vector<StepParity::Foot> FEET = {LEFT_HEEL, LEFT_TOE, RIGHT_HEEL, RIGHT_TOE};
	
	// If column >= columns.size(), we've reached the end of the row.
	// Perform some final validation before returning the contents of columns
	if (column >= columns.size())
	{
		int leftHeelIndex = StepParity::INVALID_COLUMN;
		int leftToeIndex = StepParity::INVALID_COLUMN;
		int rightHeelIndex = StepParity::INVALID_COLUMN;
		int rightToeIndex = StepParity::INVALID_COLUMN;
		
		for (unsigned long i = 0; i < columns.size(); i++)
		{
			if (columns[i] == NONE)
			{
				continue;
			}
			if (columns[i] == LEFT_HEEL)
			{
				leftHeelIndex = i;
			}
			if (columns[i] == LEFT_TOE)
			{
				leftToeIndex = i;
			}
			if (columns[i] == RIGHT_HEEL)
			{
				rightHeelIndex = i;
			}
			if (columns[i] == RIGHT_TOE)
			{
				rightToeIndex = i;
			}
		}
		
		// Filter out actually invalid combinations:
		// - We don't want permutations where the toe is on an arrow, but not the heel
		// - We don't want impossible brackets (eg you can't bracket up and down)
		if (
			(leftHeelIndex == StepParity::INVALID_COLUMN && leftToeIndex != StepParity::INVALID_COLUMN) ||
			(rightHeelIndex == StepParity::INVALID_COLUMN && rightToeIndex != StepParity::INVALID_COLUMN))
		{
			return std::vector<FootPlacement>();
		}
		if (leftHeelIndex != StepParity::INVALID_COLUMN && leftToeIndex != StepParity::INVALID_COLUMN)
		{
			if (!bracketCheck(leftHeelIndex, leftToeIndex))
			{
				return std::vector<FootPlacement>();
			}
		}
		if (rightHeelIndex != StepParity::INVALID_COLUMN && rightToeIndex != StepParity::INVALID_COLUMN)
		{
			if (!bracketCheck(rightHeelIndex, rightToeIndex))
			{
				return std::vector<FootPlacement>();
			}
		}
		return {columns};
	}
		// If this column has a valid tap/hold head, or is actively holding a note,
		// iterate through values of StepParity::Foot. For each foot part, check that
		// it's not already present in columns, and if not, create a copy of columns,
		// and set the current foot part to the current column.
		// Then pass it to PermuteFootPlacements() and increment the column index.
		// Collect each permutationm, and then return all of them.
		//
		// The `ignoreHolds` flag is used as a workaround for situations where
		// we can't find a valid foot placement that allows us to continue the holds
		// (BREACH PROTOCOL doubles has  a row 0311 1000 which isn't bracketable
		// while still holding p1 down)
		std::vector<FootPlacement> permutations;
		bool active = (mask & (0x1 << column)) != 0;
	
		if (active)
		{
			for (StepParity::Foot foot: FEET) {
				if(std::find(columns.begin(), columns.end(), foot) != columns.end())
				{
					continue;
				}

				FootPlacement newColumns = columns;

				newColumns[column] = foot;
				std::vector<FootPlacement> p = PermuteFootPlacements(mask, newColumns, column + 1);
				permutations.insert(permutations.end(), p.begin(), p.end());
			}
			  return permutations;
		}
		// If the current column doesn't have any taps or holds,
		// then we don't need to generate any permutations for it.
		// Return the contents of calling PermuteFootPlacements() for the next column.
		return PermuteFootPlacements(mask, columns, column + 1);
}

StagePoint StageLayout::averagePoint(int leftIndex, int rightIndex) const {
	if (leftIndex == INVALID_COLUMN && rightIndex == INVALID_COLUMN) return { 0,0 };
	if (leftIndex == INVALID_COLUMN) return columns[rightIndex];
	if (rightIndex == INVALID_COLUMN) return columns[leftIndex];
	int idx = leftIndex * columnCount + rightIndex;
	return avgPoints[idx];
}

float StageLayout::getPlayerAngle(int c1, int c2)const
{
	return getPlayerAngle(columns[c1], columns[c2]);
}
float StageLayout::getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right) const
{
	float x1 = right.x - left.x;
	float y1 = right.y - left.y;
	float x2 = 1;
	float y2 = 0;
	float dot = x1 * x2 + y1 * y2;
	float det = x1 * y2 - y1 * x2;
	return atan2f(det, dot);
}

// Row
void Row::setFootPlacement(const std::vector<Foot> & footPlacement)
{
	for (int c = 0; c < columnCount; c++) {
		if(notes[c].type != TapNoteType_Empty) {
			notes[c].parity = footPlacement[c];
			columns[c] = footPlacement[c];
			whereTheFeetAre[footPlacement[c]] = c;
			noteCount += 1;
		}
	}
}

bool Row::operator==(const Row& other) const
{
	return second == other.second &&
	beat == other.beat &&
	rowIndex == other.rowIndex &&
	columnCount == other.columnCount &&
	noteCount == other.noteCount &&
	mines == other.mines &&
	fakeMines == other.fakeMines &&
	columns == other.columns &&
	whereTheFeetAre == other.whereTheFeetAre;
	
}

bool Row::operator!=(const Row& other) const
{
	return !operator==(other);
}
