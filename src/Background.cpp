#include "global.h"

#include "Background.h"

#include <cfloat>
#include <vector>

#include "ActorUtil.h"
#include "AutoActor.h"
#include "BackgroundUtil.h"
#include "BeginnerHelper.h"
#include "DancingCharacters.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteTypes.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageTextureManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "Song.h"
#include "StatsManager.h"
#include "Steps.h"
#include "ThemeMetric.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"

static ThemeMetric<float> LEFT_EDGE(
    "Background", "LeftEdge");
static ThemeMetric<float> TOP_EDGE(
    "Background", "TopEdge");
static ThemeMetric<float> RIGHT_EDGE(
    "Background", "RightEdge");
static ThemeMetric<float> BOTTOM_EDGE(
    "Background", "BottomEdge");
static ThemeMetric<float> CLAMP_OUTPUT_PERCENT(
    "Background", "ClampOutputPercent");
static ThemeMetric<bool> SHOW_DANCING_CHARACTERS(
    "Background", "ShowDancingCharacters");
static ThemeMetric<bool> USE_STATIC_BG(
    "Background", "UseStaticBackground");
static ThemeMetric<float> RAND_BG_START_BEAT(
    "Background", "RandomBGStartBeat");
static ThemeMetric<float> RAND_BG_CHANGE_MEASURES(
    "Background", "RandomBGChangeMeasures");
static ThemeMetric<bool> RAND_BG_CHANGES_WHEN_BPM_CHANGES(
    "Background", "RandomBGChangesWhenBPMChangesAtMeasureStart");
static ThemeMetric<bool> RAND_BG_ENDS_AT_LAST_BEAT(
    "Background", "RandomBGEndsAtLastBeat");

static Preference<bool> g_bShowDanger(
    "ShowDanger", true);
static Preference<float> g_fBGBrightness(
    "BGBrightness", 0.7f);
static Preference<RandomBackgroundMode> g_RandomBackgroundMode(
    "RandomBackgroundMode", BGMODE_RANDOMMOVIES);
static Preference<int> g_iNumBackgrounds(
    "NumBackgrounds", 10);
static Preference<bool> g_bSongBackgrounds(
    "SongBackgrounds", true);

// Width of the region separating the left and right brightness areas:
static float kBackgroundCenterWidth = 40;

class BrightnessOverlay : public ActorFrame {
 public:
  BrightnessOverlay();
  void Update(float delta);

  void FadeToActualBrightness();
  void SetActualBrightness();
  void Set(float brightness);

 private:
  Quad quad_bg_brightness_[NUM_PLAYERS];
  Quad quad_bg_brightness_fade_;
};

struct BackgroundTransition {
  apActorCommands cmdLeaves;
  apActorCommands cmdRoot;
};

class BackgroundImpl : public ActorFrame {
 public:
  BackgroundImpl();
  ~BackgroundImpl();
  void Init();

  virtual void LoadFromSong(const Song* song);
  virtual void Unload();

  virtual void Update(float delta);
  virtual void DrawPrimitives();

  void FadeToActualBrightness() { brightness_.FadeToActualBrightness(); }
  // overrides pref and Cover
  void SetBrightness(float brightness) {
    brightness_.Set(brightness);
  }

  DancingCharacters* GetDancingCharacters() { return dancing_characters_; };

  void GetLoadedBackgroundChanges(
      std::vector<BackgroundChange>*
          background_changes_out[NUM_BackgroundLayer]);

 protected:
  bool initialized_;
  DancingCharacters* dancing_characters_;
  const Song* song_;
  std::map<RString, BackgroundTransition> name_to_transition_;
  // random background to choose from.
  // These may or may not be loaded into bg_animations_.
  std::deque<BackgroundDef> random_bg_animations_;  

  void LoadFromRandom(
      float first_beat, float end_beat, const BackgroundChange& change);
  bool IsDangerAllVisible();

  class Layer {
   public:
    Layer();
    void Unload();

    // return true if created and added to bg_animations_
    bool CreateBackground(
        const Song* song, const BackgroundDef& bd, Actor* parent);
    // return def of the background that was created and added to
    // bg_animations_. calls CreateBackground
    BackgroundDef CreateRandomBGA(
        const Song* song, const RString& effect,
        std::deque<BackgroundDef>& random_bg_animations, Actor* parent);

    int FindBGSegmentForBeat(float fBeat) const;
    void UpdateCurBGChange(
        const Song* song, float last_music_seconds, float current_time,
        const std::map<RString, BackgroundTransition>& name_to_transition);

    std::map<BackgroundDef, Actor*> bg_animations_;
    std::vector<BackgroundChange> bg_changes_;
    int cur_bg_change_index_;
    Actor* cur_bg_animation_;
    Actor* fading_bg_animation_;
  };
  Layer layer_[NUM_BackgroundLayer];

  float last_music_seconds_;
  bool danger_all_was_visible_;

  // cover up the edge of animations that might hang outside of the background
  // rectangle
  Quad quad_border_left_;
  Quad quad_border_top_;
  Quad quad_border_right_;
  Quad quad_border_bottom_;

  BrightnessOverlay brightness_;

  BackgroundDef static_background_def_;
};

static RageColor GetBrightnessColor(float brightness_percent) {
  RageColor brightness = RageColor(0, 0, 0, 1 - brightness_percent);
  RageColor clamp = RageColor(0.5f, 0.5f, 0.5f, CLAMP_OUTPUT_PERCENT);

  // Blend the two colors above as if brightness is drawn, then clamp drawn on
  // top
  brightness.a *= (1 - clamp.a);  // premultiply alpha

  RageColor ret;
  ret.a = brightness.a + clamp.a;
  ret.r = (brightness.r * brightness.a + clamp.r * clamp.a) / ret.a;
  ret.g = (brightness.g * brightness.a + clamp.g * clamp.a) / ret.a;
  ret.b = (brightness.b * brightness.a + clamp.b * clamp.a) / ret.a;
  return ret;
}

BackgroundImpl::BackgroundImpl() {
  initialized_ = false;
  dancing_characters_ = nullptr;
  song_ = nullptr;
}

BackgroundImpl::Layer::Layer() {
  cur_bg_change_index_ = -1;
  cur_bg_animation_ = nullptr;
  fading_bg_animation_ = nullptr;
}

void BackgroundImpl::Init() {
  if (initialized_) {
    return;
  }
  initialized_ = true;
  danger_all_was_visible_ = false;
  static_background_def_ = BackgroundDef();

  if (!USE_STATIC_BG) {
    static_background_def_.color1_ = "#00000000";
    static_background_def_.color2_ = "#00000000";
  }

  // Load transitions
  {
    ASSERT(name_to_transition_.empty());
    std::vector<RString> paths;
    std::vector<RString> names;
    BackgroundUtil::GetBackgroundTransitions("", paths, names);
    for (unsigned i = 0; i < paths.size(); ++i) {
      const RString& path = paths[i];
      const RString& name = names[i];

      XNode xml;
      XmlFileUtil::LoadFromFileShowErrors(xml, path);
      ASSERT(xml.GetName() == "BackgroundTransition");
      BackgroundTransition& transition = name_to_transition_[name];

      RString sCmdLeaves;
      bool success = xml.GetAttrValue("LeavesCommand", sCmdLeaves);
      ASSERT(success);
      transition.cmdLeaves = ActorUtil::ParseActorCommands(sCmdLeaves);

      RString sCmdRoot;
      success = xml.GetAttrValue("RootCommand", sCmdRoot);
      ASSERT(success);
      transition.cmdRoot = ActorUtil::ParseActorCommands(sCmdRoot);
    }
  }

  bool one_or_more_chars = false;
  bool showing_beginner_helper = false;
  FOREACH_HumanPlayer(p) {
    one_or_more_chars = true;
    // Disable dancing characters if Beginner Helper will be showing.
    if (PREFSMAN->m_bShowBeginnerHelper && BeginnerHelper::CanUse(p) &&
        GAMESTATE->cur_steps_[p] &&
        GAMESTATE->cur_steps_[p]->GetDifficulty() == Difficulty_Beginner)
      showing_beginner_helper = true;
  }

  if (one_or_more_chars && !showing_beginner_helper && SHOW_DANCING_CHARACTERS)
    dancing_characters_ = new DancingCharacters;

  RageColor c = GetBrightnessColor(0);

  quad_border_left_.StretchTo(
      RectF(SCREEN_LEFT, SCREEN_TOP, LEFT_EDGE, SCREEN_BOTTOM));
  quad_border_left_.SetDiffuse(c);
  quad_border_top_.StretchTo(
      RectF(LEFT_EDGE, SCREEN_TOP, RIGHT_EDGE, TOP_EDGE));
  quad_border_top_.SetDiffuse(c);
  quad_border_right_.StretchTo(
      RectF(RIGHT_EDGE, SCREEN_TOP, SCREEN_RIGHT, SCREEN_BOTTOM));
  quad_border_right_.SetDiffuse(c);
  quad_border_bottom_.StretchTo(
      RectF(LEFT_EDGE, BOTTOM_EDGE, RIGHT_EDGE, SCREEN_BOTTOM));
  quad_border_bottom_.SetDiffuse(c);

  this->AddChild(&quad_border_left_);
  this->AddChild(&quad_border_top_);
  this->AddChild(&quad_border_right_);
  this->AddChild(&quad_border_bottom_);

  this->AddChild(&brightness_);
}

BackgroundImpl::~BackgroundImpl() {
  Unload();
  delete dancing_characters_;
}

void BackgroundImpl::Unload() {
  FOREACH_BackgroundLayer(i) {
    layer_[i].Unload();
  }
  song_ = nullptr;
  last_music_seconds_ = -9999;
  random_bg_animations_.clear();
}

void BackgroundImpl::Layer::Unload() {
  for (std::pair<const BackgroundDef&, Actor*> iter : bg_animations_) {
    delete iter.second;
  }
  bg_animations_.clear();
  bg_changes_.clear();

  cur_bg_animation_ = nullptr;
  fading_bg_animation_ = nullptr;
  cur_bg_change_index_ = -1;
}

bool BackgroundImpl::Layer::CreateBackground(
    const Song* song, const BackgroundDef& bd, Actor* parent) {
  ASSERT(bg_animations_.find(bd) == bg_animations_.end());

  // Resolve the background names
  std::vector<RString> name_to_resolve;
  name_to_resolve.push_back(bd.file1_);
  name_to_resolve.push_back(bd.file2_);

  std::vector<RString> resolved;
  resolved.resize(name_to_resolve.size());
  std::vector<LuaThreadVariable *> resolved_ref;
  resolved_ref.resize(name_to_resolve.size());

  for (unsigned i = 0; i < name_to_resolve.size(); i++) {
    const RString& to_resolve = name_to_resolve[i];

    if (to_resolve.empty()) {
      if (i == 0) {
        return false;
      } else {
        continue;
      }
    }

    // Look for vsFileToResolve[i] in:
    // - song's dir
    // - RandomMovies dir
    // - BGAnimations dir.
    std::vector<RString> paths;
    std::vector<RString> throwaway;

    // Look for BGAnims in the song dir
    if (to_resolve == SONG_BACKGROUND_FILE)
      paths.push_back(
          song->HasBackground()
              ? song->GetBackgroundPath()
              : THEME->GetPathG("Common", "fallback background"));
    if (paths.empty()) {
      BackgroundUtil::GetSongBGAnimations( song, to_resolve, paths, throwaway);
    }
    if (paths.empty()) {
      BackgroundUtil::GetSongMovies(song, to_resolve, paths, throwaway);
    }
    if (paths.empty()) {
      BackgroundUtil::GetSongBitmaps(song, to_resolve, paths, throwaway);
    }
    if (paths.empty()) {
      BackgroundUtil::GetGlobalBGAnimations(song, to_resolve, paths, throwaway);
    }
    if (paths.empty()) {
      BackgroundUtil::GetGlobalRandomMovies(song, to_resolve, paths, throwaway);
    }

    RString& resolved_name = resolved[i];

    if (!paths.empty()) {
      resolved_name = paths[0];
    } else {
      // If the main background file is missing, we failed.
      if (i == 0) {
        return false;
      } else {
        resolved_name = "../" + ThemeManager::GetBlankGraphicPath();
      }
    }

    ASSERT(!resolved_name.empty());

    resolved_ref[i] =
        new LuaThreadVariable(ssprintf("File%d", i + 1), resolved_name);
  }

  RString effect = bd.effect_;
  if (effect.empty()) {
    FileType ft = ActorUtil::GetFileType(resolved[0]);
    switch (ft) {
      default:
        LuaHelpers::ReportScriptErrorFmt(
            "CreateBackground() Unknown file type '%s'", resolved[0].c_str());
        // fall through
      case FT_Bitmap:
      case FT_Sprite:
      case FT_Movie:
        effect = SBE_StretchNormal;
        break;
      case FT_Lua:
      case FT_Model:
        effect = SBE_UpperLeft;
        break;
      case FT_Xml:
        effect = SBE_Centered;
        break;
      case FT_Directory:
        if (DoesFileExist(resolved[0] + "/default.lua")) {
          effect = SBE_UpperLeft;
        } else {
          effect = SBE_Centered;
        }
        break;
    }
  }
  ASSERT(!effect.empty());

  // Set Lua color globals
  LuaThreadVariable sColor1(
      "Color1", bd.color1_.empty() ? RString("#FFFFFFFF") : bd.color1_);
  LuaThreadVariable sColor2(
      "Color2", bd.color2_.empty() ? RString("#FFFFFFFF") : bd.color2_);

  // Resolve the effect file.
  RString effect_file;
  for (int i = 0; i < 2; i++) {
    std::vector<RString> paths;
    std::vector<RString> throwaway;
    BackgroundUtil::GetBackgroundEffects(effect, paths, throwaway);
    if (paths.empty()) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' is missing.", effect.c_str());
      effect = SBE_Centered;
    } else if (paths.size() > 1) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' has more than one match.", effect.c_str());
      effect = SBE_Centered;
    } else {
      effect_file = paths[0];
      break;
    }
  }
  ASSERT(!effect_file.empty());

  Actor* actor = ActorUtil::MakeActor(effect_file);

  if (actor == nullptr) {
    actor = new Actor;
  }
  bg_animations_[bd] = actor;

  for (unsigned i = 0; i < resolved_ref.size(); ++i) {
    delete resolved_ref[i];
  }

  return true;
}

BackgroundDef BackgroundImpl::Layer::CreateRandomBGA(
    const Song* song, const RString& effect,
    std::deque<BackgroundDef>& random_bg_animations, Actor* parent) {
  if (g_RandomBackgroundMode == BGMODE_OFF) {
    return BackgroundDef();
  }

  // Set to not show any BGChanges, whether scripted or random
  if (GAMESTATE->song_options_.GetCurrent().m_bStaticBackground) {
    return BackgroundDef();
  }

  if (random_bg_animations.empty()) {
    return BackgroundDef();
  }

  // Every time we fully loop, shuffle, so we don't play the same sequence
  // over and over; and nudge the shuffle so the next one won't be a repeat
  BackgroundDef bd = random_bg_animations.front();
  random_bg_animations.push_back(random_bg_animations.front());
  random_bg_animations.pop_front();

  if (!effect.empty()) {
    bd.effect_ = effect;
  }

  // create the background if it's not already created
  if (bg_animations_.find(bd) == bg_animations_.end()) {
    bool success = CreateBackground(song, bd, parent);
    // We fed it valid files, so this shouldn't fail
    ASSERT(success);
  }
  return bd;
}

void BackgroundImpl::LoadFromRandom(
    float first_beat, float end_beat, const BackgroundChange& change) {
  int start_row = BeatToNoteRow(first_beat);
  int end_row = BeatToNoteRow(end_beat);

  const TimingData& timing = song_->m_SongTiming;

  // Change BG every time signature change or 4 measures
  const std::vector<TimingSegment*>& timing_segment =
      timing.GetTimingSegments(SEGMENT_TIME_SIG);

  for (unsigned i = 0; i < timing_segment.size(); ++i) {
    TimeSignatureSegment* ts =
        static_cast<TimeSignatureSegment*>(timing_segment[i]);
    int iSegmentEndRow =
        (i + 1 == timing_segment.size())
            ? end_row
            : timing_segment[i + 1]->GetRow();

    int time_signature_start = std::max(ts->GetRow(), start_row);
    for (int j = time_signature_start; j < std::min(end_row, iSegmentEndRow);
         j += int(RAND_BG_CHANGE_MEASURES * ts->GetNoteRowsPerMeasure())) {
      // Don't fade. It causes frame rate dip, especially on slower machines.
      BackgroundDef bd = layer_[0].CreateRandomBGA(
          song_, change.background_def_.effect_, random_bg_animations_, this);
      if (!bd.IsEmpty()) {
        BackgroundChange bg_change = change;
        bg_change.background_def_ = bd;
        if (j == time_signature_start && i == 0) {
          bg_change.start_beat_ = RAND_BG_START_BEAT;
        } else {
          bg_change.start_beat_ = NoteRowToBeat(j);
        }
        layer_[0].bg_changes_.push_back(bg_change);
      }
    }
  }

  if (RAND_BG_CHANGES_WHEN_BPM_CHANGES) {
    // change BG every BPM change that is at the beginning of a measure
    const std::vector<TimingSegment*>& bpms =
        timing.GetTimingSegments(SEGMENT_BPM);
    for (unsigned i = 0; i < bpms.size(); ++i) {
      bool at_beginning_of_measure = false;
      for (unsigned j = 0; j < timing_segment.size(); ++j) {
        TimeSignatureSegment* ts =
            static_cast<TimeSignatureSegment*>(timing_segment[j]);
        if ((bpms[i]->GetRow() - ts->GetRow()) % ts->GetNoteRowsPerMeasure() ==
            0) {
          at_beginning_of_measure = true;
          break;
        }
      }

      if (!at_beginning_of_measure) {
        continue;
      }

      // start so that we don't create a BGChange right on top of end_beat
      bool in_range =
          bpms[i]->GetRow() >= start_row && bpms[i]->GetRow() < end_row;
      if (!in_range) {
        continue;
      }

      BackgroundDef bd = layer_[0].CreateRandomBGA(
          song_, change.background_def_.effect_, random_bg_animations_, this);
      if (!bd.IsEmpty()) {
        BackgroundChange bg_change = change;
        bg_change.background_def_.file1_ = bd.file1_;
        bg_change.background_def_.file2_ = bd.file2_;
        bg_change.start_beat_ = bpms[i]->GetBeat();
        layer_[0].bg_changes_.push_back(bg_change);
      }
    }
  }
}

void BackgroundImpl::LoadFromSong(const Song* song) {
  Init();
  Unload();
  song_ = song;
  static_background_def_.file1_ = SONG_BACKGROUND_FILE;

  if (g_fBGBrightness == 0.0f) {
    return;
  }

  // Choose a bunch of backgrounds that we'll use for the random file marker
  {
    std::vector<RString> names;
    std::vector<RString> throwaway;
    switch (g_RandomBackgroundMode) {
      default:
        ASSERT_M(
            0, ssprintf(
                   "Invalid RandomBackgroundMode: %i",
                   int(g_RandomBackgroundMode)));
        break;
      case BGMODE_OFF:
        break;
      case BGMODE_ANIMATIONS:
        BackgroundUtil::GetGlobalBGAnimations(song, "", throwaway, names);
        break;
      case BGMODE_RANDOMMOVIES:
        BackgroundUtil::GetGlobalRandomMovies(
            song, "", throwaway, names, true, true);
        break;
    }

    // Pick the same random items every time the song is played.
    RandomGen rnd(GetHashForString(song->GetSongDir()));
    std::shuffle(names.begin(), names.end(), rnd);
    int iSize = std::min((int)g_iNumBackgrounds, (int)names.size());
    names.resize(iSize);

    for (const RString& s : names) {
      BackgroundDef bd;
      bd.file1_ = s;
      random_bg_animations_.push_back(bd);
    }
  }

  // Song backgrounds (even just background stills) can get very big; never keep
  // them in memory.
  RageTextureID::TexPolicy old_policy = TEXTUREMAN->GetDefaultTexturePolicy();
  TEXTUREMAN->SetDefaultTexturePolicy(RageTextureID::TEX_VOLATILE);

  TEXTUREMAN->DisableOddDimensionWarning();

  // Set to not show any BGChanges, whether scripted or random if
  // m_bStaticBackground is on
  if (!g_bSongBackgrounds ||
      GAMESTATE->song_options_.GetCurrent().m_bStaticBackground) {
    // Backgrounds are disabled; just display the song background.
    BackgroundChange bg_change;
    bg_change.background_def_ = static_background_def_;
    bg_change.start_beat_ = 0;
    layer_[0].bg_changes_.push_back(bg_change);
  }
  // If m_bRandomBGOnly is on, then we want to ignore the scripted BG in favour
  // of randomly loaded BGs
  else if (
      song->HasBGChanges() &&
      !GAMESTATE->song_options_.GetCurrent().m_bRandomBGOnly) {
    FOREACH_BackgroundLayer(i) {
      Layer& layer = layer_[i];

      // Load all song-specified backgrounds
      for (const BackgroundChange& change : song->GetBackgroundChanges(i)) {
        BackgroundChange bg_change = change;
        BackgroundDef& bd = bg_change.background_def_;

        bool is_already_loaded =
            layer.bg_animations_.find(bd) != layer.bg_animations_.end();

        if (bd.file1_ != RANDOM_BACKGROUND_FILE && !is_already_loaded) {
          if (!layer.CreateBackground(song_, bd, this)) {
            if (i == BACKGROUND_LAYER_1) {
              // The background was not found. Try to use a random one instead.
              // Don't use the BackgroundDef's effect, because it may be an
              // effect that requires 2 files, and random BGA will only supply
              // one file
              bd = layer.CreateRandomBGA(song, "", random_bg_animations_, this);
              if (bd.IsEmpty()) {
                bd = static_background_def_;
              }
            }
          }
        }

        if (!bd.IsEmpty()) {
          layer.bg_changes_.push_back(change);
        }
      }
    }
  } else {
    // song doesn't have an animation plan
    Layer& layer = layer_[0];
    float first_beat = song->GetFirstBeat();
    float last_beat = song->GetLastBeat();

    LoadFromRandom(first_beat, last_beat, BackgroundChange());

    if (RAND_BG_ENDS_AT_LAST_BEAT) {
      // end showing the static song background
      BackgroundChange bg_change;
      bg_change.background_def_ = static_background_def_;
      bg_change.start_beat_ = last_beat;
      layer.bg_changes_.push_back(bg_change);
    }
  }

  // sort segments
  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    BackgroundUtil::SortBackgroundChangesArray(layer.bg_changes_);
  }

  Layer& main_layer = layer_[0];

  BackgroundChange bg_change;
  bg_change.background_def_ = static_background_def_;
  bg_change.start_beat_ = -10000;
  main_layer.bg_changes_.insert(main_layer.bg_changes_.begin(), bg_change);

  // If any BGChanges use the background image, load it.
  bool static_background_used = false;
  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    for (const BackgroundChange& change : layer.bg_changes_) {
      const BackgroundDef& bd = change.background_def_;
      if (bd == static_background_def_) {
        static_background_used = true;
        break;
      }
    }
    if (static_background_used) {
      break;
    }
  }

  if (static_background_used) {
    bool is_already_loaded =
        main_layer.bg_animations_.find(static_background_def_) !=
        main_layer.bg_animations_.end();
    if (!is_already_loaded) {
      bool success =
          main_layer.CreateBackground(song_, static_background_def_, this);
      ASSERT(success);
    }
  }

  // Look for the random file marker, and replace the segment with
  // LoadFromRandom.
  for (unsigned i = 0; i < main_layer.bg_changes_.size(); ++i) {
    const BackgroundChange change = main_layer.bg_changes_[i];
    if (change.background_def_.file1_ != RANDOM_BACKGROUND_FILE) {
      continue;
    }

    float start_beat = change.start_beat_;
    float end_beat = song->GetLastBeat();
    if (i + 1 < main_layer.bg_changes_.size()) {
      end_beat = main_layer.bg_changes_[i + 1].start_beat_;
    }

    main_layer.bg_changes_.erase(main_layer.bg_changes_.begin() + i);
    --i;

    LoadFromRandom(start_beat, end_beat, change);
  }

  // At this point, we shouldn't have any BGChanges to "". "" is an invalid
  // name.
  for (unsigned i = 0; i < main_layer.bg_changes_.size(); ++i) {
    ASSERT(!main_layer.bg_changes_[i].background_def_.file1_.empty());
  }

  // Re-sort.
  BackgroundUtil::SortBackgroundChangesArray(main_layer.bg_changes_);

  TEXTUREMAN->EnableOddDimensionWarning();

  if (dancing_characters_) {
    dancing_characters_->LoadNextSong();
  }

  TEXTUREMAN->SetDefaultTexturePolicy(old_policy);
}

int BackgroundImpl::Layer::FindBGSegmentForBeat(float beat) const {
  if (bg_changes_.empty()) {
    return -1;
  }
  if (beat < bg_changes_[0].start_beat_) {
    return -1;
  }

  // Assumes that bg_changes_ are sorted by m_fStartBeat.
  for (int i = bg_changes_.size() - 1; i >= 0; --i) {
    if (beat >= bg_changes_[i].start_beat_) {
      return i;
    }
  }

  return -1;
}

// If the BG segment has changed, move focus to it. Send Update() calls.
void BackgroundImpl::Layer::UpdateCurBGChange(
    const Song* song, float last_music_seconds, float current_time,
    const std::map<RString, BackgroundTransition>& name_to_transition) {
  ASSERT(current_time != GameState::MUSIC_SECONDS_INVALID);

  if (bg_changes_.size() == 0) {
    return;
  }

  TimingData::GetBeatArgs beat_info;
  beat_info.elapsed_time = current_time;
  song->m_SongTiming.GetBeatAndBPSFromElapsedTime(beat_info);

  // Calls to Update() should *not* be scaled by music rate unless
  // RateModsAffectFGChanges is enabled; current_time is. Undo it.
  const float fRate = PREFSMAN->m_bRateModsAffectTweens
                          ? 1.0f
                          : GAMESTATE->song_options_.GetCurrent().m_fMusicRate;

  // Find the BGSegment we're in
  const int i = FindBGSegmentForBeat(beat_info.beat);

  float delta_time = current_time - last_music_seconds;
  // We're changing backgrounds
  if (i != -1 && i != cur_bg_change_index_) {
    BackgroundChange old_bg_change;
    if (cur_bg_change_index_ != -1) {
      old_bg_change = bg_changes_[cur_bg_change_index_];
    }

    cur_bg_change_index_ = i;

    const BackgroundChange& bg_change = bg_changes_[i];

    fading_bg_animation_ = cur_bg_animation_;

    auto iter = bg_animations_.find(bg_change.background_def_);
    if (iter == bg_animations_.end()) {
      XNode* node = bg_change.background_def_.CreateNode();
      RString xml = XmlFileUtil::GetXML(node);
      Trim(xml);
      LuaHelpers::ReportScriptErrorFmt(
          "Tried to switch to a background that was never loaded:\n%s",
          xml.c_str());
      SAFE_DELETE(node);
      return;
    }

    cur_bg_animation_ = iter->second;

    if (fading_bg_animation_ == cur_bg_animation_) {
      fading_bg_animation_ = nullptr;
    } else {
      if (fading_bg_animation_) {
        fading_bg_animation_->PlayCommand("LoseFocus");

        if (!bg_change.transition_.empty()) {
          auto it = name_to_transition.find(bg_change.transition_);
          if (it == name_to_transition.end()) {
            LuaHelpers::ReportScriptErrorFmt(
                "'%s' is not the name of a BackgroundTransition file.",
                bg_change.transition_.c_str());
          } else {
            const BackgroundTransition& transition = it->second;
            fading_bg_animation_->RunCommandsOnLeaves(*transition.cmdLeaves);
            fading_bg_animation_->RunCommands(*transition.cmdRoot);
          }
        }
      }
    }

    cur_bg_animation_->SetUpdateRate(bg_change.rate_);

    cur_bg_animation_->InitState();
    cur_bg_animation_->PlayCommand("On");
    cur_bg_animation_->PlayCommand("GainFocus");

    // How much time of this BGA have we skipped?  (This happens with
    // SetSeconds.)
    const float start_second =
        song->m_SongTiming.GetElapsedTimeFromBeat(bg_change.start_beat_);

    // This is affected by the music rate.
    delta_time = current_time - start_second;
  }

  if (fading_bg_animation_) {
    if (fading_bg_animation_->GetTweenTimeLeft() == 0) {
      fading_bg_animation_ = nullptr;
    }
  }

  // This is unaffected by the music rate.
  float delta_time_no_music_rate = std::max(delta_time / fRate, 0.0f);

  if (cur_bg_animation_) {
    cur_bg_animation_->Update(delta_time_no_music_rate);
  }
  if (fading_bg_animation_) {
    fading_bg_animation_->Update(delta_time_no_music_rate);
  }
}

void BackgroundImpl::Update(float delta_time) {
  ActorFrame::Update(delta_time);

  {
    bool visible = IsDangerAllVisible();
    if (danger_all_was_visible_ != visible) {
      MESSAGEMAN->Broadcast(visible ? "ShowDangerAll" : "HideDangerAll");
    }
    danger_all_was_visible_ = visible;
  }

  if (dancing_characters_) {
    dancing_characters_->Update(delta_time);
  }

  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    layer.UpdateCurBGChange(
        song_, last_music_seconds_, GAMESTATE->position_.m_fMusicSeconds,
        name_to_transition_);
  }
  last_music_seconds_ = GAMESTATE->position_.m_fMusicSeconds;
}

void BackgroundImpl::DrawPrimitives() {
  if (g_fBGBrightness == 0.0f) {
    return;
  }

  if (IsDangerAllVisible()) {
    // Since this only shows when DANGER is visible, it will flash red on it's
    // own accord :)
    if (dancing_characters_) {
      dancing_characters_->m_bDrawDangerLight = true;
    }
  }

  if (dancing_characters_) {
    dancing_characters_->m_bDrawDangerLight = false;
  }

  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    if (layer.cur_bg_animation_) {
      layer.cur_bg_animation_->Draw();
    }
    if (layer.fading_bg_animation_) {
      layer.fading_bg_animation_->Draw();
    }
  }

  if (dancing_characters_) {
    dancing_characters_->Draw();
    DISPLAY->ClearZBuffer();
  }

  ActorFrame::DrawPrimitives();
}

void BackgroundImpl::GetLoadedBackgroundChanges(
    std::vector<BackgroundChange>* background_changes_out[NUM_BackgroundLayer]) {
  FOREACH_BackgroundLayer(i) * background_changes_out[i] =
      layer_[i].bg_changes_;
}

bool BackgroundImpl::IsDangerAllVisible() {
  // The players are never in danger in FAIL_OFF.
  FOREACH_PlayerNumber(p) {
    if (GAMESTATE->GetPlayerFailType(GAMESTATE->player_state_[p]) ==
        FailType_Off) {
      return false;
    }
  }
  if (!g_bShowDanger) {
    return false;
  }

  // Don't show it if everyone is already failing: it's already too late and
  // it's annoying for it to show for the entire duration of a song.
  if (STATSMAN->m_CurStageStats.AllFailed()) {
    return false;
  }

  return GAMESTATE->AllAreInDangerOrWorse();
}

BrightnessOverlay::BrightnessOverlay() {
  float quad_width = (RIGHT_EDGE - LEFT_EDGE) / 2;
  quad_width -= kBackgroundCenterWidth / 2;

  quad_bg_brightness_[0].StretchTo(
      RectF(LEFT_EDGE, TOP_EDGE, LEFT_EDGE + quad_width, BOTTOM_EDGE));
  quad_bg_brightness_fade_.StretchTo(RectF(
      LEFT_EDGE + quad_width, TOP_EDGE, RIGHT_EDGE - quad_width, BOTTOM_EDGE));
  quad_bg_brightness_[1].StretchTo(
      RectF(RIGHT_EDGE - quad_width, TOP_EDGE, RIGHT_EDGE, BOTTOM_EDGE));

  quad_bg_brightness_[0].SetName("BrightnessOverlay");
  ActorUtil::LoadAllCommands(quad_bg_brightness_[0], "Background");
  this->AddChild(&quad_bg_brightness_[0]);

  quad_bg_brightness_[1].SetName("BrightnessOverlay");
  ActorUtil::LoadAllCommands(quad_bg_brightness_[1], "Background");
  this->AddChild(&quad_bg_brightness_[1]);

  quad_bg_brightness_fade_.SetName("BrightnessOverlay");
  ActorUtil::LoadAllCommands(quad_bg_brightness_fade_, "Background");
  this->AddChild(&quad_bg_brightness_fade_);

  SetActualBrightness();
}

void BrightnessOverlay::Update(float delta_time) {
  ActorFrame::Update(delta_time);
  // If we're actually playing, then we're past fades, etc; update the
  // background brightness to follow Cover.
  if (!GAMESTATE->gameplay_lead_in_) {
    SetActualBrightness();
  }
}

void BrightnessOverlay::SetActualBrightness() {
  float left_brightness = 1 - GAMESTATE->player_state_[PLAYER_1]
                                  ->m_PlayerOptions.GetCurrent()
                                  .m_fCover;
  float right_brightness = 1 - GAMESTATE->player_state_[PLAYER_2]
                                   ->m_PlayerOptions.GetCurrent()
                                   .m_fCover;

  float base_bg_brightness = g_fBGBrightness;

  // NOTE(Kyz): Themes that implement a training mode should handle the
  // brightness for it.  The engine should not force the brightness for
  // anything.
  left_brightness *= base_bg_brightness;
  right_brightness *= base_bg_brightness;

  if (!GAMESTATE->IsHumanPlayer(PLAYER_1)) {
    left_brightness = right_brightness;
  }
  if (!GAMESTATE->IsHumanPlayer(PLAYER_2)) {
    right_brightness = left_brightness;
  }

  RageColor left_color = GetBrightnessColor(left_brightness);
  RageColor right_color = GetBrightnessColor(right_brightness);

  quad_bg_brightness_[PLAYER_1].SetDiffuse(left_color);
  quad_bg_brightness_[PLAYER_2].SetDiffuse(right_color);
  quad_bg_brightness_fade_.SetDiffuseLeftEdge(left_color);
  quad_bg_brightness_fade_.SetDiffuseRightEdge(right_color);
}

void BrightnessOverlay::Set(float brightness) {
  RageColor color = GetBrightnessColor(brightness);

  FOREACH_PlayerNumber(pn) {
    quad_bg_brightness_[pn].SetDiffuse(color);
  }
  quad_bg_brightness_fade_.SetDiffuse(color);
}

void BrightnessOverlay::FadeToActualBrightness() {
  this->PlayCommand("Fade");
  SetActualBrightness();
}

Background::Background() {
  disable_draw_ = false;
  background_impl_ = new BackgroundImpl;
  this->AddChild(background_impl_);
}
Background::~Background() { SAFE_DELETE(background_impl_); }
void Background::Init() { background_impl_->Init(); }
void Background::LoadFromSong(const Song* song) {
  background_impl_->LoadFromSong(song);
}
void Background::Unload() { background_impl_->Unload(); }
void Background::FadeToActualBrightness() { background_impl_->FadeToActualBrightness(); }
void Background::SetBrightness(float brightness) {
  background_impl_->SetBrightness(brightness);
}
DancingCharacters *Background::GetDancingCharacters() {
  return background_impl_->GetDancingCharacters();
}
void Background::GetLoadedBackgroundChanges(
    std::vector<BackgroundChange>* background_changes_out[NUM_BackgroundLayer]) {
  background_impl_->GetLoadedBackgroundChanges(background_changes_out);
}

/*
 * (c) 2001-2004 Chris Danford, Ben Nordstrom
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
