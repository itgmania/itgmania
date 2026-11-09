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
  const float luma = 0.3f * color.r_ + 0.59f * color.g_ + 0.11f * color.b_;
  color.r_ += desaturation * (luma - color.r_);
  color.g_ += desaturation * (luma - color.g_);
  color.b_ += desaturation * (luma - color.b_);
  return color;
}
}  // namespace

REGISTER_ACTOR_CLASS(DensityGraph);

DensityGraph::DensityGraph()
    : steps_(nullptr),
      player_(PLAYER_1),
      view_width_(0.0f),
      view_height_(0.0f),
      graph_width_(std::nullopt),
      scroll_offset_(0.0f),
      bottom_color_(kDefaultBottomColor),
      top_color_(kDefaultTopColor),
      desaturation_(0.0f) {
  SetDrawState(DrawMode_QuadStrip, 0, -1);
}

DensityGraph::~DensityGraph() {}

void DensityGraph::SetSteps(Steps* steps, PlayerNumber pn) {
  steps_ = steps;
  player_ = pn;
}

void DensityGraph::SetSize(float width, float height) {
  view_width_ = width;
  view_height_ = height;
}

void DensityGraph::SetGraphWidth(float width) { graph_width_ = width; }

void DensityGraph::SetColors(const RageColor& bottom, const RageColor& top) {
  bottom_color_ = bottom;
  top_color_ = top;
}

void DensityGraph::SetDesaturation(float desaturation) {
  desaturation_ = desaturation;
}

void DensityGraph::Build() {
  BuildAllVertices();
  BuildVisibleVertices();
}

void DensityGraph::SetScrollOffset(float offset) {
  if (scroll_offset_ == offset) {
    return;
  }
  scroll_offset_ = offset;
  BuildVisibleVertices();
}

void DensityGraph::BuildAllVertices() {
  points_.clear();

  if (steps_ == nullptr || view_width_ <= 0.0f || view_height_ <= 0.0f) {
    return;
  }
  const float width = graph_width_.value_or(view_width_);
  if (width <= 0.0f) {
    return;
  }

  const std::vector<float>& nps = steps_->GetNpsPerMeasure(player_);
  const float peak = steps_->GetPeakNps(player_);
  Song* song = steps_->song_;
  if (nps.empty() || peak <= 0.0f || song == nullptr) {
    return;
  }
  TimingData* timing = steps_->GetTimingData();

  const float first = std::min(timing->GetElapsedTimeFromBeat(0.0f), 0.0f);
  const float last = song->GetLastSecond();
  if (last - first <= 0.0f) {
    return;
  }

  const RageColor bottom = Desaturate(bottom_color_, desaturation_);
  const RageColor top = Desaturate(top_color_, desaturation_);

  const float height = view_height_;

  bool started = false;
  float prev_x = 0.0f;
  for (size_t i = 0; i < nps.size(); ++i) {
    if (nps[i] > 0.0f) {
      started = true;
    }
    if (!started) {
      continue;
    }

    const float seconds = timing->GetElapsedTimeFromBeat(
        static_cast<float>(i) * static_cast<float>(kBeatsPerMeasure));
    // Keep x non-decreasing so the visible-window search can assume order.
    const float x = std::max(SCALE(seconds, first, last, 0.0f, width), prev_x);
    prev_x = x;
    // Quantize y to the nearest pixel so the run-length compression below
    // merges near-equal measures.
    const float y = std::round(-SCALE(nps[i], 0.0f, peak, 0.0f, height));

    // If the height of this measure matches the previous two, extend the
    // previous point instead of adding a new one. This noticeably shrinks the
    // point count for songs with long streams.
    const size_t count = points_.size();
    if (count >= 2 && points_[count - 1].y_ == y &&
        points_[count - 2].y_ == y) {
      points_[count - 1].x_ = x;
    } else {
      RageColor color;
      lerp_rage_color(color, bottom, top, std::abs(y / height));
      points_.push_back({x, y, color});
    }
  }

  // Add a 0 NPS point at the end of the last measure so the final measure
  // slopes down like the others.
  if (nps.back() != 0.0f) {
    const float end_seconds = timing->GetElapsedTimeFromBeat(
        static_cast<float>(nps.size()) * static_cast<float>(kBeatsPerMeasure));
    const float end_x = std::max(
        prev_x, std::min(width, SCALE(end_seconds, first, last, 0.0f, width)));
    points_.push_back({end_x, 0.0f, bottom});
  }
}

void DensityGraph::BuildVisibleVertices() {
  std::vector<Point> visible;

  const float left = scroll_offset_;
  const float right = scroll_offset_ + view_width_;

  const auto begin = points_.begin();
  const auto end = points_.end();

  // First point at or past the left edge of the window.
  const auto first_visible_it = std::lower_bound(
      begin, end, left,
      [](const Point& p, float value) { return p.x_ < value; });
  // First point past the right edge of the window.
  const auto past_last_visible_it = std::upper_bound(
      begin, end, right,
      [](float value, const Point& p) { return value < p.x_; });

  const size_t first_visible = static_cast<size_t>(first_visible_it - begin);
  const size_t past_last_visible =
      static_cast<size_t>(past_last_visible_it - begin);

  if (first_visible < points_.size() && past_last_visible > 0) {
    auto interpolate = [](const Point& p1, const Point& p2, float x) {
      const float dx = p2.x_ - p1.x_;
      const float ratio = (dx != 0.0f) ? (x - p1.x_) / dx : 0.0f;
      Point out;
      out.x_ = x;
      out.y_ = p1.y_ * (1.0f - ratio) + p2.y_ * ratio;
      lerp_rage_color(out.color_, p1.color_, p2.color_, ratio);
      return out;
    };

    visible.reserve(past_last_visible - first_visible + 2);

    // Trim the partial segments at the window edges so the graph does not
    // extend past the visible area.
    if (first_visible > 0) {
      visible.push_back(interpolate(
          points_[first_visible - 1], points_[first_visible], left));
    }

    for (size_t i = first_visible; i < past_last_visible; ++i) {
      visible.push_back(points_[i]);
    }

    if (past_last_visible < points_.size()) {
      visible.push_back(interpolate(
          points_[past_last_visible - 1], points_[past_last_visible], right));
    }
  }

  Emit(visible);
}

void DensityGraph::Emit(const std::vector<Point>& points) {
  SetNumVertices(points.size() * 2);
  const RageColor bottom = Desaturate(bottom_color_, desaturation_);
  for (size_t i = 0; i < points.size(); ++i) {
    const int bottom_index = static_cast<int>(i * 2);
    const int top_index = bottom_index + 1;
    SetVertexPos(bottom_index, points[i].x_, 0.0f, 0.0f);
    SetVertexColor(bottom_index, bottom);
    SetVertexPos(top_index, points[i].x_, points[i].y_, 0.0f);
    SetVertexColor(top_index, points[i].color_);
  }
}

/** @brief Allow Lua to have access to the DensityGraph. */
class LunaDensityGraph : public Luna<DensityGraph> {
 public:
  static int SetSteps(T* p, lua_State* L) {
    Steps* steps = nullptr;
    if (!lua_isnoneornil(L, 1)) {
      steps = Luna<Steps>::check(L, 1);
    }
    PlayerNumber pn = PLAYER_1;
    if (!lua_isnoneornil(L, 2)) {
      pn = Enum::Check<PlayerNumber>(L, 2);
    }
    p->SetSteps(steps, pn);
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
