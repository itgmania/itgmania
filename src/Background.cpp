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

static Preference<bool> g_bShowDanger("ShowDanger", true);
static Preference<float> g_fBGBrightness("BGBrightness", 0.7f);
static Preference<RandomBackgroundMode> g_RandomBackgroundMode(
    "RandomBackgroundMode", BGMODE_RANDOMMOVIES);
static Preference<int> g_iNumBackgrounds("NumBackgrounds", 10);
static Preference<bool> g_bSongBackgrounds("SongBackgrounds", true);

// Width of the region separating the left and right brightness areas:
static float g_fBackgroundCenterWidth = 40;

class BrightnessOverlay : public ActorFrame {
 public:
  BrightnessOverlay();
  void Update(float fDeltaTime);

  void FadeToActualBrightness();
  void SetActualBrightness();
  void Set(float fBrightness);

 private:
  Quad quad_bg_brightness_[NUM_PLAYERS];
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

  virtual void LoadFromSong(const Song* pSong);
  virtual void Unload();

  virtual void Update(float fDeltaTime);
  virtual void DrawPrimitives();

  void FadeToActualBrightness() { brightness_.FadeToActualBrightness(); }
  void SetBrightness(float fBrightness) {
    brightness_.Set(fBrightness);
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
      float fFirstBeat, float fEndBeat, const BackgroundChange& change);
  bool IsDangerAllVisible();

  class Layer {
   public:
    Layer();
    void Unload();

    // return true if created and added to m_BGAnimations
    bool CreateBackground(
        const Song* pSong, const BackgroundDef& bd, Actor* pParent);
    // return def of the background that was created and added to
    // m_BGAnimations. calls CreateBackground
    BackgroundDef CreateRandomBGA(
        const Song* pSong, const std::string& sEffect,
        std::deque<BackgroundDef>& RandomBGAnimations, Actor* pParent);

    int FindBGSegmentForBeat(float fBeat) const;
    void UpdateCurBGChange(
        const Song* pSong, float fLastMusicSeconds, float fCurrentTime,
        const std::map<std::string, BackgroundTransition>& mapNameToTransition);

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

static RageColor GetBrightnessColor(float fBrightnessPercent) {
  RageColor cBrightness = RageColor(0, 0, 0, 1 - fBrightnessPercent);
  RageColor cClamp = RageColor(0.5f, 0.5f, 0.5f, CLAMP_OUTPUT_PERCENT);

  // blend the two colors above as if cBrightness is drawn, then cClamp drawn on
  // top
  cBrightness.a_ *= (1 - cClamp.a_);  // premultiply alpha

  RageColor ret;
  ret.a_ = cBrightness.a_ + cClamp.a_;
  ret.r_ = (cBrightness.r_ * cBrightness.a_ + cClamp.r_ * cClamp.a_) / ret.a_;
  ret.g_ = (cBrightness.g_ * cBrightness.a_ + cClamp.g_ * cClamp.a_) / ret.a_;
  ret.b_ = (cBrightness.b_ * cBrightness.a_ + cClamp.b_ * cClamp.a_) / ret.a_;
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
    std::vector<std::string> vsPaths, vsNames;
    BackgroundUtil::GetBackgroundTransitions("", vsPaths, vsNames);
    for (unsigned i = 0; i < vsPaths.size(); i++) {
      const std::string& sPath = vsPaths[i];
      const std::string& sName = vsNames[i];

      XNode xml;
      XmlFileUtil::LoadFromFileShowErrors(xml, sPath);
      ASSERT(xml.GetName() == "BackgroundTransition");
      BackgroundTransition& bgt = map_name_to_transition_[sName];

      std::string sCmdLeaves;
      bool bSuccess = xml.GetAttrValue("LeavesCommand", sCmdLeaves);
      ASSERT(bSuccess);
      bgt.leaves_ = ActorUtil::ParseActorCommands(sCmdLeaves);

      std::string sCmdRoot;
      bSuccess = xml.GetAttrValue("RootCommand", sCmdRoot);
      ASSERT(bSuccess);
      bgt.root_ = ActorUtil::ParseActorCommands(sCmdRoot);
    }
  }

  bool bOneOrMoreChars = false;
  bool bShowingBeginnerHelper = false;
  FOREACH_HumanPlayer(p) {
    bOneOrMoreChars = true;
    // Disable dancing characters if Beginner Helper will be showing.
    if (PREFSMAN->show_beginner_helper_ && BeginnerHelper::CanUse(p) &&
        GAMESTATE->cur_steps_[p] &&
        GAMESTATE->cur_steps_[p]->GetDifficulty() == Difficulty_Beginner) {
      bShowingBeginnerHelper = true;
    }
  }

  if (bOneOrMoreChars && !bShowingBeginnerHelper && SHOW_DANCING_CHARACTERS) {
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
    const Song* pSong, const BackgroundDef& bd, Actor* pParent) {
  ASSERT(bg_animations_.find(bd) == bg_animations_.end());

  // Resolve the background names
  std::vector<std::string> vsToResolve;
  vsToResolve.push_back(bd.file1_);
  vsToResolve.push_back(bd.file2_);

  std::vector<std::string> vsResolved;
  vsResolved.resize(vsToResolve.size());
  std::vector<LuaThreadVariable*> vsResolvedRef;
  vsResolvedRef.resize(vsToResolve.size());

  for (unsigned i = 0; i < vsToResolve.size(); i++) {
    const std::string& sToResolve = vsToResolve[i];

    if (sToResolve.empty()) {
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
    std::vector<std::string> vsPaths, vsThrowAway;

    // Look for BGAnims in the song dir
    if (sToResolve == SONG_BACKGROUND_FILE) {
      vsPaths.push_back(
          pSong->HasBackground()
              ? pSong->GetBackgroundPath()
              : THEME->GetPathG("Common", "fallback background"));
    }
    if (vsPaths.empty()) {
      BackgroundUtil::GetSongBGAnimations(
          pSong, sToResolve, vsPaths, vsThrowAway);
    }
    if (vsPaths.empty()) {
      BackgroundUtil::GetSongMovies(pSong, sToResolve, vsPaths, vsThrowAway);
    }
    if (vsPaths.empty()) {
      BackgroundUtil::GetSongBitmaps(pSong, sToResolve, vsPaths, vsThrowAway);
    }
    if (vsPaths.empty()) {
      BackgroundUtil::GetGlobalBGAnimations(
          pSong, sToResolve, vsPaths, vsThrowAway);
    }
    if (vsPaths.empty()) {
      BackgroundUtil::GetGlobalRandomMovies(
          pSong, sToResolve, vsPaths, vsThrowAway);
    }

    std::string& sResolved = vsResolved[i];

    if (!vsPaths.empty()) {
      sResolved = vsPaths[0];
    } else {
      // If the main background file is missing, we failed.
      if (i == 0) {
        return false;
      } else {
        sResolved = "../" + ThemeManager::GetBlankGraphicPath();
      }
    }

    ASSERT(!sResolved.empty());

    vsResolvedRef[i] =
        new LuaThreadVariable(ssprintf("File%d", i + 1), sResolved);
  }

  std::string sEffect = bd.effect_;
  if (sEffect.empty()) {
    FileType ft = ActorUtil::GetFileType(vsResolved[0]);
    switch (ft) {
      default:
        LuaHelpers::ReportScriptErrorFmt(
            "CreateBackground() Unknown file type '%s'", vsResolved[0].c_str());
        [[fallthrough]];
      case FT_Bitmap:
      case FT_Sprite:
      case FT_Movie:
        sEffect = SBE_StretchNormal;
        break;
      case FT_Lua:
      case FT_Model:
        sEffect = SBE_UpperLeft;
        break;
      case FT_Xml:
        sEffect = SBE_Centered;
        break;
      case FT_Directory:
        if (DoesFileExist(vsResolved[0] + "/default.lua")) {
          sEffect = SBE_UpperLeft;
        } else {
          sEffect = SBE_Centered;
        }
        break;
    }
  }
  ASSERT(!sEffect.empty());

  // Set Lua color globals
  LuaThreadVariable sColor1(
      "Color1", bd.color1_.empty() ? std::string("#FFFFFFFF") : bd.color1_);
  LuaThreadVariable sColor2(
      "Color2", bd.color2_.empty() ? std::string("#FFFFFFFF") : bd.color2_);

  // Resolve the effect file.
  std::string sEffectFile;
  for (int i = 0; i < 2; i++) {
    std::vector<std::string> vsPaths, vsThrowAway;
    BackgroundUtil::GetBackgroundEffects(sEffect, vsPaths, vsThrowAway);
    if (vsPaths.empty()) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' is missing.", sEffect.c_str());
      sEffect = SBE_Centered;
    } else if (vsPaths.size() > 1) {
      LuaHelpers::ReportScriptErrorFmt(
          "BackgroundEffect '%s' has more than one match.", sEffect.c_str());
      sEffect = SBE_Centered;
    } else {
      sEffectFile = vsPaths[0];
      break;
    }
  }
  ASSERT(!sEffectFile.empty());

  Actor* pActor = ActorUtil::MakeActor(sEffectFile);

  if (pActor == nullptr) {
    pActor = new Actor;
  }
  bg_animations_[bd] = pActor;

  for (unsigned i = 0; i < vsResolvedRef.size(); i++) {
    delete vsResolvedRef[i];
  }

  return true;
}

BackgroundDef BackgroundImpl::Layer::CreateRandomBGA(
    const Song* pSong, const std::string& sEffect,
    std::deque<BackgroundDef>& RandomBGAnimations, Actor* pParent) {
  if (g_RandomBackgroundMode == BGMODE_OFF) {
    return BackgroundDef();
  }

  // Set to not show any BGChanges, whether scripted or random
  if (GAMESTATE->song_options_.GetCurrent().static_background_) {
    return BackgroundDef();
  }

  if (RandomBGAnimations.empty()) {
    return BackgroundDef();
  }

  /* XXX: every time we fully loop, shuffle, so we don't play the same sequence
   * over and over; and nudge the shuffle so the next one won't be a repeat */
  BackgroundDef bd = RandomBGAnimations.front();
  RandomBGAnimations.push_back(RandomBGAnimations.front());
  RandomBGAnimations.pop_front();

  if (!sEffect.empty()) {
    bd.effect_ = sEffect;
  }

  std::map<BackgroundDef, Actor*>::const_iterator iter =
      bg_animations_.find(bd);

  // create the background if it's not already created
  if (iter == bg_animations_.end()) {
    bool bSuccess = CreateBackground(pSong, bd, pParent);
    ASSERT(bSuccess);  // we fed it valid files, so this shouldn't fail
  }
  return bd;
}

void BackgroundImpl::LoadFromRandom(
    float fFirstBeat, float fEndBeat, const BackgroundChange& change) {
  int iStartRow = BeatToNoteRow(fFirstBeat);
  int iEndRow = BeatToNoteRow(fEndBeat);

  const TimingData& timing = song_->song_timing_;

  // change BG every time signature change or 4 measures
  const std::vector<TimingSegment*>& tSigs =
      timing.GetTimingSegments(SEGMENT_TIME_SIG);

  for (unsigned i = 0; i < tSigs.size(); i++) {
    TimeSignatureSegment* ts = static_cast<TimeSignatureSegment*>(tSigs[i]);
    int iSegmentEndRow =
        (i + 1 == tSigs.size()) ? iEndRow : tSigs[i + 1]->GetRow();

    int time_signature_start = std::max(ts->GetRow(), iStartRow);
    for (int j = time_signature_start; j < std::min(iEndRow, iSegmentEndRow);
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
      bool bAtBeginningOfMeasure = false;
      for (unsigned j = 0; j < tSigs.size(); j++) {
        TimeSignatureSegment* ts = static_cast<TimeSignatureSegment*>(tSigs[j]);
        if ((bpms[i]->GetRow() - ts->GetRow()) % ts->GetNoteRowsPerMeasure() ==
            0) {
          bAtBeginningOfMeasure = true;
          break;
        }
      }

      if (!bAtBeginningOfMeasure) {
        continue;  // skip
      }

      // start so that we don't create a BGChange right on top of fEndBeat
      bool bInRange =
          bpms[i]->GetRow() >= iStartRow && bpms[i]->GetRow() < iEndRow;
      if (!bInRange) {
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

void BackgroundImpl::LoadFromSong(const Song* pSong) {
  Init();
  Unload();
  song_ = pSong;
  static_background_def_.file1_ = SONG_BACKGROUND_FILE;

  if (g_fBGBrightness == 0.0f) {
    return;
  }

  // Choose a bunch of backgrounds that we'll use for the random file marker
  {
    std::vector<std::string> vsThrowAway, vsNames;
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
        BackgroundUtil::GetGlobalBGAnimations(pSong, "", vsThrowAway, vsNames);
        break;
      case BGMODE_RANDOMMOVIES:
        BackgroundUtil::GetGlobalRandomMovies(
            pSong, "", vsThrowAway, vsNames, true, true);
        break;
    }

    // Pick the same random items every time the song is played.
    RandomGen rnd(GetHashForString(pSong->GetSongDir()));
    std::shuffle(vsNames.begin(), vsNames.end(), rnd);
    int iSize = std::min((int)g_iNumBackgrounds, (int)vsNames.size());
    vsNames.resize(iSize);

    for (const std::string& s : vsNames) {
      BackgroundDef bd;
      bd.file1_ = s;
      random_bg_animations_.push_back(bd);
    }
  }

  /* Song backgrounds (even just background stills) can get very big; never keep
   * them in memory. */
  RageTextureID::TexPolicy OldPolicy = TEXTUREMAN->GetDefaultTexturePolicy();
  TEXTUREMAN->SetDefaultTexturePolicy(RageTextureID::TEX_VOLATILE);

  TEXTUREMAN->DisableOddDimensionWarning();

  // Set to not show any BGChanges, whether scripted or random if
  // m_bStaticBackground is on
  if (!g_bSongBackgrounds ||
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
      pSong->HasBGChanges() &&
      !GAMESTATE->song_options_.GetCurrent().random_bg_only_) {
    FOREACH_BackgroundLayer(i) {
      Layer& layer = layer_[i];

      // Load all song-specified backgrounds
      for (const BackgroundChange& bgc : pSong->GetBackgroundChanges(i)) {
        BackgroundChange change = bgc;
        BackgroundDef& bd = change.def_;

        bool bIsAlreadyLoaded =
            layer.bg_animations_.find(bd) != layer.bg_animations_.end();

        if (bd.file1_ != RANDOM_BACKGROUND_FILE && !bIsAlreadyLoaded) {
          if (layer.CreateBackground(song_, bd, this)) {
            ;  // do nothing.  Create was successful.
          } else {
            if (i == BACKGROUND_LAYER_1) {
              // The background was not found. Try to use a random one instead.
              // Don't use the BackgroundDef's effect, because it may be an
              // effect that requires 2 files, and random BGA will only supply
              // one file
              bd =
                  layer.CreateRandomBGA(pSong, "", random_bg_animations_, this);
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
    float firstBeat = pSong->GetFirstBeat();
    float lastBeat = pSong->GetLastBeat();

    LoadFromRandom(firstBeat, lastBeat, BackgroundChange());

    if (RAND_BG_ENDS_AT_LAST_BEAT) {
      // end showing the static song background
      BackgroundChange change;
      change.def_ = static_background_def_;
      change.start_beat_ = lastBeat;
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
  bool bStaticBackgroundUsed = false;
  FOREACH_BackgroundLayer(i) {
    Layer& layer = layer_[i];
    for (const BackgroundChange& bgc : layer.bg_changes_) {
      const BackgroundDef& bd = bgc.def_;
      if (bd == static_background_def_) {
        bStaticBackgroundUsed = true;
        break;
      }
    }
    if (bStaticBackgroundUsed) {
      break;
    }
  }

  if (bStaticBackgroundUsed) {
    bool bIsAlreadyLoaded =
        mainlayer.bg_animations_.find(static_background_def_) !=
        mainlayer.bg_animations_.end();
    if (!bIsAlreadyLoaded) {
      bool bSuccess =
          mainlayer.CreateBackground(song_, static_background_def_, this);
      ASSERT(bSuccess);
    }
  }

  // Look for the random file marker, and replace the segment with
  // LoadFromRandom.
  for (unsigned i = 0; i < mainlayer.bg_changes_.size(); i++) {
    const BackgroundChange bgChange = mainlayer.bg_changes_[i];
    if (bgChange.def_.file1_ != RANDOM_BACKGROUND_FILE) {
      continue;
    }

    float fStartBeat = bgChange.start_beat_;
    float fEndBeat = pSong->GetLastBeat();
    if (i + 1 < mainlayer.bg_changes_.size()) {
      fEndBeat = mainlayer.bg_changes_[i + 1].start_beat_;
    }

    mainlayer.bg_changes_.erase(mainlayer.bg_changes_.begin() + i);
    --i;

    LoadFromRandom(fStartBeat, fEndBeat, bgChange);
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

  TEXTUREMAN->SetDefaultTexturePolicy(OldPolicy);
}

int BackgroundImpl::Layer::FindBGSegmentForBeat(float fBeat) const {
  if (bg_changes_.empty()) {
    return -1;
  }
  if (fBeat < bg_changes_[0].start_beat_) {
    return -1;
  }

  // assumption: m_aBGChanges are sorted by m_fStartBeat
  int i;
  for (i = bg_changes_.size() - 1; i >= 0; i--) {
    if (fBeat >= bg_changes_[i].start_beat_) {
      return i;
    }
  }

  return i;
}

/* If the BG segment has changed, move focus to it. Send Update() calls. */
void BackgroundImpl::Layer::UpdateCurBGChange(
    const Song* pSong, float fLastMusicSeconds, float fCurrentTime,
    const std::map<std::string, BackgroundTransition>& mapNameToTransition) {
  ASSERT(fCurrentTime != GameState::kMusicSecondsInvalid);

  if (bg_changes_.size() == 0) {
    return;
  }

  TimingData::GetBeatArgs beat_info;
  beat_info.elapsed_time_ = fCurrentTime;
  pSong->song_timing_.GetBeatAndBPSFromElapsedTime(beat_info);

  // Calls to Update() should *not* be scaled by music rate unless
  // RateModsAffectFGChanges is enabled; fCurrentTime is. Undo it.
  const float fRate = PREFSMAN->rate_mods_affect_tweens_
                          ? 1.0f
                          : GAMESTATE->song_options_.GetCurrent().music_rate_;

  // Find the BGSegment we're in
  const int i = FindBGSegmentForBeat(beat_info.beat_);

  float fDeltaTime = fCurrentTime - fLastMusicSeconds;
  if (i != -1 && i != cur_bg_change_index_)  // we're changing backgrounds
  {
    // LOG->Trace( "old bga %d -> new bga %d (%s), %f, %f", m_iCurBGChangeIndex,
    // i, m_aBGChanges[i].GetTextDescription().c_str(),
    // m_aBGChanges[i].m_fStartBeat, fBeat );

    BackgroundChange oldChange;
    if (cur_bg_change_index_ != -1) {
      oldChange = bg_changes_[cur_bg_change_index_];
    }

    cur_bg_change_index_ = i;

    const BackgroundChange& change = bg_changes_[i];

    fading_bga_ = current_bga_;

    std::map<BackgroundDef, Actor*>::const_iterator iter =
        bg_animations_.find(change.def_);
    if (iter == bg_animations_.end()) {
      XNode* pNode = change.def_.CreateNode();
      std::string xml = XmlFileUtil::GetXML(pNode);
      Trim(xml);
      LuaHelpers::ReportScriptErrorFmt(
          "Tried to switch to a background that was never loaded:\n%s",
          xml.c_str());
      RageUtil::SafeDelete(pNode);
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
          std::map<std::string, BackgroundTransition>::const_iterator lIter =
              mapNameToTransition.find(change.transition_);
          if (lIter == mapNameToTransition.end()) {
            LuaHelpers::ReportScriptErrorFmt(
                "'%s' is not the name of a BackgroundTransition file.",
                change.transition_.c_str());
          } else {
            const BackgroundTransition& bt = lIter->second;
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
    const float fStartSecond =
        pSong->song_timing_.GetElapsedTimeFromBeat(change.start_beat_);

    /* This is affected by the music rate. */
    fDeltaTime = fCurrentTime - fStartSecond;
  }

  if (fading_bga_) {
    if (fading_bga_->GetTweenTimeLeft() == 0) {
      fading_bga_ = nullptr;
    }
  }

  /* This is unaffected by the music rate. */
  float fDeltaTimeNoMusicRate = std::max(fDeltaTime / fRate, 0.0f);

  if (current_bga_) {
    current_bga_->Update(fDeltaTimeNoMusicRate);
  }
  if (fading_bga_) {
    fading_bga_->Update(fDeltaTimeNoMusicRate);
  }
}

void BackgroundImpl::Update(float fDeltaTime) {
  ActorFrame::Update(fDeltaTime);

  {
    bool bVisible = IsDangerAllVisible();
    if (danger_all_was_visible_ != bVisible) {
      MESSAGEMAN->Broadcast(bVisible ? "ShowDangerAll" : "HideDangerAll");
    }
    danger_all_was_visible_ = bVisible;
  }

  if (dancing_characters_) {
    dancing_characters_->Update(fDeltaTime);
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
  if (g_fBGBrightness == 0.0f) {
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
  if (!g_bShowDanger) {
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
  float fQuadWidth = (RIGHT_EDGE - LEFT_EDGE) / 2;
  fQuadWidth -= g_fBackgroundCenterWidth / 2;

  quad_bg_brightness_[0].StretchTo(
      RectF(LEFT_EDGE, TOP_EDGE, LEFT_EDGE + fQuadWidth, BOTTOM_EDGE));
  quad_bg_brightness_fade_.StretchTo(RectF(
      LEFT_EDGE + fQuadWidth, TOP_EDGE, RIGHT_EDGE - fQuadWidth, BOTTOM_EDGE));
  quad_bg_brightness_[1].StretchTo(
      RectF(RIGHT_EDGE - fQuadWidth, TOP_EDGE, RIGHT_EDGE, BOTTOM_EDGE));

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

void BrightnessOverlay::Update(float fDeltaTime) {
  ActorFrame::Update(fDeltaTime);
  /* If we're actually playing, then we're past fades, etc; update the
   * background brightness to follow Cover. */
  if (!GAMESTATE->gameplay_lead_in_) {
    SetActualBrightness();
  }
}

void BrightnessOverlay::SetActualBrightness() {
  float fLeftBrightness =
      1 -
      GAMESTATE->player_state_[PLAYER_1]->player_options_.GetCurrent().cover_;
  float fRightBrightness =
      1 -
      GAMESTATE->player_state_[PLAYER_2]->player_options_.GetCurrent().cover_;

  float fBaseBGBrightness = g_fBGBrightness;

  // Revision:  Themes that implement a training mode should handle the
  // brightness for it.  The engine should not force the brightness for
  // anything. -Kyz
  /*
  // HACK: Always show training in full brightness
  if( GAMESTATE->m_pCurSong && GAMESTATE->m_pCurSong->IsTutorial() )
          fBaseBGBrightness = 1.0f;
  */

  fLeftBrightness *= fBaseBGBrightness;
  fRightBrightness *= fBaseBGBrightness;

  if (!GAMESTATE->IsHumanPlayer(PLAYER_1)) {
    fLeftBrightness = fRightBrightness;
  }
  if (!GAMESTATE->IsHumanPlayer(PLAYER_2)) {
    fRightBrightness = fLeftBrightness;
  }

  RageColor LeftColor = GetBrightnessColor(fLeftBrightness);
  RageColor RightColor = GetBrightnessColor(fRightBrightness);

  quad_bg_brightness_[PLAYER_1].SetDiffuse(LeftColor);
  quad_bg_brightness_[PLAYER_2].SetDiffuse(RightColor);
  quad_bg_brightness_fade_.SetDiffuseLeftEdge(LeftColor);
  quad_bg_brightness_fade_.SetDiffuseRightEdge(RightColor);
}

void BrightnessOverlay::Set(float fBrightness) {
  RageColor c = GetBrightnessColor(fBrightness);

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
void Background::LoadFromSong(const Song* pSong) { impl_->LoadFromSong(pSong); }
void Background::Unload() { impl_->Unload(); }
void Background::FadeToActualBrightness() { impl_->FadeToActualBrightness(); }
void Background::SetBrightness(float fBrightness) {
  impl_->SetBrightness(fBrightness);
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
