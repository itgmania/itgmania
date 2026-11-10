#ifndef SCREEN_EDIT_H
#define SCREEN_EDIT_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AutoKeysounds.h"
#include "Background.h"
#include "BitmapText.h"
#include "EditModePlayerManager.h"
#include "EnumHelper.h"
#include "Foreground.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameplayAssist.h"
#include "InputEventPlus.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NoteField.h"
#include "NoteTypes.h"
#include "Player.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "RageInputDevice.h"
#include "RageSound.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "SnapDisplay.h"
#include "Song.h"
#include "SongPosition.h"
#include "Steps.h"
#include "TempoDetector.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"

const int NUM_EDIT_BUTTON_COLUMNS = 10;
struct MenuDef;

/** @brief What is going on with the Editor? */
enum EditState {
  STATE_EDITING,          /**< The person is making adjustments to the Steps. */
  STATE_RECORDING,        /**< The person is recording some Steps live. */
  STATE_RECORDING_PAUSED, /**< The person has temporarily paused the recording
                             of Steps. */
  STATE_PLAYING,          /**< The person is just trying out the Steps. */
  NUM_EditState,
  EditState_Invalid
};
const std::string& EditStateToString(EditState es);
LuaDeclareType(EditState);

enum EditButton {
  // Add to the name_to_edit_button list when adding to this enum. -Kyz
  EDIT_BUTTON_COLUMN_0,
  EDIT_BUTTON_COLUMN_1,
  EDIT_BUTTON_COLUMN_2,
  EDIT_BUTTON_COLUMN_3,
  EDIT_BUTTON_COLUMN_4,
  EDIT_BUTTON_COLUMN_5,
  EDIT_BUTTON_COLUMN_6,
  EDIT_BUTTON_COLUMN_7,
  EDIT_BUTTON_COLUMN_8,
  EDIT_BUTTON_COLUMN_9,

  // These are modifiers to EDIT_BUTTON_COLUMN_*.
  EDIT_BUTTON_RIGHT_SIDE,
  EDIT_BUTTON_LAY_ROLL,
  EDIT_BUTTON_LAY_TAP_ATTACK,
  EDIT_BUTTON_REMOVE_NOTE,

  // These are modifiers to change the present tap note.
  EDIT_BUTTON_CYCLE_TAP_LEFT,  /**< Rotate the available tap notes once to the
                                  "left". */
  EDIT_BUTTON_CYCLE_TAP_RIGHT, /**< Rotate the available tap notes once to the
                                  "right". */

  EDIT_BUTTON_CYCLE_SEGMENT_LEFT,  /**< Select one segment to the left for
                                      jumping. */
  EDIT_BUTTON_CYCLE_SEGMENT_RIGHT, /**< Select one segment to the right for
                                      jumping. */

  EDIT_BUTTON_SCROLL_UP_LINE,
  EDIT_BUTTON_SCROLL_UP_PAGE,
  EDIT_BUTTON_SCROLL_UP_TS,
  EDIT_BUTTON_SCROLL_DOWN_LINE,
  EDIT_BUTTON_SCROLL_DOWN_PAGE,
  EDIT_BUTTON_SCROLL_DOWN_TS,
  EDIT_BUTTON_SCROLL_NEXT_MEASURE,
  EDIT_BUTTON_SCROLL_PREV_MEASURE,
  EDIT_BUTTON_SCROLL_HOME,
  EDIT_BUTTON_SCROLL_END,
  EDIT_BUTTON_SCROLL_NEXT,
  EDIT_BUTTON_SCROLL_PREV,

  EDIT_BUTTON_SEGMENT_NEXT, /**< Jump to the start of the next segment downward.
                             */
  EDIT_BUTTON_SEGMENT_PREV, /**< Jump to the start of the previous segment
                               upward. */

  // These are modifiers to EDIT_BUTTON_SCROLL_*.
  EDIT_BUTTON_SCROLL_SELECT,

  EDIT_BUTTON_LAY_SELECT,

  EDIT_BUTTON_SCROLL_SPEED_UP,
  EDIT_BUTTON_SCROLL_SPEED_DOWN,

  EDIT_BUTTON_SNAP_NEXT,
  EDIT_BUTTON_SNAP_PREV,

  EDIT_BUTTON_OPEN_EDIT_MENU,
  EDIT_BUTTON_OPEN_TIMING_MENU,
  EDIT_BUTTON_OPEN_ALTER_MENU,
  EDIT_BUTTON_OPEN_AREA_MENU,
  EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU,
  EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU,
  EDIT_BUTTON_OPEN_COURSE_MENU,
  EDIT_BUTTON_OPEN_COURSE_ATTACK_MENU,

  EDIT_BUTTON_OPEN_STEP_ATTACK_MENU, /**< Open up the Step Attacks menu. */
  EDIT_BUTTON_ADD_STEP_MODS,         /**< Add a mod attack to the row. */

  EDIT_BUTTON_OPEN_INPUT_HELP,

  EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP,
  EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE,

  EDIT_BUTTON_PLAY_FROM_START,
  EDIT_BUTTON_PLAY_FROM_CURSOR,
  EDIT_BUTTON_PLAY_SELECTION,
  EDIT_BUTTON_RECORD_FROM_CURSOR,
  EDIT_BUTTON_RECORD_SELECTION,

  EDIT_BUTTON_RECORD_HOLD_RESET,
  EDIT_BUTTON_RECORD_HOLD_OFF,
  EDIT_BUTTON_RECORD_HOLD_MORE,
  EDIT_BUTTON_RECORD_HOLD_LESS,

  EDIT_BUTTON_RETURN_TO_EDIT,

  EDIT_BUTTON_INSERT,
  EDIT_BUTTON_DELETE,
  EDIT_BUTTON_INSERT_SHIFT_PAUSES,
  EDIT_BUTTON_DELETE_SHIFT_PAUSES,

  EDIT_BUTTON_OPEN_NEXT_STEPS,
  EDIT_BUTTON_OPEN_PREV_STEPS,
  EDIT_BUTTON_PLAY_SAMPLE_MUSIC,

  EDIT_BUTTON_BPM_UP,
  EDIT_BUTTON_BPM_DOWN,
  EDIT_BUTTON_STOP_UP,
  EDIT_BUTTON_STOP_DOWN,

  EDIT_BUTTON_DELAY_UP,
  EDIT_BUTTON_DELAY_DOWN,

  EDIT_BUTTON_OFFSET_UP,
  EDIT_BUTTON_OFFSET_DOWN,
  EDIT_BUTTON_SAMPLE_START_UP,
  EDIT_BUTTON_SAMPLE_START_DOWN,
  EDIT_BUTTON_SAMPLE_LENGTH_UP,
  EDIT_BUTTON_SAMPLE_LENGTH_DOWN,

  EDIT_BUTTON_ADJUST_FINE, /**< This button modifies offset, BPM, and stop
                              segment changes. */

  EDIT_BUTTON_SAVE, /**< Save the present changes into the chart. */

  EDIT_BUTTON_UNDO,  /**< Undo a recent change. */
  EDIT_BUTTON_REDO,  /**< Redo a change that was undone. */
  EDIT_BUTTON_COPY,  /**< Copy the current selection to the clipboard. */
  EDIT_BUTTON_CUT,   /**< Cut the current selection to the clipboard. */
  EDIT_BUTTON_PASTE, /**< Paste the clipboard at the current beat. */

  EDIT_BUTTON_TOGGLE_WAVEFORM, /**< Show/hide the song waveform. */

  EDIT_BUTTON_ADD_COURSE_MODS,

  EDIT_BUTTON_SWITCH_PLAYERS, /**< Allow entering notes for a different Player.
                               */

  EDIT_BUTTON_SWITCH_TIMINGS, /**< Allow switching between Song and Step
                                 TimingData. */

  // Add to the name_to_edit_button list when adding to this enum. -Kyz
  NUM_EditButton,  // leave this at the end
  EditButton_Invalid
};
/** @brief A special foreach loop for the different edit buttons. */
#define FOREACH_EditButton(e) FOREACH_ENUM(EditButton, e)
const int NUM_EDIT_TO_DEVICE_SLOTS = 2;
const int NUM_EDIT_TO_MENU_SLOTS = 2;

/* g_MapEditToDI is a simple mapping: edit functions map to DeviceInputs.
 * If g_MapEditToDIHold for a given edit function is valid, then at least one
 * input in g_MapEditToDIHold must be held when pressing any key in
 * g_MapEditToDI for the input to occur. */
struct MapEditToDI {
  DeviceInput button_[NUM_EditButton][NUM_EDIT_TO_DEVICE_SLOTS];
  DeviceInput hold_[NUM_EditButton][NUM_EDIT_TO_DEVICE_SLOTS];
  void Clear() {
    FOREACH_EditButton(e) for (int slot = 0; slot < NUM_EDIT_TO_DEVICE_SLOTS;
                               ++slot) {
      button_[e][slot].MakeInvalid();
      hold_[e][slot].MakeInvalid();
    }
  }
};

/**
 * @brief This is similar to MapEditToDI,
 * but maps GameButton instead of DeviceInput. */
struct MapEditButtonToMenuButton {
  GameButton button_[NUM_EditButton][NUM_EDIT_TO_MENU_SLOTS];
  MapEditButtonToMenuButton() { Clear(); }
  void Clear() {
    FOREACH_EditButton(e) for (int slot = 0; slot < NUM_EDIT_TO_MENU_SLOTS;
                               ++slot) button_[e][slot] = GameButton_Invalid;
  }
};
/** @brief Edit, record, playback, and synchronize notes. */
class ScreenEdit : public ScreenWithMenuElements {
 public:
  virtual void Init();
  virtual void BeginScreen();
  virtual void EndScreen();

  virtual ~ScreenEdit();
  virtual void Update(float delta_time);
  virtual void DrawPrimitives();
  virtual bool Input(const InputEventPlus& input);
  bool InputEdit(const InputEventPlus& input, EditButton edit_b);
  bool InputRecord(const InputEventPlus& input, EditButton edit_b);
  bool InputRecordPaused(const InputEventPlus& input, EditButton edit_b);
  bool InputPlay(const InputEventPlus& input, EditButton edit_b);
  virtual void HandleMessage(const Message& msg);
  virtual void HandleScreenMessage(const ScreenMessage sm);

  void SetDirty(bool dirty);
  bool IsDirty() const { return dirty_; }

  void PerformSave(bool autosave);

  EditState GetEditState() { return edit_state_; }

  // For seeking through the song instantly.
  void SeekSong(float second);

  // Lua
  virtual void PushSelf(lua_State* L);

 protected:
  virtual ScreenType GetScreenType() const {
    return edit_state_ == STATE_PLAYING
               ? gameplay
               : ScreenWithMenuElements::GetScreenType();
  }

  void TransitionEditState(EditState em);
  void ScrollTo(float destination_beat);
  void PlayTicks();
  void PlayPreviewMusic();

  // Call this before modifying m_NoteDataEdit.
  void SaveUndo(const std::string& description = "Edit");
  /** @brief Revert the last change made to m_NoteDataEdit. */
  void Undo();
  /** @brief Reapply the last change that was undone. */
  void Redo();
  /** @brief Remove the previously stored NoteData to prevent undoing/redoing.
   */
  void ClearUndo();
  /** @brief Cut/copy the current area selection to the clipboard, or report
   * failure via a system message if there is no selection. When bIncludeTiming
   * is set, the timing segments in the selection travel with the notes. */
  void CutSelectionToClipboard(bool include_timing);
  void CopySelectionToClipboard(bool include_timing);
  /** @brief Paste the clipboard at the current beat, reporting the result via
   * a system message. */
  void PasteClipboardAtCurrentBeat(bool include_timing);
  /** @brief Show/hide the song waveform, reporting the new state via a
   * system message. */
  void ToggleWaveform();
  /**
   * @brief This is to be called after modifying m_NoteDataEdit.
   *
   * It will Undo itself if MAX_NOTES_PER_MEASURE was exceeded. */
  void CheckNumberOfNotesAndUndo();

  void OnSnapModeChange();

  float GetMaximumBeatForNewNote()
      const;  // don't allow Down key to go past this beat.
  float GetMaximumBeatForMoving()
      const;  // don't allow Down key to go past this beat.

  /** @brief Display the keyboard track menu for the current row. */
  void DoKeyboardTrackMenu();

  /** @brief Display the step attack menu for the current row. */
  void DoStepAttackMenu();

  void DoHelp();

  /** @brief Display the TimingData menu for editing song and step timing. */
  void DisplayTimingMenu();

  /** @brief Convert a screen y coordinate to the beat drawn at that point. */
  float MouseYToBeat(float screen_y);
  /** @brief Turn the area the mouse was dragged over into an area selection. */
  void FinishMouseDragSelection();

  /** @brief Display the menu for detecting and applying BPM/offset. */
  void DisplayAdjustSyncMenu();
  /** @brief Start detecting the BPM/offset of the current chart's music. */
  void StartTempoDetection();

  enum TimingChangeMenuPurpose {
    menu_is_for_copying,
    menu_is_for_shifting,
    menu_is_for_clearing
  };
  TimingChangeMenuPurpose timing_change_menu_purpose_;
  int timing_rows_being_shitted_;  // How far the timing is being shitted.
  void DisplayTimingChangeMenu();

  EditState edit_state_;

  Song* song_;
  Steps* steps_;

  PlayerNumber input_player_number_;
  PlayerNumber main_player_;
  PlayerState player_state_edit_;
  NoteField note_field_edit_;
  NoteData note_data_edit_;
  SnapDisplay snap_display_;

  BitmapText text_input_tips_;

  /** @brief The player options before messing with attacks. */
  ModsGroup<PlayerOptions> original_player_options_;

  /**
   * @brief Keep a backup of the present Step TimingData when
   * entering a playing or recording state.
   *
   * This is mainly to allow playing a chart with Song Timing. */
  TimingData backup_step_timing_;

  /**
   * @brief Allow for copying and pasting a song's (or steps's) full Timing
   * Data.
   *
   * Static so this persists across ScreenEdit instances (see m_Clipboard).
   */
  static TimingData clipboard_full_timing_;

  /** @brief The current TapNote that would be inserted. */
  TapNote selected_tap_;

  /** @brief The type of segment users will jump back and forth between. */
  TimingSegmentType current_cycle_segment_;

  /** @brief Runs while the music is being analyzed for its BPM and offset. */
  TempoDetector* tempo_detector_;
  std::vector<TempoResult> tempo_results_;
  std::string tempo_progress_;

  void UpdateTextInfo();
  BitmapText text_info_;  // status information that changes
  bool text_info_needs_update_;

  BitmapText text_play_record_help_;

  // keep track of where we are and what we're doing
  float trailing_beat_;
  // the above approaches GAMESTATE->m_fSongBeat, which is the actual beat
  /**
   * @brief The location we were at when shift was pressed.
   *
   * If shift wasn't pressed, this will be -1. */
  int shift_anchor_;

  /**
   * @brief The NoteData that has been cut or copied.
   *
   * This is static so that the clipboard persists when switching between
   * charts/difficulties, which destroys and recreates ScreenEdit. */
  static NoteData clipboard_;

  /** @brief Whether the last cut/copy also captured the timing segments in
   * the selection, so that pasting can restore them. */
  static bool clipboard_has_timing_;

  /** @brief The marquee used to select an area with the mouse. */
  bool mouse_dragging_;
  float mouse_drag_start_x_, mouse_drag_start_y_;
  float mouse_drag_current_x_, mouse_drag_current_y_;
  Quad rect_mouse_selection_;

  /** @brief One saved state in the undo/redo history. */
  struct UndoState {
    NoteData note_data_;
    std::string description_;
  };
  static const size_t kMaxUndoStates = 50;
  /** @brief States that can be restored via Undo(), most recent last. */
  std::vector<UndoState> undo_stack_;
  /** @brief States that can be restored via Redo(), most recent last. */
  std::vector<UndoState> redo_stack_;

  /** @brief Has the NoteData been changed such that a user should be prompted
   * to save? */
  bool dirty_;
  float next_autosave_time_;

  /** @brief The sound that is played when a note is added. */
  RageSound sound_add_note_;
  /** @brief The sound that is played when a note is removed. */
  RageSound sound_remove_note_;
  RageSound sound_change_line_;
  RageSound sound_change_snap_;
  RageSound sound_marker_;
  RageSound sound_value_increase_;
  RageSound sound_value_decrease_;
  /** @brief The sound that is played when switching players for Routine. */
  RageSound sound_switch_player_;
  /** @brief The sound that is played when switching song/step timing. */
  RageSound sound_switch_timing_;
  /** @brief The sound that is played when switching to a different chart. */
  RageSound sound_switch_steps_;
  /** @brief The sound that is played when the chart is saved. */
  RageSound sound_save_;

  // used for reverting
  void CopyToLastSave();
  void CopyFromLastSave();
  void RevertFromDisk();

  SongData song_last_save_;
  std::vector<std::unique_ptr<Steps>> steps_last_save_;

  // for MODE_RECORD
  NoteField note_field_record_;
  NoteData note_data_record_;
  RageTimer remove_note_button_last_changed_;
  bool remove_note_button_down_;

  // for MODE_PLAY
  EditModePlayerManager player_manager_;
  void SetupCourseAttacks();
  Background background_;
  Foreground foreground_;
  bool return_to_record_menu_after_play_;

  // for MODE_RECORD and MODE_PLAY
  int start_playing_at_, stop_playing_at_;
  float beat_to_return_to_;

  AutoKeysounds auto_keysounds_;
  RageSound* sound_music_;
  GameplayAssist gameplay_assist_;

  ThemeMetric<EditMode> EDIT_MODE;

  // Scroll acceleration tracking for mouse wheel
  RageTimer last_wheel_scroll_time_;
  int consecutive_wheel_scrolls_;
  float scroll_acceleration_multiplier_;
  static const float kScrollAccelerationResetTime;  // Time in seconds to
                                                    // reset scroll counter
  static const float kMaxScrollAccelerationMultiplier;

  // Scroll-speed presets stepped through by EDIT_BUTTON_SCROLL_SPEED_UP/DOWN
  // (CTRL+Up/Down) and CTRL+wheel; parsed from the ScrollSpeedPresets metric.
  std::vector<float> scroll_speed_presets_;

 public:
  /** @brief What are the choices that one can make on the main menu? */
  enum MainMenuChoice {
    play_selection,
    set_selection_start,
    set_selection_end,
    edit_steps_information,
    play_whole_song, /**< Play the entire chart from the beginning. */
    play_selection_start_to_end,
    play_current_beat_to_end,
    save, /**< Save the current chart to disk. */
    revert_to_last_save,
    revert_from_disk,
    options,            /**< Modify the PlayerOptions and SongOptions. */
    edit_song_info,     /**< Edit some general information about the song. */
    edit_timing_data,   /**< Edit the chart's timing data. */
    adjust_sync,        /**< Detect and apply the music's BPM and offset. */
    view_steps_data,    /**< View step statistics. */
    play_preview_music, /**< Play the song's preview music. */
    exit,
    save_on_exit,
    NUM_MAIN_MENU_CHOICES,
    MAIN_MENU_CHOICE_INVALID
  };
  int GetSongOrNotesEnd();
  void HandleMainMenuChoice(MainMenuChoice c, const std::vector<int>& answers);
  void HandleMainMenuChoice(MainMenuChoice c) {
    const std::vector<int> v;
    HandleMainMenuChoice(c, v);
  }
  MainMenuChoice current_action_;

  /** @brief How does one alter a selection of NoteData? */
  enum AlterMenuChoice {
    cut,       /**< Cut the notes. */
    copy,      /**< Copy the notes. */
    clear,     /**< Erase the notes, without putting them in the clipboard. */
    quantize,  /**< Sync the notes to an exact level. */
    turn,      /**< Rotate the notes. */
    transform, /**< Activate a specific mod. */
    alter,     /**< Perform other transformations. */
    tempo,     /**< Modify the tempo of the notes. */
    play,      /**< Play the notes in the range. */
    record,    /**< Record new notes in the range. */
    preview_designation,   /**< Set the area as the music preview. */
    convert_to_pause,      /**< Convert the range into a StopSegment. */
    convert_to_delay,      /**< Convert the range into a DelaySegment. */
    convert_to_warp,       /**< Convert the range into a WarpSegment. */
    convert_to_fake,       /**< Convert the range into a FakeSegment. */
    convert_to_attack,     /**< Convert the range into an Attack. */
    routine_invert_notes,  /**< Switch which player hits the note. */
    routine_mirror_1_to_2, /**< Mirror Player 1's notes for Player 2. */
    routine_mirror_2_to_1, /**< Mirror Player 2's notes for Player 1. */
    NUM_ALTER_MENU_CHOICES

  };

  enum AreaMenuChoice {
    paste_at_current_beat, /**< Paste note data starting at the current beat. */
    paste_at_begin_marker, /**< Paste note data starting at the first market. */
    insert_and_shift,
    delete_and_shift,
    shift_pauses_forward,  /**< Shift all timing changes forward one beat. */
    shift_pauses_backward, /**< Shift all timing changes backward one beat. */
    convert_pause_to_beat,
    convert_delay_to_beat,
    last_second_at_beat,
    undo,
    clear_clipboard,         /**< Clear the clipboards. */
    modify_attacks_at_row,   /**< Modify the chart attacks at this row. */
    modify_keysounds_at_row, /**< Modify the keysounds at this row. */
    NUM_AREA_MENU_CHOICES
  };
  void HandleArbitraryRemapping(const std::string& mapstr);
  void HandleAlterMenuChoice(
      AlterMenuChoice choice, const std::vector<int>& answers,
      bool allow_undo = true, bool prompt_clear = true);
  void HandleAlterMenuChoice(
      AlterMenuChoice c, bool allow_undo = true, bool prompt_clear = true) {
    const std::vector<int> v;
    HandleAlterMenuChoice(c, v, allow_undo, prompt_clear);
  }

  void HandleAreaMenuChoice(
      AreaMenuChoice c, const std::vector<int>& answers,
      bool allow_undo = true);
  void HandleAreaMenuChoice(AreaMenuChoice c, bool allow_undo = true) {
    const std::vector<int> v;
    HandleAreaMenuChoice(c, v, allow_undo);
  }
  /** @brief How should the selected notes be transformed? */
  enum TurnType {
    left,           /**< Turn the notes as if you were facing to the left. */
    right,          /**< Turn the notes as if you were facing to the right. */
    mirror,         /**< Flip the notes vertically. */
    lrmirror,       /**< Swap the left and right columns. */
    udmirror,       /**< Swap the up and down columns. */
    turn_backwards, /**< Turn the notes as if you were facing away from the
                       machine. */
    shuffle,        /**< Replace one column with another column. */
    super_shuffle,  /**< Replace each note individually. */
    hyper_shuffle,  /**< Replace each note individually but more fairly. */
    NUM_TURN_TYPES
  };
  enum TransformType {
    noholds,
    nomines,
    little,
    wide,
    big,
    quick,
    skippy,
    add_mines,
    echo,
    stomp,
    planted,
    floored,
    twister,
    nojumps,
    nohands,
    noquads,
    nostretch,
    NUM_TRANSFORM_TYPES
  };
  enum AlterType {
    autogen_to_fill_width,
    backwards,
    swap_sides,
    copy_left_to_right,
    copy_right_to_left,
    clear_left,
    clear_right,
    collapse_to_one,
    collapse_left,
    shift_left,
    shift_right,
    swap_up_down,
    arbitrary_remap,
    NUM_ALTER_TYPES
  };
  enum TempoType {
    compress_2x,
    compress_3_2,
    compress_4_3,
    expand_4_3,
    expand_3_2,
    expand_2x,
    NUM_TEMPO_TYPES
  };

  enum StepsInformationChoice {
    difficulty,    /**< What is the difficulty of this chart? */
    meter,         /**< What is the numerical rating of this chart? */
    predict_meter, /**< What does the game think this chart's rating should be?
                    */
    chartname,     /**< What is the name of this chart? */
    description,   /**< What is the description of this chart? */
    chartstyle,    /**< How is this chart meant to be played? */
    step_credit,   /**< Who wrote this individual chart? */
    step_display_bpm,
    step_min_bpm,
    step_max_bpm,
    step_music,
    NUM_STEPS_INFORMATION_CHOICES
  };
  void HandleStepsInformationChoice(
      StepsInformationChoice c, const std::vector<int>& answers);

  enum StepsDataChoice {
    tap_notes,
    jumps,
    hands,
    quads,
    holds,
    mines,
    rolls,
    lifts,
    fakes,
    stream,
    voltage,
    air,
    freeze,
    chaos,
    NUM_STEPS_DATA_CHOICES
  };
  void HandleStepsDataChoice(
      StepsDataChoice c, const std::vector<int>& answers);

  enum SongInformationChoice {
    main_title,
    sub_title,
    artist,
    genre,
    credit,
    preview,
    main_title_transliteration,
    sub_title_transliteration,
    artist_transliteration,
    last_second_hint,
    preview_start,
    preview_length,
    display_bpm,
    min_bpm,
    max_bpm,
    NUM_SONG_INFORMATION_CHOICES
  };
  void HandleSongInformationChoice(
      SongInformationChoice c, const std::vector<int>& answers);

  enum TimingDataInformationChoice {
    beat_0_offset,
    bpm,
    stop,
    delay,
    //		time_signature,
    label,
    tickcount,
    combo,
    warp,
    //		speed,
    speed_percent,
    speed_wait,
    speed_mode,
    scroll,
    fake,
    shift_timing_in_region_down,
    shift_timing_in_region_up,
    copy_timing_in_region,
    clear_timing_in_region,
    paste_timing_from_clip,
    copy_full_timing,
    paste_full_timing,
    erase_step_timing,
    NUM_TIMING_DATA_INFORMATION_CHOICES
  };
  void HandleTimingDataInformationChoice(
      TimingDataInformationChoice c, const std::vector<int>& answers);

  enum TimingDataChangeChoice {
    timing_all,
    timing_bpm,
    timing_stop,
    timing_delay,
    timing_time_sig,
    timing_warp,
    timing_label,
    timing_tickcount,
    timing_combo,
    timing_speed,
    timing_scroll,
    timing_fake,
    NUM_TimingDataChangeChoices
  };
  void HandleTimingDataChangeChoice(
      TimingDataChangeChoice choice, const std::vector<int>& answers);

  /**
   * @brief The rows of the Adjust Sync menu.
   *
   * Each detected result adds an "apply" row followed by two read-only rows,
   * so apply rows use adjust_sync_apply plus the index of the result. */
  enum AdjustSyncChoice {
    adjust_sync_find_bpm,
    adjust_sync_readonly,
    adjust_sync_apply
  };
  void HandleAdjustSyncMenuChoice(int row_code);

  enum BGChangeChoice {
    layer,
    rate,
    transition,
    effect,
    color1,
    color2,
    file1_type,
    file1_song_bganimation,
    file1_song_movie,
    file1_song_still,
    file1_global_bganimation,
    file1_global_movie,
    file1_global_movie_song_group,
    file1_global_movie_song_group_and_genre,
    file2_type,
    file2_song_bganimation,
    file2_song_movie,
    file2_song_still,
    file2_global_bganimation,
    file2_global_movie,
    file2_global_movie_song_group,
    file2_global_movie_song_group_and_genre,
    delete_change,
    NUM_BGCHANGE_CHOICES
  };

  enum SpeedSegmentModes { SSMODE_Beats, SSMODE_Seconds };

  /**
   * @brief Take care of any background changes that the user wants.
   *
   * It is important that this is only called in Song Timing mode.
   * @param c the Background Change style requested.
   * @param iAnswers the other settings involving the change. */
  void HandleBGChangeChoice(BGChangeChoice c, const std::vector<int>& answers);

  enum CourseAttackChoice {
    duration,
    set_mods,
    remove,
    NUM_CourseAttackChoice
  };

  void InitEditMappings();
  EditButton DeviceToEdit(const DeviceInput& device_i) const;
  EditButton MenuButtonToEditButton(GameButton menu_i) const;
  bool EditToDevice(
      EditButton button, int slot_num, DeviceInput& device_i) const;
  bool EditPressed(EditButton button, const DeviceInput& device_i);
  bool EditIsBeingPressed(EditButton button) const;
  const MapEditToDI* GetCurrentDeviceInputMap() const;
  const MapEditButtonToMenuButton* GetCurrentMenuButtonMap() const;
  void LoadKeymapSectionIntoMappingsMember(
      const XNode* section, MapEditToDI& mappings);
  MapEditToDI edit_mappings_device_input_;
  MapEditToDI play_mappings_device_input_;
  MapEditToDI record_mappings_device_input_;
  MapEditToDI record_paused_mappings_device_input_;
  MapEditButtonToMenuButton edit_mappings_menu_button_;
  MapEditButtonToMenuButton play_mappings_menu_button_;
  MapEditButtonToMenuButton record_mappings_menu_button_;
  MapEditButtonToMenuButton record_paused_mappings_menu_button_;

  void MakeFilteredMenuDef(const MenuDef* def, MenuDef& menu);
  void EditMiniMenu(
      const MenuDef* def, ScreenMessage sm_send_on_ok = SM_None,
      ScreenMessage sm_send_on_cancel = SM_None);

 private:
  int mod_in_process_;

  enum class AttackSource { Song, Steps };
  struct SelectedAttack {
    AttackSource source_;
    int index_;
  };
  std::optional<SelectedAttack> selected_attack_;

  AttackArray GetAttacksForEditing() const;
  AttackArray& GetAttackArray(AttackSource source);
  AttackArray& GetSelectedAttackArray();
  Attack& GetSelectedAttack();
  SelectedAttack ResolveAttackIndex(int combined_index) const;
  /**
   * @brief Retrieve the appropriate TimingData based on GAMESTATE.
   * @return the proper TimingData. */
  const TimingData& GetAppropriateTiming() const;
  /**
   * @brief Retrieve the appropriate TimingData to use for updating. */
  TimingData& GetAppropriateTimingForUpdate();
  /**
   * @brief Retrieve the appropriate SongPosition data based on GAMESTATE.
   * @return the proper SongPosition. */
  SongPosition& GetAppropriatePosition() const;
  void SetBeat(float beat);
  float GetBeat();
  int GetRow();
};

#endif

/*
 * (c) 2001-2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
