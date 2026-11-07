#ifndef STEP_PARITY_GENERATOR_H
#define STEP_PARITY_GENERATOR_H

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "NoteData.h"
#include "StepParityDatastructs.h"
#include "TimingData.h"

namespace StepParity {

/// @brief This class handles most of the work for generating step parities for
/// a step chart.
class StepParityGenerator {
 private:
  const StageLayout* layout_;
  TimingData* timing_;

  StepParity::State* beginning_state_ = nullptr;
  StepParity::StepParityNode* start_node_ = nullptr;
  StepParity::State* ending_state_ = nullptr;
  StepParity::StepParityNode* end_node_ = nullptr;
  StepParity::State* tmp_state_ = nullptr;

  std::deque<StepParity::State> state_pool_;
  std::deque<StepParity::StepParityNode> node_pool_;

  StepParity::State* allocState() {
    state_pool_.emplace_back();
    return &state_pool_.back();
  }

 public:
  std::unordered_map<std::uint64_t, StepParity::State*> state_cache_;
  std::vector<StepParity::StepParityNode*> nodes_;
  std::vector<Row> rows_;
  std::vector<int> nodes_for_rows_;
  int column_count_;

  StepParityGenerator(const StageLayout* l, TimingData* t) : layout_(l) {
    timing_ = t;
  }
  /// @brief Analyzes the given NoteData to generate a vector of
  /// StepParity::Rows, with each step annotated with a foot placement.
  /// @param in The NoteData to analyze
  /// @return false if an error was encountered while analyzing the note data
  bool analyzeNoteData(const NoteData& in);

  /// @brief Analyzes the given graph to find the least costly path from the
  /// beginnning to the end of the stepchart. Sets the `parity` for the relevant
  /// notes of each row in rows.
  /// @return whether or not a valid graph was generated and analyzed
  bool analyzeGraph();

  /// @brief Generates a StepParityGraph from the given vector of Rows.
  /// The graph inserts two additional nodes: one that represent the beginning
  /// of the song, before the first note, and one that represents the end of the
  /// song, after the final note.
  void buildStateGraph();

  /// @brief Creates a new State, which is the result of moving from the given
  /// initialState to the steps of the given row with the given foot placements
  /// in columns.
  /// @param initialState The state of the player prior to the next row
  /// @param row The next row for the resulting state
  /// @param columns The foot placement for the resulting state
  /// @return The resulting state
  State* initResultState(
      State* initial_state, Row& row, const FootPlacement& columns);

  void mergeInitialAndResultPosition(
      State* initial_state, State* result_state, const FootPlacement& columns,
      int column_count);

  /// @brief Returns a pointer to a vector of foot possible foot placements for
  /// the given row. Utilizes the permuteCache to re-use vectors. The returned
  /// pointer points to a vector within the permuteCache.
  /// @param row The row to calculate foot placement permutations for.
  /// @return A pointer to a vector of foot placements.
  const std::vector<FootPlacement>* getFootPlacementPermutations(
      const Row& row);

  /// @brief Computes the "cheapest" path through the given graph.
  /// This relies on the fact that the nodes stored in the graph are
  /// topologically sorted (that is, all of the nodes are ordered in such a way
  /// that each node comes before all the nodes it points to.) This allows us to
  /// find the cheapest path in a single pass. The resulting path includes one
  /// node for each row of the stepchart represented by the graph. Returns a
  /// vector of node indices, which can be mapped back to the cheapest state for
  /// each row.
  /// @return A vector of node indices, making up the cheapest path through the
  /// step chart.
  std::vector<int> computeCheapestPath();

  void CreateRows(const NoteData& in);
  void AddRow(RowCounter& counter);
  Row CreateRow(RowCounter& counter);
  std::uint64_t getStateCacheKey(State* state);
  StepParityNode* addNode(State* state, float second, int row_index);
};
};  // namespace StepParity

#endif
