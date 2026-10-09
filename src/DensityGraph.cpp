#include "DensityGraph.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "ActorUtil.h"
#include "LuaBinding.h"
#include "MeasureInfo.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "TimingData.h"
#include "global.h"

namespace {
const RageColor kDefaultBottomColor(0.0f, 1.0f, 0.0f, 1.0f);
const RageColor kDefaultTopColor(1.0f, 0.0f, 0.0f, 1.0f);

RageColor Desaturate(RageColor color, float desaturation) {
  const float luma = 0.3f * color.r + 0.59f * color.g + 0.11f * color.b;
  color.r += desaturation * (luma - color.r);
  color.g += desaturation * (luma - color.g);
  color.b += desaturation * (luma - color.b);
  return color;
}
}  // namespace

REGISTER_ACTOR_CLASS(DensityGraph);

DensityGraph::DensityGraph()
    : m_pSteps(nullptr),
      m_Player(PLAYER_1),
      m_ViewWidth(0.0f),
      m_ViewHeight(0.0f),
      m_GraphWidth(std::nullopt),
      m_ScrollOffset(0.0f),
      m_BottomColor(kDefaultBottomColor),
      m_TopColor(kDefaultTopColor),
      m_Desaturation(0.0f) {
  SetDrawState(DrawMode_QuadStrip, 0, -1);
}

DensityGraph::~DensityGraph() {}

void DensityGraph::SetSteps(Steps* pSteps, PlayerNumber pn) {
  m_pSteps = pSteps;
  m_Player = pn;
}

void DensityGraph::SetSize(float width, float height) {
  m_ViewWidth = width;
  m_ViewHeight = height;
}

void DensityGraph::SetGraphWidth(float width) { m_GraphWidth = width; }

void DensityGraph::SetColors(const RageColor& bottom, const RageColor& top) {
  m_BottomColor = bottom;
  m_TopColor = top;
}

void DensityGraph::SetDesaturation(float desaturation) {
  m_Desaturation = desaturation;
}

void DensityGraph::Build() {
  BuildAllVertices();
  BuildVisibleVertices();
}

void DensityGraph::SetScrollOffset(float offset) {
  if (m_ScrollOffset == offset) {
    return;
  }
  m_ScrollOffset = offset;
  BuildVisibleVertices();
}

void DensityGraph::BuildAllVertices() {
  m_Points.clear();

  if (m_pSteps == nullptr || m_ViewWidth <= 0.0f || m_ViewHeight <= 0.0f) {
    return;
  }
  const float width = m_GraphWidth.value_or(m_ViewWidth);
  if (width <= 0.0f) {
    return;
  }

  const std::vector<float>& nps = m_pSteps->GetNpsPerMeasure(m_Player);
  const float peak = m_pSteps->GetPeakNps(m_Player);
  Song* pSong = m_pSteps->m_pSong;
  if (nps.empty() || peak <= 0.0f || pSong == nullptr) {
    return;
  }
  TimingData* pTiming = m_pSteps->GetTimingData();

  const float first = std::min(pTiming->GetElapsedTimeFromBeat(0.0f), 0.0f);
  const float last = pSong->GetLastSecond();
  if (last - first <= 0.0f) {
    return;
  }

  const RageColor bottom = Desaturate(m_BottomColor, m_Desaturation);
  const RageColor top = Desaturate(m_TopColor, m_Desaturation);

  const float height = m_ViewHeight;

  bool started = false;
  float prev_x = 0.0f;
  for (size_t i = 0; i < nps.size(); ++i) {
    if (nps[i] > 0.0f) {
      started = true;
    }
    if (!started) {
      continue;
    }

    const float seconds = pTiming->GetElapsedTimeFromBeat(
        static_cast<float>(i) * static_cast<float>(BEATS_PER_MEASURE));
    // Keep x non-decreasing so the visible-window search can assume order.
    const float x = std::max(SCALE(seconds, first, last, 0.0f, width), prev_x);
    prev_x = x;
    // Quantize y to the nearest pixel so the run-length compression below
    // merges near-equal measures.
    const float y = std::round(-SCALE(nps[i], 0.0f, peak, 0.0f, height));

    // If the height of this measure matches the previous two, extend the
    // previous point instead of adding a new one. This noticeably shrinks the
    // point count for songs with long streams.
    const size_t count = m_Points.size();
    if (count >= 2 && m_Points[count - 1].y == y &&
        m_Points[count - 2].y == y) {
      m_Points[count - 1].x = x;
    } else {
      RageColor color;
      lerp_rage_color(color, bottom, top, std::abs(y / height));
      m_Points.push_back({x, y, color});
    }
  }

  // Add a 0 NPS point at the end of the last measure so the final measure
  // slopes down like the others.
  if (nps.back() != 0.0f) {
    const float endSeconds = pTiming->GetElapsedTimeFromBeat(
        static_cast<float>(nps.size()) * static_cast<float>(BEATS_PER_MEASURE));
    const float endX = std::max(
        prev_x, std::min(width, SCALE(endSeconds, first, last, 0.0f, width)));
    m_Points.push_back({endX, 0.0f, bottom});
  }
}

void DensityGraph::BuildVisibleVertices() {
  std::vector<Point> visible;

  const float left = m_ScrollOffset;
  const float right = m_ScrollOffset + m_ViewWidth;

  const auto begin = m_Points.begin();
  const auto end = m_Points.end();

  // First point at or past the left edge of the window.
  const auto first_visible_it = std::lower_bound(
      begin, end, left,
      [](const Point& p, float value) { return p.x < value; });
  // First point past the right edge of the window.
  const auto past_last_visible_it = std::upper_bound(
      begin, end, right,
      [](float value, const Point& p) { return value < p.x; });

  const size_t first_visible = static_cast<size_t>(first_visible_it - begin);
  const size_t past_last_visible =
      static_cast<size_t>(past_last_visible_it - begin);

  if (first_visible < m_Points.size() && past_last_visible > 0) {
    auto interpolate = [](const Point& p1, const Point& p2, float x) {
      const float dx = p2.x - p1.x;
      const float ratio = (dx != 0.0f) ? (x - p1.x) / dx : 0.0f;
      Point out;
      out.x = x;
      out.y = p1.y * (1.0f - ratio) + p2.y * ratio;
      lerp_rage_color(out.color, p1.color, p2.color, ratio);
      return out;
    };

    visible.reserve(past_last_visible - first_visible + 2);

    // Trim the partial segments at the window edges so the graph does not
    // extend past the visible area.
    if (first_visible > 0) {
      visible.push_back(interpolate(
          m_Points[first_visible - 1], m_Points[first_visible], left));
    }

    for (size_t i = first_visible; i < past_last_visible; ++i) {
      visible.push_back(m_Points[i]);
    }

    if (past_last_visible < m_Points.size()) {
      visible.push_back(interpolate(
          m_Points[past_last_visible - 1], m_Points[past_last_visible], right));
    }
  }

  Emit(visible);
}

void DensityGraph::Emit(const std::vector<Point>& points) {
  SetNumVertices(points.size() * 2);
  const RageColor bottom = Desaturate(m_BottomColor, m_Desaturation);
  for (size_t i = 0; i < points.size(); ++i) {
    const int bottom_index = static_cast<int>(i * 2);
    const int top_index = bottom_index + 1;
    SetVertexPos(bottom_index, points[i].x, 0.0f, 0.0f);
    SetVertexColor(bottom_index, bottom);
    SetVertexPos(top_index, points[i].x, points[i].y, 0.0f);
    SetVertexColor(top_index, points[i].color);
  }
}

/** @brief Allow Lua to have access to the DensityGraph. */
class LunaDensityGraph : public Luna<DensityGraph> {
 public:
  static int SetSteps(T* p, lua_State* L) {
    Steps* pSteps = nullptr;
    if (!lua_isnoneornil(L, 1)) {
      pSteps = Luna<Steps>::check(L, 1);
    }
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnoneornil(L, 2)) {
      pn = Enum::Check<PlayerNumber>(L, 2);
    }
    p->SetSteps(pSteps, pn);
    COMMON_RETURN_SELF;
  }

  static int SetSize(T* p, lua_State* L) {
    p->SetSize(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }

  static int SetGraphWidth(T* p, lua_State* L) {
    p->SetGraphWidth(FArg(1));
    COMMON_RETURN_SELF;
  }

  static int SetColors(T* p, lua_State* L) {
    RageColor bottom;
    RageColor top;
    bottom.FromStackCompat(L, 1);
    top.FromStackCompat(L, 2);
    p->SetColors(bottom, top);
    COMMON_RETURN_SELF;
  }

  static int SetDesaturation(T* p, lua_State* L) {
    p->SetDesaturation(FArg(1));
    COMMON_RETURN_SELF;
  }

  static int Build(T* p, lua_State* L) {
    p->Build();
    COMMON_RETURN_SELF;
  }

  static int SetScrollOffset(T* p, lua_State* L) {
    p->SetScrollOffset(FArg(1));
    COMMON_RETURN_SELF;
  }

  LunaDensityGraph() {
    ADD_METHOD(SetSteps);
    ADD_METHOD(SetSize);
    ADD_METHOD(SetGraphWidth);
    ADD_METHOD(SetColors);
    ADD_METHOD(SetDesaturation);
    ADD_METHOD(Build);
    ADD_METHOD(SetScrollOffset);
  }
};

LUA_REGISTER_DERIVED_CLASS(DensityGraph, ActorMultiVertex)
