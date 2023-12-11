#include "global.h"

#include "InputMapper.h"

#include <cstddef>
#include <vector>

#include "IniFile.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "arch/Dialog/Dialog.h"

#define AUTOMAPPINGS_DIR "/Data/AutoMappings/"

static Preference<RString> g_sLastSeenInputDevices("LastSeenInputDevices", "");
static Preference<bool> g_bAutoMapOnJoyChange("AutoMapOnJoyChange", true);

namespace {
// lookup for efficiency from a DeviceInput to a GameInput
// This is repopulated every time m_PItoDI changes by calling
// UpdateTempDItoPI().
std::map<DeviceInput, GameInput> g_tempDItoGI;

PlayerNumber g_JoinControllers;

};  // namespace

// Global and accessible from anywhere in our program.
InputMapper* INPUTMAPPER = nullptr;

InputMapper::InputMapper() {
  g_JoinControllers = PLAYER_INVALID;
  input_scheme_ = nullptr;
}

InputMapper::~InputMapper() {
  SaveMappingsToDisk();
  g_tempDItoGI.clear();
}

void InputMapper::ClearAllMappings() {
  mappings_.Clear();

  UpdateTempDItoGI();
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
    // Laptop keyboards.
    AutoMappingEntry(0, KEY_BACKSLASH, GAME_BUTTON_BACK, true),
    AutoMappingEntry(0, KEY_F1, GAME_BUTTON_COIN, false),
    AutoMappingEntry(0, KEY_SCRLLOCK, GAME_BUTTON_OPERATOR, false));

void InputMapper::AddDefaultMappingsForCurrentGameIfUnmapped() {
  // Clear default mappings.  Default mappings are in the third slot.
  FOREACH_ENUM(GameController, i)
  FOREACH_ENUM(GameButton, j)
  ClearFromInputMap(GameInput(i, j), 2);

  std::vector<AutoMappingEntry> auto_mapping_entries;
  auto_mapping_entries.reserve(32);

  for (const AutoMappingEntry& iter :
       g_DefaultKeyMappings.auto_mapping_entries_) {
    auto_mapping_entries.push_back(iter);
  }
  for (const AutoMappingEntry& iter :
       input_scheme_->auto_mappings_->auto_mapping_entries_) {
    auto_mapping_entries.push_back(iter);
  }

  // There may be duplicate GAME_BUTTON maps. Process the list backwards, so
  // game-specific mappings override g_DefaultKeyMappings.
  std::reverse(auto_mapping_entries.begin(), auto_mapping_entries.end());

  for (const AutoMappingEntry& m : auto_mapping_entries) {
    DeviceButton key = m.device_button_;
    DeviceInput device_input(DEVICE_KEYBOARD, key);
    GameInput game_input(
        m.second_controller_ ? GameController_2 : GameController_1,
        m.game_button_);
    // if this key isn't already being used by another user-made mapping.
    if (!IsMapped(device_input)) {
      if (!game_input.IsValid()) {
        ClearFromInputMap(device_input);
      } else {
        SetInputMap(device_input, game_input, 2);
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
        AutoMappingEntry(
            1, JOY_BUTTON_4, DANCE_BUTTON_UP, false),  // Y
                                                       // L shoulder
        AutoMappingEntry(0, JOY_BUTTON_7, DANCE_BUTTON_UPLEFT, false),
        // R shoulder
        AutoMappingEntry(0, JOY_BUTTON_8, DANCE_BUTTON_UPRIGHT, false),
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
        AutoMappingEntry(0, JOY_BUTTON_9, GAME_BUTTON_START, true),
        AutoMappingEntry(0, JOY_BUTTON_10, DANCE_BUTTON_UP, true),
        AutoMappingEntry(0, JOY_BUTTON_11, DANCE_BUTTON_DOWN, true),
        AutoMappingEntry(0, JOY_BUTTON_12, DANCE_BUTTON_LEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_13, DANCE_BUTTON_RIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_15, GAME_BUTTON_MENULEFT, true),
        AutoMappingEntry(0, JOY_BUTTON_16, GAME_BUTTON_MENURIGHT, true),
        AutoMappingEntry(0, JOY_BUTTON_17, GAME_BUTTON_START, false),
        AutoMappingEntry(0, JOY_BUTTON_18, DANCE_BUTTON_UP, false),
        AutoMappingEntry(0, JOY_BUTTON_19, DANCE_BUTTON_DOWN, false),
        AutoMappingEntry(0, JOY_BUTTON_20, DANCE_BUTTON_LEFT, false),
        AutoMappingEntry(0, JOY_BUTTON_21, DANCE_BUTTON_RIGHT, false),
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

void InputMapper::Unmap(InputDevice input_device) {
  mappings_.Unmap(input_device);

  UpdateTempDItoGI();
}

void InputMapper::ApplyMapping(
    const std::vector<AutoMappingEntry>& auto_mapping_entries,
    GameController game_controller, InputDevice input_device) {
  std::map<GameInput, int> mapped_buttons;
  for (const AutoMappingEntry& iter : auto_mapping_entries) {
    GameController mapped_game_controller = game_controller;
    if (iter.second_controller_) {
      mapped_game_controller = (GameController)(mapped_game_controller + 1);

      // If that pushed it over, then it's a second controller for a joystick
      // that's already a second controller, so we'll just ignore it. (This
      // can happen if eg. two primary Pump pads are connected.)
      if (mapped_game_controller >= NUM_GameController) {
        continue;
      }
    }

    DeviceInput device_input(input_device, iter.device_button_);
    GameInput game_input(mapped_game_controller, iter.game_button_);
    int slot = mapped_buttons[game_input];
    ++mapped_buttons[game_input];
    SetInputMap(device_input, game_input, slot);
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
    std::vector<RString> file_paths;
    GetDirListing(AUTOMAPPINGS_DIR "*.ini", file_paths, false, true);
    for (const RString& file_path : file_paths) {
      InputMappings mappings;
      mappings.ReadMappings(input_scheme_, file_path, true);

      AutoMappings mapping(
          input_scheme_->name_, mappings.device_regex_, mappings.description_);

      FOREACH_ENUM(GameController, game_controller) {
        FOREACH_ENUM(GameButton, game_button) {
          for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
            const DeviceInput& device_input =
                mappings.game_input_to_device_input_[game_controller]
                                                    [game_button][slot];
            if (!device_input.IsValid()) {
              continue;
            }

            AutoMappingEntry mapping_entry(
                slot, device_input.button, game_button,
                game_controller > GameController_1);
            mapping.auto_mapping_entries_.push_back(mapping_entry);
          }
        }
      }

      auto_mappings.push_back(mapping);
    }

    // hard-coded automaps
    for (unsigned j = 0; j < ARRAYLEN(g_AutoMappings); j++) {
      const AutoMappings& mapping = g_AutoMappings[j];
      if (mapping.game_.EqualsNoCase(input_scheme_->name_)) {
        auto_mappings.push_back(mapping);
      }
    }
  }

  // apply auto mappings
  int num_joysticks_mapped = 0;
  for (const InputDeviceInfo& device : devices) {
    InputDevice input_device = device.id;
    const RString& description = device.sDesc;
    for (const AutoMappings& mapping : auto_mappings) {
      Regex regex(mapping.driver_regex_);
      if (!regex.Compare(description)) {
        // Driver names don't match.
        continue;
      }

      // We have a mapping for this joystick
      GameController game_controller = (GameController)num_joysticks_mapped;
      if (game_controller >= NUM_GameController) {
        // Stop mapping. We already mapped one device for each game controller.
        break;
      }

      LOG->Info(
          "Applying default joystick mapping #%d for device '%s' (%s)",
          num_joysticks_mapped + 1, mapping.driver_regex_.c_str(),
          mapping.controller_name_.c_str());

      Unmap(input_device);
      ApplyMapping(
          mapping.auto_mapping_entries_, game_controller, input_device);

      num_joysticks_mapped++;
    }
  }
}

void InputMapper::SetInputScheme(const InputScheme* input_scheme) {
  input_scheme_ = input_scheme;

  ReadMappingsFromDisk();
}

const InputScheme* InputMapper::GetInputScheme() const { return input_scheme_; }

// This isn't used in any key names.
const RString DEVICE_INPUT_SEPARATOR = ":";

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
    GameButton menu, std::vector<RString>& full_reason,
    const RString& sub_reason) {
  std::vector<GameInput> inputs;
  bool exists = false;
  // NOTE(Kyz): Only player 1 is checked because the player 2 buttons are rarely
  // unmapped and do not exist on some keyboard models.
  GetInputScheme()->MenuButtonToGameInputs(menu, PLAYER_1, inputs);
  if (!inputs.empty()) {
    std::vector<DeviceInput> device_inputs;
    for (GameInput& input : inputs) {
      for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
        device_inputs.push_back(
            mappings_.game_input_to_device_input_[input.controller]
                                                 [input.button][slot]);
      }
    }
    for (DeviceInput& input : device_inputs) {
      if (!input.IsValid()) {
        continue;
      }
      int use_count = 0;
      FOREACH_ENUM(GameController, game_controller) {
        FOREACH_GameButtonInScheme(GetInputScheme(), game_button) {
          for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
            use_count +=
                (input ==
                 mappings_.game_input_to_device_input_[game_controller]
                                                      [game_button][slot]);
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

void InputMapper::SanityCheckMappings(std::vector<RString>& reason) {
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
bool InputMapper::CheckForChangedInputDevicesAndRemap(RString& message_out) {
  // Only check for changes in joysticks since that's all we know how to remap.

  // update last seen joysticks
  std::vector<InputDeviceInfo> devices;
  INPUTMAN->GetDevicesAndDescriptions(devices);

  // Strip non-joysticks.
  std::vector<RString> last_seen_joysticks;
  // Don't use "," since some vendors have a name like "company Ltd., etc".
  // For now, use a pipe character. -aj, fix from Mordae.
  split(g_sLastSeenInputDevices, "|", last_seen_joysticks);

  std::vector<RString> current;
  std::vector<RString> current_joysticks;
  for (int i = devices.size() - 1; i >= 0; i--) {
    current.push_back(devices[i].sDesc);
    if (IsJoystick(devices[i].id)) {
      current_joysticks.push_back(devices[i].sDesc);
    } else {
      std::vector<RString>::iterator iter = find(
          last_seen_joysticks.begin(), last_seen_joysticks.end(),
          devices[i].sDesc);
      if (iter != last_seen_joysticks.end()) {
        last_seen_joysticks.erase(iter);
      }
    }
  }

  bool joysticks_changed = current_joysticks != last_seen_joysticks;
  if (!joysticks_changed) {
    return false;
  }

  std::vector<RString> connects;
  std::vector<RString> disconnects;
  GetConnectsDisconnects(
      last_seen_joysticks, current_joysticks, disconnects, connects);

  message_out = RString();
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

  // NOTE(aj): see above comment about not using ",".
  g_sLastSeenInputDevices.Set(join("|", current));
  PREFSMAN->SavePrefsToDisk();

  return true;
}

void InputMapper::SetInputMap(
    const DeviceInput& device_input, const GameInput& game_input,
    int slot_index) {
  mappings_.SetInputMap(device_input, game_input, slot_index);

  UpdateTempDItoGI();
}

void InputMapper::ClearFromInputMap(const DeviceInput& device_input) {
  mappings_.ClearFromInputMap(device_input);

  UpdateTempDItoGI();
}

bool InputMapper::ClearFromInputMap(
    const GameInput& game_input, int slot_index) {
  if (!mappings_.ClearFromInputMap(game_input, slot_index)) {
    return false;
  }

  UpdateTempDItoGI();
  return true;
}

bool InputMapper::IsMapped(const DeviceInput& device_input) const {
  return g_tempDItoGI.find(device_input) != g_tempDItoGI.end();
}

void InputMapper::UpdateTempDItoGI() {
  // repopulate g_tempDItoGI
  g_tempDItoGI.clear();
  FOREACH_ENUM(GameController, game_controller) {
    FOREACH_ENUM(GameButton, game_button) {
      for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
        const DeviceInput& device_input =
            mappings_.game_input_to_device_input_[game_controller][game_button]
                                                 [slot];
        if (!device_input.IsValid()) {
          continue;
        }

        g_tempDItoGI[device_input] = GameInput(game_controller, game_button);
      }
    }
  }
}

// Return true if there is a mapping from device to pad.
bool InputMapper::DeviceToGame(
    const DeviceInput& device_input, GameInput& game_input) const {
  game_input = g_tempDItoGI[device_input];
  return game_input.controller != GameController_Invalid;
}

// Return true if there is a mapping from pad to device.
bool InputMapper::GameToDevice(
    const GameInput& game_input, int slot_num,
    DeviceInput& device_input) const {
  device_input =
      mappings_.game_input_to_device_input_[game_input.controller]
                                           [game_input.button][slot_num];
  return device_input.device != InputDevice_Invalid;
}

PlayerNumber InputMapper::ControllerToPlayerNumber(
    GameController game_controller) const {
  if (game_controller == GameController_Invalid) {
    return PLAYER_INVALID;
  } else if (g_JoinControllers != PLAYER_INVALID) {
    return g_JoinControllers;
  } else {
    return (PlayerNumber)game_controller;
  }
}

GameButton InputMapper::GameButtonToMenuButton(GameButton game_button) const {
  return input_scheme_->GameButtonToMenuButton(game_button);
}

// If set (not PLAYER_INVALID), inputs from both GameControllers will be mapped
// to the specified player. If PLAYER_INVALID, GameControllers will be mapped
// individually.
void InputMapper::SetJoinControllers(PlayerNumber pn) {
  g_JoinControllers = pn;
}

void InputMapper::MenuToGame(
    GameButton menu_input, PlayerNumber pn,
    std::vector<GameInput>& game_input_out) const {
  if (g_JoinControllers != PLAYER_INVALID) {
    pn = PLAYER_INVALID;
  }

  input_scheme_->MenuButtonToGameInputs(menu_input, pn, game_input_out);
}

bool InputMapper::IsBeingPressed(
    const GameInput& game_input, MultiPlayer multiplayer,
    const DeviceInputList* button_state) const {
  if (game_input.button == GameButton_Invalid) {
    return false;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; ++i) {
    DeviceInput device_input;

    if (GameToDevice(game_input, i, device_input)) {
      if (multiplayer != MultiPlayer_Invalid) {
        device_input.device = MultiPlayerToInputDevice(multiplayer);
      }
      if (INPUTFILTER->IsBeingPressed(device_input, button_state)) {
        return true;
      }
    }
  }

  return false;
}

bool InputMapper::IsBeingPressed(GameButton menu_input, PlayerNumber pn) const {
  if (menu_input == GameButton_Invalid) {
    return false;
  }
  std::vector<GameInput> game_inputs;
  MenuToGame(menu_input, pn, game_inputs);
  for (std::size_t i = 0; i < game_inputs.size(); i++) {
    if (IsBeingPressed(game_inputs[i])) {
      return true;
    }
  }

  return false;
}

bool InputMapper::IsBeingPressed(
    const std::vector<GameInput>& game_input, MultiPlayer multiplayer,
    const DeviceInputList* button_state) const {
  bool pressed = false;
  for (std::size_t i = 0; i < game_input.size(); ++i) {
    pressed |= IsBeingPressed(game_input[i], multiplayer, button_state);
  }
  return pressed;
}

void InputMapper::RepeatStopKey(const GameInput& game_input) {
  if (game_input.button == GameButton_Invalid) {
    return;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; ++i) {
    DeviceInput device_input;

    if (GameToDevice(game_input, i, device_input)) {
      INPUTFILTER->RepeatStopKey(device_input);
    }
  }
}

void InputMapper::RepeatStopKey(GameButton menu_input, PlayerNumber pn) {
  if (menu_input == GameButton_Invalid) {
    return;
  }
  std::vector<GameInput> game_input;
  MenuToGame(menu_input, pn, game_input);
  for (std::size_t i = 0; i < game_input.size(); i++) {
    RepeatStopKey(game_input[i]);
  }
}

float InputMapper::GetSecsHeld(
    const GameInput& game_input, MultiPlayer multiplayer) const {
  if (game_input.button == GameButton_Invalid) {
    return 0.f;
  }
  float max_seconds_held = 0;

  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; ++i) {
    DeviceInput DeviceI;
    if (GameToDevice(game_input, i, DeviceI)) {
      if (multiplayer != MultiPlayer_Invalid) {
        DeviceI.device = MultiPlayerToInputDevice(multiplayer);
      }
      max_seconds_held =
          std::max(max_seconds_held, INPUTFILTER->GetSecsHeld(DeviceI));
    }
  }

  return max_seconds_held;
}

float InputMapper::GetSecsHeld(GameButton menu_input, PlayerNumber pn) const {
  if (menu_input == GameButton_Invalid) {
    return 0.f;
  }
  float max_seconds_held = 0;

  std::vector<GameInput> game_inputs;
  MenuToGame(menu_input, pn, game_inputs);
  for (std::size_t i = 0; i < game_inputs.size(); ++i) {
    max_seconds_held = std::max(max_seconds_held, GetSecsHeld(game_inputs[i]));
  }

  return max_seconds_held;
}

void InputMapper::ResetKeyRepeat(const GameInput& game_input) {
  if (game_input.button == GameButton_Invalid) {
    return;
  }
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; i++) {
    DeviceInput device_input;
    if (GameToDevice(game_input, i, device_input)) {
      INPUTFILTER->ResetKeyRepeat(device_input);
    }
  }
}

void InputMapper::ResetKeyRepeat(GameButton menu_input, PlayerNumber pn) {
  if (menu_input == GameButton_Invalid) {
    return;
  }
  std::vector<GameInput> game_inputs;
  MenuToGame(menu_input, pn, game_inputs);
  for (std::size_t i = 0; i < game_inputs.size(); i++) {
    ResetKeyRepeat(game_inputs[i]);
  }
}

float InputMapper::GetLevel(const GameInput& game_input) const {
  if (game_input.button == GameButton_Invalid) {
    return 0.f;
  }
  float level = 0;
  for (int i = 0; i < NUM_GAME_TO_DEVICE_SLOTS; ++i) {
    DeviceInput device_input;
    if (GameToDevice(game_input, i, device_input)) {
      level = std::max(level, INPUTFILTER->GetLevel(device_input));
    }
  }
  return level;
}

float InputMapper::GetLevel(GameButton menu_input, PlayerNumber pn) const {
  if (menu_input == GameButton_Invalid) {
    return 0.f;
  }
  std::vector<GameInput> game_inputs;
  MenuToGame(menu_input, pn, game_inputs);

  float level = 0;
  for (std::size_t i = 0; i < game_inputs.size(); ++i) {
    level = std::max(level, GetLevel(game_inputs[i]));
  }

  return level;
}

InputDevice InputMapper::MultiPlayerToInputDevice(MultiPlayer multiplayer) {
  if (multiplayer == MultiPlayer_Invalid) {
    return InputDevice_Invalid;
  }
  return enum_add2(DEVICE_JOY1, multiplayer);
}

MultiPlayer InputMapper::InputDeviceToMultiPlayer(InputDevice input_device) {
  if (input_device == InputDevice_Invalid) {
    return MultiPlayer_Invalid;
  }
  return enum_add2(MultiPlayer_P1, input_device - DEVICE_JOY1);
}

GameButton InputScheme::ButtonNameToIndex(const RString& button_name) const {
  for (GameButton game_button = (GameButton)0;
       game_button < buttons_per_controller_;
       game_button = (GameButton)(game_button + 1)) {
    if (strcasecmp(GetGameButtonName(game_button), button_name) == 0) {
      return game_button;
    }
  }

  return GameButton_Invalid;
}

void InputScheme::MenuButtonToGameInputs(
    GameButton menu_input, PlayerNumber pn,
    std::vector<GameInput>& game_input_out) const {
  ASSERT(menu_input != GameButton_Invalid);

  std::vector<GameButton> game_buttons;
  MenuButtonToGameButtons(menu_input, game_buttons);
  for (const GameButton& game_button : game_buttons) {
    if (pn == PLAYER_INVALID) {
      game_input_out.push_back(GameInput(GameController_1, game_button));
      game_input_out.push_back(GameInput(GameController_2, game_button));
    } else {
      game_input_out.push_back(GameInput((GameController)pn, game_button));
    }
  }
}

void InputScheme::MenuButtonToGameButtons(
    GameButton menu_input, std::vector<GameButton>& game_buttons) const {
  ASSERT(menu_input != GameButton_Invalid);

  if (menu_input == GameButton_Invalid) {
    return;
  }

  FOREACH_ENUM(GameButton, game_button) {
    if (PREFSMAN->m_bOnlyDedicatedMenuButtons &&
        game_button >= GAME_BUTTON_NEXT) {
      break;
    }

    const GameButtonInfo* pGameButtonInfo = GetGameButtonInfo(game_button);
    if (pGameButtonInfo->secondary_menu_button_ != menu_input) {
      continue;
    }
    game_buttons.push_back(game_button);
  }
}

GameButton InputScheme::GameButtonToMenuButton(GameButton game_button) const {
  if (game_button == GameButton_Invalid) {
    return GameButton_Invalid;
  }
  if (game_button >= GAME_BUTTON_NEXT &&
      PREFSMAN->m_bOnlyDedicatedMenuButtons) {
    return GameButton_Invalid;
  }
  return GetGameButtonInfo(game_button)->secondary_menu_button_;
}

static const InputScheme::GameButtonInfo g_CommonGameButtonInfo[] = {
    {"MenuLeft", GAME_BUTTON_MENULEFT},
    {"MenuRight", GAME_BUTTON_MENURIGHT},
    {"MenuUp", GAME_BUTTON_MENUUP},
    {"MenuDown", GAME_BUTTON_MENUDOWN},
    {"Start", GAME_BUTTON_START},
    {"Select", GAME_BUTTON_SELECT},
    {"Back", GAME_BUTTON_BACK},
    {"Coin", GAME_BUTTON_COIN},
    {"Operator", GAME_BUTTON_OPERATOR},
    {"EffectUp", GAME_BUTTON_EFFECT_UP},
    {"EffectDown", GAME_BUTTON_EFFECT_DOWN},
};

const InputScheme::GameButtonInfo* InputScheme::GetGameButtonInfo(
    GameButton game_button) const {
  static_assert(GAME_BUTTON_NEXT == ARRAYLEN(g_CommonGameButtonInfo));
  if (game_button < GAME_BUTTON_NEXT) {
    return &g_CommonGameButtonInfo[game_button];
  } else {
    return &game_button_info_[game_button - GAME_BUTTON_NEXT];
  }
}

const char* InputScheme::GetGameButtonName(GameButton game_button) const {
  if (game_button == GameButton_Invalid) {
    return "";
  }
  return GetGameButtonInfo(game_button)->name_;
}

void InputMappings::Clear() {
  FOREACH_ENUM(GameController, game_controller)
  FOREACH_ENUM(GameButton, game_button)
  for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
    game_input_to_device_input_[game_controller][game_button][slot]
        .MakeInvalid();
  }
}

void InputMappings::Unmap(InputDevice input_device) {
  FOREACH_ENUM(GameController, game_controller) {
    FOREACH_ENUM(GameButton, game_button) {
      for (int slot = 0; slot < NUM_USER_GAME_TO_DEVICE_SLOTS; ++slot) {
        DeviceInput& device_input =
            game_input_to_device_input_[game_controller][game_button][slot];
        if (device_input.device == input_device) {
          device_input.MakeInvalid();
        }
      }
    }
  }
}

void InputMappings::ReadMappings(
    const InputScheme* input_scheme, RString file_path, bool is_auto_mapping) {
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
      const RString& name = i->first;
      RString value;
      i->second->GetValue(value);

      GameInput game_input;
      game_input.FromString(input_scheme, name);
      if (!game_input.IsValid()) {
        continue;
      }

      std::vector<RString> device_input_strings;
      split(value, DEVICE_INPUT_SEPARATOR, device_input_strings, false);

      for (unsigned j = 0; j < device_input_strings.size() &&
                           j < unsigned(NUM_GAME_TO_DEVICE_SLOTS);
           ++j) {
        DeviceInput device_input;
        device_input.FromString(device_input_strings[j]);
        if (device_input.IsValid()) {
          SetInputMap(device_input, game_input, j);
        }
      }
    }
  }
}

void InputMappings::WriteMappings(
    const InputScheme* input_scheme, RString file_path) {
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
  FOREACH_ENUM(GameController, game_controller) {
    FOREACH_GameButtonInScheme(input_scheme, game_button) {
      GameInput game_input(game_controller, game_button);
      RString name_str = game_input.ToString(input_scheme);

      std::vector<RString> values;
      for (int slot = 0; slot < NUM_USER_GAME_TO_DEVICE_SLOTS;
           ++slot) {  // don't save data from the last (keyboard automap) slot
        values.push_back(
            game_input_to_device_input_[game_controller][game_button][slot]
                .ToString());
      }

      while (values.size() && values.back() == "") {
        values.erase(values.begin() + values.size() - 1);
      }

      RString value_str = join(DEVICE_INPUT_SEPARATOR, values);

      key->AppendAttr(name_str, value_str);
    }
  }

  ini.WriteFile(file_path);
}

void InputMappings::SetInputMap(
    const DeviceInput& device_input, const GameInput& game_input,
    int slot_index) {
  // Remove the old input.
  ClearFromInputMap(device_input);
  ClearFromInputMap(game_input, slot_index);

  ASSERT_M(
      game_input.controller < NUM_GameController,
      ssprintf(
          "controller: %u >= %u", game_input.controller, NUM_GameController));
  ASSERT_M(
      game_input.button < NUM_GameButton,
      ssprintf("button: %u >= %u", game_input.button, NUM_GameButton));
  ASSERT_M(
      slot_index < NUM_GAME_TO_DEVICE_SLOTS,
      ssprintf("slot: %u >= %u", slot_index, NUM_GAME_TO_DEVICE_SLOTS));
  game_input_to_device_input_[game_input.controller][game_input.button]
                             [slot_index] = device_input;
}

void InputMappings::ClearFromInputMap(const DeviceInput& device_input) {
  // Search for where this device_input maps to.
  FOREACH_ENUM(GameController, game_controller) {
    FOREACH_ENUM(GameButton, game_button) {
      for (int slot = 0; slot < NUM_GAME_TO_DEVICE_SLOTS; ++slot) {
        if (game_input_to_device_input_[game_controller][game_button][slot] ==
            device_input) {
          game_input_to_device_input_[game_controller][game_button][slot]
              .MakeInvalid();
        }
      }
    }
  }
}

bool InputMappings::ClearFromInputMap(
    const GameInput& game_input, int slot_index) {
  if (!game_input.IsValid()) {
    return false;
  }

  DeviceInput& device_input =
      game_input_to_device_input_[game_input.controller][game_input.button]
                                 [slot_index];
  if (!device_input.IsValid()) {
    return false;
  }
  device_input.MakeInvalid();

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
