/* ScreenMapControllers - Maps device input to instrument buttons. */

#ifndef SCREEN_MAP_CONTROLLERS_H
#define SCREEN_MAP_CONTROLLERS_H

#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorScroller.h"
#include "AutoActor.h"
#include "BitmapText.h"
#include "GameInput.h"
#include "InputMapper.h"
#include "InputQueue.h"
#include "MessageManager.h"
#include "RageInputDevice.h"
#include "RageSound.h"
#include "RageTimer.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"

class ScreenMapControllers : public ScreenWithMenuElements {
 public:
  ScreenMapControllers();
  ~ScreenMapControllers();
  virtual void Init();
  virtual void BeginScreen();

  virtual void Update(float fDeltaTime);
  virtual bool Input(const InputEventPlus& input);
  virtual void HandleMessage(const Message& msg);
  virtual void HandleScreenMessage(const ScreenMessage SM);

 private:
  Actor* GetActorWithFocus();
  void BeforeChangeFocus();
  void AfterChangeFocus();
  void Refresh();
  void DismissWarning();
  bool CursorOnAction();
  bool CursorOnHeader();
  bool CursorOnKey();
  bool CursorCanGoUp();
  bool CursorCanGoDown();
  bool CursorCanGoLeft();
  bool CursorCanGoRight();
  int CurKeyIndex();
  int CurActionIndex();
  void SetCursorFromSetListCurrent();
  void StartWaitingForPress();

  unsigned int cur_controller_;
  unsigned int cur_button_;
  unsigned int cur_slot_;
  unsigned int max_dest_item_;

  bool change_occurred_;

  RageTimer waiting_for_press_;
  DeviceInput device_i_to_map_;

  struct KeyToMap {
    GameButton game_button_;

    // owned by m_Line
    BitmapText* text_mapped_to_[NUM_GameController]
                               [NUM_SHOWN_GAME_TO_DEVICE_SLOTS];
  };
  std::vector<KeyToMap> keys_to_map_;

  BitmapText text_devices_;

  BitmapText text_label_[NUM_GameController];
  BitmapText list_header_center_;
  BitmapText list_header_labels_[NUM_GameController]
                                [NUM_SHOWN_GAME_TO_DEVICE_SLOTS];

  float auto_dismiss_warning_secs_;
  AutoActor warning_;

  float auto_dismiss_no_set_list_prompt_secs_;
  AutoActor no_set_list_prompt_;

  float auto_dismiss_sanity_secs_;
  AutoActor sanity_message_;

  struct SetListEntry {
    int button_;
    int controller_;
    int slot_;
    SetListEntry(int b, int c, int s) : button_(b), controller_(c), slot_(s) {}
    bool operator<(const SetListEntry& rhs) const {
      if (controller_ != rhs.controller_) {
        return controller_ < rhs.controller_;
      }
      if (button_ != rhs.button_) {
        return button_ < rhs.button_;
      }
      return slot_ < rhs.slot_;
    }
  };
  std::set<SetListEntry> set_list_;
  std::set<SetListEntry>::iterator set_list_current_;
  bool in_set_list_mode_;

  typedef void (ScreenMapControllers::*action_fun_t)();
  struct ActionRow {
    std::string name_;
    AutoActor actor_;
    action_fun_t action_;
    void Load(
        const std::string& scr_name, const std::string& name,
        ScreenMapControllers::action_fun_t action, ActorFrame* line,
        ActorScroller* scroller);
  };
  void ClearToDefault();
  void ReloadFromDisk();
  void SaveToDisk();
  void SetListMode();
  void ExitAction();
  bool SanityCheckWrapper();

  std::vector<ActionRow> actions_;

  std::vector<ActorFrame*> line_;
  ActorScroller line_scroller_;

  RageSound sound_change_;
  RageSound sound_delete_;
};

#endif

/*
 * (c) 2001-2004 Chris Danford
 * 2014 Eric Reese
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
