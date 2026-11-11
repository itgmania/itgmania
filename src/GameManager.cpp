#include "GameManager.h"

#include <cstddef>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"  // for GameButton constants
#include "GameLoop.h"   // for ChangeGame
#include "InputMapper.h"
#include "LightsManager.h"  // for NUM_CabinetLight
#include "LuaManager.h"
#include "NoteSkinManager.h"
#include "RageException.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "StdString.h"
#include "StepMania.h"
#include "Style.h"
#include "ThemeManager.h"
#include "global.h"

GameManager* GAMEMAN =
    nullptr;  // global and accessable from anywhere in our program

enum {
  TRACK_1 = 0,
  TRACK_2,
  TRACK_3,
  TRACK_4,
  TRACK_5,
  TRACK_6,
  TRACK_7,
  TRACK_8,
  TRACK_9,
  TRACK_10,
  TRACK_11,
  TRACK_12,
  TRACK_13,
  TRACK_14,
  TRACK_15,
  TRACK_16,
  // 16 tracks needed for beat-double7 and techno-double8
};

std::string StepsTypeInfo::GetLocalizedString() const {
  if (THEME->HasString("StepsType", name_)) {
    return THEME->GetString("StepsType", name_);
  }
  return name_;
}

static const StepsTypeInfo kStepsTypeInfos[] = {
    // dance
    {"dance-single", 4, true, StepsTypeCategory_Single},
    {"dance-double", 8, true, StepsTypeCategory_Double},
    {"dance-couple", 8, true, StepsTypeCategory_Couple},
    {"dance-solo", 6, true, StepsTypeCategory_Single},
    {"dance-threepanel", 3, true,
     StepsTypeCategory_Single},  // thanks to kurisu
    {"dance-routine", 8, false, StepsTypeCategory_Routine},
    // pump
    {"pump-single", 5, true, StepsTypeCategory_Single},
    {"pump-halfdouble", 6, true, StepsTypeCategory_Double},
    {"pump-double", 10, true, StepsTypeCategory_Double},
    {"pump-couple", 10, true, StepsTypeCategory_Couple},
    // uh, dance-routine has that one bool as false... wtf? -aj
    {"pump-routine", 10, true, StepsTypeCategory_Routine},
    // technomotion
    {"techno-single4", 4, true, StepsTypeCategory_Single},
    {"techno-single5", 5, true, StepsTypeCategory_Single},
    {"techno-single8", 8, true, StepsTypeCategory_Single},
    {"techno-double4", 8, true, StepsTypeCategory_Double},
    {"techno-double5", 10, true, StepsTypeCategory_Double},
    {"techno-double8", 16, true, StepsTypeCategory_Double},
    // cabinet lights and other fine StepsTypes that don't exist lol
    {"lights-cabinet", NUM_CabinetLight, false,
     StepsTypeCategory_Single},  // XXX disable lights autogen for now
};
static_assert(
    ARRAYLEN(kStepsTypeInfos) == NUM_StepsType,
    "ARRAYLEN(g_StepsTypeInfos) != NUM_StepsType");

// Important:  Every game must define the buttons: "Start", "Back", "MenuLeft",
// "Operator" and "MenuRight"
static const AutoMappings kAutoKeyMappingsDance = AutoMappings(
    "", "", "", AutoMappingEntry(0, KEY_DEL, GAME_BUTTON_MENULEFT, false),
    AutoMappingEntry(0, KEY_PGDN, GAME_BUTTON_MENURIGHT, false),
    AutoMappingEntry(0, KEY_HOME, GAME_BUTTON_MENUUP, false),
    AutoMappingEntry(0, KEY_END, GAME_BUTTON_MENUDOWN, false),
    AutoMappingEntry(0, KEY_LEFT, DANCE_BUTTON_LEFT, false),
    AutoMappingEntry(0, KEY_RIGHT, DANCE_BUTTON_RIGHT, false),
    AutoMappingEntry(0, KEY_UP, DANCE_BUTTON_UP, false),
    AutoMappingEntry(0, KEY_DOWN, DANCE_BUTTON_DOWN, false),
    AutoMappingEntry(0, KEY_KP_SLASH, GAME_BUTTON_MENULEFT, true),
    AutoMappingEntry(0, KEY_KP_ASTERISK, GAME_BUTTON_MENURIGHT, true),
    AutoMappingEntry(0, KEY_KP_HYPHEN, GAME_BUTTON_MENUUP, true),
    AutoMappingEntry(0, KEY_KP_PLUS, GAME_BUTTON_MENUDOWN, true),
    AutoMappingEntry(0, KEY_KP_C4, DANCE_BUTTON_LEFT, true),
    AutoMappingEntry(0, KEY_KP_C6, DANCE_BUTTON_RIGHT, true),
    AutoMappingEntry(0, KEY_KP_C8, DANCE_BUTTON_UP, true),
    AutoMappingEntry(0, KEY_KP_C2, DANCE_BUTTON_DOWN, true),
    AutoMappingEntry(0, KEY_KP_C7, DANCE_BUTTON_UPLEFT, true),
    AutoMappingEntry(0, KEY_KP_C9, DANCE_BUTTON_UPRIGHT, true));

// xxx: get this from the theme? (see others)
// the problem with getting it from the noteskin is that this is meant to be
// static const; if we switch to anything we likely won't get const anymore
// but i may be talking out of my ass -aj
static const int kDanceColSpacing = 64;

// static ThemeMetric<int>	DANCE_COL_SPACING("ColumnSpacing","Dance");
//  named after a similar metric in Aldo_MX's build for
//  compatibility/familiarity:
// float	DANCE_COL_SPACING
// NOTESKIN->GetMetricF("NoteDisplay","ArrowColSpacing");
/* looking for ARROW_SIZE should be enough (ArrowSize)
 * just arroweffects.cpp for rowspacing (ArrowRowSpacing)
 */

static const Style kStyleDanceSingle = {
    // STYLE_DANCE_SINGLE
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    true,                        // m_bUsedForDemonstration
    true,                        // m_bUsedForHowToPlay
    "single",                    // m_szName
    StepsType_dance_single,      // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    4,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_3, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 1.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_3, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 1.5f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {0, 3, 2, 1, Style::END_MAPPING},
     {0, 3, 2, 1, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3},
    true,   // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleDanceVersus = {
    // STYLE_DANCE_VERSUS
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    true,                          // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "versus",                      // m_szName
    StepsType_dance_single,        // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    4,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_3, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 1.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_3, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 1.5f, nullptr},
        },
    },
    {{0, 3, 2, 1, Style::END_MAPPING}, {0, 3, 2, 1, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3},
    true,   // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleDanceDouble = {
    // STYLE_DANCE_DOUBLE
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    true,                         // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "double",                     // m_szName
    StepsType_dance_double,       // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    8,                            // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {0, 3, 2, 1, Style::END_MAPPING},
     {4, 7, 6, 5, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleDanceCouple = {
    // STYLE_DANCE_COUPLE
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    false,                         // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "couple",                      // m_szName
    StepsType_dance_couple,        // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    4,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_3, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 1.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_5, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_6, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 1.5f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {0, 3, 2, 1, Style::END_MAPPING},
     {0, 3, 2, 1, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3},
    true,  // m_bCanUseBeginnerHelper
    true,  // m_bLockDifficulties
};

static const Style kStyleDanceSolo = {
    // STYLE_DANCE_SOLO
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    false,                       // m_bUsedForHowToPlay
    "solo",                      // m_szName
    StepsType_dance_solo,        // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    6,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 2.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_4, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 2.5f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {0, 5, 3, 2, 1, 4, Style::END_MAPPING},
     {0, 5, 3, 2, 1, 4, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleDanceCoupleEdit = {
    // STYLE_DANCE_COUPLE
    false,                        // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "couple-edit",                // m_szName
    StepsType_dance_couple,       // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    8,                            // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 4.f, nullptr},
            {TRACK_2, -kDanceColSpacing * 3.f, nullptr},
            {TRACK_3, -kDanceColSpacing * 2.f, nullptr},
            {TRACK_4, -kDanceColSpacing * 1.f, nullptr},
            {TRACK_5, +kDanceColSpacing * 1.f, nullptr},
            {TRACK_6, +kDanceColSpacing * 2.f, nullptr},
            {TRACK_7, +kDanceColSpacing * 3.f, nullptr},
            {TRACK_8, +kDanceColSpacing * 4.f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 4.f, nullptr},
            {TRACK_2, -kDanceColSpacing * 3.f, nullptr},
            {TRACK_3, -kDanceColSpacing * 2.f, nullptr},
            {TRACK_4, -kDanceColSpacing * 1.f, nullptr},
            {TRACK_5, +kDanceColSpacing * 1.f, nullptr},
            {TRACK_6, +kDanceColSpacing * 2.f, nullptr},
            {TRACK_7, +kDanceColSpacing * 3.f, nullptr},
            {TRACK_8, +kDanceColSpacing * 4.f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {0, 3, 2, 1, Style::END_MAPPING},
        {4, 7, 6, 5, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleDanceThreePanel = {
    // STYLE_DANCE_THREEPANEL
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    false,                       // m_bUsedForHowToPlay
    "threepanel",                // m_szName
    StepsType_dance_threepanel,  // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    3,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 1.0f, nullptr},
            {TRACK_2, +kDanceColSpacing * 0.0f, nullptr},
            {TRACK_3, +kDanceColSpacing * 1.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 1.0f, nullptr},
            {TRACK_2, +kDanceColSpacing * 0.0f, nullptr},
            {TRACK_3, +kDanceColSpacing * 1.0f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     // 4 3 5
     {0, 2, Style::NO_MAPPING, 1, 0, 2, Style::END_MAPPING},
     {0, 2, Style::NO_MAPPING, 1, 0, 2, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

// todo: re-enable? (lol)
/*
static const Style g_Style_Dance_Solo_Versus =
{	// STYLE_DANCE_SOLO_VERSUS
        "dance-solo-versus",		// m_szName
        StepsType_dance_solo,		// m_StepsType
        ONE_PLAYER_ONE_CREDIT,		// m_StyleType
        6,				// m_iColsPerPlayer
        {	// m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
                {	// PLAYER_1
                        { TRACK_1,	-DANCE_6PANEL_VERSUS_COL_SPACING*2.5f },
                        { TRACK_2,	-DANCE_6PANEL_VERSUS_COL_SPACING*1.5f },
                        { TRACK_3,	-DANCE_6PANEL_VERSUS_COL_SPACING*0.5f },
                        { TRACK_4,	+DANCE_6PANEL_VERSUS_COL_SPACING*0.5f },
                        { TRACK_5,	+DANCE_6PANEL_VERSUS_COL_SPACING*1.5f },
                        { TRACK_6,	+DANCE_6PANEL_VERSUS_COL_SPACING*2.5f },
                },
                {	// PLAYER_2
                        { TRACK_1,	-DANCE_6PANEL_VERSUS_COL_SPACING*2.5f },
                        { TRACK_2,	-DANCE_6PANEL_VERSUS_COL_SPACING*1.5f },
                        { TRACK_3,	-DANCE_6PANEL_VERSUS_COL_SPACING*0.5f },
                        { TRACK_4,	+DANCE_6PANEL_VERSUS_COL_SPACING*0.5f },
                        { TRACK_5,	+DANCE_6PANEL_VERSUS_COL_SPACING*1.5f },
                        { TRACK_6,	+DANCE_6PANEL_VERSUS_COL_SPACING*2.5f },
                },
        },
        {	// m_iInputColumn[NUM_GameController][NUM_GameButton]
                { 0, 5, 3, 2, 1, 4, Style::END_MAPPING },
                { 0, 5, 3, 2, 1, 4, Style::END_MAPPING }
        },
        {	// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
                0,5,1,4,2,3		// outside in
        },
        false, // m_bCanUseBeginnerHelper
        false, // m_bLockDifficulties
};	*/

static const Style kStyleDanceRoutine = {
    // STYLE_DANCE_ROUTINE
    true,                             // m_bUsedForGameplay
    true,                             // m_bUsedForEdit
    false,                            // m_bUsedForDemonstration
    false,                            // m_bUsedForHowToPlay
    "routine",                        // m_szName
    StepsType_dance_routine,          // m_StepsType
    StyleType_TwoPlayersSharedSides,  // m_StyleType
    8,                                // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {0, 3, 2, 1, Style::END_MAPPING},
     {4, 7, 6, 5, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    true,   // m_bLockDifficulties
};

static const Style* game_dance_styles[] = {
    &kStyleDanceSingle,  &kStyleDanceVersus,     &kStyleDanceDouble,
    &kStyleDanceCouple,  &kStyleDanceSolo,       &kStyleDanceCoupleEdit,
    &kStyleDanceRoutine, &kStyleDanceThreePanel, nullptr};

static const Game kGameDance = {
    "dance",             // m_szName
    game_dance_styles,   // m_apStyles
    false,               // m_bCountNotesSeparately
    false,               // m_PlayersHaveSeparateStyles
    {                    // m_InputScheme
     "dance",            // m_szName
     NUM_DANCE_BUTTONS,  // m_iButtonsPerController
     {
         // m_szButtonNames
         {"Left", GAME_BUTTON_LEFT},
         {"Right", GAME_BUTTON_RIGHT},
         {"Up", GAME_BUTTON_UP},
         {"Down", GAME_BUTTON_DOWN},
         {"UpLeft", GameButton_Invalid},
         {"UpRight", GameButton_Invalid},
     },
     &kAutoKeyMappingsDance},
    {
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
    },
    TNS_W1,  // m_mapW1To
    TNS_W2,  // m_mapW2To
    TNS_W3,  // m_mapW3To
    TNS_W4,  // m_mapW4To
    TNS_W5,  // m_mapW5To
};

static const AutoMappings kAutoKeyMappingsPump = AutoMappings(
    "", "", "", AutoMappingEntry(0, KEY_Cq, PUMP_BUTTON_UPLEFT, false),
    AutoMappingEntry(0, KEY_Ce, PUMP_BUTTON_UPRIGHT, false),
    AutoMappingEntry(0, KEY_Cs, PUMP_BUTTON_CENTER, false),
    AutoMappingEntry(0, KEY_Cz, PUMP_BUTTON_DOWNLEFT, false),
    AutoMappingEntry(0, KEY_Cc, PUMP_BUTTON_DOWNRIGHT, false),
    AutoMappingEntry(0, KEY_KP_C7, PUMP_BUTTON_UPLEFT, true),
    AutoMappingEntry(0, KEY_KP_C9, PUMP_BUTTON_UPRIGHT, true),
    AutoMappingEntry(0, KEY_KP_C5, PUMP_BUTTON_CENTER, true),
    AutoMappingEntry(0, KEY_KP_C1, PUMP_BUTTON_DOWNLEFT, true),
    AutoMappingEntry(0, KEY_KP_C3, PUMP_BUTTON_DOWNRIGHT, true),

    // unmap confusing default MenuButtons
    AutoMappingEntry(0, KEY_KP_C8, GameButton_Invalid, false),
    AutoMappingEntry(0, KEY_KP_C2, GameButton_Invalid, false),
    AutoMappingEntry(0, KEY_KP_C4, GameButton_Invalid, false),
    AutoMappingEntry(0, KEY_KP_C6, GameButton_Invalid, false));

// PIU Defaults: RowSpacing = 60; ColSpacing = 52; ArrowSize = 54;
// apparently column spacing is 48px
// static ThemeMetric<int>	PUMP_COL_SPACING
// ("ColumnSpacing","Pump");
static const int kPumpColSpacing = 48;

static const Style kStylePumpSingle = {
    // STYLE_PUMP_SINGLE
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    true,                        // m_bUsedForHowToPlay
    "single",                    // m_szName
    StepsType_pump_single,       // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    5,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_3, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_4, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_5, +kPumpColSpacing * 2.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_3, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_4, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_5, +kPumpColSpacing * 2.0f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {1, 3, 2, 0, 4, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStylePumpVersus = {
    // STYLE_PUMP_VERSUS
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    true,                          // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "versus",                      // m_szName
    StepsType_pump_single,         // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    5,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_3, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_4, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_5, +kPumpColSpacing * 2.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_3, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_4, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_5, +kPumpColSpacing * 2.0f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {1, 3, 2, 0, 4, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStylePumpHalfDouble = {
    // STYLE_PUMP_HALFDOUBLE
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "halfdouble",                 // m_szName
    StepsType_pump_halfdouble,    // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    6,                            // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_4, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_5, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 2.5f + 4, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_4, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_5, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 2.5f + 4, nullptr},
        },
    },
    {// m_iInputColumn[NUM_GameController][NUM_GameButton]
     {Style::NO_MAPPING, 1, 0, Style::NO_MAPPING, 2, Style::END_MAPPING},
     {4, Style::NO_MAPPING, 5, 3, Style::NO_MAPPING, Style::END_MAPPING}},
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 3, 1, 4, 0, 5},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStylePumpDouble = {
    // STYLE_PUMP_DOUBLE
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "double",                     // m_szName
    StepsType_pump_double,        // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    10,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 4.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 3.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 2.5f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 3.5f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 4.5f + 4, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 4.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 3.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 2.5f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 3.5f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 4.5f + 4, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {6, 8, 7, 5, 9, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4, 2 + 5, 1 + 5, 3 + 5, 0 + 5, 4 + 5},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStylePumpCouple = {
    // STYLE_PUMP_COUPLE
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    false,                         // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "couple",                      // m_szName
    StepsType_pump_couple,         // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    5,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_2, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_3, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_4, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_5, +kPumpColSpacing * 2.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_6, -kPumpColSpacing * 2.0f, nullptr},
            {TRACK_7, -kPumpColSpacing * 1.0f, nullptr},
            {TRACK_8, +kPumpColSpacing * 0.0f, nullptr},
            {TRACK_9, +kPumpColSpacing * 1.0f, nullptr},
            {TRACK_10, +kPumpColSpacing * 2.0f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {1, 3, 2, 0, 4, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4},
    false,  // m_bCanUseBeginnerHelper
    true,   // m_bLockDifficulties
};

static const Style kStylePumpCoupleEdit = {
    // STYLE_PUMP_EDIT_COUPLE
    false,                        // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "couple-edit",                // m_szName
    StepsType_pump_couple,        // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    10,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 5.0f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 4.0f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 3.0f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 2.0f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 1.0f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 1.0f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 2.0f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 3.0f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 4.0f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 5.0f + 4, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 5.0f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 4.0f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 3.0f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 2.0f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 1.0f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 1.0f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 2.0f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 3.0f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 4.0f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 5.0f + 4, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {6, 8, 7, 5, 9, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4, 2 + 5, 1 + 5, 3 + 5, 0 + 5, 4 + 5},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStylePumpRoutine = {
    // STYLE_PUMP_ROUTINE
    true,                             // m_bUsedForGameplay
    true,                             // m_bUsedForEdit
    false,                            // m_bUsedForDemonstration
    false,                            // m_bUsedForHowToPlay
    "routine",                        // m_szName
    StepsType_pump_routine,           // m_StepsType
    StyleType_TwoPlayersSharedSides,  // m_StyleType
    10,                               // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kPumpColSpacing * 4.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 3.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 2.5f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 3.5f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 4.5f + 4, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kPumpColSpacing * 4.5f - 4, nullptr},
            {TRACK_2, -kPumpColSpacing * 3.5f - 4, nullptr},
            {TRACK_3, -kPumpColSpacing * 2.5f - 4, nullptr},
            {TRACK_4, -kPumpColSpacing * 1.5f - 4, nullptr},
            {TRACK_5, -kPumpColSpacing * 0.5f - 4, nullptr},
            {TRACK_6, +kPumpColSpacing * 0.5f + 4, nullptr},
            {TRACK_7, +kPumpColSpacing * 1.5f + 4, nullptr},
            {TRACK_8, +kPumpColSpacing * 2.5f + 4, nullptr},
            {TRACK_9, +kPumpColSpacing * 3.5f + 4, nullptr},
            {TRACK_10, +kPumpColSpacing * 4.5f + 4, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 3, 2, 0, 4, Style::END_MAPPING},
        {6, 8, 7, 5, 9, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     2, 1, 3, 0, 4, 7, 6, 8, 5, 9},
    false,  // m_bCanUseBeginnerHelper
    true,   // m_bLockDifficulties
};

static const Style* game_pump_styles[] = {
    &kStylePumpSingle,     &kStylePumpVersus,
    &kStylePumpHalfDouble, &kStylePumpDouble,
    &kStylePumpCouple,     &kStylePumpCoupleEdit,
    &kStylePumpRoutine,    nullptr};

static const Game kGamePump = {
    "pump",             // m_szName
    game_pump_styles,   // m_apStyles
    false,              // m_bCountNotesSeparately
    false,              // m_PlayersHaveSeparateStyles
    {                   // m_InputScheme
     "pump",            // m_szName
     NUM_PUMP_BUTTONS,  // m_iButtonsPerController
     {
         // m_szButtonNames
         {"UpLeft", GAME_BUTTON_UP},
         {"UpRight", GAME_BUTTON_DOWN},
         {"Center", GAME_BUTTON_START},
         {"DownLeft", GAME_BUTTON_LEFT},
         {"DownRight", GAME_BUTTON_RIGHT},
     },
     &kAutoKeyMappingsPump},
    {
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
    },
    TNS_W1,  // m_mapW1To
    TNS_W2,  // m_mapW2To
    TNS_W3,  // m_mapW3To
    TNS_W4,  // m_mapW4To
    TNS_W5,  // m_mapW5To
};

/** Techno *******************************************************************/

// ThemeMetric<int>	TECHNO_COL_SPACING	("ColumnSpacing","Techno");
static const int kTechnoColSpacing = 56;
// ThemeMetric<int>	TECHNO_VERSUS_COL_SPACING
// ("ColumnSpacing","TechnoVersus");
static const Style kStyleTechnoSingle4 = {
    // STYLE_TECHNO_SINGLE4
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    false,                       // m_bUsedForHowToPlay
    "single4",                   // m_szName
    StepsType_techno_single4,    // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    4,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {0, 3, 2, 1, Style::END_MAPPING},
        {0, 3, 2, 1, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoSingle5 = {
    // STYLE_TECHNO_SINGLE5
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    false,                       // m_bUsedForHowToPlay
    "single5",                   // m_szName
    StepsType_techno_single5,    // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    5,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 2.0f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.0f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 2.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 2.0f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.0f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 2.0f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 1, 3, 2, 0, 4, Style::END_MAPPING},
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 1, 3, 2, 0, 4, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoSingle8 = {
    // STYLE_TECHNO_SINGLE8
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    true,                        // m_bUsedForHowToPlay
    "single8",                   // m_szName
    StepsType_techno_single8,    // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    8,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 6, 4, 3, 2, 5, Style::NO_MAPPING, 0, 7, Style::END_MAPPING},
        {1, 6, 4, 3, 2, 5, Style::NO_MAPPING, 0, 7, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoVersus4 = {
    // STYLE_TECHNO_VERSUS4
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    false,                         // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "versus4",                     // m_szName
    StepsType_techno_single4,      // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    4,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {0, 3, 2, 1, Style::END_MAPPING},
        {0, 3, 2, 1, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoVersus5 = {
    // STYLE_TECHNO_VERSUS5
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    false,                         // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "versus5",                     // m_szName
    StepsType_techno_single5,      // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    5,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 2.0f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.0f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 2.0f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 2.0f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_3, +kTechnoColSpacing * 0.0f, nullptr},
            {TRACK_4, +kTechnoColSpacing * 1.0f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 2.0f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 1, 3, 2, 0, 4, Style::END_MAPPING},
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 1, 3, 2, 0, 4, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoVersus8 = {
    // STYLE_TECHNO_VERSUS8
    true,                          // m_bUsedForGameplay
    false,                         // m_bUsedForEdit
    true,                          // m_bUsedForDemonstration
    false,                         // m_bUsedForHowToPlay
    "versus8",                     // m_szName
    StepsType_techno_single8,      // m_StepsType
    StyleType_TwoPlayersTwoSides,  // m_StyleType
    8,                             // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 6, 4, 3, 2, 5, Style::NO_MAPPING, 0, 7, Style::END_MAPPING},
        {1, 6, 4, 3, 2, 5, Style::NO_MAPPING, 0, 7, Style::END_MAPPING},
        //			{ 1, 6, 4, Style::NO_MAPPING, 2, 5,
        // Style::NO_MAPPING, 0, 7, Style::END_MAPPING },
        //			{ 1, 6, 4, Style::NO_MAPPING, 2, 5,
        // Style::NO_MAPPING, 0, 7, Style::END_MAPPING },
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoDouble4 = {
    // STYLE_TECHNO_DOUBLE4
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "double4",                    // m_szName
    StepsType_techno_double4,     // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    8,                            // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_5, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 3.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {0, 3, 2, 1, Style::END_MAPPING},
        {4, 7, 6, 5, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoDouble5 = {
    // STYLE_TECHNO_DOUBLE5
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "double5",                    // m_szName
    StepsType_techno_double5,     // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    10,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_5, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_9, +kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_10, +kTechnoColSpacing * 4.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_5, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_6, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_7, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_8, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_9, +kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_10, +kTechnoColSpacing * 4.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 1, 3, 2, 0, 4, Style::END_MAPPING},
        {Style::NO_MAPPING, Style::NO_MAPPING, Style::NO_MAPPING,
         Style::NO_MAPPING, 6, 8, 7, 5, 9, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style kStyleTechnoDouble8 = {
    // STYLE_TECHNO_DOUBLE8
    true,                         // m_bUsedForGameplay
    true,                         // m_bUsedForEdit
    false,                        // m_bUsedForDemonstration
    false,                        // m_bUsedForHowToPlay
    "double8",                    // m_szName
    StepsType_techno_double8,     // m_StepsType
    StyleType_OnePlayerTwoSides,  // m_StyleType
    16,                           // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kTechnoColSpacing * 7.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 6.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 5.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_5, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_6, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_7, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_8, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_9, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_10, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_11, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_12, +kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_13, +kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_14, +kTechnoColSpacing * 5.5f, nullptr},
            {TRACK_15, +kTechnoColSpacing * 6.5f, nullptr},
            {TRACK_16, +kTechnoColSpacing * 7.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kTechnoColSpacing * 7.5f, nullptr},
            {TRACK_2, -kTechnoColSpacing * 6.5f, nullptr},
            {TRACK_3, -kTechnoColSpacing * 5.5f, nullptr},
            {TRACK_4, -kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_5, -kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_6, -kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_7, -kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_8, -kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_9, +kTechnoColSpacing * 0.5f, nullptr},
            {TRACK_10, +kTechnoColSpacing * 1.5f, nullptr},
            {TRACK_11, +kTechnoColSpacing * 2.5f, nullptr},
            {TRACK_12, +kTechnoColSpacing * 3.5f, nullptr},
            {TRACK_13, +kTechnoColSpacing * 4.5f, nullptr},
            {TRACK_14, +kTechnoColSpacing * 5.5f, nullptr},
            {TRACK_15, +kTechnoColSpacing * 6.5f, nullptr},
            {TRACK_16, +kTechnoColSpacing * 7.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {1, 6, 4, 3, 2, 5, Style::NO_MAPPING, 0, 7, Style::END_MAPPING},
        {9, 14, 12, 11, 10, 13, Style::NO_MAPPING, 8, 15, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style* game_techno_styles[] = {
    &kStyleTechnoSingle4, &kStyleTechnoSingle5,
    &kStyleTechnoSingle8, &kStyleTechnoVersus4,
    &kStyleTechnoVersus5, &kStyleTechnoVersus8,
    &kStyleTechnoDouble4, &kStyleTechnoDouble5,
    &kStyleTechnoDouble8, nullptr};

static const AutoMappings kAutoKeyMappingsTechno = AutoMappings(
    "", "", "", AutoMappingEntry(0, KEY_Ca, TECHNO_BUTTON_LEFT, false),
    AutoMappingEntry(0, KEY_Cd, TECHNO_BUTTON_RIGHT, false),
    AutoMappingEntry(0, KEY_Cw, TECHNO_BUTTON_UP, false),
    AutoMappingEntry(0, KEY_Cx, TECHNO_BUTTON_DOWN, false),
    AutoMappingEntry(0, KEY_Cq, TECHNO_BUTTON_UPLEFT, false),
    AutoMappingEntry(0, KEY_Ce, TECHNO_BUTTON_UPRIGHT, false),
    AutoMappingEntry(0, KEY_Cs, TECHNO_BUTTON_CENTER, false),
    AutoMappingEntry(0, KEY_Cz, TECHNO_BUTTON_DOWNLEFT, false),
    AutoMappingEntry(0, KEY_Cc, TECHNO_BUTTON_DOWNRIGHT, false),
    AutoMappingEntry(0, KEY_KP_C4, TECHNO_BUTTON_LEFT, true),
    AutoMappingEntry(0, KEY_KP_C6, TECHNO_BUTTON_RIGHT, true),
    AutoMappingEntry(0, KEY_KP_C8, TECHNO_BUTTON_UP, true),
    AutoMappingEntry(0, KEY_KP_C2, TECHNO_BUTTON_DOWN, true),
    AutoMappingEntry(0, KEY_KP_C7, TECHNO_BUTTON_UPLEFT, true),
    AutoMappingEntry(0, KEY_KP_C9, TECHNO_BUTTON_UPRIGHT, true),
    AutoMappingEntry(0, KEY_KP_C5, TECHNO_BUTTON_CENTER, true),
    AutoMappingEntry(0, KEY_KP_C1, TECHNO_BUTTON_DOWNLEFT, true),
    AutoMappingEntry(0, KEY_KP_C3, TECHNO_BUTTON_DOWNRIGHT, true));

static const Game kGameTechno = {
    "techno",             // m_szName
    game_techno_styles,   // m_apStyles
    false,                // m_bCountNotesSeparately
    false,                // m_PlayersHaveSeparateStyles
    {                     // m_InputScheme
     "techno",            // m_szName
     NUM_TECHNO_BUTTONS,  // m_iButtonsPerController
     {
         // m_szButtonNames
         {"Left", GAME_BUTTON_LEFT},
         {"Right", GAME_BUTTON_RIGHT},
         {"Up", GAME_BUTTON_UP},
         {"Down", GAME_BUTTON_DOWN},
         {"UpLeft", GameButton_Invalid},
         {"UpRight", GameButton_Invalid},
         {"Center", GameButton_Invalid},
         {"DownLeft", GameButton_Invalid},
         {"DownRight", GameButton_Invalid},
     },
     &kAutoKeyMappingsTechno},
    {
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
    },
    TNS_W1,  // m_mapW1To
    TNS_W2,  // m_mapW2To
    TNS_W3,  // m_mapW3To
    TNS_W4,  // m_mapW4To
    TNS_W5,  // m_mapW5To
};

/** Lights *******************************************************************/

static const AutoMappings kAutoKeyMappingsLights = AutoMappings(
    "", "", "",
    AutoMappingEntry(0, KEY_Cq, LIGHTS_BUTTON_MARQUEE_UP_LEFT, false),
    AutoMappingEntry(0, KEY_Cw, LIGHTS_BUTTON_MARQUEE_UP_RIGHT, false),
    AutoMappingEntry(0, KEY_Ce, LIGHTS_BUTTON_MARQUEE_LR_LEFT, false),
    AutoMappingEntry(0, KEY_Cr, LIGHTS_BUTTON_MARQUEE_LR_RIGHT, false),
    AutoMappingEntry(0, KEY_Ct, LIGHTS_BUTTON_BUTTONS_LEFT, false),
    AutoMappingEntry(0, KEY_Cy, LIGHTS_BUTTON_BUTTONS_RIGHT, false),
    AutoMappingEntry(0, KEY_Cu, LIGHTS_BUTTON_BASS_LEFT, false),
    AutoMappingEntry(0, KEY_Ci, LIGHTS_BUTTON_BASS_RIGHT, false));

static const Style kStyleLightsCabinet = {
    // STYLE_LIGHTS_CABINET
    true,                        // m_bUsedForGameplay
    true,                        // m_bUsedForEdit
    false,                       // m_bUsedForDemonstration
    false,                       // m_bUsedForHowToPlay
    "cabinet",                   // m_szName
    StepsType_lights_cabinet,    // m_StepsType
    StyleType_OnePlayerOneSide,  // m_StyleType
    NUM_CabinetLight,            // m_iColsPerPlayer
    {
        // m_ColumnInfo[NUM_PLAYERS][MAX_COLS_PER_PLAYER];
        {
            // PLAYER_1
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
        {
            // PLAYER_2
            {TRACK_1, -kDanceColSpacing * 3.5f, nullptr},
            {TRACK_2, -kDanceColSpacing * 2.5f, nullptr},
            {TRACK_3, -kDanceColSpacing * 1.5f, nullptr},
            {TRACK_4, -kDanceColSpacing * 0.5f, nullptr},
            {TRACK_5, +kDanceColSpacing * 0.5f, nullptr},
            {TRACK_6, +kDanceColSpacing * 1.5f, nullptr},
            {TRACK_7, +kDanceColSpacing * 2.5f, nullptr},
            {TRACK_8, +kDanceColSpacing * 3.5f, nullptr},
        },
    },
    {
        // m_iInputColumn[NUM_GameController][NUM_GameButton]
        {0, 1, 2, 3, 4, 5, 6, 7, 8, Style::END_MAPPING},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, Style::END_MAPPING},
    },
    {// m_iColumnDrawOrder[MAX_COLS_PER_PLAYER];
     0, 1, 2, 3, 4, 5, 6, 7},
    false,  // m_bCanUseBeginnerHelper
    false,  // m_bLockDifficulties
};

static const Style* game_lights_styles[] = {&kStyleLightsCabinet, nullptr};

static const Game kGameLights = {
    "lights",             // m_szName
    game_lights_styles,   // m_apStyles
    false,                // m_bCountNotesSeparately
    false,                // m_PlayersHaveSeparateStyles
    {                     // m_InputScheme
     "lights",            // m_szName
     NUM_LIGHTS_BUTTONS,  // m_iButtonsPerController
     {
         // m_szButtonNames
         {"MarqueeUpLeft", GAME_BUTTON_LEFT},
         {"MarqueeUpRight", GAME_BUTTON_RIGHT},
         {"MarqueeLrLeft", GAME_BUTTON_UP},
         {"MarqueeLrRight", GAME_BUTTON_DOWN},
         {"ButtonsLeft", GameButton_Invalid},
         {"ButtonsRight", GameButton_Invalid},
         {"BassLeft", GameButton_Invalid},
         {"BassRight", GameButton_Invalid},
     },
     &kAutoKeyMappingsLights},
    {
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
        {GameButtonType_Step},
    },
    TNS_W1,  // m_mapW1To
    TNS_W2,  // m_mapW2To
    TNS_W3,  // m_mapW3To
    TNS_W4,  // m_mapW4To
    TNS_W5,  // m_mapW5To
};

static const Game* g_games[] = {
    &kGameDance,
    &kGamePump,
    &kGameTechno,
    &kGameLights,
};

GameManager::GameManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "GAMEMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

GameManager::~GameManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("GAMEMAN");
}

void GameManager::GetStylesForGame(
    const Game* game, std::vector<const Style*>& styles_add_to, bool editor) {
  for (int s = 0; game->styles_[s]; ++s) {
    const Style* style = game->styles_[s];
    if (!editor && !style->used_for_gameplay_) {
      continue;
    }
    if (editor && !style->used_for_edit_) {
      continue;
    }

    styles_add_to.push_back(style);
  }
}

const Game* GameManager::GetGameForStyle(const Style* style) {
  for (size_t g = 0; g < ARRAYLEN(g_games); ++g) {
    const Game* game = g_games[g];
    for (int s = 0; game->styles_[s]; ++s) {
      if (game->styles_[s] == style) {
        return game;
      }
    }
  }
  FAIL_M(style->name_);
}

const Style* GameManager::GetEditorStyleForStepsType(StepsType st) {
  for (size_t g = 0; g < ARRAYLEN(g_games); ++g) {
    const Game* game = g_games[g];
    for (int s = 0; game->styles_[s]; ++s) {
      const Style* style = game->styles_[s];
      if (style->steps_type_ == st && style->used_for_edit_) {
        return style;
      }
    }
  }

  ASSERT_M(
      0, ssprintf("The current game cannot use this Style with the editor!"));
  return nullptr;
}

void GameManager::GetStepsTypesForGame(
    const Game* game, std::vector<StepsType>& steps_type_add_to) {
  for (int i = 0; game->styles_[i]; ++i) {
    StepsType st = game->styles_[i]->steps_type_;
    ASSERT(st < NUM_StepsType);

    // Some Styles use the same StepsType (e.g. single and versus) so check
    // that we aren't doubling up.
    bool found = false;
    for (unsigned j = 0; j < steps_type_add_to.size(); j++) {
      if (st == steps_type_add_to[j]) {
        found = true;
        break;
      }
    }
    if (found) {
      continue;
    }

    steps_type_add_to.push_back(st);
  }
}

void GameManager::GetDemonstrationStylesForGame(
    const Game* game, std::vector<const Style*>& styles_out) {
  styles_out.clear();

  for (int s = 0; game->styles_[s]; ++s) {
    const Style* style = game->styles_[s];
    if (style->used_for_demonstration_) {
      styles_out.push_back(style);
    }
  }

  ASSERT(styles_out.size() > 0);  // this Game is missing a Style that can be
                                  // used with the demonstration
}

const Style* GameManager::GetHowToPlayStyleForGame(const Game* game) {
  for (int s = 0; game->styles_[s]; ++s) {
    const Style* style = game->styles_[s];
    if (style->used_for_how_to_play_) {
      return style;
    }
  }

  FAIL_M(ssprintf(
      "Game has no Style that can be used with HowToPlay: %s", game->name_));
}

void GameManager::GetCompatibleStyles(
    const Game* game, int num_players, std::vector<const Style*>& styles_out) {
  FOREACH_ENUM(StyleType, style_type) {
    int num_players_required;
    switch (style_type) {
      DEFAULT_FAIL(style_type);
      case StyleType_OnePlayerOneSide:
      case StyleType_OnePlayerTwoSides:
        num_players_required = 1;
        break;
      case StyleType_TwoPlayersTwoSides:
      case StyleType_TwoPlayersSharedSides:
        num_players_required = 2;
        break;
    }

    if (num_players != num_players_required) {
      continue;
    }

    for (int s = 0; game->styles_[s]; ++s) {
      const Style* style = game->styles_[s];
      if (style->style_type_ != style_type) {
        continue;
      }
      if (!style->used_for_gameplay_) {
        continue;
      }

      styles_out.push_back(style);
    }
  }
}

const Style* GameManager::GetFirstCompatibleStyle(
    const Game* game, int num_players, StepsType st) {
  std::vector<const Style*> styles;
  GetCompatibleStyles(game, num_players, styles);
  for (const Style* s : styles) {
    if (s->steps_type_ == st) {
      return s;
    }
  }
  return nullptr;
}

void GameManager::GetEnabledGames(std::vector<const Game*>& games_out) {
  for (size_t g = 0; g < ARRAYLEN(g_games); ++g) {
    const Game* game = g_games[g];
    if (IsGameEnabled(game)) {
      games_out.push_back(game);
    }
  }
}

const Game* GameManager::GetDefaultGame() {
  const Game* p_default = nullptr;
  if (p_default == nullptr) {
    for (size_t i = 0; p_default == nullptr && i < ARRAYLEN(g_games); ++i) {
      if (IsGameEnabled(g_games[i])) {
        p_default = g_games[i];
      }
    }

    if (p_default == nullptr) {
      RageException::Throw("No NoteSkins found");
    }
  }

  return p_default;
}

bool GameManager::IsGameEnabled(const Game* game) {
  return NOTESKIN->DoNoteSkinsExistForGame(game);
}

const StepsTypeInfo& GameManager::GetStepsTypeInfo(StepsType st) {
  ASSERT_M(
      st < NUM_StepsType,
      ssprintf("StepsType %d < NUM_StepsType (%d)", st, NUM_StepsType));
  return kStepsTypeInfos[st];
}

StepsType GameManager::StringToStepsType(std::string steps_type) {
  MakeLower(steps_type);

  for (int i = 0; i < NUM_StepsType; i++) {
    if (kStepsTypeInfos[i].name_ == steps_type) {
      return StepsType(i);
    }
  }

  return StepsType_Invalid;
}

std::string GameManager::StyleToLocalizedString(const Style* style) {
  std::string s = style->name_;
  s = Capitalize(s);
  if (THEME->HasString("Style", s)) {
    return THEME->GetString("Style", s);
  } else {
    return s;
  }
}

const Game* GameManager::StringToGame(std::string game) {
  for (size_t i = 0; i < ARRAYLEN(g_games); ++i) {
    if (!CompareNoCase(game, g_games[i]->name_)) {
      return g_games[i];
    }
  }

  return nullptr;
}

const Style* GameManager::GameAndStringToStyle(
    const Game* game, std::string s_style) {
  for (int s = 0; game->styles_[s]; ++s) {
    const Style* style = game->styles_[s];
    if (CompareNoCase(s_style, style->name_) == 0) {
      return style;
    }
  }

  return nullptr;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the GameManager. */
class LunaGameManager : public Luna<GameManager> {
 public:
  static int StepsTypeToLocalizedString(T* p, lua_State* L) {
    lua_pushstring(
        L, p->GetStepsTypeInfo(Enum::Check<StepsType>(L, 1))
               .GetLocalizedString()
               .c_str());
    return 1;
  }
  static int GetFirstStepsTypeForGame(T* p, lua_State* L) {
    Game* game = Luna<Game>::check(L, 1);

    std::vector<StepsType> add_to;
    p->GetStepsTypesForGame(game, add_to);
    ASSERT(!add_to.empty());
    StepsType st = add_to[0];
    LuaHelpers::Push(L, st);
    return 1;
  }
  static int IsGameEnabled(T* p, lua_State* L) {
    const Game* game = p->StringToGame(SArg(1));
    if (game) {
      lua_pushboolean(L, p->IsGameEnabled(game));
    } else {
      lua_pushnil(L);
    }

    return 1;
  }
  static int GetStylesForGame(T* p, lua_State* L) {
    std::string game_name = SArg(1);
    const Game* game = p->StringToGame(game_name);
    if (!game) {
      luaL_error(L, "GetStylesForGame: Invalid Game: '%s'", game_name.c_str());
    }
    std::vector<Style*> styles;
    lua_createtable(L, 0, 0);
    for (int s = 0; game->styles_[s]; ++s) {
      Style* style = const_cast<Style*>(game->styles_[s]);
      style->PushSelf(L);
      lua_rawseti(L, -2, s + 1);
    }
    return 1;
  }
  static int GetEnabledGames(T* p, lua_State* L) {
    std::vector<const Game*> games;
    p->GetEnabledGames(games);
    lua_createtable(L, games.size(), 0);
    for (size_t i = 0; i < games.size(); ++i) {
      lua_pushstring(L, games[i]->name_);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  static int SetGame(T* p, lua_State* L) {
    std::string game_name = SArg(1);
    const Game* game = p->StringToGame(game_name);
    if (!game) {
      luaL_error(L, "SetGame: Invalid Game: '%s'", game_name.c_str());
    }
    std::string theme;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      theme = SArg(2);
      if (!THEME->IsThemeSelectable(theme)) {
        luaL_error(L, "SetGame: Invalid Theme: '%s'", theme.c_str());
      }
    }
    GameLoop::ChangeGame(game_name, theme);
    return 0;
  }

  LunaGameManager() {
    ADD_METHOD(StepsTypeToLocalizedString);
    ADD_METHOD(GetFirstStepsTypeForGame);
    ADD_METHOD(IsGameEnabled);
    ADD_METHOD(GetStylesForGame);
    ADD_METHOD(GetEnabledGames);
    ADD_METHOD(SetGame);
  };
};

LUA_REGISTER_CLASS(GameManager)
// lua end

/*
 * (c) 2001-2006 Chris Danford, Glenn Maynard
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
