#ifndef INPUT_MAPPER_H
#define INPUT_MAPPER_H

#include <vector>

#include "GameInput.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"

// TODO(teejusb): Remove forward declaration.
struct Game;

// Five device inputs may map to one game input.
const int NUM_GAME_TO_DEVICE_SLOTS = 5;
const int NUM_SHOWN_GAME_TO_DEVICE_SLOTS = 3;
const int NUM_USER_GAME_TO_DEVICE_SLOTS = 2;
extern const RString DEVICE_INPUT_SEPARATOR;

struct AutoMappingEntry {
  AutoMappingEntry(
      int slot_index, DeviceButton device_button, GameButton game_button,
      bool second_controller)
      : slot_index_(slot_index),
        device_button_(device_button),
        game_button_(game_button),
        second_controller_(second_controller) {}
  AutoMappingEntry()
      : slot_index_(-1),
        device_button_(DeviceButton_Invalid),
        game_button_(GameButton_Invalid),
        second_controller_(false) {}
  bool IsEmpty() const {
    return device_button_ == DeviceButton_Invalid &&
           game_button_ == GameButton_Invalid;
  }

  int slot_index_;
  DeviceButton device_button_;
  // GameButton_Invalid means unmap this button
  GameButton game_button_;
  // If this is true, this is an auxilliary mapping assigned to the second
  // player. If two of the same device are found, and the device has secondary
  // entries, the later entries take precedence. This way, if a Pump pad is
  // found, it'll map P1 to the primary pad and P2 to the secondary pad. (We
  // can't tell if a slave pad is actually there.) Then, if a second primary
  // is found (DEVICE_PUMP2), 2P will be mapped to it.
  bool second_controller_;
};

struct AutoMappings {
  AutoMappings(
      RString game, RString driver_regex, RString controller_name,
      AutoMappingEntry im0 = AutoMappingEntry(),
      AutoMappingEntry im1 = AutoMappingEntry(),
      AutoMappingEntry im2 = AutoMappingEntry(),
      AutoMappingEntry im3 = AutoMappingEntry(),
      AutoMappingEntry im4 = AutoMappingEntry(),
      AutoMappingEntry im5 = AutoMappingEntry(),
      AutoMappingEntry im6 = AutoMappingEntry(),
      AutoMappingEntry im7 = AutoMappingEntry(),
      AutoMappingEntry im8 = AutoMappingEntry(),
      AutoMappingEntry im9 = AutoMappingEntry(),
      AutoMappingEntry im10 = AutoMappingEntry(),
      AutoMappingEntry im11 = AutoMappingEntry(),
      AutoMappingEntry im12 = AutoMappingEntry(),
      AutoMappingEntry im13 = AutoMappingEntry(),
      AutoMappingEntry im14 = AutoMappingEntry(),
      AutoMappingEntry im15 = AutoMappingEntry(),
      AutoMappingEntry im16 = AutoMappingEntry(),
      AutoMappingEntry im17 = AutoMappingEntry(),
      AutoMappingEntry im18 = AutoMappingEntry(),
      AutoMappingEntry im19 = AutoMappingEntry(),
      AutoMappingEntry im20 = AutoMappingEntry(),
      AutoMappingEntry im21 = AutoMappingEntry(),
      AutoMappingEntry im22 = AutoMappingEntry(),
      AutoMappingEntry im23 = AutoMappingEntry(),
      AutoMappingEntry im24 = AutoMappingEntry(),
      AutoMappingEntry im25 = AutoMappingEntry(),
      AutoMappingEntry im26 = AutoMappingEntry(),
      AutoMappingEntry im27 = AutoMappingEntry(),
      AutoMappingEntry im28 = AutoMappingEntry(),
      AutoMappingEntry im29 = AutoMappingEntry(),
      AutoMappingEntry im30 = AutoMappingEntry(),
      AutoMappingEntry im31 = AutoMappingEntry(),
      AutoMappingEntry im32 = AutoMappingEntry(),
      AutoMappingEntry im33 = AutoMappingEntry(),
      AutoMappingEntry im34 = AutoMappingEntry(),
      AutoMappingEntry im35 = AutoMappingEntry(),
      AutoMappingEntry im36 = AutoMappingEntry(),
      AutoMappingEntry im37 = AutoMappingEntry(),
      AutoMappingEntry im38 = AutoMappingEntry(),
      AutoMappingEntry im39 = AutoMappingEntry())
      : game_(game),
        driver_regex_(driver_regex),
        controller_name_(controller_name),
        auto_mapping_entries_() {
#define PUSH(im) \
  if (!im.IsEmpty()) auto_mapping_entries_.push_back(im);
    PUSH(im0);
    PUSH(im1);
    PUSH(im2);
    PUSH(im3);
    PUSH(im4);
    PUSH(im5);
    PUSH(im6);
    PUSH(im7);
    PUSH(im8);
    PUSH(im9);
    PUSH(im10);
    PUSH(im11);
    PUSH(im12);
    PUSH(im13);
    PUSH(im14);
    PUSH(im15);
    PUSH(im16);
    PUSH(im17);
    PUSH(im18);
    PUSH(im19);
    PUSH(im20);
    PUSH(im21);
    PUSH(im22);
    PUSH(im23);
    PUSH(im24);
    PUSH(im25);
    PUSH(im26);
    PUSH(im27);
    PUSH(im28);
    PUSH(im29);
    PUSH(im30);
    PUSH(im31);
    PUSH(im32);
    PUSH(im33);
    PUSH(im34);
    PUSH(im35);
    PUSH(im36);
    PUSH(im37);
    PUSH(im38);
    PUSH(im39);
#undef PUSH
  }

  // Strings used by automatic joystick mappings.
  RString game_;             // only used
  RString driver_regex_;     // reported by InputHandler
  RString controller_name_;  // the product name of the controller

  std::vector<AutoMappingEntry> auto_mapping_entries_;
};

class InputScheme {
 public:
  const char* name_;
  int buttons_per_controller_;
  struct GameButtonInfo {
    // The name used by the button graphics system
    // e.g. "left", "right", "middle C", "snare"
    const char* name_;
    GameButton secondary_menu_button_;
  };
  // Data for each Game-specific GameButton. This starts at GAME_BUTTON_NEXT.
  GameButtonInfo game_button_info_[NUM_GameButton];
  const AutoMappings* auto_mappings_;

  GameButton ButtonNameToIndex(const RString& button_name) const;
  GameButton GameButtonToMenuButton(GameButton game_button) const;
  void MenuButtonToGameInputs(
      GameButton menu_input, PlayerNumber pn,
      std::vector<GameInput>& game_input_out) const;
  void MenuButtonToGameButtons(
      GameButton menu_input, std::vector<GameButton>& game_buttons) const;
  const GameButtonInfo* GetGameButtonInfo(GameButton game_button) const;
  const char* GetGameButtonName(GameButton game_button) const;
};

// A special foreach loop to handle the various GameButtons.
#define FOREACH_GameButtonInScheme(s, var)                               \
  for (GameButton var = (GameButton)0; var < s->buttons_per_controller_; \
       enum_add<GameButton>(var, +1))

class InputMappings {
 public:
  // Only filled for automappings.
  RString device_regex_;
  RString description_;

  // map from a GameInput to multiple DeviceInputs
  DeviceInput game_input_to_device_input_[NUM_GameController][NUM_GameButton]
                                         [NUM_GAME_TO_DEVICE_SLOTS];

  void Clear();
  void Unmap(InputDevice input_device);
  void WriteMappings(const InputScheme* input_scheme, RString file_path);
  void ReadMappings(
      const InputScheme* input_scheme, RString file_path, bool is_auto_mapping);
  void SetInputMap(
      const DeviceInput& device_input, const GameInput& game_input,
      int slot_index);

  void ClearFromInputMap(const DeviceInput& device_input);
  bool ClearFromInputMap(const GameInput& game_input, int slot_index);
};

// Holds user-chosen input preferences and saves it between sessions.
class InputMapper {
 public:
  InputMapper();
  ~InputMapper();

  void SetInputScheme(const InputScheme* input_scheme);
  const InputScheme* GetInputScheme() const;
  void SetJoinControllers(PlayerNumber pn);

  void ReadMappingsFromDisk();
  void SaveMappingsToDisk();
  void ResetMappingsToDefault();
  void CheckButtonAndAddToReason(
      GameButton menu, std::vector<RString>& full_reason,
      const RString& sub_reason);
  void SanityCheckMappings(std::vector<RString>& reason);

  void ClearAllMappings();

  void SetInputMap(
      const DeviceInput& device_input, const GameInput& game_input,
      int slot_index);
  void ClearFromInputMap(const DeviceInput& device_input);
  bool ClearFromInputMap(const GameInput& game_input, int slot_index);

  void AddDefaultMappingsForCurrentGameIfUnmapped();
  void AutoMapJoysticksForCurrentGame();
  bool CheckForChangedInputDevicesAndRemap(RString& message_out);

  bool IsMapped(const DeviceInput& device_input) const;

  // Return true if there is a mapping from device to pad.
  bool DeviceToGame(
      const DeviceInput& device_input, GameInput& game_input) const;
  // Return true if there is a mapping from pad to device.
  bool GameToDevice(
      const GameInput& game_input, int slot_num,
      DeviceInput& device_input) const;

  GameButton GameButtonToMenuButton(GameButton gb) const;
  void MenuToGame(
      GameButton menu_input, PlayerNumber pn,
      std::vector<GameInput>& game_input_out) const;
  PlayerNumber ControllerToPlayerNumber(GameController controller) const;

  float GetSecsHeld(
      const GameInput& game_input,
      MultiPlayer multiplayer = MultiPlayer_Invalid) const;
  float GetSecsHeld(GameButton menu_input, PlayerNumber pn) const;

  bool IsBeingPressed(
      const GameInput& game_input,
      MultiPlayer multiplayer = MultiPlayer_Invalid,
      const DeviceInputList* button_state = nullptr) const;
  bool IsBeingPressed(GameButton menu_input, PlayerNumber pn) const;
  bool IsBeingPressed(
      const std::vector<GameInput>& game_input,
      MultiPlayer multiplayer = MultiPlayer_Invalid,
      const DeviceInputList* button_state = nullptr) const;

  void ResetKeyRepeat(const GameInput& game_input);
  void ResetKeyRepeat(GameButton menu_input, PlayerNumber pn);

  void RepeatStopKey(const GameInput& game_input);
  void RepeatStopKey(GameButton menu_input, PlayerNumber pn);

  float GetLevel(const GameInput& game_input) const;
  float GetLevel(GameButton menu_input, PlayerNumber pn) const;

  static InputDevice MultiPlayerToInputDevice(MultiPlayer multiplayer);
  static MultiPlayer InputDeviceToMultiPlayer(InputDevice input_device);

  void Unmap(InputDevice input_device);
  void ApplyMapping(
      const std::vector<AutoMappingEntry>& auto_mapping_entries,
      GameController game_controller, InputDevice input_device);

 protected:
  InputMappings mappings_;

  void UpdateTempDItoGI();
  const InputScheme* input_scheme_;

 private:
  InputMapper(const InputMapper& rhs);
  InputMapper& operator=(const InputMapper& rhs);
};

// Global and accessible from anywhere in our program.
extern InputMapper* INPUTMAPPER;

#endif  // INPUT_MAPPER_H

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
