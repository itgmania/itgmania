#ifndef GROOVE_RADAR_H
#define GROOVE_RADAR_H

#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "RadarValues.h"
#include "Sprite.h"
#include "Steps.h"
#include "XmlFile.h"

// The song's GrooveRadar displayed in SelectMusic.
class GrooveRadar : public ActorFrame {
 public:
  GrooveRadar();
  virtual GrooveRadar* Copy() const;
  virtual void LoadFromNode(const XNode* node);

  // Give the Player an empty GrooveRadar.
  void SetEmpty(PlayerNumber pn);
  void SetFromRadarValues(PlayerNumber pn, const RadarValues& radar_values);
  // Give the Player a GrooveRadar based on some Steps.
  void SetFromSteps(PlayerNumber pn, Steps* steps);
  void SetFromValues(PlayerNumber pn, std::vector<float> values);

  // Lua
  void PushSelf(lua_State* L);

 protected:
  // The companion ValueMap to the GrooveRadar.
  // This must be a separate Actor so that it can be tweened separately from the
  // labels.
  class GrooveRadarValueMap : public ActorFrame {
   public:
    GrooveRadarValueMap();

    virtual void Update(float delta);
    virtual void DrawPrimitives();

    void SetEmpty();
    void SetFromSteps(const RadarValues& radar_values);
    void SetFromValues(std::vector<float> values);

    void SetRadius(float f) {
      m_size.x = f;
      m_size.y = f;
    }

    bool values_visible_;
    float percent_toward_new_;
    float values_new_[NUM_RadarCategory];
    float values_old_[NUM_RadarCategory];

    PlayerNumber player_number_;
  };

  AutoActor radar_base_;
  GrooveRadarValueMap groove_radar_value_map_[NUM_PLAYERS];
  // TODO(aj): Convert Sprite to AutoActor.
  Sprite radar_labels_[NUM_RadarCategory];
  ActorFrame frame_;
};

#endif  // GROOVE_RADAR_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
