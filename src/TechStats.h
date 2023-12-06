#ifndef TECH_STATS_H
#define TECH_STATS_H

#include "GameConstantsAndTypes.h"

enum Foot
{
	Foot_None = -1,
	Foot_Left = 0,
	Foot_Right
};

typedef int StepDirection;
const StepDirection StepDirection_None = 0;
const StepDirection StepDirection_Left = 1;
const StepDirection StepDirection_Down = 2;
const StepDirection StepDirection_Up = 4;
const StepDirection StepDirection_Right = 8;

const StepDirection StepDirection_LR = StepDirection_Left | StepDirection_Right;
const StepDirection StepDirection_UD = StepDirection_Up | StepDirection_Down;
const StepDirection StepDirection_LD = StepDirection_Left | StepDirection_Down;
const StepDirection StepDirection_LU = StepDirection_Left | StepDirection_Up;
const StepDirection StepDirection_RD = StepDirection_Right | StepDirection_Down;
const StepDirection StepDirection_RU = StepDirection_Right | StepDirection_Up;

inline int TrackIntToStepDirection(int track)
{
	return ((track > 3 || track < 0) ? -1 : pow(2, track) );
}

inline bool IsSingleStep(StepDirection s)
{
	return ((s == StepDirection_Left) || (s == StepDirection_Down) || (s == StepDirection_Up) || (s == StepDirection_Right));
}

inline bool IsJump(StepDirection s)
{
	return ( (s == StepDirection_LR) || (s == StepDirection_UD) || (s == StepDirection_LD) || (s == StepDirection_LU) || (s == StepDirection_RD) || (s == StepDirection_RU) );
}

inline bool StepContainsStep(StepDirection instep, StepDirection step)
{
	return (instep & step) == step;
}

inline Foot SwitchFeet(Foot f) 
{
	return ((f == Foot_None || f == Foot_Left) ? Foot_Right : Foot_Left);
}

/** @brief Technical statistics */
struct TechStats
{
public:
	int crossovers; /** Number of crossovers in song */
	int footswitches; /** Number of footswitches in song */
	int sideswitches; /** Number of sideswitches in song */
	int jacks; /** Number of jacks in song */
	int brackets; /** Number of brackets in song */
	float peakNps; /** Peak notes-per-second in song */
	std::vector<int> notesPerMeasure; /** Number of notes for each measure */
	std::vector<float> NpsPerMeasure; /** notes-per-second for each measure */

	TechStats() {
		crossovers = 0;
		footswitches = 0;
		sideswitches = 0;
		jacks = 0;
		brackets = 0;
		peakNps = 0;
	}

	RString stringDescription();
};

struct TechStatsCounter
{

	bool wasLastStreamFlipped;
	bool anyStepsSinceLastCommitStream;
	bool justBracketed;
	bool lastFlip;

	std::vector<bool> stepsLr;

	Foot lastFoot;
	Foot trueLastFoot;

	StepDirection lastStep;
	StepDirection lastRepeatedFoot;
	StepDirection lastArrowL;
	StepDirection lastArrowR;

	StepDirection trueLastArrowL;
	StepDirection trueLastArrowR;

	int recursionCount;
	TechStatsCounter()
	{
		lastFoot = Foot_Left;
		wasLastStreamFlipped = false;
		lastStep = StepDirection_None;
		lastRepeatedFoot = StepDirection_None;

		anyStepsSinceLastCommitStream = false;
		
		lastArrowL = StepDirection_None;
		lastArrowR = StepDirection_None;
		
		trueLastArrowL = StepDirection_None;
		trueLastArrowR = StepDirection_None;

		trueLastFoot = Foot_None;
		justBracketed = false;
		lastFlip = false;

		recursionCount = 0;
	}
};

namespace TechStatsCalculator
{
	void UpdateTechStats(TechStats &stats, TechStatsCounter &counter, StepDirection currentStep);

	void CommitStream(TechStats &stats, TechStatsCounter &counter, Foot tieBreaker);
};
// TODO: This data needs to be available to LUA

#endif