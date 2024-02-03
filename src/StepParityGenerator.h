#ifndef STEP_PARITY_GENERATOR_H
#define STEP_PARITY_GENERATOR_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "StepParityDatastructs.h"
#include <queue>
#include <unordered_map>


namespace StepParity {
	
	/// @brief This class handles most of the work for generating step parities for a step chart.
	class StepParityGenerator 
	{
	private:
		StageLayout layout;
		std::map < int, std::vector<std::vector<StepParity::Foot>>> permuteCache;

	public:
		/// @brief Analyzes the given NoteData to generate a vector of StepParity::Rows, with each step annotated with
		/// a foot placement.
		/// @param in The NoteData to analyze
		/// @param out The destination for generated Rows
		/// @param stepsTypeStr StepsType, currently only supports "dance-single"
		void analyzeNoteData(const NoteData &in, std::vector<StepParity::Row> &out, RString stepsTypeStr);

		/// @brief Analyzes the given graph to find the least costly path from the beginnning to the end of the stepchart.
		/// Sets the `parity` for the relevant notes of each row in rows.
		/// @param rows The destination for the parity data.
		/// @param graph The graph to analyze
		/// @param columnCount Number of columns
		void analyzeGraph(std::vector<Row> &rows, const StepParityGraph & graph, int columnCount);

		/// @brief Generates a StepParityGraph from the given vector of Rows.
		/// @param rows The rows to convert into a graph.
		/// @param graph Destination for the generated graph.
		/// @param columnCount Number of columns
		void buildStateGraph(std::vector<Row> &rows, StepParityGraph & graph, int columnCount);

		/// @brief Creates a new State, which is the result of moving from the given initialState 
		/// to the steps of the given row with the given foot placements in columns.
		/// @param initialState The state of the player prior to the next row
		/// @param row The next row for the resulting state
		/// @param columns The foot placement for the resulting state
		/// @return The resulting state
		State initResultState(State &initialState, Row &row, const FootPlacement &columns);

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

		/// @brief Computes the "cheapest" path through the	 given graph from start to end. 
		/// The resulting path includes one node for each row of the stepchart represented by the graph.
		/// Returns a vector of node indices, which can be mapped back to the cheapest state for each row. 
		/// @param graph The graph to be traversed
		/// @param start The index of the starting node
		/// @param end The index of the destination node
		/// @return A vector of node indices, making up the cheapest path from start to end.
		std::vector<int> computeCheapestPath(const StepParity::StepParityGraph &graph, int start, int end);
		
		
		void CreateIntermediateNoteData(const NoteData &in, std::vector<IntermediateNoteData> &out);
		void CreateRows(const NoteData &in, std::vector<Row> &out);
		Row CreateRow(RowCounter &counter, int columnCount);
		int getPermuteCacheKey(const Row &row);
		bool bracketCheck(int column1, int column2);
		float getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2);
	};
};

#endif