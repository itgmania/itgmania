#include "Background.h"

#include <algorithm>
#include <cfloat>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "AutoActor.h"
#include "BackgroundUtil.h"
#include "BeginnerHelper.h"
#include "DancingCharacters.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Quad.h"
#include "RageDisplay.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "ScreenDimensions.h"
#include "Song.h"
#include "StatsManager.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TimingSegments.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "global.h"

static ThemeMetric<float> LEFT_EDGE("Background", "LeftEdge");
static ThemeMetric<float> TOP_EDGE("Background", "TopEdge");
static ThemeMetric<float> RIGHT_EDGE("Background", "RightEdge");
static ThemeMetric<float> BOTTOM_EDGE("Background", "BottomEdge");
static ThemeMetric<float> CLAMP_OUTPUT_PERCENT(
    "Background", "ClampOutputPercent");
static ThemeMetric<bool> SHOW_DANCING_CHARACTERS(
    "Background", "ShowDancingCharacters");
static ThemeMetric<bool> USE_STATIC_BG("Background", "UseStaticBackground");
static ThemeMetric<float> RAND_BG_START_BEAT("Background", "RandomBGStartBeat");
static ThemeMetric<float> RAND_BG_CHANGE_MEASURES(
    "Background", "RandomBGChangeMeasures");
static ThemeMetric<bool> RAND_BG_CHANGES_WHEN_BPM_CHANGES(
    "Background", "RandomBGChangesWhenBPMChangesAtMeasureStart");
static ThemeMetric<bool> RAND_BG_ENDS_AT_LAST_BEAT(
    "Background", "RandomBGEndsAtLastBeat");

static Preference<bool> show_danger("ShowDanger", true);
static Preference<float> bg_brightness("BGBrightness", 0.7f);
static Preference<RandomBackgroundMode> random_background_mode(
    "RandomBackgroundMode", BGMODE_RANDOMMOVIES);
static Preference<int> num_backgrounds("NumBackgrounds", 10);
static Preference<bool> song_backgrounds("SongBackgrounds", true);

// Width of the region separating the left and right brightness areas:
static float background_center_width = 40;

class BrightnessOverlay : public ActorFrame {
 public:
  BrightnessOverlay();
  void Update(float delta_time);

  void FadeToActualBrightness();
  void SetActualBrightness();
  void Set(float brightness);

 private:
  Quad quad_bg_brightness_[kNumPlayers];
  Quad quad_bg_brightness_fade_;
};

struct BackgroundTransition {
  apActorCommands leaves_;
  apActorCommands root_;
};

class BackgroundImpl : public ActorFrame {
 public:
  BackgroundImpl();
  ~BackgroundImpl();
  void Init();

  virtual void LoadFromSong(const Song* song);
  virtual void Unload();

  virtual void Update(float delta_time);
  virtual void DrawPrimitives();

  void FadeToActualBrightness() { brightness_.FadeToActualBrightness(); }
  void SetBrightness(float brightness) {
    brightness_.Set(brightness);
  } /* overrides pref and Cover */

  DancingCharacters* GetDancingCharacters() { return dancing_characters_; };

 protected:
  bool initted_;
  DancingCharacters* dancing_characters_;
  const Song* song_;
  std::map<std::string, BackgroundTransition> map_name_to_transition_;
  std::deque<BackgroundDef>
      random_bg_animations_;  // random background to choose from.  These may or
                              // may not be loaded into m_BGAnimations.

  void LoadFromRandom(
      float first_beat, float end_beat, const BackgroundChange& change);
  bool IsDangerAllVisible();

  class Layer {
   public:
    Layer();
    void Unload();

    // return true if created and added to m_BGAnimations
    bool CreateBackground(
        const Song* song, const BackgroundDef& bd, Actor* parent);
    // return def of the background that was created and added to
    // m_BGAnimations. calls CreateBackground
    BackgroundDef CreateRandomBGA(
        const Song* song, const std::string& effect,
        std::deque<BackgroundDef>& random_bg_animations, Actor* parent);

    int FindBGSegmentForBeat(float beat) const;
    void UpdateCurBGChange(
        const Song* song, float last_music_seconds, float current_time,
        const std::map<std::string, BackgroundTransition>&
            map_name_to_transition);

    std::map<BackgroundDef, Actor*> bg_animations_;
    std::vector<BackgroundChange> bg_changes_;
    int cur_bg_change_index_;
    Actor* current_bga_;
    Actor* fading_bga_;
  };
  Layer layer_[NUM_BackgroundLayer];

  float last_music_seconds_;
  bool danger_all_was_visible_;

  // cover up the edge of animations that might hang outside of the background
  // rectangle
  Quad quad_border_left_, quad_border_top_, quad_border_right_,
      quad_border_bottom_;

  BrightnessOverlay brightness_;

  BackgroundDef static_background_def_;
};

static RageColor GetBrightnessColor(float brightness_percent) {
  RageColor brightness = RageColor(0, 0, 0, 1 - brightness_percent);
  RageColor clamp = RageColor(0.5f, 0.5f, 0.5f, CLAMP_OUTPUT_PERCENT);

  // blend the two colors above as if cBrightness is drawn, then cClamp drawn on
  // top
  brightness.a_ *= (1 - clamp.a_);  // premultiply alpha

  RageColor ret;
  ret.a_ = brightness.a_ + clamp.a_;
  ret.r_ = (brightness.r_ * brightness.a_ + clamp.r_ * clamp.a_) / ret.a_;
  ret.g_ = (brightness.g_ * brightness.a_ + clamp.g_ * clamp.a_) / ret.a_;
  ret.b_ = (brightness.b_ * brightness.a_ + clamp.b_ * clamp.a_) / ret.a_;
  return ret;
}

BackgroundImpl::BackgroundImpl() {
  initted_ = false;
  dancing_characters_ = nullptr;
  song_ = nullptr;
  danger_all_was_visible_ = false;
}

BackgroundImpl::Layer::Layer() {
  cur_bg_change_index_ = -1;
  current_bga_ = nullptr;
  fading_bga_ = nullptr;
}

void BackgroundImpl::Init() {
  if (initted_) {
    return;
  }
  initted_ = true;
  danger_all_was_visible_ = false;
  static_background_def_ = BackgroundDef();

  if (!USE_STATIC_BG) {
    static_background_def_.color1_ = "#00000000";
    static_background_def_.color2_ = "#00000000";
  }

  // load transitions
  {
    ASSERT(map_name_to_transition_.empty());
    std::vector<std::string> paths, names;
    BackgroundUtil::GetBackgroundTransitions("", paths, names);
    for (unsigned i = 0; i < paths.size(); i++) {
      const std::string& path = paths[i];
      const std::string& name = names[i];

      XNode xml;
      XmlFileUtil::LoadFromFileShowErrors(xml, path);
      ASSERT(xml.GetName() == "BackgroundTransition");
      BackgroundTransition& bgt = map_name_to_transition_[name];

      std::string cmd_leaves;
      bool success = xml.GetAttrValue("LeavesCommand", cmd_leaves);
      ASSERT(success);
      bgt.leaves_ = ActorUtil::ParseActorCommands(cmd_leaves);

      std::string cmd_root;
      success = xml.GetAttrValue("RootCommand", cmd_root);
      ASSERT(success);
      bgt.root_ = ActorUtil::ParseActorCommands(cmd_root);
    }
  }

  bool one_or_more_chars = false;
  bool showing_beginner_helper = false;
  FOREACH_HumanPlayer(p) {
    one_or_more_chars = true;
    // Disable dancing characters if Beginner Helper will be showing.
    if (PREFSMAN->show_beginner_helper_ && BeginnerHelper::CanUse(p) &&
        GAMESTATE->cur_steps_[p] &&
        GAMESTATE->cur_steps_[p]->GetDifficulty() == Difficulty_Beginner) {
      showing_beginner_helper = true;
    }
  }

  if (one_or_more_chars && !showing_beginner_helper &&
      SHOW_DANCING_CHARACTERS) {
    dancing_characters_ = new DancingCharacters;
  }

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
  FOREACH_BackgroundLayer(i) layer_[i].Unload();
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

  current_bga_ = nullptr;
  fading_bga_ = nullptr;
  cur_bg_change_index_ = -1;
}

bool BackgroundImpl::Layer::CreateBackground(
    const Song* song, const BackgroundDef& bd, Actor* parent) {
  ASSERT(bg_animations_.find(bd) == bg_animations_.end());

  // Resolve the background names
  std::vector<std::string> vs_to_resolve;
  vs_to_resolve.push_back(bd.file1_);
  vs_to_resolve.push_back(bd.file2_);

  std::vector<std::string> vs_resolved;
  vs_resolved.resize(vs_to_resolve.size());
  std::vector<LuaThreadVariable*> resolved_ref;
  resolved_ref.resize(vs_to_resolve.size());

  for (unsigned i = 0; i < vs_to_resolve.size(); i++) {
    const std::string& s_to_resolve = vs_to_resolve[i];

    if (s_to_resolve.empty()) {
      if (i == 0) {
        return false;
      } else {
        continue;
      }
    }

    /* Look for vsFileToResolve[i] in:
     * song's dir
     * RandomMovies dir
     * BGAnimations dir.
     */
    std::vector<std::string> paths, throw_away;

    // Look for BGAnims in the song dir
    if (s_to_resolve == kSongBackgroundFile) {
      paths.push_back(
          song->HasBackground()
              ? song->GetBackgroundPath()
              : THEME->GetPathG("Common", "fallback background"));
    }
    if (paths.empty()) {
      BackgroundUtil::GetSongBGAnimations(
          song, s_to_resolve, paths, throw_away);
    }
    if (paths.empty()) {
      BackgroundUtil::GetSongMovies(song, s_to_resolve, paths, throw_away);
    }
    if (paths.empty()) {
      BackgroundUtil::GetSongBitmaps(song, s_to_resolve, paths, throw_away);
    }
    if (paths.empty()) {
      BackgroundUtil::GetGlobalBGAnimations(
          song, s_to_resolve, paths, throw_away);
    }
    if (paths.empty()) {
      BackgroundUtil::GetGlobalRandomMovies(
          song, s_to_resolve, paths, throw_away);
    }

    std::string& s_resolved = vs_resolved[i];

    if (!paths.empty()) {
      s_resolved = paths[0];
    } else {
      // If the main background file is missing, we failed.
      if (i == 0) {
        return false;
      } else {
        s_resolved = "../" + ThemeManager::GetBlankGraphicPath();
      }
    }

    ASSERT(!s_resolved.empty());

    resolved_ref[i] =
        new LuaThreadVariable(ssprintf("File%d", i + 1), s_resolved);
  }

  std::string effect = bd.effect_;
  if (effect.empty()) {
    FileType ft = ActorUtil::GetFileType(vs_resolved[0]);
    switch (ft) {
      default:
        LuaHelpers::ReportScriptErrorFmt(
            "CreateBackground() Unknown file type '%s'",
            vs_resolved[0].c_str());
        [[fallthrough]];
      case FT_Bitmap:
      case FT_Sprite:
      case FT_Movie:
        effect = kSbeStretchNormal;
        break;
      case FT_Lua:
      case FT_Model:
        effect = kSbeUpperLeft;
        break;
      case FT_Xml:
        effect = kSbeCentered;
        break;
      case FT_Directory:
        if (DoesFileExist(vs_resolved[0] + "/default.lua")) {
          effect = kSbeUpperLeft;
        } else {
          effect = kSbeCentered;
        }
        break;
    }
  }
  ASSERT(!effect.empty());

  // Set Lua color globals
  LuaThreadVariable color1(
      "Color1", bd.color1_.empty() ? std::string("#FFFFFFFF") : bd.color1_);
  LuaThreadVariable color2(
      "Color2", bd.color2_.empty() ? std::string("#FFFFFFFF") : bd.color2_);

  // Resolve the effect file.
  std::string effect_file;
  for (int i = 0; i < 2; i++) {
    std::vector<std::string> paths, throw_away;
    BackgroundUtil::GetBackgroundEffects(effect, paths, throw_away);
    if (paths.empty()) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' is missing.", effect.c_str());
      effect = kSbeCentered;
    } else if (paths.size() > 1) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' has more than one match.", effect.c_str());
      effect = kSbeCentered;
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

  for (unsigned i = 0; i < resolved_ref.size(); i++) {
    delete resolved_ref[i];
  }

  return true;
}

BackgroundDef BackgroundImpl::Layer::CreateRandomBGA(
    const Song* song, const std::string& effect,
    std::deque<BackgroundDef>& random_bg_animations, Actor* parent) {
  if (random_background_mode == BGMODE_OFF) {
    return BackgroundDef();
  }

  // Set to not show any BGChanges, whether scripted or random
  if (GAMESTATE->song_options_.GetCurrent().static_background_) {
    return BackgroundDef();
  }

  if (random_bg_animations.empty()) {
    return BackgroundDef();
  }

  /* XXX: every time we fully loop, shuffle, so we don't play the same sequence
   * over and over; and nudge the shuffle so the next one won't be a repeat */
  BackgroundDef bd = random_bg_animations.front();
  random_bg_animations.push_back(random_bg_animations.front());
  random_bg_animations.pop_front();

  if (!effect.empty()) {
    bd.effect_ = effect;
  }

  std::map<BackgroundDef, Actor*>::const_iterator iter =
      bg_animations_.find(bd);

  // create the background if it's not already created
  if (iter == bg_animations_.end()) {
    bool success = CreateBackground(song, bd, parent);
    ASSERT(success);  // we fed it valid files, so this shouldn't fail
  }
  return bd;
}

void BackgroundImpl::LoadFromRandom(
    float first_beat, float end_beat, const BackgroundChange& change) {
  int start_row = BeatToNoteRow(first_beat);
  int end_row = BeatToNoteRow(end_beat);

  const TimingData& timing = song_->song_timing_;

  // change BG every time signature change or 4 measures
  const std::vector<TimingSegment*>& t_sigs =
      timing.GetTimingSegments(SEGMENT_TIME_SIG);

  for (unsigned i = 0; i < t_sigs.size(); i++) {
    TimeSignatureSegment* ts = static_cast<TimeSignatureSegment*>(t_sigs[i]);
    int segment_end_row =
        (i + 1 == t_sigs.size()) ? end_row : t_sigs[i + 1]->GetRow();

    int time_signature_start = std::max(ts->GetRow(), start_row);
    for (int j = time_signature_start; j < std::min(end_row, segment_end_row);
         j += int(RAND_BG_CHANGE_MEASURES * ts->GetNoteRowsPerMeasure())) {
      // Don't fade. It causes frame rate dip, especially on slower machines.
      BackgroundDef bd = layer_[0].CreateRandomBGA(
          song_, change.def_.effect_, random_bg_animations_, this);
      if (!bd.IsEmpty()) {
        BackgroundChange c = change;
        c.def_ = bd;
        if (j == time_signature_start && i == 0) {
          c.start_beat_ = RAND_BG_START_BEAT;
        } else {
          c.start_beat_ = NoteRowToBeat(j);
        }
        layer_[0].bg_changes_.push_back(c);
      }
    }
  }

  if (RAND_BG_CHANGES_WHEN_BPM_CHANGES) {
    // change BG every BPM change that is at the beginning of a measure
    const std::vector<TimingSegment*>& bpms =
        timing.GetTimingSegments(SEGMENT_BPM);
    for (unsigned i = 0; i < bpms.size(); i++) {
      bool at_beginning_of_measure = false;
      for (unsigned j = 0; j < t_sigs.size(); j++) {
        TimeSignatureSegment* ts =
            static_cast<TimeSignatureSegment*>(t_sigs[j]);
        if ((bpms[i]->GetRow() - ts->GetRow()) % ts->GetNoteRowsPerMeasure() ==
            0) {
          at_beginning_of_measure = true;
          break;
        }
      }

      if (!at_beginning_of_measure) {
        continue;  // skip
      }

      // start so that we don't create a BGChange right on top of fEndBeat
      bool in_range =
          bpms[i]->GetRow() >= start_row && bpms[i]->GetRow() < end_row;
      if (!in_range) {
        continue;  // skip
      }

      BackgroundDef bd = layer_[0].CreateRandomBGA(
          song_, change.def_.effect_, random_bg_animations_, this);
      if (!bd.IsEmpty()) {
        BackgroundChange c = change;
        c.def_.file1_ = bd.file1_;
        c.def_.file2_ = bd.file2_;
        c.start_beat_ = bpms[i]->GetBeat();
        layer_[0].bg_changes_.push_back(c);
      }
    }
  }
}

void BackgroundImpl::LoadFromSong(const Song* song) {
  Init();
  Unload();
  song_ = song;
  static_background_def_.file1_ = kSongBackgroundFile;

  if (bg_brightness == 0.0f) {
    return;
  }

  // Choose a bunch of backgrounds that we'll use for the random file marker
  {
    std::vector<std::string> throw_away, names;
    switch (random_background_mode) {
      default:
        ASSERT_M(
            0, ssprintf(
                   "Invalid RandomBackgroundMode: %i",
                   int(random_background_mode)));
        break;
      case BGMODE_OFF:
        break;
      case BGMODE_ANIMATIONS:
        BackgroundUtil::GetGlobalBGAnimations(song, "", throw_away, names);
        break;
      case BGMODE_RANDOMMOVIES:
        BackgroundUtil::GetGlobalRandomMovies(
            song, "", throw_away, names, true, true);
        break;
    }

    // Pick the same random items every time the song is played.
    RandomGen rnd(GetHashForString(song->GetSongDir()));
    std::shuffle(names.begin(), names.end(), rnd);
    int size = std::min((int)num_backgrounds, (int)names.size());
    names.resize(size);

    for (const std::string& s : names) {
      BackgroundDef bd;
      bd.file1_ = s;
      random_bg_animations_.push_back(bd);
    }
  }

  /* Song backgrounds (even just background stills) can get very big; never keep
   * them in memory. */
  RageTextureID::TexPolicy old_policy = TEXTUREMAN->GetDefaultTexturePolicy();
  TEXTUREMAN->SetDefaultTexturePolicy(RageTextureID::TEX_VOLATILE);

  TEXTUREMAN->DisableOddDimensionWarning();

  // Set to not show any BGChanges, whether scripted or random if
  // m_bStaticBackground is on
  if (!song_backgrounds ||
      GAMESTATE->song_options_.GetCurrent().static_background_) {
    // Backgrounds are disabled; just display the song background.
    BackgroundChange change;
    change.def_ = static_background_def_;
    change.start_beat_ = 0;
    layer_[0].bg_changes_.push_back(change);
  }
  // If m_bRandomBGOnly is on, then we want to ignore the scripted BG in favour
  // of randomly loaded BGs
  else if (
      song->HasBGChanges() &&
      !GAMESTATE->song_options_.GetCurrent().random_bg_only_) {
    FOREACH_BackgroundLayer(i) {
      Layer& layer = layer_[i];

      // Load all song-specified backgrounds
      for (const BackgroundChange& bgc : song->GetBackgroundChanges(i)) {
        BackgroundChange change = bgc;
        BackgroundDef& bd = change.def_;

        bool is_already_loaded =
            layer.bg_animations_.find(bd) != layer.bg_animations_.end();

        if (bd.file1_ != kRandomBackgroundFile && !is_already_loaded) {
          if (layer.CreateBackground(song_, bd, this)) {
            ;  // do nothing.  Create was successful.
          } else {
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
  } else  // pSong doesn't have an animation plan
  {
    Layer& layer = layer_[0];
    float first_beat = song->GetFirstBeat();
    float last_beat = song->GetLastBeat();

    LoadFromRandom(first_beat, last_beat, BackgroundChange());

    if (RAND_BG_ENDS_AT_LAST_BEAT) {
      // end showing the static song background
      BackgroundChange change;
      change.def_ = static_background_def_;
      change.start_beat_ = last_beat;
      layer.bg_changes_.push_back(change);
    }
  }

  // sort segments
  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    BackgroundUtil::SortBackgroundChangesArray(layer.bg_changes_);
  }

  Layer& mainlayer = layer_[0];

  BackgroundChange change;
  change.def_ = static_background_def_;
  change.start_beat_ = -10000;
  mainlayer.bg_changes_.insert(mainlayer.bg_changes_.begin(), change);

  // If any BGChanges use the background image, load it.
  bool static_background_used = false;
  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    for (const BackgroundChange& bgc : layer.bg_changes_) {
      const BackgroundDef& bd = bgc.def_;
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
        mainlayer.bg_animations_.find(static_background_def_) !=
        mainlayer.bg_animations_.end();
    if (!is_already_loaded) {
      bool success =
          mainlayer.CreateBackground(song_, static_background_def_, this);
      ASSERT(success);
    }
  }

  // Look for the random file marker, and replace the segment with
  // LoadFromRandom.
  for (unsigned i = 0; i < mainlayer.bg_changes_.size(); i++) {
    const BackgroundChange bg_change = mainlayer.bg_changes_[i];
    if (bg_change.def_.file1_ != kRandomBackgroundFile) {
      continue;
    }

    float start_beat = bg_change.start_beat_;
    float end_beat = song->GetLastBeat();
    if (i + 1 < mainlayer.bg_changes_.size()) {
      end_beat = mainlayer.bg_changes_[i + 1].start_beat_;
    }

    mainlayer.bg_changes_.erase(mainlayer.bg_changes_.begin() + i);
    --i;

    LoadFromRandom(start_beat, end_beat, bg_change);
  }

  // At this point, we shouldn't have any BGChanges to "".  "" is an invalid
  // name.
  for (unsigned i = 0; i < mainlayer.bg_changes_.size(); i++) {
    ASSERT(!mainlayer.bg_changes_[i].def_.file1_.empty());
  }

  // Re-sort.
  BackgroundUtil::SortBackgroundChangesArray(mainlayer.bg_changes_);

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

  // assumption: m_aBGChanges are sorted by m_fStartBeat
  int i;
  for (i = bg_changes_.size() - 1; i >= 0; i--) {
    if (beat >= bg_changes_[i].start_beat_) {
      return i;
    }
  }

  return i;
}

/* If the BG segment has changed, move focus to it. Send Update() calls. */
void BackgroundImpl::Layer::UpdateCurBGChange(
    const Song* song, float last_music_seconds, float current_time,
    const std::map<std::string, BackgroundTransition>& map_name_to_transition) {
  ASSERT(current_time != GameState::kMusicSecondsInvalid);

  if (bg_changes_.size() == 0) {
    return;
  }

  TimingData::GetBeatArgs beat_info;
  beat_info.elapsed_time_ = current_time;
  song->song_timing_.GetBeatAndBPSFromElapsedTime(beat_info);

  // Calls to Update() should *not* be scaled by music rate unless
  // RateModsAffectFGChanges is enabled; fCurrentTime is. Undo it.
  const float rate = PREFSMAN->rate_mods_affect_tweens_
                         ? 1.0f
                         : GAMESTATE->song_options_.GetCurrent().music_rate_;

  // Find the BGSegment we're in
  const int i = FindBGSegmentForBeat(beat_info.beat_);

  float delta_time = current_time - last_music_seconds;
  if (i != -1 && i != cur_bg_change_index_)  // we're changing backgrounds
  {
    // LOG->Trace( "old bga %d -> new bga %d (%s), %f, %f", m_iCurBGChangeIndex,
    // i, m_aBGChanges[i].GetTextDescription().c_str(),
    // m_aBGChanges[i].m_fStartBeat, fBeat );

    BackgroundChange old_change;
    if (cur_bg_change_index_ != -1) {
      old_change = bg_changes_[cur_bg_change_index_];
    }

    cur_bg_change_index_ = i;

    const BackgroundChange& change = bg_changes_[i];

    fading_bga_ = current_bga_;

    std::map<BackgroundDef, Actor*>::const_iterator iter =
        bg_animations_.find(change.def_);
    if (iter == bg_animations_.end()) {
      XNode* node = change.def_.CreateNode();
      std::string xml = XmlFileUtil::GetXML(node);
      Trim(xml);
      LuaHelpers::ReportScriptErrorFmt(
          "Tried to switch to a background that was never loaded:\n%s",
          xml.c_str());
      RageUtil::SafeDelete(node);
      return;
    }

    current_bga_ = iter->second;

    if (fading_bga_ == current_bga_) {
      fading_bga_ = nullptr;
      // LOG->Trace( "bg didn't actually change.  Ignoring." );
    } else {
      if (fading_bga_) {
        fading_bga_->PlayCommand("LoseFocus");

        if (!change.transition_.empty()) {
          std::map<std::string, BackgroundTransition>::const_iterator l_iter =
              map_name_to_transition.find(change.transition_);
          if (l_iter == map_name_to_transition.end()) {
            LuaHelpers::ReportScriptErrorFmt(
                "'%s' is not the name of a BackgroundTransition file.",
                change.transition_.c_str());
          } else {
            const BackgroundTransition& bt = l_iter->second;
            fading_bga_->RunCommandsOnLeaves(*bt.leaves_);
            fading_bga_->RunCommands(*bt.root_);
          }
        }
      }
    }

    current_bga_->SetUpdateRate(change.rate_);

    current_bga_->InitState();
    current_bga_->PlayCommand("On");
    current_bga_->PlayCommand("GainFocus");

    /* How much time of this BGA have we skipped?  (This happens with
     * SetSeconds.) */
    const float start_second =
        song->song_timing_.GetElapsedTimeFromBeat(change.start_beat_);

    /* This is affected by the music rate. */
    delta_time = current_time - start_second;
  }

  if (fading_bga_) {
    if (fading_bga_->GetTweenTimeLeft() == 0) {
      fading_bga_ = nullptr;
    }
  }

  /* This is unaffected by the music rate. */
  float delta_time_no_music_rate = std::max(delta_time / rate, 0.0f);

  if (current_bga_) {
    current_bga_->Update(delta_time_no_music_rate);
  }
  if (fading_bga_) {
    fading_bga_->Update(delta_time_no_music_rate);
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
        song_, last_music_seconds_, GAMESTATE->position_.music_seconds_,
        map_name_to_transition_);
  }
  last_music_seconds_ = GAMESTATE->position_.music_seconds_;
}

void BackgroundImpl::DrawPrimitives() {
  if (bg_brightness == 0.0f) {
    return;
  }

  if (IsDangerAllVisible()) {
    // Since this only shows when DANGER is visible, it will flash red on it's
    // own accord :)
    if (dancing_characters_) {
      dancing_characters_->draw_danger_light_ = true;
    }
  }

  {
    if (dancing_characters_) {
      dancing_characters_->draw_danger_light_ = false;
    }

    FOREACH_BackgroundLayer(i) {
      Layer& layer = layer_[i];
      if (layer.current_bga_) {
        layer.current_bga_->Draw();
      }
      if (layer.fading_bga_) {
        layer.fading_bga_->Draw();
      }
    }
  }

  if (dancing_characters_) {
    dancing_characters_->Draw();
    DISPLAY->ClearZBuffer();
  }

  ActorFrame::DrawPrimitives();
}

bool BackgroundImpl::IsDangerAllVisible() {
  // The players are never in danger in FAIL_OFF.
  // Only consider enabled players. A disabled player's m_pCurSteps and
  // PlayerOptions may be stale (e.g. when transitioning out of a screen
  // where steps were temporarily assigned like in couples/routine editing)
  FOREACH_EnabledPlayer(p) if (
      GAMESTATE->GetPlayerFailType(GAMESTATE->player_state_[p]) ==
      FailType_Off) return false;
  if (!show_danger) {
    return false;
  }

  /* Don't show it if everyone is already failing: it's already too late and
   * it's annoying for it to show for the entire duration of a song. */
  if (STATSMAN->cur_stage_stats_.AllFailed()) {
    return false;
  }

  return GAMESTATE->AllAreInDangerOrWorse();
}

BrightnessOverlay::BrightnessOverlay() {
  float quad_width = (RIGHT_EDGE - LEFT_EDGE) / 2;
  quad_width -= background_center_width / 2;

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
  /* If we're actually playing, then we're past fades, etc; update the
   * background brightness to follow Cover. */
  if (!GAMESTATE->gameplay_lead_in_) {
    SetActualBrightness();
  }
}

void BrightnessOverlay::SetActualBrightness() {
  float left_brightness =
      1 -
      GAMESTATE->player_state_[PLAYER_1]->player_options_.GetCurrent().cover_;
  float right_brightness =
      1 -
      GAMESTATE->player_state_[PLAYER_2]->player_options_.GetCurrent().cover_;

  float base_bg_brightness = bg_brightness;

  // Revision:  Themes that implement a training mode should handle the
  // brightness for it.  The engine should not force the brightness for
  // anything. -Kyz
  /*
  // HACK: Always show training in full brightness
  if( GAMESTATE->m_pCurSong && GAMESTATE->m_pCurSong->IsTutorial() )
          fBaseBGBrightness = 1.0f;
  */

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
  RageColor c = GetBrightnessColor(brightness);

  FOREACH_PlayerNumber(pn) quad_bg_brightness_[pn].SetDiffuse(c);
  quad_bg_brightness_fade_.SetDiffuse(c);
}

void BrightnessOverlay::FadeToActualBrightness() {
  this->PlayCommand("Fade");
  SetActualBrightness();
}

Background::Background() {
  disable_draw_ = false;
  impl_ = new BackgroundImpl;
  this->AddChild(impl_);
}
Background::~Background() { RageUtil::SafeDelete(impl_); }
void Background::Init() { impl_->Init(); }
void Background::LoadFromSong(const Song* song) { impl_->LoadFromSong(song); }
void Background::Unload() { impl_->Unload(); }
void Background::FadeToActualBrightness() { impl_->FadeToActualBrightness(); }
void Background::SetBrightness(float brightness) {
  impl_->SetBrightness(brightness);
}
DancingCharacters* Background::GetDancingCharacters() {
  return impl_->GetDancingCharacters();
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
