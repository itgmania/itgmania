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
	StagePoint p1 = columns[column1];
	StagePoint p2 = columns[column2];
	return getDistanceSq(p1, p2) <= 2;
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

StagePoint StageLayout::averagePoint(int leftIndex, int rightIndex) {
	if (leftIndex == -1 && rightIndex == -1) return { 0,0 };
	if (leftIndex == -1) return columns[rightIndex];
	if (rightIndex == -1) return columns[leftIndex];
	return {
	  (columns[leftIndex].x + columns[rightIndex].x) / 2.0f,
	  (columns[leftIndex].y + columns[rightIndex].y) / 2.0f,
	};
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
	holdTails == other.holdTails &&
	mines == other.mines &&
	fakeMines == other.fakeMines &&
	columns == other.columns &&
	whereTheFeetAre == other.whereTheFeetAre;
	
}

bool Row::operator!=(const Row& other) const
{
	return !operator==(other);
}
