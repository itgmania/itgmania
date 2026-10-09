#ifndef DENSITY_GRAPH_H
#define DENSITY_GRAPH_H

#include <optional>
#include <vector>

#include "ActorMultiVertex.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "global.h"

class Steps;

/**
 * @brief A rendering of a chart's NPS-per-measure density graph.
 */
class DensityGraph : public ActorMultiVertex {
 public:
  DensityGraph();
  virtual ~DensityGraph();
  virtual DensityGraph* Copy() const override;
  // Sets the chart to graph and which player's data to use.
  void SetSteps(Steps* pSteps, PlayerNumber pn);
  // The size of the visible graph area, in pixels.
  void SetSize(float width, float height);
  // The full width of the graph. Defaults to the width passed to SetSize if
  // unset. Set this wider than the visible size to enable scrolling.
  void SetGraphWidth(float width);
  // Gradient endpoints. The gradient is interpolated by the height of each
  // measure, from the bottom color to the top color.
  void SetColors(const RageColor& bottom, const RageColor& top);
  // 0 = full color, 1 = fully desaturated.
  void SetDesaturation(float desaturation);
  // Builds the graph from the current configuration.
  void Build();
  // The left edge of the visible window, in graph pixels.
  void SetScrollOffset(float offset);

  // Lua
  virtual void PushSelf(lua_State* L) override;

 private:
  struct Point {
    float x;
    float y;
    RageColor color;
  };

  void BuildAllVertices();
  void BuildVisibleVertices();
  void Emit(const std::vector<Point>& points);

  Steps* m_pSteps;
  PlayerNumber m_Player;
  float m_ViewWidth;
  float m_ViewHeight;
  std::optional<float> m_GraphWidth;
  float m_ScrollOffset;
  RageColor m_BottomColor;
  RageColor m_TopColor;
  float m_Desaturation;
  std::vector<Point> m_Points;
};

#endif
