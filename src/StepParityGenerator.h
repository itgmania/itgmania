#ifndef STEP_PARITY_GENERATOR_H
#define STEP_PARITY_GENERATOR_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "StepParityDatastructs.h"
#include <queue>
#include <unordered_map>
#include "json/json.h"

namespace StepParity {
	
	const std::map<StepsType, StageLayout> Layouts = {
		{StepsType_dance_single, StageLayout(StepsType_dance_single, {
			{0, 1},  // Left
			{1, 0},  // Down
			{1, 2},  // Up
			{2, 1}   // Right
		}, {2}, {1}, {0, 3})},
		{StepsType_dance_double, StageLayout(StepsType_dance_double, {
			{0, 1},  // P1 Left
			{1, 0},  // P1 Down
			{1, 2},  // P1 Up
			{2, 1},  // P1 Right
			
			{3, 1},  // P2 Left
			{4, 0},  // P2 Down
			{4, 2},  // P2 Up
			{5, 1}   // P2 Right
		}, {2, 6}, {1, 5}, {0, 3, 4, 7})}
	};
	
	/// @brief This class handles most of the work for generating step parities for a step chart.
	class StepParityGenerator 
	{
	private:
		StageLayout layout;
		std::unordered_map < int, std::vector<std::vector<StepParity::Foot>>> permuteCache;
		std::unordered_map <std::uint64_t, StepParity::State*> stateCache;
		std::vector<StepParity::StepParityNode*> nodes;
		StepParity::State * beginningState = nullptr;
		StepParity::StepParityNode * startNode = nullptr;
		StepParity::State * endingState = nullptr;
		StepParity::StepParityNode * endNode = nullptr;
		
	public:
		std::vector<Row> rows;
		std::vector<int> nodes_for_rows;
		int columnCount;
		
		StepParityGenerator(const StageLayout & l) : layout(l) {
			
		}
		
		~StepParityGenerator()
		{
			for(auto s : stateCache)
			{
				delete s.second;
			}
			for(auto n: nodes)
			{
				delete n;
			}
			if(beginningState != nullptr)
			{
				delete beginningState;
			}
			
			if(endingState != nullptr)
			{
				delete endingState;
			}
		}
		/// @brief Analyzes the given NoteData to generate a vector of StepParity::Rows, with each step annotated with
		/// a foot placement.
		/// @param in The NoteData to analyze
		void analyzeNoteData(const NoteData &in);

		/// @brief Analyzes the given graph to find the least costly path from the beginnning to the end of the stepchart.
		/// Sets the `parity` for the relevant notes of each row in rows.
		void analyzeGraph();

		/// @brief Generates a StepParityGraph from the given vector of Rows.
		/// The graph inserts two additional nodes: one that represent the beginning of the song, before the first note,
		/// and one that represents the end of the song, after the final note.
		void buildStateGraph();

		void addStateToGraph(State * resultState, StepParityNode * initialNode, Row & row, std::vector<StepParityNode *> &existingNodesForThisRow, float cost);
		/// @brief Creates a new State, which is the result of moving from the given initialState
		/// to the steps of the given row with the given foot placements in columns.
		/// @param initialState The state of the player prior to the next row
		/// @param row The next row for the resulting state
		/// @param columns The foot placement for the resulting state
		/// @return The resulting state
		State * initResultState(State * initialState, Row &row, const FootPlacement &columns);

		void mergeInitialAndResultPosition(State * initialState, State * resultState, int columnCount);
		
		/// @brief Returns a pointer to a vector of foot possible foot placements for the given row.
		/// Utilizes the permuteCache to re-use vectors. The returned pointer points to a vector within the permuteCache.
		/// @param row The row to calculate foot placement permutations for.
		/// @return A pointer to a vector of foot placements.
		std::vector<FootPlacement>* getFootPlacementPermutations(const Row &row);

		/// @brief A recursive function that generates a vector of possible foot placements for the given row.
		/// This function should not be used directly, instead use getFootPlacementPermutations().
		/// @param row
		/// @param columns
		/// @param column
		/// @return
		std::vector<FootPlacement> PermuteFootPlacements(const Row &row, FootPlacement columns, unsigned long column);

		/// @brief Computes the "cheapest" path through the given graph.
		/// This relies on the fact that the nodes stored in the graph are topologically sorted (that is, all
		/// of the nodes are ordered in such a way that each node comes before all the nodes it points to.)
		/// This allows us to find the cheapest path in a single pass.
		/// The resulting path includes one node for each row of the stepchart represented by the graph.
		/// Returns a vector of node indices, which can be mapped back to the cheapest state for each row.
		/// @return A vector of node indices, making up the cheapest path through the step chart.
		std::vector<int> computeCheapestPath();
		
		/// @brief Converts NoteData into an intermediate form that's a little more convenient
		/// to work with when creating rows.
		void CreateIntermediateNoteData(const NoteData &in, std::vector<IntermediateNoteData> &out);
		void CreateRows(const NoteData &in);
		void AddRow(RowCounter &counter);
		Row CreateRow(RowCounter &counter);
		int getPermuteCacheKey(const Row &row);
		std::uint64_t getStateCacheKey(State * state);
		StepParityNode * addNode(State *state, float second, int rowIndex);
		void addEdge(StepParityNode* from, StepParityNode* to, float cost);
	};
};

#endif
