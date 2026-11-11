#include "ScreenDebugOverlay.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "CodeDetector.h"
#include "DateTime.h"
#include "EnumHelper.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameLoop.h"
#include "GamePreferences.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "Grade.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RadarValues.h"
#include "RageDisplay.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageTextureManager.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongOptions.h"
#include "StepMania.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "XmlToLua.h"
#include "global.h"

static bool is_displayed = false;
static bool is_slow = false;
static bool is_halt = false;
static RageTimer halt_timer(kRageZeroTimer);
static float image_scale_current = 1;
static float image_scale_destination = 1;

// This will disable the debug menu ENTIRELY - including F6 for autosync, F8 for
// autoplay, etc
static bool enable_debug_menu = true;

// DebugLine theming
static const ThemeMetric<RageColor> BACKGROUND_COLOR(
    "ScreenDebugOverlay", "BackgroundColor");
static const ThemeMetric<RageColor> LINE_ON_COLOR(
    "ScreenDebugOverlay", "LineOnColor");
static const ThemeMetric<RageColor> LINE_OFF_COLOR(
    "ScreenDebugOverlay", "LineOffColor");
static const ThemeMetric<float> LINE_START_Y(
    "ScreenDebugOverlay", "LineStartY");
static const ThemeMetric<float> LINE_SPACING(
    "ScreenDebugOverlay", "LineSpacing");
static const ThemeMetric<float> LINE_BUTTON_X(
    "ScreenDebugOverlay", "LineButtonX");
static const ThemeMetric<float> LINE_FUNCTION_X(
    "ScreenDebugOverlay", "LineFunctionX");
static const ThemeMetric<float> PAGE_START_X(
    "ScreenDebugOverlay", "PageStartX");
static const ThemeMetric<float> PAGE_SPACING_X(
    "ScreenDebugOverlay", "PageSpacingX");

// self-registering debug lines
// We don't use SubscriptionManager, because we want to keep the line order.
static LocalizedString ON("ScreenDebugOverlay", "on");
static LocalizedString OFF("ScreenDebugOverlay", "off");
static LocalizedString MUTE_ACTIONS_ON("ScreenDebugOverlay", "Mute actions on");
static LocalizedString MUTE_ACTIONS_OFF(
    "ScreenDebugOverlay", "Mute actions off");

class IDebugLine;
static std::vector<IDebugLine*>* subscribers = nullptr;
class IDebugLine {
 public:
  IDebugLine() {
    if (subscribers == nullptr) {
      subscribers = new std::vector<IDebugLine*>;
    }
    subscribers->push_back(this);
  }
  virtual ~IDebugLine() {}
  enum Type { all_screens, gameplay_only };
  virtual Type GetType() const { return all_screens; }
  virtual std::string GetDisplayTitle() = 0;
  virtual std::string GetDisplayValue() {
    return IsEnabled() ? ON.GetValue() : OFF.GetValue();
  }
  virtual std::string GetPageName() const { return "Main"; }
  virtual bool ForceOffAfterUse() const { return false; }
  virtual bool IsEnabled() = 0;
  virtual void DoAndLog(std::string& message_out) {
    std::string s1 = GetDisplayTitle();
    std::string s2 = GetDisplayValue();
    if (!s2.empty()) {
      s1 += " - ";
    }
    message_out = s1 + s2;
  };

  DeviceInput button_;
};

static bool IsGameplay() {
  return SCREENMAN && SCREENMAN->GetTopScreen() &&
         SCREENMAN->GetTopScreen()->GetScreenType() == gameplay;
}

REGISTER_SCREEN_CLASS(ScreenDebugOverlay);

ScreenDebugOverlay::~ScreenDebugOverlay() {
  this->RemoveAllChildren();

  for (BitmapText* p : text_pages_) {
    RageUtil::SafeDelete(p);
  }
  for (BitmapText* p : text_button_) {
    RageUtil::SafeDelete(p);
  }
  text_button_.clear();
  for (BitmapText* p : text_function_) {
    RageUtil::SafeDelete(p);
  }
  text_function_.clear();
}

const int kMaxDebugLines = 30;
struct MapDebugToDI {
  DeviceInput hold_for_debug1_;
  DeviceInput hold_for_debug2_;
  DeviceInput hold_for_slow_;
  DeviceInput hold_for_fast_;
  DeviceInput toggle_mute_;
  DeviceInput debug_button_[kMaxDebugLines];
  DeviceInput gameplay_button_[kMaxDebugLines];
  std::map<DeviceInput, int> page_button_;
  void Clear() {
    hold_for_debug1_.MakeInvalid();
    hold_for_debug2_.MakeInvalid();
    hold_for_slow_.MakeInvalid();
    hold_for_fast_.MakeInvalid();
    toggle_mute_.MakeInvalid();
    for (int i = 0; i < kMaxDebugLines; i++) {
      debug_button_[i].MakeInvalid();
      gameplay_button_[i].MakeInvalid();
    }
  }
};
static MapDebugToDI mappings;

static LocalizedString IN_GAMEPLAY("ScreenDebugOverlay", "%s in gameplay");
static std::string GetDebugButtonName(const IDebugLine* line) {
  std::string s = INPUTMAN->GetDeviceSpecificInputString(line->button_);
  IDebugLine::Type type = line->GetType();
  switch (type) {
    case IDebugLine::all_screens:
      return s;
    case IDebugLine::gameplay_only:
      return ssprintf(IN_GAMEPLAY.GetValue().c_str(), s.c_str());
    default:
      FAIL_M(ssprintf("Invalid debug line type: %i", type));
  }
}

template <typename U, typename V>
static bool GetKeyFromMap(const std::map<U, V>& m, const V& val, U& key) {
  for (typename std::map<U, V>::const_iterator iter = m.begin();
       iter != m.end(); ++iter) {
    if (iter->second == val) {
      key = iter->first;
      return true;
    }
  }
  return false;
}

static LocalizedString DEBUG_MENU("ScreenDebugOverlay", "Debug Menu");
void ScreenDebugOverlay::Init() {
  enable_debug_menu = PREFSMAN->debug_menu_enabled_.Get();

  Screen::Init();

  if (!enable_debug_menu) {
    return;
  }

  // Init debug mappings
  // TODO: Arch-specific?
  {
    mappings.Clear();

    mappings.hold_for_debug1_ = DeviceInput(DEVICE_KEYBOARD, KEY_F3);
    mappings.hold_for_debug2_.MakeInvalid();
    mappings.hold_for_slow_ = DeviceInput(DEVICE_KEYBOARD, KEY_ACCENT);
    mappings.hold_for_fast_ = DeviceInput(DEVICE_KEYBOARD, KEY_TAB);
    mappings.toggle_mute_ = DeviceInput(DEVICE_KEYBOARD, KEY_PAUSE);

    /* TODO: Find a better way of indicating which option is which here.
     * Maybe we should take a page from ScreenEdit's menus and make
     * RowDefs()? */

    int i = 0;
    mappings.gameplay_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_F8);
    mappings.gameplay_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_F7);
    mappings.gameplay_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_F6);
    i = 0;
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C1);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C2);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C3);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C4);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C5);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C6);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C7);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C8);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C9);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_C0);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cq);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cw);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Ce);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cr);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Ct);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cy);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cu);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Ci);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Co);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cp);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Ca);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cs);
    mappings.debug_button_[i++] = DeviceInput(DEVICE_KEYBOARD, KEY_Cd);
    mappings.page_button_[DeviceInput(DEVICE_KEYBOARD, KEY_F5)] = 0;
    mappings.page_button_[DeviceInput(DEVICE_KEYBOARD, KEY_F6)] = 1;
    mappings.page_button_[DeviceInput(DEVICE_KEYBOARD, KEY_F7)] = 2;
    mappings.page_button_[DeviceInput(DEVICE_KEYBOARD, KEY_F8)] = 3;
  }

  std::map<std::string, int> next_debug_button;
  int next_gameplay_button = 0;
  for (IDebugLine* p : *subscribers) {
    std::string page_name = p->GetPageName();

    DeviceInput di;
    switch (p->GetType()) {
      case IDebugLine::all_screens:
        di = mappings.debug_button_[next_debug_button[page_name]++];
        break;
      case IDebugLine::gameplay_only:
        di = mappings.gameplay_button_[next_gameplay_button++];
        break;
    }
    p->button_ = di;

    if (find(pages_.begin(), pages_.end(), page_name) == pages_.end()) {
      pages_.push_back(page_name);
    }
  }

  current_page_ = 0;
  forced_hidden_ = false;

  quad_.StretchTo(RectF(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT));
  quad_.SetDiffuse(BACKGROUND_COLOR);
  this->AddChild(&quad_);

  // if you're going to add user commands, make sure to have the overrides
  // set after parsing the metrics. -aj
  text_header_.SetName("HeaderText");
  text_header_.LoadFromFont(THEME->GetPathF("ScreenDebugOverlay", "header"));
  LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(text_header_);
  text_header_.SetText(DEBUG_MENU);
  this->AddChild(&text_header_);

  auto start = pages_.begin();
  for (std::vector<std::string>::const_iterator s = pages_.begin();
       s != pages_.end(); ++s) {
    int page = s - start;

    DeviceInput di;
    bool b = GetKeyFromMap(mappings.page_button_, page, di);
    ASSERT(b);

    std::string button = INPUTMAN->GetDeviceSpecificInputString(di);

    BitmapText* p = new BitmapText;
    p->SetName("PageText");
    p->LoadFromFont(THEME->GetPathF("ScreenDebugOverlay", "page"));
    LOAD_ALL_COMMANDS_AND_ON_COMMAND(p);
    // todo: Y value is still hardcoded. -aj
    p->SetXY(PAGE_START_X + page * PAGE_SPACING_X, SCREEN_TOP + 20);
    p->SetText(*s + " (" + button + ")");
    text_pages_.push_back(p);
    this->AddChild(p);
  }

  for (auto p = subscribers->begin(); p != subscribers->end(); ++p) {
    {
      BitmapText* bt = new BitmapText;
      bt->SetName("ButtonText");
      bt->LoadFromFont(THEME->GetPathF("ScreenDebugOverlay", "line"));
      bt->SetHorizAlign(kAlignRight);
      bt->SetText("blah");
      LOAD_ALL_COMMANDS_AND_ON_COMMAND(*bt);
      text_button_.push_back(bt);
      this->AddChild(bt);
    }
    {
      BitmapText* bt = new BitmapText;
      bt->SetName("FunctionText");
      bt->LoadFromFont(THEME->GetPathF("ScreenDebugOverlay", "line"));
      bt->SetHorizAlign(kAlignLeft);
      bt->SetText("blah");
      LOAD_ALL_COMMANDS_AND_ON_COMMAND(*bt);
      text_function_.push_back(bt);
      this->AddChild(bt);
    }
  }

  this->SetVisible(false);
}

void ScreenDebugOverlay::Update(float delta_time) {
  if (!enable_debug_menu) {
    return;
  }

  {
    float rate = 1;
    if (INPUTFILTER->IsBeingPressed(mappings.hold_for_fast_)) {
      if (INPUTFILTER->IsBeingPressed(mappings.hold_for_slow_)) {
        rate = 0;  // both; stop time
      } else {
        rate *= 4;
      }
    } else if (INPUTFILTER->IsBeingPressed(mappings.hold_for_slow_)) {
      rate /= 4;
    }

    if (is_halt) {
      rate = 0;
    } else if (is_slow) {
      rate /= 4;
    }

    GameLoop::SetUpdateRate(rate);
  }

  bool centering_needs_update = image_scale_current != image_scale_destination;
  fapproach(image_scale_current, image_scale_destination, delta_time);
  if (centering_needs_update) {
    DISPLAY->ChangeCentering(
        PREFSMAN->center_image_translate_x_,
        PREFSMAN->center_image_translate_y_,
        PREFSMAN->center_image_add_width_ - (int)SCREEN_WIDTH +
            (int)(image_scale_current * SCREEN_WIDTH),
        PREFSMAN->center_image_add_height_ - (int)SCREEN_HEIGHT +
            (int)(image_scale_current * SCREEN_HEIGHT));
  }

  Screen::Update(delta_time);

  this->SetVisible(is_displayed && !forced_hidden_);
  if (!is_displayed) {
    return;
  }

  UpdateText();
}

void ScreenDebugOverlay::UpdateText() {
  auto start = pages_.begin();
  for (std::vector<std::string>::const_iterator s = pages_.begin();
       s != pages_.end(); ++s) {
    int page = s - start;
    text_pages_[page]->PlayCommand(
        (page == current_page_) ? "GainFocus" : "LoseFocus");
  }

  // todo: allow changing of various spacing/location things -aj
  int offset = 0;
  auto sub_start = subscribers->begin();
  for (std::vector<IDebugLine*>::const_iterator p = sub_start;
       p != subscribers->end(); ++p) {
    std::string page_name = (*p)->GetPageName();

    int i = p - sub_start;

    float y = LINE_START_Y + offset * LINE_SPACING;

    BitmapText& txt1 = *text_button_[i];
    BitmapText& txt2 = *text_function_[i];
    if (page_name != GetCurrentPageName()) {
      txt1.SetVisible(false);
      txt2.SetVisible(false);
      continue;
    }
    txt1.SetVisible(true);
    txt2.SetVisible(true);
    ++offset;

    txt1.SetX(LINE_BUTTON_X);
    txt1.SetY(y);

    txt2.SetX(LINE_FUNCTION_X);
    txt2.SetY(y);

    std::string s1 = (*p)->GetDisplayTitle();
    std::string s2 = (*p)->GetDisplayValue();

    bool on = (*p)->IsEnabled();

    txt1.SetDiffuse(on ? LINE_ON_COLOR : LINE_OFF_COLOR);
    txt2.SetDiffuse(on ? LINE_ON_COLOR : LINE_OFF_COLOR);

    std::string button = GetDebugButtonName(*p);
    if (!button.empty()) {
      button += ": ";
    }
    txt1.SetText(button);
    if (!s2.empty()) {
      s1 += " - ";
    }
    txt2.SetText(s1 + s2);
  }

  if (is_halt) {
    /* More than once I've paused the game accidentally and wasted time
     * figuring out why, so warn. */
    if (halt_timer.Ago() >= 5.0f) {
      halt_timer.Touch();
      LOG->Warn("Game halted");
    }
  }
}

template <typename U, typename V>
static bool GetValueFromMap(const std::map<U, V>& m, const U& key, V& val) {
  typename std::map<U, V>::const_iterator it = m.find(key);
  if (it == m.end()) {
    return false;
  }
  val = it->second;
  return true;
}

bool ScreenDebugOverlay::Input(const InputEventPlus& input) {
  if (!enable_debug_menu) {
    return Screen::Input(input);
  }

  if (input.device_i_ == mappings.hold_for_debug1_ ||
      input.device_i_ == mappings.hold_for_debug2_) {
    bool holding_neither =
        (!mappings.hold_for_debug1_.IsValid() ||
         !INPUTFILTER->IsBeingPressed(mappings.hold_for_debug1_)) &&
        (!mappings.hold_for_debug2_.IsValid() ||
         !INPUTFILTER->IsBeingPressed(mappings.hold_for_debug2_));
    bool holding_both =
        (!mappings.hold_for_debug1_.IsValid() ||
         INPUTFILTER->IsBeingPressed(mappings.hold_for_debug1_)) &&
        (!mappings.hold_for_debug2_.IsValid() ||
         INPUTFILTER->IsBeingPressed(mappings.hold_for_debug2_));
    if (holding_neither) {
      forced_hidden_ = false;
    }

    if (holding_both) {
      is_displayed = true;
    } else {
      is_displayed = false;
    }
  }
  if (input.device_i_ == mappings.toggle_mute_) {
    PREFSMAN->mute_actions_.Set(!PREFSMAN->mute_actions_);
    SCREENMAN->SystemMessage(
        PREFSMAN->mute_actions_ ? MUTE_ACTIONS_ON.GetValue()
                                : MUTE_ACTIONS_OFF.GetValue());
  }

  int page = 0;
  if (is_displayed &&
      GetValueFromMap(mappings.page_button_, input.device_i_, page)) {
    if (input.type_ != IET_FIRST_PRESS) {
      return true;  // eat the input but do nothing
    }
    current_page_ = page;
    rage_clamp(current_page_, 0, (int)pages_.size() - 1);
    return true;
  }

  auto start = subscribers->begin();
  for (std::vector<IDebugLine*>::const_iterator p = start;
       p != subscribers->end(); ++p) {
    std::string page_name = (*p)->GetPageName();

    int i = p - start;

    // Gameplay buttons are available only in gameplay. Non-gameplay buttons
    // are only available when the screen is displayed.
    IDebugLine::Type type = (*p)->GetType();
    switch (type) {
      case IDebugLine::all_screens:
        if (!is_displayed) {
          continue;
        }
        if (page_name != GetCurrentPageName()) {
          continue;
        }
        break;
      case IDebugLine::gameplay_only:
        if (!IsGameplay()) {
          continue;
        }
        break;
      default:
        FAIL_M(ssprintf("Invalid debug line type: %i", type));
    }

    if (input.device_i_ == (*p)->button_) {
      if (input.type_ != IET_FIRST_PRESS) {
        return true;  // eat the input but do nothing
      }

      // do the action
      std::string message;
      (*p)->DoAndLog(message);
      if (!message.empty()) {
        LOG->Trace("DEBUG: %s", message.c_str());
      }
      if ((*p)->ForceOffAfterUse()) {
        forced_hidden_ = true;
      }

      // update text to show the effect of what changed above
      UpdateText();

      // show what changed
      BitmapText& bt = *text_button_[i];
      bt.FinishTweening();
      bt.PlayCommand("Toggled");

      return true;
    }
  }

  return Screen::Input(input);
}

// DebugLine helpers
static void SetSpeed() {
  // PauseMusic( g_bIsHalt );
}

void ChangeVolume(float delta) {
  Preference<float>* ret =
      Preference<float>::GetPreferenceByName("SoundVolume");
  float vol = ret->Get();
  vol += delta;
  rage_clamp(vol, 0.0f, 1.0f);
  ret->Set(vol);
  SOUNDMAN->SetMixVolume();
}

void ChangeVisualDelay(float delta) {
  Preference<float>* ret =
      Preference<float>::GetPreferenceByName("VisualDelaySeconds");
  float secs = ret->Get();
  secs += delta;
  rage_clamp(secs, -1.0f, 1.0f);
  ret->Set(secs);
}

// DebugLines
static LocalizedString AUTO_PLAY("ScreenDebugOverlay", "AutoPlay");
static LocalizedString ASSIST("ScreenDebugOverlay", "Assist");
static LocalizedString AUTOSYNC("ScreenDebugOverlay", "Autosync");
static LocalizedString COIN_MODE("ScreenDebugOverlay", "CoinMode");
static LocalizedString HALT("ScreenDebugOverlay", "Halt");
static LocalizedString LIGHTS_DEBUG("ScreenDebugOverlay", "Lights Debug");
static LocalizedString RENDERING_STATS("ScreenDebugOverlay", "Rendering Stats");
static LocalizedString VSYNC("ScreenDebugOverlay", "Vsync");
static LocalizedString MULTITEXTURE("ScreenDebugOverlay", "Multitexture");
static LocalizedString SCREEN_SHOW_MASKS("ScreenDebugOverlay", "Show Masks");
static LocalizedString PROFILE("ScreenDebugOverlay", "Profile");
static LocalizedString SEND_NOTES_ENDED(
    "ScreenDebugOverlay", "Send Notes Ended");
static LocalizedString RESET_KEY_MAP(
    "ScreenDebugOverlay", "Reset key mapping to default");
static LocalizedString MUTE_ACTIONS("ScreenDebugOverlay", "Mute actions");
static LocalizedString RELOAD("ScreenDebugOverlay", "Reload");
static LocalizedString RESTART("ScreenDebugOverlay", "Restart");
static LocalizedString SCREEN_ON("ScreenDebugOverlay", "Send On To Screen");
static LocalizedString SCREEN_OFF("ScreenDebugOverlay", "Send Off To Screen");
static LocalizedString RELOAD_OVERLAY_SCREENS(
    "ScreenDebugOverlay", "Reload Overlay Screens");
static LocalizedString TOGGLE_ERRORS("ScreenDebugOverlay", "Toggle Errors");
static LocalizedString SHOW_RECENT_ERRORS(
    "ScreenDebugOverlay", "Show Recent Errors");
static LocalizedString CLEAR_ERRORS("ScreenDebugOverlay", "Clear Errors");
static LocalizedString CONVERT_XML("ScreenDebugOverlay", "Convert XML");
static LocalizedString RELOAD_PREFS("ScreenDebugOverlay", "Reload Prefs");
static LocalizedString RELOAD_THEME_AND_TEXTURES(
    "ScreenDebugOverlay", "Reload Theme and Textures");
static LocalizedString WRITE_PROFILES("ScreenDebugOverlay", "Write Profiles");
static LocalizedString WRITE_PREFERENCES(
    "ScreenDebugOverlay", "Write Preferences");
static LocalizedString MENU_TIMER("ScreenDebugOverlay", "Menu Timer");
static LocalizedString FLUSH_LOG("ScreenDebugOverlay", "Flush Log");
static LocalizedString PULL_BACK_CAMERA(
    "ScreenDebugOverlay", "Pull Back Camera");
static LocalizedString VISUAL_DELAY_UP("ScreenDebugOverlay", "Visual Delay Up");
static LocalizedString VISUAL_DELAY_DOWN(
    "ScreenDebugOverlay", "Visual Delay Down");
static LocalizedString VOLUME_UP("ScreenDebugOverlay", "Volume Up");
static LocalizedString VOLUME_DOWN("ScreenDebugOverlay", "Volume Down");
static LocalizedString UPTIME("ScreenDebugOverlay", "Uptime");
static LocalizedString SLOW("ScreenDebugOverlay", "Slow");
static LocalizedString CPU("ScreenDebugOverlay", "CPU");
static LocalizedString SONG("ScreenDebugOverlay", "Song");
static LocalizedString MACHINE("ScreenDebugOverlay", "Machine");
static LocalizedString SYNC_TEMPO("ScreenDebugOverlay", "Tempo");

class DebugLineAutoplay : public IDebugLine {
  virtual std::string GetDisplayTitle() {
    return AUTO_PLAY.GetValue() + " (+Shift = AI)";
  }
  virtual std::string GetDisplayValue() {
    PlayerController pc = GamePreferences::auto_play.Get();
    switch (pc) {
      case PC_HUMAN:
        return OFF.GetValue();
        break;
      case PC_AUTOPLAY:
        return ON.GetValue();
        break;
      case PC_CPU:
        return CPU.GetValue();
        break;
      default:
        FAIL_M(ssprintf("Invalid PlayerController: %i", pc));
    }
  }
  virtual Type GetType() const { return IDebugLine::gameplay_only; }
  virtual bool IsEnabled() {
    return GamePreferences::auto_play.Get() != PC_HUMAN;
  }
  virtual void DoAndLog(std::string& message_out) {
    ASSERT(GAMESTATE->GetMasterPlayerNumber() != kPlayerInvalid);
    PlayerController pc =
        GAMESTATE->player_state_[GAMESTATE->GetMasterPlayerNumber()]
            ->player_controller_;
    bool holding_shift =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));
    if (holding_shift) {
      pc = (pc == PC_CPU) ? PC_HUMAN : PC_CPU;
    } else {
      pc = (pc == PC_AUTOPLAY) ? PC_HUMAN : PC_AUTOPLAY;
    }
    GamePreferences::auto_play.Set(pc);
    FOREACH_HumanPlayer(p) GAMESTATE->player_state_[p]->player_controller_ =
        GamePreferences::auto_play;

    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineAssist : public IDebugLine {
  virtual std::string GetDisplayTitle() { return ASSIST.GetValue(); }
  virtual Type GetType() const { return gameplay_only; }
  virtual std::string GetDisplayValue() {
    SongOptions so;
    so.assist_clap_ = GAMESTATE->song_options_.GetSong().assist_clap_;
    so.assist_metronome_ = GAMESTATE->song_options_.GetSong().assist_metronome_;
    if (so.assist_clap_ || so.assist_metronome_) {
      return so.GetLocalizedString();
    } else {
      return OFF.GetValue();
    }
  }
  virtual bool IsEnabled() {
    return GAMESTATE->song_options_.GetSong().assist_clap_ ||
           GAMESTATE->song_options_.GetSong().assist_metronome_;
  }
  virtual void DoAndLog(std::string& message_out) {
    ASSERT(GAMESTATE->GetMasterPlayerNumber() != kPlayerInvalid);
    bool holding_shift =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT));
    bool b;
    if (holding_shift) {
      b = !GAMESTATE->song_options_.GetSong().assist_metronome_;
    } else {
      b = !GAMESTATE->song_options_.GetSong().assist_clap_;
    }
    if (holding_shift) {
      SO_GROUP_ASSIGN(
          GAMESTATE->song_options_, ModsLevel_Preferred, assist_metronome_, b);
    } else {
      SO_GROUP_ASSIGN(
          GAMESTATE->song_options_, ModsLevel_Preferred, assist_clap_, b);
    }

    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineAutosync : public IDebugLine {
  virtual std::string GetDisplayTitle() { return AUTOSYNC.GetValue(); }
  virtual std::string GetDisplayValue() {
    AutosyncType type = GAMESTATE->song_options_.GetSong().autosync_type_;
    switch (type) {
      case AutosyncType_Off:
        return OFF.GetValue();
        break;
      case AutosyncType_Song:
        return SONG.GetValue();
        break;
      case AutosyncType_Machine:
        return MACHINE.GetValue();
        break;
      case AutosyncType_Tempo:
        return SYNC_TEMPO.GetValue();
        break;
      default:
        FAIL_M(ssprintf("Invalid autosync type: %i", type));
    }
  }
  virtual Type GetType() const { return IDebugLine::gameplay_only; }
  virtual bool IsEnabled() {
    return GAMESTATE->song_options_.GetSong().autosync_type_ !=
           AutosyncType_Off;
  }
  virtual void DoAndLog(std::string& message_out) {
    int as = GAMESTATE->song_options_.GetSong().autosync_type_ + 1;
    bool allow_song_autosync = !GAMESTATE->IsCourseMode();
    if (!allow_song_autosync &&
        (as == AutosyncType_Song || as == AutosyncType_Tempo)) {
      as = AutosyncType_Machine;
    }
    wrap(as, NUM_AutosyncType);
    SO_GROUP_ASSIGN(
        GAMESTATE->song_options_, ModsLevel_Song, autosync_type_,
        AutosyncType(as));
    MESSAGEMAN->Broadcast(Message_AutosyncChanged);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineCoinMode : public IDebugLine {
  virtual std::string GetDisplayTitle() { return COIN_MODE.GetValue(); }
  virtual std::string GetDisplayValue() {
    return CoinModeToString(GAMESTATE->GetCoinMode());
  }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    if (GAMESTATE->GetCoinMode() == CoinMode_Home) {
      GamePreferences::coin_mode.Set(CoinMode_Free);
    } else if (
        GAMESTATE->GetCoinMode() == CoinMode_Free &&
        !GAMESTATE->IsEventMode()) {
      GamePreferences::coin_mode.Set(CoinMode_Pay);
    } else {
      GamePreferences::coin_mode.Set(CoinMode_Home);
    }
    SCREENMAN->RefreshCreditsMessages();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineSlow : public IDebugLine {
  virtual std::string GetDisplayTitle() { return SLOW.GetValue(); }
  virtual bool IsEnabled() { return is_slow; }
  virtual void DoAndLog(std::string& message_out) {
    is_slow = !is_slow;
    SetSpeed();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineHalt : public IDebugLine {
  virtual std::string GetDisplayTitle() { return HALT.GetValue(); }
  virtual bool IsEnabled() { return is_halt; }
  virtual void DoAndLog(std::string& message_out) {
    is_halt = !is_halt;
    halt_timer.Touch();
    SetSpeed();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineLightsDebug : public IDebugLine {
  virtual std::string GetDisplayTitle() { return LIGHTS_DEBUG.GetValue(); }
  virtual bool IsEnabled() { return PREFSMAN->debug_lights_.Get(); }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->debug_lights_.Set(!PREFSMAN->debug_lights_);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineStats : public IDebugLine {
  virtual std::string GetDisplayTitle() { return RENDERING_STATS.GetValue(); }
  virtual bool IsEnabled() { return PREFSMAN->show_stats_.Get(); }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->show_stats_.Set(!PREFSMAN->show_stats_);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineVsync : public IDebugLine {
  virtual std::string GetDisplayTitle() { return VSYNC.GetValue(); }
  virtual bool IsEnabled() { return PREFSMAN->vsync_.Get(); }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->vsync_.Set(!PREFSMAN->vsync_);
    StepMania::ApplyGraphicOptions();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineAllowMultitexture : public IDebugLine {
  virtual std::string GetDisplayTitle() { return MULTITEXTURE.GetValue(); }
  virtual bool IsEnabled() { return PREFSMAN->allow_multitexture_.Get(); }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->allow_multitexture_.Set(!PREFSMAN->allow_multitexture_);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineShowMasks : public IDebugLine {
  virtual std::string GetDisplayTitle() { return SCREEN_SHOW_MASKS.GetValue(); }
  virtual bool IsEnabled() { return GetPref()->Get(); }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    GetPref()->Set(!GetPref()->Get());
    IDebugLine::DoAndLog(message_out);
  }

  Preference<bool>* GetPref() {
    return Preference<bool>::GetPreferenceByName("ShowMasks");
  }
};

static ProfileSlot profile_slot = ProfileSlot_Machine;
static bool IsSelectProfilePersistent() {
  if (profile_slot == ProfileSlot_Machine) {
    return true;
  }
  return PROFILEMAN->IsPersistentProfile((PlayerNumber)profile_slot);
}

class DebugLineProfileSlot : public IDebugLine {
  virtual std::string GetDisplayTitle() { return PROFILE.GetValue(); }
  virtual std::string GetDisplayValue() {
    switch (profile_slot) {
      case ProfileSlot_Machine:
        return "Machine";
      case ProfileSlot_Player1:
        return "Player 1";
      case ProfileSlot_Player2:
        return "Player 2";
      default:
        return std::string();
    }
  }
  virtual bool IsEnabled() { return IsSelectProfilePersistent(); }
  virtual std::string GetPageName() const { return "Profiles"; }
  virtual void DoAndLog(std::string& message_out) {
    enum_add(profile_slot, +1);
    if (profile_slot == NUM_ProfileSlot) {
      profile_slot = ProfileSlot_Player1;
    }

    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineSendNotesEnded : public IDebugLine {
  virtual std::string GetDisplayTitle() { return SEND_NOTES_ENDED.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    SCREENMAN->PostMessageToTopScreen(SM_NotesEnded, 0);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineResetKeyMapping : public IDebugLine {
  virtual std::string GetDisplayTitle() { return RESET_KEY_MAP.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    INPUTMAPPER->ResetMappingsToDefault();
    INPUTMAPPER->SaveMappingsToDisk();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineMuteActions : public IDebugLine {
  virtual std::string GetDisplayTitle() { return MUTE_ACTIONS.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return PREFSMAN->mute_actions_; }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->mute_actions_.Set(!PREFSMAN->mute_actions_);
    SCREENMAN->SystemMessage(
        PREFSMAN->mute_actions_ ? MUTE_ACTIONS_ON.GetValue()
                                : MUTE_ACTIONS_OFF.GetValue());
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineReloadCurrentScreen : public IDebugLine {
  virtual std::string GetDisplayTitle() { return RELOAD.GetValue(); }
  virtual std::string GetDisplayValue() {
    return SCREENMAN && SCREENMAN->GetTopScreen()
               ? SCREENMAN->GetTopScreen()->GetName()
               : std::string();
  }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    std::string screen_name = SCREENMAN->GetScreen(0)->GetName();
    SCREENMAN->PopAllScreens();

    SOUND->StopMusic();
    // StepMania::ResetGame();

    SCREENMAN->SetNewScreen(screen_name);
    IDebugLine::DoAndLog(message_out);
    message_out = "";
  }
};

class DebugLineRestartCurrentScreen : public IDebugLine {
  virtual std::string GetDisplayTitle() { return RESTART.GetValue(); }
  virtual std::string GetDisplayValue() {
    return SCREENMAN && SCREENMAN->GetTopScreen()
               ? SCREENMAN->GetTopScreen()->GetName()
               : std::string();
  }
  virtual bool IsEnabled() { return true; }
  virtual bool ForceOffAfterUse() const { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    SCREENMAN->GetTopScreen()->BeginScreen();
    IDebugLine::DoAndLog(message_out);
    message_out = "";
  }
};

class DebugLineCurrentScreenOn : public IDebugLine {
  virtual std::string GetDisplayTitle() { return SCREEN_ON.GetValue(); }
  virtual std::string GetDisplayValue() {
    return SCREENMAN && SCREENMAN->GetTopScreen()
               ? SCREENMAN->GetTopScreen()->GetName()
               : std::string();
  }
  virtual bool IsEnabled() { return true; }
  virtual bool ForceOffAfterUse() const { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    SCREENMAN->GetTopScreen()->PlayCommand("On");
    IDebugLine::DoAndLog(message_out);
    message_out = "";
  }
};

class DebugLineCurrentScreenOff : public IDebugLine {
  virtual std::string GetDisplayTitle() { return SCREEN_OFF.GetValue(); }
  virtual std::string GetDisplayValue() {
    return SCREENMAN && SCREENMAN->GetTopScreen()
               ? SCREENMAN->GetTopScreen()->GetName()
               : std::string();
  }
  virtual bool IsEnabled() { return true; }
  virtual bool ForceOffAfterUse() const { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    SCREENMAN->GetTopScreen()->PlayCommand("Off");
    IDebugLine::DoAndLog(message_out);
    message_out = "";
  }
};

class DebugLineReloadTheme : public IDebugLine {
  virtual std::string GetDisplayTitle() {
    return RELOAD_THEME_AND_TEXTURES.GetValue();
  }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    THEME->ReloadMetrics();
    TEXTUREMAN->ReloadAll();
    NOTESKIN->RefreshNoteSkinData(GAMESTATE->cur_game_);
    CodeDetector::RefreshCacheItems();
    // HACK: Don't update text below. Return immediately because this screen
    // was just destroyed as part of the theme reload.
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineReloadOverlayScreens : public IDebugLine {
  virtual std::string GetDisplayTitle() {
    return RELOAD_OVERLAY_SCREENS.GetValue();
  }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    SCREENMAN->ReloadOverlayScreensAfterInputFinishes();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineToggleErrors : public IDebugLine {
  virtual std::string GetDisplayTitle() { return TOGGLE_ERRORS.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return PREFSMAN->show_theme_errors_; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->show_theme_errors_.Set(!PREFSMAN->show_theme_errors_);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineShowRecentErrors : public IDebugLine {
  virtual std::string GetDisplayTitle() {
    return SHOW_RECENT_ERRORS.GetValue();
  }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    Message msg("ToggleScriptError");
    MESSAGEMAN->Broadcast(msg);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineClearErrors : public IDebugLine {
  virtual std::string GetDisplayTitle() { return CLEAR_ERRORS.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    Message msg("ClearScriptError");
    MESSAGEMAN->Broadcast(msg);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineConvertXML : public IDebugLine {
  virtual std::string GetDisplayTitle() { return CONVERT_XML.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Theme"; }
  virtual void DoAndLog(std::string& message_out) {
    Song* cur_song = GAMESTATE->cur_song_;
    if (cur_song) {
      convert_xmls_in_dir(cur_song->GetSongDir() + "/");
      IDebugLine::DoAndLog(message_out);
    }
  }
};

class DebugLineWriteProfiles : public IDebugLine {
  virtual std::string GetDisplayTitle() { return WRITE_PROFILES.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return IsSelectProfilePersistent(); }
  virtual std::string GetPageName() const { return "Profiles"; }
  virtual void DoAndLog(std::string& message_out) {
    // Also save bookkeeping and profile info for debugging
    // so we don't have to play through a whole song to get new output.
    if (profile_slot == ProfileSlot_Machine) {
      GAMESTATE->SaveLocalData();
    } else {
      PlayerNumber pn = (PlayerNumber)profile_slot;
      GAMESTATE->SaveCurrentSettingsToProfile(pn);
      GAMESTATE->SavePlayerProfile(pn);
    }
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineWritePreferences : public IDebugLine {
  virtual std::string GetDisplayTitle() { return WRITE_PREFERENCES.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->SavePrefsToDisk();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineReloadPreferences : public IDebugLine {
  virtual std::string GetDisplayTitle() { return RELOAD_PREFS.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual std::string GetPageName() const { return "Profiles"; }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->ReadPrefsFromDisk();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineMenuTimer : public IDebugLine {
  virtual std::string GetDisplayTitle() { return MENU_TIMER.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return PREFSMAN->menu_timer_.Get(); }
  virtual void DoAndLog(std::string& message_out) {
    PREFSMAN->menu_timer_.Set(!PREFSMAN->menu_timer_);
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineFlushLog : public IDebugLine {
  virtual std::string GetDisplayTitle() { return FLUSH_LOG.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    LOG->Flush();
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLinePullBackCamera : public IDebugLine {
  virtual std::string GetDisplayTitle() { return PULL_BACK_CAMERA.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return image_scale_destination != 1; }
  virtual void DoAndLog(std::string& message_out) {
    if (image_scale_destination == 1) {
      image_scale_destination = 0.5f;
    } else {
      image_scale_destination = 1;
    }
    IDebugLine::DoAndLog(message_out);
  }
};

class DebugLineVolumeUp : public IDebugLine {
  virtual std::string GetDisplayTitle() { return VOLUME_UP.GetValue(); }
  virtual std::string GetDisplayValue() {
    return ssprintf("%.0f%%", GetPref()->Get() * 100);
  }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    ChangeVolume(+0.1f);
    IDebugLine::DoAndLog(message_out);
  }
  Preference<float>* GetPref() {
    return Preference<float>::GetPreferenceByName("SoundVolume");
  }
};

class DebugLineVolumeDown : public IDebugLine {
  virtual std::string GetDisplayTitle() { return VOLUME_DOWN.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    ChangeVolume(-0.1f);
    IDebugLine::DoAndLog(message_out);
    message_out += " - " + ssprintf("%.0f%%", GetPref()->Get() * 100);
  }
  Preference<float>* GetPref() {
    return Preference<float>::GetPreferenceByName("SoundVolume");
  }
};

class DebugLineVisualDelayUp : public IDebugLine {
  virtual std::string GetDisplayTitle() { return VISUAL_DELAY_UP.GetValue(); }
  virtual std::string GetDisplayValue() {
    return ssprintf("%.03f", GetPref()->Get());
  }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    ChangeVisualDelay(+0.001f);
    IDebugLine::DoAndLog(message_out);
  }
  Preference<float>* GetPref() {
    return Preference<float>::GetPreferenceByName("VisualDelaySeconds");
  }
};

class DebugLineVisualDelayDown : public IDebugLine {
  virtual std::string GetDisplayTitle() { return VISUAL_DELAY_DOWN.GetValue(); }
  virtual std::string GetDisplayValue() { return std::string(); }
  virtual bool IsEnabled() { return true; }
  virtual void DoAndLog(std::string& message_out) {
    ChangeVisualDelay(-0.001f);
    IDebugLine::DoAndLog(message_out);
    message_out += " - " + ssprintf("%.03f", GetPref()->Get());
  }
  Preference<float>* GetPref() {
    return Preference<float>::GetPreferenceByName("VisualDelaySeconds");
  }
};

class DebugLineUptime : public IDebugLine {
  virtual std::string GetDisplayTitle() { return UPTIME.GetValue(); }
  virtual std::string GetDisplayValue() {
    return MicrosecondsToMMSSMsMsMs(RageTimer::GetTimeSinceStartMicroseconds());
  }
  virtual bool IsEnabled() { return false; }
  virtual void DoAndLog(std::string& message_out) {}
};

/* If you comment out a DECLARE_ONE at the end of the file, it will remove that
 * debug menu line, but it will also change the arrangement of keys to debug
 * menu options. We need a way to generate fake classes in order to preserve the
 * Debug Menu key mappings, so we can instead swap in one of these fake classes
 * for the class we want to remove. DEFINE_FAKE_CLASS1 handles menu options
 * which need to return a page title, and DEFINE_FAKE_CLASS2 handles the normal
 * (F5) menu options. */
#define DEFINE_FAKE_CLASS1(className, pageName)                    \
  class className : public IDebugLine {                            \
   public:                                                         \
    virtual std::string GetDisplayTitle() { return "(disabled)"; } \
    virtual std::string GetDisplayValue() { return "(disabled)"; } \
    virtual bool IsEnabled() { return false; }                     \
    virtual std::string GetPageName() const { return pageName; }   \
    virtual void DoAndLog(std::string& sMessageOut) {              \
      sMessageOut = "(disabled)";                                  \
    }                                                              \
  };

#define DEFINE_FAKE_CLASS2(className)                              \
  class className : public IDebugLine {                            \
   public:                                                         \
    virtual std::string GetDisplayTitle() { return "(disabled)"; } \
    virtual bool IsEnabled() { return false; }                     \
    virtual void DoAndLog(std::string& sMessageOut) {              \
      sMessageOut = "(disabled)";                                  \
    }                                                              \
  };

DEFINE_FAKE_CLASS1(FakeClearProfileStats, "Profiles")
DEFINE_FAKE_CLASS1(FakeFillProfileStats, "Profiles")
DEFINE_FAKE_CLASS2(FakeDebugLine1)
DEFINE_FAKE_CLASS2(FakeDebugLine2)

#define DECLARE_ONE(x) static x g_##x
DECLARE_ONE(DebugLineAutoplay);
DECLARE_ONE(DebugLineAssist);
DECLARE_ONE(DebugLineAutosync);
DECLARE_ONE(DebugLineCoinMode);
DECLARE_ONE(DebugLineSlow);
DECLARE_ONE(DebugLineHalt);
DECLARE_ONE(DebugLineLightsDebug);
DECLARE_ONE(FakeDebugLine1);
DECLARE_ONE(DebugLineStats);
DECLARE_ONE(DebugLineVsync);
DECLARE_ONE(DebugLineAllowMultitexture);
DECLARE_ONE(DebugLineShowMasks);
DECLARE_ONE(DebugLineProfileSlot);
DECLARE_ONE(FakeClearProfileStats);
DECLARE_ONE(FakeFillProfileStats);
DECLARE_ONE(DebugLineSendNotesEnded);
DECLARE_ONE(DebugLineReloadCurrentScreen);
DECLARE_ONE(DebugLineRestartCurrentScreen);
DECLARE_ONE(DebugLineCurrentScreenOn);
DECLARE_ONE(DebugLineCurrentScreenOff);
DECLARE_ONE(DebugLineReloadTheme);
DECLARE_ONE(DebugLineReloadOverlayScreens);
DECLARE_ONE(DebugLineToggleErrors);
DECLARE_ONE(DebugLineShowRecentErrors);
DECLARE_ONE(DebugLineClearErrors);
DECLARE_ONE(DebugLineConvertXML);
DECLARE_ONE(DebugLineWriteProfiles);
DECLARE_ONE(DebugLineWritePreferences);
DECLARE_ONE(DebugLineReloadPreferences);
DECLARE_ONE(DebugLineMenuTimer);
DECLARE_ONE(DebugLineFlushLog);
DECLARE_ONE(DebugLinePullBackCamera);
DECLARE_ONE(DebugLineVolumeDown);
DECLARE_ONE(DebugLineVolumeUp);
DECLARE_ONE(DebugLineVisualDelayDown);
DECLARE_ONE(DebugLineVisualDelayUp);
DECLARE_ONE(FakeDebugLine2);  // force crash
DECLARE_ONE(DebugLineUptime);
DECLARE_ONE(DebugLineResetKeyMapping);
DECLARE_ONE(DebugLineMuteActions);

/*
 * (c) 2001-2005 Chris Danford, Glenn Maynard
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
