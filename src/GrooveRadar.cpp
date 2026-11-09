#include "GrooveRadar.h"

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
// I feel weird about this coupling, but it has to be done. -aj
#include <algorithm>
#include <cmath>
#include <vector>

#include "GameState.h"

REGISTER_ACTOR_CLASS(GrooveRadar);

static const ThemeMetric<float> RADAR_EDGE_WIDTH("GrooveRadar", "EdgeWidth");
static const ThemeMetric<float> RADAR_CENTER_ALPHA(
    "GrooveRadar", "CenterAlpha");

static float RADAR_VALUE_ROTATION(int value_index) {
  return kPi / 2 + kPi * 2 / 5.0f * value_index;
}

static const int kNumShownRadarCategories = 5;

GrooveRadar::GrooveRadar() {
  spr_radar_base_.Load(THEME->GetPathG("GrooveRadar", "base"));
  frame_.AddChild(spr_radar_base_);
  frame_.SetName("RadarFrame");
  ActorUtil::LoadAllCommands(frame_, "GrooveRadar");

  FOREACH_PlayerNumber(p) {
    // todo: remove dependency on radar base being a sprite. -aj
    groove_radar_value_map_[p].SetRadius(spr_radar_base_->GetZoomedWidth());
    frame_.AddChild(&groove_radar_value_map_[p]);
    groove_radar_value_map_[p].SetName(ssprintf("RadarValueMapP%d", p + 1));
    ActorUtil::LoadAllCommands(groove_radar_value_map_[p], "GrooveRadar");
  }

  this->AddChild(&frame_);

  for (int c = 0; c < kNumShownRadarCategories; c++) {
    spr_radar_labels_[c].SetName(ssprintf("Label%i", c + 1));
    spr_radar_labels_[c].Load(THEME->GetPathG("GrooveRadar", "labels 1x5"));
    spr_radar_labels_[c].StopAnimating();
    spr_radar_labels_[c].SetState(c);
    ActorUtil::LoadAllCommandsAndSetXY(spr_radar_labels_[c], "GrooveRadar");
    this->AddChild(&spr_radar_labels_[c]);
  }
}

void GrooveRadar::LoadFromNode(const XNode* node) {
  ActorFrame::LoadFromNode(node);
}

void GrooveRadar::SetEmpty(PlayerNumber pn) { SetFromSteps(pn, nullptr); }

void GrooveRadar::SetFromRadarValues(PlayerNumber pn, const RadarValues& rv) {
  groove_radar_value_map_[pn].SetFromSteps(rv);
}

void GrooveRadar::SetFromSteps(
    PlayerNumber pn, Steps* steps)  // nullptr means no Song
{
  if (steps == nullptr) {
    groove_radar_value_map_[pn].SetEmpty();
    return;
  }

  const RadarValues& rv = steps->GetRadarValues(pn);
  groove_radar_value_map_[pn].SetFromSteps(rv);
}

void GrooveRadar::SetFromValues(PlayerNumber pn, std::vector<float> vals) {
  groove_radar_value_map_[pn].SetFromValues(vals);
}

GrooveRadar::GrooveRadarValueMap::GrooveRadarValueMap() {
  values_visible_ = false;
  percent_toward_new_ = 0;

  for (int c = 0; c < kNumShownRadarCategories; c++) {
    values_new_[c] = 0;
    values_old_[c] = 0;
  }
}

void GrooveRadar::GrooveRadarValueMap::SetEmpty() { values_visible_ = false; }

void GrooveRadar::GrooveRadarValueMap::SetFromSteps(const RadarValues& rv) {
  values_visible_ = true;
  for (int c = 0; c < kNumShownRadarCategories; c++) {
    const float value_current = values_old_[c] * (1 - percent_toward_new_) +
                                values_new_[c] * percent_toward_new_;
    values_old_[c] = value_current;
    values_new_[c] = std::clamp(rv[c], 0.0f, 1.0f);
  }

  if (!values_visible_) {  // the values WERE invisible
    percent_toward_new_ = 1;
  } else {
    percent_toward_new_ = 0;
  }
}

void GrooveRadar::GrooveRadarValueMap::SetFromValues(std::vector<float> vals) {
  values_visible_ = true;
  for (int c = 0; c < kNumShownRadarCategories; c++) {
    const float value_current = values_old_[c] * (1 - percent_toward_new_) +
                                values_new_[c] * percent_toward_new_;
    values_old_[c] = value_current;
    values_new_[c] = vals[c];
  }

  if (!values_visible_) {  // the values WERE invisible
    percent_toward_new_ = 1;
  } else {
    percent_toward_new_ = 0;
  }
}

void GrooveRadar::GrooveRadarValueMap::Update(float delta_time) {
  ActorFrame::Update(delta_time);

  percent_toward_new_ = std::min(percent_toward_new_ + 4.0f * delta_time, 1.0f);
}

void GrooveRadar::GrooveRadarValueMap::DrawPrimitives() {
  ActorFrame::DrawPrimitives();

  // draw radar filling
  const float radius = GetUnzoomedWidth() / 2.0f * 1.1f;

  DISPLAY->ClearAllTextures();
  DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);
  RageSpriteVertex
      v[12];  // needed to draw 5 fan primitives and 10 strip primitives

  // xxx: We could either make the values invisible or draw a dot
  // (simulating real DDR). TODO: Make that choice up to the themer. -aj
  if (!values_visible_) {
    return;
  }

  // use a fan to draw the volume
  RageColor color = this->temp_state_->diffuse_[0];
  color.a_ = 0.5f;
  v[0].p_ = RageVector3(0, 0, 0);
  RageColor midcolor = color;
  midcolor.a_ = RADAR_CENTER_ALPHA;
  v[0].c_ = midcolor;
  v[1].c_ = color;

  for (int i = 0; i < kNumShownRadarCategories + 1;
       i++)  // do one extra to close the fan
  {
    const int c = i % kNumShownRadarCategories;
    const float dist_from_center =
        (values_old_[c] * (1 - percent_toward_new_) +
         values_new_[c] * percent_toward_new_ + 0.07f) *
        radius;
    const float rotation = RADAR_VALUE_ROTATION(i);
    const float x = std::cos(rotation) * dist_from_center;
    const float y = -std::sin(rotation) * dist_from_center;

    v[1 + i].p_ = RageVector3(x, y, 0);
    v[1 + i].c_ = v[1].c_;
  }

  DISPLAY->DrawFan(v, kNumShownRadarCategories + 2);

  // use a line loop to draw the thick line
  for (int i = 0; i <= kNumShownRadarCategories; i++) {
    const int c = i % kNumShownRadarCategories;
    const float dist_from_center =
        (values_old_[c] * (1 - percent_toward_new_) +
         values_new_[c] * percent_toward_new_ + 0.07f) *
        radius;
    const float rotation = RADAR_VALUE_ROTATION(i);
    const float x = std::cos(rotation) * dist_from_center;
    const float y = -std::sin(rotation) * dist_from_center;

    v[i].p_ = RageVector3(x, y, 0);
    v[i].c_ = this->temp_state_->diffuse_[0];
  }

  // TODO: Add this back in -Chris
  //	switch( PREFSMAN->m_iPolygonRadar )
  //	{
  //	case 0:		DISPLAY->DrawLoop_LinesAndPoints( v,
  // NUM_SHOWN_RADAR_CATEGORIES, RADAR_EDGE_WIDTH );	break; 	case 1:
  // DISPLAY->DrawLoop_Polys( v, NUM_SHOWN_RADAR_CATEGORIES, RADAR_EDGE_WIDTH );
  // break; 	default: 	case -1:
  DISPLAY->DrawLineStrip(v, kNumShownRadarCategories + 1, RADAR_EDGE_WIDTH);
  //	break;
  //	}
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the GrooveRadar. */
class LunaGrooveRadar : public Luna<GrooveRadar> {
 public:
  static int SetFromRadarValues(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    if (lua_isnil(L, 2)) {
      p->SetEmpty(pn);
    } else {
      RadarValues* rv = Luna<RadarValues>::check(L, 2);
      p->SetFromRadarValues(pn, *rv);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromValues(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    if (!lua_istable(L, 2) || lua_isnil(L, 2)) {
      p->SetEmpty(pn);
    } else {
      std::vector<float> vals;
      LuaHelpers::ReadArrayFromTable(vals, L);
      p->SetFromValues(pn, vals);
    }
    COMMON_RETURN_SELF;
  }
  static int SetEmpty(T* p, lua_State* L) {
    p->SetEmpty(Enum::Check<PlayerNumber>(L, 1));
    COMMON_RETURN_SELF;
  }

  LunaGrooveRadar() {
    ADD_METHOD(SetFromRadarValues);
    ADD_METHOD(SetFromValues);
    ADD_METHOD(SetEmpty);
  }
};

LUA_REGISTER_DERIVED_CLASS(GrooveRadar, ActorFrame)
// lua end

/*
 * (c) 2001-2004 Chris Danford
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
