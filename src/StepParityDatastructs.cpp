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
bool StageLayout::bracketCheck(int column1, int column2)
{
	float dist =getDistance(column1, column2);
	return (dist * dist) <= 2;
}

bool StageLayout::isSideArrow(int column)
{
	return std::find(sideArrows.begin(), sideArrows.end(), column) != sideArrows.end();
}

bool StageLayout::isUpArrow(int column)
{
	return std::find(upArrows.begin(), upArrows.end(), column) != upArrows.end();
}

bool StageLayout::isDownArrow(int column)
{
	return std::find(downArrows.begin(), downArrows.end(), column) != downArrows.end();
}

float StageLayout::getDistanceSq(int c1, int c2)
{
	return getDistanceSq(columns[c1], columns[c2]);
}

float StageLayout::getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2)
{
	return (p1.y - p2.y) * (p1.y - p2.y) + (p1.x - p2.x) * (p1.x - p2.x);
}

float StageLayout::getDistance(int leftIndex, int rightIndex) {
	if(leftIndex == INVALID_COLUMN || rightIndex == INVALID_COLUMN)
	{
		return 0;
	}
	int idx = leftIndex * columnCount + rightIndex;
	return distances[idx];
}

float StageLayout::getXDifference(int leftIndex, int rightIndex) {
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

float StageLayout::getYDifference(int leftIndex, int rightIndex) {
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

float StageLayout::getXFacingPenalty(int leftIndex, int rightIndex) {
	if(leftIndex == INVALID_COLUMN || rightIndex == INVALID_COLUMN)
	{
		return 0;
	}
	
	int idx = leftIndex * columnCount + rightIndex;
	return facingXPenalties[idx];
}

float StageLayout::getYFacingPenalty(int leftIndex, int rightIndex) {
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

StagePoint StageLayout::averagePoint(int leftIndex, int rightIndex) {
	if (leftIndex == INVALID_COLUMN && rightIndex == INVALID_COLUMN) return { 0,0 };
	if (leftIndex == INVALID_COLUMN) return columns[rightIndex];
	if (rightIndex == INVALID_COLUMN) return columns[leftIndex];
//	return {
//	  (columns[leftIndex].x + columns[rightIndex].x) / 2.0f,
//	  (columns[leftIndex].y + columns[rightIndex].y) / 2.0f,
//	};
	int idx = leftIndex * columnCount + rightIndex;
	return avgPoints[idx];
}

float StageLayout::getPlayerAngle(int c1, int c2)
{
	return getPlayerAngle(columns[c1], columns[c2]);
}
float StageLayout::getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right)
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
