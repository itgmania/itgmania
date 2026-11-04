#include "ScreenMapControllers.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorScroller.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "EnumHelper.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "PeriodicCaller.h"
#include "PlayerNumber.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenPrompt.h"
#include "ScreenWithMenuElements.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"

AutoScreenMessage(SM_DoSaveAndExit);
#define BUTTONS_TO_MAP THEME->GetMetric(name_, "ButtonsToMap")
static LocalizedString INVALID_BUTTON("ScreenMapControllers", "InvalidButton");
static LocalizedString SAVE_PROMPT("ScreenMapControllers", "SavePrompt");
#define MAPPED_TO_COMMAND(gc, slot) \
  THEME->GetMetricA(name_, ssprintf("MappedToP%iS%iCommand", gc + 1, slot + 1))

static const float g_fSecondsToWaitForInput = 0.05f;

// reserve the 3rd slot for hard-coded keys
static const int NUM_CHANGABLE_SLOTS = NUM_SHOWN_GAME_TO_DEVICE_SLOTS - 1;

REGISTER_SCREEN_CLASS(ScreenMapControllers);

ScreenMapControllers::ScreenMapControllers() {
  in_set_list_mode_ = false;
  change_occurred_ = false;
  this->SubscribeToMessage(Message_AutoJoyMappingApplied);
}

ScreenMapControllers::~ScreenMapControllers() {
  for (size_t i = 0; i < line_.size(); ++i) {
    RageUtil::SafeDelete(line_[i]);
  }
}

static LocalizedString PLAYER_SLOTS("ScreenMapControllers", "%s slots");
static LocalizedString SLOT_NAMES[3] = {
    LocalizedString("ScreenMapControllers", "Primary"),
    LocalizedString("ScreenMapControllers", "Secondary"),
    LocalizedString("ScreenMapControllers", "Default")};
static LocalizedString KEYNAME("ScreenMapControllers", "KeyName");
void ScreenMapControllers::Init() {
  ScreenWithMenuElements::Init();

  sound_change_.Load(THEME->GetPathS(name_, "change"), true);
  sound_delete_.Load(THEME->GetPathS(name_, "delete"), true);

  text_devices_.LoadFromFont(THEME->GetPathF(name_, "devices"));
  text_devices_.SetName("Devices");
  LOAD_ALL_COMMANDS_AND_SET_XY(text_devices_);
  this->AddChild(&text_devices_);

  std::string sButtons = BUTTONS_TO_MAP;
  if (sButtons.empty()) {
    /* Map all buttons for this game. */
    FOREACH_GameButtonInScheme(INPUTMAPPER->GetInputScheme(), gb) {
      KeyToMap k;
      k.game_button_ = gb;
      keys_to_map_.push_back(k);
    }
  } else {
    /* Map the specified buttons. */
    std::vector<std::string> asBits;
    split(sButtons, ",", asBits);
    for (unsigned i = 0; i < asBits.size(); ++i) {
      KeyToMap k;
      k.game_button_ =
          StringToGameButton(INPUTMAPPER->GetInputScheme(), asBits[i]);
      keys_to_map_.push_back(k);
    }
  }

  // slot names row
  {
    line_.push_back(new ActorFrame);
    FOREACH_ENUM(GameController, c) {
      BitmapText& text = text_label_[c];
      text.LoadFromFont(THEME->GetPathF(name_, "title"));
      PlayerNumber pn = (PlayerNumber)c;
      text.SetName("Label" + PlayerNumberToString(pn));
      std::string sText = ssprintf(
          PLAYER_SLOTS.GetValue().c_str(),
          PlayerNumberToLocalizedString(pn).c_str());
      text.SetText(sText);
      ActorUtil::LoadAllCommands(text, name_);
      line_.back()->AddChild(&text_label_[c]);
    }
    line_scroller_.AddChild(line_.back());
  }
  // header row
  {
    line_.push_back(new ActorFrame);
    list_header_center_.LoadFromFont(THEME->GetPathF(name_, "title"));
    list_header_center_.SetName("ListHeaderCenter");
    list_header_center_.SetText(KEYNAME);
    ActorUtil::LoadAllCommands(list_header_center_, name_);
    line_.back()->AddChild(&list_header_center_);
    FOREACH_ENUM(GameController, c) {
      for (int s = 0; s < NUM_SHOWN_GAME_TO_DEVICE_SLOTS; ++s) {
        BitmapText& text = list_header_labels_[c][s];
        text.LoadFromFont(THEME->GetPathF(name_, "title"));
        text.SetName("ListHeader");
        text.SetText(SLOT_NAMES[s]);
        text.RunCommands(THEME->GetMetricA(
            name_, ssprintf("ListHeaderP%iS%iCommand", c + 1, s + 1)));
        ActorUtil::LoadAllCommands(text, name_);
        line_.back()->AddChild(&text);
      }
    }
    line_scroller_.AddChild(line_.back());
  }

  // normal rows
  for (unsigned b = 0; b < keys_to_map_.size(); b++) {
    line_.push_back(new ActorFrame);
    KeyToMap* pKey = &keys_to_map_[b];

    {
      BitmapText* pName = new BitmapText;
      pName->SetName("Primary");
      pName->LoadFromFont(THEME->GetPathF(name_, "title"));
      std::string sText = GameButtonToLocalizedString(
          INPUTMAPPER->GetInputScheme(), pKey->game_button_);
      pName->SetText(sText);
      ActorUtil::LoadAllCommands(*pName, name_);
      line_.back()->AddChild(pName);
    }
    {
      BitmapText* pSecondary = new BitmapText;
      pSecondary->SetName("Secondary");
      pSecondary->LoadFromFont(THEME->GetPathF(name_, "title"));
      GameButton mb = INPUTMAPPER->GetInputScheme()->GameButtonToMenuButton(
          pKey->game_button_);
      std::string sText;
      if (mb != GameButton_Invalid && mb != pKey->game_button_) {
        sText = GameButtonToLocalizedString(INPUTMAPPER->GetInputScheme(), mb);
      }
      ActorUtil::LoadAllCommands(*pSecondary, name_);
      pSecondary->SetText(sText);
      line_.back()->AddChild(pSecondary);
    }

    FOREACH_ENUM(GameController, c) {
      for (int s = 0; s < NUM_SHOWN_GAME_TO_DEVICE_SLOTS; s++) {
        pKey->text_mapped_to_[c][s] = new BitmapText;
        pKey->text_mapped_to_[c][s]->SetName("MappedTo");
        pKey->text_mapped_to_[c][s]->LoadFromFont(
            THEME->GetPathF(name_, "entry"));
        pKey->text_mapped_to_[c][s]->RunCommands(MAPPED_TO_COMMAND(c, s));
        ActorUtil::LoadAllCommands(*pKey->text_mapped_to_[c][s], name_);
        line_.back()->AddChild(pKey->text_mapped_to_[c][s]);
      }
    }
    line_.back()->DeleteChildrenWhenDone();
    line_.back()->SetName("Line");
    ActorUtil::LoadAllCommands(line_.back(), name_);
    line_scroller_.AddChild(line_.back());
  }

#define ADD_ACTION(index, a, b, c, d, e) \
  line_.push_back(new ActorFrame);       \
  actions_[index].Load(a, b, c, d, e);

  actions_.resize(5);
  ADD_ACTION(
      0, name_, "Clear", &ScreenMapControllers::ClearToDefault, line_.back(),
      &line_scroller_);
  ADD_ACTION(
      1, name_, "Reload", &ScreenMapControllers::ReloadFromDisk, line_.back(),
      &line_scroller_);
  ADD_ACTION(
      2, name_, "Save", &ScreenMapControllers::SaveToDisk, line_.back(),
      &line_scroller_);
  ADD_ACTION(
      3, name_, "SetList", &ScreenMapControllers::SetListMode, line_.back(),
      &line_scroller_);
  ADD_ACTION(
      4, name_, "Exit", &ScreenMapControllers::ExitAction, line_.back(),
      &line_scroller_);
#undef ADD_ACTION

  max_dest_item_ = (1 + keys_to_map_.size() + actions_.size()) -
                   THEME->GetMetricI("ScreenMapControllers", "LinesVisible");

  line_scroller_.SetName("LineScroller");
  ActorUtil::LoadAllCommands(line_scroller_, name_);
  line_scroller_.SetNumItemsToDraw((float)line_scroller_.GetNumChildren() * 2);
  line_scroller_.Load2();
  this->AddChild(&line_scroller_);

  no_set_list_prompt_.Load(THEME->GetPathG(name_, "nosetlistprompt"));
  no_set_list_prompt_->SetName("NoSetListPrompt");
  this->AddChild(no_set_list_prompt_);

  sanity_message_.Load(THEME->GetPathG(name_, "sanitymessage"));
  sanity_message_->SetName("SanityMessage");
  this->AddChild(sanity_message_);

  warning_.Load(THEME->GetPathG(name_, "warning"));
  warning_->SetName("Warning");
  warning_->SetDrawOrder(DRAW_ORDER_TRANSITIONS);
  this->AddChild(warning_);
  LOAD_ALL_COMMANDS(warning_);
}

void ScreenMapControllers::BeginScreen() {
  cur_controller_ = 0;
  cur_button_ = 0;
  cur_slot_ = 0;

  ScreenWithMenuElements::BeginScreen();

  waiting_for_press_.SetZero();

  Refresh();
  AfterChangeFocus();
  lock_input_secs_ = THEME->GetMetricF(name_, "LockInputSecs");
  auto_dismiss_warning_secs_ =
      THEME->GetMetricF(name_, "AutoDismissWarningSecs");
  auto_dismiss_no_set_list_prompt_secs_ = 0.0f;
  auto_dismiss_sanity_secs_ = 0.0f;
  if (auto_dismiss_warning_secs_ > 0.25) {
    warning_->PlayCommand("TweenOn");
  } else {
    if (warning_->HasCommand("NeverShow")) {
      warning_->PlayCommand("NeverShow");
    } else {
      warning_->SetHibernate(16777216.0f);
    }
  }
}

void ScreenMapControllers::Update(float fDeltaTime) {
  ScreenWithMenuElements::Update(fDeltaTime);

  if (lock_input_secs_ <= 0.0f) {
    bool was_above = auto_dismiss_warning_secs_ > 0.0f;
    auto_dismiss_warning_secs_ -= fDeltaTime;
    if (was_above && auto_dismiss_warning_secs_ <= 0.0f) {
      DismissWarning();
    }
  }
  if (auto_dismiss_no_set_list_prompt_secs_ > 0.0f) {
    auto_dismiss_no_set_list_prompt_secs_ -= fDeltaTime;
    if (auto_dismiss_no_set_list_prompt_secs_ <= 0.0f) {
      no_set_list_prompt_->PlayCommand("TweenOff");
    }
  }
  if (auto_dismiss_sanity_secs_ > 0.0f) {
    auto_dismiss_sanity_secs_ -= fDeltaTime;
    if (auto_dismiss_sanity_secs_ <= 0.0f) {
      sanity_message_->PlayCommand("TweenOff");
    }
  }

  // We don't expect the connected devices to change frequently.
  // As a result, delay how often we check for and update the device strings.
  CallEveryNFrames(250, [this]() {
    text_devices_.SetText(INPUTMAN->GetDisplayDevicesString());
  });

  if (!waiting_for_press_.IsZero() &&
      device_i_to_map_.IsValid())  // we're going to map an input
  {
    if (waiting_for_press_.Ago() < g_fSecondsToWaitForInput) {
      return; /* keep waiting */
    }
    waiting_for_press_.SetZero();

    ASSERT(CursorOnKey());
    const KeyToMap* pKey = &keys_to_map_[CurKeyIndex()];

    GameInput curGameI((GameController)cur_controller_, pKey->game_button_);

    INPUTMAPPER->SetInputMap(device_i_to_map_, curGameI, cur_slot_);
    INPUTMAPPER->AddDefaultMappingsForCurrentGameIfUnmapped();
    change_occurred_ = true;

    BitmapText* pText = pKey->text_mapped_to_[cur_controller_][cur_slot_];
    pText->PlayCommand("MappedInput");
    if (in_set_list_mode_) {
      pText->PlayCommand("LoseMark");
      ++set_list_current_;
      if (set_list_current_ == set_list_.end()) {
        in_set_list_mode_ = false;
        set_list_.clear();
      } else {
        BeforeChangeFocus();
        SetCursorFromSetListCurrent();
        AfterChangeFocus();
        StartWaitingForPress();
      }
    }
    Refresh();
    SCREENMAN->PlayStartSound();
  }
}

/* Note that this isn't necessarily correct.  For example, JOY_LEFT might
 * actually be a D-pad and not an axis.  All this is actually doing is giving
 * priority to some inputs over others; this function is unsuitable for other
 * use. */
static bool IsAxis(const DeviceInput& DeviceI) {
  if (!DeviceI.IsJoystick()) {
    return false;
  }

  static int axes[] = {
      JOY_LEFT,       JOY_RIGHT,    JOY_UP,        JOY_DOWN,      JOY_LEFT_2,
      JOY_RIGHT_2,    JOY_UP_2,     JOY_DOWN_2,    JOY_Z_UP,      JOY_Z_DOWN,
      JOY_ROT_UP,     JOY_ROT_DOWN, JOY_ROT_LEFT,  JOY_ROT_RIGHT, JOY_ROT_Z_UP,
      JOY_ROT_Z_DOWN, JOY_HAT_LEFT, JOY_HAT_RIGHT, JOY_HAT_UP,    JOY_HAT_DOWN,
      JOY_AUX_1,      JOY_AUX_2,    JOY_AUX_3,     JOY_AUX_4,     -1};

  for (int ax = 0; axes[ax] != -1; ++ax) {
    if (DeviceI.button_ == axes[ax]) {
      return true;
    }
  }

  return false;
}

bool ScreenMapControllers::Input(const InputEventPlus& input) {
  if (lock_input_secs_ > 0.0f) {
    return false;
  }

  if (auto_dismiss_warning_secs_ > 0.0f) {
    if (input.type_ == IET_FIRST_PRESS &&
        input.device_i_.device_ == DEVICE_KEYBOARD &&
        input.device_i_.button_ == KEY_ENTER) {
      auto_dismiss_warning_secs_ = 0.0f;
      DismissWarning();
    }
    return false;
  }

  if (auto_dismiss_no_set_list_prompt_secs_ > 0.0f) {
    if (input.type_ == IET_FIRST_PRESS &&
        input.device_i_.device_ == DEVICE_KEYBOARD &&
        input.device_i_.button_ == KEY_ENTER) {
      auto_dismiss_no_set_list_prompt_secs_ = 0.0f;
      no_set_list_prompt_->PlayCommand("TweenOff");
    }
    return false;
  }

  if (auto_dismiss_sanity_secs_ > 0.0f) {
    if (input.type_ == IET_FIRST_PRESS &&
        input.device_i_.device_ == DEVICE_KEYBOARD &&
        input.device_i_.button_ == KEY_ENTER) {
      auto_dismiss_sanity_secs_ = 0.0f;
      sanity_message_->PlayCommand("TweenOff");
    }
    return false;
  }

  if (input.type_ != IET_FIRST_PRESS && input.type_ != IET_REPEAT) {
    return false;  // ignore
  }
  if (IsTransitioning()) {
    return false;  // ignore
  }

  // Whoever wants it can uncomment this log spew, I don't think it's necessary.
  // -Kyz LOG->Trace( "ScreenMapControllers::Input():  device: %d, button: %d",
  // 	input.DeviceI.device, input.DeviceI.button );

  int button = input.device_i_.button_;
  bool bHandled = false;

  /* TRICKY: Some adapters map the PlayStation digital d-pad to both axes and
   * buttons. We want buttons to be used for any mappings where possible
   * because presses of buttons aren't mutually exclusive and presses of axes
   * are (e.g. can't read presses of both Left and Right simultaneously). So,
   * when the user presses a button, we'll wait until the next Update before
   * adding a mapping so that we get a chance to see all input events the
   * user's press of a panel. Prefer non-axis events over axis events. */
  if (!waiting_for_press_.IsZero()) {
    if (input.type_ != IET_FIRST_PRESS) {
      return false;
    }

    // Don't allow function keys to be mapped.
    if (input.device_i_.device_ == DEVICE_KEYBOARD &&
        (input.device_i_.button_ >= KEY_F1 &&
         input.device_i_.button_ <= KEY_F12)) {
      SCREENMAN->SystemMessage(INVALID_BUTTON);
      SCREENMAN->PlayInvalidSound();
    } else {
      if (device_i_to_map_.IsValid() && !IsAxis(device_i_to_map_) &&
          IsAxis(input.device_i_)) {
        LOG->Trace("Ignored input; non-axis event already received");
        return false;  // ignore this press
      }

      device_i_to_map_ = input.device_i_;
    }
  } else if (input.device_i_.device_ == DEVICE_KEYBOARD) {
    switch (button) {
        /* We only advertise space as doing this, but most games use either
         * delete or backspace, and I find them more intuitive, so allow them,
         * too. -gm */

        /* XXX: For some reason that eludes me, this function gets sent an
         * KEY_SPACE button press every time the JOY_HAT_UP button is pressed.
         * Had to put this in to prevent mappings being erased everytime the
         * user pressed up on the joypad. */

      case KEY_DEL:
      case KEY_SPACE:
      case KEY_BACK:  // Clear the selected input mapping.
        if (!CursorOnKey()) {
          break;
        }
        {
          const KeyToMap* pKey = &keys_to_map_[CurKeyIndex()];
          GameInput curGameI(
              (GameController)cur_controller_, pKey->game_button_);
          if (!INPUTMAPPER->ClearFromInputMap(curGameI, cur_slot_)) {
            break;
          }

          INPUTMAPPER->AddDefaultMappingsForCurrentGameIfUnmapped();

          sound_delete_.Play(true);
          bHandled = true;
        }
        break;
      case KEY_LEFT:  // Move the selection left, wrapping up.
        if (!CursorCanGoLeft()) {
          break;
        }
        BeforeChangeFocus();
        if (cur_slot_ == 0) {
          cur_slot_ = NUM_CHANGABLE_SLOTS - 1;
          --cur_controller_;
        } else {
          --cur_slot_;
        }
        AfterChangeFocus();
        sound_change_.Play(true);
        bHandled = true;
        break;
      case KEY_RIGHT:  // Move the selection right, wrapping down.
        if (!CursorCanGoRight()) {
          break;
        }
        BeforeChangeFocus();
        cur_slot_++;
        if (cur_slot_ > NUM_CHANGABLE_SLOTS - 1) {
          cur_slot_ = 0;
          cur_controller_++;
        }
        AfterChangeFocus();
        sound_change_.Play(true);
        bHandled = true;
        break;
      case KEY_UP:  // Move the selection up.
        if (!CursorCanGoUp()) {
          break;
        }
        BeforeChangeFocus();
        cur_button_--;
        AfterChangeFocus();
        sound_change_.Play(true);
        bHandled = true;
        break;
      case KEY_DOWN:  // Move the selection down.
        if (!CursorCanGoDown()) {
          break;
        }
        BeforeChangeFocus();
        cur_button_++;
        AfterChangeFocus();
        sound_change_.Play(true);
        bHandled = true;
        break;
      case KEY_ESC:  // Quit the screen.
        ExitAction();
        bHandled = true;
        break;
      case KEY_Cm:
        if (CursorOnKey()) {
          SetListEntry to_add(
              SetListEntry(CurKeyIndex(), cur_controller_, cur_slot_));
          std::set<SetListEntry>::iterator found = set_list_.find(to_add);
          if (found == set_list_.end()) {
            set_list_.insert(to_add);
            GetActorWithFocus()->PlayCommand("GainMark");
          } else {
            set_list_.erase(found);
            GetActorWithFocus()->PlayCommand("LoseMark");
          }
        }
        break;
      case KEY_ENTER:  // Change the selection.
      case KEY_KP_ENTER:
        bHandled = true;
        if (CursorOnAction()) {
          (this->*actions_[CurActionIndex()].action_)();
          SCREENMAN->PlayStartSound();
          break;
        }
        if (CursorOnHeader()) {
          break;
        }
        StartWaitingForPress();
        SCREENMAN->PlayStartSound();
        break;
    }
  }

  //	ScreenWithMenuElements::Input( input );	// default handler

  // This trace is also useless log spew.  Create a preference or something
  // configurable if you disagree. -Kyz
  // LOG->Trace( "m_CurSlot: %d m_CurController: %d m_CurButton: %d", m_CurSlot,
  // m_CurController, m_CurButton );

  Refresh();
  return bHandled;
}

Actor* ScreenMapControllers::GetActorWithFocus() {
  if (CursorOnAction()) {
    return actions_[CurActionIndex()].actor_;
  }
  if (CursorOnHeader()) {
    return &(list_header_labels_[cur_controller_][cur_slot_]);
  }

  const KeyToMap* pKey = &keys_to_map_[CurKeyIndex()];
  return pKey->text_mapped_to_[cur_controller_][cur_slot_];
}

void ScreenMapControllers::BeforeChangeFocus() {
  Actor* pActor = GetActorWithFocus();
  pActor->PlayCommand("LoseFocus");
}

void ScreenMapControllers::AfterChangeFocus() {
  Actor* pActor = GetActorWithFocus();
  pActor->PlayCommand("GainFocus");
}

void ScreenMapControllers::Refresh() {
  FOREACH_ENUM(GameController, p) {
    for (unsigned b = 0; b < keys_to_map_.size(); b++) {
      const KeyToMap* pKey = &keys_to_map_[b];
      for (int s = 0; s < NUM_SHOWN_GAME_TO_DEVICE_SLOTS; s++) {
        BitmapText* pText = pKey->text_mapped_to_[p][s];
        GameInput cur_gi(p, pKey->game_button_);
        DeviceInput di;
        std::string sText = "-----------";
        if (INPUTMAPPER->GameToDevice(cur_gi, s, di)) {
          sText = INPUTMAN->GetDeviceSpecificInputString(di);
        }
        pText->SetText(sText);
      }
    }
  }

  line_scroller_.SetDestinationItem(
      static_cast<float>(std::min(cur_button_, max_dest_item_)));
}

void ScreenMapControllers::DismissWarning() {
  warning_->PlayCommand("TweenOff");
}

bool ScreenMapControllers::CursorOnAction() {
  // We have a header row, the rows for the keys, and the action rows.
  // So every row after m_KeysToMap.size is an action row.
  return cur_button_ >= keys_to_map_.size() + 1;
}

bool ScreenMapControllers::CursorOnHeader() {
  // We have a header row before all others.
  // So the header is at 0.
  return cur_button_ == 0;
}

bool ScreenMapControllers::CursorOnKey() {
  return !(CursorOnHeader() || CursorOnAction());
}

bool ScreenMapControllers::CursorCanGoUp() { return cur_button_ > 0; }

bool ScreenMapControllers::CursorCanGoDown() {
  return cur_button_ < keys_to_map_.size() + actions_.size();
}

bool ScreenMapControllers::CursorCanGoLeft() {
  return !CursorOnAction() && (cur_slot_ > 0 || cur_controller_ > 0);
}

bool ScreenMapControllers::CursorCanGoRight() {
  return !CursorOnAction() && (cur_slot_ < NUM_CHANGABLE_SLOTS - 1 ||
                               cur_controller_ < NUM_GameController - 1);
}

int ScreenMapControllers::CurKeyIndex() {
  // The header row is at 0, so subtract 1 from m_CurButton.
  return cur_button_ - 1;
}

int ScreenMapControllers::CurActionIndex() {
  // Subtract the header row and the keys.
  return cur_button_ - 1 - keys_to_map_.size();
}

void ScreenMapControllers::SetCursorFromSetListCurrent() {
  cur_button_ = set_list_current_->button_ + 1;
  cur_controller_ = set_list_current_->controller_;
  cur_slot_ = set_list_current_->slot_;
}

void ScreenMapControllers::StartWaitingForPress() {
  const KeyToMap* pKey = &keys_to_map_[CurKeyIndex()];
  BitmapText* pText = pKey->text_mapped_to_[cur_controller_][cur_slot_];
  pText->PlayCommand("Waiting");
  waiting_for_press_.Touch();
  device_i_to_map_.MakeInvalid();
}

void ScreenMapControllers::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_DoSaveAndExit) {
    switch (ScreenPrompt::last_answer_) {
      DEFAULT_FAIL(ScreenPrompt::last_answer_);
      case ANSWER_YES:
        SaveToDisk();
        StartTransitioningScreen(SM_GoToNextScreen);
        break;
      case ANSWER_NO:
        ReloadFromDisk();
        StartTransitioningScreen(SM_GoToNextScreen);
        break;
      case ANSWER_CANCEL:
        break;
    }
  } else {
    ScreenWithMenuElements::HandleScreenMessage(SM);
  }
}

void ScreenMapControllers::HandleMessage(const Message& msg) {
  if (msg == Message_AutoJoyMappingApplied) {
    Refresh();
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

void ScreenMapControllers::ClearToDefault() {
  INPUTMAPPER->ResetMappingsToDefault();
}

void ScreenMapControllers::ReloadFromDisk() {
  INPUTMAPPER->ReadMappingsFromDisk();
}

void ScreenMapControllers::SaveToDisk() {
  if (SanityCheckWrapper()) {
    INPUTMAPPER->SaveMappingsToDisk();
    change_occurred_ = false;
  }
}

void ScreenMapControllers::SetListMode() {
  if (set_list_.size() < 1) {
    no_set_list_prompt_->PlayCommand("TweenOn");
    auto_dismiss_no_set_list_prompt_secs_ =
        THEME->GetMetricF(name_, "AutoDismissNoSetListPromptSecs");
  } else {
    set_list_current_ = set_list_.begin();
    in_set_list_mode_ = true;
    BeforeChangeFocus();
    SetCursorFromSetListCurrent();
    AfterChangeFocus();
    StartWaitingForPress();
  }
}

void ScreenMapControllers::ExitAction() {
  if (change_occurred_) {
    // If the current mapping doesn't pass the sanity check, then the user
    // can't navigate the prompt screen to pick a choice. -Kyz
    if (SanityCheckWrapper()) {
      ScreenPrompt::Prompt(
          SM_DoSaveAndExit, SAVE_PROMPT, PROMPT_YES_NO_CANCEL, ANSWER_YES);
    }
  } else {
    SCREENMAN->PlayStartSound();
    StartTransitioningScreen(SM_GoToNextScreen);
  }
}

bool ScreenMapControllers::SanityCheckWrapper() {
  std::vector<std::string> reasons_not_sane;
  INPUTMAPPER->SanityCheckMappings(reasons_not_sane);
  if (reasons_not_sane.empty()) {
    return true;
  } else {
    for (std::string& reason : reasons_not_sane) {
      reason = THEME->GetString("ScreenMapControllers", reason);
    }
    std::string joined_reasons = join("\n", reasons_not_sane);
    joined_reasons = THEME->GetString("ScreenMapControllers", "VitalButtons") +
                     "\n" + joined_reasons;
    Message msg("SetText");
    msg.SetParam("Text", joined_reasons);
    sanity_message_->HandleMessage(msg);
    auto_dismiss_sanity_secs_ =
        THEME->GetMetricF(name_, "AutoDismissSanitySecs");
    return false;
  }
}

void ScreenMapControllers::ActionRow::Load(
    const std::string& scr_name, const std::string& name,
    ScreenMapControllers::action_fun_t action, ActorFrame* line,
    ActorScroller* scroller) {
  action_ = action;
  std::string lower_name = name;
  MakeLower(lower_name);
  // Make the specific actor optional, use a fallback if it doesn't exist.
  std::string path = THEME->GetPathG(scr_name, lower_name, true);
  if (path.empty()) {
    path = THEME->GetPathG(scr_name, "action");
  }
  actor_.Load(path);
  actor_->SetName(name);
  ActorUtil::LoadAllCommands(*actor_, scr_name);
  line->AddChild(actor_);
  scroller->AddChild(line);
}

/*
 * (c) 2001-2005 Chris Danford, Glenn Maynard
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
