#ifndef STEP_PARITY_GENERATOR_H
#define STEP_PARITY_GENERATOR_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "StepParityDatastructs.h"
#include <queue>
#include <unordered_map>
#include "json/json.h"

namespace StepParity {
	
	/// @brief This class handles most of the work for generating step parities for a step chart.
	class StepParityGenerator 
	{
	private:
		StageLayout layout;
		std::map < int, std::vector<std::vector<StepParity::Foot>>> permuteCache;

	public:
		StepParityGraph graph;
		std::vector<Row> rows;
		std::vector<int> nodes_for_rows;
		int columnCount;
		
		/// @brief Analyzes the given NoteData to generate a vector of StepParity::Rows, with each step annotated with
		/// a foot placement.
		/// @param in The NoteData to analyze
		/// @param stepsTypeStr StepsType, currently only supports "dance-single"
		void analyzeNoteData(const NoteData &in, StepsType stepsType);

		/// @brief Analyzes the given graph to find the least costly path from the beginnning to the end of the stepchart.
		/// Sets the `parity` for the relevant notes of each row in rows.
		void analyzeGraph();

		/// @brief Generates a StepParityGraph from the given vector of Rows.
		/// The graph inserts two additional nodes: one that represent the beginning of the song, before the first note,
		/// and one that represents the end of the song, after the final note.
		void buildStateGraph();

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

		/// @brief Computes the "cheapest" path through the    given graph.
		/// This relies on the fact that the nodes stored in the graph are topologically sorted (that is, all
		/// of the nodes are ordered in such a way that each node comes before all the nodes it points to.)
		/// This allows us to find the cheapest path in a single pass.
		/// The resulting path includes one node for each row of the stepchart represented by the graph.
		/// Returns a vector of node indices, which can be mapped back to the cheapest state for each row.
		/// @return A vector of node indices, making up the cheapest path through the step chart.
		std::vector<int> computeCheapestPath();
		
		
		void CreateIntermediateNoteData(const NoteData &in, std::vector<IntermediateNoteData> &out);
		void CreateRows(const NoteData &in);
		Row CreateRow(RowCounter &counter);
		int getPermuteCacheKey(const Row &row);
		bool bracketCheck(int column1, int column2);
		float getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2);
		Json::Value SMEditorParityJson();
	};
};

#endif
