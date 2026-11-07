#ifndef INPUT_MAPPER_H
#define INPUT_MAPPER_H

#include <string>
#include <vector>

#include "GameInput.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"

struct Game;

const int NUM_GAME_TO_DEVICE_SLOTS =
    5;  // five device inputs may map to one game input
const int NUM_SHOWN_GAME_TO_DEVICE_SLOTS = 3;
const int NUM_USER_GAME_TO_DEVICE_SLOTS = 2;
extern const std::string DEVICE_INPUT_SEPARATOR;

struct AutoMappingEntry {
  AutoMappingEntry(int, DeviceButton db, GameButton gb, bool b)
      : device_button_(db), gb_(gb), second_controller_(b) {}
  AutoMappingEntry()
      : device_button_(DeviceButton_Invalid),
        gb_(GameButton_Invalid),
        second_controller_(false) {}
  bool IsEmpty() const {
    return device_button_ == DeviceButton_Invalid && gb_ == GameButton_Invalid;
  }

  DeviceButton device_button_;
  GameButton gb_;  // GameButton_Invalid means unmap this button
  /* If this is true, this is an auxilliary mapping assigned to the second
   * player. If two of the same device are found, and the device has secondary
   * entries, the later entries take precedence. This way, if a Pump pad is
   * found, it'll map P1 to the primary pad and P2 to the secondary pad. (We
   * can't tell if a slave pad is actually there.) Then, if a second primary
   * is found (DEVICE_PUMP2), 2P will be mapped to it. */
  bool second_controller_;
};

struct AutoMappings {
  AutoMappings(
      std::string s1, std::string s2, std::string s3,
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
      : game_(s1), driver_regex_(s2), controller_name_(s3), maps_() {
#define PUSH(im) \
  if (!im.IsEmpty()) maps_.push_back(im);
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
  std::string game_;             // only used
  std::string driver_regex_;     // reported by InputHandler
  std::string controller_name_;  // the product name of the controller

  std::vector<AutoMappingEntry> maps_;
};

class InputScheme {
 public:
  const char* name_;
  int buttons_per_controller_;
  struct GameButtonInfo {
    const char* name_;  // The name used by the button graphics system.  e.g.
                        // "left", "right", "middle C", "snare"
    GameButton secondary_menu_button_;
  };
  // Data for each Game-specific GameButton. This starts at GAME_BUTTON_NEXT.
  GameButtonInfo game_button_info_[NUM_GameButton];
  const AutoMappings* auto_mappings_;

  GameButton ButtonNameToIndex(const std::string& button_name) const;
  GameButton GameButtonToMenuButton(GameButton gb) const;
  void MenuButtonToGameInputs(
      GameButton menu_i, PlayerNumber pn,
      std::vector<GameInput>& game_iout) const;
  void MenuButtonToGameButtons(
      GameButton menu_i, std::vector<GameButton>& game_buttons) const;
  const GameButtonInfo* GetGameButtonInfo(GameButton gb) const;
  const char* GetGameButtonName(GameButton gb) const;
};
/** @brief A special foreach loop to handle the various GameButtons. */
#define FOREACH_GameButtonInScheme(s, var)                               \
  for (GameButton var = (GameButton)0; var < s->buttons_per_controller_; \
       enum_add<GameButton>(var, +1))

class InputMappings {
 public:
  // only filled for automappings
  std::string device_regex_;
  std::string description_;

  // map from a GameInput to multiple DeviceInputs
  DeviceInput gi_to_di_[NUM_GameController][NUM_GameButton]
                       [NUM_GAME_TO_DEVICE_SLOTS];

  void Clear();
  void Unmap(InputDevice id);
  void WriteMappings(const InputScheme* input_scheme, std::string file_path);
  void ReadMappings(
      const InputScheme* input_scheme, std::string file_path,
      bool is_auto_mapping);
  void SetInputMap(
      const DeviceInput& device_i, const GameInput& game_i, int slot_index);

  void ClearFromInputMap(const DeviceInput& device_i);
  bool ClearFromInputMap(const GameInput& game_i, int slot_index);
};
/** @brief Holds user-chosen input preferences and saves it between sessions. */
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
      GameButton menu, std::vector<std::string>& full_reason,
      const std::string& sub_reason);
  void SanityCheckMappings(std::vector<std::string>& reason);

  void SetInputMap(
      const DeviceInput& device_i, const GameInput& game_i, int slot_index);
  void ClearFromInputMap(const DeviceInput& device_i);
  bool ClearFromInputMap(const GameInput& game_i, int slot_index);

  void AddDefaultMappingsForCurrentGameIfUnmapped();
  void AutoMapJoysticksForCurrentGame();
  bool CheckForChangedInputDevicesAndRemap(std::string& message_out);

  bool IsMapped(const DeviceInput& device_i) const;

  bool DeviceToGame(const DeviceInput& device_i, GameInput& game_i)
      const;  // return true if there is a mapping from device to pad
  bool GameToDevice(
      const GameInput& game_i, int slot_num, DeviceInput& device_i)
      const;  // return true if there is a mapping from pad to device

  GameButton GameButtonToMenuButton(GameButton gb) const;
  void MenuToGame(
      GameButton menu_i, PlayerNumber pn,
      std::vector<GameInput>& game_iout) const;
  PlayerNumber ControllerToPlayerNumber(GameController controller) const;

  float GetSecsHeld(const GameInput& game_i) const;

  bool IsBeingPressed(
      const GameInput& game_i,
      const DeviceInputList* button_state = nullptr) const;
  bool IsBeingPressed(GameButton menu_i, PlayerNumber pn) const;
  bool IsBeingPressed(
      const std::vector<GameInput>& game_i,
      const DeviceInputList* button_state = nullptr) const;

  void ResetKeyRepeat(const GameInput& game_i);
  void ResetKeyRepeat(GameButton menu_i, PlayerNumber pn);

  void RepeatStopKey(const GameInput& game_i);
  void RepeatStopKey(GameButton menu_i, PlayerNumber pn);

  float GetLevel(const GameInput& game_i) const;
  float GetLevel(GameButton menu_i, PlayerNumber pn) const;

  void Unmap(InputDevice device);
  void ApplyMapping(
      const std::vector<AutoMappingEntry>& mmaps, GameController gc,
      InputDevice id);

 protected:
  InputMappings mappings_;

  void UpdateTempDItoGI();
  const InputScheme* input_scheme_;

 private:
  InputMapper(const InputMapper& rhs);
  InputMapper& operator=(const InputMapper& rhs);
};

extern InputMapper*
    INPUTMAPPER;  // global and accessible from anywhere in our program

#endif

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
