#include "InputMapper.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameInput.h"
#include "IniFile.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

#define AUTOMAPPINGS_DIR "/Data/AutoMappings/"

static Preference<std::string> g_sLastSeenInputDevices(
    "LastSeenInputDevices", "");
static Preference<bool> g_bAutoMapOnJoyChange("AutoMapOnJoyChange", true);

namespace {
// lookup for efficiency from a DeviceInput to a GameInput
// This is repopulated every time m_PItoDI changes by calling
// UpdateTempDItoPI().
std::map<DeviceInput, GameInput> g_tempDItoGI;

PlayerNumber g_JoinControllers;
};  // namespace

InputMapper* INPUTMAPPER =
    nullptr;  // global and accessible from anywhere in our program

InputMapper::InputMapper() {
  g_JoinControllers = PLAYER_INVALID;
  input_scheme_ = nullptr;
}

InputMapper::~InputMapper() {
  SaveMappingsToDisk();
  g_tempDItoGI.clear();
}

static const AutoMappings g_DefaultKeyMappings = AutoMappings(
    "", "", "", AutoMappingEntry(0, KEY_LEFT, GAME_BUTTON_MENULEFT, false),
    AutoMappingEntry(0, KEY_RIGHT, GAME_BUTTON_MENURIGHT, false),
    AutoMappingEntry(0, KEY_UP, GAME_BUTTON_MENUUP, false),
    AutoMappingEntry(0, KEY_DOWN, GAME_BUTTON_MENUDOWN, false),
    AutoMappingEntry(0, KEY_ENTER, GAME_BUTTON_START, false),
    AutoMappingEntry(0, KEY_SLASH, GAME_BUTTON_SELECT, false),
    AutoMappingEntry(0, KEY_ESC, GAME_BUTTON_BACK, false),
    AutoMappingEntry(0, KEY_KP_C4, GAME_BUTTON_MENULEFT, true),
    AutoMappingEntry(0, KEY_KP_C6, GAME_BUTTON_MENURIGHT, true),
    AutoMappingEntry(0, KEY_KP_C8, GAME_BUTTON_MENUUP, true),
    AutoMappingEntry(0, KEY_KP_C2, GAME_BUTTON_MENUDOWN, true),
    AutoMappingEntry(0, KEY_KP_ENTER, GAME_BUTTON_START, true),
    AutoMappingEntry(0, KEY_KP_C0, GAME_BUTTON_SELECT, true),
    AutoMappingEntry(
        0, KEY_BACKSLASH, GAME_BUTTON_BACK, true),  // laptop keyboards.
    AutoMappingEntry(0, KEY_F1, GAME_BUTTON_COIN, false),
    AutoMappingEntry(0, KEY_SCRLLOCK, GAME_BUTTON_OPERATOR, false));

void InputMapper::AddDefaultMappingsForCurrentGameIfUnmapped() {
  // Clear default mappings.  Default mappings are in the third slot.
  FOREACH_ENUM(GameController, i)
  FOREACH_ENUM(GameButton, j)
  ClearFromInputMap(GameInput(i, j), 2);

  std::vector<AutoMappingEntry> maps;
  maps.reserve(32);

  for (const AutoMappingEntry& iter : g_DefaultKeyMappings.maps_) {
    maps.push_back(iter);
  }
  for (const AutoMappingEntry& iter : input_scheme_->auto_mappings_->maps_) {
    maps.push_back(iter);
  }

  /* There may be duplicate GAME_BUTTON maps.  Process the list backwards,
   * so game-specific mappings override g_DefaultKeyMappings. */
  std::reverse(maps.begin(), maps.end());

  for (const AutoMappingEntry& m : maps) {
    DeviceButton key = m.device_button_;
    DeviceInput device_i(DEVICE_KEYBOARD, key);
    GameInput game_i(
        m.second_controller_ ? GameController_2 : GameController_1, m.gb_);
    if (!IsMapped(device_i))  // if this key isn't already being used by another
                              // user-made mapping
    {
      if (!game_i.IsValid()) {
        ClearFromInputMap(device_i);
      } else {
        SetInputMap(device_i, game_i, 2);
      }
    }
  }
}

static const AutoMappings g_AutoMappings[] = {
    AutoMappings(
        "dance", "GIC USB Joystick", "Boom USB convertor (black/gray)",
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_12, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "4 axis 16 button joystick", "EMS USB2",
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance",
        "GamePad Pro USB ",  // yes, there is a space at the end
        "GamePad Pro USB",
        AutoMappingEntry(0, JOY_LEFT, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_5, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "SideWinder Game Pad USB version 1.0",
        "SideWinder Game Pad USB",
        AutoMappingEntry(0, JOY_LEFT, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_5, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "4 axis 12 button joystick with hat switch", "Super Joy Box 5",
        AutoMappingEntry(0, JOY_LEFT, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, false)),
    /*	AutoMappings(

                    "dance",
                    "MP-8866 Dual USB Joypad",
                    "Super Dual Box (from Lik-Sang, 2004)",
                    //NEEDS_DANCE_PAD_MAPPING_CODE,
                    AutoMappingEntry( 0, JOY_LEFT,
       DANCE_BUTTON_LEFT,      false ), AutoMappingEntry( 0, JOY_RIGHT,
       DANCE_BUTTON_RIGHT,     false ), AutoMappingEntry( 0, JOY_UP,
       DANCE_BUTTON_UP,	       false ), AutoMappingEntry( 0, JOY_DOWN,
       DANCE_BUTTON_DOWN,      false ), AutoMappingEntry( 1, JOY_BUTTON_4,
       DANCE_BUTTON_LEFT,      false ), AutoMappingEntry( 1, JOY_BUTTON_2,
       DANCE_BUTTON_RIGHT,     false ), AutoMappingEntry( 1, JOY_BUTTON_1,
       DANCE_BUTTON_UP,	       false ), AutoMappingEntry( 1, JOY_BUTTON_3,
       DANCE_BUTTON_DOWN,      false ), AutoMappingEntry( 0, JOY_BUTTON_5,
       DANCE_BUTTON_UPLEFT,    false ), AutoMappingEntry( 0, JOY_BUTTON_6,
       DANCE_BUTTON_UPRIGHT,   false ), AutoMappingEntry( 1, JOY_BUTTON_7,
       DANCE_BUTTON_UPLEFT,    false ), AutoMappingEntry( 1, JOY_BUTTON_8,
       DANCE_BUTTON_UPRIGHT,   false ), AutoMappingEntry( 0, JOY_BUTTON_10,
       GAME_BUTTON_BACK,       false ), AutoMappingEntry( 0, JOY_BUTTON_9,
       GAME_BUTTON_START,      false ),
               ),
    */
    AutoMappings(
        "dance", "MP-8866 Dual USB Joypad",
        "Super Dual Box (from DDRGame.com, Feb 2008)",
        // NEEDS_DANCE_PAD_MAPPING_CODE,
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_1, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_4, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_5,    DANCE_BUTTON_DOWNLEFT,  false
        // ), AutoMappingEntry{ 0, JOY_BUTTON_6,    DANCE_BUTTON_DOWNRIGHT,
        // false ),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "NTPAD", "NTPAD",
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_5, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "Psx Gamepad", "PSXPAD",
        AutoMappingEntry(0, JOY_LEFT, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_5, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_8, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_6, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "XBOX Gamepad Plugin V0.01", "X-Box gamepad",
        AutoMappingEntry(0, JOY_LEFT, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_DOWN, false),   // A
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),  // B
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_LEFT, false),   // X
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_UP, false),     // Y
        AutoMappingEntry(
            0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),  // L shoulder
        AutoMappingEntry(
            0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),  // R shoulder
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_BACK, false)),
    AutoMappings(
        "dance",
        "0b43:0003",  // The EMS USB2 doesn't provide a model string, so Linux
                      // just gives us the VendorID and ModelID in hex.
        "EMS USB2",
        // Player 1.
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_4, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(1, JOY_BUTTON_2, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_SELECT, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_5, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_6, GAME_BUTTON_COIN, false),
        // Player 2.
        AutoMappingEntry(0, JOY_BUTTON_32, DANCE_BUTTON_LEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_30, DANCE_BUTTON_RIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_29, DANCE_BUTTON_UP, true),
        AutoMappingEntry(0, JOY_BUTTON_31, DANCE_BUTTON_DOWN, true),
        AutoMappingEntry(1, JOY_BUTTON_20, DANCE_BUTTON_LEFT, true),
        AutoMappingEntry(1, JOY_BUTTON_18, DANCE_BUTTON_RIGHT, true),
        AutoMappingEntry(1, JOY_BUTTON_17, DANCE_BUTTON_UP, true),
        AutoMappingEntry(1, JOY_BUTTON_19, DANCE_BUTTON_DOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_23, DANCE_BUTTON_UPRIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_24, DANCE_BUTTON_UPLEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_25, GAME_BUTTON_SELECT, true),
        AutoMappingEntry(0, JOY_BUTTON_26, GAME_BUTTON_START, true),
        AutoMappingEntry(0, JOY_BUTTON_21, GAME_BUTTON_BACK, true),
        AutoMappingEntry(0, JOY_BUTTON_22, GAME_BUTTON_COIN, true)),
    AutoMappings(
        "dance",
        "Dance ",                     // Notice extra space at end
        "LevelSix USB Pad (DDR638)",  // "DDR638" is the model number of the pad
        AutoMappingEntry(0, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_4, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "SmartJoy PLUS Adapter", "SmartJoy PLUS Adapter",
        AutoMappingEntry(0, JOY_LEFT, /* dpad L */ DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, /* dpad R */ DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_UP, /* dpad U */ DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_DOWN, /* dpad D */ DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(
            1, JOY_BUTTON_4, /* Square */ DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(
            1, JOY_BUTTON_2, /* Circle */ DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(1, JOY_BUTTON_1, /* Tri */ DANCE_BUTTON_UP, false),
        AutoMappingEntry(1, JOY_BUTTON_3, /* X */ DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, /* L1 */ DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, /* R1 */ DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(
            0, JOY_BUTTON_10, /* Select */ GAME_BUTTON_BACK, false),
        AutoMappingEntry(
            0, JOY_BUTTON_9, /* Start	*/ GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_5, /* R1 */ GAME_BUTTON_SELECT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, /* R2 */ GAME_BUTTON_COIN, false)),
    AutoMappings(
        "dance",
        "RedOctane USB Pad|XBOX DDR",  // "RedOctane USB Pad" is Ignition 3s and
                                       // newer Afterburners.  "XBOX DDR" is
                                       // older Afterburners.
        "RedOctane Ignition 3 or Afterburner",
        AutoMappingEntry(
            0, JOY_BUTTON_1, /* dpad L */ DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(
            0, JOY_BUTTON_4, /* dpad R */ DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, /* dpad U */ DANCE_BUTTON_UP, false),
        AutoMappingEntry(
            0, JOY_BUTTON_2, /* dpad D */ DANCE_BUTTON_DOWN, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_5,    /* Tri */ GAME_BUTTON_BACK,
        // false ), AutoMappingEntry{ 0, JOY_BUTTON_6,    /* Square */
        // GAME_BUTTON_BACK,       false ), AutoMappingEntry{ 1, JOY_BUTTON_7,
        // /* X */	       GAME_BUTTON_START,      false ),
        AutoMappingEntry(0, JOY_BUTTON_8, /* O */ GAME_BUTTON_START, false),
        AutoMappingEntry(
            1, JOY_BUTTON_9, /* Start	*/ GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_10, /* Sel */ GAME_BUTTON_BACK, false)),
    AutoMappings(
        "dance", "Joypad to USB converter", "EMS Trio Linker",
        AutoMappingEntry(
            0, JOY_BUTTON_16, /* dpad L */ DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(
            0, JOY_BUTTON_14, /* dpad R */ DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_13, /* dpad U */ DANCE_BUTTON_UP, false),
        AutoMappingEntry(
            0, JOY_BUTTON_15, /* dpad D */ DANCE_BUTTON_DOWN, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_5,    /* Tri */ GAME_BUTTON_BACK,
        // false ), AutoMappingEntry{ 0, JOY_BUTTON_6,    /* Square */
        // GAME_BUTTON_BACK,       false ), AutoMappingEntry{ 1, JOY_BUTTON_7,
        // /* X */	       GAME_BUTTON_START,      false ),
        AutoMappingEntry(0, JOY_BUTTON_2, /* O */ GAME_BUTTON_START, false),
        AutoMappingEntry(
            1, JOY_BUTTON_10, /* Start	*/ GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_9, /* Sel */ GAME_BUTTON_BACK, false)),
    AutoMappings(
        "dance", "Positive Gaming Impact USB pad",
        "Positive Gaming Impact USB pad",
        AutoMappingEntry(
            0, JOY_BUTTON_1, /* dpad L */ DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(
            0, JOY_BUTTON_4, /* dpad R */ DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, /* dpad U */ DANCE_BUTTON_UP, false),
        AutoMappingEntry(
            0, JOY_BUTTON_2, /* dpad D */ DANCE_BUTTON_DOWN, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_5,    /* Tri */ GAME_BUTTON_BACK,
        // false ), AutoMappingEntry{ 0, JOY_BUTTON_6,    /* Square */
        // GAME_BUTTON_BACK,       false ),
        AutoMappingEntry(0, JOY_BUTTON_7, /* X */ DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, /* O */ DANCE_BUTTON_UPRIGHT, false),
        AutoMappingEntry(
            1, JOY_BUTTON_9, /* Start	*/ GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_10, /* Sel */ GAME_BUTTON_BACK, false)),
    AutoMappings(
        "dance", "USB Dance Pad", "DDRGame Energy Dance Pad",
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_UPRIGHT, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_1,    DANCE_BUTTON_DOWNLEFT,  false
        // ), AutoMappingEntry{ 0, JOY_BUTTON_4,    DANCE_BUTTON_DOWNRIGHT,
        // false ),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "USB DancePad", "D-Force Dance Pad",
        AutoMappingEntry(0, JOY_BUTTON_1, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_4, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_1,    DANCE_BUTTON_DOWNLEFT,  false
        // ), AutoMappingEntry{ 0, JOY_BUTTON_4,    DANCE_BUTTON_DOWNRIGHT,
        // false ),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "Dual USB Vibration Joystick",
        "PC Multi Hub Double Power Box 4",
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_15, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_16, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_14, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_UPRIGHT, false),
        // AutoMappingEntry{ 0, JOY_BUTTON_1,    DANCE_BUTTON_DOWNLEFT,  false
        // ), AutoMappingEntry{ 0, JOY_BUTTON_4,    DANCE_BUTTON_DOWNRIGHT,
        // false ),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "Controller \\(Harmonix Drum Kit for Xbox 360\\)",
        "Rock Band drum controller (Xbox 360, Windows driver)",
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_UP, false),  // blue drum
        AutoMappingEntry(
            0, JOY_BUTTON_4, DANCE_BUTTON_DOWN, false),  // yellow drum
        AutoMappingEntry(
            0, JOY_BUTTON_2, DANCE_BUTTON_LEFT, false),  // red drum
        AutoMappingEntry(
            0, JOY_BUTTON_1, DANCE_BUTTON_RIGHT, false),  // green	drum
        AutoMappingEntry(
            0, JOY_HAT_LEFT, GAME_BUTTON_MENULEFT, false),  // d-pad	left
        AutoMappingEntry(
            0, JOY_HAT_RIGHT, GAME_BUTTON_MENURIGHT, false),  // d-pad	right
        AutoMappingEntry(
            0, JOY_HAT_UP, GAME_BUTTON_MENUUP, false),  // d-pad	up
        AutoMappingEntry(
            0, JOY_HAT_DOWN, GAME_BUTTON_MENUDOWN, false),  // d-pad	down
        AutoMappingEntry(
            0, JOY_BUTTON_8, GAME_BUTTON_START, false),  // start	button
        AutoMappingEntry(
            0, JOY_BUTTON_7, GAME_BUTTON_BACK, false)  // back button
        ),
    AutoMappings(
        "dance", "Honey Bee Xbox360 dancepad", "Honey Bee Xbox360 dancepad",
        AutoMappingEntry(0, JOY_BUTTON_17, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_18, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_19, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_20, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_27, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_28, GAME_BUTTON_START, false)),
    AutoMappings(
        "dance", "Raw Thrills I/O", "Raw Thrills I/O",
        AutoMappingEntry(0, JOY_BUTTON_1, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_2, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_3, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_4, DANCE_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_5, GAME_BUTTON_MENULEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, GAME_BUTTON_MENUDOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, GAME_BUTTON_MENUUP, false),
        AutoMappingEntry(0, JOY_BUTTON_8, GAME_BUTTON_MENURIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_10, DANCE_BUTTON_LEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_11, DANCE_BUTTON_DOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_12, DANCE_BUTTON_UP, true),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_RIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_14, GAME_BUTTON_MENULEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_15, GAME_BUTTON_MENUDOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_16, GAME_BUTTON_MENUUP, true),
        AutoMappingEntry(0, JOY_BUTTON_17, GAME_BUTTON_MENURIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_18, GAME_BUTTON_START, true),
        AutoMappingEntry(0, JOY_BUTTON_19, GAME_BUTTON_OPERATOR, false),
        AutoMappingEntry(0, JOY_BUTTON_20, GAME_BUTTON_SELECT, false),
        AutoMappingEntry(0, JOY_BUTTON_21, GAME_BUTTON_COIN, false),
        AutoMappingEntry(0, JOY_BUTTON_22, GAME_BUTTON_COIN, false),
        AutoMappingEntry(0, JOY_BUTTON_23, GAME_BUTTON_EFFECT_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_24, GAME_BUTTON_EFFECT_UP, false)),
    AutoMappings(
        "dance", "ddrio", "ddrio",
        AutoMappingEntry(0, JOY_BUTTON_1, GAME_BUTTON_MENUUP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, GAME_BUTTON_MENUDOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_3, GAME_BUTTON_MENUUP, true),
        AutoMappingEntry(0, JOY_BUTTON_4, GAME_BUTTON_MENUDOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_5, GAME_BUTTON_OPERATOR, false),
        AutoMappingEntry(0, JOY_BUTTON_6, GAME_BUTTON_COIN, false),
        AutoMappingEntry(0, JOY_BUTTON_7, GAME_BUTTON_OPERATOR, false),
        // AutoMappingEntry(0, JOY_BUTTON_8, GameButton_Invalid, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, true),
        AutoMappingEntry(0, JOY_BUTTON_10, DANCE_BUTTON_UP, true),
        AutoMappingEntry(0, JOY_BUTTON_11, DANCE_BUTTON_DOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_12, DANCE_BUTTON_LEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_RIGHT, true),
        // AutoMappingEntry(0, JOY_BUTTON_14, GameButton_Invalid, true),
        AutoMappingEntry(0, JOY_BUTTON_15, GAME_BUTTON_MENULEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_16, GAME_BUTTON_MENURIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_17, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_18, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_19, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_20, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_21, DANCE_BUTTON_RIGHT, false),
        // AutoMappingEntry(0, JOY_BUTTON_22, GameButton_Invalid, false),
        AutoMappingEntry(0, JOY_BUTTON_23, GAME_BUTTON_MENULEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_24, GAME_BUTTON_MENURIGHT, false)),
    AutoMappings(
        "pump", "Pump USB", "Pump USB pad",
        AutoMappingEntry(0, JOY_BUTTON_1, PUMP_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_2, PUMP_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, PUMP_BUTTON_CENTER, false),
        AutoMappingEntry(0, JOY_BUTTON_4, PUMP_BUTTON_DOWNLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_5, PUMP_BUTTON_DOWNRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_7, PUMP_BUTTON_UPLEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_8, PUMP_BUTTON_UPRIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_9, PUMP_BUTTON_CENTER, true),
        AutoMappingEntry(0, JOY_BUTTON_10, PUMP_BUTTON_DOWNLEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_11, PUMP_BUTTON_DOWNRIGHT, true)),
    AutoMappings(
        "pump",
        "GamePad Pro USB ",  // yes, there is a space at the end
        "GamePad Pro USB",
        AutoMappingEntry(0, JOY_BUTTON_5, PUMP_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, PUMP_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_7, PUMP_BUTTON_DOWNLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, PUMP_BUTTON_DOWNRIGHT, false),
        AutoMappingEntry(0, JOY_LEFT, GAME_BUTTON_MENULEFT, false),
        AutoMappingEntry(0, JOY_RIGHT, GAME_BUTTON_MENURIGHT, false),
        AutoMappingEntry(0, JOY_UP, GAME_BUTTON_MENUUP, false),
        AutoMappingEntry(0, JOY_DOWN, GAME_BUTTON_MENUDOWN, false),
        AutoMappingEntry(1, JOY_BUTTON_1, PUMP_BUTTON_CENTER, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
    AutoMappings(
        "pump", "Controller \\(Harmonix Drum Kit for Xbox 360\\)",
        "Rock Band drum controller (Xbox 360, Windows driver)",
        AutoMappingEntry(
            0, JOY_BUTTON_5, PUMP_BUTTON_CENTER, false),  // bass pedal
        AutoMappingEntry(
            0, JOY_BUTTON_3, PUMP_BUTTON_UPRIGHT, false),  // blue drum
        AutoMappingEntry(
            0, JOY_BUTTON_4, PUMP_BUTTON_UPLEFT, false),  // yellow drum
        AutoMappingEntry(
            0, JOY_BUTTON_2, PUMP_BUTTON_DOWNLEFT, false),  // red drum
        AutoMappingEntry(
            0, JOY_BUTTON_1, PUMP_BUTTON_DOWNRIGHT, false),  // green	drum
        AutoMappingEntry(
            0, JOY_HAT_LEFT, GAME_BUTTON_MENULEFT, false),  // d-pad	left
        AutoMappingEntry(
            0, JOY_HAT_RIGHT, GAME_BUTTON_MENURIGHT, false),  // d-pad	right
        AutoMappingEntry(
            0, JOY_HAT_UP, GAME_BUTTON_MENUUP, false),  // d-pad	up
        AutoMappingEntry(
            0, JOY_HAT_DOWN, GAME_BUTTON_MENUDOWN, false),  // d-pad	down
        AutoMappingEntry(
            0, JOY_BUTTON_8, GAME_BUTTON_START, false),  // start	button
        AutoMappingEntry(
            0, JOY_BUTTON_7, GAME_BUTTON_BACK, false)  // back button
        ),
    AutoMappings(
        "para", "ParaParaParadise Controller", "ParaParaParadise Controller",
        AutoMappingEntry(0, JOY_BUTTON_5, PARA_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_4, PARA_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_3, PARA_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, PARA_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_1, PARA_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_11, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_12, GAME_BUTTON_MENULEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_MENURIGHT, false)),
    AutoMappings(
        "techno",
        "Dance ",                     // Notice the extra space at end
        "LevelSix USB Pad (DDR638)",  // "DDR638" is the model number of the pad
        AutoMappingEntry(0, JOY_BUTTON_1, TECHNO_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_2, TECHNO_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_3, TECHNO_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_4, TECHNO_BUTTON_RIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_5, TECHNO_BUTTON_DOWNRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_6, TECHNO_BUTTON_DOWNLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_7, TECHNO_BUTTON_UPRIGHT, false),
        AutoMappingEntry(0, JOY_BUTTON_8, TECHNO_BUTTON_UPLEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_BACK, false),
        AutoMappingEntry(0, JOY_BUTTON_10, GAME_BUTTON_START, false)),
};

void InputMapper::Unmap(InputDevice id) {
  mappings_.Unmap(id);

  UpdateTempDItoGI();
}

void InputMapper::ApplyMapping(
    const std::vector<AutoMappingEntry>& mmaps, GameController gc,
    InputDevice id) {
  std::map<GameInput, int> mapped_buttons;
  for (const AutoMappingEntry& iter : mmaps) {
    GameController map_gc = gc;
    if (iter.second_controller_) {
      map_gc = (GameController)(map_gc + 1);

      /* If that pushed it over, then it's a second controller for a joystick
       * that's already a second controller, so we'll just ignore it.  (This
       * can happen if eg. two primary Pump pads are connected.) */
      if (map_gc >= NUM_GameController) {
        continue;
      }
    }

    DeviceInput di(id, iter.device_button_);
    GameInput gi(map_gc, iter.gb_);
    int slot = mapped_buttons[gi];
    ++mapped_buttons[gi];
    SetInputMap(di, gi, slot);  // maps[k].iSlotIndex );
  }
}

void InputMapper::AutoMapJoysticksForCurrentGame() {
  std::vector<InputDeviceInfo> devices;
  INPUTMAN->GetDevicesAndDescriptions(devices);

  // fill vector with all auto mappings
  std::vector<AutoMappings> auto_mappings;
  {
    // file automaps - Add these first so that they can match before the
    // hard-coded mappings
    std::vector<std::string> vs;
    GetDirListing(AUTOMAPPINGS_DIR "*.ini", vs, false, true);
    for (const std::string& file_path : vs) {
      InputMappings km;
      km.ReadMappings(input_scheme_, file_path, true);

      AutoMappings mapping(
          input_scheme_->name_, km.device_regex_, km.description_);

      FOREACH_ENUM(GameController, c) {
        FOREACH_ENUM(GameButton, b) {
          for (int s = 0; s < NUM_GAME_TO_DEVICE_SLOTS; s++) {
            const DeviceInput& device_i = km.gi_to_di_[c][b][s];
            if (!device_i.IsValid()) {
              continue;
            }

            AutoMappingEntry im(s, device_i.button_, b, c > GameController_1);
            mapping.maps_.push_back(im);
          }
        }
      }

      auto_mappings.push_back(mapping);
    }

    // hard-coded automaps
    for (unsigned j = 0; j < ARRAYLEN(g_AutoMappings); j++) {
      const AutoMappings& mapping = g_AutoMappings[j];
      if (EqualsNoCase(mapping.game_, input_scheme_->name_)) {
        auto_mappings.push_back(mapping);
      }
    }
  }

  // apply auto mappings
  int num_joysticks_mapped = 0;
  for (const InputDeviceInfo& device : devices) {
    InputDevice id = device.id_;
    const std::string& description = device.desc_;
    for (const AutoMappings& mapping : auto_mappings) {
      Regex regex(mapping.driver_regex_);
      if (!regex.Compare(description)) {
        continue;  // driver names don't match
      }

      // We have a mapping for this joystick
      GameController gc = (GameController)num_joysticks_mapped;
      if (gc >= NUM_GameController) {
        break;  // stop mapping.  We already mapped one device for each game
                // controller.
      }

      LOG->Info(
          "Applying default joystick mapping #%d for device '%s' (%s)",
          num_joysticks_mapped + 1, mapping.driver_regex_.c_str(),
          mapping.controller_name_.c_str());

      Unmap(id);
      ApplyMapping(mapping.maps_, gc, id);

      num_joysticks_mapped++;
    }
  }
}

void InputMapper::SetInputScheme(const InputScheme* input_scheme) {
  input_scheme_ = input_scheme;

  ReadMappingsFromDisk();
}

const InputScheme* InputMapper::GetInputScheme() const { return input_scheme_; }

const std::string DEVICE_INPUT_SEPARATOR =
    ":";  // this isn't used in any key names

void InputMapper::ReadMappingsFromDisk() {
  mappings_.ReadMappings(input_scheme_, SpecialFiles::KEYMAPS_PATH, false);
  UpdateTempDItoGI();

  AddDefaultMappingsForCurrentGameIfUnmapped();
}

void InputMapper::SaveMappingsToDisk() {
  mappings_.WriteMappings(input_scheme_, SpecialFiles::KEYMAPS_PATH);
}

void InputMapper::ResetMappingsToDefault() {
  mappings_.Clear();
  UpdateTempDItoGI();
  AddDefaultMappingsForCurrentGameIfUnmapped();
}

void InputMapper::CheckButtonAndAddToReason(
    GameButton menu, std::vector<std::string>& full_reason,
    const std::string& sub_reason) {
  std::vector<GameInput> inputs;
  bool exists = false;
  // Only player 1 is checked because the player 2 buttons are rarely
  // unmapped and do not exist on some keyboard models. -Kyz
  GetInputScheme()->MenuButtonToGameInputs(menu, PLAYER_1, inputs);
  if (!inputs.empty()) {
    std::vector<DeviceInput> device_inputs;
    for (GameInput& inp : inputs) {
      for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
        device_inputs.push_back(
            mappings_.gi_to_di_[inp.controller_][inp.button_][slot]);
      }
    }
    for (DeviceInput& inp : device_inputs) {
      if (!inp.IsValid()) {
        continue;
      }
      int use_count = 0;
      FOREACH_ENUM(GameController, cont) {
        FOREACH_GameButtonInScheme(GetInputScheme(), gb) {
          for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
            use_count += (inp == mappings_.gi_to_di_[cont][gb][slot]);
          }
        }
      }
      // If the device input is used more than once, it's a case where a
      // default mapped key was remapped to some other game button. -Kyz
      if (use_count == 1) {
        exists = true;
      }
    }
  }
  if (!exists) {
    full_reason.push_back(sub_reason);
  }
}

void InputMapper::SanityCheckMappings(std::vector<std::string>& reason) {
  // This is just to check whether the current mapping has the minimum
  // necessary to navigate the menus so the user can reach the config screen.
  // For this purpose, only the following keys are needed:
  // MenuLeft, MenuRight, Start, Operator
  // The InputScheme handles OnlyDedicatedMenuButtons logic. -Kyz
  CheckButtonAndAddToReason(GAME_BUTTON_MENULEFT, reason, "MenuLeftMissing");
  CheckButtonAndAddToReason(GAME_BUTTON_MENURIGHT, reason, "MenuRightMissing");
  CheckButtonAndAddToReason(GAME_BUTTON_START, reason, "StartMissing");
  CheckButtonAndAddToReason(GAME_BUTTON_OPERATOR, reason, "OperatorMissing");
}

static LocalizedString CONNECTED("InputMapper", "Connected");
static LocalizedString DISCONNECTED("InputMapper", "Disconnected");
static LocalizedString AUTOMAPPING_ALL_JOYSTICKS(
    "InputMapper", "Auto-mapping all joysticks.");
bool InputMapper::CheckForChangedInputDevicesAndRemap(
    std::string& message_out) {
  // Only check for changes in joysticks since that's all we know how to remap.

  // update last seen joysticks
  std::vector<InputDeviceInfo> devices;
  INPUTMAN->GetDevicesAndDescriptions(devices);

  // Strip non-joysticks.
  std::vector<std::string> last_seen_joysticks;
  // Don't use "," since some vendors have a name like "company Ltd., etc".
  // For now, use a pipe character. -aj, fix from Mordae.
  split(g_sLastSeenInputDevices, "|", last_seen_joysticks);

  std::vector<std::string> current;
  std::vector<std::string> current_joysticks;
  for (int i = devices.size() - 1; i >= 0; i--) {
    current.push_back(devices[i].desc_);
    if (IsJoystick(devices[i].id_)) {
      current_joysticks.push_back(devices[i].desc_);
    } else {
      std::vector<std::string>::iterator iter = find(
          last_seen_joysticks.begin(), last_seen_joysticks.end(),
          devices[i].desc_);
      if (iter != last_seen_joysticks.end()) {
        last_seen_joysticks.erase(iter);
      }
    }
  }

  bool joysticks_changed = current_joysticks != last_seen_joysticks;
  if (!joysticks_changed) {
    return false;
  }

  std::vector<std::string> connects, disconnects;
  GetConnectsDisconnects(
      last_seen_joysticks, current_joysticks, disconnects, connects);

  message_out = std::string();
  if (!connects.empty()) {
    message_out += CONNECTED.GetValue() + ": " + join("\n", connects) + "\n";
  }
  if (!disconnects.empty()) {
    message_out +=
        DISCONNECTED.GetValue() + ": " + join("\n", disconnects) + "\n";
  }

  if (g_bAutoMapOnJoyChange) {
    message_out += AUTOMAPPING_ALL_JOYSTICKS.GetValue();
    AutoMapJoysticksForCurrentGame();
    SaveMappingsToDisk();
    MESSAGEMAN->Broadcast(Message_AutoJoyMappingApplied);
  }

  LOG->Info("%s", message_out.c_str());

  // see above comment about not using ",". -aj
  g_sLastSeenInputDevices.Set(join("|", current));
  PREFSMAN->SavePrefsToDisk();

  return true;
}

void InputMapper::SetInputMap(
    const DeviceInput& device_i, const GameInput& game_i, int slot_index) {
  mappings_.SetInputMap(device_i, game_i, slot_index);

  UpdateTempDItoGI();
}

void InputMapper::ClearFromInputMap(const DeviceInput& device_i) {
  mappings_.ClearFromInputMap(device_i);

  UpdateTempDItoGI();
}

bool InputMapper::ClearFromInputMap(const GameInput& game_i, int slot_index) {
  if (!mappings_.ClearFromInputMap(game_i, slot_index)) {
    return false;
  }

  UpdateTempDItoGI();
  return true;
}

bool InputMapper::IsMapped(const DeviceInput& device_i) const {
  return g_tempDItoGI.find(device_i) != g_tempDItoGI.end();
}

void InputMapper::UpdateTempDItoGI() {
  // repopulate g_tempDItoGI
  g_tempDItoGI.clear();
  FOREACH_ENUM(GameController, n) {
    FOREACH_ENUM(GameButton, b) {
      for (int s = 0; s < NUM_GAME_TO_DEVICE_SLOTS; s++) {
        const DeviceInput& device_i = mappings_.gi_to_di_[n][b][s];
        if (!device_i.IsValid()) {
          continue;
        }

        g_tempDItoGI[device_i] = GameInput(n, b);
      }
    }
  }
}

// return true if there is a mapping from device to pad
bool InputMapper::DeviceToGame(
    const DeviceInput& device_i, GameInput& game_i) const {
  game_i = g_tempDItoGI[device_i];
  return game_i.controller_ != GameController_Invalid;
}

// return true if there is a mapping from pad to device
bool InputMapper::GameToDevice(
    const GameInput& game_i, int slot_num, DeviceInput& device_i) const {
  device_i = mappings_.gi_to_di_[game_i.controller_][game_i.button_][slot_num];
  return device_i.device_ != InputDevice_Invalid;
}

PlayerNumber InputMapper::ControllerToPlayerNumber(
    GameController controller) const {
  if (controller == GameController_Invalid) {
    return PLAYER_INVALID;
  } else if (g_JoinControllers != PLAYER_INVALID) {
    return g_JoinControllers;
  } else {
    return (PlayerNumber)controller;
  }
}

GameButton InputMapper::GameButtonToMenuButton(GameButton gb) const {
  return input_scheme_->GameButtonToMenuButton(gb);
}

/* If set (not PLAYER_INVALID), inputs from both GameControllers will be mapped
 * to the specified player. If PLAYER_INVALID, GameControllers will be mapped
 * individually. */
void InputMapper::SetJoinControllers(PlayerNumber pn) {
  g_JoinControllers = pn;
}

void InputMapper::MenuToGame(
    GameButton menu_i, PlayerNumber pn,
    std::vector<GameInput>& game_iout) const {
  if (g_JoinControllers != PLAYER_INVALID) {
    pn = PLAYER_INVALID;
  }

  input_scheme_->MenuButtonToGameInputs(menu_i, pn, game_iout);
}

bool InputMapper::IsBeingPressed(
    const GameInput& game_i, const DeviceInputList* button_state) const {
  if (game_i.button_ == GameButton_Invalid) {
    return false;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_i;

    if (GameToDevice(game_i, i, device_i)) {
      if (INPUTFILTER->IsBeingPressed(device_i, button_state)) {
        return true;
      }
    }
  }

  return false;
}

bool InputMapper::IsBeingPressed(GameButton menu_i, PlayerNumber pn) const {
  if (menu_i == GameButton_Invalid) {
    return false;
  }
  std::vector<GameInput> game_i;
  MenuToGame(menu_i, pn, game_i);
  for (size_t i = 0; i < game_i.size(); i++) {
    if (IsBeingPressed(game_i[i])) {
      return true;
    }
  }

  return false;
}

bool InputMapper::IsBeingPressed(
    const std::vector<GameInput>& game_i,
    const DeviceInputList* button_state) const {
  bool pressed = false;
  for (size_t i = 0; i < game_i.size(); ++i) {
    pressed |= IsBeingPressed(game_i[i], button_state);
  }
  return pressed;
}

void InputMapper::RepeatStopKey(const GameInput& game_i) {
  if (game_i.button_ == GameButton_Invalid) {
    return;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_i;

    if (GameToDevice(game_i, i, device_i)) {
      INPUTFILTER->RepeatStopKey(device_i);
    }
  }
}

void InputMapper::RepeatStopKey(GameButton menu_i, PlayerNumber pn) {
  if (menu_i == GameButton_Invalid) {
    return;
  }
  std::vector<GameInput> game_i;
  MenuToGame(menu_i, pn, game_i);
  for (size_t i = 0; i < game_i.size(); i++) {
    RepeatStopKey(game_i[i]);
  }
}

float InputMapper::GetSecsHeld(const GameInput& game_i) const {
  if (game_i.button_ == GameButton_Invalid) {
    return 0.f;
  }
  float max_secs_held = 0;

  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_i;
    if (GameToDevice(game_i, i, device_i)) {
      max_secs_held =
          std::max(max_secs_held, INPUTFILTER->GetSecsHeld(device_i));
    }
  }

  return max_secs_held;
}

void InputMapper::ResetKeyRepeat(const GameInput& game_i) {
  if (game_i.button_ == GameButton_Invalid) {
    return;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_i;
    if (GameToDevice(game_i, i, device_i)) {
      INPUTFILTER->ResetKeyRepeat(device_i);
    }
  }
}

void InputMapper::ResetKeyRepeat(GameButton menu_i, PlayerNumber pn) {
  if (menu_i == GameButton_Invalid) {
    return;
  }
  std::vector<GameInput> game_i;
  MenuToGame(menu_i, pn, game_i);
  for (size_t i = 0; i < game_i.size(); i++) {
    ResetKeyRepeat(game_i[i]);
  }
}

float InputMapper::GetLevel(const GameInput& game_i) const {
  if (game_i.button_ == GameButton_Invalid) {
    return 0.f;
  }
  float level = 0;
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_i;

    if (GameToDevice(game_i, i, device_i)) {
      level = std::max(level, INPUTFILTER->GetLevel(device_i));
    }
  }
  return level;
}

float InputMapper::GetLevel(GameButton menu_i, PlayerNumber pn) const {
  if (menu_i == GameButton_Invalid) {
    return 0.f;
  }
  std::vector<GameInput> game_i;
  MenuToGame(menu_i, pn, game_i);

  float level = 0;
  for (size_t i = 0; i < game_i.size(); i++) {
    level = std::max(level, GetLevel(game_i[i]));
  }

  return level;
}

GameButton InputScheme::ButtonNameToIndex(
    const std::string& button_name) const {
  for (GameButton gb = (GameButton)0; gb < buttons_per_controller_;
       gb = (GameButton)(gb + 1)) {
    if (strcasecmp(GetGameButtonName(gb), button_name.c_str()) == 0) {
      return gb;
    }
  }

  return GameButton_Invalid;
}

void InputScheme::MenuButtonToGameInputs(
    GameButton menu_i, PlayerNumber pn,
    std::vector<GameInput>& game_iout) const {
  ASSERT(menu_i != GameButton_Invalid);

  std::vector<GameButton> game_buttons;
  MenuButtonToGameButtons(menu_i, game_buttons);
  for (const GameButton& gb : game_buttons) {
    if (pn == PLAYER_INVALID) {
      game_iout.push_back(GameInput(GameController_1, gb));
      game_iout.push_back(GameInput(GameController_2, gb));
    } else {
      game_iout.push_back(GameInput((GameController)pn, gb));
    }
  }
}

void InputScheme::MenuButtonToGameButtons(
    GameButton menu_i, std::vector<GameButton>& game_buttons) const {
  ASSERT(menu_i != GameButton_Invalid);

  if (menu_i == GameButton_Invalid) {
    return;
  }

  FOREACH_ENUM(GameButton, gb) {
    if (PREFSMAN->only_dedicated_menu_buttons_ && gb >= GAME_BUTTON_NEXT) {
      break;
    }

    const GameButtonInfo* game_button_info = GetGameButtonInfo(gb);
    if (game_button_info->secondary_menu_button_ != menu_i) {
      continue;
    }
    game_buttons.push_back(gb);
  }
}

GameButton InputScheme::GameButtonToMenuButton(GameButton gb) const {
  if (gb == GameButton_Invalid) {
    return GameButton_Invalid;
  }
  if (gb >= GAME_BUTTON_NEXT && PREFSMAN->only_dedicated_menu_buttons_) {
    return GameButton_Invalid;
  }
  return GetGameButtonInfo(gb)->secondary_menu_button_;
}

static const InputScheme::GameButtonInfo g_CommonGameButtonInfo[] = {
    {"MenuLeft", GAME_BUTTON_MENULEFT},
    {"MenuRight", GAME_BUTTON_MENURIGHT},
    {"MenuUp", GAME_BUTTON_MENUUP},
    {"MenuDown", GAME_BUTTON_MENUDOWN},
    {"Start", GAME_BUTTON_START},
    {"Select", GAME_BUTTON_SELECT},
    {"Back", GAME_BUTTON_BACK},
    {"Restart", GAME_BUTTON_RESTART},
    {"Coin", GAME_BUTTON_COIN},
    {"Operator", GAME_BUTTON_OPERATOR},
    {"EffectUp", GAME_BUTTON_EFFECT_UP},
    {"EffectDown", GAME_BUTTON_EFFECT_DOWN},
};

const InputScheme::GameButtonInfo* InputScheme::GetGameButtonInfo(
    GameButton gb) const {
  static_assert(GAME_BUTTON_NEXT == ARRAYLEN(g_CommonGameButtonInfo));
  if (gb < GAME_BUTTON_NEXT) {
    return &g_CommonGameButtonInfo[gb];
  } else {
    return &game_button_info_[gb - GAME_BUTTON_NEXT];
  }
}

const char* InputScheme::GetGameButtonName(GameButton gb) const {
  if (gb == GameButton_Invalid) {
    return "";
  }
  return GetGameButtonInfo(gb)->name_;
}

void InputMappings::Clear() {
  FOREACH_ENUM(GameController, i)
  FOREACH_ENUM(GameButton, j)
  for (int k = 0; k < NUM_GAME_TO_DEVICE_SLOTS; k++) {
    gi_to_di_[i][j][k].MakeInvalid();
  }
}

void InputMappings::Unmap(InputDevice id) {
  FOREACH_ENUM(GameController, i) {
    FOREACH_ENUM(GameButton, j) {
      for (int k = 0; k < NUM_USER_GAME_TO_DEVICE_SLOTS; k++) {
        DeviceInput& di = gi_to_di_[i][j][k];
        if (di.device_ == id) {
          di.MakeInvalid();
        }
      }
    }
  }
}

void InputMappings::ReadMappings(
    const InputScheme* input_scheme, std::string file_path,
    bool is_auto_mapping) {
  Clear();

  IniFile ini;
  if (!ini.ReadFile(file_path)) {
    LOG->Trace(
        "Couldn't open mapping file \"%s\": %s.",
        SpecialFiles::KEYMAPS_PATH.c_str(), ini.GetError().c_str());
  }

  if (is_auto_mapping) {
    if (!ini.GetValue("AutoMapping", "DeviceRegex", device_regex_)) {
      Dialog::OK("Missing AutoMapping::DeviceRegex in '%s'", file_path.c_str());
    }

    if (!ini.GetValue("AutoMapping", "Description", description_)) {
      Dialog::OK("Missing AutoMapping::Description in '%s'", file_path.c_str());
    }
  }

  const XNode* key = ini.GetChild(input_scheme->name_);

  if (key) {
    FOREACH_CONST_Attr(key, i) {
      const std::string& name = i->first;
      std::string value;
      i->second->GetValue(value);

      GameInput game_i;
      game_i.FromString(input_scheme, name);
      if (!game_i.IsValid()) {
        continue;
      }

      std::vector<std::string> device_input_strings;
      split(value, DEVICE_INPUT_SEPARATOR, device_input_strings, false);

      for (unsigned j = 0; j < device_input_strings.size() &&
                           j < unsigned(NUM_GAME_TO_DEVICE_SLOTS);
           j++) {
        DeviceInput device_i;
        device_i.FromString(device_input_strings[j]);
        if (device_i.IsValid()) {
          SetInputMap(device_i, game_i, j);
        }
      }
    }
  }
}

void InputMappings::WriteMappings(
    const InputScheme* input_scheme, std::string file_path) {
  IniFile ini;
  ini.ReadFile(file_path);

  // erase the key so that we overwrite everything for this game
  ini.DeleteKey(input_scheme->name_);

  XNode* key = ini.GetChild(input_scheme->name_);
  if (key != nullptr) {
    ini.RemoveChild(key);
  }
  key = ini.AppendChild(input_scheme->name_);

  // iterate over our input map and write all mappings to the ini file
  FOREACH_ENUM(GameController, i) {
    FOREACH_GameButtonInScheme(input_scheme, j) {
      GameInput game_i(i, j);
      std::string name_string = game_i.ToString(input_scheme);

      std::vector<std::string> values;
      for (int slot = 0; slot < NUM_USER_GAME_TO_DEVICE_SLOTS;
           ++slot) {  // don't save data from the last (keyboard automap) slot
        values.push_back(gi_to_di_[i][j][slot].ToString());
      }

      while (values.size() && values.back() == "") {
        values.erase(values.begin() + values.size() - 1);
      }

      std::string value_string = join(DEVICE_INPUT_SEPARATOR, values);

      key->AppendAttr(name_string, value_string);
    }
  }

  ini.WriteFile(file_path);
}

void InputMappings::SetInputMap(
    const DeviceInput& device_i, const GameInput& game_i, int slot_index) {
  // remove the old input
  ClearFromInputMap(device_i);
  ClearFromInputMap(game_i, slot_index);

  ASSERT_M(
      game_i.controller_ < NUM_GameController,
      ssprintf("controller: %u >= %u", game_i.controller_, NUM_GameController));
  ASSERT_M(
      game_i.button_ < NUM_GameButton,
      ssprintf("button: %u >= %u", game_i.button_, NUM_GameButton));
  ASSERT_M(
      slot_index < NUM_GAME_TO_DEVICE_SLOTS,
      ssprintf("slot: %u >= %u", slot_index, NUM_GAME_TO_DEVICE_SLOTS));
  gi_to_di_[game_i.controller_][game_i.button_][slot_index] = device_i;
}

void InputMappings::ClearFromInputMap(const DeviceInput& device_i) {
  // search for where this DeviceI maps to

  FOREACH_ENUM(GameController, p) {
    FOREACH_ENUM(GameButton, b) {
      for (int s = 0; s < NUM_GAME_TO_DEVICE_SLOTS; s++) {
        if (gi_to_di_[p][b][s] == device_i) {
          gi_to_di_[p][b][s].MakeInvalid();
        }
      }
    }
  }
}

bool InputMappings::ClearFromInputMap(const GameInput& game_i, int slot_index) {
  if (!game_i.IsValid()) {
    return false;
  }

  DeviceInput& di = gi_to_di_[game_i.controller_][game_i.button_][slot_index];
  if (!di.IsValid()) {
    return false;
  }
  di.MakeInvalid();

  return true;
}

/*
 * (c) 2001-2003 Chris Danford
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
