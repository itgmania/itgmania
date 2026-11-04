#include "GraphDisplay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Song.h"
#include "StageStats.h"
#include "ThemeManager.h"

// #define DIVIDE_LINE_WIDTH
// THEME->GetMetricI(m_sName,"TexturedBottomHalf")
REGISTER_ACTOR_CLASS(GraphDisplay);

enum { VALUE_RESOLUTION = 100 };

class GraphLine : public Actor {
 public:
  enum { iSubdivisions = 4 };
  enum { iCircleVertices = iSubdivisions + 2 };
  void DrawPrimitives() {
    Actor::SetGlobalRenderStates();  // set Actor-specified render states

    DISPLAY->ClearAllTextures();

    // Must call this after setting the texture or else texture
    // parameters have no effect.
    Actor::SetTextureRenderStates();

    for (unsigned i = 0; i < quads_.size(); ++i) {
      quads_[i].c_ = this->temp_state_->diffuse_[0];
    }
    for (unsigned i = 0; i < circles_.size(); ++i) {
      circles_[i].c_ = this->temp_state_->diffuse_[0];
    }

    DISPLAY->DrawQuads(&quads_[0], quads_.size());

    int iFans = circles_.size() / iCircleVertices;
    for (int i = 0; i < iFans; ++i) {
      DISPLAY->DrawFan(&circles_[0] + iCircleVertices * i, iCircleVertices);
    }
  }

  static void MakeCircle(
      const RageSpriteVertex& v, RageSpriteVertex* pVerts, int iSubdivisions,
      float fRadius) {
    pVerts[0] = v;

    for (int i = 0; i < iSubdivisions + 1; ++i) {
      const float fRotation = float(i) / iSubdivisions * 2 * PI;
      const float fX = std::cos(fRotation) * fRadius;
      const float fY = -std::sin(fRotation) * fRadius;
      pVerts[1 + i] = v;
      pVerts[1 + i].p_.x_ += fX;
      pVerts[1 + i].p_.y_ += fY;
    }
  }

  void Set(const RageSpriteVertex* m_LineStrip, int iSize) {
    circles_.resize(iSize * iCircleVertices);

    for (int i = 0; i < iSize; ++i) {
      MakeCircle(
          m_LineStrip[i], &circles_[0] + iCircleVertices * i, iSubdivisions, 1);
    }

    int iNumLines = iSize - 1;
    quads_.resize(iNumLines * 4);
    for (int i = 0; i < iNumLines; ++i) {
      const RageSpriteVertex& p1 = m_LineStrip[i];
      const RageSpriteVertex& p2 = m_LineStrip[i + 1];

      float opp = p2.p_.x_ - p1.p_.x_;
      float adj = p2.p_.y_ - p1.p_.y_;
      float hyp = std::pow(opp * opp + adj * adj, 0.5f);

      float lsin = opp / hyp;
      float lcos = adj / hyp;

      RageSpriteVertex* v = &quads_[i * 4];
      v[0] = v[1] = p1;
      v[2] = v[3] = p2;

      int iLineWidth = 2;
      float ydist = lsin * iLineWidth / 2;
      float xdist = lcos * iLineWidth / 2;

      v[0].p_.x_ += xdist;
      v[0].p_.y_ -= ydist;
      v[1].p_.x_ -= xdist;
      v[1].p_.y_ += ydist;
      v[2].p_.x_ -= xdist;
      v[2].p_.y_ += ydist;
      v[3].p_.x_ += xdist;
      v[3].p_.y_ -= ydist;
    }
  }

  virtual GraphLine* Copy() const;

 private:
  std::vector<RageSpriteVertex> quads_;
  std::vector<RageSpriteVertex> circles_;
};
REGISTER_ACTOR_CLASS(GraphLine);

class GraphBody : public Actor {
 public:
  GraphBody(std::string sFile) {
    texture_ = TEXTUREMAN->LoadTexture(sFile);

    for (int i = 0; i < 2 * VALUE_RESOLUTION; ++i) {
      slices_[i].c_ = RageColor(1, 1, 1, 1);
      slices_[i].t_ = RageVector2(0, 0);
    }
  }
  ~GraphBody() {
    TEXTUREMAN->UnloadTexture(texture_);
    texture_ = nullptr;
  }

  void DrawPrimitives() {
    Actor::SetGlobalRenderStates();  // set Actor-specified render states

    DISPLAY->ClearAllTextures();
    DISPLAY->SetTexture(TextureUnit_1, texture_->GetTexHandle());

    // Must call this after setting the texture or else texture
    // parameters have no effect.
    Actor::SetTextureRenderStates();

    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);
    DISPLAY->DrawQuadStrip(slices_, ARRAYLEN(slices_));
  }

  RageTexture* texture_;
  RageSpriteVertex slices_[2 * VALUE_RESOLUTION];
};

GraphDisplay::GraphDisplay() {
  graph_line_ = nullptr;
  graph_body_ = nullptr;
}

GraphDisplay::~GraphDisplay() {
  for (Actor* p : song_boundaries_) {
    RageUtil::SafeDelete(p);
  }
  song_boundaries_.clear();
  RageUtil::SafeDelete(graph_line_);
  RageUtil::SafeDelete(graph_body_);
}

void GraphDisplay::Set(const StageStats& ss, const PlayerStageStats& pss) {
  float fTotalStepSeconds = ss.GetTotalPossibleStepsSeconds();

  values_.resize(VALUE_RESOLUTION);
  pss.GetLifeRecord(
      &values_[0], VALUE_RESOLUTION, ss.GetTotalPossibleStepsSeconds());
  for (unsigned i = 0; i < ARRAYLEN(values_); i++) {
    rage_clamp(values_[i], 0.f, 1.f);
  }

  UpdateVerts();

  // Show song boundaries
  float fSec = 0;
  const std::vector<Song*>& possibleSongs = ss.possible_songs_;

  std::for_each(
      possibleSongs.begin(), possibleSongs.end() - 1, [&](Song* song) {
        fSec += song->GetStepsSeconds();

        Actor* p = spr_song_boundary_->Copy();
        song_boundaries_.push_back(p);
        float fX = SCALE(
            fSec, 0, fTotalStepSeconds, quad_vertices_.left_,
            quad_vertices_.right_);
        p->SetX(fX);
        this->AddChild(p);
      });

  if (!pss.failed_) {
    // Search for the min life record to show "Just Barely!"
    float fMinLifeSoFar = 1.0f;
    int iMinLifeSoFarAt = 0;

    for (int i = 0; i < VALUE_RESOLUTION; ++i) {
      float fLife = values_[i];
      if (fLife < fMinLifeSoFar) {
        fMinLifeSoFar = fLife;
        iMinLifeSoFarAt = i;
      }
    }

    if (fMinLifeSoFar > 0.0f && fMinLifeSoFar < 0.1f) {
      float fX = SCALE(
          float(iMinLifeSoFarAt), 0.0f, float(VALUE_RESOLUTION - 1),
          quad_vertices_.left_, quad_vertices_.right_);
      spr_barely_->SetX(fX);
    } else {
      spr_barely_->SetVisible(false);
    }
    this->AddChild(spr_barely_);
  }
}

void GraphDisplay::Load(std::string sMetricsGroup) {
  size_.x_ = THEME->GetMetricI(sMetricsGroup, "BodyWidth");
  size_.y_ = THEME->GetMetricI(sMetricsGroup, "BodyHeight");

  spr_backing_.Load(THEME->GetPathG(sMetricsGroup, "Backing"));
  spr_backing_->ZoomToWidth(size_.x_);
  spr_backing_->ZoomToHeight(size_.y_);
  this->AddChild(spr_backing_);

  graph_body_ = new GraphBody(THEME->GetPathG(sMetricsGroup, "Body"));
  this->AddChild(graph_body_);

  graph_line_ = new GraphLine;
  graph_line_->SetName("Line");
  ActorUtil::LoadAllCommands(graph_line_, sMetricsGroup);
  this->AddChild(graph_line_);

  spr_song_boundary_.Load(THEME->GetPathG(sMetricsGroup, "SongBoundary"));

  spr_barely_.Load(THEME->GetPathG(sMetricsGroup, "Barely"));
}

void GraphDisplay::UpdateVerts() {
  quad_vertices_.left_ = -size_.x_ / 2;
  quad_vertices_.right_ = size_.x_ / 2;
  quad_vertices_.top_ = -size_.y_ / 2;
  quad_vertices_.bottom_ = size_.y_ / 2;

  RageSpriteVertex LineStrip[VALUE_RESOLUTION];
  for (int i = 0; i < VALUE_RESOLUTION; ++i) {
    const float fX = SCALE(
        float(i), 0.0f, float(VALUE_RESOLUTION - 1), quad_vertices_.left_,
        quad_vertices_.right_);
    const float fY = SCALE(
        values_[i], 0.0f, 1.0f, quad_vertices_.bottom_, quad_vertices_.top_);

    graph_body_->slices_[i * 2 + 0].p_ = RageVector3(fX, fY, 0);
    graph_body_->slices_[i * 2 + 1].p_ =
        RageVector3(fX, quad_vertices_.bottom_, 0);

    const RectF* pRect = graph_body_->texture_->GetTextureCoordRect(0);

    const float fU = SCALE(
        fX, quad_vertices_.left_, quad_vertices_.right_, pRect->left_,
        pRect->right_);
    const float fV = SCALE(
        fY, quad_vertices_.top_, quad_vertices_.bottom_, pRect->top_,
        pRect->bottom_);
    graph_body_->slices_[i * 2 + 0].t_ = RageVector2(fU, fV);
    graph_body_->slices_[i * 2 + 1].t_ = RageVector2(fU, pRect->bottom_);

    LineStrip[i].p_ = RageVector3(fX, fY, 0);
    LineStrip[i].c_ = RageColor(1, 1, 1, 1);
    LineStrip[i].t_ = RageVector2(0, 0);
  }

  graph_line_->Set(LineStrip, VALUE_RESOLUTION);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the GraphDisplay. */
class LunaGraphDisplay : public Luna<GraphDisplay> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int Set(T* p, lua_State* L) {
    StageStats* pStageStats = Luna<StageStats>::check(L, 1);
    PlayerStageStats* pPlayerStageStats = Luna<PlayerStageStats>::check(L, 2);
    if (pStageStats == nullptr) {
      luaL_error(L, "The StageStats passed to GraphDisplay:Set are nil.");
    }
    if (pPlayerStageStats == nullptr) {
      luaL_error(L, "The PlayerStageStats passed to GraphDisplay:Set are nil.");
    }
    p->Set(*pStageStats, *pPlayerStageStats);
    COMMON_RETURN_SELF;
  }

  LunaGraphDisplay() {
    ADD_METHOD(Load);
    ADD_METHOD(Set);
  }
};

LUA_REGISTER_DERIVED_CLASS(GraphDisplay, ActorFrame)
// lua end

/*
 * (c) 2003 Glenn Maynard
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
