#include "ActorMultiVertex.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "CubicSpline.h"
#include "EnumHelper.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Tween.h"
#include "XmlFile.h"
#include "global.h"

const float min_state_delay = 0.0001f;

static const char* DrawModeNames[] = {
    "Quads",     "QuadStrip",          "Fan", "Strip", "Triangles",
    "LineStrip", "SymmetricQuadStrip",
};

static const int DrawModeVertexGroupSizes[] = {
    4,  // Quads
    2,  // QuadStrip
    1,  // Fan
    1,  // Strip
    3,  // Triangles
    1,  // LineStrip
    3,  // SymmetricQuadStrip
    1,  // NUM_DrawMode
    1,  // DrawMode_Invalid
};

static const int DrawModeMinimumToDraw[] = {
    4,  // Quads
    4,  // QuadStrip
    3,  // Fan
    3,  // Strip
    3,  // Triangles
    2,  // LineStrip
    6,  // SymmetricQuadStrip
    1,  // NUM_DrawMode
    1,  // DrawMode_Invalid
};

XToString(DrawMode);
XToLocalizedString(DrawMode);
LuaXType(DrawMode);

REGISTER_ACTOR_CLASS(ActorMultiVertex);

ActorMultiVertex::ActorMultiVertex() {
  // Use blank texture by default.
  RageTextureID ID = TEXTUREMAN->GetDefaultTextureID();
  texture_ = TEXTUREMAN->LoadTexture(ID);

  effect_mode_ = EffectMode_Normal;
  texture_mode_ = TextureMode_Modulate;
  splines_.resize(kNumVertSplines);
  for (size_t i = 0; i < kNumVertSplines; ++i) {
    splines_[i].redimension(3);
    splines_[i].owned_by_actor_ = true;
  }
  skip_next_update_ = true;
  decode_movie_ = true;
  use_animation_state_ = false;
  secs_into_state_ = 0.0f;
  cur_state_ = 0;
}

ActorMultiVertex::~ActorMultiVertex() { UnloadTexture(); }

ActorMultiVertex::ActorMultiVertex(const ActorMultiVertex& cpy) : Actor(cpy) {
  amv_tweens_ = cpy.amv_tweens_;
  amv_current_ = cpy.amv_current_;
  amv_start_ = cpy.amv_start_;
  effect_mode_ = cpy.effect_mode_;
  texture_mode_ = cpy.texture_mode_;
  splines_ = cpy.splines_;
  skip_next_update_ = cpy.skip_next_update_;
  use_animation_state_ = cpy.use_animation_state_;
  secs_into_state_ = cpy.secs_into_state_;
  cur_state_ = cpy.cur_state_;
  states_ = cpy.states_;

  if (cpy.texture_ != nullptr) {
    texture_ = TEXTUREMAN->CopyTexture(cpy.texture_);
  } else {
    texture_ = nullptr;
  }
}

void ActorMultiVertex::LoadFromNode(const XNode* Node) {
  std::string path;
  Node->GetAttrValue("Texture", path);
  if (!path.empty() && !TEXTUREMAN->IsTextureRegistered(RageTextureID(path))) {
    ActorUtil::GetAttrPath(Node, "Texture", path);
  }
  if (!path.empty()) {
    LoadFromTexture(path);
  }

  Actor::LoadFromNode(Node);
}

void ActorMultiVertex::SetTexture(RageTexture* Texture) {
  if (texture_ != Texture) {
    UnloadTexture();
    texture_ = Texture;
  }
}

void ActorMultiVertex::LoadFromTexture(RageTextureID ID) {
  RageTexture* Texture = nullptr;
  if (texture_ && texture_->GetID() == ID) {
    return;
  }
  Texture = TEXTUREMAN->LoadTexture(ID);
  SetTexture(Texture);
}

void ActorMultiVertex::UnloadTexture() {
  if (texture_ != nullptr) {
    TEXTUREMAN->UnloadTexture(texture_);
    texture_ = nullptr;
  }
}

void ActorMultiVertex::SetNumVertices(size_t n) {
  if (n == 0) {
    for (size_t i = 0; i < amv_tweens_.size(); ++i) {
      amv_tweens_[i].vertices_.clear();
    }
    amv_current_.vertices_.clear();
    amv_start_.vertices_.clear();
  } else {
    for (size_t i = 0; i < amv_tweens_.size(); ++i) {
      amv_tweens_[i].vertices_.resize(n);
    }
    amv_current_.vertices_.resize(n);
    amv_start_.vertices_.resize(n);
  }
}

void ActorMultiVertex::AddVertices(int Add) {
  int size = AMV_DestTweenState().vertices_.size();
  size += Add;
  for (size_t i = 0; i < amv_tweens_.size(); ++i) {
    amv_tweens_[i].vertices_.resize(size);
  }
  amv_current_.vertices_.resize(size);
  amv_start_.vertices_.resize(size);
}

void ActorMultiVertex::SetVertexPos(int index, float x, float y, float z) {
  AMV_DestTweenState().vertices_[index].p_ = RageVector3(x, y, z);
}

void ActorMultiVertex::SetVertexColor(int index, RageColor c) {
  AMV_DestTweenState().vertices_[index].c_ = c;
}

void ActorMultiVertex::SetVertexCoords(
    int index, float TexCoordX, float TexCoordY) {
  AMV_DestTweenState().vertices_[index].t_ = RageVector2(TexCoordX, TexCoordY);
}

void ActorMultiVertex::DrawPrimitives() {
  Actor::SetGlobalRenderStates();  // set Actor-specified render states

  DISPLAY->ClearAllTextures();
  DISPLAY->SetTexture(TextureUnit_1, texture_->GetTexHandle());

  Actor::SetTextureRenderStates();
  DISPLAY->SetEffectMode(effect_mode_);

  // set temporary diffuse and glow
  static AMV_TweenState TS;

  amv_temp_state_ = &TS;
  TS = amv_current_;

  // skip if no change or fully transparent
  if (temp_state_->diffuse_[0] != RageColor(1, 1, 1, 1) &&
      temp_state_->diffuse_[0].a_ > 0) {
    for (size_t i = 0; i < TS.vertices_.size(); i++) {
      // RageVColor uses a uint8_t for each channel.  0-255.
      // RageColor uses a float. 0-1.
      // So each channel of the RageVColor needs to be converted to a float,
      // multiplied by the channel from the RageColor, then the result
      // converted to uint8_t.  If implicit conversion is allowed to happen,
      // sometimes the compiler decides to turn the RageColor into a uint8_t,
      // which makes any value other than 1 into 0.  Thus, the explicit
      // conversions.  -Kyz
#define MULT_COLOR_ELEMENTS(color_a, color_b) \
  color_a = static_cast<uint8_t>(static_cast<float>(color_a) * color_b);
      // RageVColor * RageColor
      MULT_COLOR_ELEMENTS(TS.vertices_[i].c_.b_, temp_state_->diffuse_[0].b_);
      MULT_COLOR_ELEMENTS(TS.vertices_[i].c_.r_, temp_state_->diffuse_[0].r_);
      MULT_COLOR_ELEMENTS(TS.vertices_[i].c_.g_, temp_state_->diffuse_[0].g_);
      MULT_COLOR_ELEMENTS(TS.vertices_[i].c_.a_, temp_state_->diffuse_[0].a_);
#undef MULT_COLOR_ELEMENTS
    }
  }

  // Draw diffuse pass.
  if (temp_state_->diffuse_[0].a_ > 0) {
    DISPLAY->SetTextureMode(TextureUnit_1, texture_mode_);
    DrawInternal(amv_temp_state_);
  }

  // Draw the glow pass
  if (temp_state_->glow_.a_ > 0) {
    for (size_t i = 0; i < TS.vertices_.size(); i++) {
      TS.vertices_[i].c_ = temp_state_->glow_;
    }
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Glow);
    DrawInternal(amv_temp_state_);
  }
}

void ActorMultiVertex::DrawInternal(const AMV_TweenState* TS) {
  int FirstToDraw = TS->first_to_draw_;
  int NumToDraw = TS->GetSafeNumToDraw(TS->draw_mode_, TS->num_to_draw_);

  if (NumToDraw == 0) {
    // Nothing to draw.
    return;
  }

  switch (TS->draw_mode_) {
    case DrawMode_Quads: {
      DISPLAY->DrawQuads(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    case DrawMode_QuadStrip: {
      DISPLAY->DrawQuadStrip(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    case DrawMode_Fan: {
      DISPLAY->DrawFan(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    case DrawMode_Strip: {
      DISPLAY->DrawStrip(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    case DrawMode_Triangles: {
      DISPLAY->DrawTriangles(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    case DrawMode_LineStrip: {
      DISPLAY->DrawLineStrip(
          &TS->vertices_[FirstToDraw], NumToDraw, TS->line_width_);
      break;
    }
    case DrawMode_SymmetricQuadStrip: {
      DISPLAY->DrawSymmetricQuadStrip(&TS->vertices_[FirstToDraw], NumToDraw);
      break;
    }
    default:
      break;
  }

  DISPLAY->SetEffectMode(EffectMode_Normal);
}

bool ActorMultiVertex::EarlyAbortDraw() const {
  if (amv_current_.first_to_draw_ >= (int)amv_current_.vertices_.size() ||
      amv_current_.draw_mode_ >= NUM_DrawMode) {
    return true;
  }
  return false;
}

void ActorMultiVertex::SetVertsFromSplinesInternal(
    size_t num_splines, size_t offset) {
  std::vector<RageSpriteVertex>& verts = AMV_DestTweenState().vertices_;
  size_t first = AMV_DestTweenState().first_to_draw_ + offset;
  size_t num_verts =
      AMV_DestTweenState().GetSafeNumToDraw(
          AMV_DestTweenState().draw_mode_, AMV_DestTweenState().num_to_draw_) -
      offset;
  std::vector<float> tper(num_splines, 0.0f);
  float num_parts =
      (static_cast<float>(num_verts) / static_cast<float>(num_splines)) - 1.0f;
  for (size_t i = 0; i < num_splines; ++i) {
    tper[i] = splines_[i].get_max_t() / num_parts;
  }
  for (size_t v = 0; v < num_verts; ++v) {
    std::vector<float> pos;
    const int spi = v % num_splines;
    float part = static_cast<float>(v / num_splines);
    splines_[spi].evaluate(part * tper[spi], pos);
    verts[v + first].p_.x_ = pos[0];
    verts[v + first].p_.y_ = pos[1];
    verts[v + first].p_.z_ = pos[2];
  }
}

void ActorMultiVertex::SetVertsFromSplines() {
  if (AMV_DestTweenState().vertices_.empty()) {
    return;
  }
  switch (AMV_DestTweenState().draw_mode_) {
    case DrawMode_Quads:
      SetVertsFromSplinesInternal(4, 0);
      break;
    case DrawMode_QuadStrip:
    case DrawMode_Strip:
      SetVertsFromSplinesInternal(2, 0);
      break;
    case DrawMode_Fan:
      // Skip the first vert because it is the center of the fan. -Kyz
      SetVertsFromSplinesInternal(1, 1);
      break;
    case DrawMode_Triangles:
    case DrawMode_SymmetricQuadStrip:
      SetVertsFromSplinesInternal(3, 0);
      break;
    case DrawMode_LineStrip:
      SetVertsFromSplinesInternal(1, 0);
      break;
    default:
      break;
  }
}

CubicSplineN* ActorMultiVertex::GetSpline(size_t i) {
  ASSERT(i < kNumVertSplines);
  return &(splines_[i]);
}

void ActorMultiVertex::SetState(int i) {
  ASSERT(i >= 0 && static_cast<size_t>(i) < states_.size());
  cur_state_ = i;
  secs_into_state_ = 0.0f;
}

void ActorMultiVertex::SetAllStateDelays(float delay) {
  for (State& s : states_) {
    s.delay_ = delay;
  }
}

float ActorMultiVertex::GetAnimationLengthSeconds() const {
  auto calcDelay = [](float total, const State& s) { return total + s.delay_; };
  return std::accumulate(states_.begin(), states_.end(), 0.f, calcDelay);
}

void ActorMultiVertex::SetSecondsIntoAnimation(float seconds) {
  SetState(0);
  if (texture_) {
    texture_->SetPosition(seconds);
  }
  secs_into_state_ = seconds;
  UpdateAnimationState(true);
}

void ActorMultiVertex::UpdateAnimationState(bool force_update) {
  AMV_TweenState& dest = AMV_DestTweenState();
  std::vector<RageSpriteVertex>& verts = dest.vertices_;
  std::vector<size_t>& qs = dest.quad_states_;
  if (!use_animation_state_ || states_.empty() ||
      dest.draw_mode_ == DrawMode_LineStrip || qs.empty()) {
    return;
  }
  bool state_changed = force_update;
  if (states_.size() > 1) {
    while (states_[cur_state_].delay_ > min_state_delay &&
           secs_into_state_ + min_state_delay > states_[cur_state_].delay_) {
      secs_into_state_ -= states_[cur_state_].delay_;
      cur_state_ = (cur_state_ + 1) % states_.size();
      state_changed = true;
    }
  }
  if (state_changed) {
    size_t first = dest.first_to_draw_;
    size_t last =
        first + dest.GetSafeNumToDraw(dest.draw_mode_, dest.num_to_draw_);

    switch (AMV_DestTweenState().draw_mode_) {
      case DrawMode_Quads:
        for (size_t i = first; i < last; ++i) {
          const size_t quad_id = (i - first) / 4;
          const size_t state_id =
              (cur_state_ + qs[quad_id % qs.size()]) % states_.size();
          const auto& rect = states_[state_id].rect_;

          switch ((i - first) % 4) {
            case 0:
              verts[i].t_.x_ = rect.left_;
              verts[i].t_.y_ = rect.top_;
              break;

            case 1:
              verts[i].t_.x_ = rect.right_;
              verts[i].t_.y_ = rect.top_;
              break;

            case 2:
              verts[i].t_.x_ = rect.right_;
              verts[i].t_.y_ = rect.bottom_;
              break;

            case 3:
              verts[i].t_.x_ = rect.left_;
              verts[i].t_.y_ = rect.bottom_;
              break;
          }
        }
        [[fallthrough]];
      case DrawMode_QuadStrip:
        for (size_t i = first; i < last; ++i) {
          const size_t quad_id = (i - first) / 2;
          const size_t state_id =
              (cur_state_ + qs[quad_id % qs.size()]) % states_.size();
          const auto& rect = states_[state_id].rect_;

          if ((i - first) % 2 == 0) {
            verts[i].t_.x_ = rect.left_;
            verts[i].t_.y_ = rect.top_;
          } else {
            verts[i].t_.x_ = rect.left_;
            verts[i].t_.y_ = rect.bottom_;
          }
        }
        break;
      case DrawMode_Strip:
      case DrawMode_Fan:
        for (size_t i = first; i < last; ++i) {
          const size_t quad_id = (i - first);
          const size_t state_id =
              (cur_state_ + qs[quad_id % qs.size()]) % states_.size();
          const auto& rect = states_[state_id].rect_;

          verts[i].t_.x_ = rect.left_;
          verts[i].t_.y_ = rect.top_;
        }
        break;
      case DrawMode_Triangles:
        for (size_t i = first; i < last; ++i) {
          const size_t quad_id = (i - first) / 3;
          const size_t state_id =
              (cur_state_ + qs[quad_id % qs.size()]) % states_.size();
          const auto& rect = states_[state_id].rect_;

          switch ((i - first) % 3) {
            case 0:
              verts[i].t_.x_ = rect.left_;
              verts[i].t_.y_ = rect.top_;
              break;
            case 1:
              verts[i].t_.x_ = rect.right_;
              verts[i].t_.y_ = rect.top_;
              break;
            case 2:
              verts[i].t_.x_ = rect.right_;
              verts[i].t_.y_ = rect.bottom_;
              break;
          }
        }
        break;
      case DrawMode_SymmetricQuadStrip:
        for (size_t i = first; i < last; ++i) {
          const size_t quad_id = (i - first) / 3;
          const size_t state_id =
              (cur_state_ + qs[quad_id % qs.size()]) % states_.size();
          const auto& rect = states_[state_id].rect_;

          switch ((i - first) % 3) {
            case 0:
            case 2:
              verts[i].t_.x_ = rect.left_;
              verts[i].t_.y_ = rect.top_;
              break;
            case 1:
              verts[i].t_.x_ = rect.right_;
              verts[i].t_.y_ = rect.top_;
              break;
          }
        }
        break;
      default:
        break;
    }
  }
}

void ActorMultiVertex::EnableAnimation(bool bEnable) {
  bool bWasEnabled = is_animating_;
  Actor::EnableAnimation(bEnable);

  if (bEnable && !bWasEnabled) {
    skip_next_update_ = true;
  }
}

void ActorMultiVertex::Update(float fDelta) {
  Actor::Update(fDelta);  // do tweening
  const bool skip_this_movie_update = skip_next_update_;
  skip_next_update_ = false;
  if (!is_animating_) {
    return;
  }
  if (!texture_) {
    return;
  }
  float time_passed = GetEffectDeltaTime();
  secs_into_state_ += time_passed;
  if (secs_into_state_ < 0) {
    wrap(secs_into_state_, GetAnimationLengthSeconds());
  }
  UpdateAnimationState();
  if (!skip_this_movie_update && decode_movie_) {
    texture_->UpdateMovie(std::max(0.0f, time_passed));
  }
}

void ActorMultiVertex::SetCurrentTweenStart() { amv_start_ = amv_current_; }

void ActorMultiVertex::EraseHeadTween() {
  amv_current_ = amv_tweens_[0];
  amv_tweens_.erase(amv_tweens_.begin());
}

void ActorMultiVertex::UpdatePercentThroughTween(float PercentThroughTween) {
  AMV_TweenState::MakeWeightedAverage(
      amv_current_, amv_start_, amv_tweens_[0], PercentThroughTween);
}

void ActorMultiVertex::BeginTweening(float time, ITween* pTween) {
  Actor::BeginTweening(time, pTween);

  if (!amv_tweens_.empty())  // if there was already a TS on the stack
  {
    amv_tweens_.push_back(amv_tweens_.back());
  } else {
    amv_tweens_.push_back(amv_current_);
  }
}

void ActorMultiVertex::StopTweening() {
  amv_tweens_.clear();
  Actor::StopTweening();
}

void ActorMultiVertex::FinishTweening() {
  if (!amv_tweens_.empty()) {
    amv_current_ = AMV_DestTweenState();
  }
  Actor::FinishTweening();
}

void ActorMultiVertex::AMV_TweenState::SetDrawState(
    DrawMode dm, int first, int num) {
  if (first >= (int)vertices_.size() && vertices_.size() > 0) {
    LuaHelpers::ReportScriptErrorFmt(
        "ActorMultiVertex:SetDrawState: FirstToDraw > vertices.size(), %d > %u",
        first_to_draw_ + 1, (unsigned int)vertices_.size());
    return;
  }
  int safe_num = GetSafeNumToDraw(dm, num);
  if (num != safe_num && num != -1) {
    LuaHelpers::ReportScriptErrorFmt(
        "ActorMultiVertex:SetDrawState: NumToDraw %d is not valid for %u "
        "vertices with DrawMode %s",
        num, (unsigned int)vertices_.size(), DrawModeNames[dm]);
    return;
  }
  draw_mode_ = dm;
  first_to_draw_ = first;
  num_to_draw_ = num;
}

void ActorMultiVertex::AMV_TweenState::MakeWeightedAverage(
    AMV_TweenState& average_out, const AMV_TweenState& ts1,
    const AMV_TweenState& ts2, float percent_between) {
  average_out.line_width_ =
      lerp(percent_between, ts1.line_width_, ts2.line_width_);
  for (size_t v = 0; v < average_out.vertices_.size(); ++v) {
    WeightedAvergeOfRSVs(
        average_out.vertices_[v], ts1.vertices_[v], ts2.vertices_[v],
        percent_between);
  }
}

int ActorMultiVertex::AMV_TweenState::GetSafeNumToDraw(
    DrawMode dm, int num) const {
  int max = vertices_.size() - first_to_draw_;
  // NumToDraw == -1 draws all vertices
  if (num == -1 || num > max) {
    num = max;
  }
  num -= (num % DrawModeVertexGroupSizes[dm]);
  if (num < DrawModeMinimumToDraw[dm]) {
    num = 0;
  }
  return num;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ActorMultiVertex. */
class LunaActorMultiVertex : public Luna<ActorMultiVertex> {
 public:
  static int SetNumVertices(T* p, lua_State* L) {
    p->SetNumVertices(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetNumVertices(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumVertices());
    return 1;
  }

  static void SetVertexFromStack(
      T* p, lua_State* L, size_t VertexIndex, int DataStackIndex) {
    // Use the number of arguments to determine which property a table is for
    if (lua_type(L, DataStackIndex) != LUA_TTABLE) {
      LuaHelpers::ReportScriptErrorFmt(
          "ActorMultiVertex::SetVertex: non-table parameter supplied. Table of "
          "tables of vertex data expected.");
      return;
    }
    size_t NumDataParts = lua_objlen(L, DataStackIndex);
    for (size_t i = 0; i < NumDataParts; ++i) {
      lua_pushnumber(L, i + 1);
      lua_gettable(L, DataStackIndex);
      int DataPieceIndex = lua_gettop(L);
      size_t DataPieceElements = lua_objlen(L, DataPieceIndex);
      if (lua_type(L, DataPieceIndex) != LUA_TTABLE) {
        LuaHelpers::ReportScriptErrorFmt(
            "ActorMultiVertex::SetVertex: non-table parameter %u supplied "
            "inside table of parameters, table expected.",
            (unsigned int)i);
        return;
      }
      int pushes = 1;
      if (DataPieceElements == 2) {
        pushes += 2;
        lua_rawgeti(L, DataPieceIndex, 1);
        float x = FArg(-1);
        lua_rawgeti(L, DataPieceIndex, 2);
        float y = FArg(-1);
        p->SetVertexCoords(VertexIndex, x, y);
      } else if (DataPieceElements == 3) {
        pushes += 3;
        lua_rawgeti(L, DataPieceIndex, 1);
        float x = FArg(-1);
        lua_rawgeti(L, DataPieceIndex, 2);
        float y = FArg(-1);
        lua_rawgeti(L, DataPieceIndex, 3);
        float z = FArg(-1);
        p->SetVertexPos(VertexIndex, x, y, z);
      } else if (DataPieceElements == 4) {
        // RageColor pops the things it pushes onto the stack, so we don't need
        // to.
        RageColor c;
        // Does not use FromStackCompat because we are not compatible with
        // passing a color in non-table form.
        c.FromStack(L, DataPieceIndex);
        p->SetVertexColor(VertexIndex, c);
      } else {
        LuaHelpers::ReportScriptErrorFmt(
            "ActorMultiVertex::SetVertex: Parameter %u has %u elements "
            "supplied. 2, 3, or 4 expected.",
            (unsigned int)i, (unsigned int)DataPieceElements);
      }
      // Avoid a stack underflow by only popping the amount we pushed.
      lua_pop(L, pushes);
    }
    return;
  }

  static int SetVertex(T* p, lua_State* L) {
    // Indices from Lua are one-indexed.  -1 to adjust.
    int Index = IArg(1) - 1;
    if (Index < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "ActorMultiVertex::SetVertex: index %d provided, cannot set Index < "
          "1",
          Index + 1);
      COMMON_RETURN_SELF;
    } else if (Index == (int)p->GetNumVertices()) {
      p->AddVertices(1);
    } else if (Index > (int)p->GetNumVertices()) {
      LuaHelpers::ReportScriptErrorFmt(
          "ActorMultiVertex::SetVertex: Cannot set vertex %d if there is no "
          "vertex %d, only %u vertices.",
          Index + 1, Index, (unsigned int)p->GetNumVertices());
      COMMON_RETURN_SELF;
    }
    SetVertexFromStack(p, L, Index, lua_gettop(L));
    COMMON_RETURN_SELF;
  }

  static int SetVertices(T* p, lua_State* L) {
    int First = 0;
    int StackIndex = lua_gettop(L);
    // Allow the user to just pass a table without specifying a starting point.
    if (lua_type(L, 1) == LUA_TNUMBER) {
      // Indices from Lua are one-indexed.  -1 to adjust.
      First = IArg(1) - 1;
      if (First < 0) {
        LuaHelpers::ReportScriptErrorFmt(
            "ActorMultiVertex::SetVertices: index %d provided, cannot set "
            "Index < 1",
            First + 1);
        COMMON_RETURN_SELF;
      }
    }
    int Last = First + lua_objlen(L, StackIndex);
    if (Last > (int)p->GetNumVertices()) {
      p->AddVertices(Last - p->GetNumVertices());
    }
    for (int n = First; n < Last; ++n) {
      lua_pushnumber(L, n - First + 1);
      lua_gettable(L, StackIndex);
      SetVertexFromStack(p, L, n, lua_gettop(L));
      lua_pop(L, 1);
    }
    COMMON_RETURN_SELF;
  }

  static int SetEffectMode(T* p, lua_State* L) {
    EffectMode em = Enum::Check<EffectMode>(L, 1);
    p->SetEffectMode(em);
    COMMON_RETURN_SELF;
  }

  static int SetTextureMode(T* p, lua_State* L) {
    TextureMode tm = Enum::Check<TextureMode>(L, 1);
    p->SetTextureMode(tm);
    COMMON_RETURN_SELF;
  }

  static int SetLineWidth(T* p, lua_State* L) {
    float Width = FArg(1);
    if (Width < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "ActorMultiVertex::SetLineWidth: cannot set negative width.");
      COMMON_RETURN_SELF;
    }
    p->SetLineWidth(Width);
    COMMON_RETURN_SELF;
  }

  static int SetDrawState(T* p, lua_State* L) {
    DrawMode dm = p->GetDestDrawMode();
    int first = p->GetDestFirstToDraw();
    int num = p->GetDestNumToDraw();
    int ArgsIndex = 1;
    if (!lua_istable(L, ArgsIndex)) {
      LuaHelpers::ReportScriptErrorFmt(
          "ActorMultiVertex:SetDrawState: Table expected, something else "
          "recieved.  Doing nothing.");
      COMMON_RETURN_SELF;
    }
    // Fetch the draw mode, if provided.
    lua_getfield(L, ArgsIndex, "Mode");
    if (!lua_isnil(L, -1)) {
      dm = Enum::Check<DrawMode>(L, -1);
    }
    lua_pop(L, 1);
    // Fetch FirstToDraw, if provided.
    lua_getfield(L, ArgsIndex, "First");
    if (!lua_isnil(L, -1)) {
      // Indices from Lua are one-indexed.  -1 to adjust.
      first = IArg(-1) - 1;
    }
    lua_pop(L, 1);
    // Fetch NumToDraw, if provided.
    lua_getfield(L, ArgsIndex, "Num");
    if (!lua_isnil(L, -1)) {
      num = IArg(-1);
    }
    lua_pop(L, 1);
    p->SetDrawState(dm, first, num);
    COMMON_RETURN_SELF;
  }

  static int GetDestDrawMode(T* p, lua_State* L) {
    Enum::Push(L, p->GetDestDrawMode());
    return 1;
  }

  static int GetDestFirstToDraw(T* p, lua_State* L) {
    // Indices in Lua are one-indexed.  +1 to adjust.
    lua_pushnumber(L, p->GetDestFirstToDraw() + 1);
    return 1;
  }

  static int GetDestNumToDraw(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDestNumToDraw());
    return 1;
  }

  static int GetCurrDrawMode(T* p, lua_State* L) {
    Enum::Push(L, p->GetCurrDrawMode());
    return 1;
  }

  static int GetCurrFirstToDraw(T* p, lua_State* L) {
    // Indices in Lua are one-indexed.  +1 to adjust.
    lua_pushnumber(L, p->GetCurrFirstToDraw() + 1);
    return 1;
  }

  static int GetCurrNumToDraw(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCurrNumToDraw());
    return 1;
  }

  static int LoadTexture(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->UnloadTexture();
      p->LoadFromTexture(TEXTUREMAN->GetDefaultTextureID());
    } else {
      RageTextureID ID(SArg(1));
      TEXTUREMAN->DisableOddDimensionWarning();
      p->LoadFromTexture(ID);
      TEXTUREMAN->EnableOddDimensionWarning();
    }
    COMMON_RETURN_SELF;
  }

  static int GetSpline(T* p, lua_State* L) {
    size_t i = static_cast<size_t>(IArg(1) - 1);
    if (i >= ActorMultiVertex::kNumVertSplines) {
      luaL_error(
          L,
          "Spline index must be greater than 0 and less than or equal to %zu.",
          ActorMultiVertex::kNumVertSplines);
    }
    p->GetSpline(i)->PushSelf(L);
    return 1;
  }

  static int SetVertsFromSplines(T* p, lua_State* L) {
    p->SetVertsFromSplines();
    COMMON_RETURN_SELF;
  }

  DEFINE_METHOD(GetUseAnimationState, use_animation_state_);
  static int SetUseAnimationState(T* p, lua_State* L) {
    p->use_animation_state_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetNumStates(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumStates());
    return 1;
  }
  static void FillStateFromLua(
      lua_State* L, ActorMultiVertex::State& state, RageTexture* tex,
      int index) {
    if (tex == nullptr) {
      luaL_error(L, "The texture must be set before adding states.");
    }
    // State looks like this:
    // {{left, top, right, bottom}, delay}
#define DATA_ERROR(i)                                                       \
  if (!lua_istable(L, i)) {                                                 \
    luaL_error(                                                             \
        L,                                                                  \
        "The state data must be in a table like this: {{left, top, right, " \
        "bottom}, delay}");                                                 \
  }
#define SET_SIDE(i, side) \
  lua_rawgeti(L, -1, i);  \
  state.side = FArg(-1);  \
  lua_pop(L, 1);

    DATA_ERROR(index);
    lua_rawgeti(L, index, 1);
    DATA_ERROR(-1);
    SET_SIDE(1, rect_.left_);
    SET_SIDE(2, rect_.top_);
    SET_SIDE(3, rect_.right_);
    SET_SIDE(4, rect_.bottom_);
    lua_pop(L, 1);
    SET_SIDE(2, delay_);
    const float width_ratio = tex->GetImageToTexCoordsRatioX();
    const float height_ratio = tex->GetImageToTexCoordsRatioY();
    state.rect_.left_ = state.rect_.left_ * width_ratio;
    state.rect_.top_ = state.rect_.top_ * height_ratio;
    // Pixel centers are at .5, so add an extra pixel to the size to adjust.
    state.rect_.right_ = (state.rect_.right_ * width_ratio) + width_ratio;
    state.rect_.bottom_ = (state.rect_.bottom_ * height_ratio) + height_ratio;
#undef SET_SIDE
#undef DATA_ERROR
  }
  static int AddState(T* p, lua_State* L) {
    ActorMultiVertex::State s;
    FillStateFromLua(L, s, p->GetTexture(), 1);
    p->AddState(s);
    COMMON_RETURN_SELF;
  }
  static size_t ValidStateIndex(T* p, lua_State* L, int pos) {
    int index = IArg(pos) - 1;
    if (index < 0 || index >= p->GetNumStates()) {
      luaL_error(L, "Invalid state index %d.", index + 1);
    }
    return static_cast<size_t>(index);
  }
  static int RemoveState(T* p, lua_State* L) {
    p->RemoveState(ValidStateIndex(p, L, 1));
    COMMON_RETURN_SELF;
  }
  static int GetState(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetState() + 1);
    return 1;
  }
  static int SetState(T* p, lua_State* L) {
    p->SetState(ValidStateIndex(p, L, 1));
    COMMON_RETURN_SELF;
  }
  static int GetStateData(T* p, lua_State* L) {
    RageTexture* tex = p->GetTexture();
    if (tex == nullptr) {
      luaL_error(L, "The texture must be set before adding states.");
    }
    const float width_pix = tex->GetImageToTexCoordsRatioX();
    const float height_pix = tex->GetImageToTexCoordsRatioY();
    const float width_ratio = 1.0f / tex->GetImageToTexCoordsRatioX();
    const float height_ratio = 1.0f / tex->GetImageToTexCoordsRatioY();
    const ActorMultiVertex::State& state =
        p->GetStateData(ValidStateIndex(p, L, 1));
    lua_createtable(L, 2, 0);
    lua_createtable(L, 4, 0);
    lua_pushnumber(L, static_cast<lua_Number>(state.rect_.left_) * width_ratio);
    lua_rawseti(L, -2, 1);
    lua_pushnumber(L, static_cast<lua_Number>(state.rect_.top_) * height_ratio);
    lua_rawseti(L, -2, 2);
    lua_pushnumber(
        L, (static_cast<lua_Number>(state.rect_.right_) - width_pix) *
               width_ratio);
    lua_rawseti(L, -2, 3);
    lua_pushnumber(
        L, (static_cast<lua_Number>(state.rect_.bottom_) + height_pix) *
               height_ratio);
    lua_rawseti(L, -2, 4);
    lua_rawseti(L, -2, 1);
    lua_pushnumber(L, state.delay_);
    lua_rawseti(L, -2, 2);
    return 1;
  }
  static int SetStateData(T* p, lua_State* L) {
    ActorMultiVertex::State& state = p->GetStateData(ValidStateIndex(p, L, 1));
    FillStateFromLua(L, state, p->GetTexture(), 2);
    COMMON_RETURN_SELF;
  }
  static int SetStateProperties(T* p, lua_State* L) {
    if (!lua_istable(L, 1)) {
      luaL_error(L, "The states must be inside a table.");
    }
    RageTexture* tex = p->GetTexture();
    if (tex == nullptr) {
      luaL_error(L, "The texture must be set before adding states.");
    }
    std::vector<ActorMultiVertex::State> new_states;
    size_t num_states = lua_objlen(L, 1);
    new_states.resize(num_states);
    for (size_t i = 0; i < num_states; ++i) {
      lua_rawgeti(L, 1, i + 1);
      FillStateFromLua(L, new_states[i], tex, -1);
      lua_pop(L, 1);
    }
    p->SetStateProperties(new_states);
    COMMON_RETURN_SELF;
  }
  static int SetAllStateDelays(T* p, lua_State* L) {
    p->SetAllStateDelays(FArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetAnimationLengthSeconds, GetAnimationLengthSeconds());
  static int SetSecondsIntoAnimation(T* p, lua_State* L) {
    p->SetSecondsIntoAnimation(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetNumQuadStates(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumQuadStates());
    return 1;
  }
  static size_t QuadStateIndex(T* p, lua_State* L, int pos) {
    int index = IArg(pos) - 1;
    if (index < 0 || static_cast<size_t>(index) >= p->GetNumQuadStates()) {
      luaL_error(L, "Invalid state index %d.", index + 1);
    }
    return static_cast<size_t>(index);
  }
  static int AddQuadState(T* p, lua_State* L) {
    p->AddQuadState(IArg(1) - 1);
    COMMON_RETURN_SELF;
  }
  static int RemoveQuadState(T* p, lua_State* L) {
    p->RemoveQuadState(QuadStateIndex(p, L, 1));
    COMMON_RETURN_SELF;
  }
  static int GetQuadState(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetQuadState(QuadStateIndex(p, L, 1)) + 1);
    return 1;
  }
  static int SetQuadState(T* p, lua_State* L) {
    p->SetQuadState(QuadStateIndex(p, L, 1), IArg(2) - 1);
    COMMON_RETURN_SELF;
  }
  static int ForceStateUpdate(T* p, lua_State* L) {
    p->UpdateAnimationState(true);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetDecodeMovie, decode_movie_);
  static int SetDecodeMovie(T* p, lua_State* L) {
    p->decode_movie_ = BArg(1);
    COMMON_RETURN_SELF;
  }

  static int SetTexture(T* p, lua_State* L) {
    RageTexture* Texture = Luna<RageTexture>::check(L, 1);
    Texture = TEXTUREMAN->CopyTexture(Texture);
    p->SetTexture(Texture);
    COMMON_RETURN_SELF;
  }
  static int GetTexture(T* p, lua_State* L) {
    RageTexture* texture = p->GetTexture();
    if (texture != nullptr) {
      texture->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  LunaActorMultiVertex() {
    ADD_METHOD(SetVertex);
    ADD_METHOD(SetVertices);

    ADD_METHOD(SetEffectMode);
    ADD_METHOD(SetTextureMode);
    ADD_METHOD(SetLineWidth);

    ADD_METHOD(SetDrawState);
    ADD_METHOD(SetNumVertices);
    ADD_METHOD(GetNumVertices);
    ADD_METHOD(GetDestDrawMode);
    ADD_METHOD(GetDestFirstToDraw);
    ADD_METHOD(GetDestNumToDraw);
    ADD_METHOD(GetCurrDrawMode);
    ADD_METHOD(GetCurrFirstToDraw);
    ADD_METHOD(GetCurrNumToDraw);

    ADD_METHOD(GetSpline);
    ADD_METHOD(SetVertsFromSplines);

    ADD_METHOD(GetUseAnimationState);
    ADD_METHOD(SetUseAnimationState);
    ADD_METHOD(GetNumStates);
    ADD_METHOD(GetNumQuadStates);
    ADD_METHOD(AddState);
    ADD_METHOD(RemoveState);
    ADD_METHOD(GetState);
    ADD_METHOD(SetState);
    ADD_METHOD(GetStateData);
    ADD_METHOD(SetStateData);
    ADD_METHOD(SetStateProperties);
    ADD_METHOD(SetAllStateDelays);
    ADD_METHOD(SetSecondsIntoAnimation);
    ADD_METHOD(AddQuadState);
    ADD_METHOD(RemoveQuadState);
    ADD_METHOD(GetQuadState);
    ADD_METHOD(SetQuadState);
    ADD_METHOD(ForceStateUpdate);
    ADD_METHOD(GetDecodeMovie);
    ADD_METHOD(SetDecodeMovie);

    // Copy from RageTexture
    ADD_METHOD(SetTexture);
    ADD_METHOD(GetTexture);
    // Load from file path
    ADD_METHOD(LoadTexture);
  }
};

LUA_REGISTER_DERIVED_CLASS(ActorMultiVertex, Actor)

/*
 * (c) 2014 Matthew Gardner and Eric Reese
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
