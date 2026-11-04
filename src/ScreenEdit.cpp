#include "ScreenEdit.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ActorUtil.h"
#include "AdjustSync.h"
#include "ArrowEffects.h"
#include "Attack.h"
#include "BackgroundUtil.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "IniFile.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteField.h"
#include "NoteSkinManager.h"
#include "NoteTypes.h"
#include "NotesWriterSM.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenMiniMenu.h"
#include "ScreenPrompt.h"
#include "ScreenSaveSync.h"
#include "ScreenTextEntry.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "SongPosition.h"
#include "SongUtil.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "XmlFile.h"
#include "global.h"

static Preference<float> g_iDefaultRecordLength("DefaultRecordLength", 4);
static Preference<bool> g_bEditorShowBGChangesPlay(
    "EditorShowBGChangesPlay", true);

/** @brief How long must the button be held to generate a hold in record mode?
 * Note this must be a float, if it isn't then record mode can't tell if you're
 * trying to input a hold or not. */
constexpr static float record_hold_default = 0.3f;
float record_hold_seconds = record_hold_default;
constexpr static float time_between_autosave = 300.0f;  // 5 minutes. -Kyz
static float GetMouseScreenX();
static float GetMouseScreenY();

#define PLAYER_X (SCREEN_CENTER_X)
#define PLAYER_Y (SCREEN_CENTER_Y)
#define PLAYER_HEIGHT (360)
#define PLAYER_Y_STANDARD \
  (PLAYER_Y - ((SCREEN_HEIGHT / 480) * (PLAYER_HEIGHT / 2)))

#define EDIT_X (SCREEN_CENTER_X)
#define EDIT_Y (PLAYER_Y)

#define RECORD_X (SCREEN_CENTER_X)
#define RECORD_Y (SCREEN_CENTER_Y)

#define PLAY_RECORD_HELP_TEXT THEME->GetString(name_, "PlayRecordHelpText")
#define EDIT_HELP_TEXT THEME->GetString(name_, "EditHelpText")

#define SET_MOD_SCREEN THEME->GetMetric("ScreenEdit", "SetModScreen")
#define OPTIONS_SCREEN THEME->GetMetric("ScreenEdit", "OptionsScreen")

AutoScreenMessage(SM_UpdateTextInfo);
AutoScreenMessage(SM_BackFromMainMenu);
AutoScreenMessage(SM_BackFromAreaMenu);
AutoScreenMessage(SM_BackFromAlterMenu);
AutoScreenMessage(SM_BackFromArbitraryRemap);
AutoScreenMessage(SM_BackFromStepsInformation);
AutoScreenMessage(SM_BackFromStepsData);
AutoScreenMessage(SM_BackFromOptions);
AutoScreenMessage(SM_BackFromSongInformation);
AutoScreenMessage(SM_BackFromBGChange);
AutoScreenMessage(SM_BackFromInsertTapAttack);
AutoScreenMessage(SM_BackFromInsertTapAttackPlayerOptions);
AutoScreenMessage(SM_BackFromAttackAtTime);
AutoScreenMessage(SM_BackFromInsertStepAttack);
AutoScreenMessage(SM_BackFromAddingModToExistingAttack);
AutoScreenMessage(SM_BackFromEditingModToExistingAttack);
AutoScreenMessage(SM_BackFromEditingAttackStart);
AutoScreenMessage(SM_BackFromEditingAttackLength);
AutoScreenMessage(SM_BackFromAddingAttackToChart);
AutoScreenMessage(SM_BackFromInsertStepAttackPlayerOptions);
AutoScreenMessage(SM_BackFromInsertCourseAttack);
AutoScreenMessage(SM_BackFromInsertCourseAttackPlayerOptions);
AutoScreenMessage(SM_BackFromCourseModeMenu);
AutoScreenMessage(SM_BackFromKeysoundTrack);
AutoScreenMessage(SM_BackFromNewKeysound);
AutoScreenMessage(SM_DoRevertToLastSave);
AutoScreenMessage(SM_DoRevertFromDisk);
AutoScreenMessage(SM_ConfirmClearArea);
AutoScreenMessage(SM_BackFromTimingDataInformation);
AutoScreenMessage(SM_BackFromTimingDataChangeInformation);
AutoScreenMessage(SM_BackFromAdjustSyncMenu);
AutoScreenMessage(SM_BackFromDifficultyMeterChange);
AutoScreenMessage(SM_BackFromBeat0Change);
AutoScreenMessage(SM_BackFromBPMChange);
AutoScreenMessage(SM_BackFromStopChange);
AutoScreenMessage(SM_BackFromDelayChange);
AutoScreenMessage(SM_BackFromTickcountChange);
AutoScreenMessage(SM_BackFromComboChange);
AutoScreenMessage(SM_BackFromLabelChange);
AutoScreenMessage(SM_BackFromWarpChange);
AutoScreenMessage(SM_BackFromSpeedPercentChange);
AutoScreenMessage(SM_BackFromSpeedWaitChange);
AutoScreenMessage(SM_BackFromSpeedModeChange);
AutoScreenMessage(SM_BackFromScrollChange);
AutoScreenMessage(SM_BackFromFakeChange);
AutoScreenMessage(SM_BackFromStepMusicChange);
AutoScreenMessage(SM_DoEraseStepTiming);
AutoScreenMessage(SM_DoSaveAndExit);
AutoScreenMessage(SM_DoExit);
AutoScreenMessage(SM_AutoSaveSuccessful);
AutoScreenMessage(SM_SaveSuccessful);
AutoScreenMessage(SM_SaveSuccessNoSM);
AutoScreenMessage(SM_SaveFailed);

static const char* EditStateNames[] = {
    "Edit", "Record", "RecordPaused", "Playing"};
XToString(EditState);
LuaXType(EditState);

std::map<std::string, EditButton> name_to_edit_button;

void ScreenEdit::InitEditMappings() {
  // Created courtesy of query replace regex.
  name_to_edit_button["COLUMN_0"] = EDIT_BUTTON_COLUMN_0;
  name_to_edit_button["COLUMN_1"] = EDIT_BUTTON_COLUMN_1;
  name_to_edit_button["COLUMN_2"] = EDIT_BUTTON_COLUMN_2;
  name_to_edit_button["COLUMN_3"] = EDIT_BUTTON_COLUMN_3;
  name_to_edit_button["COLUMN_4"] = EDIT_BUTTON_COLUMN_4;
  name_to_edit_button["COLUMN_5"] = EDIT_BUTTON_COLUMN_5;
  name_to_edit_button["COLUMN_6"] = EDIT_BUTTON_COLUMN_6;
  name_to_edit_button["COLUMN_7"] = EDIT_BUTTON_COLUMN_7;
  name_to_edit_button["COLUMN_8"] = EDIT_BUTTON_COLUMN_8;
  name_to_edit_button["COLUMN_9"] = EDIT_BUTTON_COLUMN_9;

  name_to_edit_button["RIGHT_SIDE"] = EDIT_BUTTON_RIGHT_SIDE;
  name_to_edit_button["LAY_ROLL"] = EDIT_BUTTON_LAY_ROLL;
  name_to_edit_button["LAY_TAP_ATTACK"] = EDIT_BUTTON_LAY_TAP_ATTACK;
  name_to_edit_button["REMOVE_NOTE"] = EDIT_BUTTON_REMOVE_NOTE;

  name_to_edit_button["CYCLE_TAP_LEFT"] = EDIT_BUTTON_CYCLE_TAP_LEFT;
  name_to_edit_button["CYCLE_TAP_RIGHT"] = EDIT_BUTTON_CYCLE_TAP_RIGHT;

  name_to_edit_button["CYCLE_SEGMENT_LEFT"] = EDIT_BUTTON_CYCLE_SEGMENT_LEFT;
  name_to_edit_button["CYCLE_SEGMENT_RIGHT"] = EDIT_BUTTON_CYCLE_SEGMENT_RIGHT;

  name_to_edit_button["SCROLL_UP_LINE"] = EDIT_BUTTON_SCROLL_UP_LINE;
  name_to_edit_button["SCROLL_UP_PAGE"] = EDIT_BUTTON_SCROLL_UP_PAGE;
  name_to_edit_button["SCROLL_UP_TS"] = EDIT_BUTTON_SCROLL_UP_TS;
  name_to_edit_button["SCROLL_DOWN_LINE"] = EDIT_BUTTON_SCROLL_DOWN_LINE;
  name_to_edit_button["SCROLL_DOWN_PAGE"] = EDIT_BUTTON_SCROLL_DOWN_PAGE;
  name_to_edit_button["SCROLL_DOWN_TS"] = EDIT_BUTTON_SCROLL_DOWN_TS;
  name_to_edit_button["SCROLL_NEXT_MEASURE"] = EDIT_BUTTON_SCROLL_NEXT_MEASURE;
  name_to_edit_button["SCROLL_PREV_MEASURE"] = EDIT_BUTTON_SCROLL_PREV_MEASURE;
  name_to_edit_button["SCROLL_HOME"] = EDIT_BUTTON_SCROLL_HOME;
  name_to_edit_button["SCROLL_END"] = EDIT_BUTTON_SCROLL_END;
  name_to_edit_button["SCROLL_NEXT"] = EDIT_BUTTON_SCROLL_NEXT;
  name_to_edit_button["SCROLL_PREV"] = EDIT_BUTTON_SCROLL_PREV;

  name_to_edit_button["SEGMENT_NEXT"] = EDIT_BUTTON_SEGMENT_NEXT;
  name_to_edit_button["SEGMENT_PREV"] = EDIT_BUTTON_SEGMENT_PREV;

  name_to_edit_button["SCROLL_SELECT"] = EDIT_BUTTON_SCROLL_SELECT;

  name_to_edit_button["LAY_SELECT"] = EDIT_BUTTON_LAY_SELECT;

  name_to_edit_button["SCROLL_SPEED_UP"] = EDIT_BUTTON_SCROLL_SPEED_UP;
  name_to_edit_button["SCROLL_SPEED_DOWN"] = EDIT_BUTTON_SCROLL_SPEED_DOWN;

  name_to_edit_button["SNAP_NEXT"] = EDIT_BUTTON_SNAP_NEXT;
  name_to_edit_button["SNAP_PREV"] = EDIT_BUTTON_SNAP_PREV;

  name_to_edit_button["OPEN_EDIT_MENU"] = EDIT_BUTTON_OPEN_EDIT_MENU;
  name_to_edit_button["OPEN_TIMING_MENU"] = EDIT_BUTTON_OPEN_TIMING_MENU;
  name_to_edit_button["OPEN_ALTER_MENU"] = EDIT_BUTTON_OPEN_ALTER_MENU;
  name_to_edit_button["OPEN_AREA_MENU"] = EDIT_BUTTON_OPEN_AREA_MENU;
  name_to_edit_button["OPEN_BGCHANGE_LAYER1_MENU"] =
      EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU;
  name_to_edit_button["OPEN_BGCHANGE_LAYER2_MENU"] =
      EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU;
  name_to_edit_button["OPEN_COURSE_MENU"] = EDIT_BUTTON_OPEN_COURSE_MENU;
  name_to_edit_button["OPEN_COURSE_ATTACK_MENU"] =
      EDIT_BUTTON_OPEN_COURSE_ATTACK_MENU;

  name_to_edit_button["OPEN_STEP_ATTACK_MENU"] =
      EDIT_BUTTON_OPEN_STEP_ATTACK_MENU;
  name_to_edit_button["ADD_STEP_MODS"] = EDIT_BUTTON_ADD_STEP_MODS;

  name_to_edit_button["OPEN_INPUT_HELP"] = EDIT_BUTTON_OPEN_INPUT_HELP;

  name_to_edit_button["BAKE_RANDOM_FROM_SONG_GROUP"] =
      EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP;
  name_to_edit_button["BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE"] =
      EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE;

  name_to_edit_button["PLAY_FROM_START"] = EDIT_BUTTON_PLAY_FROM_START;
  name_to_edit_button["PLAY_FROM_CURSOR"] = EDIT_BUTTON_PLAY_FROM_CURSOR;
  name_to_edit_button["PLAY_SELECTION"] = EDIT_BUTTON_PLAY_SELECTION;
  name_to_edit_button["RECORD_FROM_CURSOR"] = EDIT_BUTTON_RECORD_FROM_CURSOR;
  name_to_edit_button["RECORD_SELECTION"] = EDIT_BUTTON_RECORD_SELECTION;

  name_to_edit_button["RECORD_HOLD_RESET"] = EDIT_BUTTON_RECORD_HOLD_RESET;
  name_to_edit_button["RECORD_HOLD_OFF"] = EDIT_BUTTON_RECORD_HOLD_OFF;
  name_to_edit_button["RECORD_HOLD_MORE"] = EDIT_BUTTON_RECORD_HOLD_MORE;
  name_to_edit_button["RECORD_HOLD_LESS"] = EDIT_BUTTON_RECORD_HOLD_LESS;

  name_to_edit_button["RETURN_TO_EDIT"] = EDIT_BUTTON_RETURN_TO_EDIT;

  name_to_edit_button["INSERT"] = EDIT_BUTTON_INSERT;
  name_to_edit_button["DELETE"] = EDIT_BUTTON_DELETE;
  name_to_edit_button["INSERT_SHIFT_PAUSES"] = EDIT_BUTTON_INSERT_SHIFT_PAUSES;
  name_to_edit_button["DELETE_SHIFT_PAUSES"] = EDIT_BUTTON_DELETE_SHIFT_PAUSES;

  name_to_edit_button["OPEN_NEXT_STEPS"] = EDIT_BUTTON_OPEN_NEXT_STEPS;
  name_to_edit_button["OPEN_PREV_STEPS"] = EDIT_BUTTON_OPEN_PREV_STEPS;
  name_to_edit_button["PLAY_SAMPLE_MUSIC"] = EDIT_BUTTON_PLAY_SAMPLE_MUSIC;

  name_to_edit_button["BPM_UP"] = EDIT_BUTTON_BPM_UP;
  name_to_edit_button["BPM_DOWN"] = EDIT_BUTTON_BPM_DOWN;
  name_to_edit_button["STOP_UP"] = EDIT_BUTTON_STOP_UP;
  name_to_edit_button["STOP_DOWN"] = EDIT_BUTTON_STOP_DOWN;

  name_to_edit_button["DELAY_UP"] = EDIT_BUTTON_DELAY_UP;
  name_to_edit_button["DELAY_DOWN"] = EDIT_BUTTON_DELAY_DOWN;

  name_to_edit_button["OFFSET_UP"] = EDIT_BUTTON_OFFSET_UP;
  name_to_edit_button["OFFSET_DOWN"] = EDIT_BUTTON_OFFSET_DOWN;
  name_to_edit_button["SAMPLE_START_UP"] = EDIT_BUTTON_SAMPLE_START_UP;
  name_to_edit_button["SAMPLE_START_DOWN"] = EDIT_BUTTON_SAMPLE_START_DOWN;
  name_to_edit_button["SAMPLE_LENGTH_UP"] = EDIT_BUTTON_SAMPLE_LENGTH_UP;
  name_to_edit_button["SAMPLE_LENGTH_DOWN"] = EDIT_BUTTON_SAMPLE_LENGTH_DOWN;

  name_to_edit_button["ADJUST_FINE"] = EDIT_BUTTON_ADJUST_FINE;

  name_to_edit_button["SAVE"] = EDIT_BUTTON_SAVE;

  name_to_edit_button["UNDO"] = EDIT_BUTTON_UNDO;
  name_to_edit_button["REDO"] = EDIT_BUTTON_REDO;
  name_to_edit_button["COPY"] = EDIT_BUTTON_COPY;
  name_to_edit_button["CUT"] = EDIT_BUTTON_CUT;
  name_to_edit_button["PASTE"] = EDIT_BUTTON_PASTE;
  name_to_edit_button["TOGGLE_WAVEFORM"] = EDIT_BUTTON_TOGGLE_WAVEFORM;

  name_to_edit_button["ADD_COURSE_MODS"] = EDIT_BUTTON_ADD_COURSE_MODS;

  name_to_edit_button["SWITCH_PLAYERS"] = EDIT_BUTTON_SWITCH_PLAYERS;

  name_to_edit_button["SWITCH_TIMINGS"] = EDIT_BUTTON_SWITCH_TIMINGS;

  edit_mappings_device_input_.Clear();

  // Common mappings:

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_LINE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_UP);
  edit_mappings_menu_button_.button_[EDIT_BUTTON_SCROLL_UP_LINE][0] =
      GAME_BUTTON_UP;
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_LINE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_DOWN);
  edit_mappings_menu_button_.button_[EDIT_BUTTON_SCROLL_DOWN_LINE][0] =
      GAME_BUTTON_DOWN;

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_PAGE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGUP);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_PAGE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SEMICOLON);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_PAGE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGDN);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_PAGE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SQUOTE);

#if defined(MACOSX)
  // On macOS, Control+Up/Down are reserved for Mission Control
  // Use different keys instead
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SPEED_UP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_UP);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_UP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_UP][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SPEED_DOWN][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_DOWN);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_DOWN][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_DOWN][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
#else
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SPEED_UP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_UP);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_UP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_UP][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SPEED_DOWN][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_DOWN);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_DOWN][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_SPEED_DOWN][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
#endif

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SELECT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SELECT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SNAP_NEXT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LEFT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SNAP_PREV][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RIGHT);

#if defined(MACOSX)
  // On macOS, Control+PgUp/PgDn are reserved for Mission Control
  // Use Alt instead of Ctrl to avoid conflicts
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_UP_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_UP_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGUP);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SEMICOLON);

  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_DOWN_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_DOWN_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGDN);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SQUOTE);
#else
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_UP_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_UP_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGUP);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_UP_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SEMICOLON);

  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_DOWN_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SCROLL_DOWN_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_TS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PGDN);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_DOWN_TS][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SQUOTE);
#endif

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_HOME][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_HOME);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_END][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_END);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_NEXT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PERIOD);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_PREV][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_COMMA);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SEGMENT_NEXT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_PERIOD);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SEGMENT_NEXT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SEGMENT_NEXT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SEGMENT_PREV][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_COMMA);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SEGMENT_PREV][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SEGMENT_PREV][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SELECT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_SCROLL_SELECT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);

  edit_mappings_device_input_.button_[EDIT_BUTTON_LAY_SELECT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SPACE);

  edit_mappings_device_input_.button_[EDIT_BUTTON_PLAY_FROM_START][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cp);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PLAY_FROM_START][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PLAY_FROM_START][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
  edit_mappings_device_input_.button_[EDIT_BUTTON_PLAY_FROM_CURSOR][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cp);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PLAY_FROM_CURSOR][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PLAY_FROM_CURSOR][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_PLAY_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cp);

  // EditMode-specific mappings, for overriding options that don't make sense in
  // one mode vs. another.
  switch (EDIT_MODE.GetValue()) {
    case EditMode_Practice:
      // F1 = Show help popup
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_INPUT_HELP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F1);

      // Esc = Show Edit Menu
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
      edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
          GAME_BUTTON_START;
      edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][1] =
          GAME_BUTTON_BACK;

      // Escape, Enter = exit play/record
      play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ENTER);
      play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
      play_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
          GAME_BUTTON_BACK;
      return;
    case EditMode_CourseMods:
      // Left/Right = Snap to Next/Prev
      edit_mappings_device_input_.button_[EDIT_BUTTON_SNAP_NEXT][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LEFT);
      edit_mappings_device_input_.button_[EDIT_BUTTON_SNAP_PREV][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RIGHT);

      // k = course playback menu
      // m_EditMappingsDeviceInput.button[EDIT_BUTTON_OPEN_COURSE_ATTACK_MENU][0]
      // = DeviceInput(DEVICE_KEYBOARD, KEY_Cv);
      edit_mappings_device_input_.button_[EDIT_BUTTON_ADD_COURSE_MODS][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Co);
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_COURSE_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Ck);

      // F1 = Show help popup
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_INPUT_HELP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F1);

      // Esc = Show Edit Menu
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
      edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
          GAME_BUTTON_START;
      edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][1] =
          GAME_BUTTON_BACK;

      // Escape, Enter = exit play/record
      play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ENTER);
      play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
      play_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
          GAME_BUTTON_START;
      return;
    case EditMode_Full:
      // F4 = Show timing menu
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_TIMING_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F4);

      /* Don't allow F5/F6 in home mode. It breaks the "delay creation until
       * first save" logic. */
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_PREV_STEPS][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F5);
      edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_NEXT_STEPS][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F6);
      edit_mappings_device_input_.button_[EDIT_BUTTON_BPM_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F7);
      edit_mappings_device_input_.button_[EDIT_BUTTON_BPM_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F8);
      edit_mappings_device_input_.button_[EDIT_BUTTON_STOP_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F9);
      edit_mappings_device_input_.button_[EDIT_BUTTON_STOP_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F10);

      edit_mappings_device_input_.button_[EDIT_BUTTON_DELAY_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F9);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELAY_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELAY_DOWN][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);
      edit_mappings_device_input_.button_[EDIT_BUTTON_DELAY_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F10);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELAY_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELAY_UP][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);

      edit_mappings_device_input_.button_[EDIT_BUTTON_OFFSET_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F11);
      edit_mappings_device_input_.button_[EDIT_BUTTON_OFFSET_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_F12);

      edit_mappings_device_input_.button_[EDIT_BUTTON_SAMPLE_START_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RBRACKET);
      edit_mappings_device_input_.button_[EDIT_BUTTON_SAMPLE_START_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LBRACKET);
      edit_mappings_device_input_.button_[EDIT_BUTTON_SAMPLE_LENGTH_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RBRACKET);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_SAMPLE_LENGTH_UP][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_SAMPLE_LENGTH_UP][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);
      edit_mappings_device_input_.button_[EDIT_BUTTON_SAMPLE_LENGTH_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LBRACKET);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_SAMPLE_LENGTH_DOWN][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_SAMPLE_LENGTH_DOWN][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);

      edit_mappings_device_input_.button_[EDIT_BUTTON_PLAY_SAMPLE_MUSIC][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cl);

      edit_mappings_device_input_
          .button_[EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cb);
      edit_mappings_device_input_
          .button_[EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cb);
      edit_mappings_device_input_
          .hold_[EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
      edit_mappings_device_input_
          .hold_[EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);
      // m_EditMappingsDeviceInput.button[EDIT_BUTTON_ADD_STEP_MODS][0] =
      // DeviceInput(DEVICE_KEYBOARD, KEY_Cc);
      // m_EditMappingsDeviceInput.button[EDIT_BUTTON_OPEN_STEP_ATTACK_MENU][0]
      // = DeviceInput(DEVICE_KEYBOARD, KEY_Cv);

      edit_mappings_device_input_.button_[EDIT_BUTTON_INSERT_SHIFT_PAUSES][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_INSERT);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_INSERT_SHIFT_PAUSES][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_INSERT_SHIFT_PAUSES][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
      edit_mappings_device_input_.button_[EDIT_BUTTON_DELETE_SHIFT_PAUSES][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_DEL);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELETE_SHIFT_PAUSES][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
      edit_mappings_device_input_.hold_[EDIT_BUTTON_DELETE_SHIFT_PAUSES][1] =
          DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

      edit_mappings_device_input_.button_[EDIT_BUTTON_RECORD_HOLD_LESS][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cq);
      edit_mappings_device_input_.button_[EDIT_BUTTON_RECORD_HOLD_MORE][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cw);
      edit_mappings_device_input_.button_[EDIT_BUTTON_RECORD_HOLD_RESET][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Ce);
      edit_mappings_device_input_.button_[EDIT_BUTTON_RECORD_HOLD_OFF][0] =
          DeviceInput(DEVICE_KEYBOARD, KEY_Cr);
      break;
    default:
      break;
  }

  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_0][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C1);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_1][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C2);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_2][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C3);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_3][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C4);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_4][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C5);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_5][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C6);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_6][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C7);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_7][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C8);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_8][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C9);
  edit_mappings_device_input_.button_[EDIT_BUTTON_COLUMN_9][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_C0);

  edit_mappings_device_input_.button_[EDIT_BUTTON_RIGHT_SIDE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_RIGHT_SIDE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_LAY_ROLL][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_LAY_ROLL][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);

  edit_mappings_device_input_.button_[EDIT_BUTTON_CYCLE_TAP_LEFT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cn);
  edit_mappings_device_input_.button_[EDIT_BUTTON_CYCLE_TAP_RIGHT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cm);

  edit_mappings_device_input_.button_[EDIT_BUTTON_CYCLE_SEGMENT_LEFT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cn);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CYCLE_SEGMENT_LEFT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CYCLE_SEGMENT_LEFT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_CYCLE_SEGMENT_RIGHT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cm);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CYCLE_SEGMENT_RIGHT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CYCLE_SEGMENT_RIGHT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
  edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][0] =
      GAME_BUTTON_START;
  edit_mappings_menu_button_.button_[EDIT_BUTTON_OPEN_EDIT_MENU][1] =
      GAME_BUTTON_BACK;
  edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_AREA_MENU][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ENTER);
  edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_ALTER_MENU][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Ca);
  edit_mappings_device_input_.button_[EDIT_BUTTON_OPEN_INPUT_HELP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_F1);

  edit_mappings_device_input_
      .button_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cb);
  edit_mappings_device_input_
      .hold_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  edit_mappings_device_input_
      .hold_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  edit_mappings_device_input_
      .button_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cb);
  edit_mappings_device_input_
      .hold_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_
      .hold_[EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_RECORD_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cr);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_RECORD_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_RECORD_SELECTION][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_INSERT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_INSERT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_INSERT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_BACKSLASH);
  edit_mappings_device_input_.button_[EDIT_BUTTON_DELETE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_DEL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_ADJUST_FINE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  edit_mappings_device_input_.button_[EDIT_BUTTON_ADJUST_FINE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);

  edit_mappings_device_input_.button_[EDIT_BUTTON_SAVE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cs);
#if defined(MACOSX)
  /* use cmd */
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SAVE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LMETA);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SAVE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RMETA);
#else
  /* use ctrl */
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SAVE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_SAVE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
#endif

  edit_mappings_device_input_.button_[EDIT_BUTTON_UNDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cz);
  edit_mappings_device_input_.button_[EDIT_BUTTON_UNDO][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cu);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_UNDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_UNDO][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_REDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cy);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_REDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_REDO][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_CUT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cx);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CUT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_CUT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_COPY][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cc);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_COPY][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_COPY][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_PASTE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cv);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PASTE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_PASTE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  edit_mappings_device_input_.button_[EDIT_BUTTON_TOGGLE_WAVEFORM][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cg);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_TOGGLE_WAVEFORM][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  edit_mappings_device_input_.hold_[EDIT_BUTTON_TOGGLE_WAVEFORM][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  // Switch players, if it makes sense to do so.
  edit_mappings_device_input_.button_[EDIT_BUTTON_SWITCH_PLAYERS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_SLASH);

  // Allow song and step timing to be swapped.
  edit_mappings_device_input_.button_[EDIT_BUTTON_SWITCH_TIMINGS][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Ct);

  play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ENTER);
  play_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
  play_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
      GAME_BUTTON_BACK;
  play_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
      GAME_BUTTON_START;

  record_mappings_device_input_.button_[EDIT_BUTTON_LAY_ROLL][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT);
  record_mappings_device_input_.button_[EDIT_BUTTON_LAY_ROLL][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT);
  record_mappings_device_input_.button_[EDIT_BUTTON_REMOVE_NOTE][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LALT);
  record_mappings_device_input_.button_[EDIT_BUTTON_REMOVE_NOTE][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RALT);
  record_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
  record_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ENTER);
  record_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
      GAME_BUTTON_BACK;
  record_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
      GAME_BUTTON_START;

  record_paused_mappings_device_input_.button_[EDIT_BUTTON_PLAY_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cp);
  record_paused_mappings_device_input_
      .button_[EDIT_BUTTON_RECORD_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cr);
  record_paused_mappings_device_input_.hold_[EDIT_BUTTON_RECORD_SELECTION][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  record_paused_mappings_device_input_.hold_[EDIT_BUTTON_RECORD_SELECTION][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);
  record_paused_mappings_device_input_
      .button_[EDIT_BUTTON_RECORD_FROM_CURSOR][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cr);
  record_paused_mappings_device_input_.button_[EDIT_BUTTON_RETURN_TO_EDIT][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_ESC);
  record_paused_mappings_menu_button_.button_[EDIT_BUTTON_RETURN_TO_EDIT][1] =
      GAME_BUTTON_BACK;
  record_paused_mappings_device_input_.button_[EDIT_BUTTON_UNDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_Cu);
  record_paused_mappings_device_input_.hold_[EDIT_BUTTON_UNDO][0] =
      DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL);
  record_paused_mappings_device_input_.hold_[EDIT_BUTTON_UNDO][1] =
      DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL);

  IniFile mapping_ini;
  // Only use the mappings file if it exists.  It's meant to be optional, and
  // only used in rare cases like someone having critical keys broken. -Kyz
  if (mapping_ini.ReadFile(SpecialFiles::EDIT_MODE_KEYMAPS_PATH)) {
    LoadKeymapSectionIntoMappingsMember(
        mapping_ini.GetChild("Edit"), edit_mappings_device_input_);
    LoadKeymapSectionIntoMappingsMember(
        mapping_ini.GetChild("Play"), play_mappings_device_input_);
    LoadKeymapSectionIntoMappingsMember(
        mapping_ini.GetChild("Record"), record_mappings_device_input_);
    LoadKeymapSectionIntoMappingsMember(
        mapping_ini.GetChild("RecordPaused"),
        record_paused_mappings_device_input_);
  }
}

void ScreenEdit::LoadKeymapSectionIntoMappingsMember(
    const XNode* section, MapEditToDI& mappings) {
  if (section == nullptr) {
    return;
  }  // Not an error, sections are optional. -Kyz
  FOREACH_CONST_Attr(section, attr) {
    std::map<std::string, EditButton>::iterator name_entry =
        name_to_edit_button.find(attr->first);
    if (name_entry != name_to_edit_button.end()) {
      std::string joined_names;
      attr->second->GetValue(joined_names);
      std::vector<std::string> key_names;
      split(joined_names, DEVICE_INPUT_SEPARATOR, key_names, false);
      for (size_t k = 0; k < key_names.size() && k < NUM_EDIT_TO_DEVICE_SLOTS;
           ++k) {
        DeviceInput devi;
        devi.FromString(key_names[k]);
        if (devi.IsValid()) {
          mappings.button_[name_entry->second][k] = devi;
        }
      }
    }
  }
}

/* Given a DeviceInput that was just depressed, return an active edit function.
 */
EditButton ScreenEdit::DeviceToEdit(const DeviceInput& DeviceI) const {
  ASSERT(DeviceI.IsValid());

  const MapEditToDI* pCurrentMap = GetCurrentDeviceInputMap();

  /* First, search to see if a key that requires a modifier is pressed. */
  FOREACH_EditButton(e) {
    for (int slot = 0; slot < NUM_EDIT_TO_DEVICE_SLOTS; ++slot) {
      if (pCurrentMap->button_[e][slot] == DeviceI &&
          pCurrentMap->hold_[e][0].IsValid()) {
        /* The button maps to this function, and has one or more shift modifiers
         * attached. */
        for (int holdslot = 0; holdslot < NUM_EDIT_TO_DEVICE_SLOTS;
             ++holdslot) {
          DeviceInput hDI = pCurrentMap->hold_[e][holdslot];
          if (hDI.IsValid() && INPUTFILTER->IsBeingPressed(hDI)) {
            return e;
          }
        }
      }
    }
  }

  /* No shifted keys matched.  See if any unshifted inputs are bound to this
   * key. */
  FOREACH_EditButton(e) {
    for (int slot = 0; slot < NUM_EDIT_TO_DEVICE_SLOTS; ++slot) {
      if (pCurrentMap->button_[e][slot] == DeviceI &&
          !pCurrentMap->hold_[e][0].IsValid()) {
        /* The button maps to this function. */
        return e;
      }
    }
  }

  return EditButton_Invalid;
}

/* Given a DeviceInput that was just depressed, return an active edit function.
 */
EditButton ScreenEdit::MenuButtonToEditButton(GameButton MenuI) const {
  if (MenuI == GameButton_Invalid) {
    return EditButton_Invalid;
  }

  const MapEditButtonToMenuButton* pCurrentMap = GetCurrentMenuButtonMap();

  FOREACH_EditButton(e) {
    for (int slot = 0; slot < NUM_EDIT_TO_MENU_SLOTS; ++slot) {
      if (pCurrentMap->button_[e][slot] == MenuI) {
        /* The button maps to this function. */
        return e;
      }
    }
  }

  return EditButton_Invalid;
}

/* If DeviceI was just pressed, return true if button is triggered.  (More than
 * one function may be mapped to a key.) */
bool ScreenEdit::EditPressed(EditButton button, const DeviceInput& DeviceI) {
  ASSERT(DeviceI.IsValid());

  const MapEditToDI* pCurrentMap = GetCurrentDeviceInputMap();

  /* First, search to see if a key that requires a modifier is pressed. */
  bool bPrimaryButtonPressed = false;
  for (int slot = 0; slot < NUM_EDIT_TO_DEVICE_SLOTS; ++slot) {
    if (pCurrentMap->button_[button][slot] == DeviceI) {
      bPrimaryButtonPressed = true;
    }
  }

  if (!bPrimaryButtonPressed) {
    return false;
  }

  /* The button maps to this function.  Does the function has one or more shift
   * modifiers attached? */
  if (!pCurrentMap->hold_[button][0].IsValid()) {
    return true;
  }

  for (int holdslot = 0; holdslot < NUM_EDIT_TO_DEVICE_SLOTS; ++holdslot) {
    DeviceInput hDI = pCurrentMap->hold_[button][holdslot];
    if (INPUTFILTER->IsBeingPressed(hDI)) {
      return true;
    }
  }

  /* No shifted keys matched. */
  return false;
}

bool ScreenEdit::EditToDevice(
    EditButton button, int iSlotNum, DeviceInput& DeviceI) const {
  ASSERT(iSlotNum < NUM_EDIT_TO_DEVICE_SLOTS);
  const MapEditToDI* pCurrentMap = GetCurrentDeviceInputMap();
  if (pCurrentMap->button_[button][iSlotNum].IsValid()) {
    DeviceI = pCurrentMap->button_[button][iSlotNum];
  } else if (pCurrentMap->hold_[button][iSlotNum].IsValid()) {
    DeviceI = pCurrentMap->hold_[button][iSlotNum];
  }
  return DeviceI.IsValid();
}

bool ScreenEdit::EditIsBeingPressed(EditButton button) const {
  for (int slot = 0; slot < NUM_EDIT_TO_DEVICE_SLOTS; ++slot) {
    DeviceInput DeviceI;
    if (EditToDevice(button, slot, DeviceI) &&
        INPUTFILTER->IsBeingPressed(DeviceI)) {
      return true;
    }
  }

  return false;
}

const MapEditToDI* ScreenEdit::GetCurrentDeviceInputMap() const {
  switch (edit_state_) {
    DEFAULT_FAIL(edit_state_);
    case STATE_EDITING:
      return &edit_mappings_device_input_;
    case STATE_PLAYING:
      return &play_mappings_device_input_;
    case STATE_RECORDING:
      return &record_mappings_device_input_;
    case STATE_RECORDING_PAUSED:
      return &record_paused_mappings_device_input_;
  }
}

const MapEditButtonToMenuButton* ScreenEdit::GetCurrentMenuButtonMap() const {
  switch (edit_state_) {
    DEFAULT_FAIL(edit_state_);
    case STATE_EDITING:
      return &edit_mappings_menu_button_;
    case STATE_PLAYING:
      return &play_mappings_menu_button_;
    case STATE_RECORDING:
      return &record_mappings_menu_button_;
    case STATE_RECORDING_PAUSED:
      return &record_paused_mappings_menu_button_;
  }
}

enum RowCountChoice {
  RCC_4M,
  RCC_2M,
  RCC_1M,
  RCC_2ND,
  RCC_4TH,
  RCC_8TH,
  RCC_12TH,
  RCC_16TH,
  RCC_24TH,
  RCC_32ND,
  RCC_48TH,
  RCC_64TH,
  RCC_192ND,
};

#define RCC_CHOICES                                                       \
  RCC_4TH, "4m", "2m", "1m", "2nd", "4th", "8th", "12th", "16th", "24th", \
      "32nd", "48th", "64th", "192nd"

int GetRowsFromAnswers(int choice, const std::vector<int>& answers);
int GetRowsFromAnswers(int choice, const std::vector<int>& answers) {
  if (answers.empty()) {
    return 192 / 8;
  }
  switch (answers[choice]) {
    case RCC_4M:
      return 192 * 4;
    case RCC_2M:
      return 192 * 2;
    case RCC_1M:
      return 192;
    case RCC_2ND:
      return 192 / 2;
    case RCC_4TH:
    default:
      return 192 / 4;
    case RCC_8TH:
      return 192 / 8;
    case RCC_12TH:
      return 192 / 12;
    case RCC_16TH:
      return 192 / 16;
    case RCC_24TH:
      return 192 / 24;
    case RCC_32ND:
      return 192 / 32;
    case RCC_48TH:
      return 192 / 48;
    case RCC_64TH:
      return 3;
    case RCC_192ND:
      return 1;
  }
}

static MenuDef g_EditHelp(
    "ScreenMiniMenuEditHelp"
    // fill this in dynamically
);

static MenuDef g_AttackAtTimeMenu(
    "ScreenMiniMenuAttackAtTimeMenu"
    // fill this in dynamically
);

static MenuDef g_IndividualAttack(
    "ScreenMiniMenuIndividualAttack"
    // fill this in dynamically
);

static MenuDef g_KeysoundTrack(
    "ScreenMiniMenuKeysoundTrack");  // fill this in dynamically

static MenuDef g_AdjustSyncMenu(
    "ScreenMiniMenuAdjustSyncMenu"
    // fill this in dynamically
);

static LocalizedString DETECTING_BPM(
    "ScreenEdit", "Analyzing music, this may take a moment...");
static LocalizedString DETECT_BPM_FAILED(
    "ScreenEdit", "Could not analyze the music: %s");
static LocalizedString DETECT_BPM_NO_RESULTS(
    "ScreenEdit", "No BPM could be detected for this music.");
static LocalizedString APPLIED_SYNC(
    "ScreenEdit", "Applied %.3f BPM with an offset of %+.3f.");

static MenuDef g_MainMenu(
    "ScreenMiniMenuMainMenu",
    MenuRowDef(
        ScreenEdit::play_whole_song, "Play whole song", true, EditMode_Practice,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::play_current_beat_to_end, "Play current beat to end", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::save, "Save", true, EditMode_Home, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::play_selection, "Play selection", true, EditMode_Practice,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::set_selection_start, "Set selection start", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::set_selection_end, "Set selection end", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::revert_to_last_save, "Revert to last save", true,
        EditMode_Home, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::revert_from_disk, "Revert from disk", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::options, "Editor options", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::edit_song_info, "Edit song info", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::edit_steps_information, "Edit steps information", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::edit_timing_data, "Edit Timing Data", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::adjust_sync, "Adjust Sync", true, EditMode_Full, true, true,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::view_steps_data, "View steps data", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::play_preview_music, "Play preview music", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::exit, "Exit Edit Mode", true, EditMode_Practice, true, true,
        0, nullptr));

static MenuDef g_AlterMenu(
    "ScreenMiniMenuAlterMenu",
    MenuRowDef(
        ScreenEdit::cut, "Cut", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::copy, "Copy", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::clear, "Clear area", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::quantize, "Quantize", true, EditMode_Practice, true, true,
        0, "4th", "8th", "12th", "16th", "24th", "32nd", "48th", "64th",
        "192nd"),
    MenuRowDef(
        ScreenEdit::turn, "Turn", true, EditMode_Practice, true, true, 0,
        "Left", "Right", "Mirror", "LRMirror", "UDMirror", "Backwards",
        "Shuffle", "SuperShuffle", "HyperShuffle"),
    MenuRowDef(
        ScreenEdit::transform, "Transform", true, EditMode_Practice, true, true,
        0, "NoHolds", "NoMines", "Little", "Wide", "Big", "Quick", "Skippy",
        "Mines", "Echo", "Stomp", "Planted", "Floored", "Twister", "NoJumps",
        "NoHands", "NoQuads", "NoStretch"),
    MenuRowDef(
        ScreenEdit::alter, "Alter", true, EditMode_Practice, true, true, 0,
        "Autogen To Fill Width", "Backwards", "Swap Sides",
        "Copy Left To Right", "Copy Right To Left", "Clear Left", "Clear Right",
        "Collapse To One", "Collapse Left", "Shift Left", "Shift Right",
        "Swap Up/Down", "Arbitrary Remap Columns"),
    MenuRowDef(
        ScreenEdit::tempo, "Tempo", true, EditMode_Full, true, true, 0,
        "Compress 2x", "Compress 3->2", "Compress 4->3", "Expand 3->4",
        "Expand 2->3", "Expand 2x"),
    MenuRowDef(
        ScreenEdit::play, "Play selection", true, EditMode_Practice, true, true,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::record, "Record in selection", true, EditMode_Practice,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::preview_designation, "Designate as Music Preview", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_to_pause, "Convert selection to pause", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_to_delay, "Convert selection to delay", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_to_warp, "Convert selection to warp", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_to_fake, "Convert selection to fake", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_to_attack, "Convert selection to attack", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::routine_invert_notes, "Invert notes' player", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::routine_mirror_1_to_2, "Mirror Player 1 to 2", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::routine_mirror_2_to_1, "Mirror Player 2 to 1", true,
        EditMode_Full, true, true, 0, nullptr));

static MenuDef g_AreaMenu(
    "ScreenMiniMenuAreaMenu",
    MenuRowDef(
        ScreenEdit::paste_at_current_beat, "Paste at current beat", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::paste_at_begin_marker, "Paste at begin marker", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::insert_and_shift, "Insert beat and shift down", true,
        EditMode_Practice, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::delete_and_shift, "Delete beat and shift up", true,
        EditMode_Practice, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::shift_pauses_forward, "Shift all timing changes down", true,
        EditMode_Full, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::shift_pauses_backward, "Shift all timing changes up", true,
        EditMode_Full, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::convert_pause_to_beat, "Convert pause to beats", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::convert_delay_to_beat, "Convert delay to beats", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::last_second_at_beat,
        "Designate last second at current beat", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::undo, "Undo", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::clear_clipboard, "Clear clipboard", true, EditMode_Practice,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::modify_attacks_at_row, "Modify Attacks at current beat",
        true, EditMode_CourseMods, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::modify_keysounds_at_row, "Modify Keysounds at current beat",
        true, EditMode_Full, true, true, 0, nullptr)

);

static MenuDef g_StepsInformation(
    "ScreenMiniMenuStepsInformation",
    MenuRowDef(
        ScreenEdit::difficulty, "Difficulty", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::meter, "Meter", true, EditMode_Practice, true, false, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::predict_meter, "Predicted Meter", false, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::chartname, "Chart Name", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::description, "Description", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::chartstyle, "Chart Style", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::step_credit, "Step Author", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::step_display_bpm, "Display BPM", true, EditMode_Full, true,
        true, 0, "Actual", "Specified", "Random"),
    MenuRowDef(
        ScreenEdit::step_min_bpm, "Min BPM", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::step_max_bpm, "Max BPM", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::step_music, "Music File", true, EditMode_Full, true, true,
        0, nullptr));

static MenuDef g_StepsData(
    "ScreenMiniMenuStepsData",
    MenuRowDef(
        ScreenEdit::tap_notes, "Tap Steps", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::jumps, "Jumps", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::hands, "Hands", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::quads, "Quads", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::holds, "Holds", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::mines, "Mines", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::rolls, "Rolls", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::lifts, "Lifts", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::fakes, "Fakes", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::stream, "Stream", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::voltage, "Voltage", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::air, "Air", false, EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::freeze, "Freeze", false, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::chaos, "Chaos", false, EditMode_Full, true, true, 0,
        nullptr));

static MenuDef g_SongInformation(
    "ScreenMiniMenuSongInformation",
    MenuRowDef(
        ScreenEdit::main_title, "Main title", true, EditMode_Practice, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::sub_title, "Sub title", true, EditMode_Practice, true, true,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::artist, "Artist", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::genre, "Genre", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::credit, "Credit", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::preview, "Preview", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::main_title_transliteration, "Main title transliteration",
        true, EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::sub_title_transliteration, "Sub title transliteration",
        true, EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::artist_transliteration, "Artist transliteration", true,
        EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::last_second_hint, "Last second hint", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::preview_start, "Preview Start", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::preview_length, "Preview Length", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::display_bpm, "Display BPM", true, EditMode_Full, true, true,
        0, "Actual", "Specified", "Random"),
    MenuRowDef(
        ScreenEdit::min_bpm, "Min BPM", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::max_bpm, "Max BPM", true, EditMode_Full, true, true, 0,
        nullptr));

// Ugh, I don't like making this global pointer to clipboardFullTiming, but
// it's the only way to make it visible to EnabledIfClipboardTimingIsSafe for
// making sure it's safe to paste as the timing data for the Steps/Song. -Kyz
static TimingData* clipboard_full_timing = nullptr;
static bool EnabledIfClipboardTimingIsSafe();
static bool EnabledIfClipboardTimingIsSafe() {
  return clipboard_full_timing != nullptr &&
         clipboard_full_timing->IsSafeFullTiming();
}
static MenuDef g_TimingDataInformation(
    "ScreenMiniMenuTimingDataInformation",
    MenuRowDef(
        ScreenEdit::beat_0_offset, "Beat 0 Offset", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::bpm, "Edit BPM change", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::stop, "Edit stop", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::delay, "Edit delay", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::label, "Edit label", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::tickcount, "Edit tickcount", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::combo, "Edit combo", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::warp, "Edit warp", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::speed_percent, "Edit speed (percent)", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::speed_wait, "Edit speed (wait)", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::speed_mode, "Edit speed (mode)", true, EditMode_Full, true,
        true, 0, "Beats", "Seconds"),
    MenuRowDef(
        ScreenEdit::scroll, "Edit scrolling factor", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::fake, "Edit fake", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::shift_timing_in_region_down, "Shift timing in region down",
        true, EditMode_Full, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::shift_timing_in_region_up, "Shift timing in region up",
        true, EditMode_Full, true, true, RCC_CHOICES),
    MenuRowDef(
        ScreenEdit::copy_timing_in_region, "Copy timing in region", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::clear_timing_in_region, "Clear timing in region", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::paste_timing_from_clip, "Paste timing from clipboard", true,
        EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::copy_full_timing, "Copy timing data", true, EditMode_Full,
        true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::paste_full_timing, "Paste timing data",
        EnabledIfClipboardTimingIsSafe, EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::erase_step_timing, "Erase step timing", true, EditMode_Full,
        true, true, 0, nullptr));

static MenuDef g_TimingDataChangeInformation(
    "ScreenMiniMenuTimingDataChangeInformation",
    MenuRowDef(
        ScreenEdit::timing_all, "All timing", true, EditMode_Full, true, true,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_bpm, "BPM changes", true, EditMode_Full, true, true,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_stop, "Stops", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::timing_delay, "Delays", true, EditMode_Full, true, true, 0,
        nullptr),
    // Time signatures disabled because they don't fully work. -Kyz
    // MenuRowDef(ScreenEdit::timing_time_sig,
    // 	"Time Signatures",
    // 	true, EditMode_Full, true, true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_warp, "Warps", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::timing_label, "Labels", true, EditMode_Full, true, true, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::timing_tickcount, "Tickcount", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_combo, "Combo segments", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_speed, "Speed segments", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_scroll, "Scroll segments", true, EditMode_Full, true,
        true, 0, nullptr),
    MenuRowDef(
        ScreenEdit::timing_fake, "Fakes", true, EditMode_Full, true, true, 0,
        nullptr));

// XXX: What are these enums used for?
enum {
  song_bganimation,
  song_movie,
  song_bitmap,
  global_bganimation,
  global_movie,
  global_movie_song_group,
  global_movie_song_group_and_genre,
  dynamic_random,
  baked_random,
  none
};
static bool EnabledIfSet1SongBGAnimation();
static bool EnabledIfSet1SongMovie();
static bool EnabledIfSet1SongBitmap();
static bool EnabledIfSet1GlobalBGAnimation();
static bool EnabledIfSet1GlobalMovie();
static bool EnabledIfSet1GlobalMovieSongGroup();
static bool EnabledIfSet1GlobalMovieSongGroupAndGenre();
static bool EnabledIfSet2SongBGAnimation();
static bool EnabledIfSet2SongMovie();
static bool EnabledIfSet2SongBitmap();
static bool EnabledIfSet2GlobalBGAnimation();
static bool EnabledIfSet2GlobalMovie();
static bool EnabledIfSet2GlobalMovieSongGroup();
static bool EnabledIfSet2GlobalMovieSongGroupAndGenre();
static MenuDef g_BackgroundChange(
    "ScreenMiniMenuBackgroundChange",
    MenuRowDef(
        ScreenEdit::layer, "Layer", false, EditMode_Full, true, false, 0, ""),
    MenuRowDef(
        ScreenEdit::rate, "Rate", true, EditMode_Full, true, false, 10, "0%",
        "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%",
        "120%", "140%", "160%", "180%", "200%", "220%", "240%", "260%", "280%",
        "300%", "350%", "400%"),
    MenuRowDef(
        ScreenEdit::transition, "Force Transition", true, EditMode_Full, true,
        false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::effect, "Force Effect", true, EditMode_Full, true, false, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::color1, "Force Color 1", true, EditMode_Full, true, false,
        0, "-", "#FFFFFF", "#808080", "#FFFFFF80", "#000000", "#FF0000",
        "#00FF00", "#0000FF", "#FFFF00", "#00FFFF", "#FF00FF"),
    MenuRowDef(
        ScreenEdit::color2, "Force Color 2", true, EditMode_Full, true, false,
        0, "-", "#FFFFFF", "#808080", "#FFFFFF80", "#000000", "#FF0000",
        "#00FF00", "#0000FF", "#FFFF00", "#00FFFF", "#FF00FF"),
    MenuRowDef(
        ScreenEdit::file1_type, "File1 Type", true, EditMode_Full, true, true,
        0, "Song BGAnimation", "Song Movie", "Song Bitmap",
        "Global BGAnimation", "Global Movie", "Global Movie from Song Group",
        "Global Movie from Song Group and Genre", "Dynamic Random",
        "Baked Random", "None"),
    MenuRowDef(
        ScreenEdit::file1_song_bganimation, "File1 Song BGAnimation",
        EnabledIfSet1SongBGAnimation, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file1_song_movie, "File1 Song Movie",
        EnabledIfSet1SongMovie, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file1_song_still, "File1 Song Still",
        EnabledIfSet1SongBitmap, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file1_global_bganimation, "File1 Global BGAnimation",
        EnabledIfSet1GlobalBGAnimation, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file1_global_movie, "File1 Global Movie",
        EnabledIfSet1GlobalMovie, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file1_global_movie_song_group, "File1 Global Movie (Group)",
        EnabledIfSet1GlobalMovieSongGroup, EditMode_Full, true, false, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::file1_global_movie_song_group_and_genre,
        "File1 Global Movie (Group + Genre)",
        EnabledIfSet1GlobalMovieSongGroupAndGenre, EditMode_Full, true, false,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_type, "File2 Type", true, EditMode_Full, true, true,
        0, "Song BGAnimation", "Song Movie", "Song Bitmap",
        "Global BGAnimation", "Global Movie", "Global Movie from Song Group",
        "Global Movie from Song Group and Genre", "Dynamic Random",
        "Baked Random", "None"),
    MenuRowDef(
        ScreenEdit::file2_song_bganimation, "File2 Song BGAnimation",
        EnabledIfSet2SongBGAnimation, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_song_movie, "File2 Song Movie",
        EnabledIfSet2SongMovie, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_song_still, "File2 Song Still",
        EnabledIfSet2SongBitmap, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_global_bganimation, "File2 Global BGAnimation",
        EnabledIfSet2GlobalBGAnimation, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_global_movie, "File2 Global Movie",
        EnabledIfSet2GlobalMovie, EditMode_Full, true, false, 0, nullptr),
    MenuRowDef(
        ScreenEdit::file2_global_movie_song_group, "File2 Global Movie (Group)",
        EnabledIfSet2GlobalMovieSongGroup, EditMode_Full, true, false, 0,
        nullptr),
    MenuRowDef(
        ScreenEdit::file2_global_movie_song_group_and_genre,
        "File2 Global Movie (Group + Genre)",
        EnabledIfSet2GlobalMovieSongGroupAndGenre, EditMode_Full, true, false,
        0, nullptr),
    MenuRowDef(
        ScreenEdit::delete_change, "Remove Change", true, EditMode_Full, true,
        true, 0, nullptr));
static bool EnabledIfSet1SongBGAnimation() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] ==
             song_bganimation &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_song_bganimation]
              .choices_.empty();
}
static bool EnabledIfSet1SongMovie() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] == song_movie &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_song_movie]
              .choices_.empty();
}
static bool EnabledIfSet1SongBitmap() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] == song_bitmap &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_song_still]
              .choices_.empty();
}
static bool EnabledIfSet1GlobalBGAnimation() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] ==
             global_bganimation &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_global_bganimation]
              .choices_.empty();
}
static bool EnabledIfSet1GlobalMovie() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] ==
             global_movie &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_global_movie]
              .choices_.empty();
}
static bool EnabledIfSet1GlobalMovieSongGroup() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] ==
             global_movie_song_group &&
         !g_BackgroundChange.rows_[ScreenEdit::file1_global_movie_song_group]
              .choices_.empty();
}
static bool EnabledIfSet1GlobalMovieSongGroupAndGenre() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file1_type] ==
             global_movie_song_group_and_genre &&
         !g_BackgroundChange
              .rows_[ScreenEdit::file1_global_movie_song_group_and_genre]
              .choices_.empty();
}
static bool EnabledIfSet2SongBGAnimation() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] ==
             song_bganimation &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_song_bganimation]
              .choices_.empty();
}
static bool EnabledIfSet2SongMovie() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] == song_movie &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_song_movie]
              .choices_.empty();
}
static bool EnabledIfSet2SongBitmap() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] == song_bitmap &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_song_still]
              .choices_.empty();
}
static bool EnabledIfSet2GlobalBGAnimation() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] ==
             global_bganimation &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_global_bganimation]
              .choices_.empty();
}
static bool EnabledIfSet2GlobalMovie() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] ==
             global_movie &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_global_movie]
              .choices_.empty();
}
static bool EnabledIfSet2GlobalMovieSongGroup() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] ==
             global_movie_song_group &&
         !g_BackgroundChange.rows_[ScreenEdit::file2_global_movie_song_group]
              .choices_.empty();
}
static bool EnabledIfSet2GlobalMovieSongGroupAndGenre() {
  return ScreenMiniMenu::last_answers_[ScreenEdit::file2_type] ==
             global_movie_song_group_and_genre &&
         !g_BackgroundChange
              .rows_[ScreenEdit::file2_global_movie_song_group_and_genre]
              .choices_.empty();
}

static std::string GetOneBakedRandomFile(Song* pSong, bool bTryGenre = true) {
  std::vector<std::string> vsPaths;
  std::vector<std::string> vsNames;
  BackgroundUtil::GetGlobalRandomMovies(pSong, "", vsPaths, vsNames, bTryGenre);

  return vsNames.empty() ? std::string() : vsNames[RandomInt(vsNames.size())];
}

static MenuDef g_InsertTapAttack(
    "ScreenMiniMenuInsertTapAttack",
    MenuRowDef(
        -1, "Duration seconds", true, EditMode_Practice, true, false, 3, "5",
        "10", "15", "20", "25", "30", "35", "40", "45"),  // TODO: Replace
    MenuRowDef(
        -1, "Set modifiers", true, EditMode_Practice, true, true, 0,
        "Press Start"));

static MenuDef g_InsertCourseAttack(
    "ScreenMiniMenuInsertCourseAttack",
    MenuRowDef(
        ScreenEdit::duration, "Duration seconds", true, EditMode_Practice, true,
        false, 3, "5", "10", "15", "20", "25", "30", "35", "40", "45"),
    MenuRowDef(
        ScreenEdit::set_mods, "Set modifiers", true, EditMode_Practice, true,
        true, 0, "Press Start"),
    MenuRowDef(
        ScreenEdit::remove, "Remove", true, EditMode_Practice, true, true, 0,
        "Press Start"));

static MenuDef g_CourseMode(
    "ScreenMiniMenuCourseDisplay",
    MenuRowDef(
        -1, "Play mods from course", true, EditMode_Practice, true, false, 0,
        nullptr));

// HACK: need to remember the track we're inserting on so that we can lay the
// attack note after coming back from menus.
static int g_iLastInsertTapAttackTrack = -1;
static float g_fLastInsertAttackDurationSeconds = -1;
static float g_fLastInsertAttackPositionSeconds = -1;
static BackgroundLayer g_CurrentBGChangeLayer = BACKGROUND_LAYER_Invalid;

static void SetDefaultEditorNoteSkin(
    size_t num, std::string& sNameOut, std::string& defaultValueOut) {
  sNameOut = ssprintf("EditorNoteSkinP%d", int(num + 1));

  // XXX: We need more supported noteskins.
  defaultValueOut = "cel";
}

// In routine/couples (TwoPlayersSharedSides) default to a per-player noteskin
// configured via the RoutineEditorNoteSkinP1/P2 theme metrics
static std::string RoutineEditorNoteSkinName(size_t i) {
  return ssprintf("RoutineEditorNoteSkinP%i", int(i + 1));
}
static ThemeMetric1D<std::string> ROUTINE_EDITOR_NOTE_SKIN(
    "ScreenEdit", RoutineEditorNoteSkinName, NUM_PLAYERS);

static std::string GetRoutineEditorNoteSkin(PlayerNumber pn) {
  return ROUTINE_EDITOR_NOTE_SKIN.GetValue(pn);
}

static Preference1D<std::string> EDITOR_NOTE_SKINS(
    SetDefaultEditorNoteSkin, NUM_PLAYERS);

static ThemeMetric<std::string> EDIT_MODIFIERS("ScreenEdit", "EditModifiers");

static ThemeMetric<bool> LOOP_ON_CHART_END("ScreenEdit", "LoopOnChartEnd");

// Determines how fast the scroll wheel scrolls through the chart
static ThemeMetric<float> SCROLL_WHEEL_ACCELERATION_RATE(
    "ScreenEdit", "ScrollWheelAccelerationRate");

// Discrete speed steps used by EDIT_BUTTON_SCROLL_SPEED_UP/DOWN,
// In practice, this affects how much we can zoom the spacing of the chart
static ThemeMetric<std::string> SCROLL_SPEED_PRESETS_STR(
    "ScreenEdit", "ScrollSpeedPresets");

static ThemeMetric<bool> INVERT_SCROLL_BUTTONS(
    "ScreenEdit", "InvertScrollSpeedButtons");

// How fast the notefield's displayed scroll speed catches up to
// it's new value. Lower values will make it take longer to reach the new value
// Higher values will make it reach the new value faster.
static ThemeMetric<float> SCROLL_SPEED_APPROACH_RATE(
    "ScreenEdit", "ScrollSpeedApproachRate");

REGISTER_SCREEN_CLASS(ScreenEdit);

// Static so the clipboard (notes + full timing) persists across
// ScreenEdit instances, which lets Cut/Copy/Paste work between charts.
NoteData ScreenEdit::clipboard_;
TimingData ScreenEdit::clipboard_full_timing_;
bool ScreenEdit::clipboard_has_timing_ = false;

// Time in seconds to reset the scroll acceleration counter
const float ScreenEdit::kScrollAccelerationResetTime = 0.5f;
const float ScreenEdit::kMaxScrollAccelerationMultiplier = 8.0f;

// The cursor is reported in window pixels, so scale it the same way the Lua
// bindings do.
static float GetMouseScreenX() {
  return SCALE(
      INPUTFILTER->GetCursorX(), 0.0f,
      (float)(PREFSMAN->display_height_ * PREFSMAN->display_aspect_ratio_),
      SCREEN_LEFT, SCREEN_RIGHT);
}

static float GetMouseScreenY() {
  return SCALE(
      INPUTFILTER->GetCursorY(), 0.0f, (float)PREFSMAN->display_height_,
      SCREEN_TOP, SCREEN_BOTTOM);
}

float ScreenEdit::MouseYToBeat(float fScreenY) {
  const float fZoom = note_field_edit_.GetZoomY();

  if (std::abs(fZoom) < 0.0001f) {
    return GetBeat();
  }

  /*
   * The editor NoteField is drawn using m_fTrailingBeat rather than the
   * instantaneous editor cursor beat.  This is what gives scrolling its
   * smooth/trailing appearance.
   *
   * Mouse hit testing therefore has to temporarily use the same song
   * position that DrawPrimitives() uses, otherwise the beat under the
   * mouse will not match the arrows currently visible on screen.
   */
  const float fDisplayedBeat = trailing_beat_;

  PlayerState* pPlayerState =
      const_cast<PlayerState*>(note_field_edit_.GetPlayerState());

  /*
   * Save the real positions.
   *
   * DrawPrimitives() normally restores these after drawing the NoteField,
   * so by the time a mouse event happens they no longer represent what
   * was actually used to render the arrows.
   */
  const float fPlayerSongBeat = pPlayerState->position_.song_beat_;
  const float fPlayerSongBeatNoOffset =
      pPlayerState->position_.song_beat_no_offset_;
  const float fPlayerSongBeatVisible =
      pPlayerState->position_.song_beat_visible_;

  const float fGameSongBeat = GAMESTATE->position_.song_beat_;
  const float fGameSongBeatNoOffset = GAMESTATE->position_.song_beat_no_offset_;
  const float fGameSongBeatVisible = GAMESTATE->position_.song_beat_visible_;

  /*
   * Reproduce the exact position state used by DrawPrimitives().
   */
  pPlayerState->position_.song_beat_ = fDisplayedBeat;
  pPlayerState->position_.song_beat_no_offset_ = fDisplayedBeat;
  pPlayerState->position_.song_beat_visible_ = fDisplayedBeat;

  GAMESTATE->position_.song_beat_ = fDisplayedBeat;
  GAMESTATE->position_.song_beat_no_offset_ = fDisplayedBeat;
  GAMESTATE->position_.song_beat_visible_ = fDisplayedBeat;

  ArrowEffects::SetCurrentOptions(
      &player_state_edit_.player_options_.GetCurrent());

  const float fReverseOffsetPixels = PLAYER_HEIGHT * 2;

  /*
   * Convert from ScreenEdit coordinates into NoteField-local coordinates.
   */
  const float fLocalY = (fScreenY - note_field_edit_.GetY()) / fZoom;

  /*
   * Determine where the displayed beat and the next beat are actually
   * being drawn.
   */
  const float fY0 = ArrowEffects::GetYPos(
      pPlayerState, 0,
      ArrowEffects::GetYOffset(pPlayerState, 0, fDisplayedBeat),
      fReverseOffsetPixels);

  const float fY1 = ArrowEffects::GetYPos(
      pPlayerState, 0,
      ArrowEffects::GetYOffset(pPlayerState, 0, fDisplayedBeat + 1.0f),
      fReverseOffsetPixels);

  /*
   * Restore the real song positions immediately.
   */
  pPlayerState->position_.song_beat_ = fPlayerSongBeat;
  pPlayerState->position_.song_beat_no_offset_ = fPlayerSongBeatNoOffset;
  pPlayerState->position_.song_beat_visible_ = fPlayerSongBeatVisible;

  GAMESTATE->position_.song_beat_ = fGameSongBeat;
  GAMESTATE->position_.song_beat_no_offset_ = fGameSongBeatNoOffset;
  GAMESTATE->position_.song_beat_visible_ = fGameSongBeatVisible;

  const float fPixelsPerBeat = fY1 - fY0;

  if (std::abs(fPixelsPerBeat) < 0.0001f) {
    return fDisplayedBeat;
  }

  /*
   * Translate the mouse's Y position into the beat currently underneath it.
   */
  return fDisplayedBeat + ((fLocalY - fY0) / fPixelsPerBeat);
}

void ScreenEdit::Init() {
  sound_music_ = nullptr;
  mouse_dragging_ = false;
  mouse_drag_start_x_ = mouse_drag_start_y_ = 0;
  mouse_drag_current_x_ = mouse_drag_current_y_ = 0;
  tempo_detector_ = nullptr;

  // Initialize scroll acceleration tracking
  consecutive_wheel_scrolls_ = 0;
  scroll_acceleration_multiplier_ = 1.0f;

  // Parse the configurable scroll-speed presets; fall back to a sane default
  // if the metric is empty or malformed.
  scroll_speed_presets_.clear();
  std::vector<std::string> asPresets;
  split(SCROLL_SPEED_PRESETS_STR.GetValue(), ",", asPresets, true);
  for (const std::string& sPreset : asPresets) {
    scroll_speed_presets_.push_back(StringToFloat(sPreset));
  }
  if (scroll_speed_presets_.empty()) {
    scroll_speed_presets_ = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
  }

  GAMESTATE->is_using_step_timing_ = false;
  GAMESTATE->in_step_editor_ = true;
  // Is already zero if going into EditMode, but not always zero upon entering
  // PracticeMode
  GAMESTATE->position_.music_seconds_ = 0.0;

  SubscribeToMessage("Judgment");
  main_player_ = GAMESTATE->GetMasterPlayerNumber();

  ASSERT(GAMESTATE->cur_song_ != nullptr);
  ASSERT(GAMESTATE->cur_steps_[main_player_] != nullptr);

  // In TwoPlayersSharedSides (routine/couples) editing, both players share
  // the same chart. Ensure every enabled player's m_pCurSteps slot points at
  // the master player's Steps
  if (GAMESTATE->GetCurrentStyle(main_player_) != nullptr &&
      GAMESTATE->GetCurrentStyle(main_player_)->style_type_ ==
          StyleType_TwoPlayersSharedSides) {
    Steps* pSharedSteps = GAMESTATE->cur_steps_[main_player_];
    FOREACH_EnabledPlayer(pn) {
      if (GAMESTATE->cur_steps_[pn] != pSharedSteps) {
        GAMESTATE->cur_steps_[pn].Set(pSharedSteps);
      }
    }
  }

  EDIT_MODE.Load(name_, "EditMode");
  ScreenWithMenuElements::Init();

  InitEditMappings();

  current_cycle_segment_ = SEGMENT_LABEL;

  // save the originals for reverting later
  CopyToLastSave();

  current_action_ = MAIN_MENU_CHOICE_INVALID;
  input_player_number_ = PLAYER_INVALID;

  if (GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->style_type_ == StyleType_TwoPlayersSharedSides) {
    input_player_number_ = main_player_;
  }

  song_ = GAMESTATE->cur_song_;
  steps_ = GAMESTATE->cur_steps_[main_player_];

  /*	The user will most likely switch into Step Timing after laying down
          some initial notes. It also throws off many people at first glance.
  */
  if (song_->IsStepsUsingDifferentTiming(steps_)) {
    GAMESTATE->is_using_step_timing_ = true;
  }

  return_to_record_menu_after_play_ = false;
  beat_to_return_to_ = 0;

  selected_tap_ = TAP_ORIGINAL_TAP;

  GAMESTATE->gameplay_lead_in_.Set(true);
  GAMESTATE->edit_mode_ = EDIT_MODE.GetValue();
  SetBeat(0);
  trailing_beat_ = 0;

  shift_anchor_ = -1;
  start_playing_at_ = -1;
  stop_playing_at_ = -1;

  selected_attack_.reset();
  mod_in_process_ = -1;

  this->AddChild(&background_);

  snap_display_.SetXY(EDIT_X, PLAYER_Y_STANDARD);
  snap_display_.Load();
  // xxx: hardcoded command -aj
  snap_display_.SetZoom(SCREEN_HEIGHT / 480 * 0.5);
  this->AddChild(&snap_display_);

  // We keep track of this bit of state so that when the user is in Edit/Sync
  // Songs and makes a change to the NoteSkins, that change is "sticky" across
  // multiple ScreenEdits.  That is the way the rest of the options work.
  // TODO: It would be cleaner to do this by making it possible to set an option
  // in metrics.ini.
  const bool routine_edit =
      GAMESTATE->GetCurrentStyle(main_player_) != nullptr &&
      GAMESTATE->GetCurrentStyle(main_player_)->style_type_ ==
          StyleType_TwoPlayersSharedSides;

  if (!GAMESTATE->did_mode_change_note_skin_) {
    GAMESTATE->did_mode_change_note_skin_ = true;
    FOREACH_PlayerNumber(pn) {
      std::string sNoteSkin = routine_edit ? GetRoutineEditorNoteSkin(pn)
                                           : EDITOR_NOTE_SKINS[pn].Get();
      if (!NOTESKIN->DoesNoteSkinExist(sNoteSkin)) {
        // Fall back to the user's editor preference if the routine variant
        // is missing (e.g. user removed the couples noteskin).
        sNoteSkin = EDITOR_NOTE_SKINS[pn].Get();
      }
      if (NOTESKIN->DoesNoteSkinExist(sNoteSkin)) {
        PO_GROUP_ASSIGN(
            GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Preferred,
            note_skin_, sNoteSkin);
      }
    }
  }

  player_state_edit_.SetPlayerNumber(main_player_);
  player_state_edit_.notefield_zoom_ = 1.0f;

  // If we always go with the GAMESTATE NoteSkin, we will have fun effects
  // like Vivid or Flat in the editor notefield. This is not conducive to
  // productive editing.
  // todo: We should allow certain noteskins (note-colored/rhythm) to be
  // displayed. (Perhaps this should be a noteskin metric.) -aj
  std::string sEditNoteSkin = routine_edit
                                  ? GetRoutineEditorNoteSkin(main_player_)
                                  : EDITOR_NOTE_SKINS[main_player_].Get();
  if (!NOTESKIN->DoesNoteSkinExist(sEditNoteSkin)) {
    sEditNoteSkin = EDITOR_NOTE_SKINS[main_player_].Get();
  }
  if (NOTESKIN->DoesNoteSkinExist(sEditNoteSkin)) {
    PO_GROUP_ASSIGN(
        player_state_edit_.player_options_, ModsLevel_Stage, note_skin_,
        sEditNoteSkin);
  } else {
    PO_GROUP_ASSIGN(
        player_state_edit_.player_options_, ModsLevel_Stage, note_skin_,
        GAMESTATE->player_state_[main_player_]
            ->player_options_.GetStage()
            .note_skin_);
  }
  player_state_edit_.player_options_.FromString(
      ModsLevel_Stage, EDIT_MODIFIERS);

  this->original_player_options_.FromString(ModsLevel_Stage, EDIT_MODIFIERS);

  // Make CTRL+Up/Down and CTRL+wheel scroll-speed changes catch up configurable
  // instead of using gameplay's slow multi-second approach.
  PO_GROUP_ASSIGN(
      player_state_edit_.player_options_, ModsLevel_Stage, speed_scroll_speed_,
      SCROLL_SPEED_APPROACH_RATE.GetValue());

  steps_->GetNoteData(note_data_edit_);
  note_field_edit_.SetXY(EDIT_X, EDIT_Y);
  note_field_edit_.SetZoom(SCREEN_HEIGHT / 480 * 0.5);
  note_field_edit_.Init(&player_state_edit_, PLAYER_HEIGHT * 2, false);
  note_field_edit_.Load(&note_data_edit_, -240, 850);
  note_field_edit_.LoadWaveform(steps_->GetMusicPath());
  this->AddChild(&note_field_edit_);

  note_data_record_.SetNumTracks(note_data_edit_.GetNumTracks());
  note_field_record_.SetXY(RECORD_X, RECORD_Y);
  note_field_record_.Init(
      GAMESTATE->player_state_[main_player_], PLAYER_HEIGHT);
  note_field_record_.Load(&note_data_record_, -120, 425);
  this->AddChild(&note_field_record_);

  edit_state_ = EditState_Invalid;

  remove_note_button_down_ = false;

  clipboard_.SetNumTracks(note_data_edit_.GetNumTracks());

  // clipboardFullTiming is static and only initialized with a backup the
  // first time any ScreenEdit is created, so it persists useful data.
  if (clipboard_full_timing == nullptr) {
    clipboard_full_timing_ = GAMESTATE->cur_song_->song_timing_;
  }
  clipboard_full_timing = &clipboard_full_timing_;

  ClearUndo();

  SetDirty(note_data_edit_.IsEmpty());  // require saving if empty.
  if (GAMESTATE->cur_song_->WasLoadedFromAutosave()) {
    SetDirty(true);
  }

  // There are two conditions for centering the notefield.
  // 1. We're in EditMode
  // 2. We're in PracticeMode, the preference is enabled,
  //      sylte is OnePlayerOneSide, and the theme allows it.
  //
  // If the "center_enabled" check is modified, also modify
  // ScreenGameplay::Center1Player().
  ThemeMetric<bool> allow_center;
  allow_center.Load("ScreenGameplay", "AllowCenter1Player");
  bool center_enabled =
      (Preference<bool>::GetPreferenceByName("Center1Player")->Get() &&
       GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->style_type_ ==
           StyleType_OnePlayerOneSide) &&
      GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE && allow_center;

  bool edit_mode_screen = (name_ == "ScreenEdit");

  player_manager_.SetCenter(edit_mode_screen || center_enabled);

  player_manager_.AddPlayers(note_data_edit_);
  player_manager_.AddPlayersToActorFrame(*this);
  TransitionEditState(STATE_EDITING);

  this->AddChild(&foreground_);

  text_input_tips_.SetName("EditHelp");
  text_input_tips_.LoadFromFont(THEME->GetPathF("ScreenEdit", "EditHelp"));
  text_input_tips_.SetText(EDIT_HELP_TEXT);
  LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(text_input_tips_);
  this->AddChild(&text_input_tips_);

  text_info_.SetName("Info");
  text_info_.LoadFromFont(THEME->GetPathF("ScreenEdit", "Info"));
  LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(text_info_);
  this->AddChild(&text_info_);
  text_info_needs_update_ = false;

  text_play_record_help_.SetName("PlayRecordHelp");
  text_play_record_help_.LoadFromFont(
      THEME->GetPathF("ScreenEdit", "PlayRecordHelp"));
  text_play_record_help_.SetText(PLAY_RECORD_HELP_TEXT);
  LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(text_play_record_help_);
  this->AddChild(&text_play_record_help_);

  sound_add_note_.Load(THEME->GetPathS("ScreenEdit", "AddNote"), true);
  sound_remove_note_.Load(THEME->GetPathS("ScreenEdit", "RemoveNote"), true);
  sound_change_line_.Load(THEME->GetPathS("ScreenEdit", "line"), true);
  sound_change_snap_.Load(THEME->GetPathS("ScreenEdit", "snap"), true);
  sound_marker_.Load(THEME->GetPathS("ScreenEdit", "marker"), true);
  sound_value_increase_.Load(
      THEME->GetPathS("ScreenEdit", "value increase"), true);
  sound_value_decrease_.Load(
      THEME->GetPathS("ScreenEdit", "value decrease"), true);
  sound_switch_player_.Load(
      THEME->GetPathS("ScreenEdit", "switch player"), true);
  sound_switch_timing_.Load(
      THEME->GetPathS("ScreenEdit", "switch timing"), true);
  sound_switch_steps_.Load(THEME->GetPathS("ScreenEdit", "switch steps"));
  sound_save_.Load(THEME->GetPathS("ScreenEdit", "save"));
  gameplay_assist_.Init();

  auto_keysounds_.FinishLoading();
  sound_music_ = auto_keysounds_.GetSound();

  this->HandleScreenMessage(SM_UpdateTextInfo);
  text_info_needs_update_ = true;

  SubscribeToMessage(Message_SongModified);
}

ScreenEdit::~ScreenEdit() {
  // UGLY: Don't delete the Song's steps.
  song_last_save_.DetachSteps();

  LOG->Trace("ScreenEdit::~ScreenEdit()");
  sound_music_->StopPlaying();

  // Blocks until the analysis thread notices it should stop.
  RageUtil::SafeDelete(tempo_detector_);

  // Go back to Step Timing on leave.
  GAMESTATE->is_using_step_timing_ = true;
  // DEFINITELY reset the InStepEditor variable.
  GAMESTATE->in_step_editor_ = false;
}

void ScreenEdit::BeginScreen() {
  ScreenWithMenuElements::BeginScreen();

  /* We do this ourself. */
  SOUND->HandleSongTimer(false);
}

void ScreenEdit::EndScreen() {
  ScreenWithMenuElements::EndScreen();

  SOUND->HandleSongTimer(true);
}

// play assist ticks
void ScreenEdit::PlayTicks() {
  if (edit_state_ != STATE_PLAYING) {
    return;
  }
  player_manager_.PlayTicks(gameplay_assist_);
}

static ThemeMetric<float> FADE_IN_PREVIEW("ScreenEdit", "FadeInPreview");
static ThemeMetric<float> FADE_OUT_PREVIEW("ScreenEdit", "FadeOutPreview");

void ScreenEdit::PlayPreviewMusic() {
  SOUND->StopMusic();
  SOUND->PlayMusic(
      song_->GetPreviewMusicPath(), nullptr, false,
      song_->GetPreviewStartSeconds(), song_->music_sample_length_seconds_,
      FADE_IN_PREVIEW, FADE_OUT_PREVIEW);
}

void ScreenEdit::MakeFilteredMenuDef(const MenuDef* pDef, MenuDef& menu) {
  menu = *pDef;
  menu.rows_.clear();
  for (const MenuRowDef& r : pDef->rows_) {
    // Don't add rows that aren't applicable to this edit mode.
    if (EDIT_MODE >= r.show_in_) {
      menu.rows_.push_back(r);
    }
  }
}

void ScreenEdit::EditMiniMenu(
    const MenuDef* pDef, ScreenMessage SM_SendOnOK,
    ScreenMessage SM_SendOnCancel) {
  // Reload options.
  MenuDef menu("");
  MakeFilteredMenuDef(pDef, menu);
  ScreenMiniMenu::MiniMenu(&menu, SM_SendOnOK, SM_SendOnCancel);
}

void ScreenEdit::Update(float fDeltaTime) {
  player_state_edit_.Update(fDeltaTime);

  if (mouse_dragging_) {
    mouse_drag_current_x_ = GetMouseScreenX();
    mouse_drag_current_y_ = GetMouseScreenY();
  }

  if (tempo_detector_ != nullptr) {
    if (tempo_detector_->IsFinished()) {
      tempo_results_ = tempo_detector_->GetResults();
      RageUtil::SafeDelete(tempo_detector_);
      if (tempo_results_.empty()) {
        SCREENMAN->SystemMessage(DETECT_BPM_NO_RESULTS);
      }
      DisplayAdjustSyncMenu();
    } else {
      std::string sProgress = tempo_detector_->GetProgress();
      if (sProgress != tempo_progress_) {
        tempo_progress_ = sProgress;
        SCREENMAN->SystemMessage(sProgress);
      }
    }
  }

  const float fRate = PREFSMAN->rate_mods_affect_tweens_
                          ? GAMESTATE->song_options_.GetCurrent().music_rate_
                          : 1.0f;

  if (sound_music_->IsPlaying()) {
    RageTimer tm;
    const float fSeconds = sound_music_->GetPositionSeconds(&tm);
    GAMESTATE->UpdateSongPosition(
        fSeconds, GAMESTATE->cur_song_->song_timing_, tm);
  }

  if (edit_state_ == STATE_EDITING) {
    if (IsDirty() && next_autosave_time_ > -1.0f &&
        RageTimer::GetTimeSinceStart() > next_autosave_time_) {
      PerformSave(true);
    }
  }

  if (edit_state_ == STATE_RECORDING) {
    // TODO: Find a way to prevent STATE_RECORDING when in Song Timing.

    for (int t = 0;
         t < GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                 ->cols_per_player_;
         t++)  // for each track
    {
      std::vector<GameInput> GameI;
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->StyleInputToGameInput(t, PLAYER_1, GameI);
      float fSecsHeld = 0.0f;
      for (size_t i = 0; i < GameI.size(); ++i) {
        fSecsHeld = std::max(fSecsHeld, INPUTMAPPER->GetSecsHeld(GameI[i]));
      }
      fSecsHeld = std::min(fSecsHeld, remove_note_button_last_changed_.Ago());
      if (fSecsHeld == 0) {
        continue;
      }

      // TODO: Currently, this will ignore a hold note that's
      // held past the end of selection.  Ideally the hold
      // note should be created, ending right at the end of
      // selection (or appropriate quantization).
      float fStartPlayingAtBeat = NoteRowToBeat(start_playing_at_);
      float fStopPlayingAtBeat = NoteRowToBeat(stop_playing_at_);
      if (GAMESTATE->player_state_[main_player_]->position_.song_beat_ <=
              fStartPlayingAtBeat ||
          GAMESTATE->player_state_[main_player_]->position_.song_beat_ >=
              fStopPlayingAtBeat) {
        continue;
      }

      float fStartedHoldingSeconds =
          sound_music_->GetPositionSeconds() - fSecsHeld;
      float fStartBeat = std::max(
          fStartPlayingAtBeat, steps_->GetTimingData()->GetBeatFromElapsedTime(
                                   fStartedHoldingSeconds));
      float fEndBeat = std::max(fStartBeat, GetBeat());
      fEndBeat = std::min(fEndBeat, fStopPlayingAtBeat);

      // Round start and end to the nearest snap interval
      fStartBeat =
          Quantize(fStartBeat, NoteTypeToBeat(snap_display_.GetNoteType()));
      fEndBeat =
          Quantize(fEndBeat, NoteTypeToBeat(snap_display_.GetNoteType()));

      if (remove_note_button_down_) {
        note_data_record_.ClearRangeForTrack(
            BeatToNoteRow(fStartBeat), BeatToNoteRow(fEndBeat), t);
      } else if (fSecsHeld > record_hold_seconds) {
        // create or extend a hold or roll note
        TapNote tn = EditIsBeingPressed(EDIT_BUTTON_LAY_ROLL)
                         ? TAP_ORIGINAL_ROLL_HEAD
                         : TAP_ORIGINAL_HOLD_HEAD;

        tn.pn_ = input_player_number_;
        note_data_record_.AddHoldNote(
            t, BeatToNoteRow(fStartBeat), BeatToNoteRow(fEndBeat), tn);
      }
    }
  }

  //
  // check for end of playback/record
  //
  if (edit_state_ == STATE_RECORDING || edit_state_ == STATE_PLAYING) {
    ArrowEffects::Update();
    /*
     * If any arrow is being held, continue for up to half a second after
     * the end marker.  This makes it possible to start a hold note near
     * the end of the range.  We won't allow placing anything outside of the
     * range.
     */
    bool bButtonIsBeingPressed = false;
    for (int t = 0;
         t < GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                 ->cols_per_player_;
         t++)  // for each track
    {
      std::vector<GameInput> GameI;
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->StyleInputToGameInput(t, PLAYER_1, GameI);
      if (INPUTMAPPER->IsBeingPressed(GameI)) {
        bButtonIsBeingPressed = true;
      }
    }

    float fLastBeat = NoteRowToBeat(stop_playing_at_);
    if (bButtonIsBeingPressed && edit_state_ == STATE_RECORDING) {
      float fSeconds =
          steps_->GetTimingData()->GetElapsedTimeFromBeat(fLastBeat);
      fLastBeat =
          steps_->GetTimingData()->GetBeatFromElapsedTime(fSeconds + 0.5f);
    }

    float fStopAtSeconds = steps_->GetTimingData()->GetElapsedTimeFromBeat(
                               NoteRowToBeat(stop_playing_at_)) +
                           1;
    if (GAMESTATE->player_state_[main_player_]->position_.music_seconds_ >
        fStopAtSeconds) {
      TransitionEditState((LOOP_ON_CHART_END ? STATE_PLAYING : STATE_EDITING));
    }
  }

  // LOG->Trace( "ScreenEdit::Update(%f)", fDeltaTime );
  if (edit_state_ == STATE_PLAYING) {
    ScreenWithMenuElements::Update(fDeltaTime * fRate);
  } else {
    ScreenWithMenuElements::Update(fDeltaTime);
  }

  // Update trailing beat. Only speed up the catch-up once acceleration is
  // more than halfway to its max, so ordinary scrolling keeps its normal
  // ease instead of visibly crawling only after a heavy accelerated burst.
  const float fCatchUpMultiplier =
      scroll_acceleration_multiplier_ > (kMaxScrollAccelerationMultiplier / 2)
          ? scroll_acceleration_multiplier_
          : 1.0f;
  float fDelta = GetBeat() - trailing_beat_;
  if (std::abs(fDelta) < 10) {
    fapproach(
        trailing_beat_, GetBeat(),
        fCatchUpMultiplier * fDeltaTime * 40 /
            note_field_edit_.GetPlayerState()
                ->player_options_.GetCurrent()
                .scroll_speed_);
  } else {
    fapproach(
        trailing_beat_, GetBeat(),
        fCatchUpMultiplier * std::abs(fDelta) * fDeltaTime * 5);
  }

  PlayTicks();
}

static std::vector<int> FindAllAttacksAtTime(
    const AttackArray& attacks, float fStartTime) {
  std::vector<int> ret;
  for (unsigned i = 0; i < attacks.size(); ++i) {
    if (std::abs(attacks[i].start_second_ - fStartTime) < 0.001f) {
      ret.push_back(i);
    }
  }
  return ret;
}

static int FindAttackAtTime(const AttackArray& attacks, float fStartTime) {
  for (unsigned i = 0; i < attacks.size(); ++i) {
    if (std::abs(attacks[i].start_second_ - fStartTime) < 0.001f) {
      return i;
    }
  }
  return -1;
}

// TODO: Fix this mess. -Colby
static LocalizedString CURRENT_BEAT("ScreenEdit", "Current beat");
static LocalizedString CURRENT_SECOND("ScreenEdit", "Current second");
static LocalizedString SNAP_TO("ScreenEdit", "Snap to");
static LocalizedString NOTES("ScreenEdit", "%s notes");
static LocalizedString SELECTION_BEAT("ScreenEdit", "Selection beat");
static LocalizedString DIFFICULTY("ScreenEdit", "Difficulty");
static LocalizedString ROUTINE_PLAYER("ScreenEdit", "Routine Player");
static LocalizedString CHART_NAME("ScreenEdit", "Chart Name");
static LocalizedString STEP_AUTHOR("ScreenEdit", "Step Author");
static LocalizedString MAIN_TITLE("ScreenEdit", "Main title");
static LocalizedString SUBTITLE("ScreenEdit", "Subtitle");
static LocalizedString TAP_NOTE_TYPE("ScreenEdit", "Tap Note");
static LocalizedString SEGMENT_TYPE("ScreenEdit", "Segment");
static LocalizedString TAP_STEPS("ScreenEdit", "Tap Steps");
static LocalizedString JUMPS("ScreenEdit", "Jumps");
static LocalizedString HANDS("ScreenEdit", "Hands");
static LocalizedString HOLDS("ScreenEdit", "Holds");
static LocalizedString MINES("ScreenEdit", "Mines");
static LocalizedString ROLLS("ScreenEdit", "Rolls");
static LocalizedString LIFTS("ScreenEdit", "Lifts");
static LocalizedString FAKES("ScreenEdit", "Fakes");
static LocalizedString TIMING_MODE("ScreenEdit", "Timing Mode");
static LocalizedString STEP_TIMING("ScreenEdit", "Step Timing");
static LocalizedString SONG_TIMING("ScreenEdit", "Song Timing");
static LocalizedString BEAT_0_OFFSET("ScreenEdit", "Beat 0 offset");
static LocalizedString PREVIEW_START("ScreenEdit", "Preview Start");
static LocalizedString PREVIEW_LENGTH("ScreenEdit", "Preview Length");
static LocalizedString RECORD_HOLD_TIME("ScreenEdit", "Record Hold Time");

static ThemeMetric<std::string> CURRENT_BEAT_FORMAT(
    "ScreenEdit", "CurrentBeatFormat");
static ThemeMetric<std::string> CURRENT_SECOND_FORMAT(
    "ScreenEdit", "CurrentSecondFormat");
static ThemeMetric<std::string> SNAP_TO_FORMAT("ScreenEdit", "SnapToFormat");
static ThemeMetric<std::string> SELECTION_BEAT_BEGIN_FORMAT(
    "ScreenEdit", "SelectionBeatBeginFormat");
static ThemeMetric<std::string> SELECTION_BEAT_UNFINISHED_FORMAT(
    "ScreenEdit", "SelectionBeatUnfinishedFormat");
static ThemeMetric<std::string> SELECTION_BEAT_END_FORMAT(
    "ScreenEdit", "SelectionBeatEndFormat");
static ThemeMetric<std::string> DIFFICULTY_FORMAT(
    "ScreenEdit", "DifficultyFormat");
static ThemeMetric<std::string> ROUTINE_PLAYER_FORMAT(
    "ScreenEdit", "RoutinePlayerFormat");
static ThemeMetric<std::string> CHART_NAME_FORMAT(
    "ScreenEdit", "ChartNameFormat");
static ThemeMetric<std::string> STEP_AUTHOR_FORMAT(
    "ScreenEdit", "StepAuthorFormat");
static ThemeMetric<std::string> MAIN_TITLE_FORMAT(
    "ScreenEdit", "MainTitleFormat");
static ThemeMetric<std::string> SUBTITLE_FORMAT("ScreenEdit", "SubtitleFormat");
static ThemeMetric<std::string> TAP_NOTE_TYPE_FORMAT(
    "ScreenEdit", "TapNoteTypeFormat");
static ThemeMetric<std::string> SEGMENT_TYPE_FORMAT(
    "ScreenEdit", "SegmentTypeFormat");
static ThemeMetric<std::string> NUM_STEPS_FORMAT(
    "ScreenEdit", "NumStepsFormat");
static ThemeMetric<std::string> NUM_JUMPS_FORMAT(
    "ScreenEdit", "NumJumpsFormat");
static ThemeMetric<std::string> NUM_HOLDS_FORMAT(
    "ScreenEdit", "NumHoldsFormat");
static ThemeMetric<std::string> NUM_MINES_FORMAT(
    "ScreenEdit", "NumMinesFormat");
static ThemeMetric<std::string> NUM_HANDS_FORMAT(
    "ScreenEdit", "NumHandsFormat");
static ThemeMetric<std::string> NUM_ROLLS_FORMAT(
    "ScreenEdit", "NumRollsFormat");
static ThemeMetric<std::string> NUM_LIFTS_FORMAT(
    "ScreenEdit", "NumLiftsFormat");
static ThemeMetric<std::string> NUM_FAKES_FORMAT(
    "ScreenEdit", "NumFakesFormat");
static ThemeMetric<std::string> NUM_STEPS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumStepsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_JUMPS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumJumpsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_HOLDS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumHoldsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_MINES_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumMinesFormatTwoPlayer");
static ThemeMetric<std::string> NUM_HANDS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumHandsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_ROLLS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumRollsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_LIFTS_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumLiftsFormatTwoPlayer");
static ThemeMetric<std::string> NUM_FAKES_FORMAT_TWO_PLAYER(
    "ScreenEdit", "NumFakesFormatTwoPlayer");
static ThemeMetric<std::string> TIMING_MODE_FORMAT(
    "ScreenEdit", "TimingModeFormat");
static ThemeMetric<std::string> BEAT_0_OFFSET_FORMAT(
    "ScreenEdit", "Beat0OffsetFormat");
static ThemeMetric<std::string> PREVIEW_START_FORMAT(
    "ScreenEdit", "PreviewStartFormat");
static ThemeMetric<std::string> PREVIEW_LENGTH_FORMAT(
    "ScreenEdit", "PreviewLengthFormat");
static ThemeMetric<std::string> RECORD_HOLD_TIME_FORMAT(
    "ScreenEdit", "RecordHoldTimeFormat");
void ScreenEdit::UpdateTextInfo() {
  if (steps_ == nullptr) {
    return;
  }

  // Don't update the text during playback or record.  It causes skips.
  if (edit_state_ != STATE_EDITING) {
    return;
  }

  if (!text_info_needs_update_) {
    return;
  }

  text_info_needs_update_ = false;

  std::string sNoteType = ssprintf(
      NOTES.GetValue().c_str(),
      NoteTypeToLocalizedString(snap_display_.GetNoteType()).c_str());

  std::string sText;
  sText += ssprintf(
      CURRENT_BEAT_FORMAT.GetValue().c_str(), CURRENT_BEAT.GetValue().c_str(),
      GetBeat());
  float second =
      GetAppropriateTiming().GetElapsedTimeFromBeatNoOffset(GetBeat());
  sText += ssprintf(
      CURRENT_SECOND_FORMAT.GetValue().c_str(),
      CURRENT_SECOND.GetValue().c_str(), second);
  switch (EDIT_MODE.GetValue()) {
    DEFAULT_FAIL(EDIT_MODE.GetValue());
    case EditMode_Practice:
      break;
    case EditMode_CourseMods:
    case EditMode_Home:
    case EditMode_Full:
      sText += ssprintf(
          SNAP_TO_FORMAT.GetValue().c_str(), SNAP_TO.GetValue().c_str(),
          sNoteType.c_str());
      break;
  }

  if (note_field_edit_.begin_marker_ != -1) {
    sText += ssprintf(
        SELECTION_BEAT_BEGIN_FORMAT.GetValue().c_str(),
        SELECTION_BEAT.GetValue().c_str(),
        NoteRowToBeat(note_field_edit_.begin_marker_));

    if (note_field_edit_.end_marker_ != -1) {
      sText += ssprintf(
          SELECTION_BEAT_END_FORMAT.GetValue().c_str(),
          NoteRowToBeat(note_field_edit_.end_marker_));
    } else {
      sText += SELECTION_BEAT_UNFINISHED_FORMAT;
    }
  }

  if (EDIT_MODE.GetValue() == EditMode_Full) {
    sText += ssprintf(
        DIFFICULTY_FORMAT.GetValue().c_str(), DIFFICULTY.GetValue().c_str(),
        DifficultyToString(steps_->GetDifficulty()).c_str());
    if (input_player_number_ != PLAYER_INVALID) {
      sText += ssprintf(
          ROUTINE_PLAYER_FORMAT.GetValue().c_str(),
          ROUTINE_PLAYER.GetValue().c_str(), input_player_number_ + 1);
    }
    sText += ssprintf(
        CHART_NAME_FORMAT.GetValue().c_str(), CHART_NAME.GetValue().c_str(),
        steps_->GetChartName().c_str());
    sText += ssprintf(
        STEP_AUTHOR_FORMAT.GetValue().c_str(), STEP_AUTHOR.GetValue().c_str(),
        steps_->GetCredit().c_str());
    sText += ssprintf(
        MAIN_TITLE_FORMAT.GetValue().c_str(), MAIN_TITLE.GetValue().c_str(),
        song_->main_title_.c_str());
    if (song_->sub_title_.size()) {
      sText += ssprintf(
          SUBTITLE_FORMAT.GetValue().c_str(), SUBTITLE.GetValue().c_str(),
          song_->sub_title_.c_str());
    }
    sText += ssprintf(
        SEGMENT_TYPE_FORMAT.GetValue().c_str(), SEGMENT_TYPE.GetValue().c_str(),
        TimingSegmentTypeToString(current_cycle_segment_).c_str());
    const std::string tapnoteType = TapNoteTypeToString(selected_tap_.type_);
    sText += ssprintf(
        TAP_NOTE_TYPE_FORMAT.GetValue().c_str(),
        TAP_NOTE_TYPE.GetValue().c_str(), tapnoteType.c_str());

    AttackArray attacks =
        (GAMESTATE->is_using_step_timing_ ? steps_->GetAttacks()
                                          : song_->attacks_);
    float beat = GetAppropriateTiming().GetElapsedTimeFromBeat(GetBeat());
    sText += ssprintf(
        "Attack here?: %s\n",
        FindAttackAtTime(attacks, beat) > -1 ? "YES" : "NO");
  }

  GAMESTATE->SetProcessedTimingData(steps_->GetTimingData());
  const StepsTypeCategory& cat =
      GAMEMAN->GetStepsTypeInfo(steps_->steps_type_).steps_type_category_;
  if (cat == StepsTypeCategory_Couple || cat == StepsTypeCategory_Routine) {
    std::pair<int, int> tmp = note_data_edit_.GetNumTapNotesTwoPlayer();
    sText += ssprintf(
        NUM_STEPS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        TAP_STEPS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumJumpsTwoPlayer();
    sText += ssprintf(
        NUM_JUMPS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        JUMPS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumHandsTwoPlayer();
    sText += ssprintf(
        NUM_HANDS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        HANDS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumHoldNotesTwoPlayer();
    sText += ssprintf(
        NUM_HOLDS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        HOLDS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumMinesTwoPlayer();
    sText += ssprintf(
        NUM_MINES_FORMAT_TWO_PLAYER.GetValue().c_str(),
        MINES.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumRollsTwoPlayer();
    sText += ssprintf(
        NUM_ROLLS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        ROLLS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumLiftsTwoPlayer();
    sText += ssprintf(
        NUM_LIFTS_FORMAT_TWO_PLAYER.GetValue().c_str(),
        LIFTS.GetValue().c_str(), tmp.first, tmp.second);
    tmp = note_data_edit_.GetNumFakesTwoPlayer();
    sText += ssprintf(
        NUM_FAKES_FORMAT_TWO_PLAYER.GetValue().c_str(),
        FAKES.GetValue().c_str(), tmp.first, tmp.second);
  } else {
    sText += ssprintf(
        NUM_STEPS_FORMAT.GetValue().c_str(), TAP_STEPS.GetValue().c_str(),
        note_data_edit_.GetNumTapNotes());
    sText += ssprintf(
        NUM_JUMPS_FORMAT.GetValue().c_str(), JUMPS.GetValue().c_str(),
        note_data_edit_.GetNumJumps());
    sText += ssprintf(
        NUM_HANDS_FORMAT.GetValue().c_str(), HANDS.GetValue().c_str(),
        note_data_edit_.GetNumHands());
    sText += ssprintf(
        NUM_HOLDS_FORMAT.GetValue().c_str(), HOLDS.GetValue().c_str(),
        note_data_edit_.GetNumHoldNotes());
    sText += ssprintf(
        NUM_MINES_FORMAT.GetValue().c_str(), MINES.GetValue().c_str(),
        note_data_edit_.GetNumMines());
    sText += ssprintf(
        NUM_ROLLS_FORMAT.GetValue().c_str(), ROLLS.GetValue().c_str(),
        note_data_edit_.GetNumRolls());
    sText += ssprintf(
        NUM_LIFTS_FORMAT.GetValue().c_str(), LIFTS.GetValue().c_str(),
        note_data_edit_.GetNumLifts());
    sText += ssprintf(
        NUM_FAKES_FORMAT.GetValue().c_str(), FAKES.GetValue().c_str(),
        note_data_edit_.GetNumFakes());
  }
  switch (EDIT_MODE.GetValue()) {
    DEFAULT_FAIL(EDIT_MODE.GetValue());
    case EditMode_Practice:
    case EditMode_CourseMods:
    case EditMode_Home:
      break;
    case EditMode_Full:
      sText += ssprintf(
          TIMING_MODE_FORMAT.GetValue().c_str(), TIMING_MODE.GetValue().c_str(),
          (GAMESTATE->is_using_step_timing_ ? STEP_TIMING.GetValue().c_str()
                                            : SONG_TIMING.GetValue().c_str()));
      sText += ssprintf(
          BEAT_0_OFFSET_FORMAT.GetValue().c_str(),
          BEAT_0_OFFSET.GetValue().c_str(),
          GetAppropriateTiming().beat0_offset_in_seconds_);
      sText += ssprintf(
          PREVIEW_START_FORMAT.GetValue().c_str(),
          PREVIEW_START.GetValue().c_str(), song_->music_sample_start_seconds_);
      sText += ssprintf(
          PREVIEW_LENGTH_FORMAT.GetValue().c_str(),
          PREVIEW_LENGTH.GetValue().c_str(),
          song_->music_sample_length_seconds_);
      if (record_hold_seconds < record_hold_default - .001f ||
          record_hold_seconds > record_hold_default + .001f) {
        sText += ssprintf(
            RECORD_HOLD_TIME_FORMAT.GetValue().c_str(),
            RECORD_HOLD_TIME.GetValue().c_str(), record_hold_seconds);
      }
      break;
  }

  text_info_.SetText(sText);

  GAMESTATE->SetProcessedTimingData(nullptr);
}

void ScreenEdit::DrawPrimitives() {
  if (sound_music_->IsPlaying()) {
    ScreenWithMenuElements::DrawPrimitives();
    return;
  }

  // HACK:  Draw using the trailing beat
  PlayerState* pPlayerState =
      const_cast<PlayerState*>(note_field_edit_.GetPlayerState());

  float fPlayerSongBeat = pPlayerState->position_.song_beat_;  // save song beat
  float fPlayerSongBeatNoOffset = pPlayerState->position_.song_beat_no_offset_;
  float fPlayerSongBeatVisible = pPlayerState->position_.song_beat_visible_;

  float fGameSongBeat = GAMESTATE->position_.song_beat_;  // save song beat
  float fGameSongBeatNoOffset = GAMESTATE->position_.song_beat_no_offset_;
  float fGameSongBeatVisible = GAMESTATE->position_.song_beat_visible_;

  pPlayerState->position_.song_beat_ =
      trailing_beat_;  // put trailing beat in effect
  pPlayerState->position_.song_beat_no_offset_ =
      trailing_beat_;  // put trailing beat in effect
  pPlayerState->position_.song_beat_visible_ =
      trailing_beat_;  // put trailing beat in effect

  GAMESTATE->position_.song_beat_ =
      trailing_beat_;  // put trailing beat in effect
  GAMESTATE->position_.song_beat_no_offset_ =
      trailing_beat_;  // put trailing beat in effect
  GAMESTATE->position_.song_beat_visible_ =
      trailing_beat_;  // put trailing beat in effect

  ScreenWithMenuElements::DrawPrimitives();

  pPlayerState->position_.song_beat_ =
      fPlayerSongBeat;  // restore real song beat
  pPlayerState->position_.song_beat_no_offset_ = fPlayerSongBeatNoOffset;
  pPlayerState->position_.song_beat_visible_ = fPlayerSongBeatVisible;

  GAMESTATE->position_.song_beat_ = fGameSongBeat;  // restore real song beat
  GAMESTATE->position_.song_beat_no_offset_ = fGameSongBeatNoOffset;
  GAMESTATE->position_.song_beat_visible_ = fGameSongBeatVisible;

  if (mouse_dragging_) {
    rect_mouse_selection_.StretchTo(RectF(
        std::min(mouse_drag_start_x_, mouse_drag_current_x_),
        std::min(mouse_drag_start_y_, mouse_drag_current_y_),
        std::max(mouse_drag_start_x_, mouse_drag_current_x_),
        std::max(mouse_drag_start_y_, mouse_drag_current_y_)));
    rect_mouse_selection_.SetDiffuse(RageColor(0.6f, 0.8f, 1.0f, 0.25f));
    rect_mouse_selection_.Draw();
  }
}

static bool IsShiftHeld() {
  return INPUTFILTER->IsBeingPressed(
             DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
         INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));
}

void ScreenEdit::FinishMouseDragSelection() {
  /*
   * Treat a tiny movement as a click rather than a drag.
   */
  if (std::abs(mouse_drag_current_y_ - mouse_drag_start_y_) < 4.0f) {
    note_field_edit_.begin_marker_ = -1;
    note_field_edit_.end_marker_ = -1;
    return;
  }

  /*
   * Convert the top and bottom of the physical selection rectangle into
   * the beats actually visible underneath those screen positions.
   *
   * MouseYToBeat() uses m_fTrailingBeat, so this follows scrolling and
   * corresponds to the arrows the user currently sees.
   */
  float fStartBeat =
      MouseYToBeat(std::min(mouse_drag_start_y_, mouse_drag_current_y_));

  float fEndBeat =
      MouseYToBeat(std::max(mouse_drag_start_y_, mouse_drag_current_y_));

  /*
   * Reverse scroll or other display conditions can cause the visual
   * direction and beat direction to disagree, so normalize them.
   */
  if (fStartBeat > fEndBeat) {
    std::swap(fStartBeat, fEndBeat);
  }

  int iStartRow = std::max(0, BeatToNoteRow(fStartBeat));

  int iEndRow = std::max(0, BeatToNoteRow(fEndBeat));

  if (IsShiftHeld()) {
    /*
     * Shift + drag:
     *
     * Snap the bounds to the player's currently selected editor quant.
     *
     * For example, if the editor is currently on 16ths, both ends of the
     * selected area will land on 16th-note boundaries.
     */
    const float fSnap = NoteTypeToBeat(snap_display_.GetNoteType());

    iStartRow = std::max(0, BeatToNoteRow(Quantize(fStartBeat, fSnap)));

    iEndRow = std::max(0, BeatToNoteRow(Quantize(fEndBeat, fSnap)));
  } else {
    /*
     * Normal drag:
     *
     * The raw rectangle may start/end between arrows.  Tighten the
     * selection so its bounds become the first and last actual arrows
     * covered by the rectangle.
     */
    int iFirstNote = iStartRow - 1;
    int iLastNote = iEndRow + 1;

    const bool bHasFirst =
        note_data_edit_.GetNextTapNoteRowForAllTracks(iFirstNote) &&
        iFirstNote <= iEndRow;

    const bool bHasLast =
        note_data_edit_.GetPrevTapNoteRowForAllTracks(iLastNote) &&
        iLastNote >= iStartRow;

    if (bHasFirst && bHasLast && iFirstNote <= iLastNote) {
      iStartRow = iFirstNote;
      iEndRow = iLastNote;
    } else {
      /*
       * There were no valid arrows inside the dragged rectangle.
       */
      note_field_edit_.begin_marker_ = -1;
      note_field_edit_.end_marker_ = -1;
      return;
    }
  }

  /*
   * ScreenEdit expects an actual region with separate begin/end markers.
   */
  if (iStartRow >= iEndRow) {
    note_field_edit_.begin_marker_ = -1;
    note_field_edit_.end_marker_ = -1;
    return;
  }

  note_field_edit_.begin_marker_ = iStartRow;
  note_field_edit_.end_marker_ = iEndRow;

  sound_marker_.Play(true);
}

bool ScreenEdit::Input(const InputEventPlus& input) {
  //	LOG->Trace( "ScreenEdit::Input()" );

  // invalidate input if cmd/meta is being held.
  if (INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LMETA)) ||
      INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RMETA))) {
    return false;
  }

  if (in_.IsTransitioning() || out_.IsTransitioning()) {
    return false;
  }

  if (edit_state_ == STATE_EDITING &&
      input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_LEFT)) {
    if (input.type_ == IET_FIRST_PRESS) {
      mouse_dragging_ = true;
      mouse_drag_start_x_ = mouse_drag_current_x_ = GetMouseScreenX();
      mouse_drag_start_y_ = mouse_drag_current_y_ = GetMouseScreenY();
      return true;
    }
    if (input.type_ == IET_RELEASE && mouse_dragging_) {
      mouse_dragging_ = false;
      FinishMouseDragSelection();
      return true;
    }
  }

  EditButton EditB = DeviceToEdit(input.device_i_);
  if (EditB == EditButton_Invalid) {
    EditB = MenuButtonToEditButton(input.menu_i_);
  }

  // Allow mouse wheel chart navigation in edit mode by mapping to existing
  // scroll actions.
  if (EditB == EditButton_Invalid && edit_state_ == STATE_EDITING) {
    const bool bCtrlHeld =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL));
    const bool bShiftHeld =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));
    const bool bWheelUp =
        input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_WHEELUP);
    const bool bWheelDown =
        input.device_i_ == DeviceInput(DEVICE_MOUSE, MOUSE_WHEELDOWN);

    if ((bWheelUp || bWheelDown) && input.type_ != IET_RELEASE) {
      // Track consecutive wheel scrolls so rapid wheeling ramps up faster
      // than the key-repeat rate of holding a key down.
      if (last_wheel_scroll_time_.Ago() > kScrollAccelerationResetTime) {
        consecutive_wheel_scrolls_ = 0;
        scroll_acceleration_multiplier_ = 1.0f;
      }
      consecutive_wheel_scrolls_++;
      const float fAccelerationRate = SCROLL_WHEEL_ACCELERATION_RATE.GetValue();
      scroll_acceleration_multiplier_ =
          1.0f + (static_cast<float>(consecutive_wheel_scrolls_ - 1) *
                  fAccelerationRate);
      // Cap so a long scroll burst can't fling the cursor across the chart.
      scroll_acceleration_multiplier_ = std::min(
          scroll_acceleration_multiplier_, kMaxScrollAccelerationMultiplier);
      last_wheel_scroll_time_.Touch();

      if (bCtrlHeld) {
        // CTRL+wheel mirrors CTRL+Up/Down (scroll-speed presets), but jumps
        // multiple presets per tick as the wheel accelerates, since it's much
        // slower to get through the presets via key-repeat alone.
        PlayerState* pPlayerState =
            const_cast<PlayerState*>(note_field_edit_.GetPlayerState());
        float fScrollSpeed =
            pPlayerState->player_options_.GetSong().scroll_speed_;

        int iSpeed = 0;
        for (size_t i = 0; i < scroll_speed_presets_.size(); ++i) {
          if (scroll_speed_presets_[i] == fScrollSpeed) {
            iSpeed = static_cast<int>(i);
            break;
          }
        }

        const int iBaseDirection = bWheelUp ? +1 : -1;
        const int iDirection =
            INVERT_SCROLL_BUTTONS ? -iBaseDirection : iBaseDirection;
        const int iSteps = std::max(
            1, static_cast<int>(std::lround(scroll_acceleration_multiplier_)));
        iSpeed = std::clamp(
            iSpeed + iDirection * iSteps, 0,
            (int)scroll_speed_presets_.size() - 1);

        if (scroll_speed_presets_[iSpeed] != fScrollSpeed) {
          sound_marker_.Play(true);
          fScrollSpeed = scroll_speed_presets_[iSpeed];
        }

        PO_GROUP_ASSIGN(
            pPlayerState->player_options_, ModsLevel_Song, scroll_speed_,
            fScrollSpeed);
        return true;
      }

      EditB = bWheelUp ? (bShiftHeld ? EDIT_BUTTON_SCROLL_UP_PAGE
                                     : EDIT_BUTTON_SCROLL_UP_LINE)
                       : (bShiftHeld ? EDIT_BUTTON_SCROLL_DOWN_PAGE
                                     : EDIT_BUTTON_SCROLL_DOWN_LINE);
    }
  }

  if (EditB == EDIT_BUTTON_REMOVE_NOTE) {
    /* Ugly: we need to know when the button was pressed or released, so we
     * can clamp operations to that time.  Should InputFilter keep track of
     * last release, too? */
    remove_note_button_down_ = (input.type_ != IET_RELEASE);
    remove_note_button_last_changed_.Touch();
  }

  switch (edit_state_) {
    DEFAULT_FAIL(edit_state_);
    case STATE_EDITING:
      text_info_needs_update_ = true;
      return InputEdit(input, EditB);
    case STATE_RECORDING:
      return InputRecord(input, EditB);
    case STATE_RECORDING_PAUSED:
      return InputRecordPaused(input, EditB);
    case STATE_PLAYING:
      return InputPlay(input, EditB);
  }
}

static void ShiftToRightSide(int& iCol, int iNumTracks) {
  switch (GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->style_type_) {
    DEFAULT_FAIL(GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                     ->style_type_);
    case StyleType_OnePlayerOneSide:
      break;
    case StyleType_TwoPlayersTwoSides:
    case StyleType_OnePlayerTwoSides:
    case StyleType_TwoPlayersSharedSides:
      iCol += iNumTracks / 2;
      break;
  }
}

static LocalizedString BG_CHANGE_STEP_TIMING(
    "ScreenEdit", "You must be in Song Timing Mode to edit BG Changes.");
static LocalizedString ALTER_MENU_NO_SELECTION(
    "ScreenEdit", "You must have an area selected to enter the Alter Menu.");
static LocalizedString SWITCHED_TO("ScreenEdit", "Switched to");
static LocalizedString NO_BACKGROUNDS_AVAILABLE(
    "ScreenEdit", "No backgrounds available");

bool ScreenEdit::InputEdit(const InputEventPlus& input, EditButton EditB) {
  if (input.type_ == IET_RELEASE) {
    if (EditPressed(EDIT_BUTTON_SCROLL_SELECT, input.device_i_)) {
      shift_anchor_ = -1;
    }
    // XXX Key releases usually don't count as "handled," but what
    // does it mean in this case?
    return false;
  }
  const TimingData& sTiming = GetAppropriateTiming();
  float playerBeat = GetAppropriatePosition().song_beat_;
  int beatsPerMeasure =
      sTiming.GetTimeSignatureSegmentAtBeat(playerBeat)->GetNum();

  switch (EditB) {
    case EDIT_BUTTON_COLUMN_0:
    case EDIT_BUTTON_COLUMN_1:
    case EDIT_BUTTON_COLUMN_2:
    case EDIT_BUTTON_COLUMN_3:
    case EDIT_BUTTON_COLUMN_4:
    case EDIT_BUTTON_COLUMN_5:
    case EDIT_BUTTON_COLUMN_6:
    case EDIT_BUTTON_COLUMN_7:
    case EDIT_BUTTON_COLUMN_8:
    case EDIT_BUTTON_COLUMN_9: {
      if (input.type_ != IET_FIRST_PRESS) {
        return false;  // We only care about first presses
      }

      int iCol = EditB - EDIT_BUTTON_COLUMN_0;

      // Alt + number = input to right half
      if (EditIsBeingPressed(EDIT_BUTTON_RIGHT_SIDE)) {
        ShiftToRightSide(iCol, note_data_edit_.GetNumTracks());
      }

      if (iCol >= note_data_edit_.GetNumTracks()) {
        return false;  // this button is not in the range of columns for this
                       // Style
      }

      const float fSongBeat = GetBeat();
      const int iSongIndex = BeatToNoteRow(fSongBeat);

      // check for to see if the user intended to remove a HoldNote
      int iHeadRow;
      if (note_data_edit_.IsHoldNoteAtRow(iCol, iSongIndex, &iHeadRow)) {
        sound_remove_note_.Play(true);
        SetDirty(true);
        SaveUndo("Remove Hold");
        note_data_edit_.SetTapNote(iCol, iHeadRow, TAP_EMPTY);
        // Don't CheckNumberOfNotesAndUndo.  We don't want to revert any change
        // that removes notes.
      } else if (
          note_data_edit_.GetTapNote(iCol, iSongIndex).type_ !=
          TapNoteType_Empty) {
        sound_remove_note_.Play(true);
        SetDirty(true);
        SaveUndo("Remove Note");
        note_data_edit_.SetTapNote(iCol, iSongIndex, TAP_EMPTY);
        // Don't CheckNumberOfNotesAndUndo.  We don't want to revert any change
        // that removes notes.
      } else if (EditIsBeingPressed(EDIT_BUTTON_LAY_TAP_ATTACK)) {
        g_iLastInsertTapAttackTrack = iCol;
        EditMiniMenu(&g_InsertTapAttack, SM_BackFromInsertTapAttack);
      } else {
        sound_add_note_.Play(true);
        SetDirty(true);
        SaveUndo("Place Note");
        TapNote tn = selected_tap_;
        tn.pn_ = input_player_number_;
        note_data_edit_.SetTapNote(iCol, iSongIndex, tn);
        CheckNumberOfNotesAndUndo();
      }
    }
      return true;

    case EDIT_BUTTON_CYCLE_TAP_LEFT: {
      switch (selected_tap_.type_) {
        case TapNoteType_Tap:
          selected_tap_ = TAP_ORIGINAL_FAKE;
          break;
        case TapNoteType_Mine:
          selected_tap_ = TAP_ORIGINAL_TAP;
          break;
        case TapNoteType_Lift:
          selected_tap_ = TAP_ORIGINAL_MINE;
          break;
        case TapNoteType_Fake:
          selected_tap_ = TAP_ORIGINAL_LIFT;
          break;
          DEFAULT_FAIL(selected_tap_.type_);
      }
      return true;
    }
    case EDIT_BUTTON_CYCLE_TAP_RIGHT: {
      switch (selected_tap_.type_) {
        case TapNoteType_Tap:
          selected_tap_ = TAP_ORIGINAL_MINE;
          break;
        case TapNoteType_Mine:
          selected_tap_ = TAP_ORIGINAL_LIFT;
          break;
        case TapNoteType_Lift:
          selected_tap_ = TAP_ORIGINAL_FAKE;
          break;
        case TapNoteType_Fake:
          selected_tap_ = TAP_ORIGINAL_TAP;
          break;
          DEFAULT_FAIL(selected_tap_.type_);
      }
      return true;
    }
    case EDIT_BUTTON_CYCLE_SEGMENT_LEFT: {
      int tmp = enum_add2(this->current_cycle_segment_, -1);
      wrap(*ConvertValue<int>(&tmp), NUM_TimingSegmentType);
      current_cycle_segment_ = (TimingSegmentType)tmp;
      return true;
    }
    case EDIT_BUTTON_CYCLE_SEGMENT_RIGHT: {
      int tmp = enum_add2(this->current_cycle_segment_, +1);
      wrap(*ConvertValue<int>(&tmp), NUM_TimingSegmentType);
      current_cycle_segment_ = (TimingSegmentType)tmp;
      return true;
    }
    case EDIT_BUTTON_SCROLL_SPEED_UP:
    case EDIT_BUTTON_SCROLL_SPEED_DOWN: {
      PlayerState* pPlayerState =
          const_cast<PlayerState*>(note_field_edit_.GetPlayerState());
      float fScrollSpeed =
          pPlayerState->player_options_.GetSong().scroll_speed_;

      int iSpeed = 0;
      for (size_t i = 0; i < scroll_speed_presets_.size(); ++i) {
        if (scroll_speed_presets_[i] == fScrollSpeed) {
          iSpeed = static_cast<int>(i);
          break;
        }
      }

      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_SCROLL_SPEED_DOWN:
          INVERT_SCROLL_BUTTONS ? ++iSpeed : --iSpeed;
          break;
        case EDIT_BUTTON_SCROLL_SPEED_UP:
          INVERT_SCROLL_BUTTONS ? --iSpeed : ++iSpeed;
          break;
      }
      iSpeed = std::clamp(iSpeed, 0, (int)scroll_speed_presets_.size() - 1);

      if (scroll_speed_presets_[iSpeed] != fScrollSpeed) {
        sound_marker_.Play(true);
        fScrollSpeed = scroll_speed_presets_[iSpeed];
      }

      PO_GROUP_ASSIGN(
          pPlayerState->player_options_, ModsLevel_Song, scroll_speed_,
          fScrollSpeed);
    }

      return true;
    case EDIT_BUTTON_SCROLL_HOME:
      ScrollTo(0);
      return true;
    case EDIT_BUTTON_SCROLL_END:
      ScrollTo(note_data_edit_.GetLastBeat());
      return true;
    case EDIT_BUTTON_SCROLL_UP_LINE:
    case EDIT_BUTTON_SCROLL_UP_PAGE:
    case EDIT_BUTTON_SCROLL_UP_TS:
    case EDIT_BUTTON_SCROLL_DOWN_LINE:
    case EDIT_BUTTON_SCROLL_DOWN_PAGE:
    case EDIT_BUTTON_SCROLL_DOWN_TS: {
      float fBeatsToMove = 0.f;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_SCROLL_UP_LINE:
        case EDIT_BUTTON_SCROLL_DOWN_LINE:
          fBeatsToMove = NoteTypeToBeat(snap_display_.GetNoteType());
          if (EditB == EDIT_BUTTON_SCROLL_UP_LINE) {
            fBeatsToMove *= -1;
          }
          break;
        case EDIT_BUTTON_SCROLL_UP_PAGE:
        case EDIT_BUTTON_SCROLL_DOWN_PAGE:
          fBeatsToMove = 4;
          if (EditB == EDIT_BUTTON_SCROLL_UP_PAGE) {
            fBeatsToMove *= -1;
          }
          break;
        case EDIT_BUTTON_SCROLL_UP_TS:
        case EDIT_BUTTON_SCROLL_DOWN_TS:
          fBeatsToMove = beatsPerMeasure;
          if (EditB == EDIT_BUTTON_SCROLL_UP_TS) {
            fBeatsToMove *= -1;
          }
          break;
      }

      // Apply scroll acceleration multiplier for LINE scrolls (mouse wheel)
      if ((EditB == EDIT_BUTTON_SCROLL_UP_LINE ||
           EditB == EDIT_BUTTON_SCROLL_DOWN_LINE) &&
          input.device_i_.device_ == DEVICE_MOUSE &&
          (input.device_i_.button_ == MOUSE_WHEELUP ||
           input.device_i_.button_ == MOUSE_WHEELDOWN) &&
          scroll_acceleration_multiplier_ > 1.0f) {
        fBeatsToMove *= scroll_acceleration_multiplier_;
      }

      if (player_state_edit_.player_options_.GetSong()
              .scrolls_[PlayerOptions::SCROLL_REVERSE] > 0.5) {
        fBeatsToMove *= -1;
      }

      float fDestinationBeat = GetBeat() + fBeatsToMove;
      fDestinationBeat = Quantize(
          fDestinationBeat, NoteTypeToBeat(snap_display_.GetNoteType()));

      ScrollTo(fDestinationBeat);
    }
      return true;
    case EDIT_BUTTON_SCROLL_NEXT_MEASURE: {
      float fDestinationBeat = GetBeat() + beatsPerMeasure;
      fDestinationBeat = ftruncf(fDestinationBeat, (float)beatsPerMeasure);
      ScrollTo(fDestinationBeat);
      return true;
    }
    case EDIT_BUTTON_SCROLL_PREV_MEASURE: {
      float fDestinationBeat = QuantizeUp(GetBeat(), (float)beatsPerMeasure);
      fDestinationBeat -= (float)beatsPerMeasure;
      ScrollTo(fDestinationBeat);
      return true;
    }
    case EDIT_BUTTON_SCROLL_NEXT: {
      int iRow = BeatToNoteRow(GetBeat());
      NoteDataUtil::GetNextEditorPosition(note_data_edit_, iRow);
      ScrollTo(NoteRowToBeat(iRow));
    }
      return true;
    case EDIT_BUTTON_SCROLL_PREV: {
      int iRow = BeatToNoteRow(GetBeat());
      NoteDataUtil::GetPrevEditorPosition(note_data_edit_, iRow);
      ScrollTo(NoteRowToBeat(iRow));
    }
      return true;
    case EDIT_BUTTON_SEGMENT_NEXT: {
      // TODO: Work around Stops and Delays. We MAY have to separate them.
      const TimingData& timing = GetAppropriateTiming();
      ScrollTo(timing.GetNextSegmentBeatAtBeat(
          this->current_cycle_segment_, GetBeat()));
    }
      return true;
    case EDIT_BUTTON_SEGMENT_PREV: {
      // TODO: Work around Stops and Delays. We MAY have to separate them.
      const TimingData& timing = GetAppropriateTiming();
      ScrollTo(timing.GetPreviousSegmentBeatAtBeat(
          this->current_cycle_segment_, GetBeat()));
    }
      return true;
    case EDIT_BUTTON_SNAP_NEXT:
      if (snap_display_.PrevSnapMode()) {
        OnSnapModeChange();
      }
      return true;
    case EDIT_BUTTON_SNAP_PREV:
      if (snap_display_.NextSnapMode()) {
        OnSnapModeChange();
      }
      return true;
    case EDIT_BUTTON_LAY_SELECT: {
      const int iCurrentRow =
          BeatToNoteRow(GetAppropriatePosition().song_beat_);
      if (note_field_edit_.begin_marker_ == -1 &&
          note_field_edit_.end_marker_ == -1) {
        // lay begin marker
        note_field_edit_.begin_marker_ =
            BeatToNoteRow(GetAppropriatePosition().song_beat_);
      } else if (note_field_edit_.end_marker_ == -1)  // only begin marker is
                                                      // laid
      {
        if (iCurrentRow == note_field_edit_.begin_marker_) {
          note_field_edit_.begin_marker_ = -1;
        } else {
          note_field_edit_.end_marker_ =
              std::max(note_field_edit_.begin_marker_, iCurrentRow);
          note_field_edit_.begin_marker_ =
              std::min(note_field_edit_.begin_marker_, iCurrentRow);
        }
      } else  // both markers are laid
      {
        note_field_edit_.begin_marker_ = iCurrentRow;
        note_field_edit_.end_marker_ = -1;
      }
      sound_marker_.Play(true);
    }
      return true;
    case EDIT_BUTTON_OPEN_AREA_MENU: {
      // TODO: Improve behavior if timing changes are shifted down on beat 0.
      g_AreaMenu.rows_[shift_pauses_forward].b_enabled_ = (GetBeat() != 0);
      g_AreaMenu.rows_[paste_at_current_beat].b_enabled_ =
          !clipboard_.IsEmpty();
      g_AreaMenu.rows_[paste_at_begin_marker].b_enabled_ =
          !clipboard_.IsEmpty() != 0 && note_field_edit_.begin_marker_ != -1;
      g_AreaMenu.rows_[undo].b_enabled_ = !undo_stack_.empty();
      EditMiniMenu(&g_AreaMenu, SM_BackFromAreaMenu);
    }
      return true;
    case EDIT_BUTTON_OPEN_ALTER_MENU: {
      bool bAreaSelected = note_field_edit_.begin_marker_ != -1 &&
                           note_field_edit_.end_marker_ != -1;
      if (!bAreaSelected) {
        SCREENMAN->SystemMessage(ALTER_MENU_NO_SELECTION);
        SCREENMAN->PlayInvalidSound();
      } else {
        bool isRoutine = (input_player_number_ != PLAYER_INVALID);
        g_AlterMenu.rows_[routine_invert_notes].b_enabled_ = isRoutine;
        g_AlterMenu.rows_[routine_mirror_1_to_2].b_enabled_ = isRoutine;
        g_AlterMenu.rows_[routine_mirror_2_to_1].b_enabled_ = isRoutine;
        EditMiniMenu(&g_AlterMenu, SM_BackFromAlterMenu);
      }
      return true;
    }
    case EDIT_BUTTON_OPEN_EDIT_MENU:
      EditMiniMenu(&g_MainMenu, SM_BackFromMainMenu);
      return true;
    case EDIT_BUTTON_OPEN_INPUT_HELP:
      DoHelp();
      return true;
    case EDIT_BUTTON_OPEN_TIMING_MENU: {
      DisplayTimingMenu();
      return true;
    }
    case EDIT_BUTTON_OPEN_NEXT_STEPS:
    case EDIT_BUTTON_OPEN_PREV_STEPS: {
      // don't keep undo when changing Steps
      ClearUndo();

      // get the second of the current step.
      float curSecond =
          GetAppropriateTiming().GetElapsedTimeFromBeat(GetBeat());

      // save current steps
      Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
      ASSERT(pSteps != nullptr);
      pSteps->SetNoteData(note_data_edit_);

      // Get all Steps of this StepsType
      const StepsType st = pSteps->steps_type_;
      std::vector<Steps*> vSteps;
      SongUtil::GetSteps(GAMESTATE->cur_song_, vSteps, st);

      // Sort them by difficulty.
      StepsUtil::SortStepsByTypeAndDifficulty(vSteps);

      // Find out what index the current Steps are
      std::vector<Steps*>::iterator it =
          find(vSteps.begin(), vSteps.end(), pSteps);
      ASSERT(it != vSteps.end());

      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_OPEN_PREV_STEPS:
          if (it == vSteps.begin()) {
            SCREENMAN->PlayInvalidSound();
            return true;
          }
          it--;
          break;
        case EDIT_BUTTON_OPEN_NEXT_STEPS:
          it++;
          if (it == vSteps.end()) {
            SCREENMAN->PlayInvalidSound();
            return true;
          }
          break;
      }

      pSteps = *it;
      GAMESTATE->cur_steps_[PLAYER_1].Set(pSteps);
      steps_ = pSteps;
      pSteps->GetNoteData(note_data_edit_);
      note_field_edit_.LoadWaveform(pSteps->GetMusicPath());

      std::string s = ssprintf(
          (SWITCHED_TO.GetValue() + " %s %s '%s' (%d of %d)").c_str(),
          GAMEMAN->GetStepsTypeInfo(st).name_,
          DifficultyToString(pSteps->GetDifficulty()).c_str(),
          pSteps->GetChartName().c_str(), it - vSteps.begin() + 1,
          int(vSteps.size()));
      SCREENMAN->SystemMessage(s);
      sound_switch_steps_.Play(true);
      // Reload the music because it can be different for every steps. -Kyz
      auto_keysounds_.FinishLoading();
      sound_music_ = auto_keysounds_.GetSound();

      ScrollTo(GetAppropriateTiming().GetBeatFromElapsedTime(curSecond));
    }
      return true;
    case EDIT_BUTTON_BPM_UP:
    case EDIT_BUTTON_BPM_DOWN: {
      float fBPM = GetAppropriateTiming().GetBPMAtBeat(GetBeat());
      float fDelta;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_BPM_UP:
          fDelta = +0.020f;
          break;
        case EDIT_BUTTON_BPM_DOWN:
          fDelta = -0.020f;
          break;
      }
      if (EditIsBeingPressed(EDIT_BUTTON_ADJUST_FINE)) {
        fDelta /= 20;  // .001 bpm
      } else if (input.type_ == IET_REPEAT) {
        if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
          fDelta *= 10;
        } else {
          fDelta *= 40;
        }
      }

      float fNewBPM = fBPM + fDelta;
      if (fNewBPM > 0.0f) {
        GetAppropriateTimingForUpdate().AddSegment(
            BPMSegment(GetRow(), fNewBPM));
      }
      (fDelta > 0 ? sound_value_increase_ : sound_value_decrease_).Play(true);
      SetDirty(true);
    }
      return true;
    case EDIT_BUTTON_STOP_UP:
    case EDIT_BUTTON_STOP_DOWN: {
      float fDelta;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_STOP_UP:
          fDelta = +0.020f;
          break;
        case EDIT_BUTTON_STOP_DOWN:
          fDelta = -0.020f;
          break;
      }
      if (EditIsBeingPressed(EDIT_BUTTON_ADJUST_FINE)) {
        fDelta /= 20;  // 1ms
      } else if (input.type_ == IET_REPEAT) {
        if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
          fDelta *= 10;
        } else {
          fDelta *= 40;
        }
      }

      // is there a StopSegment on the current row?
      TimingData& timing = GetAppropriateTimingForUpdate();
      StopSegment* seg = timing.GetStopSegmentAtRow(GetRow());
      int i = timing.GetSegmentIndexAtRow(SEGMENT_STOP, GetRow());
      if (i == -1 || seg->GetRow() != GetRow())  // invalid
      {
        if (fDelta > 0) {
          timing.AddSegment(StopSegment(GetRow(), fDelta));
        } else {
          return false;
        }
      } else {
        std::vector<TimingSegment*>& stops =
            timing.GetTimingSegments(SEGMENT_STOP);
        seg->SetPause(seg->GetPause() + fDelta);
        if (seg->GetPause() <= 0) {
          stops.erase(stops.begin() + i, stops.begin() + i + 1);
        }
      }

      (fDelta > 0 ? sound_value_increase_ : sound_value_decrease_).Play(true);
      SetDirty(true);
    }
      return true;
    // TODO: Combine the stop and delay call somehow?
    case EDIT_BUTTON_DELAY_UP:
    case EDIT_BUTTON_DELAY_DOWN: {
      float fDelta;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_DELAY_UP:
          fDelta = +0.020f;
          break;
        case EDIT_BUTTON_DELAY_DOWN:
          fDelta = -0.020f;
          break;
      }
      if (EditIsBeingPressed(EDIT_BUTTON_ADJUST_FINE)) {
        fDelta /= 20;  // 1ms
      } else if (input.type_ == IET_REPEAT) {
        if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
          fDelta *= 10;
        } else {
          fDelta *= 40;
        }
      }

      // is there a StopSegment on the current row?
      TimingData& timing = GetAppropriateTimingForUpdate();
      DelaySegment* seg = timing.GetDelaySegmentAtRow(GetRow());
      int i = timing.GetSegmentIndexAtRow(SEGMENT_DELAY, GetRow());
      if (i == -1 || seg->GetRow() != GetRow())  // invalid
      {
        if (fDelta > 0) {
          timing.AddSegment(DelaySegment(GetRow(), fDelta));
        } else {
          return false;
        }
      } else {
        std::vector<TimingSegment*>& stops =
            timing.GetTimingSegments(SEGMENT_DELAY);
        seg->SetPause(seg->GetPause() + fDelta);
        if (seg->GetPause() <= 0) {
          stops.erase(stops.begin() + i, stops.begin() + i + 1);
        }
      }

      (fDelta > 0 ? sound_value_increase_ : sound_value_decrease_).Play(true);
      SetDirty(true);
    }
      return true;
    case EDIT_BUTTON_OFFSET_UP:
    case EDIT_BUTTON_OFFSET_DOWN: {
      float fDelta;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_OFFSET_DOWN:
          fDelta = -0.02f;
          break;
        case EDIT_BUTTON_OFFSET_UP:
          fDelta = +0.02f;
          break;
      }
      if (EditIsBeingPressed(EDIT_BUTTON_ADJUST_FINE)) {
        fDelta /= 20; /* 1ms */
      } else if (input.type_ == IET_REPEAT) {
        if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
          fDelta *= 10;
        } else {
          fDelta *= 40;
        }
      }
      GetAppropriateTimingForUpdate().beat0_offset_in_seconds_ += fDelta;
      (fDelta > 0 ? sound_value_increase_ : sound_value_decrease_).Play(true);
      if (GAMESTATE->is_using_step_timing_) {
        GAMESTATE->cur_steps_[PLAYER_1]->GetStepsAttacks().UpdateStartTimes(
            fDelta);
      } else {
        GAMESTATE->cur_song_->attacks_.UpdateStartTimes(fDelta);
        GAMESTATE->cur_song_->music_sample_start_seconds_ += fDelta;
      }
      SetDirty(true);
    }
      return true;
    case EDIT_BUTTON_SAMPLE_START_UP:
    case EDIT_BUTTON_SAMPLE_START_DOWN:
    case EDIT_BUTTON_SAMPLE_LENGTH_DOWN:
    case EDIT_BUTTON_SAMPLE_LENGTH_UP: {
      float fDelta;
      switch (EditB) {
        DEFAULT_FAIL(EditB);
        case EDIT_BUTTON_SAMPLE_START_DOWN:
          fDelta = -0.02f;
          break;
        case EDIT_BUTTON_SAMPLE_START_UP:
          fDelta = +0.02f;
          break;
        case EDIT_BUTTON_SAMPLE_LENGTH_DOWN:
          fDelta = -0.02f;
          break;
        case EDIT_BUTTON_SAMPLE_LENGTH_UP:
          fDelta = +0.02f;
          break;
      }

      if (input.type_ == IET_REPEAT) {
        if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
          fDelta *= 10;
        } else {
          fDelta *= 40;
        }
      }

      if (EditB == EDIT_BUTTON_SAMPLE_LENGTH_DOWN ||
          EditB == EDIT_BUTTON_SAMPLE_LENGTH_UP) {
        song_->music_sample_length_seconds_ += fDelta;
        song_->music_sample_length_seconds_ =
            std::max(song_->music_sample_length_seconds_, 0.0f);
      } else {
        song_->music_sample_start_seconds_ += fDelta;
        song_->music_sample_start_seconds_ =
            std::max(song_->music_sample_start_seconds_, 0.0f);
      }
      (fDelta > 0 ? sound_value_increase_ : sound_value_decrease_).Play(true);
      SetDirty(true);
    }
      return true;
    case EDIT_BUTTON_PLAY_SAMPLE_MUSIC:
      PlayPreviewMusic();
      return true;
    case EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU:
    case EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU:
      if (!GAMESTATE->is_using_step_timing_) {
        switch (EditB) {
          DEFAULT_FAIL(EditB);
          case EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU:
            g_CurrentBGChangeLayer = BACKGROUND_LAYER_1;
            break;
          case EDIT_BUTTON_OPEN_BGCHANGE_LAYER2_MENU:
            g_CurrentBGChangeLayer = BACKGROUND_LAYER_2;
            break;
        }

        {
          // Fill in option names
          std::vector<std::string> vThrowAway;

          MenuDef& menu = g_BackgroundChange;

          menu.rows_[layer].choices_[0] =
              ssprintf("%d", g_CurrentBGChangeLayer);
          BackgroundUtil::GetBackgroundTransitions(
              "", vThrowAway, menu.rows_[transition].choices_);
          g_BackgroundChange.rows_[transition].choices_.insert(
              g_BackgroundChange.rows_[transition].choices_.begin(),
              "None");  // add "no transition"
          BackgroundUtil::GetBackgroundEffects(
              "", vThrowAway, menu.rows_[effect].choices_);
          menu.rows_[effect].choices_.insert(
              menu.rows_[effect].choices_.begin(),
              "Default");  // add "default effect"

          BackgroundUtil::GetSongBGAnimations(
              song_, "", vThrowAway,
              menu.rows_[file1_song_bganimation].choices_);
          BackgroundUtil::GetSongMovies(
              song_, "", vThrowAway, menu.rows_[file1_song_movie].choices_);
          BackgroundUtil::GetSongBitmaps(
              song_, "", vThrowAway, menu.rows_[file1_song_still].choices_);
          BackgroundUtil::GetGlobalBGAnimations(
              song_, "", vThrowAway,
              menu.rows_[file1_global_bganimation]
                  .choices_);  // nullptr to get all background files
          BackgroundUtil::GetGlobalRandomMovies(
              song_, "", vThrowAway, menu.rows_[file1_global_movie].choices_,
              false, false);  // all backgrounds
          BackgroundUtil::GetGlobalRandomMovies(
              song_, "", vThrowAway,
              menu.rows_[file1_global_movie_song_group].choices_, false,
              true);  // song group's backgrounds
          BackgroundUtil::GetGlobalRandomMovies(
              song_, "", vThrowAway,
              menu.rows_[file1_global_movie_song_group_and_genre].choices_,
              true,
              true);  // song group and genre's backgrounds

          menu.rows_[file2_type].choices_ = menu.rows_[file1_type].choices_;
          menu.rows_[file2_song_bganimation].choices_ =
              menu.rows_[file1_song_bganimation].choices_;
          menu.rows_[file2_song_movie].choices_ =
              menu.rows_[file1_song_movie].choices_;
          menu.rows_[file2_song_still].choices_ =
              menu.rows_[file1_song_still].choices_;
          menu.rows_[file2_global_bganimation].choices_ =
              menu.rows_[file1_global_bganimation].choices_;
          menu.rows_[file2_global_movie].choices_ =
              menu.rows_[file1_global_movie].choices_;
          menu.rows_[file2_global_movie_song_group].choices_ =
              menu.rows_[file1_global_movie_song_group].choices_;
          menu.rows_[file2_global_movie_song_group_and_genre].choices_ =
              menu.rows_[file1_global_movie_song_group_and_genre].choices_;

          // Fill in lines enabled/disabled
          bool bAlreadyBGChangeHere = false;
          BackgroundChange bgChange;
          for (BackgroundChange& bgc :
               song_->GetBackgroundChanges(g_CurrentBGChangeLayer)) {
            if (bgc.start_beat_ ==
                GAMESTATE->player_state_[main_player_]->position_.song_beat_) {
              bAlreadyBGChangeHere = true;
              bgChange = bgc;
            }
          }

#define FILL_ENABLED(x) \
  menu.rows_[x].b_enabled_ = menu.rows_[x].choices_.size() > 0;
          FILL_ENABLED(transition);
          FILL_ENABLED(effect);
          FILL_ENABLED(file1_song_bganimation);
          FILL_ENABLED(file1_song_movie);
          FILL_ENABLED(file1_song_still);
          FILL_ENABLED(file1_global_bganimation);
          FILL_ENABLED(file1_global_movie);
          FILL_ENABLED(file1_global_movie_song_group);
          FILL_ENABLED(file1_global_movie_song_group_and_genre);
          FILL_ENABLED(file2_song_bganimation);
          FILL_ENABLED(file2_song_movie);
          FILL_ENABLED(file2_song_still);
          FILL_ENABLED(file2_global_bganimation);
          FILL_ENABLED(file2_global_movie);
          FILL_ENABLED(file2_global_movie_song_group);
          FILL_ENABLED(file2_global_movie_song_group_and_genre);
#undef FILL_ENABLED
          menu.rows_[delete_change].b_enabled_ = bAlreadyBGChangeHere;

          // set default choices
          menu.rows_[rate].SetDefaultChoiceIfPresent(
              ssprintf("%2.0f%%", bgChange.rate_ * 100));
          menu.rows_[transition].SetDefaultChoiceIfPresent(
              bgChange.transition_);
          menu.rows_[effect].SetDefaultChoiceIfPresent(bgChange.def_.effect_);
          menu.rows_[file1_type].default_choice_ = none;
          if (bgChange.def_.file1_ == RANDOM_BACKGROUND_FILE) {
            menu.rows_[file1_type].default_choice_ = dynamic_random;
          }
          if (menu.rows_[file1_song_bganimation].SetDefaultChoiceIfPresent(
                  bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = song_bganimation;
          }
          if (menu.rows_[file1_song_movie].SetDefaultChoiceIfPresent(
                  bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = song_movie;
          }
          if (menu.rows_[file1_song_still].SetDefaultChoiceIfPresent(
                  bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = song_bitmap;
          }
          if (menu.rows_[file1_global_bganimation].SetDefaultChoiceIfPresent(
                  bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = global_bganimation;
          }
          if (menu.rows_[file1_global_movie].SetDefaultChoiceIfPresent(
                  bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = global_movie;
          }
          if (menu.rows_[file1_global_movie_song_group]
                  .SetDefaultChoiceIfPresent(bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ = global_movie_song_group;
          }
          if (menu.rows_[file1_global_movie_song_group_and_genre]
                  .SetDefaultChoiceIfPresent(bgChange.def_.file1_)) {
            menu.rows_[file1_type].default_choice_ =
                global_movie_song_group_and_genre;
          }
          menu.rows_[file2_type].default_choice_ = none;
          if (bgChange.def_.file2_ == RANDOM_BACKGROUND_FILE) {
            menu.rows_[file2_type].default_choice_ = dynamic_random;
          }
          if (menu.rows_[file2_song_bganimation].SetDefaultChoiceIfPresent(
                  bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = song_bganimation;
          }
          if (menu.rows_[file2_song_movie].SetDefaultChoiceIfPresent(
                  bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = song_movie;
          }
          if (menu.rows_[file2_song_still].SetDefaultChoiceIfPresent(
                  bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = song_bitmap;
          }
          if (menu.rows_[file2_global_bganimation].SetDefaultChoiceIfPresent(
                  bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = global_bganimation;
          }
          if (menu.rows_[file2_global_movie].SetDefaultChoiceIfPresent(
                  bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = global_movie;
          }
          if (menu.rows_[file2_global_movie_song_group]
                  .SetDefaultChoiceIfPresent(bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ = global_movie_song_group;
          }
          if (menu.rows_[file2_global_movie_song_group_and_genre]
                  .SetDefaultChoiceIfPresent(bgChange.def_.file2_)) {
            menu.rows_[file2_type].default_choice_ =
                global_movie_song_group_and_genre;
          }
          menu.rows_[color1].SetDefaultChoiceIfPresent(bgChange.def_.color1_);
          menu.rows_[color2].SetDefaultChoiceIfPresent(bgChange.def_.color2_);

          EditMiniMenu(&g_BackgroundChange, SM_BackFromBGChange);
        }
      } else {
        SCREENMAN->SystemMessage(BG_CHANGE_STEP_TIMING);
        SCREENMAN->PlayInvalidSound();
      }
      return true;

    case EDIT_BUTTON_OPEN_COURSE_MENU: {
      g_CourseMode.rows_[0].choices_.clear();
      g_CourseMode.rows_[0].choices_.push_back(
          CommonMetrics::LocalizeOptionItem("Off", false));
      g_CourseMode.rows_[0].default_choice_ = 0;

      std::vector<Course*> courses;
      SONGMAN->GetAllCourses(courses, false);
      for (unsigned i = 0; i < courses.size(); ++i) {
        const Course* crs = courses[i];

        bool bUsesThisSong = false;
        for (unsigned e = 0; e < crs->entries_.size(); ++e) {
          if (crs->entries_[e].song_id_.ToSong() != song_) {
            continue;
          }
          bUsesThisSong = true;
        }

        if (bUsesThisSong) {
          g_CourseMode.rows_[0].choices_.push_back(crs->GetDisplayFullTitle());
          if (crs == GAMESTATE->cur_course_) {
            g_CourseMode.rows_[0].default_choice_ =
                g_CourseMode.rows_[0].choices_.size() - 1;
          }
        }
      }

      EditMiniMenu(&g_CourseMode, SM_BackFromCourseModeMenu);
    }
      return true;
    case EDIT_BUTTON_OPEN_STEP_ATTACK_MENU: {
      this->DoStepAttackMenu();
      return true;
    }
    case EDIT_BUTTON_OPEN_COURSE_ATTACK_MENU: {
      // TODO: Give Song/Step Timing switches/functions here?
      Course* pCourse = GAMESTATE->cur_course_;
      if (pCourse == nullptr) {
        return false;
      }
      CourseEntry& ce = pCourse->entries_[GAMESTATE->edit_course_entry_index_];
      float fStartTime = steps_->GetTimingData()->GetElapsedTimeFromBeat(
          GAMESTATE->player_state_[main_player_]->position_.song_beat_);
      int iAttack = FindAttackAtTime(ce.attacks_, fStartTime);

      if (iAttack >= 0) {
        const std::string sDuration =
            std::to_string(ce.attacks_[iAttack].secs_remaining_);

        g_InsertCourseAttack.rows_[remove].b_enabled_ = true;
        if (g_InsertCourseAttack.rows_[duration].choices_.size() == 9) {
          g_InsertCourseAttack.rows_[duration].choices_.push_back(sDuration);
        } else {
          g_InsertCourseAttack.rows_[duration].choices_.back() = sDuration;
        }
        g_InsertCourseAttack.rows_[duration].default_choice_ = 9;
      } else {
        if (g_InsertCourseAttack.rows_[duration].choices_.size() == 10) {
          g_InsertCourseAttack.rows_[duration].choices_.pop_back();
        }
        g_InsertCourseAttack.rows_[duration].default_choice_ = 3;
      }

      EditMiniMenu(&g_InsertCourseAttack, SM_BackFromInsertCourseAttack);
    }
      return true;
    case EDIT_BUTTON_ADD_STEP_MODS: {
      float start = -1;
      float end = -1;
      PlayerOptions po;
      selected_attack_.reset();

      if (note_field_edit_.begin_marker_ == -1)  // not highlighted
      {
        po.FromString("");
      } else {
        const TimingData& timing = GetAppropriateTiming();
        start = timing.GetElapsedTimeFromBeat(
            NoteRowToBeat(note_field_edit_.begin_marker_));
        AttackArray attacks = GetAttacksForEditing();
        int index = FindAttackAtTime(attacks, start);

        if (index >= 0) {
          po.FromString("");
          selected_attack_ = ResolveAttackIndex(index);
        }
        if (note_field_edit_.end_marker_ == -1) {
          end = song_->music_length_seconds_;
        } else {
          end = timing.GetElapsedTimeFromBeat(
              NoteRowToBeat(note_field_edit_.end_marker_));
        }
      }
      ModsGroup<PlayerOptions>& toEdit =
          GAMESTATE->player_state_[main_player_]->player_options_;
      this->original_player_options_.Assign(
          ModsLevel_Preferred, toEdit.GetPreferred());
      g_fLastInsertAttackPositionSeconds = start;
      g_fLastInsertAttackDurationSeconds = end - start;
      toEdit.Assign(ModsLevel_Stage, po);
      SCREENMAN->AddNewScreenToTop(
          SET_MOD_SCREEN, SM_BackFromInsertStepAttackPlayerOptions);
      return true;
    }
    case EDIT_BUTTON_ADD_COURSE_MODS: {
      float fStart, fEnd;
      PlayerOptions po;
      const Course* pCourse = GAMESTATE->cur_course_;
      if (pCourse == nullptr) {
        return false;
      }
      const CourseEntry& ce =
          pCourse->entries_[GAMESTATE->edit_course_entry_index_];

      if (note_field_edit_.begin_marker_ == -1) {
        fStart = -1;
        fEnd = -1;
        po.FromString(ce.modifiers_);
      } else {
        // TODO: Give Song/Step Timing switches/functions here?
        TimingData* timing = steps_->GetTimingData();
        fStart = timing->GetElapsedTimeFromBeat(
            NoteRowToBeat(note_field_edit_.begin_marker_));
        int iAttack = FindAttackAtTime(ce.attacks_, fStart);

        if (iAttack >= 0) {
          po.FromString(ce.attacks_[iAttack].modifiers_);
        }

        if (note_field_edit_.end_marker_ == -1) {
          fEnd = song_->music_length_seconds_;
        } else {
          fEnd = timing->GetElapsedTimeFromBeat(
              NoteRowToBeat(note_field_edit_.end_marker_));
        }
      }
      g_fLastInsertAttackPositionSeconds = fStart;
      g_fLastInsertAttackDurationSeconds = fEnd - fStart;
      GAMESTATE->player_state_[main_player_]->player_options_.Assign(
          ModsLevel_Stage, po);
      SCREENMAN->AddNewScreenToTop(
          SET_MOD_SCREEN, SM_BackFromInsertCourseAttackPlayerOptions);
    }
      return true;
    case EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP:
    case EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE: {
      bool bTryGenre =
          EditB == EDIT_BUTTON_BAKE_RANDOM_FROM_SONG_GROUP_AND_GENRE;
      std::string sName = GetOneBakedRandomFile(song_, bTryGenre);
      if (sName.empty()) {
        SCREENMAN->PlayInvalidSound();
        SCREENMAN->SystemMessage(NO_BACKGROUNDS_AVAILABLE);
      } else {
        BackgroundLayer iLayer = BACKGROUND_LAYER_1;
        BackgroundChange bgChange;
        bgChange.start_beat_ = GAMESTATE->position_.song_beat_;
        auto& changes = song_->GetBackgroundChanges(iLayer);
        for (auto bgc = changes.begin(); bgc != changes.end(); ++bgc) {
          if (bgc->start_beat_ == GAMESTATE->position_.song_beat_) {
            bgChange = *bgc;
            changes.erase(bgc);
            break;
          }
        }
        bgChange.def_.file1_ = sName;
        song_->AddBackgroundChange(iLayer, bgChange);
        sound_marker_.Play(true);
      }
    }
      return true;

    case EDIT_BUTTON_PLAY_FROM_START:
      HandleMainMenuChoice(play_whole_song);
      return true;

    case EDIT_BUTTON_PLAY_FROM_CURSOR:
      HandleMainMenuChoice(play_current_beat_to_end);
      return true;

    case EDIT_BUTTON_PLAY_SELECTION:
      HandleMainMenuChoice(play_selection);
      return true;
    case EDIT_BUTTON_RECORD_SELECTION:
      if (note_field_edit_.begin_marker_ != -1 &&
          note_field_edit_.end_marker_ != -1) {
        HandleAlterMenuChoice(record);
      } else {
        if (g_iDefaultRecordLength.Get() == -1) {
          start_playing_at_ =
              BeatToNoteRow(GetAppropriatePosition().song_beat_);
          stop_playing_at_ =
              std::max(start_playing_at_, note_data_edit_.GetLastRow() + 1);
        } else {
          start_playing_at_ = BeatToNoteRow(ftruncf(
              GetAppropriatePosition().song_beat_,
              g_iDefaultRecordLength.Get()));
          stop_playing_at_ =
              start_playing_at_ + BeatToNoteRow(g_iDefaultRecordLength.Get());
        }

        if (GAMESTATE->cur_steps_[0]->IsAnEdit()) {
          stop_playing_at_ = std::min(
              stop_playing_at_, BeatToNoteRow(GetMaximumBeatForNewNote()));
        }

        if (start_playing_at_ >= stop_playing_at_) {
          SCREENMAN->PlayInvalidSound();
          return true;
        }

        TransitionEditState(STATE_RECORDING);
      }
      return true;
    case EDIT_BUTTON_RECORD_FROM_CURSOR:
      start_playing_at_ = BeatToNoteRow(GetAppropriatePosition().song_beat_);
      stop_playing_at_ =
          std::max(start_playing_at_, note_data_edit_.GetLastRow());
      TransitionEditState(STATE_RECORDING);
      return true;

    case EDIT_BUTTON_INSERT:
      HandleAreaMenuChoice(insert_and_shift);
      SCREENMAN->PlayInvalidSound();
      return true;

    case EDIT_BUTTON_INSERT_SHIFT_PAUSES:
      HandleAreaMenuChoice(shift_pauses_forward);
      SCREENMAN->PlayInvalidSound();
      return true;

    case EDIT_BUTTON_DELETE:
      HandleAreaMenuChoice(delete_and_shift);
      SCREENMAN->PlayInvalidSound();
      return true;

    case EDIT_BUTTON_DELETE_SHIFT_PAUSES:
      HandleAreaMenuChoice(shift_pauses_backward);
      SCREENMAN->PlayInvalidSound();
      return true;

    case EDIT_BUTTON_RECORD_HOLD_LESS:
      record_hold_seconds -= .01f;
      if (record_hold_seconds <= 0.0f) {
        record_hold_seconds = 0.0f;
      }
      return true;

    case EDIT_BUTTON_RECORD_HOLD_MORE:
      record_hold_seconds += .01f;
      return true;

    case EDIT_BUTTON_RECORD_HOLD_RESET:
      record_hold_seconds = record_hold_default;
      return true;

    case EDIT_BUTTON_RECORD_HOLD_OFF:
      record_hold_seconds = 120.0f;
      return true;

    case EDIT_BUTTON_SAVE:
      HandleMainMenuChoice(ScreenEdit::save);
      return true;

    case EDIT_BUTTON_UNDO:
      Undo();
      return true;

    case EDIT_BUTTON_REDO:
      Redo();
      return true;

    case EDIT_BUTTON_CUT:
      CutSelectionToClipboard(IsShiftHeld());
      return true;

    case EDIT_BUTTON_COPY:
      CopySelectionToClipboard(IsShiftHeld());
      return true;

    case EDIT_BUTTON_PASTE:
      PasteClipboardAtCurrentBeat(IsShiftHeld());
      return true;

    case EDIT_BUTTON_TOGGLE_WAVEFORM:
      ToggleWaveform();
      return true;

    case EDIT_BUTTON_SWITCH_PLAYERS:
      if (input_player_number_ == PLAYER_INVALID) {
        return false;
      }
      enum_add(input_player_number_, 1);
      if (input_player_number_ == NUM_PLAYERS) {
        input_player_number_ = PLAYER_1;
      }
      sound_switch_player_.Play(true);
      return true;

    case EDIT_BUTTON_SWITCH_TIMINGS:
      GAMESTATE->is_using_step_timing_ = !GAMESTATE->is_using_step_timing_;
      sound_switch_timing_.Play(true);
      return true;
    default:
      return false;
  }
}

bool ScreenEdit::InputRecord(const InputEventPlus& input, EditButton EditB) {
  if (input.type_ == IET_FIRST_PRESS && EditB == EDIT_BUTTON_RETURN_TO_EDIT) {
    TransitionEditState(STATE_EDITING);
    return true;
  }

  if (input.pn_ != PLAYER_1) {
    return false;  // ignore
  }

  const int iCol =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->GameInputToColumn(input.game_i_);

  // Is this actually a column? If not, ignore the input.
  if (iCol == -1) {
    return false;
  }

  switch (input.type_) {
    DEFAULT_FAIL(input.type_);
    case IET_FIRST_PRESS: {
      if (EditIsBeingPressed(EDIT_BUTTON_REMOVE_NOTE)) {
        // Remove notes in Update.
        return false;
      }

      // Add a tap
      float fBeat = GetBeat();
      fBeat = Quantize(fBeat, NoteTypeToBeat(snap_display_.GetNoteType()));

      const int iRow = BeatToNoteRow(fBeat);
      if (iRow < 0) {
        return false;
      }

      // Don't add outside of the range.
      if (iRow < start_playing_at_ || iRow >= stop_playing_at_) {
        return false;
      }

      // Remove hold if any so that we don't have taps inside of a hold.
      int iHeadRow;
      if (note_data_record_.IsHoldNoteAtRow(iCol, iRow, &iHeadRow)) {
        note_data_record_.SetTapNote(iCol, iHeadRow, TAP_EMPTY);
      }

      TapNote tn = TAP_ORIGINAL_TAP;
      tn.pn_ = input_player_number_;
      note_data_record_.SetTapNote(iCol, iRow, tn);
      note_field_record_.Step(iCol, TNS_W1);
    }
      return true;
    case IET_REPEAT:
    case IET_RELEASE:
      // don't add or extend holds here; we do it in Update()
      return false;
  }
}

bool ScreenEdit::InputRecordPaused(
    const InputEventPlus& input, EditButton EditB) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;  // don't care
  }

  switch (EditB) {
    case EDIT_BUTTON_UNDO:
      /* We've already actually committed changes to m_NoteDataEdit, so all we
       * have to do to undo is Undo() as usual, and copy the note data back in.
       */
      Undo();
      note_data_record_.CopyAll(note_data_edit_);
      return true;

    case EDIT_BUTTON_PLAY_SELECTION:
      TransitionEditState(STATE_PLAYING);
      return true;

    case EDIT_BUTTON_RECORD_SELECTION:
      TransitionEditState(STATE_RECORDING);
      return true;

    case EDIT_BUTTON_RECORD_FROM_CURSOR:
      if (GAMESTATE->cur_steps_[0]->IsAnEdit()) {
        float fMaximumBeat = GetMaximumBeatForNewNote();
        if (NoteRowToBeat(stop_playing_at_) >= fMaximumBeat) {
          // We're already at the end.
          SCREENMAN->PlayInvalidSound();
          return true;
        }
      }

      // Pick up where we left off.
      {
        int iSize = stop_playing_at_ - start_playing_at_;
        start_playing_at_ = stop_playing_at_;
        stop_playing_at_ += iSize;

        if (GAMESTATE->cur_steps_[0]->IsAnEdit()) {
          stop_playing_at_ = std::min(
              stop_playing_at_, BeatToNoteRow(GetMaximumBeatForNewNote()));
        }
      }

      TransitionEditState(STATE_RECORDING);
      return true;

    case EDIT_BUTTON_RETURN_TO_EDIT:
      TransitionEditState(STATE_EDITING);
      return true;
    default:
      return false;
  }
}

bool ScreenEdit::InputPlay(const InputEventPlus& input, EditButton EditB) {
  switch (input.type_) {
    case IET_RELEASE:
    case IET_FIRST_PRESS:
      break;
    default:
      return false;
  }

  GameButtonType gbt =
      GAMESTATE->cur_game_->GetPerButtonInfo(input.game_i_.button_)->gbt_;
  if (player_manager_.HandleGameplayInput(input, gbt)) {
    return true;
  };

  if (gbt == GameButtonType_Menu && input.type_ == IET_FIRST_PRESS) {
    switch (EditB) {
      case EDIT_BUTTON_RETURN_TO_EDIT:
        /* When exiting play mode manually, leave the cursor where it is. */
        beat_to_return_to_ = GetAppropriatePosition().song_beat_;
        TransitionEditState(STATE_EDITING);
        return true;
      case EDIT_BUTTON_OFFSET_UP:
      case EDIT_BUTTON_OFFSET_DOWN: {
        float fOffsetDelta;
        switch (EditB) {
          DEFAULT_FAIL(EditB);
          case EDIT_BUTTON_OFFSET_DOWN:
            fOffsetDelta = -0.020f;
            break;
          case EDIT_BUTTON_OFFSET_UP:
            fOffsetDelta = +0.020f;
            break;
        }

        if (EditIsBeingPressed(EDIT_BUTTON_ADJUST_FINE)) {
          fOffsetDelta /= 20; /* 1ms */
        } else if (input.type_ == IET_REPEAT) {
          if (INPUTFILTER->GetSecsHeld(input.device_i_) < 1.0f) {
            fOffsetDelta *= 10;
          } else {
            fOffsetDelta *= 40;
          }
        }

        GetAppropriateTimingForUpdate().beat0_offset_in_seconds_ +=
            fOffsetDelta;
        if (!GAMESTATE->is_using_step_timing_) {
          GAMESTATE->cur_song_->music_sample_start_seconds_ += fOffsetDelta;
        }
      }
        return true;
      default:
        break;
    }
  }
  return false;
}

void ScreenEdit::TransitionEditState(EditState em) {
  EditState old = edit_state_;

  // If we're going from recording to paused, come back when we're done.
  if (old == STATE_RECORDING_PAUSED && em == STATE_PLAYING) {
    return_to_record_menu_after_play_ = true;
  }

  const bool bStateChanging = em != old;

#if 0
	// If switching out of record, open the menu.
	{
		bool bGoToRecordMenu = (old == STATE_RECORDING);
		if( return_to_record_menu_after_play_ && old == STATE_PLAYING )
		{
			bGoToRecordMenu = true;
			return_to_record_menu_after_play_ = false;
		}

		if( bGoToRecordMenu )
			em = STATE_RECORDING_PAUSED;
	}
#endif

  // If we're playing music or assist ticks when changing modes, stop.
  SOUND->StopMusic();
  if (sound_music_) {
    sound_music_->StopPlaying();
  }
  gameplay_assist_.StopPlaying();
  GAMESTATE->gameplay_lead_in_.Set(true);

  if (bStateChanging) {
    switch (old) {
      case STATE_EDITING:
        // If exiting EDIT mode, save the cursor position.
        beat_to_return_to_ = GetAppropriatePosition().song_beat_;
        break;

      case STATE_PLAYING:
        AdjustSync::HandleSongEnd();
        if (!GAMESTATE->is_using_step_timing_) {
          GAMESTATE->cur_steps_[main_player_]->timing_ = backup_step_timing_;
        }
        if (AdjustSync::IsSyncDataChanged()) {
          ScreenSaveSync::PromptSaveSync();
        }
        break;

      case STATE_RECORDING:
        SetDirty(true);
        if (!GAMESTATE->is_using_step_timing_) {
          GAMESTATE->cur_steps_[PLAYER_1]->timing_ = backup_step_timing_;
        }
        SaveUndo("Record");

        // delete old TapNotes in the range
        note_data_edit_.ClearRange(start_playing_at_, stop_playing_at_);
        note_data_edit_.CopyRange(
            note_data_record_, start_playing_at_, stop_playing_at_,
            start_playing_at_);
        note_data_record_.ClearAll();

        CheckNumberOfNotesAndUndo();
        break;
      default:
        break;
    }
  }

  // Set up player options for this mode. (EDITING uses m_PlayerStateEdit,
  // which we don't need to change.)
  if (em != STATE_EDITING) {
    // Stop displaying course attacks, if any.
    FOREACH_EnabledPlayer(pn) {
      GAMESTATE->player_state_[pn]->RemoveActiveAttacks();
      // Load the player's default PlayerOptions.
      GAMESTATE->player_state_[pn]->RebuildPlayerOptionsFromActiveAttacks();

      // Snap to current options.
      GAMESTATE->player_state_[pn]->player_options_.SetCurrentToLevel(
          ModsLevel_Stage);
    }
  }

  switch (em) {
    DEFAULT_FAIL(em);
    case STATE_EDITING:
      // Important: people will stop playing, change the BG and start again;
      // make sure we reload
      background_.Unload();
      foreground_.Unload();

      // Restore the cursor position + Quantize + Clamp
      SetBeat(
          std::max(
              0.0f, Quantize(
                        beat_to_return_to_,
                        NoteTypeToBeat(snap_display_.GetNoteType()))));
      GAMESTATE->in_step_editor_ = true;
      break;

    case STATE_PLAYING:
    case STATE_RECORDING: {
      note_data_edit_.RevalidateATIs(std::vector<int>(), false);
      if (bStateChanging) {
        AdjustSync::ResetOriginalSyncData();
      }

      /* Give a lead-in.  If we're loading Player, this must be done first.
       * Also be sure to get the right timing. */
      float fSeconds = GetAppropriateTiming().GetElapsedTimeFromBeat(
                           NoteRowToBeat(start_playing_at_)) -
                       PREFSMAN->edit_record_mode_lead_in_;
      GAMESTATE->UpdateSongPosition(
          fSeconds, GetAppropriateTiming(), RageZeroTimer);

      GAMESTATE->gameplay_lead_in_.Set(false);

      if (!GAMESTATE->is_using_step_timing_) {
        // Substitute the song timing for the step timing during
        // preview if we're in song mode
        backup_step_timing_ = GAMESTATE->cur_steps_[main_player_]->timing_;
        GAMESTATE->cur_steps_[main_player_]->timing_.Clear();
      }

      /* Reset the note skin, in case preferences have changed. */
      // XXX
      // GAMESTATE->ResetNoteSkins();
      // GAMESTATE->res
      GAMESTATE->in_step_editor_ = false;
      break;
    }
    case STATE_RECORDING_PAUSED:
      GAMESTATE->in_step_editor_ = false;
      break;
  }

  switch (em) {
    case STATE_PLAYING:
      // If we're in course display mode, set that up.
      SetupCourseAttacks();
      player_manager_.SetupAutoplay();
      player_manager_.ReloadNoteData(note_data_edit_);

      if (g_bEditorShowBGChangesPlay) {
        /* FirstBeat affects backgrounds, so commit changes to memory (not to
         * disk) and recalc it. */
        Steps* pSteps = GAMESTATE->cur_steps_[main_player_];
        ASSERT(pSteps != nullptr);
        pSteps->SetNoteData(note_data_edit_);
        song_->ReCalculateStepStatsAndLastSecond(false);

        // TODO: Background videos don't support seeking, when they do, make
        // sure to load the appropriate part of the video.
        background_.Unload();
        background_.LoadFromSong(song_);

        foreground_.Unload();
        foreground_.LoadFromSong(song_);
      }

      break;
    case STATE_RECORDING:
    case STATE_RECORDING_PAUSED:
      // initialize m_NoteFieldRecord
      note_data_record_.CopyAll(note_data_edit_);

      // highlight the section being recorded
      note_field_record_.begin_marker_ = start_playing_at_;
      note_field_record_.end_marker_ = stop_playing_at_;

      break;
    default:
      break;
  }

  // Show/hide depending on edit state (em)
  spr_overlay_->PlayCommand(EditStateToString(em));
  spr_underlay_->PlayCommand(EditStateToString(em));

  background_.SetVisible(g_bEditorShowBGChangesPlay && em != STATE_EDITING);
  text_input_tips_.SetVisible(em == STATE_EDITING);
  text_info_.SetVisible(em == STATE_EDITING);
  // Play the OnCommands again so that these will be re-hidden if the OnCommand
  // hides them.
  if (em == STATE_EDITING) {
    text_input_tips_.PlayCommand("On");
    text_info_.PlayCommand("On");
  }
  text_play_record_help_.SetVisible(em != STATE_EDITING);
  snap_display_.SetVisible(em == STATE_EDITING);
  note_field_edit_.SetVisible(em == STATE_EDITING);
  note_field_record_.SetVisible(
      em == STATE_RECORDING || em == STATE_RECORDING_PAUSED);

  player_manager_.SetVisible(em == STATE_PLAYING);
  foreground_.SetVisible(g_bEditorShowBGChangesPlay && em != STATE_EDITING);

  switch (em) {
    case STATE_PLAYING:
    case STATE_RECORDING: {
      const float fStartSeconds =
          GetAppropriateTiming().GetElapsedTimeFromBeat(GetBeat());
      LOG->Trace("Starting playback at %f", fStartSeconds);

      RageSoundParams p;
      p.speed_ = GAMESTATE->song_options_.GetCurrent().music_rate_;
      p.start_second_ = fStartSeconds;
      p.stop_mode_ = RageSoundParams::M_CONTINUE;
      sound_music_->SetProperty("AccurateSync", true);
      sound_music_->Play(false, &p);
      break;
    }
    default:
      break;
  }

  edit_state_ = em;
}

void ScreenEdit::ScrollTo(float fDestinationBeat) {
  rage_clamp(fDestinationBeat, 0, GetMaximumBeatForMoving());

  // Don't play the sound and do the hold note logic below if our position
  // didn't change.
  const float fOriginalBeat = GetAppropriatePosition().song_beat_;
  if (fOriginalBeat == fDestinationBeat) {
    return;
  }

  SetBeat(fDestinationBeat);

  // check to see if they're holding a button
  for (int n = 0; n < NUM_EDIT_BUTTON_COLUMNS; n++) {
    int iCol = n;

    // Ctrl + number = input to right half
    if (EditIsBeingPressed(EDIT_BUTTON_RIGHT_SIDE)) {
      ShiftToRightSide(iCol, note_data_edit_.GetNumTracks());
    }

    if (iCol >= note_data_edit_.GetNumTracks()) {
      continue;  // skip
    }

    EditButton b = EditButton(EDIT_BUTTON_COLUMN_0 + n);
    if (!EditIsBeingPressed(b)) {
      continue;
    }

    // create a new hold note
    int iStartRow = BeatToNoteRow(std::min(fOriginalBeat, fDestinationBeat));
    int iEndRow = BeatToNoteRow(std::max(fOriginalBeat, fDestinationBeat));

    // Don't SaveUndo.  We want to undo the whole hold, not just the last
    // segment that the user made.  Dragging the hold bigger can only absorb and
    // remove other taps, so dragging won't cause us to exceed the note limit.
    TapNote tn = EditIsBeingPressed(EDIT_BUTTON_LAY_ROLL)
                     ? TAP_ORIGINAL_ROLL_HEAD
                     : TAP_ORIGINAL_HOLD_HEAD;

    tn.pn_ = input_player_number_;
    note_data_edit_.AddHoldNote(iCol, iStartRow, iEndRow, tn);
  }

  if (EditIsBeingPressed(EDIT_BUTTON_SCROLL_SELECT)) {
    /* Shift is being held.
     * If this is the first time we've moved since shift was depressed,
     * the old position (before this move) becomes the start pos: */
    int iDestinationRow = BeatToNoteRow(fDestinationBeat);
    if (shift_anchor_ == -1) {
      shift_anchor_ = BeatToNoteRow(fOriginalBeat);
    }

    if (iDestinationRow == shift_anchor_) {
      // We're back at the anchor, so we have nothing selected.
      note_field_edit_.begin_marker_ = note_field_edit_.end_marker_ = -1;
    } else {
      note_field_edit_.begin_marker_ = shift_anchor_;
      note_field_edit_.end_marker_ = iDestinationRow;
      if (note_field_edit_.begin_marker_ > note_field_edit_.end_marker_) {
        std::swap(note_field_edit_.begin_marker_, note_field_edit_.end_marker_);
      }
    }
  }
  GAMESTATE->position_.music_seconds_ =
      GetAppropriateTiming().GetElapsedTimeFromBeat(fDestinationBeat);
  sound_change_line_.Play(true);
  spr_overlay_->PlayCommand("ScrollSong");
  spr_underlay_->PlayCommand("ScrollSong");
}

static LocalizedString NEW_KEYSOUND_FILE(
    "ScreenEdit", "Enter New Keysound File");

void ScreenEdit::HandleMessage(const Message& msg) {
  if (msg == "Judgment") {
    if (GAMESTATE->player_state_[main_player_]
            ->player_options_.GetCurrent()
            .mute_on_error_) {
      RageSoundReader* pSoundReader = auto_keysounds_.GetSharedSound();

      HoldNoteScore hns;
      msg.GetParam("HoldNoteScore", hns);
      TapNoteScore tns;
      msg.GetParam("TapNoteScore", tns);

      bool bOn = false;
      if (hns != HoldNoteScore_Invalid) {
        bOn = hns != HNS_LetGo;
      } else {
        bOn = tns != TNS_Miss;
      }

      if (pSoundReader) {
        pSoundReader->SetProperty("Volume", bOn ? 1.0f : 0.0f);
      }
    }
  }
  if (msg == Message_SongModified) {
    SetDirty(true);
  }
  Screen::HandleMessage(msg);
}

static LocalizedString SAVE_SUCCESSFUL("ScreenEdit", "Save successful.");
static LocalizedString AUTOSAVE_SUCCESSFUL(
    "ScreenEdit", "Autosave successful.");
static LocalizedString SAVE_SUCCESS_NO_SM_SPLIT_TIMING(
    "ScreenEdit", "save_success_no_sm_split_timing");

static LocalizedString ADD_NEW_MOD("ScreenEdit", "Adding New Mod");
static LocalizedString ADD_NEW_ATTACK("ScreenEdit", "Adding New Attack");
static LocalizedString EDIT_EXISTING_MOD("ScreenEdit", "Edit Existing Mod");
static LocalizedString EDIT_ATTACK_START("ScreenEdit", "Edit Attack Start");
static LocalizedString EDIT_ATTACK_LENGTH("ScreenEdit", "Edit Attack Length");

void ScreenEdit::HandleScreenMessage(const ScreenMessage SM) {
  if (SM != SM_UpdateTextInfo) {
    text_info_needs_update_ = true;
  }

  if (SM == SM_UpdateTextInfo) {
    UpdateTextInfo();
    this->PostScreenMessage(SM_UpdateTextInfo, 0.1f);
  } else if (SM == SM_GoToNextScreen) {
    GAMESTATE->edit_mode_ = EditMode_Invalid;
  } else if (SM == SM_BackFromMainMenu) {
    HandleMainMenuChoice(
        (MainMenuChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromAreaMenu) {
    AreaMenuChoice amc =
        static_cast<AreaMenuChoice>(ScreenMiniMenu::last_row_code_);
    const std::vector<int>& answers = ScreenMiniMenu::last_answers_;
    HandleAreaMenuChoice(amc, answers);
  } else if (SM == SM_BackFromAlterMenu) {
    HandleAlterMenuChoice(
        (AlterMenuChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromArbitraryRemap) {
    HandleArbitraryRemapping(ScreenTextEntry::last_answer_);
  } else if (SM == SM_BackFromStepsInformation) {
    HandleStepsInformationChoice(
        (StepsInformationChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromStepsData) {
    HandleStepsDataChoice(
        (StepsDataChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  }

  else if (SM == SM_BackFromSongInformation) {
    HandleSongInformationChoice(
        (SongInformationChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromTimingDataInformation) {
    HandleTimingDataInformationChoice(
        (TimingDataInformationChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromTimingDataChangeInformation) {
    HandleTimingDataChangeChoice(
        (TimingDataChangeChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
  } else if (SM == SM_BackFromAdjustSyncMenu) {
    if (!ScreenMiniMenu::cancelled_) {
      HandleAdjustSyncMenuChoice(ScreenMiniMenu::last_row_code_);
    }
  } else if (SM == SM_BackFromDifficultyMeterChange) {
    int i = StringToInt(ScreenTextEntry::last_answer_);
    GAMESTATE->cur_steps_[PLAYER_1]->SetMeter(i);
    SetDirty(true);
  } else if (
      SM == SM_BackFromBeat0Change && !ScreenTextEntry::cancelled_last_) {
    float fBeat0 = StringToFloat(ScreenTextEntry::last_answer_);

    TimingData& timing = GetAppropriateTimingForUpdate();
    float old = timing.beat0_offset_in_seconds_;
    timing.beat0_offset_in_seconds_ = fBeat0;
    float delta = timing.beat0_offset_in_seconds_ - old;

    if (GAMESTATE->is_using_step_timing_) {
      GAMESTATE->cur_steps_[PLAYER_1]->GetStepsAttacks().UpdateStartTimes(
          delta);
    } else {
      GAMESTATE->cur_song_->attacks_.UpdateStartTimes(delta);
      GAMESTATE->cur_song_->music_sample_start_seconds_ += delta;
    }

    SetDirty(true);
  } else if (SM == SM_BackFromBPMChange && !ScreenTextEntry::cancelled_last_) {
    float fBPM = StringToFloat(ScreenTextEntry::last_answer_);

    if (fBPM > 0) {
      GetAppropriateTimingForUpdate().AddSegment(BPMSegment(GetRow(), fBPM));
    }

    SetDirty(true);
  } else if (SM == SM_BackFromStopChange && !ScreenTextEntry::cancelled_last_) {
    float fStop = StringToFloat(ScreenTextEntry::last_answer_);

    if (fStop >= 0) {
      GetAppropriateTimingForUpdate().AddSegment(StopSegment(GetRow(), fStop));
    }

    SetDirty(true);
  } else if (
      SM == SM_BackFromDelayChange && !ScreenTextEntry::cancelled_last_) {
    float fDelay = StringToFloat(ScreenTextEntry::last_answer_);

    if (fDelay >= 0) {
      GetAppropriateTimingForUpdate().AddSegment(
          DelaySegment(GetRow(), fDelay));
    }

    SetDirty(true);
  } else if (
      SM == SM_BackFromTickcountChange && !ScreenTextEntry::cancelled_last_) {
    int iTick = StringToInt(ScreenTextEntry::last_answer_);

    if (iTick >= 0 && iTick <= ROWS_PER_BEAT) {
      GetAppropriateTimingForUpdate().AddSegment(
          TickcountSegment(GetRow(), iTick));
    }

    SetDirty(true);
  } else if (
      SM == SM_BackFromComboChange && !ScreenTextEntry::cancelled_last_) {
    int iCombo, iMiss;

    if (sscanf(
            ScreenTextEntry::last_answer_.c_str(), " %d / %d ", &iCombo,
            &iMiss) == 2) {
      GetAppropriateTimingForUpdate().AddSegment(
          ComboSegment(GetRow(), iCombo, iMiss));
    }

    SetDirty(true);
  } else if (
      SM == SM_BackFromLabelChange && !ScreenTextEntry::cancelled_last_) {
    std::string sLabel = ScreenTextEntry::last_answer_;

    if (!GetAppropriateTiming().DoesLabelExist(sLabel)) {
      // XXX: these should be in the NotesWriters where they're needed.
      Replace(sLabel, "=", "_");
      Replace(sLabel, ",", "_");
      GetAppropriateTimingForUpdate().AddSegment(
          LabelSegment(GetRow(), sLabel));
      SetDirty(true);
    }
  } else if (SM == SM_BackFromWarpChange && !ScreenTextEntry::cancelled_last_) {
    float fWarp = StringToFloat(ScreenTextEntry::last_answer_);
    if (fWarp >= 0)  // allow 0 to kill a warp.
    {
      GetAppropriateTimingForUpdate().SetWarpAtBeat(GetBeat(), fWarp);
      SetDirty(true);
    }
  } else if (
      SM == SM_BackFromSpeedPercentChange &&
      !ScreenTextEntry::cancelled_last_) {
    float fNum = StringToFloat(ScreenTextEntry::last_answer_);
    GetAppropriateTimingForUpdate().SetSpeedPercentAtBeat(GetBeat(), fNum);
    SetDirty(true);
  } else if (
      SM == SM_BackFromSpeedWaitChange && !ScreenTextEntry::cancelled_last_) {
    float fDen = StringToFloat(ScreenTextEntry::last_answer_);
    if (fDen >= 0) {
      GetAppropriateTimingForUpdate().SetSpeedWaitAtBeat(GetBeat(), fDen);
    }
    SetDirty(true);
  } else if (
      SM == SM_BackFromSpeedModeChange && !ScreenTextEntry::cancelled_last_) {
    if (ScreenTextEntry::last_answer_.substr(0, 1) == "b" ||
        ScreenTextEntry::last_answer_.substr(0, 1) == "B") {
      GetAppropriateTimingForUpdate().SetSpeedModeAtBeat(
          GetBeat(), SpeedSegment::UNIT_BEATS);
    } else if (
        ScreenTextEntry::last_answer_.substr(0, 1) == "s" ||
        ScreenTextEntry::last_answer_.substr(0, 1) == "S") {
      GetAppropriateTimingForUpdate().SetSpeedModeAtBeat(
          GetBeat(), SpeedSegment::UNIT_SECONDS);
    } else {
      int tmp = StringToInt(ScreenTextEntry::last_answer_);

      SpeedSegment::BaseUnit unit =
          (tmp == 0) ? SpeedSegment::UNIT_BEATS : SpeedSegment::UNIT_SECONDS;

      GetAppropriateTimingForUpdate().SetSpeedModeAtBeat(GetBeat(), unit);
    }
    SetDirty(true);
  } else if (
      SM == SM_BackFromScrollChange && !ScreenTextEntry::cancelled_last_) {
    float fNum = StringToFloat(ScreenTextEntry::last_answer_);
    GetAppropriateTimingForUpdate().SetScrollAtBeat(GetBeat(), fNum);
    SetDirty(true);
  } else if (SM == SM_BackFromFakeChange && !ScreenTextEntry::cancelled_last_) {
    float fFake = StringToFloat(ScreenTextEntry::last_answer_);
    if (fFake >= 0)  // allow 0 to kill a fake.
    {
      GetAppropriateTimingForUpdate().AddSegment(FakeSegment(GetRow(), fFake));
      SetDirty(true);
    }
  } else if (
      SM == SM_BackFromStepMusicChange && !ScreenTextEntry::cancelled_last_) {
    // Reload the music because it just changed. -Kyz
    auto_keysounds_.FinishLoading();
    sound_music_ = auto_keysounds_.GetSound();
  } else if (SM == SM_BackFromBGChange) {
    HandleBGChangeChoice(
        (BGChangeChoice)ScreenMiniMenu::last_row_code_,
        ScreenMiniMenu::last_answers_);
    SetDirty(true);
  } else if (SM == SM_BackFromCourseModeMenu) {
    const int num = ScreenMiniMenu::last_answers_[0];
    GAMESTATE->cur_course_.Set(nullptr);
    if (num != 0) {
      const std::string name = g_CourseMode.rows_[0].choices_[num];
      Course* pCourse = SONGMAN->FindCourse(name);

      int iCourseEntryIndex = -1;
      int index = 0;
      for (const CourseEntry& i : pCourse->entries_) {
        if (i.song_id_.ToSong() == GAMESTATE->cur_song_.Get()) {
          iCourseEntryIndex = index;
        }
        ++index;
      }

      ASSERT(iCourseEntryIndex != -1);

      GAMESTATE->cur_course_.Set(pCourse);
      GAMESTATE->edit_course_entry_index_.Set(iCourseEntryIndex);
      ASSERT(GAMESTATE->cur_course_ != nullptr);
    }
  } else if (SM == SM_BackFromKeysoundTrack) {
    const int track = ScreenMiniMenu::last_row_code_;
    const int tracks = note_data_edit_.GetNumTracks();
    const int row = this->GetRow();
    unsigned int sound = ScreenMiniMenu::last_answers_[track];
    std::vector<std::string>& kses = song_->keysound_file_;

    if (track < tracks) {
      if (sound == kses.size()) {
        // create a new sound (filename), point it.
        // if it's empty, make it an auto keysound.
        ScreenTextEntry::TextEntry(
            SM_BackFromNewKeysound, NEW_KEYSOUND_FILE, "", 64);
        return;
      }
      const TapNote& oldNote = note_data_edit_.GetTapNote(track, row);
      TapNote newNote =
          oldNote;  // need to lose the const. not feeling like casting.
      if (sound < kses.size()) {
        // set note at this row to use this keysound file.
        // if it's empty, make it an auto keysound.
        newNote.keysound_index_ = sound;
        if (newNote.type_ == TapNoteType_Empty) {
          newNote.type_ =
              TapNoteType_AutoKeysound;  // keysounds need something non empty.
        }
      } else  // sound > kses.size()
      {
        // remove the sound. if it's an auto keysound, make it empty.
        newNote.keysound_index_ = -1;
        if (newNote.type_ == TapNoteType_AutoKeysound) {
          newNote.type_ =
              TapNoteType_Empty;  // autoKeysound with no sound is pointless.
        }
      }
      note_data_edit_.SetTapNote(track, row, newNote);
    } else if (track == tracks) {
      kses.erase(kses.begin() + sound);
      // TODO: Make the following a part of NoteData?
      for (int t = 0; t < tracks; ++t) {
        FOREACH_NONEMPTY_ROW_IN_TRACK(note_data_edit_, t, r) {
          const TapNote& oldNote = note_data_edit_.GetTapNote(t, r);
          TapNote newNote =
              oldNote;  // need to lose the const. not feeling like casting.
          if (newNote.keysound_index_ == static_cast<int>(sound)) {
            newNote.keysound_index_ = -1;
            if (newNote.type_ == TapNoteType_AutoKeysound) {
              newNote.type_ = TapNoteType_Empty;
            }
          } else if (newNote.keysound_index_ > static_cast<int>(sound)) {
            newNote.keysound_index_--;
          }

          note_data_edit_.SetTapNote(t, r, newNote);
        }
      }
    }
    SetDirty(true);
  } else if (
      SM == SM_BackFromNewKeysound && !ScreenTextEntry::cancelled_last_) {
    std::string answer = ScreenTextEntry::last_answer_;
    const int track =
        ScreenMiniMenu::last_row_code_;  // still keeps the same value.
    const int row = this->GetRow();
    const TapNote& oldNote = note_data_edit_.GetTapNote(track, row);
    TapNote newNote =
        oldNote;  // need to lose the const. not feeling like casting.
    std::vector<std::string>& kses = song_->keysound_file_;
    unsigned pos = find(kses.begin(), kses.end(), answer) - kses.begin();
    if (pos == kses.size()) {
      newNote.keysound_index_ = kses.size();
      kses.push_back(answer);
    } else {
      newNote.keysound_index_ = pos;
    }
    if (newNote.type_ == TapNoteType_Empty) {
      newNote.type_ =
          TapNoteType_AutoKeysound;  // keysounds need something non empty.
    }
    note_data_edit_.SetTapNote(track, row, newNote);
    SetDirty(true);
  } else if (SM == SM_BackFromOptions) {
    // The options may have changed the note skin.
    note_field_record_.CacheAllUsedNoteSkins();
    player_manager_.CacheAllUsedNoteSkins();

    // stop any music that screen may have been playing
    SOUND->StopMusic();
  } else if (SM == SM_BackFromInsertTapAttack) {
    int iDurationChoice = ScreenMiniMenu::last_answers_[0];
    g_fLastInsertAttackDurationSeconds =
        StringToFloat(g_InsertTapAttack.rows_[0].choices_[iDurationChoice]);

    SCREENMAN->AddNewScreenToTop(
        SET_MOD_SCREEN, SM_BackFromInsertTapAttackPlayerOptions);
  } else if (SM == SM_BackFromInsertTapAttackPlayerOptions) {
    PlayerOptions poChosen =
        GAMESTATE->player_state_[main_player_]->player_options_.GetPreferred();
    std::string sMods = poChosen.GetString();
    const int row = BeatToNoteRow(GAMESTATE->position_.song_beat_);

    TapNote tn(
        TapNoteType_Attack, TapNoteSubType_Invalid, TapNoteSource_Original,
        sMods, g_fLastInsertAttackDurationSeconds, -1);
    tn.pn_ = input_player_number_;
    SetDirty(true);
    SaveUndo("Add Attack");
    note_data_edit_.SetTapNote(g_iLastInsertTapAttackTrack, row, tn);
    CheckNumberOfNotesAndUndo();
  } else if (
      SM == SM_BackFromEditingAttackStart &&
      !ScreenTextEntry::cancelled_last_) {
    float time = StringToFloat(ScreenTextEntry::last_answer_);
    Attack& attack = GetSelectedAttack();
    attack.start_second_ = time;
    SetDirty(true);
  } else if (
      SM == SM_BackFromEditingAttackLength &&
      !ScreenTextEntry::cancelled_last_) {
    float time = StringToFloat(ScreenTextEntry::last_answer_);
    Attack& attack = GetSelectedAttack();
    attack.secs_remaining_ = time;
    SetDirty(true);
  } else if (
      SM == SM_BackFromAddingModToExistingAttack &&
      !ScreenTextEntry::cancelled_last_) {
    std::string mod = ScreenTextEntry::last_answer_;
    Trim(mod);
    if (mod.length() > 0) {
      Attack& attack = GetSelectedAttack();
      attack.modifiers_ += "," + mod;
      SetDirty(true);
    }

  } else if (
      SM == SM_BackFromEditingModToExistingAttack &&
      !ScreenTextEntry::cancelled_last_) {
    AttackArray& attacks = GetSelectedAttackArray();
    Attack& attack = GetSelectedAttack();
    std::vector<std::string> mods;
    split(attack.modifiers_, ",", mods);
    std::string mod = ScreenTextEntry::last_answer_;
    Trim(mod);
    if (mod.length() > 0) {
      mods[mod_in_process_ - 2] = mod;
    } else {
      mods.erase(mods.begin() + (mod_in_process_ - 2));
    }
    if (mods.size() > 0) {
      attack.modifiers_ = join(",", mods);
    } else {
      attacks.erase(attacks.begin() + selected_attack_->index_);
      selected_attack_.reset();
    }
    SetDirty(true);
  } else if (SM == SM_BackFromInsertStepAttack) {
    unsigned option = ScreenMiniMenu::last_row_code_;
    Attack& attack = GetSelectedAttack();
    std::vector<std::string> mods;
    split(attack.modifiers_, ",", mods);
    mod_in_process_ = option;
    if (option == 0)  // adjusting the starting time
    {
      ScreenTextEntry::TextEntry(
          SM_BackFromEditingAttackStart, EDIT_ATTACK_START,
          std::to_string(attack.start_second_), 10);
    } else if (option == 1)  // adjusting the length of the attack
    {
      ScreenTextEntry::TextEntry(
          SM_BackFromEditingAttackLength, EDIT_ATTACK_LENGTH,
          std::to_string(attack.secs_remaining_), 10);
    } else if (option >= 2 + mods.size())  // adding a new mod
    {
      ScreenTextEntry::TextEntry(
          SM_BackFromAddingModToExistingAttack, ADD_NEW_MOD, "", 64);
    } else  // modifying existing mod.
    {
      ScreenTextEntry::TextEntry(
          SM_BackFromEditingModToExistingAttack, EDIT_EXISTING_MOD,
          mods[option - 2], 64);
    }
  } else if (
      SM == SM_BackFromAddingAttackToChart &&
      !ScreenTextEntry::cancelled_last_) {
    std::string mod = ScreenTextEntry::last_answer_;
    Trim(mod);
    if (mod.length() > 0) {
      AttackArray& attacks = GetAttackArray(
          GAMESTATE->is_using_step_timing_ ? AttackSource::Steps
                                           : AttackSource::Song);
      Attack attack;
      attack.start_second_ =
          GetAppropriateTiming().GetElapsedTimeFromBeat(GetBeat());
      attack.secs_remaining_ = 5;  // Users can change later.
      attack.modifiers_ = mod;
      attacks.push_back(attack);
      SetDirty(true);
    }
  } else if (SM == SM_BackFromAttackAtTime) {
    int attackChoice = ScreenMiniMenu::last_row_code_;
    int attackDecision = ScreenMiniMenu::last_answers_[attackChoice];
    const TimingData& timing = GetAppropriateTiming();
    float startTime = timing.GetElapsedTimeFromBeat(GetBeat());
    AttackArray attacks = GetAttacksForEditing();
    std::vector<int> points = FindAllAttacksAtTime(attacks, startTime);
    if (attackChoice == (int)points.size()) {
      // TODO: Add attack code.
      ScreenTextEntry::TextEntry(
          SM_BackFromAddingAttackToChart, ADD_NEW_ATTACK, "", 64);
    } else {
      selected_attack_ = ResolveAttackIndex(points[attackChoice]);
      AttackArray& sourceAttacks = GetAttackArray(selected_attack_->source_);
      if (attackDecision == 1)  // remove
      {
        sourceAttacks.erase(sourceAttacks.begin() + selected_attack_->index_);
        selected_attack_.reset();
        SetDirty(true);
      } else {
        Attack& attack = sourceAttacks[selected_attack_->index_];
        g_IndividualAttack.rows_.clear();

        g_IndividualAttack.rows_.push_back(MenuRowDef(
            0, "Starting Time", true, EditMode_CourseMods, true, true, 0,
            nullptr));
        g_IndividualAttack.rows_[0].SetOneUnthemedChoice(
            std::to_string(attack.start_second_));
        g_IndividualAttack.rows_.push_back(MenuRowDef(
            1, "Secs Remaining", true, EditMode_CourseMods, true, true, 0,
            nullptr));
        g_IndividualAttack.rows_[1].SetOneUnthemedChoice(
            std::to_string(attack.secs_remaining_));
        std::vector<std::string> mods;
        split(attack.modifiers_, ",", mods);
        for (unsigned i = 0; i < mods.size(); ++i) {
          unsigned col = i + 2;
          g_IndividualAttack.rows_.push_back(MenuRowDef(
              col, ssprintf("Attack %d", i + 1), true, EditMode_CourseMods,
              false, true, 0, nullptr));
          g_IndividualAttack.rows_[col].SetOneUnthemedChoice(mods[i].c_str());
        }

        g_IndividualAttack.rows_.push_back(MenuRowDef(
            mods.size() + 2, "Add Mod", true, EditMode_CourseMods, true, true,
            0, nullptr));

        EditMiniMenu(&g_IndividualAttack, SM_BackFromInsertStepAttack);
      }
    }
  } else if (SM == SM_BackFromInsertCourseAttack) {
    int iDurationChoice = ScreenMiniMenu::last_answers_[0];
    Course* pCourse = GAMESTATE->cur_course_;
    CourseEntry& ce = pCourse->entries_[GAMESTATE->edit_course_entry_index_];
    int iAttack;

    // TODO: Handle Song/Step Timing functions/switches here?

    g_fLastInsertAttackPositionSeconds =
        steps_->GetTimingData()->GetElapsedTimeFromBeat(
            GAMESTATE->position_.song_beat_);
    g_fLastInsertAttackDurationSeconds =
        StringToFloat(g_InsertCourseAttack.rows_[0].choices_[iDurationChoice]);
    iAttack = FindAttackAtTime(ce.attacks_, g_fLastInsertAttackPositionSeconds);

    if (ScreenMiniMenu::last_row_code_ == ScreenEdit::remove) {
      ASSERT(iAttack >= 0);
      ce.attacks_.erase(ce.attacks_.begin() + iAttack);
    } else {
      PlayerOptions po;

      if (iAttack >= 0) {
        po.FromString(ce.attacks_[iAttack].modifiers_);
      }

      GAMESTATE->player_state_[main_player_]->player_options_.Assign(
          ModsLevel_Preferred, po);
      SCREENMAN->AddNewScreenToTop(
          SET_MOD_SCREEN, SM_BackFromInsertCourseAttackPlayerOptions);
    }
  } else if (SM == SM_BackFromInsertStepAttackPlayerOptions) {
    ModsGroup<PlayerOptions>& toRestore =
        GAMESTATE->player_state_[main_player_]->player_options_;
    PlayerOptions poChosen = toRestore.GetPreferred();
    std::string mods = poChosen.GetString();

    if (g_fLastInsertAttackPositionSeconds >= 0) {
      Attack a(
          ATTACK_LEVEL_1, g_fLastInsertAttackPositionSeconds,
          g_fLastInsertAttackDurationSeconds, mods, false, false);
      if (selected_attack_.has_value()) {
        GetSelectedAttack() = a;
      } else {
        GetAttackArray(
            GAMESTATE->is_using_step_timing_ ? AttackSource::Steps
                                             : AttackSource::Song)
            .push_back(a);
      }
    }
    toRestore.Assign(
        ModsLevel_Preferred, this->original_player_options_.GetPreferred());
  } else if (SM == SM_BackFromInsertCourseAttackPlayerOptions) {
    PlayerOptions poChosen =
        GAMESTATE->player_state_[main_player_]->player_options_.GetPreferred();
    std::string sMods = poChosen.GetString();

    Course* pCourse = GAMESTATE->cur_course_;
    CourseEntry& ce = pCourse->entries_[GAMESTATE->edit_course_entry_index_];
    if (g_fLastInsertAttackPositionSeconds < 0) {
      ce.modifiers_ = sMods;
    } else {
      Attack a(
          ATTACK_LEVEL_1, g_fLastInsertAttackPositionSeconds,
          g_fLastInsertAttackDurationSeconds, sMods, false, false);
      int iAttack =
          FindAttackAtTime(ce.attacks_, g_fLastInsertAttackPositionSeconds);

      if (iAttack >= 0) {
        ce.attacks_[iAttack] = a;
      } else {
        ce.attacks_.push_back(a);
      }
    }
  } else if (SM == SM_DoRevertToLastSave) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      SaveUndo("Revert to Last Save");
      CopyFromLastSave();
      steps_->GetNoteData(note_data_edit_);
      SetDirty(false);
    }
  } else if (SM == SM_DoRevertFromDisk) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      SaveUndo("Revert From Disk");
      RevertFromDisk();
      steps_->GetNoteData(note_data_edit_);
      SetDirty(false);
    }
  } else if (SM == SM_ConfirmClearArea) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      note_data_edit_.ClearRange(
          note_field_edit_.begin_marker_, note_field_edit_.end_marker_);
    }
  } else if (SM == SM_DoEraseStepTiming) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      SaveUndo("Erase Step Timing");
      steps_->timing_.Clear();
      SetDirty(true);
    }
  } else if (SM == SM_DoSaveAndExit)  // just asked "save before exiting? yes,
                                      // no, cancel"
  {
    switch (ScreenPrompt::last_answer_) {
      DEFAULT_FAIL(ScreenPrompt::last_answer_);
      case ANSWER_YES:
        // This will send SM_SaveSuccessful or SM_SaveFailed.
        HandleMainMenuChoice(ScreenEdit::save_on_exit);
        return;
      case ANSWER_NO:
        // Don't save; just exit.
        song_->RemoveAutosave();
        SCREENMAN->SendMessageToTopScreen(SM_DoExit);
        return;
      case ANSWER_CANCEL:
        break;  // do nothing
    }
  } else if (SM == SM_SaveSuccessful || SM == SM_SaveSuccessNoSM) {
    LOG->Trace("Save successful.");
    CopyToLastSave();
    SetDirty(false);
    SONGMAN->Invalidate(GAMESTATE->cur_song_);

    const LocalizedString* message = &SAVE_SUCCESSFUL;
    if (SM == SM_SaveSuccessNoSM) {
      message = &SAVE_SUCCESS_NO_SM_SPLIT_TIMING;
    }

    if (current_action_ == save_on_exit) {
      ScreenPrompt::Prompt(SM_DoExit, *message);
    } else {
      SCREENMAN->SystemMessage(*message);
    }
  } else if (SM == SM_AutoSaveSuccessful) {
    LOG->Trace("AutoSave successful.");
    next_autosave_time_ =
        RageTimer::GetTimeSinceStart() + time_between_autosave;
    SCREENMAN->SystemMessage(AUTOSAVE_SUCCESSFUL);
  } else if (SM == SM_SaveFailed)  // save failed; stay in the editor
  {
    /* We committed the steps to SongManager. Revert to the last save, and
     * recommit the reversion to SongManager. */
    LOG->Trace("Save failed. Changes uncommitted from memory.");
    CopyFromLastSave();
    steps_->SetNoteData(note_data_edit_);
  } else if (SM == SM_DoExit) {
    // IMPORTANT: CopyFromLastSave before deleting the Steps below
    CopyFromLastSave();

    /* The user has been given a choice to save.
     * Delete all unsaved steps before exiting the editor. */
    /* FIXME: This code causes all the steps to be deleted if you quit
     * without saving. However, without this code, any new steps will get
     * saved on quit. -aj */

    // At this point, the last good song copy is in use.
    Song* pSong = GAMESTATE->cur_song_;
    const std::vector<Steps*>& apSteps = pSong->GetAllSteps();
    std::vector<Steps*> apToDelete;
    for (Steps* s : apSteps) {
      // If we're not on the same style, let it go.
      if (GAMESTATE->cur_steps_[main_player_]->steps_type_ != s->steps_type_) {
        continue;
      }
      // If autogenned, it isn't being saved.
      if (s->IsAutogen()) {
        continue;
      }
      // If the notedata has content, let it go.
      if (!s->GetNoteData().IsEmpty()) {
        continue;
      }
      // It's hard to say if these steps were saved to disk or not.
      /*
      if( !(s->GetSavedToDisk() )
              continue;
       */
      apToDelete.push_back(s);
    }
    for (Steps* pSteps : apToDelete) {
      pSong->DeleteSteps(pSteps);
      if (steps_ == pSteps) {
        steps_ = nullptr;
      }
      // In routine/couples editing every enabled player's m_pCurSteps are
      // pointed at the shared Steps.
      FOREACH_PlayerNumber(pn) {
        if (GAMESTATE->cur_steps_[pn].Get() == pSteps) {
          GAMESTATE->cur_steps_[pn].Set(nullptr);
        }
      }
    }

    out_.StartTransitioning(SM_GoToNextScreen);
  } else if (SM == SM_GainFocus) {
    /* When another screen comes up, RageSounds takes over the sound timer. When
     * we come back, put the timer back to where it was. */
    SetBeat(trailing_beat_);
  } else if (SM == SM_LoseFocus) {
    // Snap the trailing beat, in case we lose focus while tweening.
    trailing_beat_ =
        GetBeat();  // GAMESTATE->m_pPlayerState[main_player_]->m_Position.m_fSongBeat;
  }

  ScreenWithMenuElements::HandleScreenMessage(SM);
}

void ScreenEdit::SetDirty(bool dirty) {
  if (EDIT_MODE.GetValue() != EditMode_Full) {
    dirty_ = false;
    next_autosave_time_ = -1.0f;
    return;
  }
  if (dirty) {
    if (!dirty_) {
      next_autosave_time_ =
          RageTimer::GetTimeSinceStart() + time_between_autosave;
    }
  } else {
    next_autosave_time_ = -1.0f;
  }
  dirty_ = dirty;
}

void ScreenEdit::PerformSave(bool autosave) {
  // copy edit into current Steps
  steps_->SetNoteData(note_data_edit_);

  // don't forget the attacks.
  song_->attacks_ = GAMESTATE->cur_song_->attacks_;
  song_->attack_string_ = GAMESTATE->cur_song_->attacks_.ToVectorString();
  steps_->GetStepsAttacks() =
      GAMESTATE->cur_steps_[PLAYER_1]->GetStepsAttacks();
  steps_->GetStepsAttackStrings() =
      GAMESTATE->cur_steps_[PLAYER_1]->GetStepsAttacks().ToVectorString();

  // If one of the charts uses split timing, then it cannot be accurately
  // saved in the .sm format.  So saving the .sm is disabled.
  bool uses_split = song_->AnyChartUsesSplitTiming();
  const ScreenMessage save_message =
      autosave ? SM_AutoSaveSuccessful
               : (uses_split ? SM_SaveSuccessNoSM : SM_SaveSuccessful);

  switch (EDIT_MODE.GetValue()) {
    DEFAULT_FAIL(EDIT_MODE.GetValue());
    case EditMode_Home: {
      ASSERT(steps_->IsAnEdit());

      std::string sError;
      steps_->CalculateStepStats(song_->music_length_seconds_);
      if (!NotesWriterSM::WriteEditFileToMachine(song_, steps_, sError)) {
        ScreenPrompt::Prompt(SM_None, sError);
        break;
      }

      steps_->SetSavedToDisk(true);

      // HACK: clear undo, so "exit" below knows we don't need to save.
      // This only works because important non-steps data can't be changed in
      // home mode (BPMs, stops).
      ClearUndo();

      SCREENMAN->ZeroNextUpdate();

      HandleScreenMessage(save_message);

      /* FIXME
               std::string s;
               switch( c )
               {
               case save:			s =
         "ScreenMemcardSaveEditsAfterSave";	break; case save_on_exit:
         s = "ScreenMemcardSaveEditsAfterExit";	break; default:
         FAIL_M(ssprintf("Invalid menu choice: %i", c));
               }
               SCREENMAN->AddNewScreenToTop( s );
      */
    } break;
    case EditMode_Full: {
      // This will recalculate radar values.
      song_->Save(autosave);
      SCREENMAN->ZeroNextUpdate();

      HandleScreenMessage(save_message);
    } break;
    case EditMode_CourseMods:
    case EditMode_Practice:
      break;
  }
  sound_save_.Play(true);
}

void ScreenEdit::OnSnapModeChange() {
  sound_change_snap_.Play(true);

  NoteType nt = snap_display_.GetNoteType();
  int iStepIndex = BeatToNoteRow(GetBeat());
  int iElementsPerNoteType = BeatToNoteRow(NoteTypeToBeat(nt));
  int iStepIndexHangover = iStepIndex % iElementsPerNoteType;
  SetBeat(GetBeat() - NoteRowToBeat(iStepIndexHangover));
}

// Helper function for below

// Begin helper functions for InputEdit

static void ChangeDescription(const std::string& sNew) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];

  // Don't erase edit descriptions.
  if (sNew.empty() && pSteps->GetDifficulty() == Difficulty_Edit) {
    return;
  }

  pSteps->SetDescription(sNew);
}

static void ChangeChartName(const std::string& sNew) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
  pSteps->SetChartName(sNew);
}

static void ChangeChartStyle(const std::string& sNew) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
  pSteps->SetChartStyle(sNew);
}

static void ChangeStepCredit(const std::string& sNew) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
  pSteps->SetCredit(sNew);
}

static void ChangeStepMeter(const std::string& sNew) {
  int diff = StringToInt(sNew);
  GAMESTATE->cur_steps_[PLAYER_1]->SetMeter(std::max(diff, 1));
}

static void ChangeStepMusic(const std::string& sNew) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
  pSteps->SetMusicFile(sNew);
}

static void ChangeMainTitle(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->main_title_ = sNew;
}

static void ChangeSubTitle(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->sub_title_ = sNew;
}

static void ChangeArtist(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->artist_ = sNew;
}

static void ChangeGenre(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->genre_ = sNew;
}

static void ChangeCredit(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->credit_ = sNew;
}

static void ChangePreview(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  if (!sNew.empty()) {
    std::string error;
    RageSoundReader* sample = RageSoundReader_FileReader::OpenFile(
        pSong->GetPreviewMusicPath(), error);
    if (sample == nullptr) {
      LOG->UserLog(
          "Preview file", pSong->GetPreviewMusicPath(),
          "couldn't be opened: %s", error.c_str());
    } else {
      pSong->music_sample_length_seconds_ = sample->GetLength() / 1000.0f;
      pSong->preview_file_ = sNew;
      delete sample;
    }
  } else {
    pSong->preview_file_ = sNew;
  }
}

static void ChangeMainTitleTranslit(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->main_title_translit_ = sNew;
}

static void ChangeSubTitleTranslit(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->sub_title_translit_ = sNew;
}

static void ChangeArtistTranslit(const std::string& sNew) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->artist_translit_ = sNew;
}

static void ChangeLastSecondHint(const std::string& sNew) {
  Song& s = *GAMESTATE->cur_song_;
  s.SetSpecifiedLastSecond(StringToFloat(sNew));
}

static void ChangePreviewStart(const std::string& sNew) {
  GAMESTATE->cur_song_->music_sample_start_seconds_ = StringToFloat(sNew);
}

static void ChangePreviewLength(const std::string& sNew) {
  GAMESTATE->cur_song_->music_sample_length_seconds_ = StringToFloat(sNew);
}

static void ChangeMinBPM(const std::string& sNew) {
  GAMESTATE->cur_song_->specified_bpm_min_ = StringToFloat(sNew);
}

static void ChangeStepsMinBPM(const std::string& sNew) {
  Steps* step = GAMESTATE->cur_steps_[PLAYER_1];
  step->SetMinBPM(StringToFloat(sNew));
}

static void ChangeMaxBPM(const std::string& sNew) {
  GAMESTATE->cur_song_->specified_bpm_max_ = StringToFloat(sNew);
}

static void ChangeStepsMaxBPM(const std::string& sNew) {
  Steps* step = GAMESTATE->cur_steps_[PLAYER_1];
  step->SetMaxBPM(StringToFloat(sNew));
}

const TimingData& ScreenEdit::GetAppropriateTiming() const {
  if (GAMESTATE->is_using_step_timing_) {
    return *steps_->GetTimingData();
  }
  return song_->song_timing_;
}

TimingData& ScreenEdit::GetAppropriateTimingForUpdate() {
  if (GAMESTATE->is_using_step_timing_) {
    // Copy from song if there is no step timing
    if (steps_->timing_.empty()) {
      steps_->timing_ = song_->song_timing_;
    }
    return steps_->timing_;
  }
  return song_->song_timing_;
}

SongPosition& ScreenEdit::GetAppropriatePosition() const {
  if (GAMESTATE->is_using_step_timing_) {
    return GAMESTATE->player_state_[main_player_]->position_;
  }
  return GAMESTATE->position_;
}

inline void ScreenEdit::SetBeat(float fBeat) {
  if (!GAMESTATE->is_using_step_timing_) {
    GAMESTATE->position_.song_beat_ = fBeat;
    GAMESTATE->player_state_[main_player_]->position_.song_beat_ =
        steps_->GetTimingData()->GetBeatFromElapsedTime(
            song_->song_timing_.GetElapsedTimeFromBeat(fBeat));
  } else {
    GAMESTATE->player_state_[main_player_]->position_.song_beat_ = fBeat;
    GAMESTATE->position_.song_beat_ =
        song_->song_timing_.GetBeatFromElapsedTime(
            steps_->GetTimingData()->GetElapsedTimeFromBeat(fBeat));
  }
}

inline float ScreenEdit::GetBeat() {
  if (!GAMESTATE->is_using_step_timing_) {
    return GAMESTATE->position_.song_beat_;
  }
  return GAMESTATE->player_state_[main_player_]->position_.song_beat_;
}

inline int ScreenEdit::GetRow() { return BeatToNoteRow(GetBeat()); }

void ScreenEdit::DisplayTimingMenu() {
  int row = GetRow();
  const TimingData& pTime = GetAppropriateTiming();
  bool bHasSpeedOnThisRow = pTime.GetSpeedSegmentAtRow(row)->GetRow() == row;
  // bool bIsSelecting = ( (m_NoteFieldEdit.m_iEndMarker != -1) &&
  // (m_NoteFieldEdit.m_iBeginMarker != -1) );

  g_TimingDataInformation.rows_[beat_0_offset].SetOneUnthemedChoice(
      std::to_string(pTime.beat0_offset_in_seconds_));
  g_TimingDataInformation.rows_[bpm].SetOneUnthemedChoice(
      std::to_string(pTime.GetBPMAtRow(row)));
  g_TimingDataInformation.rows_[stop].SetOneUnthemedChoice(
      std::to_string(pTime.GetStopAtRow(row)));
  g_TimingDataInformation.rows_[delay].SetOneUnthemedChoice(
      std::to_string(pTime.GetDelayAtRow(row)));

  g_TimingDataInformation.rows_[label].SetOneUnthemedChoice(
      pTime.GetLabelAtRow(row).c_str());
  g_TimingDataInformation.rows_[tickcount].SetOneUnthemedChoice(
      ssprintf("%d", pTime.GetTickcountAtRow(row)));
  g_TimingDataInformation.rows_[combo].SetOneUnthemedChoice(ssprintf(
      "%d / %d", pTime.GetComboAtRow(row), pTime.GetMissComboAtRow(row)));
  g_TimingDataInformation.rows_[warp].SetOneUnthemedChoice(
      std::to_string(pTime.GetWarpAtRow(row)));
  g_TimingDataInformation.rows_[speed_percent].SetOneUnthemedChoice(
      bHasSpeedOnThisRow ? std::to_string(pTime.GetSpeedPercentAtRow(row))
                         : "---");
  g_TimingDataInformation.rows_[speed_wait].SetOneUnthemedChoice(
      bHasSpeedOnThisRow ? std::to_string(pTime.GetSpeedWaitAtRow(row))
                         : "---");

  std::string starting =
      (pTime.GetSpeedModeAtRow(row) == 1 ? "Seconds" : "Beats");
  g_TimingDataInformation.rows_[speed_mode].SetOneUnthemedChoice(
      starting.c_str());

  g_TimingDataInformation.rows_[scroll].SetOneUnthemedChoice(
      std::to_string(pTime.GetScrollAtRow(row)));
  g_TimingDataInformation.rows_[fake].SetOneUnthemedChoice(
      std::to_string(pTime.GetFakeAtRow(row)));

  // g_TimingDataInformation.rows[speed_percent].bEnabled = !bIsSelecting;
  g_TimingDataInformation.rows_[speed_wait].b_enabled_ = bHasSpeedOnThisRow;
  g_TimingDataInformation.rows_[speed_mode].b_enabled_ = bHasSpeedOnThisRow;

  EditMiniMenu(&g_TimingDataInformation, SM_BackFromTimingDataInformation);
}

void ScreenEdit::DisplayTimingChangeMenu() {
  int row = GetRow();
  EditMiniMenu(
      &g_TimingDataChangeInformation, SM_BackFromTimingDataChangeInformation);
}

static LocalizedString FIND_BPM("ScreenEdit", "Find BPM");
static LocalizedString APPLY_SYNC("ScreenEdit", "Apply Sync #%d");
static LocalizedString SYNC_OFFSET("ScreenEdit", "Offset");
static LocalizedString SYNC_BPM("ScreenEdit", "BPM");
static LocalizedString SYNC_NO_RESULTS("ScreenEdit", "Apply Sync");

void ScreenEdit::DisplayAdjustSyncMenu() {
  g_AdjustSyncMenu.rows_.clear();

  g_AdjustSyncMenu.rows_.push_back(MenuRowDef(
      adjust_sync_find_bpm, FIND_BPM, tempo_detector_ == nullptr, EditMode_Full,
      false, false, 0, nullptr));

  if (tempo_results_.empty()) {
    // Nothing has been detected yet, so there is nothing to apply.
    g_AdjustSyncMenu.rows_.push_back(MenuRowDef(
        adjust_sync_readonly, SYNC_NO_RESULTS, false, EditMode_Full, false,
        false, 0, nullptr));
    g_AdjustSyncMenu.rows_.push_back(MenuRowDef(
        adjust_sync_readonly, SYNC_OFFSET, false, EditMode_Full, false, false,
        0, "---"));
    g_AdjustSyncMenu.rows_.push_back(MenuRowDef(
        adjust_sync_readonly, SYNC_BPM, false, EditMode_Full, false, false, 0,
        "---"));
  } else {
    for (size_t i = 0; i < tempo_results_.size(); ++i) {
      const TempoResult& result = tempo_results_[i];

      MenuRowDef apply(
          adjust_sync_apply + (int)i,
          ssprintf(APPLY_SYNC.GetValue().c_str(), (int)i + 1), true,
          EditMode_Full, false, false, 0, nullptr);
      apply.SetOneUnthemedChoice(ssprintf("%.0f%%", result.fitness_ * 100));
      g_AdjustSyncMenu.rows_.push_back(apply);

      MenuRowDef offset(
          adjust_sync_readonly, SYNC_OFFSET, false, EditMode_Full, false, false,
          0, nullptr);
      offset.SetOneUnthemedChoice(ssprintf("%+.3f", result.offset_));
      g_AdjustSyncMenu.rows_.push_back(offset);

      MenuRowDef bpm(
          adjust_sync_readonly, SYNC_BPM, false, EditMode_Full, false, false, 0,
          nullptr);
      bpm.SetOneUnthemedChoice(ssprintf("%.3f", result.bpm_));
      g_AdjustSyncMenu.rows_.push_back(bpm);
    }
  }

  EditMiniMenu(&g_AdjustSyncMenu, SM_BackFromAdjustSyncMenu);
}

void ScreenEdit::StartTempoDetection() {
  if (tempo_detector_ != nullptr) {
    return;
  }

  std::string sError;
  tempo_detector_ = TempoDetector::Create(steps_->GetMusicPath(), sError);
  if (tempo_detector_ == nullptr) {
    SCREENMAN->SystemMessage(
        ssprintf(DETECT_BPM_FAILED.GetValue().c_str(), sError.c_str()));
    SCREENMAN->PlayInvalidSound();
    return;
  }

  tempo_results_.clear();
  tempo_progress_.clear();
  SCREENMAN->SystemMessage(DETECTING_BPM);
}

void ScreenEdit::HandleAdjustSyncMenuChoice(int iRowCode) {
  if (iRowCode == adjust_sync_find_bpm) {
    StartTempoDetection();
    return;
  }

  const int iResult = iRowCode - adjust_sync_apply;
  if (iResult < 0 || iResult >= (int)tempo_results_.size()) {
    return;
  }

  const TempoResult& result = tempo_results_[iResult];
  TimingData& timing = GetAppropriateTimingForUpdate();
  timing.beat0_offset_in_seconds_ = (float)result.offset_;
  timing.AddSegment(BPMSegment(0, (float)result.bpm_));

  SetDirty(true);
  SCREENMAN->SystemMessage(
      ssprintf(APPLIED_SYNC.GetValue().c_str(), result.bpm_, result.offset_));
}

// End helper functions

static LocalizedString REVERT_LAST_SAVE(
    "ScreenEdit", "Do you want to revert to your last save?");
static LocalizedString DESTROY_ALL_UNSAVED_CHANGES(
    "ScreenEdit", "This will destroy all unsaved changes.");
static LocalizedString REVERT_FROM_DISK(
    "ScreenEdit", "Do you want to revert from disk?");
static LocalizedString SAVE_CHANGES_BEFORE_EXITING(
    "ScreenEdit", "Do you want to save changes before exiting?");

int ScreenEdit::GetSongOrNotesEnd() {
  return std::max(
      start_playing_at_,
      std::max(
          note_data_edit_.GetLastRow(),
          BeatToNoteRow(steps_->GetTimingData()->GetBeatFromElapsedTime(
              GAMESTATE->cur_song_->music_length_seconds_))));
}

void ScreenEdit::HandleMainMenuChoice(
    MainMenuChoice c, const std::vector<int>& iAnswers) {
  GAMESTATE->SetProcessedTimingData(steps_->GetTimingData());
  switch (c) {
    DEFAULT_FAIL(c);
    case play_selection:
      if (note_field_edit_.begin_marker_ != -1 &&
          note_field_edit_.end_marker_ != -1) {
        HandleAlterMenuChoice(play);
      } else if (note_field_edit_.begin_marker_ != -1) {
        HandleMainMenuChoice(play_selection_start_to_end);
      } else {
        HandleMainMenuChoice(play_current_beat_to_end);
      }
      break;
    case play_whole_song: {
      start_playing_at_ = 0;
      stop_playing_at_ = GetSongOrNotesEnd();
      TransitionEditState(STATE_PLAYING);
    } break;
    case play_selection_start_to_end: {
      start_playing_at_ = note_field_edit_.begin_marker_;
      stop_playing_at_ =
          std::max(start_playing_at_, note_data_edit_.GetLastRow());
      TransitionEditState(STATE_PLAYING);
    } break;
    case play_current_beat_to_end: {
      start_playing_at_ = BeatToNoteRow(
          GAMESTATE->player_state_[main_player_]->position_.song_beat_);
      stop_playing_at_ = GetSongOrNotesEnd();
      TransitionEditState(STATE_PLAYING);
    } break;
    case set_selection_start: {
      const int iCurrentRow = BeatToNoteRow(
          GAMESTATE->player_state_[main_player_]->position_.song_beat_);
      if (note_field_edit_.end_marker_ != -1 &&
          iCurrentRow >= note_field_edit_.end_marker_) {
        SCREENMAN->PlayInvalidSound();
      } else {
        note_field_edit_.begin_marker_ = iCurrentRow;
        sound_marker_.Play(true);
      }
    } break;
    case set_selection_end: {
      const int iCurrentRow = BeatToNoteRow(
          GAMESTATE->player_state_[main_player_]->position_.song_beat_);
      if (note_field_edit_.begin_marker_ != -1 &&
          iCurrentRow <= note_field_edit_.begin_marker_) {
        SCREENMAN->PlayInvalidSound();
      } else {
        note_field_edit_.end_marker_ = iCurrentRow;
        sound_marker_.Play(true);
      }
    } break;
    case edit_steps_information: {
      /* XXX: If the difficulty is changed from EDIT, and
       * pSteps->WasLoadedFromProfile() is true, we should warn that the steps
       * will no longer be saved to the profile. */
      Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];

      g_StepsInformation.rows_[difficulty].choices_.clear();
      FOREACH_ENUM(Difficulty, dc) {
        g_StepsInformation.rows_[difficulty].choices_.push_back(
            "|" + CustomDifficultyToLocalizedString(GetCustomDifficulty(
                      pSteps->steps_type_, dc, CourseType_Invalid)));
      }
      g_StepsInformation.rows_[difficulty].default_choice_ =
          pSteps->GetDifficulty();
      g_StepsInformation.rows_[difficulty].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[meter].SetOneUnthemedChoice(
          ssprintf("%d", pSteps->GetMeter()));
      g_StepsInformation.rows_[meter].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Home);
      g_StepsInformation.rows_[predict_meter].SetOneUnthemedChoice(
          ssprintf("%.2f", pSteps->PredictMeter()));
      g_StepsInformation.rows_[chartname].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[chartname].SetOneUnthemedChoice(
          pSteps->GetChartName());
      g_StepsInformation.rows_[description].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[description].SetOneUnthemedChoice(
          pSteps->GetDescription());
      g_StepsInformation.rows_[chartstyle].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[chartstyle].SetOneUnthemedChoice(
          pSteps->GetChartStyle());
      g_StepsInformation.rows_[step_credit].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[step_credit].SetOneUnthemedChoice(
          pSteps->GetCredit());
      g_StepsInformation.rows_[step_display_bpm].default_choice_ =
          pSteps->GetDisplayBPM();
      g_StepsInformation.rows_[step_min_bpm].SetOneUnthemedChoice(
          std::to_string(pSteps->GetMinBPM()));
      g_StepsInformation.rows_[step_max_bpm].SetOneUnthemedChoice(
          std::to_string(pSteps->GetMaxBPM()));
      g_StepsInformation.rows_[step_music].b_enabled_ =
          (EDIT_MODE.GetValue() >= EditMode_Full);
      g_StepsInformation.rows_[step_music].SetOneUnthemedChoice(
          pSteps->GetMusicFile());
      EditMiniMenu(&g_StepsInformation, SM_BackFromStepsInformation, SM_None);
    } break;
    case view_steps_data: {
      float fMusicSeconds = sound_music_->GetLengthSeconds();
      Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
      const StepsTypeCategory& cat =
          GAMEMAN->GetStepsTypeInfo(pSteps->steps_type_).steps_type_category_;
      if (cat == StepsTypeCategory_Couple || cat == StepsTypeCategory_Routine) {
        std::pair<int, int> tmp = note_data_edit_.GetNumTapNotesTwoPlayer();
        g_StepsData.rows_[tap_notes].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumJumpsTwoPlayer();
        g_StepsData.rows_[jumps].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumHandsTwoPlayer();
        g_StepsData.rows_[hands].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumQuadsTwoPlayer();
        g_StepsData.rows_[quads].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumHoldNotesTwoPlayer();
        g_StepsData.rows_[holds].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumMinesTwoPlayer();
        g_StepsData.rows_[mines].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumRollsTwoPlayer();
        g_StepsData.rows_[rolls].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumLiftsTwoPlayer();
        g_StepsData.rows_[lifts].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
        tmp = note_data_edit_.GetNumFakesTwoPlayer();
        g_StepsData.rows_[fakes].SetOneUnthemedChoice(
            ssprintf("%d / %d", tmp.first, tmp.second));
      } else {
        g_StepsData.rows_[tap_notes].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumTapNotes()));
        g_StepsData.rows_[jumps].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumJumps()));
        g_StepsData.rows_[hands].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumHands()));
        g_StepsData.rows_[quads].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumQuads()));
        g_StepsData.rows_[holds].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumHoldNotes()));
        g_StepsData.rows_[mines].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumMines()));
        g_StepsData.rows_[rolls].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumRolls()));
        g_StepsData.rows_[lifts].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumLifts()));
        g_StepsData.rows_[fakes].SetOneUnthemedChoice(
            ssprintf("%d", note_data_edit_.GetNumFakes()));
      }
      RadarValues radar;
      radar.Zero();
      NoteDataUtil::CalculateRadarValues(note_data_edit_, fMusicSeconds, radar);
      g_StepsData.rows_[stream].SetOneUnthemedChoice(
          ssprintf("%.2f", radar[RadarCategory_Stream]));
      g_StepsData.rows_[voltage].SetOneUnthemedChoice(
          ssprintf("%.2f", radar[RadarCategory_Voltage]));
      g_StepsData.rows_[air].SetOneUnthemedChoice(
          ssprintf("%.2f", radar[RadarCategory_Air]));
      g_StepsData.rows_[freeze].SetOneUnthemedChoice(
          ssprintf("%.2f", radar[RadarCategory_Freeze]));
      g_StepsData.rows_[chaos].SetOneUnthemedChoice(
          ssprintf("%.2f", radar[RadarCategory_Chaos]));
      EditMiniMenu(&g_StepsData, SM_BackFromStepsData, SM_None);
      break;
    }
    case save:
    case save_on_exit:
      current_action_ = c;
      PerformSave(false);
      break;
    case revert_to_last_save:
      ScreenPrompt::Prompt(
          SM_DoRevertToLastSave,
          REVERT_LAST_SAVE.GetValue() + "\n\n" +
              DESTROY_ALL_UNSAVED_CHANGES.GetValue(),
          PROMPT_YES_NO, ANSWER_NO);
      break;
    case revert_from_disk:
      ScreenPrompt::Prompt(
          SM_DoRevertFromDisk,
          REVERT_FROM_DISK.GetValue() + "\n\n" +
              DESTROY_ALL_UNSAVED_CHANGES.GetValue(),
          PROMPT_YES_NO, ANSWER_NO);
      break;
    case options:
      SCREENMAN->AddNewScreenToTop(OPTIONS_SCREEN, SM_BackFromOptions);
      break;
    case edit_song_info: {
      const Song* pSong = GAMESTATE->cur_song_;
      g_SongInformation.rows_[main_title].SetOneUnthemedChoice(
          pSong->main_title_);
      g_SongInformation.rows_[sub_title].SetOneUnthemedChoice(
          pSong->sub_title_);
      g_SongInformation.rows_[artist].SetOneUnthemedChoice(pSong->artist_);
      g_SongInformation.rows_[genre].SetOneUnthemedChoice(pSong->genre_);
      g_SongInformation.rows_[credit].SetOneUnthemedChoice(pSong->credit_);
      g_SongInformation.rows_[preview].SetOneUnthemedChoice(
          pSong->preview_file_);
      g_SongInformation.rows_[main_title_transliteration].SetOneUnthemedChoice(
          pSong->main_title_translit_);
      g_SongInformation.rows_[sub_title_transliteration].SetOneUnthemedChoice(
          pSong->sub_title_translit_);
      g_SongInformation.rows_[artist_transliteration].SetOneUnthemedChoice(
          pSong->artist_translit_);
      g_SongInformation.rows_[last_second_hint].SetOneUnthemedChoice(
          std::to_string(pSong->GetSpecifiedLastSecond()));
      g_SongInformation.rows_[preview_start].SetOneUnthemedChoice(
          std::to_string(pSong->music_sample_start_seconds_));
      g_SongInformation.rows_[preview_length].SetOneUnthemedChoice(
          std::to_string(pSong->music_sample_length_seconds_));
      g_SongInformation.rows_[display_bpm].default_choice_ =
          pSong->display_bpm_type_;
      g_SongInformation.rows_[min_bpm].SetOneUnthemedChoice(
          std::to_string(pSong->specified_bpm_min_));
      g_SongInformation.rows_[max_bpm].SetOneUnthemedChoice(
          std::to_string(pSong->specified_bpm_max_));

      EditMiniMenu(&g_SongInformation, SM_BackFromSongInformation);
    } break;
    case edit_timing_data: {
      DisplayTimingMenu();
    } break;
    case adjust_sync: {
      DisplayAdjustSyncMenu();
    } break;

    case play_preview_music:
      PlayPreviewMusic();
      break;
    case exit:
      switch (EDIT_MODE.GetValue()) {
        DEFAULT_FAIL(EDIT_MODE.GetValue());
        case EditMode_Full:
        case EditMode_Home:
          if (IsDirty()) {
            ScreenPrompt::Prompt(
                SM_DoSaveAndExit, SAVE_CHANGES_BEFORE_EXITING,
                PROMPT_YES_NO_CANCEL, ANSWER_CANCEL);
          } else {
            SCREENMAN->SendMessageToTopScreen(SM_DoExit);
          }
          break;
        case EditMode_Practice:
        case EditMode_CourseMods:
          SCREENMAN->SendMessageToTopScreen(SM_DoExit);
          break;
      }
      break;
  };
  GAMESTATE->SetProcessedTimingData(nullptr);
}

static LocalizedString ENTER_ARBITRARY_MAPPING(
    "ScreenEdit", "Enter the new track mapping.");
static LocalizedString TOO_MANY_TRACKS(
    "ScreenEdit", "Too many tracks specified.");
static LocalizedString NOT_A_TRACK("ScreenEdit", "'%s' is not a track id.");
static LocalizedString OUT_OF_RANGE_ID(
    "ScreenEdit", "Entry %d, '%d', is out of range 1 to %d.");
static LocalizedString CONFIRM_CLEAR(
    "ScreenEdit", "Are you sure you want to clear %d notes?");

static bool ConvertMappingInputToMapping(
    const std::string& mapstr, int* mapping, std::string& error) {
  std::vector<std::string> mapping_input;
  split(mapstr, ",", mapping_input);
  size_t tracks_for_type =
      GAMEMAN->GetStepsTypeInfo(GAMESTATE->cur_steps_[0]->steps_type_)
          .num_tracks_;
  if (mapping_input.size() > tracks_for_type) {
    error = TOO_MANY_TRACKS;
    return false;
  }
  // mapping_input.size() < tracks_for_type is not checked because
  // unspecified tracks are mapped directly. -Kyz
  size_t track = 0;
  // track will be used for filling in the unspecified part of the mapping.
  for (; track < mapping_input.size(); ++track) {
    if (mapping_input[track].empty() || mapping_input[track] == " ") {
      // This allows blank entries to mean "pass through".
      mapping[track] = track + 1;
    } else if (!(mapping_input[track] >> mapping[track])) {
      error = ssprintf(
          NOT_A_TRACK.GetValue().c_str(), mapping_input[track].c_str());
      return false;
    }
    if (mapping[track] < 1 ||
        mapping[track] > static_cast<int>(tracks_for_type)) {
      error = ssprintf(
          OUT_OF_RANGE_ID.GetValue().c_str(), track + 1, mapping[track],
          tracks_for_type);
      return false;
    }
    // Simpler for the user if they input track ids starting at 1.
    --mapping[track];
  }
  for (; track < tracks_for_type; ++track) {
    mapping[track] = track;
  }
  return true;
}

static bool ArbitraryRemapValidate(
    const std::string& answer, std::string& error_out) {
  int mapping[MAX_NOTE_TRACKS];
  return ConvertMappingInputToMapping(answer, mapping, error_out);
}

void ScreenEdit::HandleArbitraryRemapping(const std::string& mapstr) {
  const NoteData OldClipboard(clipboard_);
  HandleAlterMenuChoice(cut, false);
  int mapping[MAX_NOTE_TRACKS];
  std::string error;
  // error is actually reported by the validate function, and unused here.
  if (ConvertMappingInputToMapping(mapstr, mapping, error)) {
    NoteDataUtil::ArbitraryRemap(clipboard_, mapping);
  }
  HandleAreaMenuChoice(paste_at_begin_marker, false);
  clipboard_ = OldClipboard;
}

void ScreenEdit::HandleAlterMenuChoice(
    AlterMenuChoice c, const std::vector<int>& answers, bool allow_undo,
    bool prompt_clear) {
  ASSERT_M(
      note_field_edit_.begin_marker_ != -1 &&
          note_field_edit_.end_marker_ != -1,
      "You can only alter a selection of notes with a selection to begin "
      "with!");

  bool bSaveUndo = true;
  switch (c) {
    case play:
    case record:
    case cut:
    case copy: {
      bSaveUndo = false;
    }
    default:
      break;
  }

  if (bSaveUndo) {
    SetDirty(true);
  }

  /* We call HandleAreaMenuChoice recursively. Only the outermost
   * HandleAreaMenuChoice should allow Undo so that the inner calls don't
   * also save Undo and mess up the outermost */
  if (!allow_undo) {
    bSaveUndo = false;
  }

  if (bSaveUndo) {
    SaveUndo(g_AlterMenu.rows_[c].name_);
  }

  switch (c) {
    case cut: {
      HandleAlterMenuChoice(copy, false, false);
      HandleAlterMenuChoice(clear, false, false);
    } break;
    case copy: {
      clipboard_.ClearAll();
      clipboard_.CopyRange(
          note_data_edit_, note_field_edit_.begin_marker_,
          note_field_edit_.end_marker_);
    } break;
    case clear: {
      int note_count = note_data_edit_.GetNumTapNotesNoTiming(
          note_field_edit_.begin_marker_, note_field_edit_.end_marker_);
      if (note_count >= PREFSMAN->edit_clear_prompt_threshold_ &&
          prompt_clear) {
        ScreenPrompt::Prompt(
            SM_ConfirmClearArea,
            ssprintf(CONFIRM_CLEAR.GetValue().c_str(), note_count),
            PROMPT_YES_NO);
      } else {
        note_data_edit_.ClearRange(
            note_field_edit_.begin_marker_, note_field_edit_.end_marker_);
      }
    } break;
    case quantize: {
      NoteType nt = (NoteType)answers[c];
      NoteDataUtil::SnapToNearestNoteType(
          note_data_edit_, nt, nt, note_field_edit_.begin_marker_,
          note_field_edit_.end_marker_);
      break;
    }
    case turn: {
      const NoteData OldClipboard(clipboard_);
      HandleAlterMenuChoice(cut, false);

      StepsType st =
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_;
      TurnType tt = (TurnType)answers[c];
      switch (tt) {
        DEFAULT_FAIL(tt);
        case left:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::left);
          break;
        case right:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::right);
          break;
        case mirror:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::mirror);
          break;
        case lrmirror:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::lrmirror);
          break;
        case udmirror:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::udmirror);
          break;
        case turn_backwards:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::backwards);
          break;
        case shuffle:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::shuffle);
          break;
        case super_shuffle:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::super_shuffle);
          break;
        case hyper_shuffle:
          NoteDataUtil::Turn(clipboard_, st, NoteDataUtil::hyper_shuffle);
          break;
      }

      HandleAreaMenuChoice(paste_at_begin_marker, false);
      clipboard_ = OldClipboard;
    } break;
    case transform: {
      int iBeginRow = note_field_edit_.begin_marker_;
      int iEndRow = note_field_edit_.end_marker_;
      TransformType tt = (TransformType)answers[c];
      StepsType st =
          GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->steps_type_;

      switch (tt) {
        DEFAULT_FAIL(tt);
        case noholds:
          NoteDataUtil::RemoveHoldNotes(note_data_edit_, iBeginRow, iEndRow);
          break;
        case nomines:
          NoteDataUtil::RemoveMines(note_data_edit_, iBeginRow, iEndRow);
          break;
        case little:
          NoteDataUtil::Little(note_data_edit_, iBeginRow, iEndRow);
          break;
        case wide:
          NoteDataUtil::Wide(note_data_edit_, iBeginRow, iEndRow);
          break;
        case big:
          NoteDataUtil::Big(note_data_edit_, iBeginRow, iEndRow);
          break;
        case quick:
          NoteDataUtil::Quick(note_data_edit_, iBeginRow, iEndRow);
          break;
        case skippy:
          NoteDataUtil::Skippy(note_data_edit_, iBeginRow, iEndRow);
          break;
        case add_mines:
          NoteDataUtil::AddMines(note_data_edit_, iBeginRow, iEndRow);
          break;
        case echo:
          NoteDataUtil::Echo(note_data_edit_, iBeginRow, iEndRow);
          break;
        case stomp:
          NoteDataUtil::Stomp(note_data_edit_, st, iBeginRow, iEndRow);
          break;
        case planted:
          NoteDataUtil::Planted(note_data_edit_, iBeginRow, iEndRow);
          break;
        case floored:
          NoteDataUtil::Floored(note_data_edit_, iBeginRow, iEndRow);
          break;
        case twister:
          NoteDataUtil::Twister(note_data_edit_, iBeginRow, iEndRow);
          break;
        case nojumps:
          NoteDataUtil::RemoveJumps(note_data_edit_, iBeginRow, iEndRow);
          break;
        case nohands:
          NoteDataUtil::RemoveHands(note_data_edit_, iBeginRow, iEndRow);
          break;
        case noquads:
          NoteDataUtil::RemoveQuads(note_data_edit_, iBeginRow, iEndRow);
          break;
        case nostretch:
          NoteDataUtil::RemoveStretch(
              note_data_edit_, st, iBeginRow, iBeginRow);
          break;
      }

      // bake in the additions
      NoteDataUtil::ConvertAdditionsToRegular(note_data_edit_);
      break;
    }
    case alter: {
      const NoteData OldClipboard(clipboard_);
      HandleAlterMenuChoice(cut, false);

      AlterType at = (AlterType)answers[c];
      switch (at) {
        DEFAULT_FAIL(at);
        case autogen_to_fill_width: {
          NoteData temp(clipboard_);
          int iMaxNonEmptyTrack = NoteDataUtil::GetMaxNonEmptyTrack(temp);
          if (iMaxNonEmptyTrack == -1) {
            break;
          }
          temp.SetNumTracks(iMaxNonEmptyTrack + 1);
          NoteDataUtil::LoadTransformedSlidingWindow(
              temp, clipboard_, clipboard_.GetNumTracks());
          NoteDataUtil::RemoveStretch(
              clipboard_, GAMESTATE->cur_steps_[0]->steps_type_);
        } break;
        case backwards:
          NoteDataUtil::Backwards(clipboard_);
          break;
        case swap_sides:
          NoteDataUtil::SwapSides(clipboard_);
          break;
        case copy_left_to_right:
          NoteDataUtil::CopyLeftToRight(clipboard_);
          break;
        case copy_right_to_left:
          NoteDataUtil::CopyRightToLeft(clipboard_);
          break;
        case clear_left:
          NoteDataUtil::ClearLeft(clipboard_);
          break;
        case clear_right:
          NoteDataUtil::ClearRight(clipboard_);
          break;
        case collapse_to_one:
          NoteDataUtil::CollapseToOne(clipboard_);
          break;
        case collapse_left:
          NoteDataUtil::CollapseLeft(clipboard_);
          break;
        case shift_left:
          NoteDataUtil::ShiftLeft(clipboard_);
          break;
        case shift_right:
          NoteDataUtil::ShiftRight(clipboard_);
          break;
        case swap_up_down:
          NoteDataUtil::SwapUpDown(
              clipboard_, GAMESTATE->cur_steps_[0]->steps_type_);
          break;
        case arbitrary_remap:
          ScreenTextEntry::TextEntry(
              SM_BackFromArbitraryRemap, ENTER_ARBITRARY_MAPPING, "1, 2, 3, 4",
              MAX_NOTE_TRACKS * 4,
              // 2 chars for digit, one for comma, one for space.
              ArbitraryRemapValidate);
          break;
      }

      HandleAreaMenuChoice(paste_at_begin_marker, false);
      clipboard_ = OldClipboard;
      break;
    }
    case tempo: {
      // This affects all steps.
      const TempoType tt = static_cast<TempoType>(answers[c]);
      float fScale = -1;

      switch (tt) {
        DEFAULT_FAIL(tt);
        case compress_2x:
          fScale = 0.5f;
          break;
        case compress_3_2:
          fScale = 2.0f / 3;
          break;
        case compress_4_3:
          fScale = 0.75f;
          break;
        case expand_4_3:
          fScale = 4.0f / 3;
          break;
        case expand_3_2:
          fScale = 1.5f;
          break;
        case expand_2x:
          fScale = 2;
          break;
      }

      int iStartIndex = note_field_edit_.begin_marker_;
      int iEndIndex = note_field_edit_.end_marker_;
      int iNewEndIndex =
          iEndIndex + std::lrint((iEndIndex - iStartIndex) * (fScale - 1));

      // scale currently editing notes
      NoteDataUtil::ScaleRegion(
          note_data_edit_, fScale, iStartIndex, iEndIndex);

      // scale timing data
      GetAppropriateTimingForUpdate().ScaleRegion(
          fScale, note_field_edit_.begin_marker_, note_field_edit_.end_marker_,
          true);

      note_field_edit_.end_marker_ = iNewEndIndex;
      break;
    }

    case play:
      start_playing_at_ = note_field_edit_.begin_marker_;
      stop_playing_at_ = note_field_edit_.end_marker_;
      TransitionEditState(STATE_PLAYING);
      break;
    case record:
      start_playing_at_ = note_field_edit_.begin_marker_;
      stop_playing_at_ = note_field_edit_.end_marker_;
      TransitionEditState(STATE_RECORDING);
      break;
    case preview_designation: {
      float fMarkerStart = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.begin_marker_));
      float fMarkerEnd = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.end_marker_));
      GAMESTATE->cur_song_->music_sample_start_seconds_ = fMarkerStart;
      GAMESTATE->cur_song_->music_sample_length_seconds_ =
          fMarkerEnd - fMarkerStart;
      break;
    }
    case convert_to_pause: {
      float fMarkerStart = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.begin_marker_));
      float fMarkerEnd = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.end_marker_));

      // The length of the stop segment we're going to create.  This includes
      // time spent in any stops in the selection, which will be deleted and
      // subsumed into the new stop.
      float fStopLength = fMarkerEnd - fMarkerStart;

      // be sure not to clobber the row at the start - a row at the end
      // can be dropped safely, though
      NoteDataUtil::DeleteRows(
          note_data_edit_, note_field_edit_.begin_marker_ + 1,
          note_field_edit_.end_marker_ - note_field_edit_.begin_marker_);
      // For TimingData, it makes more sense not to offset by a row
      GetAppropriateTimingForUpdate().DeleteRows(
          note_field_edit_.begin_marker_,
          note_field_edit_.end_marker_ - note_field_edit_.begin_marker_);
      GetAppropriateTimingForUpdate().SetStopAtRow(
          note_field_edit_.begin_marker_, fStopLength);
      note_field_edit_.begin_marker_ = -1;
      note_field_edit_.end_marker_ = -1;
      break;
    }
    case convert_to_delay: {
      float fMarkerStart = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.begin_marker_));
      float fMarkerEnd = GetAppropriateTiming().GetElapsedTimeFromBeat(
          NoteRowToBeat(note_field_edit_.end_marker_));

      // The length of the delay segment we're going to create.  This includes
      // time spent in any stops in the selection, which will be deleted and
      // subsumed into the new stop.
      float fStopLength = fMarkerEnd - fMarkerStart;

      NoteDataUtil::DeleteRows(
          note_data_edit_, note_field_edit_.begin_marker_,
          note_field_edit_.end_marker_ - note_field_edit_.begin_marker_);
      GetAppropriateTimingForUpdate().DeleteRows(
          note_field_edit_.begin_marker_,
          note_field_edit_.end_marker_ - note_field_edit_.begin_marker_);
      GetAppropriateTimingForUpdate().SetDelayAtRow(
          note_field_edit_.begin_marker_, fStopLength);
      note_field_edit_.begin_marker_ = -1;
      note_field_edit_.end_marker_ = -1;
      break;
    }
    case convert_to_warp: {
      float startBeat = NoteRowToBeat(note_field_edit_.begin_marker_);
      float lengthBeat =
          NoteRowToBeat(note_field_edit_.end_marker_) - startBeat;
      GetAppropriateTimingForUpdate().SetWarpAtBeat(startBeat, lengthBeat);
      SetDirty(true);
      break;
    }
    case convert_to_attack: {
      float startBeat = NoteRowToBeat(note_field_edit_.begin_marker_);
      float endBeat = NoteRowToBeat(note_field_edit_.end_marker_);
      const TimingData& timing = GetAppropriateTiming();
      float& start = g_fLastInsertAttackPositionSeconds;
      float& length = g_fLastInsertAttackDurationSeconds;
      start = timing.GetElapsedTimeFromBeat(startBeat);
      length = timing.GetElapsedTimeFromBeat(endBeat) - start;

      AttackArray attacks = GetAttacksForEditing();
      int iAttack = FindAttackAtTime(attacks, start);
      selected_attack_.reset();

      ModsGroup<PlayerOptions>& toEdit =
          GAMESTATE->player_state_[main_player_]->player_options_;
      this->original_player_options_.Assign(
          ModsLevel_Preferred, toEdit.GetPreferred());
      PlayerOptions po;
      if (iAttack >= 0) {
        po.FromString(attacks[iAttack].modifiers_);
        selected_attack_ = ResolveAttackIndex(iAttack);
      }

      toEdit.Assign(ModsLevel_Preferred, po);
      SCREENMAN->AddNewScreenToTop(
          SET_MOD_SCREEN, SM_BackFromInsertStepAttackPlayerOptions);
      SetDirty(true);
      break;
    }
    case convert_to_fake: {
      int startRow = note_field_edit_.begin_marker_;
      float lengthBeat =
          NoteRowToBeat(note_field_edit_.end_marker_) - NoteRowToBeat(startRow);
      GetAppropriateTimingForUpdate().AddSegment(
          FakeSegment(startRow, lengthBeat));
      SetDirty(true);
      break;
    }
    case routine_invert_notes: {
      NoteData& nd = this->note_data_edit_;
      NoteField& nf = this->note_field_edit_;
      FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
          nd, r, nf.begin_marker_, nf.end_marker_) {
        for (int t = 0; t < nd.GetNumTracks(); t++) {
          const TapNote& tn = nd.GetTapNote(t, r);
          if (tn.type_ != TapNoteType_Empty) {
            TapNote nTap = tn;
            nTap.pn_ = (tn.pn_ == PLAYER_1 ? PLAYER_2 : PLAYER_1);
            note_data_edit_.SetTapNote(t, r, nTap);
          }
        }
      }
      break;
    }
    case routine_mirror_1_to_2:
    case routine_mirror_2_to_1: {
      PlayerNumber oPN = (c == routine_mirror_1_to_2 ? PLAYER_1 : PLAYER_2);
      PlayerNumber nPN = (c == routine_mirror_1_to_2 ? PLAYER_2 : PLAYER_1);
      int nTrack = -1;
      NoteData& nd = this->note_data_edit_;
      NoteField& nf = this->note_field_edit_;
      int tracks = nd.GetNumTracks();
      FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
          nd, r, nf.begin_marker_, nf.end_marker_) {
        for (int t = 0; t < tracks; t++) {
          const TapNote& tn = nd.GetTapNote(t, r);
          if (tn.type_ != TapNoteType_Empty && tn.pn_ == oPN) {
            TapNote nTap = tn;
            nTap.pn_ = nPN;
            StepsType curType = GAMESTATE->cur_steps_[PLAYER_1]->steps_type_;
            // TODO: Find a better way to do this.
            if (curType == StepsType_dance_routine) {
              nTrack = tracks - t - 1;
            } else if (curType == StepsType_pump_routine) {
              switch (t) {
                case 0:
                  nTrack = 8;
                  break;
                case 1:
                  nTrack = 9;
                  break;
                case 2:
                  nTrack = 7;
                  break;
                case 3:
                  nTrack = 5;
                  break;
                case 4:
                  nTrack = 6;
                  break;
                case 5:
                  nTrack = 3;
                  break;
                case 6:
                  nTrack = 4;
                  break;
                case 7:
                  nTrack = 2;
                  break;
                case 8:
                  nTrack = 0;
                  break;
                case 9:
                  nTrack = 1;
                  break;
                default:
                  FAIL_M(ssprintf("Invalid column %d for pump-routine", t));
                  break;
              }
            }
            note_data_edit_.SetTapNote(nTrack, r, nTap);
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

void ScreenEdit::HandleAreaMenuChoice(
    AreaMenuChoice c, const std::vector<int>& iAnswers, bool bAllowUndo) {
  bool bSaveUndo = true;
  switch (c) {
    case clear_clipboard:
    case undo:
      bSaveUndo = false;
      break;
    default:
      break;
  }

  if (bSaveUndo) {
    SetDirty(true);
  }

  /* We call HandleAreaMenuChoice recursively. Only the outermost
   * HandleAreaMenuChoice should allow Undo so that the inner calls don't
   * also save Undo and mess up the outermost */
  if (!bAllowUndo) {
    bSaveUndo = false;
  }

  if (bSaveUndo) {
    SaveUndo(g_AreaMenu.rows_[c].name_);
  }

  switch (c) {
    DEFAULT_FAIL(c);

    case paste_at_current_beat:
    case paste_at_begin_marker: {
      int iDestFirstRow = -1;
      switch (c) {
        DEFAULT_FAIL(c);
        case paste_at_current_beat:
          iDestFirstRow = BeatToNoteRow(GetAppropriatePosition().song_beat_);
          break;
        case paste_at_begin_marker:
          ASSERT(note_field_edit_.begin_marker_ != -1);
          iDestFirstRow = note_field_edit_.begin_marker_;
          break;
      }

      int iRowsToCopy = clipboard_.GetLastRow() + 1;
      note_data_edit_.CopyRange(clipboard_, 0, iRowsToCopy, iDestFirstRow);
    } break;

    case insert_and_shift:
      NoteDataUtil::InsertRows(
          note_data_edit_, GetRow(), GetRowsFromAnswers(c, iAnswers));
      break;
    case delete_and_shift:
      NoteDataUtil::DeleteRows(
          note_data_edit_, GetRow(), GetRowsFromAnswers(c, iAnswers));
      break;
    case shift_pauses_forward:
      GetAppropriateTimingForUpdate().InsertRows(
          GetRow(), GetRowsFromAnswers(c, iAnswers));
      break;
    case shift_pauses_backward:
      GetAppropriateTimingForUpdate().DeleteRows(
          GetRow(), GetRowsFromAnswers(c, iAnswers));
      break;

    case convert_pause_to_beat: {
      float fStopSeconds = GetAppropriateTiming().GetStopAtRow(GetRow());
      GetAppropriateTimingForUpdate().SetStopAtBeat(GetBeat(), 0);

      float fStopBeats =
          fStopSeconds * GetAppropriateTiming().GetBPMAtBeat(GetBeat()) / 60;

      // don't move the step from where it is, just move everything later
      NoteDataUtil::InsertRows(
          note_data_edit_, GetRow() + 1, BeatToNoteRow(fStopBeats));
      GetAppropriateTimingForUpdate().InsertRows(
          GetRow() + 1, BeatToNoteRow(fStopBeats));
    } break;
    case convert_delay_to_beat: {
      TimingData& timing = GetAppropriateTimingForUpdate();
      float pause = timing.GetDelayAtRow(GetRow());
      timing.SetDelayAtRow(GetRow(), 0);

      float pauseBeats = pause * timing.GetBPMAtBeat(GetBeat()) / 60;

      NoteDataUtil::InsertRows(
          note_data_edit_, GetRow(), BeatToNoteRow(pauseBeats));
      timing.InsertRows(GetRow(), BeatToNoteRow(pauseBeats));
      break;
    }
    case last_second_at_beat: {
      const TimingData& timing = GetAppropriateTiming();
      Song& s = *GAMESTATE->cur_song_;
      s.SetSpecifiedLastSecond(
          timing.GetElapsedTimeFromBeatNoOffset(GetBeat()));
      break;
    }
    case undo:
      Undo();
      break;
    case clear_clipboard: {
      clipboard_.ClearAll();
      break;
    }
    case modify_attacks_at_row: {
      this->DoStepAttackMenu();
      break;
    }
    case modify_keysounds_at_row: {
      this->DoKeyboardTrackMenu();
      break;
    }
  };

  if (bSaveUndo) {
    CheckNumberOfNotesAndUndo();
  }
}

void ScreenEdit::HandleStepsDataChoice(
    StepsDataChoice c, const std::vector<int>& iAnswers) {
  return;  // nothing is done with the choices. Yet.
}

static LocalizedString ENTER_NEW_DESCRIPTION(
    "ScreenEdit", "Enter a new description.");
static LocalizedString ENTER_NEW_CHART_NAME(
    "ScreenEdit", "Enter a new chart name.");
static LocalizedString ENTER_NEW_CHART_STYLE(
    "ScreenEdit", "Enter a new chart style.");
static LocalizedString ENTER_NEW_STEP_AUTHOR(
    "ScreenEdit", "Enter the author who made this step pattern.");
static LocalizedString ENTER_NEW_METER("ScreenEdit", "Enter a new meter.");
static LocalizedString ENTER_MIN_BPM("ScreenEdit", "Enter a new min BPM.");
static LocalizedString ENTER_MAX_BPM("ScreenEdit", "Enter a new max BPM.");
static LocalizedString ENTER_NEW_STEP_MUSIC(
    "ScreenEdit", "Enter the music file for this chart.");
void ScreenEdit::HandleStepsInformationChoice(
    StepsInformationChoice c, const std::vector<int>& iAnswers) {
  Steps* pSteps = GAMESTATE->cur_steps_[PLAYER_1];
  Difficulty dc = (Difficulty)iAnswers[difficulty];
  pSteps->SetDifficulty(dc);
  pSteps->SetDisplayBPM(static_cast<DisplayBPM>(iAnswers[step_display_bpm]));

  switch (c) {
    case chartname: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_NEW_CHART_NAME, steps_->GetChartName(),
          MAX_STEPS_DESCRIPTION_LENGTH, SongUtil::ValidateCurrentStepsChartName,
          ChangeChartName, nullptr);
      break;
    }
    case description: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_NEW_DESCRIPTION, steps_->GetDescription(),
          MAX_STEPS_DESCRIPTION_LENGTH,
          SongUtil::ValidateCurrentStepsDescription, ChangeDescription,
          nullptr);
      break;
    }
    case chartstyle: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_NEW_CHART_STYLE, steps_->GetChartStyle(), 255, nullptr,
          ChangeChartStyle, nullptr);
      break;
    }
    case step_credit: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_NEW_STEP_AUTHOR, steps_->GetCredit(), 255,
          SongUtil::ValidateCurrentStepsCredit, ChangeStepCredit, nullptr);
      break;
    }
    case meter: {
      ScreenTextEntry::TextEntry(
          SM_BackFromDifficultyMeterChange, ENTER_NEW_METER,
          ssprintf("%d", steps_->GetMeter()), 4, ScreenTextEntry::IntValidate,
          ChangeStepMeter, nullptr);
      break;
    }
    case step_min_bpm: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MIN_BPM, std::to_string(pSteps->GetMinBPM()), 20,
          ScreenTextEntry::FloatValidate, ChangeStepsMinBPM, nullptr);
      break;
    }
    case step_max_bpm: {
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MAX_BPM, std::to_string(pSteps->GetMaxBPM()), 20,
          ScreenTextEntry::FloatValidate, ChangeStepsMaxBPM, nullptr);
      break;
    }
    case step_music: {
      ScreenTextEntry::TextEntry(
          SM_BackFromStepMusicChange, ENTER_NEW_STEP_MUSIC,
          steps_->GetMusicFile(), 255, SongUtil::ValidateCurrentStepsMusic,
          ChangeStepMusic, nullptr);
      break;
    }
    default:
      break;
  }
  SetDirty(true);
}

static LocalizedString ENTER_MAIN_TITLE(
    "ScreenEdit", "Enter a new main title.");
static LocalizedString ENTER_SUB_TITLE("ScreenEdit", "Enter a new sub title.");
static LocalizedString ENTER_ARTIST("ScreenEdit", "Enter a new artist.");
static LocalizedString ENTER_GENRE("ScreenEdit", "Enter a new genre.");
static LocalizedString ENTER_CREDIT("ScreenEdit", "Enter a new credit.");
static LocalizedString ENTER_PREVIEW("ScreenEdit", "Enter a preview file.");
static LocalizedString ENTER_MAIN_TITLE_TRANSLIT(
    "ScreenEdit", "Enter a new main title transliteration.");
static LocalizedString ENTER_SUB_TITLE_TRANSLIT(
    "ScreenEdit", "Enter a new sub title transliteration.");
static LocalizedString ENTER_ARTIST_TRANSLIT(
    "ScreenEdit", "Enter a new artist transliteration.");
static LocalizedString ENTER_LAST_SECOND_HINT(
    "ScreenEdit", "Enter a new last second hint.");
static LocalizedString ENTER_PREVIEW_START(
    "ScreenEdit", "Enter a new preview start.");
static LocalizedString ENTER_PREVIEW_LENGTH(
    "ScreenEdit", "Enter a new preview length.");
void ScreenEdit::HandleSongInformationChoice(
    SongInformationChoice c, const std::vector<int>& iAnswers) {
  Song* pSong = GAMESTATE->cur_song_;
  pSong->display_bpm_type_ = static_cast<DisplayBPM>(iAnswers[display_bpm]);

  switch (c) {
    case main_title:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MAIN_TITLE, pSong->main_title_, 100, nullptr,
          ChangeMainTitle, nullptr);
      break;
    case sub_title:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_SUB_TITLE, pSong->sub_title_, 100, nullptr,
          ChangeSubTitle, nullptr);
      break;
    case artist:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_ARTIST, pSong->artist_, 100, nullptr, ChangeArtist,
          nullptr);
      break;
    case genre:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_GENRE, pSong->genre_, 100, nullptr, ChangeGenre,
          nullptr);
      break;
    case credit:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_CREDIT, pSong->credit_, 100, nullptr, ChangeCredit,
          nullptr);
      break;
    case preview:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_PREVIEW, pSong->preview_file_, 100,
          SongUtil::ValidateCurrentSongPreview, ChangePreview, nullptr);
      break;
    case main_title_transliteration:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MAIN_TITLE_TRANSLIT, pSong->main_title_translit_, 100,
          nullptr, ChangeMainTitleTranslit, nullptr);
      break;
    case sub_title_transliteration:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_SUB_TITLE_TRANSLIT, pSong->sub_title_translit_, 100,
          nullptr, ChangeSubTitleTranslit, nullptr);
      break;
    case artist_transliteration:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_ARTIST_TRANSLIT, pSong->artist_translit_, 100, nullptr,
          ChangeArtistTranslit, nullptr);
      break;
    case last_second_hint:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_LAST_SECOND_HINT,
          std::to_string(pSong->GetSpecifiedLastSecond()), 20,
          ScreenTextEntry::FloatValidate, ChangeLastSecondHint, nullptr);
      break;
    case preview_start:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_PREVIEW_START,
          std::to_string(pSong->music_sample_start_seconds_), 20,
          ScreenTextEntry::FloatValidate, ChangePreviewStart, nullptr);
      break;
    case preview_length:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_PREVIEW_LENGTH,
          std::to_string(pSong->music_sample_length_seconds_), 20,
          ScreenTextEntry::FloatValidate, ChangePreviewLength, nullptr);
      break;
    case min_bpm:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MIN_BPM, std::to_string(pSong->specified_bpm_min_), 20,
          ScreenTextEntry::FloatValidate, ChangeMinBPM, nullptr);
      break;
    case max_bpm:
      ScreenTextEntry::TextEntry(
          SM_None, ENTER_MAX_BPM, std::to_string(pSong->specified_bpm_max_), 20,
          ScreenTextEntry::FloatValidate, ChangeMaxBPM, nullptr);
      break;
    default:
      break;
  };
  SetDirty(true);
}

static LocalizedString ENTER_BEAT_0_OFFSET(
    "ScreenEdit", "Enter the offset for the song.");
static LocalizedString ENTER_BPM_VALUE("ScreenEdit", "Enter a new BPM value.");
static LocalizedString ENTER_STOP_VALUE(
    "ScreenEdit", "Enter a new Stop value.");
static LocalizedString ENTER_DELAY_VALUE(
    "ScreenEdit", "Enter a new Delay value.");
static LocalizedString ENTER_TICKCOUNT_VALUE(
    "ScreenEdit", "Enter a new Tickcount value.");
static LocalizedString ENTER_COMBO_VALUE(
    "ScreenEdit", "Enter a new Combo value.");
static LocalizedString ENTER_LABEL_VALUE(
    "ScreenEdit", "Enter a new Label value.");
static LocalizedString ENTER_WARP_VALUE(
    "ScreenEdit", "Enter a new Warp value.");
static LocalizedString ENTER_SPEED_PERCENT_VALUE(
    "ScreenEdit", "Enter a new Speed percent value.");
static LocalizedString ENTER_SPEED_WAIT_VALUE(
    "ScreenEdit", "Enter a new Speed wait value.");
static LocalizedString ENTER_SPEED_MODE_VALUE(
    "ScreenEdit", "Enter a new Speed mode value.");
static LocalizedString ENTER_SCROLL_VALUE(
    "ScreenEdit", "Enter a new Scroll value.");
static LocalizedString ENTER_FAKE_VALUE(
    "ScreenEdit", "Enter a new Fake value.");
static LocalizedString CONFIRM_TIMING_ERASE(
    "ScreenEdit", "Are you sure you want to erase this chart's timing data?");
void ScreenEdit::HandleTimingDataInformationChoice(
    TimingDataInformationChoice c, const std::vector<int>& iAnswers) {
  switch (c) {
    DEFAULT_FAIL(c);
    case beat_0_offset:
      ScreenTextEntry::TextEntry(
          SM_BackFromBeat0Change, ENTER_BEAT_0_OFFSET,
          std::to_string(GetAppropriateTiming().beat0_offset_in_seconds_), 20);
      break;
    case bpm:
      ScreenTextEntry::TextEntry(
          SM_BackFromBPMChange, ENTER_BPM_VALUE,
          std::to_string(GetAppropriateTiming().GetBPMAtBeat(GetBeat())), 10);
      break;
    case stop:
      ScreenTextEntry::TextEntry(
          SM_BackFromStopChange, ENTER_STOP_VALUE,
          std::to_string(GetAppropriateTiming().GetStopAtBeat(GetBeat())), 10);
      break;
    case delay:
      ScreenTextEntry::TextEntry(
          SM_BackFromDelayChange, ENTER_DELAY_VALUE,
          std::to_string(GetAppropriateTiming().GetDelayAtBeat(GetBeat())), 10);
      break;
    case tickcount:
      ScreenTextEntry::TextEntry(
          SM_BackFromTickcountChange, ENTER_TICKCOUNT_VALUE,
          ssprintf("%d", GetAppropriateTiming().GetTickcountAtBeat(GetBeat())),
          2);
      break;
    case combo: {
      const ComboSegment* cs =
          GetAppropriateTiming().GetComboSegmentAtBeat(GetBeat());
      ScreenTextEntry::TextEntry(
          SM_BackFromComboChange, ENTER_COMBO_VALUE,
          ssprintf("%d/%d", cs->GetCombo(), cs->GetMissCombo()), 7);
      break;
    }
    case label:
      ScreenTextEntry::TextEntry(
          SM_BackFromLabelChange, ENTER_LABEL_VALUE,
          ssprintf(
              "%s", GetAppropriateTiming().GetLabelAtBeat(GetBeat()).c_str()),
          64);
      break;
    case warp:
      ScreenTextEntry::TextEntry(
          SM_BackFromWarpChange, ENTER_WARP_VALUE,
          std::to_string(GetAppropriateTiming().GetWarpAtBeat(GetBeat())), 10);
      break;
    case speed_percent:
      ScreenTextEntry::TextEntry(
          SM_BackFromSpeedPercentChange, ENTER_SPEED_PERCENT_VALUE,
          std::to_string(
              GetAppropriateTiming()
                  .GetSpeedSegmentAtBeat(GetBeat())
                  ->GetRatio()),
          10);
      break;
    case scroll:
      ScreenTextEntry::TextEntry(
          SM_BackFromScrollChange, ENTER_SCROLL_VALUE,
          std::to_string(
              GetAppropriateTiming()
                  .GetScrollSegmentAtBeat(GetBeat())
                  ->GetRatio()),
          10);
      break;
    case speed_wait:
      ScreenTextEntry::TextEntry(
          SM_BackFromSpeedWaitChange, ENTER_SPEED_WAIT_VALUE,
          std::to_string(
              GetAppropriateTiming()
                  .GetSpeedSegmentAtBeat(GetBeat())
                  ->GetDelay()),
          10);
      break;
    case speed_mode: {
      ScreenTextEntry::TextEntry(
          SM_BackFromSpeedModeChange, ENTER_SPEED_MODE_VALUE, "", 3);

      break;
    }
    case fake: {
      ScreenTextEntry::TextEntry(
          SM_BackFromFakeChange, ENTER_FAKE_VALUE,
          std::to_string(GetAppropriateTiming().GetFakeAtBeat(GetBeat())), 10);
      break;
    }
    case shift_timing_in_region_down:
      timing_change_menu_purpose_ = menu_is_for_shifting;
      timing_rows_being_shitted_ = GetRowsFromAnswers(c, iAnswers);
      DisplayTimingChangeMenu();
      break;
    case shift_timing_in_region_up:
      timing_change_menu_purpose_ = menu_is_for_shifting;
      timing_rows_being_shitted_ = -GetRowsFromAnswers(c, iAnswers);
      DisplayTimingChangeMenu();
      break;
    case copy_timing_in_region:
      timing_change_menu_purpose_ = menu_is_for_copying;
      DisplayTimingChangeMenu();
      break;
    case clear_timing_in_region:
      timing_change_menu_purpose_ = menu_is_for_clearing;
      DisplayTimingChangeMenu();
      break;
    case paste_timing_from_clip:
      clipboard_full_timing_.CopyRange(
          0, MAX_NOTE_ROW, TimingSegmentType_Invalid, GetRow(),
          GetAppropriateTimingForUpdate());
      break;
    case copy_full_timing: {
      clipboard_full_timing_ = GetAppropriateTiming();
      break;
    }
    case paste_full_timing: {
      if (GAMESTATE->is_using_step_timing_) {
        GAMESTATE->cur_steps_[PLAYER_1]->timing_ = clipboard_full_timing_;
      } else {
        GAMESTATE->cur_song_->song_timing_ = clipboard_full_timing_;
      }
      SetDirty(true);
      break;
    }
    case erase_step_timing:
      ScreenPrompt::Prompt(
          SM_DoEraseStepTiming, CONFIRM_TIMING_ERASE, PROMPT_YES_NO, ANSWER_NO);
      break;
  }
}

void ScreenEdit::HandleTimingDataChangeChoice(
    TimingDataChangeChoice choice, const std::vector<int>& answers) {
  TimingSegmentType change_type = TimingSegmentType_Invalid;
  switch (choice) {
    case timing_all:
      change_type = TimingSegmentType_Invalid;
      break;
    case timing_bpm:
      change_type = SEGMENT_BPM;
      break;
    case timing_stop:
      change_type = SEGMENT_STOP;
      break;
    case timing_delay:
      change_type = SEGMENT_DELAY;
      break;
    case timing_time_sig:
      change_type = SEGMENT_TIME_SIG;
      break;
    case timing_warp:
      change_type = SEGMENT_WARP;
      break;
    case timing_label:
      change_type = SEGMENT_LABEL;
      break;
    case timing_tickcount:
      change_type = SEGMENT_TICKCOUNT;
      break;
    case timing_combo:
      change_type = SEGMENT_COMBO;
      break;
    case timing_speed:
      change_type = SEGMENT_SPEED;
      break;
    case timing_scroll:
      change_type = SEGMENT_SCROLL;
      break;
    case timing_fake:
      change_type = SEGMENT_FAKE;
      break;
    default:
      break;
  }
  int begin = note_field_edit_.begin_marker_;
  int end = note_field_edit_.end_marker_;
  if (begin < 0) {
    begin = GetRow();
  }
  if (end < 0) {
    end = MAX_NOTE_ROW;
  }
  switch (timing_change_menu_purpose_) {
    case menu_is_for_copying:
      clipboard_full_timing_.Clear();
      GetAppropriateTiming().CopyRange(
          begin, end, change_type, 0, clipboard_full_timing_);
      break;
    case menu_is_for_shifting:
      GetAppropriateTimingForUpdate().ShiftRange(
          begin, end, change_type, timing_rows_being_shitted_);
      break;
    case menu_is_for_clearing:
      GetAppropriateTimingForUpdate().ClearRange(begin, end, change_type);
      break;
    default:
      break;
  }
}

void ScreenEdit::HandleBGChangeChoice(
    BGChangeChoice c, const std::vector<int>& iAnswers) {
  BackgroundChange newChange;

  auto& changes = song_->GetBackgroundChanges(g_CurrentBGChangeLayer);
  for (auto iter = changes.begin(); iter != changes.end(); ++iter) {
    if (iter->start_beat_ == GAMESTATE->position_.song_beat_) {
      newChange = *iter;
      // delete the old change.  We'll add a new one below.
      changes.erase(iter);
      break;
    }
  }

  newChange.start_beat_ = GAMESTATE->position_.song_beat_;
  newChange.rate_ =
      StringToFloat(g_BackgroundChange.rows_[rate].choices_[iAnswers[rate]]) /
      100.f;
  newChange.transition_ =
      iAnswers[transition]
          ? g_BackgroundChange.rows_[transition].choices_[iAnswers[transition]]
          : std::string();
  newChange.def_.effect_ =
      iAnswers[effect]
          ? g_BackgroundChange.rows_[effect].choices_[iAnswers[effect]]
          : std::string();
  newChange.def_.color1_ =
      iAnswers[color1]
          ? g_BackgroundChange.rows_[color1].choices_[iAnswers[color1]]
          : std::string();
  newChange.def_.color2_ =
      iAnswers[color2]
          ? g_BackgroundChange.rows_[color2].choices_[iAnswers[color2]]
          : std::string();
  switch (iAnswers[file1_type]) {
    DEFAULT_FAIL(iAnswers[file1_type]);
    case none:
      newChange.def_.file1_ = "";
      break;
    case dynamic_random:
      newChange.def_.file1_ = RANDOM_BACKGROUND_FILE;
      break;
    case baked_random:
      newChange.def_.file1_ = GetOneBakedRandomFile(song_);
      break;
    case song_bganimation:
    case song_movie:
    case song_bitmap:
    case global_bganimation:
    case global_movie:
    case global_movie_song_group:
    case global_movie_song_group_and_genre: {
      BGChangeChoice row1 =
          (BGChangeChoice)(file1_song_bganimation + iAnswers[file1_type]);
      newChange.def_.file1_ =
          g_BackgroundChange.rows_[row1].choices_.empty()
              ? std::string("")
              : g_BackgroundChange.rows_[row1].choices_[iAnswers[row1]];
    } break;
  }
  switch (iAnswers[file2_type]) {
    DEFAULT_FAIL(iAnswers[file2_type]);
    case none:
      newChange.def_.file2_ = "";
      break;
    case dynamic_random:
      newChange.def_.file2_ = RANDOM_BACKGROUND_FILE;
      break;
    case baked_random:
      newChange.def_.file2_ = GetOneBakedRandomFile(song_);
      break;
    case song_bganimation:
    case song_movie:
    case song_bitmap:
    case global_bganimation:
    case global_movie:
    case global_movie_song_group:
    case global_movie_song_group_and_genre: {
      BGChangeChoice row2 =
          (BGChangeChoice)(file2_song_bganimation + iAnswers[file2_type]);
      newChange.def_.file2_ =
          g_BackgroundChange.rows_[row2].choices_.empty()
              ? std::string("")
              : g_BackgroundChange.rows_[row2].choices_[iAnswers[row2]];
    } break;
  }

  if (c == delete_change || newChange.def_.file1_.empty()) {
    // don't add
  } else {
    song_->AddBackgroundChange(g_CurrentBGChangeLayer, newChange);
  }
  g_CurrentBGChangeLayer = BACKGROUND_LAYER_Invalid;
}

void ScreenEdit::SetupCourseAttacks() {
  /* This is the first beat that can be changed without it being visible.  Until
   * we draw for the first time, any beat can be changed. */
  GAMESTATE->player_state_[main_player_]->last_drawn_beat_ = -100;

  // Put course options into effect.
  GAMESTATE->player_state_[main_player_]->mods_to_apply_.clear();
  GAMESTATE->player_state_[main_player_]->RemoveActiveAttacks();

  if (GAMESTATE->cur_course_) {
    AttackArray Attacks;

    if (EDIT_MODE == EditMode_CourseMods) {
      Attacks =
          GAMESTATE->cur_course_->entries_[GAMESTATE->edit_course_entry_index_]
              .attacks_;
    } else {
      GAMESTATE->cur_course_
          ->RevertFromDisk();  // Remove this and have a separate reload key?

      for (unsigned e = 0; e < GAMESTATE->cur_course_->entries_.size(); ++e) {
        if (GAMESTATE->cur_course_->entries_[e].song_id_.ToSong() != song_) {
          continue;
        }

        Attacks = GAMESTATE->cur_course_->entries_[e].attacks_;
        break;
      }
    }

    for (Attack& attack : Attacks) {
      GAMESTATE->player_state_[main_player_]->LaunchAttack(attack);
    }
  } else {
    const PlayerOptions& p =
        GAMESTATE->player_state_[main_player_]->player_options_.GetCurrent();
    if (GAMESTATE->cur_song_ && p.no_attack_ == 0 && p.rand_attack_ == 0) {
      AttackArray attacks = GAMESTATE->is_using_step_timing_
                                ? GAMESTATE->cur_steps_[PLAYER_1]->GetAttacks()
                                : GAMESTATE->cur_song_->attacks_;

      if (attacks.size() > 0) {
        for (Attack& attack : attacks) {
          // LaunchAttack is actually a misnomer.  The function actually adds
          // the attack to a list in the PlayerState which is checked and
          // updated every tick to see which ones to actually activate. -Kyz
          GAMESTATE->player_state_[main_player_]->LaunchAttack(attack);
        }
      }
    }
  }

  GAMESTATE->player_state_[main_player_]
      ->RebuildPlayerOptionsFromActiveAttacks();
}

void ScreenEdit::CopyToLastSave() {
  ASSERT(GAMESTATE->cur_steps_[main_player_] != nullptr);

  song_last_save_ = *GAMESTATE->cur_song_;
  steps_last_save_.clear();
  const std::vector<Steps*>& vSteps = GAMESTATE->cur_song_->GetStepsByStepsType(
      GAMESTATE->cur_steps_[main_player_]->steps_type_);
  for (Steps* it : vSteps) {
    steps_last_save_.push_back(*it);
  }
}

void ScreenEdit::CopyFromLastSave() {
  // We are assuming two things here:
  // 1) No steps can be created by ScreenEdit
  // 2) No steps can be deleted by ScreenEdit (except possibly when we exit)
  *GAMESTATE->cur_song_ = song_last_save_;
  const std::vector<Steps*>& vSteps = GAMESTATE->cur_song_->GetStepsByStepsType(
      GAMESTATE->cur_steps_[main_player_]->steps_type_);
  ASSERT_M(
      vSteps.size() == steps_last_save_.size(),
      ssprintf(
          "Step sizes don't match: %d, %d", int(vSteps.size()),
          int(steps_last_save_.size())));
  for (unsigned i = 0; i < vSteps.size(); ++i) {
    *vSteps[i] = steps_last_save_[i];
  }
}

void ScreenEdit::RevertFromDisk() {
  ASSERT(GAMESTATE->cur_steps_[PLAYER_1] != nullptr);
  StepsID id;
  id.FromSteps(GAMESTATE->cur_steps_[PLAYER_1]);
  ASSERT(id.IsValid());

  // If m_bInStepEditor is true while the song is reloaded, it screws up
  // loading and results in the steps being cleared.  -Kyz
  GAMESTATE->in_step_editor_ = false;
  GAMESTATE->cur_song_->ReloadFromSongDir();
  GAMESTATE->in_step_editor_ = true;

  Steps* pNewSteps = id.ToSteps(GAMESTATE->cur_song_, true);
  if (!pNewSteps) {
    // If the Steps we were currently editing vanished when we did the revert,
    // put a blank Steps in its place.  Note that this does not have to be the
    // work of someone maliciously changing the simfile; it could happen to
    // someone editing a new stepchart and reverting from disk, for example.
    pNewSteps = GAMESTATE->cur_song_->CreateSteps();
    pNewSteps->CreateBlank(id.GetStepsType());
    pNewSteps->SetDifficulty(id.GetDifficulty());
    GAMESTATE->cur_song_->AddSteps(pNewSteps);
  }
  GAMESTATE->cur_steps_[PLAYER_1].Set(pNewSteps);
  steps_ = pNewSteps;
  note_field_edit_.LoadWaveform(steps_->GetMusicPath());

  CopyToLastSave();
  SetDirty(false);
  SONGMAN->Invalidate(GAMESTATE->cur_song_);
}

void ScreenEdit::SaveUndo(const std::string& sDescription) {
  UndoState state;
  state.note_data_.CopyAll(note_data_edit_);
  state.description_ = sDescription;
  undo_stack_.push_back(state);
  if (undo_stack_.size() > kMaxUndoStates) {
    undo_stack_.erase(undo_stack_.begin());
  }
  // A new action invalidates any history that was previously undone.
  redo_stack_.clear();
}

static LocalizedString UNDO("ScreenEdit", "Undo - %s");
static LocalizedString CANT_UNDO("ScreenEdit", "Can't undo - no undo data.");
void ScreenEdit::Undo() {
  if (!undo_stack_.empty()) {
    UndoState redoState;
    redoState.note_data_.CopyAll(note_data_edit_);
    redoState.description_ = undo_stack_.back().description_;

    const std::string sDescription = undo_stack_.back().description_;
    note_data_edit_.CopyAll(undo_stack_.back().note_data_);
    undo_stack_.pop_back();

    redo_stack_.push_back(redoState);
    if (redo_stack_.size() > kMaxUndoStates) {
      redo_stack_.erase(redo_stack_.begin());
    }

    SCREENMAN->SystemMessage(
        ssprintf(UNDO.GetValue().c_str(), sDescription.c_str()));
  } else {
    SCREENMAN->SystemMessage(CANT_UNDO);
    SCREENMAN->PlayInvalidSound();
  }
}

static LocalizedString REDO("ScreenEdit", "Redo - %s");
static LocalizedString CANT_REDO("ScreenEdit", "Can't redo - no redo data.");
void ScreenEdit::Redo() {
  if (!redo_stack_.empty()) {
    UndoState undoState;
    undoState.note_data_.CopyAll(note_data_edit_);
    undoState.description_ = redo_stack_.back().description_;

    const std::string sDescription = redo_stack_.back().description_;
    note_data_edit_.CopyAll(redo_stack_.back().note_data_);
    redo_stack_.pop_back();

    undo_stack_.push_back(undoState);
    if (undo_stack_.size() > kMaxUndoStates) {
      undo_stack_.erase(undo_stack_.begin());
    }

    SCREENMAN->SystemMessage(
        ssprintf(REDO.GetValue().c_str(), sDescription.c_str()));
  } else {
    SCREENMAN->SystemMessage(CANT_REDO);
    SCREENMAN->PlayInvalidSound();
  }
}

void ScreenEdit::ClearUndo() {
  undo_stack_.clear();
  redo_stack_.clear();
}

static LocalizedString NOTHING_SELECTED(
    "ScreenEdit", "Nothing selected - use Select an area first.");
static LocalizedString CUT_TO_CLIPBOARD(
    "ScreenEdit", "Cut - Selection moved to clipboard.");
static LocalizedString COPY_TO_CLIPBOARD(
    "ScreenEdit", "Copy - Selection copied to clipboard.");
static LocalizedString CLIPBOARD_EMPTY(
    "ScreenEdit", "Clipboard is empty - nothing to paste.");
static LocalizedString PASTE_FROM_CLIPBOARD(
    "ScreenEdit", "Paste - Clipboard pasted at current beat.");

static LocalizedString CUT_WITH_TIMING(
    "ScreenEdit", "Cut - Selection and timing cut to clipboard.");
static LocalizedString COPY_WITH_TIMING(
    "ScreenEdit", "Copy - Selection and timing copied to clipboard.");
static LocalizedString PASTE_WITH_TIMING(
    "ScreenEdit", "Paste - Clipboard and timing pasted at current beat.");

// Moves the timing segments covering the current selection in or out of the
// timing clipboard.
void ScreenEdit::CutSelectionToClipboard(bool bIncludeTiming) {
  if (note_field_edit_.begin_marker_ == -1 ||
      note_field_edit_.end_marker_ == -1) {
    SCREENMAN->SystemMessage(NOTHING_SELECTED);
    SCREENMAN->PlayInvalidSound();
    return;
  }

  const int iBegin = note_field_edit_.begin_marker_;
  const int iEnd = note_field_edit_.end_marker_;

  if (bIncludeTiming) {
    clipboard_full_timing_.Clear();
    GetAppropriateTiming().CopyRange(
        iBegin, iEnd, TimingSegmentType_Invalid, 0, clipboard_full_timing_);
  }
  clipboard_has_timing_ = bIncludeTiming;

  HandleAlterMenuChoice(cut);

  if (bIncludeTiming) {
    GetAppropriateTimingForUpdate().ClearRange(
        iBegin, iEnd, TimingSegmentType_Invalid);
    SetDirty(true);
  }

  SCREENMAN->SystemMessage(bIncludeTiming ? CUT_WITH_TIMING : CUT_TO_CLIPBOARD);
}

void ScreenEdit::CopySelectionToClipboard(bool bIncludeTiming) {
  if (note_field_edit_.begin_marker_ == -1 ||
      note_field_edit_.end_marker_ == -1) {
    SCREENMAN->SystemMessage(NOTHING_SELECTED);
    SCREENMAN->PlayInvalidSound();
    return;
  }

  if (bIncludeTiming) {
    clipboard_full_timing_.Clear();
    GetAppropriateTiming().CopyRange(
        note_field_edit_.begin_marker_, note_field_edit_.end_marker_,
        TimingSegmentType_Invalid, 0, clipboard_full_timing_);
  }
  clipboard_has_timing_ = bIncludeTiming;

  HandleAlterMenuChoice(copy);
  SCREENMAN->SystemMessage(
      bIncludeTiming ? COPY_WITH_TIMING : COPY_TO_CLIPBOARD);
}

void ScreenEdit::PasteClipboardAtCurrentBeat(bool bIncludeTiming) {
  if (clipboard_.IsEmpty()) {
    SCREENMAN->SystemMessage(CLIPBOARD_EMPTY);
    SCREENMAN->PlayInvalidSound();
    return;
  }

  HandleAreaMenuChoice(paste_at_current_beat);

  const bool bPastedTiming = bIncludeTiming && clipboard_has_timing_;
  if (bPastedTiming) {
    clipboard_full_timing_.CopyRange(
        0, MAX_NOTE_ROW, TimingSegmentType_Invalid, GetRow(),
        GetAppropriateTimingForUpdate());
    SetDirty(true);
  }

  SCREENMAN->SystemMessage(
      bPastedTiming ? PASTE_WITH_TIMING : PASTE_FROM_CLIPBOARD);
}

static LocalizedString WAVEFORM_SHOWN("ScreenEdit", "Waveform - Shown.");
static LocalizedString WAVEFORM_HIDDEN("ScreenEdit", "Waveform - Hidden.");
void ScreenEdit::ToggleWaveform() {
  const bool bShow = !note_field_edit_.GetShowWaveform();
  note_field_edit_.SetShowWaveform(bShow);
  SCREENMAN->SystemMessage(bShow ? WAVEFORM_SHOWN : WAVEFORM_HIDDEN);
}

static LocalizedString CREATES_NOTES_PAST_END(
    "ScreenEdit",
    "This change creates notes past the end of the music and is not allowed.");
static LocalizedString CHANGE_REVERTED(
    "ScreenEdit", "The change has been reverted.");
void ScreenEdit::CheckNumberOfNotesAndUndo() {
  if (EDIT_MODE.GetValue() != EditMode_Home) {
    return;
  }

  const float fBeat =
      GAMESTATE->player_state_[main_player_]->position_.song_beat_;
  const TimeSignatureSegment* curTime =
      GAMESTATE->cur_song_->song_timing_.GetTimeSignatureSegmentAtBeat(fBeat);
  int rowsPerMeasure = curTime->GetDen() * curTime->GetNum();

  for (int row = 0; row <= note_data_edit_.GetLastRow();
       row += rowsPerMeasure) {
    int iNumNotesThisMeasure = 0;
    FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
        note_data_edit_, r, row, row + rowsPerMeasure)
    iNumNotesThisMeasure += note_data_edit_.GetNumTapNonEmptyTracks(r);
  }

  if (GAMESTATE->cur_steps_[0]->IsAnEdit()) {
    /* Check that the action didn't push notes any farther past the last
     * measure. This blocks Insert Beat from pushing past the end, but allows
     * Delete Beat to pull back the notes that are already past the end.
     */
    float fNewLastBeat = note_data_edit_.GetLastBeat();
    float fPreviousLastBeat =
        undo_stack_.empty() ? 0.f : undo_stack_.back().note_data_.GetLastBeat();
    bool bLastBeatIncreased = fNewLastBeat > fPreviousLastBeat;
    bool bPassedTheEnd = fNewLastBeat > GetMaximumBeatForNewNote();
    if (bLastBeatIncreased && bPassedTheEnd) {
      Undo();
      // The reverted state is invalid; don't let it be reached via Redo.
      if (!redo_stack_.empty()) {
        redo_stack_.pop_back();
      }
      std::string sError = CREATES_NOTES_PAST_END.GetValue() + "\n\n" +
                           CHANGE_REVERTED.GetValue();
      ScreenPrompt::Prompt(SM_None, sError);
      return;
    }
  }
}

float ScreenEdit::GetMaximumBeatForNewNote() const {
  switch (EDIT_MODE.GetValue()) {
    DEFAULT_FAIL(EDIT_MODE.GetValue());
    case EditMode_Practice:
    case EditMode_CourseMods:
    case EditMode_Home: {
      Song& s = *GAMESTATE->cur_song_;
      float fEndBeat = s.GetLastBeat();

      /* Round up to the next measure end.  Some songs end on weird beats
       * mid-measure, and it's odd to have movement capped to these weird
       * beats. */
      TimingData& timing = s.song_timing_;
      float playerBeat = GetAppropriatePosition().song_beat_;
      int beatsPerMeasure =
          timing.GetTimeSignatureSegmentAtBeat(playerBeat)->GetNum();
      fEndBeat += beatsPerMeasure;
      fEndBeat = ftruncf(fEndBeat, (float)beatsPerMeasure);

      return fEndBeat;
    }
    case EditMode_Full:
      return NoteRowToBeat(MAX_NOTE_ROW);
  }
}

float ScreenEdit::GetMaximumBeatForMoving() const {
  float fEndBeat = GetMaximumBeatForNewNote();

  /* Jump to GetLastBeat even if it's past the song's last beat
   * so that users can delete garbage steps past then end that they have
   * have inserted in a text editor.  Once they delete all steps on
   * GetLastBeat() and move off of that beat, they won't be able to return. */
  fEndBeat = std::max(fEndBeat, note_data_edit_.GetLastBeat());

  return fEndBeat;
}

struct EditHelpLine {
  const char* english_description_;
  std::vector<EditButton> veb_;

  EditHelpLine(
      const char* _szEnglishDescription, EditButton eb0,
      EditButton eb1 = EditButton_Invalid, EditButton eb2 = EditButton_Invalid,
      EditButton eb3 = EditButton_Invalid, EditButton eb4 = EditButton_Invalid,
      EditButton eb5 = EditButton_Invalid, EditButton eb6 = EditButton_Invalid,
      EditButton eb7 = EditButton_Invalid, EditButton eb8 = EditButton_Invalid,
      EditButton eb9 = EditButton_Invalid) {
    english_description_ = _szEnglishDescription;
#define PUSH_IF_VALID(x) \
  if (x != EditButton_Invalid) veb_.push_back(x);
    PUSH_IF_VALID(eb0);
    PUSH_IF_VALID(eb1);
    PUSH_IF_VALID(eb2);
    PUSH_IF_VALID(eb3);
    PUSH_IF_VALID(eb4);
    PUSH_IF_VALID(eb5);
    PUSH_IF_VALID(eb6);
    PUSH_IF_VALID(eb7);
    PUSH_IF_VALID(eb8);
    PUSH_IF_VALID(eb9);
#undef PUSH_IF_VALID
  }
};
// TODO: Identify which of these can be removed and sent to a readme.
static const EditHelpLine g_EditHelpLines[] = {
    EditHelpLine(
        "Move cursor", EDIT_BUTTON_SCROLL_UP_LINE,
        EDIT_BUTTON_SCROLL_DOWN_LINE),
    EditHelpLine(
        "Jump measure", EDIT_BUTTON_SCROLL_UP_PAGE,
        EDIT_BUTTON_SCROLL_DOWN_PAGE),
    EditHelpLine(
        "Jump measure", EDIT_BUTTON_SCROLL_PREV_MEASURE,
        EDIT_BUTTON_SCROLL_NEXT_MEASURE),
    EditHelpLine("Select region", EDIT_BUTTON_SCROLL_SELECT),
    EditHelpLine(
        "Jump to first/last beat", EDIT_BUTTON_SCROLL_HOME,
        EDIT_BUTTON_SCROLL_END),
    EditHelpLine(
        "Change zoom", EDIT_BUTTON_SCROLL_SPEED_UP,
        EDIT_BUTTON_SCROLL_SPEED_DOWN),
    EditHelpLine("Play", EDIT_BUTTON_PLAY_SELECTION),
    EditHelpLine("Play current beat to end", EDIT_BUTTON_PLAY_FROM_CURSOR),
    EditHelpLine("Play whole song", EDIT_BUTTON_PLAY_FROM_START),
    EditHelpLine("Record", EDIT_BUTTON_RECORD_SELECTION),
    EditHelpLine("Set selection", EDIT_BUTTON_LAY_SELECT),
    EditHelpLine(
        "Next/prev steps of same StepsType", EDIT_BUTTON_OPEN_PREV_STEPS,
        EDIT_BUTTON_OPEN_NEXT_STEPS),
    EditHelpLine(
        "Decrease/increase BPM at cur beat", EDIT_BUTTON_BPM_DOWN,
        EDIT_BUTTON_BPM_UP),
    EditHelpLine(
        "Decrease/increase stop at cur beat", EDIT_BUTTON_STOP_DOWN,
        EDIT_BUTTON_STOP_UP),
    EditHelpLine(
        "Decrease/increase delay at cur beat", EDIT_BUTTON_DELAY_DOWN,
        EDIT_BUTTON_DELAY_UP),
    EditHelpLine(
        "Decrease/increase music offset", EDIT_BUTTON_OFFSET_DOWN,
        EDIT_BUTTON_OFFSET_UP),
    EditHelpLine(
        "Decrease/increase sample music start", EDIT_BUTTON_SAMPLE_START_DOWN,
        EDIT_BUTTON_SAMPLE_START_UP),
    EditHelpLine(
        "Decrease/increase sample music length", EDIT_BUTTON_SAMPLE_LENGTH_DOWN,
        EDIT_BUTTON_SAMPLE_LENGTH_UP),
    EditHelpLine("Play sample music", EDIT_BUTTON_PLAY_SAMPLE_MUSIC),
    EditHelpLine(
        "Add/Edit Background Change", EDIT_BUTTON_OPEN_BGCHANGE_LAYER1_MENU),
    EditHelpLine("Insert beat and shift down", EDIT_BUTTON_INSERT),
    EditHelpLine(
        "Shift BPM changes and stops down one beat",
        EDIT_BUTTON_INSERT_SHIFT_PAUSES),
    EditHelpLine("Delete beat and shift up", EDIT_BUTTON_DELETE),
    EditHelpLine(
        "Shift BPM changes and stops up one beat",
        EDIT_BUTTON_DELETE_SHIFT_PAUSES),
    EditHelpLine(
        "Cycle between tap notes", EDIT_BUTTON_CYCLE_TAP_LEFT,
        EDIT_BUTTON_CYCLE_TAP_RIGHT),
    EditHelpLine("Add to/remove from right half", EDIT_BUTTON_RIGHT_SIDE),
    EditHelpLine("Switch Timing", EDIT_BUTTON_SWITCH_TIMINGS),
    EditHelpLine("Switch player (Routine only)", EDIT_BUTTON_SWITCH_PLAYERS),
    EditHelpLine("Undo/Redo", EDIT_BUTTON_UNDO, EDIT_BUTTON_REDO),
    EditHelpLine(
        "Cut/Copy selection to clipboard", EDIT_BUTTON_CUT, EDIT_BUTTON_COPY),
    EditHelpLine("Paste from clipboard", EDIT_BUTTON_PASTE),
    EditHelpLine("Toggle waveform display", EDIT_BUTTON_TOGGLE_WAVEFORM),
};

static bool IsMapped(EditButton eb, const MapEditToDI& editmap) {
  for (int s = 0; s < NUM_EDIT_TO_DEVICE_SLOTS; s++) {
    DeviceInput diPress = editmap.button_[eb][s];
    if (diPress.IsValid()) {
      return true;
    }
  }
  return false;
}

static void ProcessKeyName(std::string& s) { Replace(s, "Key_", ""); }

static void ProcessKeyNames(std::vector<std::string>& vs, bool doSort) {
  for (std::string& s : vs) {
    ProcessKeyName(s);
  }

  if (doSort) {
    sort(vs.begin(), vs.end());
  }
  std::vector<std::string>::iterator toDelete = unique(vs.begin(), vs.end());
  vs.erase(toDelete, vs.end());
}

static std::string GetDeviceButtonsLocalized(
    const std::vector<EditButton>& veb, const MapEditToDI& editmap) {
  std::vector<std::string> vsPress;
  std::vector<std::string> vsHold;
  for (const EditButton& eb : veb) {
    if (!IsMapped(eb, editmap)) {
      continue;
    }

    for (int s = 0; s < NUM_EDIT_TO_DEVICE_SLOTS; s++) {
      DeviceInput diPress = editmap.button_[eb][s];
      DeviceInput diHold = editmap.hold_[eb][s];
      if (diPress.IsValid()) {
        vsPress.push_back(
            Capitalize(INPUTMAN->GetLocalizedInputString(diPress)));
      }
      if (diHold.IsValid()) {
        vsHold.push_back(Capitalize(INPUTMAN->GetLocalizedInputString(diHold)));
      }
    }
  }

  ProcessKeyNames(vsPress, false);
  ProcessKeyNames(vsHold, true);

  std::string s = join("/", vsPress);
  if (!vsHold.empty()) {
    s = join("/", vsHold) + " + " + s;
  }
  return s;
}

AttackArray ScreenEdit::GetAttacksForEditing() const {
  if (GAMESTATE->is_using_step_timing_) {
    return steps_->GetAttacks();
  }
  return song_->attacks_;
}

AttackArray& ScreenEdit::GetAttackArray(AttackSource source) {
  return source == AttackSource::Song ? song_->attacks_
                                      : steps_->GetStepsAttacks();
}

AttackArray& ScreenEdit::GetSelectedAttackArray() {
  ASSERT(selected_attack_.has_value());
  return GetAttackArray(selected_attack_->source_);
}

Attack& ScreenEdit::GetSelectedAttack() {
  return GetSelectedAttackArray()[selected_attack_->index_];
}

ScreenEdit::SelectedAttack ScreenEdit::ResolveAttackIndex(
    int combinedIndex) const {
  if (!GAMESTATE->is_using_step_timing_) {
    return {AttackSource::Song, combinedIndex};
  }

  const int songAttackCount = song_->attacks_.size();
  if (combinedIndex < songAttackCount) {
    return {AttackSource::Song, combinedIndex};
  }
  return {AttackSource::Steps, combinedIndex - songAttackCount};
}

void ScreenEdit::DoStepAttackMenu() {
  const TimingData& timing = GetAppropriateTiming();
  float startTime = timing.GetElapsedTimeFromBeat(GetBeat());
  AttackArray attacks = GetAttacksForEditing();
  std::vector<int> points = FindAllAttacksAtTime(attacks, startTime);

  g_AttackAtTimeMenu.rows_.clear();
  unsigned index = 0;

  for (int& i : points) {
    const Attack& attack = attacks[i];
    std::string desc = ssprintf(
        "%g -> %g (%d mod[s])", startTime, startTime + attack.secs_remaining_,
        attack.GetNumAttacks());

    g_AttackAtTimeMenu.rows_.push_back(MenuRowDef(
        index++, desc, true, EditMode_CourseMods, false, false, 0, "Modify",
        "Delete"));
  }
  g_AttackAtTimeMenu.rows_.push_back(MenuRowDef(
      index, "Add Attack", true, EditMode_CourseMods, true, true, 0, nullptr));

  EditMiniMenu(&g_AttackAtTimeMenu, SM_BackFromAttackAtTime);
}

static LocalizedString TRACK_NUM("ScreenEdit", "Track %d");
static LocalizedString NO_KEYSND("ScreenEdit", "None");
static LocalizedString NEWKEYSND("ScreenEdit", "New Sound");

void ScreenEdit::DoKeyboardTrackMenu() {
  g_KeysoundTrack.rows_.clear();
  std::vector<std::string>& kses = song_->keysound_file_;

  std::vector<std::string> choices;
  for (const std::string& ks : kses) {
    choices.push_back(ks);
  }
  choices.push_back(NEWKEYSND);
  choices.push_back(NO_KEYSND);
  int numKeysounds = kses.size();
  int foundKeysounds = 0;
  for (int i = 0; i < note_data_edit_.GetNumTracks(); ++i) {
    const TapNote& tn = note_data_edit_.GetTapNote(i, this->GetRow());
    int keyIndex = tn.keysound_index_;
    if (keyIndex == -1) {
      keyIndex = numKeysounds;
    } else {
      ++foundKeysounds;
    }

    g_KeysoundTrack.rows_.push_back(MenuRowDef(
        i, ssprintf(TRACK_NUM.GetValue().c_str(), i + 1), true, EditMode_Full,
        false, false, keyIndex, choices));
  }
  g_KeysoundTrack.rows_.push_back(MenuRowDef(
      note_data_edit_.GetNumTracks(), "Remove Keysound", foundKeysounds > 0,
      EditMode_Full, false, false, 0, kses));

  EditMiniMenu(&g_KeysoundTrack, SM_BackFromKeysoundTrack);
}

void ScreenEdit::DoHelp() {
  g_EditHelp.rows_.clear();

  for (unsigned i = 0; i < ARRAYLEN(g_EditHelpLines); ++i) {
    const EditHelpLine& hl = g_EditHelpLines[i];

    if (!IsMapped(hl.veb_[0], edit_mappings_device_input_)) {
      continue;
    }

    std::string sButtons =
        GetDeviceButtonsLocalized(hl.veb_, edit_mappings_device_input_);
    std::string sDescription =
        THEME->GetString("EditHelpDescription", hl.english_description_);

#if defined(MACOSX)
    Replace(sButtons, "Ctrl", "Cmd");
    Replace(sButtons, "Alt", "Option");
#endif

    // TODO: Better way of hiding routine only key on non-routine.
    if (hl.veb_[0] == EDIT_BUTTON_SWITCH_PLAYERS &&
        input_player_number_ == PLAYER_INVALID) {
      continue;
    }

    g_EditHelp.rows_.push_back(MenuRowDef(
        -1, sDescription, false, EditMode_Practice, false, false, 0,
        sButtons.c_str()));
  }

  EditMiniMenu(&g_EditHelp);
}

void ScreenEdit::SeekSong(float second) {
  float desired_beat = GetAppropriateTiming().GetBeatFromElapsedTime(second);
  // Don't seek past the final beat of the song.
  ScrollTo(std::min(desired_beat, GetMaximumBeatForNewNote()));
  trailing_beat_ = GetBeat();
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to ScreenEdit. */
class LunaScreenEdit : public Luna<ScreenEdit> {
 public:
  static int SeekSong(T* p, lua_State* L) {
    p->SeekSong(FArg(1));
    return 1;
  }
  DEFINE_METHOD(GetEditState, GetEditState());

  LunaScreenEdit() {
    ADD_METHOD(GetEditState);
    ADD_METHOD(SeekSong);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenEdit, ScreenWithMenuElements)

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
