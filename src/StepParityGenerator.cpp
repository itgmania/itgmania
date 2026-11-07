#include "StepParityGenerator.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "RageLog.h"
#include "StepParityCost.h"
#include "StepParityDatastructs.h"

using namespace StepParity;

bool StepParityGenerator::analyzeNoteData(const NoteData& in) {
  column_count_ = in.GetNumTracks();

  CreateRows(in);

  if (rows_.size() == 0) {
    LOG->Trace("StepParityGenerator::analyze no rows, bailing out");
    return false;
  }
  buildStateGraph();
  return analyzeGraph();
}

bool StepParityGenerator::analyzeGraph() {
  nodes_for_rows_ = computeCheapestPath();
  if (nodes_for_rows_.size() != rows_.size()) {
    LOG->Info(
        "StepParityGenerator::analyzeGraph: nodes_for_rows should be the same "
        "length as rows! This means we probably generated an invalid graph fro "
        "this chart.");
    return false;
  }

  for (unsigned long i = 0; i < rows_.size(); i++) {
    StepParityNode* node = nodes_[nodes_for_rows_[i]];
    rows_[i].setFootPlacement(node->state_);
  }
  return true;
}

void StepParityGenerator::buildStateGraph() {
  // The first node of the graph is beginningState, which represents the time
  // before the first note (and so it's roIndex is considered -1)
  beginning_state_ = allocState();
  start_node_ = addNode(beginning_state_, rows_[0].second_ - 1, -1);

  std::vector<StepParityNode*> previous_nodes;
  std::vector<StepParityNode*> result_nodes;
  previous_nodes.push_back(start_node_);
  StepParityCost cost_calculator(layout_);

  // maps the current row's Nodes based on their State's cache key
  std::unordered_map<uint64_t, StepParityNode*> state_map;

  for (unsigned long i = 0; i < rows_.size(); i++) {
    state_map.clear();
    result_nodes.clear();

    Row& row = rows_[i];
    const std::vector<FootPlacement>* permutations =
        getFootPlacementPermutations(row);

    for (StepParityNode* initial_node : previous_nodes) {
      float elapsed_time = row.second_ - initial_node->second_;
      for (auto it = permutations->begin(); it != permutations->end(); it++) {
        State* result_state = initResultState(initial_node->state_, row, *it);
        Row* previous_row = i > 0 ? &rows_[i - 1] : nullptr;
        float cost = cost_calculator.getActionCost(
            initial_node->state_, result_state, row, previous_row, *it,
            elapsed_time);

        std::uint64_t key = getStateCacheKey(result_state);

        // Do we already have a Node in this row for this same resultState?
        // If so, and the totalCost is lower, update that node's totalCost
        // and previousNode.
        // Otherwise, add a new node and save it to stateMap
        auto itn = state_map.find(key);
        if (itn != state_map.end()) {
          StepParityNode* existing_node = itn->second;
          float total_cost = initial_node->total_cost_ + cost;
          if (total_cost < existing_node->total_cost_) {
            existing_node->total_cost_ = total_cost;
            existing_node->previous_node_ = initial_node;
          }
        } else {
          StepParityNode* new_node = addNode(result_state, row.second_, i);
          new_node->total_cost_ = initial_node->total_cost_ + cost;
          new_node->previous_node_ = initial_node;
          state_map[key] = new_node;
          result_nodes.push_back(new_node);
        }
      }
    }
    std::swap(previous_nodes, result_nodes);
  }

  // at this point, previousStates holds all of the states for the very last
  // row, which just get connected to the endState
  ending_state_ = allocState();
  end_node_ =
      addNode(ending_state_, rows_[rows_.size() - 1].second_ + 1, rows_.size());
  end_node_->total_cost_ = FLT_MAX;

  for (StepParityNode* node : previous_nodes) {
    if (node->total_cost_ < end_node_->total_cost_) {
      end_node_->total_cost_ = node->total_cost_;
      end_node_->previous_node_ = node;
    }
  }
}

State* StepParityGenerator::initResultState(
    State* initial_state, Row& row, const FootPlacement& columns) {
  if (tmp_state_ == nullptr) {
    tmp_state_ = allocState();
  }

  State* result_state = tmp_state_;

  // reset resultState

  result_state->moved_mask_ = 0;
  result_state->holding_mask_ = 0;
  result_state->combined_mask_ = 0;

  for (int i = 0; i < NUM_Foot; i++) {
    result_state->where_the_feet_are_[i] = INVALID_COLUMN;
    result_state->what_note_the_foot_is_hitting_[i] = INVALID_COLUMN;
    result_state->did_the_foot_move_[i] = false;
    result_state->is_the_foot_holding_[i] = false;
  }

  for (unsigned long i = 0; i < columns.size(); i++) {
    result_state->combined_columns_[i] = Foot_None;
  }

  // I tried to condense this, but kept getting the logic messed up
  for (unsigned long i = 0; i < columns.size(); i++) {
    if (columns[i] == Foot_None) {
      continue;
    }
    result_state->what_note_the_foot_is_hitting_[columns[i]] = i;

    if (row.holds_[i].type_ == TapNoteType_Empty) {
      result_state->did_the_foot_move_[columns[i]] = true;
      continue;
    }

    if (initial_state->combined_columns_[i] != columns[i]) {
      result_state->did_the_foot_move_[columns[i]] = true;
    }
  }

  for (unsigned long i = 0; i < columns.size(); i++) {
    if (columns[i] == Foot_None) {
      continue;
    }

    if (row.holds_[i].type_ != TapNoteType_Empty) {
      result_state->is_the_foot_holding_[columns[i]] = true;
    }

    uint16_t bit = 0x1 << i;
    uint16_t foot_mask = FOOT_MASKS[columns[i]];
    if ((row.hold_mask_ & bit) != 0) {
      result_state->holding_mask_ |= foot_mask;
    }
    if ((row.hold_mask_ & bit) == 0 ||
        (initial_state->combined_columns_[i] != columns[i])) {
      result_state->moved_mask_ |= foot_mask;
    }
  }

  mergeInitialAndResultPosition(
      initial_state, result_state, columns, (int)columns.size());

  std::uint64_t state_hash = getStateCacheKey(result_state);

  auto maybe_state = state_cache_.find(state_hash);

  if (maybe_state != state_cache_.end()) {
    State* cached_state = maybe_state->second;
    return maybe_state->second;
  }

  state_cache_.insert({state_hash, result_state});
  tmp_state_ = nullptr;
  return result_state;
}

// This merges the `combinedColumns` of initialState with the next `columns`,
// which fully represents the player's position on the dance stage. For example:
// initialState.combinedColumns = [L,0,0,R]
// columns = [0,L,0,0]
// resultState.combinedColumns = [0,L,0,R]
// This eventually gets saved back to resultState
void StepParityGenerator::mergeInitialAndResultPosition(
    State* initial_state, State* result_state, const FootPlacement& columns,
    int column_count) {
  // Merge initial + result position
  for (int i = 0; i < column_count; i++) {
    // copy in data from columns over the top which overrides it, as long as
    // it's not nothing
    if (columns[i] != Foot_None) {
      result_state->combined_columns_[i] = columns[i];
      continue;
    }

    // copy in data from initialState, if it wasn't moved
    if (initial_state->combined_columns_[i] == Foot_LeftHeel ||
        initial_state->combined_columns_[i] == Foot_RightHeel) {
      if (!result_state
               ->did_the_foot_move_[initial_state->combined_columns_[i]]) {
        result_state->combined_columns_[i] =
            initial_state->combined_columns_[i];
      }
    } else if (initial_state->combined_columns_[i] == Foot_LeftToe) {
      if (!result_state->did_the_foot_move_[Foot_LeftToe] &&
          !result_state->did_the_foot_move_[Foot_LeftHeel]) {
        result_state->combined_columns_[i] =
            initial_state->combined_columns_[i];
      }
    } else if (initial_state->combined_columns_[i] == Foot_RightToe) {
      if (!result_state->did_the_foot_move_[Foot_RightToe] &&
          !result_state->did_the_foot_move_[Foot_RightHeel]) {
        result_state->combined_columns_[i] =
            initial_state->combined_columns_[i];
      }
    }
  }

  for (int i = 0; i < column_count; i++) {
    if (result_state->combined_columns_[i] != Foot_None) {
      result_state->where_the_feet_are_[result_state->combined_columns_[i]] = i;
    }
    result_state->combined_mask_ |=
        (static_cast<uint32_t>(result_state->combined_columns_[i])) << (i * 3);
  }
}

const std::vector<FootPlacement>*
StepParityGenerator::getFootPlacementPermutations(const Row& row) {
  int cache_key = row.note_mask_ | row.hold_mask_;

  auto maybe_permute_foot_placements = layout_->permute_cache_.find(cache_key);

  // If no valid foot placements were found for (note_mask | hold_mask), then
  // check if there is a valid placement for just note_mask
  // (basically, assume that the player has to drop the holds)
  if (maybe_permute_foot_placements == layout_->permute_cache_.end()) {
    maybe_permute_foot_placements =
        layout_->permute_cache_.find(row.note_mask_);
  }

  // And if there still aren't any valid placements, return
  // permuteCache[0], which will be an empty array.
  if (maybe_permute_foot_placements == layout_->permute_cache_.end()) {
    return &(layout_->permute_cache_.at(0));
  } else {
    auto& value = maybe_permute_foot_placements->second;
    return &value;
  }
}

std::vector<int> StepParityGenerator::computeCheapestPath() {
  // Find the best final node
  StepParityNode* best_final_node =
      end_node_->previous_node_;  // If you're using the endNode approach

  // OR if you're tracking the best final node directly:
  // StepParityNode* bestFinalNode = ... (the one with minimum totalCost)

  if (best_final_node == nullptr) {
    LOG->Info("StepParityGenerator::computeCheapestPath: no valid path found");
    return {};
  }

  std::vector<int> path;
  StepParityNode* current = best_final_node;

  // Walk backwards using pred pointers
  while (current != start_node_ && current != nullptr) {
    path.push_back(current->id_);
    current = current->previous_node_;
  }

  if (current == nullptr) {
    LOG->Info(
        "StepParityGenerator::computeCheapestPath: path did not reach start "
        "node");
    return {};
  }

  std::reverse(path.begin(), path.end());
  return path;
}
void StepParityGenerator::CreateRows(const NoteData& in) {
  int column_count = in.GetNumTracks();

  RowCounter counter = RowCounter(column_count);

  NoteData::all_tracks_const_iterator curr_note =
      in.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW);

  for (; !curr_note.IsAtEnd(); ++curr_note) {
    int col = curr_note.Track();
    int row = curr_note.Row();

    IntermediateNoteData note;
    note.type_ = curr_note->type_;
    note.beat_ = NoteRowToBeat(row);
    note.second_ = timing_->GetElapsedTimeFromBeat(note.beat_);
    note.fake_ = note.type_ == TapNoteType_Fake || timing_->IsFakeAtRow(row);
    if (note.type_ == TapNoteType_HoldHead) {
      note.hold_length_ = NoteRowToBeat(curr_note->duration_);
    } else {
      note.hold_length_ = -1;
    }

    if (note.type_ == TapNoteType_Empty ||
        note.type_ == TapNoteType_AutoKeysound) {
      continue;
    }

    if (note.type_ == TapNoteType_Mine) {
      // If this mine occurs on the same row as everything else that's been
      // counted (in other words, if this note doesn't represent the start of a
      // new row), and this isn't the very first row, put it in nextMines?? I
      // honestly don't know why this works the way it does, it all feels really
      // backwards to me. I think the complication comes from the fact that this
      // is getting handled before checking whether or not this note represens a
      // new row. But we only want to create a new Row if it has at least one
      // note. So probably something like
      /*

       for(note of notes)
       {
              if(note is empty note)
               {
                      continue
               }
              if(note is on new row and counter has at least one note)
              {
                      create new row
                      reset counter
              }
              check if note is a mine or fake mine
              if note is fake continue
              put note into counter.notes
       }
       */
      if (note.second_ == counter.last_column_second_ && rows_.size() > 0) {
        if (note.fake_) {
          counter.next_fake_mines_[col] = note.second_;
        } else {
          counter.next_mines_[col] = note.second_;
        }
      } else {
        if (note.fake_) {
          counter.fake_mines_[col] = note.second_;
        } else {
          counter.mines_[col] = note.second_;
        }
      }
      continue;
    }

    if (note.fake_) {
      continue;
    }

    if (counter.last_column_second_ != note.second_) {
      // We're past the previous row, so save all of the previous row's data.
      if (counter.last_column_second_ != CLM_SECOND_INVALID) {
        AddRow(counter);
      }

      // Move mines and fakeMines to "next", and reset counters.
      counter.last_column_second_ = note.second_;
      counter.last_column_beat_ = note.beat_;
      counter.next_mines_.assign(counter.mines_.begin(), counter.mines_.end());
      counter.next_fake_mines_.assign(
          counter.fake_mines_.begin(), counter.fake_mines_.end());
      counter.notes_ = std::vector<IntermediateNoteData>(column_count_);
      counter.mines_ = std::vector<float>(column_count_);
      counter.fake_mines_ = std::vector<float>(column_count_);

      // Reset any now-inactive holds to empty values.
      for (int c = 0; c < column_count_; c++) {
        if (counter.active_holds_[c].type_ == TapNoteType_Empty ||
            note.beat_ > counter.active_holds_[c].beat_ +
                             counter.active_holds_[c].hold_length_) {
          counter.active_holds_[c] = IntermediateNoteData();
        }
      }
    }

    counter.notes_[col] = note;
    if (note.type_ == TapNoteType_HoldHead) {
      counter.active_holds_[col] = note;
    }
  }

  AddRow(counter);
}

void StepParityGenerator::AddRow(RowCounter& counter) {
  Row new_row = CreateRow(counter);
  new_row.row_index_ = rows_.size();
  rows_.push_back(new_row);
}

Row StepParityGenerator::CreateRow(RowCounter& counter) {
  Row row = Row(column_count_);
  std::copy(counter.notes_.begin(), counter.notes_.end(), row.notes_.begin());
  std::copy(
      counter.next_mines_.begin(), counter.next_mines_.end(),
      row.mines_.begin());
  std::copy(
      counter.next_fake_mines_.begin(), counter.next_fake_mines_.end(),
      row.fake_mines_.begin());
  row.second_ = counter.last_column_second_;
  row.beat_ = counter.last_column_beat_;

  for (int c = 0; c < column_count_; c++) {
    // save any active holds
    if (counter.active_holds_[c].type_ == TapNoteType_Empty ||
        counter.active_holds_[c].second_ >= counter.last_column_second_) {
      row.holds_[c] = IntermediateNoteData();
    } else {
      row.holds_[c] = counter.active_holds_[c];
    }

    // write to masks
    uint16_t bit = 0x1 << c;
    if (row.notes_[c].type_ == TapNoteType_Tap ||
        row.notes_[c].type_ == TapNoteType_HoldHead) {
      row.note_mask_ |= bit;
    }

    if (row.holds_[c].type_ != TapNoteType_Empty) {
      row.hold_mask_ |= bit;
    }

    if (row.mines_[c] != 0) {
      row.mine_mask_ |= bit;
    }
    if (row.fake_mines_[c] != 0) {
      row.fake_mine_mask_ |= bit;
    }
  }
  return row;
}

std::uint64_t StepParityGenerator::getStateCacheKey(State* state) {
  std::uint64_t value = 0;
  value = state->combined_mask_ |
          (static_cast<uint64_t>(state->moved_mask_) << 30) |
          (static_cast<uint64_t>(state->holding_mask_) << 46);
  return value;
}

StepParityNode* StepParityGenerator::addNode(
    State* state, float second, int row_index) {
  node_pool_.emplace_back(state, second, row_index);
  StepParityNode* new_node = &node_pool_.back();
  new_node->id_ = int(nodes_.size());
  nodes_.push_back(new_node);
  return new_node;
}
