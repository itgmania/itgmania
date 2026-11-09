/*
 * We maintain a pool of "prepared" screens, which are screens previously loaded
 * to be available on demand.
 *
 * When a screen pops off the stack that's in g_setPersistantScreens, it goes to
 * the prepared list.  If that screen is used again before it's deleted, it'll
 * be reused.
 *
 * Typically, the first screen in a group will prepare all of the nearby screens
 * it may load.  When it loads one, the prepared screen will be used.
 *
 * A group of screens may only want to preload some screens, and not others,
 * while still considering those screens part of the same group.  For example,
 * an attract loop typically has several very lightweight screens and a few
 * expensive screens.  Preloading the lightweight screens can improve
 * responsiveness, but preloading the expensive screens may use too much memory
 * and take too long to load all at once.  By calling GroupScreen(), entering
 * these screens will not trigger cleanup.
 *
 * Example uses:
 *  - ScreenOptions1 preloads ScreenOptions2, and persists both.  Moving from
 * Options1 and Options2 and back is instant and reuses both.
 *  - ScreenMenu groups and persists itself and ScreenSubmenu.  ScreenSubmenu
 *    is not preloaded, so it will be loaded on demand the first time it's used,
 *    but will remain loaded if it returns to ScreenMenu.
 *  - ScreenSelectMusic groups itself and ScreenPlayerOptions, and persists only
 *    ScreenSelectMusic.  This will cause SSMusic will be loaded once, and SPO
 * to be loaded on demand.
 *  - ScreenAttract1 preloads and persists ScreenAttract1, 3, 5 and 7, and
 * groups 1 through 7.  1, 3, 5 and 7 will remain in memory; the rest will be
 * loaded on demand.
 *
 * If a screen is added to the screen stack that isn't in the current screen
 * group (added to by GroupScreen), the screen group is reset: all prepared
 * screens are unloaded and the persistence list is cleared.
 *
 * Note that not all screens yet support reuse in this way; proper use of
 * BeginScreen is required.  This will misbehave if a screen pushes another
 * screen that's already in use lower on the stack, but that's not useful; it
 * would allow infinite screen recursion.
 *
 * SM_GainFocus/SM_LoseFocus: These are sent to screens when they become the
 * topmost screen, or stop being the topmost screen.
 *
 * A few subtleties:
 *
 * With delayed screens (eg. ScreenGameplay being pre-loaded by ScreenStage),
 * SM_GainFocus isn't sent until the loaded screen actually is activated (put on
 * the stack).
 *
 * With normal screen loads, the new screen is loaded before the old screen is
 * destroyed. This means that the old dtor is called *after* the new ctor.  If
 * some global properties (eg. GAMESTATE) are being unset by the old screen's
 * destructor, and set by the new screen's constructor, they'll happen in the
 * wrong order.  Use SM_GainFocus and SM_LoseFocus, instead.
 *
 * SM_LoseFocus is always sent after SM_GainFocus, and vice-versa: you can't
 * gain focus if you already have it, and you can't lose focus if you don't have
 * it.
 */

#include "ScreenManager.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameSoundManager.h"
#include "InputEventPlus.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageTextureManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenMessage.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "global.h"

ScreenManager* SCREENMAN =
    nullptr;  // global and accessible from anywhere in our program

static Preference<bool> delayed_screen_load("DelayedScreenLoad", false);
// static Preference<bool> g_bPruneFonts( "PruneFonts", true );

// Screen registration
static std::map<std::string, CreateScreenFn>* registrees = nullptr;

/** @brief Utility functions for the ScreenManager. */
namespace ScreenManagerUtil {
// in draw order first to last
struct LoadedScreen {
  Screen* screen_;

  /* Normally true. If false, the screen is owned by another screen
   * and was given to us for use, and it's not ours to free. */
  bool delete_when_done_;

  // m_input_redirected exists to allow the theme to prevent input being
  // passed to the normal Screen::Input function, on a per-player basis.
  // Input is still passed to lua callbacks, so it's intended for the case
  // where someone has a custom menu on a screen and needs to disable normal
  // input for navigating the custom menu to work. -Kyz
  bool input_redirected_[kNumPlayers];

  ScreenMessage send_on_pop_;

  LoadedScreen() {
    screen_ = nullptr;
    delete_when_done_ = true;
    ZERO(input_redirected_);
    send_on_pop_ = SM_None;
  }
};

Actor* shared_bga;  // BGA object that's persistent between screens
std::string previous_top_screen;
std::vector<LoadedScreen> screen_stack;  // bottommost to topmost
std::vector<Screen*> overlay_screens;
std::set<std::string> set_grouped_screens;
std::set<std::string> set_persistant_screens;

std::vector<LoadedScreen> prepared_screens;
std::vector<Actor*> prepared_backgrounds;

// Add a screen to g_ScreenStack. This is the only function that adds to
// g_ScreenStack.
void PushLoadedScreen(const LoadedScreen& ls) {
  LOG->Trace("PushScreen: \"%s\"", ls.screen_->GetName().c_str());
  LOG->MapLog(
      "ScreenManager::TopScreen", "Top Screen: %s",
      ls.screen_->GetName().c_str());

  // Be sure to push the screen first, so GetTopScreen returns the screen
  // during BeginScreen.
  screen_stack.push_back(ls);

  // Set the name of the loading screen.
  {
    LuaThreadVariable var1("PreviousScreen", previous_top_screen);
    LuaThreadVariable var2("LoadingScreen", ls.screen_->GetName());
    ls.screen_->BeginScreen();
  }

  // If this is the new top screen, save the name.
  if (screen_stack.size() == 1) {
    previous_top_screen = ls.screen_->GetName();
  }

  SCREENMAN->RefreshCreditsMessages();

  SCREENMAN->PostMessageToTopScreen(SM_GainFocus, 0);
}

bool ScreenIsPrepped(const std::string& screen_name) {
  return std::any_of(
      prepared_screens.begin(), prepared_screens.end(),
      [&](const LoadedScreen& s) {
        return s.screen_->GetName() == screen_name;
      });
}

/* If the named screen is loaded, remove it from the prepared list and
 * return it in ls. */
bool GetPreppedScreen(const std::string& screen_name, LoadedScreen& ls) {
  for (std::vector<LoadedScreen>::iterator s = prepared_screens.begin();
       s != prepared_screens.end(); ++s) {
    if (s->screen_->GetName() == screen_name) {
      ls = *s;
      prepared_screens.erase(s);
      return true;
    }
  }
  return false;
}

void BeforeDeleteScreen() {
  // Deleting a screen can take enough time to cause a frame skip.
  SCREENMAN->ZeroNextUpdate();
}

/* If we're deleting a screen, it's probably releasing texture and other
 * resources, so trigger cleanups. */
void AfterDeleteScreen() {
  /* Now that we've actually deleted a screen, it makes sense to clear out
   * cached textures. */
  TEXTUREMAN->DeleteCachedTextures();

  /* Cleanup song data. This can free up a fair bit of memory, so do it
   * after deleting screens. */
  SONGMAN->Cleanup();
}

/* Take ownership of all screens and backgrounds that are owned by
 * us (this excludes screens where m_bDeleteWhenDone is false).
 * Clear the prepared lists. The contents of apOut must be
 * freed by the caller. */
void GrabPreparedActors(std::vector<Actor*>& out) {
  for (const LoadedScreen& s : prepared_screens) {
    if (s.delete_when_done_) {
      out.push_back(s.screen_);
    }
  }
  prepared_screens.clear();
  for (Actor* a : prepared_backgrounds) {
    out.push_back(a);
  }
  prepared_backgrounds.clear();

  set_grouped_screens.clear();
  set_persistant_screens.clear();
}

/* Called when changing screen groups. Delete all prepared screens,
 * reset the screen group and list of persistant screens. */
void DeletePreparedScreens() {
  std::vector<Actor*> actors_to_delete;
  GrabPreparedActors(actors_to_delete);

  BeforeDeleteScreen();
  for (Actor* a : actors_to_delete) {
    RageUtil::SafeDelete(a);
  }
  AfterDeleteScreen();
}
};  // namespace ScreenManagerUtil
using namespace ScreenManagerUtil;

RegisterScreenClass::RegisterScreenClass(
    const std::string& class_name, CreateScreenFn pfn) {
  if (registrees == nullptr) {
    registrees = new std::map<std::string, CreateScreenFn>;
  }

  std::map<std::string, CreateScreenFn>::iterator iter =
      registrees->find(class_name);
  ASSERT_M(
      iter == registrees->end(),
      ssprintf("Screen class '%s' already registered.", class_name.c_str()));

  (*registrees)[class_name] = pfn;
}

ScreenManager::ScreenManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "SCREENMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  shared_bga = new Actor;

  reload_overlay_screens_after_input_ = false;
  zero_next_update_ = false;
  pop_top_screen_ = kSmInvalid;
}

ScreenManager::~ScreenManager() {
  LOG->Trace("ScreenManager::~ScreenManager()");
  LOG->UnmapLog("ScreenManager::TopScreen");

  RageUtil::SafeDelete(shared_bga);
  for (LoadedScreen& screen : screen_stack) {
    if (screen.delete_when_done_) {
      RageUtil::SafeDelete(screen.screen_);
    }
  }
  screen_stack.clear();
  DeletePreparedScreens();
  for (Screen* overlay_screen : overlay_screens) {
    RageUtil::SafeDelete(overlay_screen);
  }
  overlay_screens.clear();

  // Unregister with Lua.
  LUA->UnsetGlobal("SCREENMAN");
}

// This is called when we start up, and when the theme changes or is reloaded.
void ScreenManager::ThemeChanged() {
  LOG->Trace("ScreenManager::ThemeChanged");

  // reload common sounds
  sound_start_.Load(THEME->GetPathS("Common", "start"));
  sound_coin_.Load(THEME->GetPathS("Common", "coin"), true);
  sound_cancel_.Load(THEME->GetPathS("Common", "cancel"), true);
  sound_invalid_.Load(THEME->GetPathS("Common", "invalid"));
  sound_screenshot_.Load(THEME->GetPathS("Common", "screenshot"));

  // reload song manager colors (to avoid crashes) -aj
  SONGMAN->ResetGroupColors();

  ReloadOverlayScreens();

  // force recreate of new BGA
  RageUtil::SafeDelete(shared_bga);
  shared_bga = new Actor;
}

void ScreenManager::ReloadOverlayScreens() {
  // unload overlay screens
  for (Screen* screen : overlay_screens) {
    RageUtil::SafeDelete(screen);
  }
  overlay_screens.clear();

  // reload overlay screens
  std::string s_overlays = THEME->GetMetric("Common", "OverlayScreens");
  std::vector<std::string> as_overlays;
  split(s_overlays, ",", as_overlays);
  for (const std::string& overlay : as_overlays) {
    Screen* p_screen = MakeNewScreen(overlay);
    if (p_screen) {
      LuaThreadVariable var2("LoadingScreen", p_screen->GetName());
      p_screen->BeginScreen();
      overlay_screens.push_back(p_screen);
    }
  }

  this->RefreshCreditsMessages();
}

void ScreenManager::ReloadOverlayScreensAfterInputFinishes() {
  reload_overlay_screens_after_input_ = true;
}

Screen* ScreenManager::GetTopScreen() {
  if (screen_stack.empty()) {
    return nullptr;
  }
  return screen_stack[screen_stack.size() - 1].screen_;
}

Screen* ScreenManager::GetScreen(int position) {
  if (position >= (int)screen_stack.size()) {
    return nullptr;
  }
  return screen_stack[position].screen_;
}

bool ScreenManager::AllowOperatorMenuButton() const {
  return std::all_of(
      screen_stack.begin(), screen_stack.end(), [](const LoadedScreen& s) {
        return s.screen_->AllowOperatorMenuButton();
      });
}

bool ScreenManager::IsScreenNameValid(const std::string& name) const {
  if (name.empty() || !THEME->HasMetric(name, "Class")) {
    return false;
  }
  std::string class_name = THEME->GetMetric(name, "Class");
  return registrees->find(class_name) != registrees->end();
}

bool ScreenManager::IsStackedScreen(const Screen* screen) const {
  return std::any_of(
      screen_stack.begin() + 1, screen_stack.end(),
      [&](const LoadedScreen& s) { return s.screen_ == screen; });
}

bool ScreenManager::get_input_redirected(PlayerNumber pn) {
  if (screen_stack.empty() || pn < 0 || pn >= kNumPlayers) {
    return false;
  }
  return screen_stack.back().input_redirected_[pn];
}

void ScreenManager::set_input_redirected(PlayerNumber pn, bool redir) {
  if (!screen_stack.empty() && pn >= 0 && pn < kNumPlayers) {
    screen_stack.back().input_redirected_[pn] = redir;
  }
}

/* Pop the top screen off the stack, sending SM_LoseFocus messages and
 * returning the message the popped screen wants sent to the new top
 * screen. Does not send SM_GainFocus. */
ScreenMessage ScreenManager::PopTopScreenInternal(bool send_lose_focus) {
  if (screen_stack.empty()) {
    return SM_None;
  }

  LoadedScreen ls = screen_stack.back();
  screen_stack.erase(screen_stack.end() - 1, screen_stack.end());

  if (send_lose_focus) {
    ls.screen_->HandleScreenMessage(SM_LoseFocus);
  }
  ls.screen_->EndScreen();
  ZERO(ls.input_redirected_);

  if (set_persistant_screens.find(ls.screen_->GetName()) !=
      set_persistant_screens.end()) {
    // Move the screen back to the prepared list.
    prepared_screens.push_back(ls);
  } else {
    if (ls.delete_when_done_) {
      BeforeDeleteScreen();
      RageUtil::SafeDelete(ls.screen_);
      AfterDeleteScreen();
    }
  }

  if (screen_stack.size()) {
    LOG->MapLog(
        "ScreenManager::TopScreen", "Top Screen: %s",
        screen_stack.back().screen_->GetName().c_str());
  } else {
    LOG->UnmapLog("ScreenManager::TopScreen");
  }

  return ls.send_on_pop_;
}

void ScreenManager::Update(float delta_time) {
  // Pop the top screen, if PopTopScreen was called.
  if (pop_top_screen_ != kSmInvalid) {
    ScreenMessage sm = pop_top_screen_;
    pop_top_screen_ = kSmInvalid;

    ScreenMessage sm2 = PopTopScreenInternal();

    SendMessageToTopScreen(SM_GainFocus);
    SendMessageToTopScreen(sm);
    SendMessageToTopScreen(sm2);
  }

  /* Screens take some time to load.  If we don't do this, then screens
   * receive an initial update that includes all of the time they spent
   * loading, which will chop off their tweens.
   *
   * We don't want to simply cap update times; for example, the stage
   * screen sets a 4 second timer, preps the gameplay screen, and then
   * displays the prepped screen after the timer runs out; this lets the
   * load time be masked (as long as the load takes less than 4 seconds).
   * If we cap that large update delta from the screen load, the update
   * to load the new screen will come after 4 seconds plus the load time.
   *
   * So, let's just zero the first update for every screen. */
  ASSERT(
      !screen_stack.empty() ||
      delayed_screen_ != "");  // Why play the game if there is nothing showing?

  Screen* p_screen = screen_stack.empty() ? nullptr : GetTopScreen();

  bool first_update = p_screen && p_screen->IsFirstUpdate();

  /* Loading a new screen can take seconds and cause a big jump on the new
   * Screen's first update.  Clamp the first update delta so that the
   * animations don't jump. */
  if (p_screen && zero_next_update_) {
    LOG->Trace("Zeroing this update.  Was %f", delta_time);
    delta_time = 0;
    zero_next_update_ = false;
  }

  // Update screens.
  for (const LoadedScreen& screen : screen_stack) {
    screen.screen_->Update(delta_time);
  }

  shared_bga->Update(delta_time);

  for (Screen* overlay : overlay_screens) {
    overlay->Update(delta_time);
  }

  /* The music may be started on the first update. If we're reading from a CD,
   * it might not start immediately. Make sure we start playing the sound before
   * continuing, since it's strange to start rendering before the music starts.
   */
  if (first_update) {
    SOUND->Flush();
  }

  /* If we're currently inside a background screen load, and m_sDelayedScreen
   * is set, then the screen called SetNewScreen before we finished preparing.
   * Postpone it until we're finished loading. */
  if (delayed_screen_.size() != 0) {
    LoadDelayedScreen();
  }
}

void ScreenManager::Draw() {
  /* If it hasn't been updated yet, skip the render. We can't call Update(0),
   * since that'll confuse the "zero out the next update after loading a screen
   * logic. If we don't render, don't call BeginFrame or EndFrame. That way, we
   * won't clear the buffer, and we won't wait for vsync. */
  if (screen_stack.size() && screen_stack.back().screen_->IsFirstUpdate()) {
    return;
  }

  if (!DISPLAY->BeginFrame()) {
    return;
  }

  DISPLAY->CameraPushMatrix();
  DISPLAY->LoadMenuPerspective(
      0, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_CENTER_X, SCREEN_CENTER_Y);
  shared_bga->Draw();
  DISPLAY->CameraPopMatrix();

  for (const LoadedScreen& screen :
       screen_stack) {  // Draw all screens bottom to top
    screen.screen_->Draw();
  }

  for (Screen* overlay_screen : overlay_screens) {
    overlay_screen->Draw();
  }

  DISPLAY->EndFrame();
}

void ScreenManager::Input(const InputEventPlus& input) {
  //	LOG->Trace( "ScreenManager::Input( %d-%d, %d-%d, %d-%d, %d-%d )",
  //		DeviceI.device, DeviceI.button, GameI.controller, GameI.button,
  // MenuI.player, MenuI.button, StyleI.player, StyleI.col );

  // First, give overlay screens a shot at the input.  If Input returns
  // true, it handled the input, so don't pass it further.
  for (Screen* screen : overlay_screens) {
    bool handled = screen->Input(input);
    // Pass input to the screen and lua. Contention shouldn't be a problem
    // because anybody setting an input callback is probably doing it to
    // do something in addition to whatever the screen does.
    if (screen->PassInputToLua(input) || handled) {
      if (reload_overlay_screens_after_input_) {
        ReloadOverlayScreens();
        reload_overlay_screens_after_input_ = false;
      }
      return;
    }
  }

  // Pass input to the topmost screen.  If we have a new top screen pending,
  // don't send to the old screen, but do send to overlay screens.
  if (delayed_screen_ != "") {
    return;
  }

  if (screen_stack.empty()) {
    return;
  }

  if (!get_input_redirected(input.pn_)) {
    screen_stack.back().screen_->Input(input);
  }
  screen_stack.back().screen_->PassInputToLua(input);
}

// Just create a new screen; don't do any associated cleanup.
Screen* ScreenManager::MakeNewScreen(const std::string& screen_name) {
  RageTimer t;
  LOG->Trace("Loading screen: \"%s\"", screen_name.c_str());

  std::string class_name = THEME->GetMetric(screen_name, "Class");

  std::map<std::string, CreateScreenFn>::iterator iter =
      registrees->find(class_name);
  if (iter == registrees->end()) {
    LuaHelpers::ReportScriptErrorFmt(
        "Screen \"%s\" has an invalid class \"%s\".", screen_name.c_str(),
        class_name.c_str());
    return nullptr;
  }

  this->ZeroNextUpdate();

  CreateScreenFn pfn = iter->second;
  Screen* ret = pfn(screen_name);

  LOG->Trace(
      "Loaded \"%s\" (\"%s\") in %f", screen_name.c_str(), class_name.c_str(),
      t.GetDeltaTime());

  return ret;
}

void ScreenManager::PrepareScreen(const std::string& screen_name) {
  // If the screen is already prepared, stop.
  if (ScreenIsPrepped(screen_name)) {
    return;
  }

  Screen* new_screen = MakeNewScreen(screen_name);
  if (new_screen == nullptr) {
    return;
  }

  {
    LoadedScreen ls;
    ls.screen_ = new_screen;

    prepared_screens.push_back(ls);
  }

  /* Don't delete previously prepared versions of the screen's background,
   * and only prepare it if it's different than the current background
   * and not already loaded. */
  std::string s_new_bga = THEME->GetPathB(screen_name, "background");

  if (!s_new_bga.empty() && s_new_bga != shared_bga->GetName()) {
    Actor* p_new_bga = nullptr;
    for (Actor* a : prepared_backgrounds) {
      if (a->GetName() == s_new_bga) {
        p_new_bga = a;
        break;
      }
    }

    // Create the new background before deleting the previous so that we keep
    // any common textures loaded.
    if (p_new_bga == nullptr) {
      LOG->Trace("Loading screen background \"%s\"", s_new_bga.c_str());
      Actor* actor = ActorUtil::MakeActor(s_new_bga);
      if (actor != nullptr) {
        actor->SetName(s_new_bga);
        prepared_backgrounds.push_back(actor);
      }
    }
  }

  // Prune any unused fonts now that we have had a chance to reference the fonts
  /*
  if(g_bPruneFonts) {
          FONT->PruneFonts();
  }
  */
}

void ScreenManager::GroupScreen(const std::string& screen_name) {
  set_grouped_screens.insert(screen_name);
}

void ScreenManager::PersistantScreen(const std::string& screen_name) {
  set_persistant_screens.insert(screen_name);
}

void ScreenManager::SetNewScreen(const std::string& screen_name) {
  ASSERT(screen_name != "");
  delayed_screen_ = screen_name;
}

/* Activate the screen and/or its background, if either are loaded.
 * Return true if both were activated. */
bool ScreenManager::ActivatePreparedScreenAndBackground(
    const std::string& screen_name) {
  bool loaded_both = true;

  // Find the prepped screen.
  if (GetTopScreen() == nullptr || GetTopScreen()->GetName() != screen_name) {
    LoadedScreen ls;
    if (!GetPreppedScreen(screen_name, ls)) {
      loaded_both = false;
    } else {
      PushLoadedScreen(ls);
    }
  }

  // Find the prepared shared background (if any), and activate it.
  std::string s_new_bga = THEME->GetPathB(screen_name, "background");
  if (s_new_bga != shared_bga->GetName()) {
    Actor* p_new_bga = nullptr;
    if (s_new_bga.empty()) {
      p_new_bga = new Actor;
    } else {
      for (std::vector<Actor*>::iterator a = prepared_backgrounds.begin();
           a != prepared_backgrounds.end(); ++a) {
        if ((*a)->GetName() == s_new_bga) {
          p_new_bga = *a;
          prepared_backgrounds.erase(a);
          break;
        }
      }
    }

    /* If the BGA isn't loaded yet, load a dummy actor. If we're not going to
     * use the same BGA for the new screen, always move the old BGA back to
     * g_vPreparedBackgrounds now. */
    if (p_new_bga == nullptr) {
      loaded_both = false;
      p_new_bga = new Actor;
    }

    /* Move the old background back to the prepared list, or delete it if
     * it's a blank actor. */
    if (shared_bga->GetName() == "") {
      delete shared_bga;
    } else {
      prepared_backgrounds.push_back(shared_bga);
    }
    shared_bga = p_new_bga;
    shared_bga->PlayCommand("On");
  }

  return loaded_both;
}

void ScreenManager::LoadDelayedScreen() {
  std::string screen_name = delayed_screen_;
  delayed_screen_ = "";
  if (!IsScreenNameValid(screen_name)) {
    LuaHelpers::ReportScriptError(
        "Tried to go to invalid screen: " + screen_name, "INVALID_SCREEN");
    return;
  }

  // Pop the top screen, if any.
  ScreenMessage sm = PopTopScreenInternal();

  /* If the screen is already prepared, activate it before performing any
   * cleanup, so it doesn't get deleted by cleanup. */
  bool loaded = ActivatePreparedScreenAndBackground(screen_name);

  std::vector<Actor*> actors_to_delete;
  if (set_grouped_screens.find(screen_name) == set_grouped_screens.end()) {
    /* It's time to delete all old prepared screens. Depending on
     * DelayedScreenLoad, we can either delete the screens before or after
     * we load the new screen. Either way, we must remove them from the
     * prepared list before we prepare new screens.
     * If DelayedScreenLoad is true, delete them now; this lowers memory
     * requirements, but results in redundant loads as we unload common data. */
    if (delayed_screen_load) {
      DeletePreparedScreens();
    } else {
      GrabPreparedActors(actors_to_delete);
    }
  }

  // If the screen wasn't already prepared, load it.
  if (!loaded) {
    PrepareScreen(screen_name);

    // Screens may not call SetNewScreen from the ctor or Init(). (We don't do
    // this check inside PrepareScreen; that may be called from a thread for
    // concurrent loading, and the main thread may call SetNewScreen during that
    // time.) Emit an error instead of asserting. -Kyz
    if (!delayed_screen_.empty()) {
      LuaHelpers::ReportScriptError(
          "Setting a new screen during an InitCommand is not allowed.");
      delayed_screen_ = "";
    }

    loaded = ActivatePreparedScreenAndBackground(screen_name);
    ASSERT(loaded);
  }

  if (!actors_to_delete.empty()) {
    BeforeDeleteScreen();
    for (Actor* a : actors_to_delete) {
      RageUtil::SafeDelete(a);
    }
    AfterDeleteScreen();
  }

  MESSAGEMAN->Broadcast(Message_ScreenChanged);

  SendMessageToTopScreen(sm);
}

void ScreenManager::AddNewScreenToTop(
    const std::string& screen_name, ScreenMessage send_on_pop) {
  // Load the screen, if it's not already prepared.
  PrepareScreen(screen_name);

  // Find the prepped screen.
  LoadedScreen ls;
  bool screen_load_success = GetPreppedScreen(screen_name, ls);
  ASSERT_M(
      screen_load_success,
      ssprintf(
          "ScreenManager::AddNewScreenToTop: Failed to load screen %s",
          screen_name.c_str()));

  ls.send_on_pop_ = send_on_pop;

  if (screen_stack.size()) {
    screen_stack.back().screen_->HandleScreenMessage(SM_LoseFocus);
  }
  PushLoadedScreen(ls);
}

void ScreenManager::PopTopScreen(ScreenMessage sm) {
  ASSERT(screen_stack.size() > 0);

  pop_top_screen_ = sm;
}

/* Clear the screen stack; only used before major, unusual state changes,
 * such as resetting the game or jumping to the service menu.  Don't call
 * from inside a screen. */
void ScreenManager::PopAllScreens() {
  // Make sure only the top screen receives LoseFocus.
  bool first = true;
  while (!screen_stack.empty()) {
    PopTopScreenInternal(first);
    first = false;
  }

  DeletePreparedScreens();
}

void ScreenManager::PostMessageToTopScreen(ScreenMessage sm, float delay) {
  Screen* top_screen = GetTopScreen();
  if (top_screen != nullptr) {
    top_screen->PostScreenMessage(sm, delay);
  }
}

void ScreenManager::SendMessageToTopScreen(ScreenMessage sm) {
  Screen* top_screen = GetTopScreen();
  if (top_screen != nullptr) {
    top_screen->HandleScreenMessage(sm);
  }
}

void ScreenManager::SystemMessage(const std::string& message) {
  LOG->Trace("%s", message.c_str());
  Message msg("SystemMessage");
  msg.SetParam("Message", message);
  msg.SetParam("NoAnimate", false);
  MESSAGEMAN->Broadcast(msg);
}

void ScreenManager::SystemMessageNoAnimate(const std::string& message) {
  //	LOG->Trace( "%s", sMessage.c_str() );	// don't log because the caller
  // is likely calling us every frame
  Message msg("SystemMessage");
  msg.SetParam("Message", message);
  msg.SetParam("NoAnimate", true);
  MESSAGEMAN->Broadcast(msg);
}

void ScreenManager::RefreshCreditsMessages() {
  MESSAGEMAN->Broadcast("RefreshCreditText");
}

void ScreenManager::ZeroNextUpdate() {
  zero_next_update_ = true;

  /* Loading probably took a little while.  Let's reset stats.  This prevents us
   * from displaying an unnaturally low FPS value, and the next FPS value we
   * display will be accurate, which makes skips in the initial tween-ins more
   * apparent. */
  DISPLAY->ResetStats();
}

/** @brief Offer a quick way to play any critical sound. */
#define PLAY_CRITICAL(snd)       \
  {                              \
    RageSoundParams p;           \
    p.is_critical_sound_ = true; \
    snd.Play(false, &p);         \
  }

/* Always play these sounds, even if we're in a silent attract loop. */
void ScreenManager::PlayInvalidSound() { PLAY_CRITICAL(sound_invalid_); }
void ScreenManager::PlayStartSound() { PLAY_CRITICAL(sound_start_); }
void ScreenManager::PlayCoinSound() { PLAY_CRITICAL(sound_coin_); }
void ScreenManager::PlayCancelSound() { PLAY_CRITICAL(sound_cancel_); }
void ScreenManager::PlayScreenshotSound() { PLAY_CRITICAL(sound_screenshot_); }

#undef PLAY_CRITICAL

void ScreenManager::PlaySharedBackgroundOffCommand() {
  shared_bga->PlayCommand("Off");
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenManager. */
class LunaScreenManager : public Luna<ScreenManager> {
 public:
  // Note: PrepareScreen binding is not allowed; loading data inside
  // Lua causes the Lua lock to be held for the duration of the load,
  // which blocks concurrent rendering
  static void ValidateScreenName(lua_State* L, std::string& name) {
    if (name == "") {
      std::string errstr = "Screen name is empty.";
      SCREENMAN->SystemMessage(errstr);
      luaL_error(L, errstr.c_str());
    }
    std::string class_name = THEME->GetMetric(name, "Class");
    if (registrees->find(class_name) == registrees->end()) {
      std::string errstr = "Screen \"" + name + "\" has an invalid class \"" +
                           class_name + "\".";
      SCREENMAN->SystemMessage(errstr);
      luaL_error(L, errstr.c_str());
    }
  }
  static int SetNewScreen(T* p, lua_State* L) {
    std::string screen = SArg(1);
    ValidateScreenName(L, screen);
    p->SetNewScreen(screen);
    COMMON_RETURN_SELF;
  }
  static int GetTopScreen(T* p, lua_State* L) {
    Actor* screen = p->GetTopScreen();
    if (screen != nullptr) {
      screen->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SystemMessage(T* p, lua_State* L) {
    p->SystemMessage(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int ScreenIsPrepped(T* p, lua_State* L) {
    lua_pushboolean(L, ScreenManagerUtil::ScreenIsPrepped(SArg(1)));
    return 1;
  }
  static int ScreenClassExists(T* p, lua_State* L) {
    lua_pushboolean(L, registrees->find(SArg(1)) != registrees->end());
    return 1;
  }
  static int AddNewScreenToTop(T* p, lua_State* L) {
    std::string screen = SArg(1);
    ValidateScreenName(L, screen);
    ScreenMessage sm = SM_None;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      std::string message = SArg(2);
      sm = ScreenMessageHelpers::ToScreenMessage(message);
    }

    p->AddNewScreenToTop(screen, sm);
    COMMON_RETURN_SELF;
  }
  // static int GetScreenStackSize( T* p, lua_State *L )	{
  // lua_pushnumber( L, ScreenManagerUtil::g_ScreenStack.size() ); return 1; }
  static int ReloadOverlayScreens(T* p, lua_State* L) {
    p->ReloadOverlayScreens();
    COMMON_RETURN_SELF;
  }

  static int get_input_redirected(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushboolean(L, p->get_input_redirected(pn));
    return 1;
  }
  static int set_input_redirected(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    p->set_input_redirected(pn, BArg(2));
    COMMON_RETURN_SELF;
  }

#define SCRMAN_PLAY_SOUND(sound_name)               \
  static int Play##sound_name(T* p, lua_State* L) { \
    p->Play##sound_name();                          \
    COMMON_RETURN_SELF;                             \
  }
  SCRMAN_PLAY_SOUND(InvalidSound);
  SCRMAN_PLAY_SOUND(StartSound);
  SCRMAN_PLAY_SOUND(CoinSound);
  SCRMAN_PLAY_SOUND(CancelSound);
  SCRMAN_PLAY_SOUND(ScreenshotSound);
#undef SCRMAN_PLAY_SOUND

  LunaScreenManager() {
    ADD_METHOD(SetNewScreen);
    ADD_METHOD(GetTopScreen);
    ADD_METHOD(SystemMessage);
    ADD_METHOD(ScreenIsPrepped);
    ADD_METHOD(ScreenClassExists);
    ADD_METHOD(AddNewScreenToTop);
    // ADD_METHOD( GetScreenStackSize );
    ADD_METHOD(ReloadOverlayScreens);
    ADD_METHOD(PlayInvalidSound);
    ADD_METHOD(PlayStartSound);
    ADD_METHOD(PlayCoinSound);
    ADD_METHOD(PlayCancelSound);
    ADD_METHOD(PlayScreenshotSound);
    ADD_GET_SET_METHODS(input_redirected);
  }
};

LUA_REGISTER_CLASS(ScreenManager)
// lua end

/*
 * (c) 2001-2003 Chris Danford, Glenn Maynard
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
