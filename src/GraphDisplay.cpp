#include "global.h"

#include "GraphDisplay.h"

#include <cmath>
#include <vector>

#include "ActorUtil.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "Song.h"
#include "StageStats.h"
#include "ThemeManager.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(GraphDisplay);

enum { VALUE_RESOLUTION = 100 };

class GraphLine : public Actor {
 public:
  enum { subdivisions = 4 };
  enum { circle_vertices = subdivisions + 2 };
  void DrawPrimitives() {
    Actor::SetGlobalRenderStates();  // set Actor-specified render states

    DISPLAY->ClearAllTextures();

    // Must call this after setting the texture or else texture parameters have
		// no effect.
    Actor::SetTextureRenderStates();

    for (unsigned i = 0; i < quads_.size(); ++i) {
      quads_[i].c = this->m_pTempState->diffuse[0];
    }
    for (unsigned i = 0; i < circles_.size(); ++i) {
      circles_[i].c = this->m_pTempState->diffuse[0];
    }

    DISPLAY->DrawQuads(&quads_[0], quads_.size());

    int fans = circles_.size() / circle_vertices;
    for (int i = 0; i < fans; ++i) {
      DISPLAY->DrawFan(&circles_[0] + circle_vertices * i, circle_vertices);
    }
  }

  static void MakeCircle(
      const RageSpriteVertex& v, RageSpriteVertex* verts, int subdivisions,
      float radius) {
    verts[0] = v;

    for (int i = 0; i < subdivisions + 1; ++i) {
      const float rotation = float(i) / subdivisions * 2 * PI;
      const float x = RageFastCos(rotation) * radius;
      const float y = -RageFastSin(rotation) * radius;
      verts[1 + i] = v;
      verts[1 + i].p.x += x;
      verts[1 + i].p.y += y;
    }
  }

  void Set(const RageSpriteVertex* line_strip, int size) {
    circles_.resize(size * circle_vertices);

    for (int i = 0; i < size; ++i) {
      MakeCircle(
          line_strip[i], &circles_[0] + circle_vertices * i, subdivisions,
          1);
    }

    int num_lines = size - 1;
    quads_.resize(num_lines * 4);
    for (int i = 0; i < num_lines; ++i) {
      const RageSpriteVertex& p1 = line_strip[i];
      const RageSpriteVertex& p2 = line_strip[i + 1];

      float opp = p2.p.x - p1.p.x;
      float adj = p2.p.y - p1.p.y;
      float hyp = std::pow(opp * opp + adj * adj, 0.5f);

      float sin = opp / hyp;
      float cos = adj / hyp;

      RageSpriteVertex* v = &quads_[i * 4];
      v[0] = v[1] = p1;
      v[2] = v[3] = p2;

      int line_width = 2;
      float ydist = sin * line_width / 2;
      float xdist = cos * line_width / 2;

      v[0].p.x += xdist;
      v[0].p.y -= ydist;
      v[1].p.x -= xdist;
      v[1].p.y += ydist;
      v[2].p.x -= xdist;
      v[2].p.y += ydist;
      v[3].p.x += xdist;
      v[3].p.y -= ydist;
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
  GraphBody(RString file) {
    texture_ = TEXTUREMAN->LoadTexture(file);

    for (int i = 0; i < 2 * VALUE_RESOLUTION; ++i) {
      slices_[i].c = RageColor(1, 1, 1, 1);
      slices_[i].t = RageVector2(0, 0);
    }
  }
  ~GraphBody() {
    TEXTUREMAN->UnloadTexture(texture_);
    texture_ = nullptr;
  }

  void DrawPrimitives() {
		// Set Actor-specified render states.
    Actor::SetGlobalRenderStates();

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
    SAFE_DELETE(p);
  }
  song_boundaries_.clear();
  SAFE_DELETE(graph_line_);
  SAFE_DELETE(graph_body_);
}

void GraphDisplay::Set(
		const StageStats& stage_stats,
		const PlayerStageStats& player_stage_stats) {
  float total_step_seconds = stage_stats.GetTotalPossibleStepsSeconds();

  values_.resize(VALUE_RESOLUTION);
  player_stage_stats.GetLifeRecord(
      &values_[0], VALUE_RESOLUTION, stage_stats.GetTotalPossibleStepsSeconds());
  for (unsigned i = 0; i < ARRAYLEN(values_); ++i) {
    CLAMP(values_[i], 0.f, 1.f);
  }

  UpdateVerts();

  // Show song boundaries
  float seconds = 0;
  const std::vector<Song*>& possible_songs = stage_stats.m_vpPossibleSongs;

  std::for_each(
      possible_songs.begin(), possible_songs.end() - 1, [&](Song* song) {
        seconds += song->GetStepsSeconds();

        Actor* p = song_boundary_->Copy();
        song_boundaries_.push_back(p);
        float x = SCALE(
            seconds, 0, total_step_seconds, quad_vertices_.left,
            quad_vertices_.right);
        p->SetX(x);
        this->AddChild(p);
      });

  if (!player_stage_stats.m_bFailed) {
    // Search for the min life record to show "Just Barely!"
    float min_life_so_far = 1.0f;
    int min_life_so_far_at = 0;

    for (int i = 0; i < VALUE_RESOLUTION; ++i) {
      float life = values_[i];
      if (life < min_life_so_far) {
        min_life_so_far = life;
        min_life_so_far_at = i;
      }
    }

    if (min_life_so_far > 0.0f && min_life_so_far < 0.1f) {
      float fX = SCALE(
          float(min_life_so_far_at), 0.0f, float(VALUE_RESOLUTION - 1),
          quad_vertices_.left, quad_vertices_.right);
      barely_->SetX(fX);
    } else {
      barely_->SetVisible(false);
    }
    this->AddChild(barely_);
  }
}

void GraphDisplay::Load(RString metrics_group) {
  m_size.x = THEME->GetMetricI(metrics_group, "BodyWidth");
  m_size.y = THEME->GetMetricI(metrics_group, "BodyHeight");

  backing_.Load(THEME->GetPathG(metrics_group, "Backing"));
  backing_->ZoomToWidth(m_size.x);
  backing_->ZoomToHeight(m_size.y);
  this->AddChild(backing_);

  graph_body_ = new GraphBody(THEME->GetPathG(metrics_group, "Body"));
  this->AddChild(graph_body_);

  graph_line_ = new GraphLine;
  graph_line_->SetName("Line");
  ActorUtil::LoadAllCommands(graph_line_, metrics_group);
  this->AddChild(graph_line_);

  song_boundary_.Load(THEME->GetPathG(metrics_group, "SongBoundary"));

  barely_.Load(THEME->GetPathG(metrics_group, "Barely"));
}

void GraphDisplay::UpdateVerts() {
  quad_vertices_.left = -m_size.x / 2;
  quad_vertices_.right = m_size.x / 2;
  quad_vertices_.top = -m_size.y / 2;
  quad_vertices_.bottom = m_size.y / 2;

  RageSpriteVertex line_strip[VALUE_RESOLUTION];
  for (int i = 0; i < VALUE_RESOLUTION; ++i) {
    const float x = SCALE(
        float(i), 0.0f, float(VALUE_RESOLUTION - 1), quad_vertices_.left,
        quad_vertices_.right);
    const float y = SCALE(
        values_[i], 0.0f, 1.0f, quad_vertices_.bottom, quad_vertices_.top);

    graph_body_->slices_[i * 2 + 0].p = RageVector3(x, y, 0);
    graph_body_->slices_[i * 2 + 1].p =
        RageVector3(x, quad_vertices_.bottom, 0);

    const RectF* rect = graph_body_->texture_->GetTextureCoordRect(0);

    const float u = SCALE(
        x, quad_vertices_.left, quad_vertices_.right, rect->left,
        rect->right);
    const float v = SCALE(
        y, quad_vertices_.top, quad_vertices_.bottom, rect->top,
        rect->bottom);
    graph_body_->slices_[i * 2 + 0].t = RageVector2(u, v);
    graph_body_->slices_[i * 2 + 1].t = RageVector2(u, rect->bottom);

    line_strip[i].p = RageVector3(x, y, 0);
    line_strip[i].c = RageColor(1, 1, 1, 1);
    line_strip[i].t = RageVector2(0, 0);
  }

  graph_line_->Set(line_strip, VALUE_RESOLUTION);
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
