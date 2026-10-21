#include "DancingBot.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "ActorUtil.h"
#include "GameState.h"
#include "LuaBinding.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "Sprite.h"
#include "StepParityGenerator.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "TimingData.h"

REGISTER_ACTOR_CLASS(DancingBot);

namespace {
const float PANEL_SIZE = 24;
const char* FOOT_NAMES[] = {"LeftFoot", "RightFoot"};

StepParity::StagePoint DrawPosition(StepParity::StagePoint p, float centerX) {
  return {(p.x - centerX) * PANEL_SIZE, (1 - p.y) * PANEL_SIZE};
}

Sprite* AddSprite(
    ActorFrame& parent, const std::string& name, const std::string& graphic,
    float width, float height) {
  Sprite* sprite = new Sprite;
  sprite->Load(THEME->GetPathG("DancingBot", graphic));
  sprite->SetName(name);
  sprite->SetWidth(width);
  sprite->SetHeight(height);
  parent.AddChild(sprite);
  return sprite;
}

} /* namespace */

DancingBot::DancingBot() {
  DeleteChildrenWhenDone(true);
  ActorFrame* pad = new ActorFrame;
  pad->SetName("Pad");
  pad->DeleteChildrenWhenDone(true);
  AddChild(pad);
  for (int p = 0; p < 2; ++p) {
    /* TestInput's artwork has panel centers 84 pixels apart in a 260px pad. */
    AddSprite(
        *pad, "Base" + std::to_string(p + 1), "pad", PANEL_SIZE * 260 / 84,
        PANEL_SIZE * 260 / 84);
  }
  for (int c = 0; c < StepParity::MAX_COLUMNS; ++c) {
    Sprite* panel = AddSprite(
        *pad, "Panel" + std::to_string(c + 1), "highlight",
        PANEL_SIZE * 80 / 84, PANEL_SIZE * 80 / 84);
    panel->SetDiffuse(RageColor(0.4f, 0.9f, 1, 0));
  }
  for (int f = 0; f < 2; ++f) {
    ActorFrame* foot = new ActorFrame;
    foot->SetName(FOOT_NAMES[f]);
    foot->DeleteChildrenWhenDone(true);
    AddChild(foot);
    const RageColor color =
        f == 0 ? RageColor(0.3f, 0.7f, 1, 1) : RageColor(1, 0.4f, 0.4f, 1);
    Sprite* footprint = AddSprite(*foot, "Footprint", "feet", 32, 28);
    footprint->SetCustomImageRect(RectF(f * 0.5f, 0, (f + 1) * 0.5f, 1));
    footprint->SetDiffuse(color);
  }
  SetSecond(0);
}

void DancingBot::SetSteps(Steps* steps, PlayerNumber player) {
  if (steps == nullptr) {
    m_bSupported = false;
    for (std::vector<FootPose>& foot : m_Feet) {
      foot.clear();
    }
    for (std::vector<PanelContact>& contacts : m_Contacts) {
      contacts.clear();
    }
    SetSecond(0);
    return;
  }
  if (player == PLAYER_INVALID) {
    player = GAMESTATE->GetMasterPlayerNumber();
  }
  NoteData notes;
  steps->GetNoteData(notes);
  Load(notes, steps->m_StepsType, *steps->GetTimingData(), player);
}

void DancingBot::Load(
    const NoteData& notes, StepsType type, TimingData& timing,
    PlayerNumber player) {
  m_bSupported = false;
  for (std::vector<FootPose>& foot : m_Feet) {
    foot.clear();
  }
  for (std::vector<PanelContact>& contacts : m_Contacts) {
    contacts.clear();
  }
  const std::optional<StepParity::StageLayout> layout =
      StepParity::GetStageLayout(type);
  if (!layout || player < PLAYER_1 || player >= NUM_PLAYERS) {
    SetSecond(m_fSecond);
    return;
  }

  std::vector<NoteData> split;
  const NoteData* input = &notes;
  if (NoteDataUtil::SplitCompositeOrStackedNoteData(notes, split, type)) {
    if (static_cast<size_t>(player) >= split.size()) {
      SetSecond(m_fSecond);
      return;
    }
    input = &split[player];
  }
  if (input->GetNumTracks() != layout->columnCount) {
    SetSecond(m_fSecond);
    return;
  }

  const float centerX = layout->columnCount == 8 ? 2.5f : 1.0f;
  ActorFrame* pad = static_cast<ActorFrame*>(GetChild("Pad"));
  pad->GetChild("Base1")->SetX((1 - centerX) * PANEL_SIZE);
  pad->GetChild("Base2")->SetX((4 - centerX) * PANEL_SIZE);
  pad->GetChild("Base2")->SetVisible(layout->columnCount == 8);
  for (int c = 0; c < StepParity::MAX_COLUMNS; ++c) {
    Actor* panel = pad->GetChild("Panel" + std::to_string(c + 1));
    panel->SetVisible(c < layout->columnCount);
    if (c < layout->columnCount) {
      const StepParity::StagePoint p =
          DrawPosition(layout->columns[c], centerX);
      panel->SetXY(p.x, p.y);
    }
  }

  for (int f = 0; f < 2; ++f) {
    const StepParity::StagePoint p =
        DrawPosition(layout->columns[f == 0 ? 0 : 3], centerX);
    m_Rest[f].second = -std::numeric_limits<float>::infinity();
    m_Rest[f].points = {{{p.x, p.y + 6}, {p.x, p.y - 6}}};
    m_Rest[f].release.fill(m_Rest[f].second);
  }

  StepParity::StepParityGenerator generator(&*layout, &timing);
  if (generator.analyzeNoteData(*input)) {
    /* Binary search requires chronological events. Do not invent a dance for
     * timing that the parity solver cannot represent as a forward timeline. */
    if (!std::is_sorted(
            generator.rows.begin(), generator.rows.end(),
            [](const StepParity::Row& a, const StepParity::Row& b) {
              return a.second < b.second;
            })) {
      SetSecond(m_fSecond);
      return;
    }
    for (size_t i = 0; i < generator.rows.size(); ++i) {
      const StepParity::Row& row = generator.rows[i];
      /* Rows annotate notes only. The chosen graph state also includes feet
       * resting on panels and the heel/toe assignments of ongoing holds. */
      const StepParity::State& resultState =
          *generator.nodes[generator.nodes_for_rows[i]]->state;
      for (int c = 0; c < layout->columnCount; ++c) {
        if ((row.note_mask & (1 << c)) == 0) {
          continue;
        }
        const StepParity::IntermediateNoteData& note = row.notes[c];
        const float end =
            note.type == TapNoteType_HoldHead
                ? timing.GetElapsedTimeFromBeat(note.beat + note.hold_length)
                : row.second;
        m_Contacts[c].push_back({row.second, end});
      }
      for (int f = 0; f < 2; ++f) {
        const int heel =
            f == 0 ? StepParity::Foot_LeftHeel : StepParity::Foot_RightHeel;
        const int toe = heel + 1;
        const int columns[] = {
            resultState.whereTheFeetAre[heel],
            resultState.whereTheFeetAre[toe]};
        if (columns[0] < 0 && columns[1] < 0) {
          continue;
        }
        const FootPose& previous =
            m_Feet[f].empty() ? m_Rest[f] : m_Feet[f].back();
        FootPose pose = previous;
        pose.second = row.second;
        bool hit = false;
        for (int part = 0; part < 2; ++part) {
          const int c = columns[part] >= 0 ? columns[part] : columns[1 - part];
          pose.points[part] = DrawPosition(layout->columns[c], centerX);
          if (columns[0] < 0 || columns[1] < 0) {
            pose.points[part].y += part == 0 ? 6 : -6;
          }
          pose.release[part] = row.second;
          const StepParity::IntermediateNoteData& hold = row.holds[c];
          const StepParity::IntermediateNoteData& note = row.notes[c];
          if (hold.type == TapNoteType_HoldHead) {
            pose.release[part] =
                timing.GetElapsedTimeFromBeat(hold.beat + hold.hold_length);
          }
          if (note.type == TapNoteType_HoldHead) {
            pose.release[part] = std::max(
                pose.release[part],
                timing.GetElapsedTimeFromBeat(note.beat + note.hold_length));
          }
          hit |= (row.note_mask & (1 << c)) != 0;
        }
        if (hit) {
          pose.lastHit = row.second;
        }
        if (hit || pose.points[0].x != previous.points[0].x ||
            pose.points[0].y != previous.points[0].y ||
            pose.points[1].x != previous.points[1].x ||
            pose.points[1].y != previous.points[1].y) {
          m_Feet[f].push_back(pose);
        }
      }
    }
    m_bSupported = true;
  } else {
    /* Empty charts still have a pad and a neutral stance. Failed analysis of
     * a nonempty chart must not present a fabricated foot assignment. */
    m_bSupported = input->IsEmpty();
  }
  SetSecond(m_fSecond);
}

void DancingBot::SetMotionSeconds(float seconds) {
  if (std::isfinite(seconds)) {
    m_fMotionSeconds = std::max(0.0f, std::min(seconds, 1.0f));
    SetSecond(m_fSecond);
  }
}

void DancingBot::SetSecond(float second) {
  if (!std::isfinite(second)) {
    return;
  }
  m_fSecond = second;
  GetChild("Pad")->SetVisible(m_bSupported);
  ActorFrame* pad = static_cast<ActorFrame*>(GetChild("Pad"));
  for (int c = 0; c < StepParity::MAX_COLUMNS; ++c) {
    const std::vector<PanelContact>& contacts = m_Contacts[c];
    const std::vector<PanelContact>::const_iterator next = std::upper_bound(
        contacts.begin(), contacts.end(), second,
        [](float time, const PanelContact& contact) {
          return time < contact.second;
        });
    float alpha = 0;
    if (next != contacts.begin()) {
      const PanelContact& contact = *(next - 1);
      if (second < contact.endSecond) {
        alpha = 0.65f + 0.1f * std::sin((second - contact.second) * 12);
      } else {
        alpha =
            0.85f * std::max(0.0f, 1 - (second - contact.endSecond) / 0.18f);
      }
    }
    Actor* panel = pad->GetChild("Panel" + std::to_string(c + 1));
    panel->SetDiffuse(RageColor(0.4f, 0.9f, 1, alpha));
  }
  Actor* steppingFoot = nullptr;
  float latestHit = -std::numeric_limits<float>::infinity();
  for (int f = 0; f < 2; ++f) {
    ActorFrame* foot = static_cast<ActorFrame*>(GetChild(FOOT_NAMES[f]));
    foot->SetVisible(m_bSupported);
    if (!m_bSupported) {
      continue;
    }
    const std::vector<FootPose>& events = m_Feet[f];
    const std::vector<FootPose>::const_iterator next = std::upper_bound(
        events.begin(), events.end(), second,
        [](float time, const FootPose& pose) { return time < pose.second; });
    const FootPose& previous = next == events.begin() ? m_Rest[f] : *(next - 1);
    if (previous.lastHit >= latestHit) {
      latestHit = previous.lastHit;
      steppingFoot = foot;
    }
    std::array<StepParity::StagePoint, 2> points = previous.points;
    if (next != events.end() && m_fMotionSeconds > 0) {
      for (int part = 0; part < 2; ++part) {
        const float start =
            std::max(previous.release[part], next->second - m_fMotionSeconds);
        if (start < next->second) {
          float t = std::max(
              0.0f, std::min(1.0f, (second - start) / (next->second - start)));
          t = t * t * (3 - 2 * t);
          points[part].x += (next->points[part].x - points[part].x) * t;
          points[part].y += (next->points[part].y - points[part].y) * t;
        }
      }
    }
    const float dx = points[1].x - points[0].x;
    const float dy = points[1].y - points[0].y;
    const float length = std::sqrt(dx * dx + dy * dy);
    foot->SetXY(
        (points[0].x + points[1].x) / 2, (points[0].y + points[1].y) / 2);
    foot->SetRotationZ(std::atan2(dx, -dy) * 180 / 3.14159265f);
    /* Animate every step, including jacks. Use song time so scrolling,
     * low frame rates and looping cannot leave a stuck animation. */
    const float stepAnimation =
        std::max(0.0f, 1 - (second - previous.lastHit) / 0.14f);
    Actor* footprint = foot->GetChild("Footprint");
    /* Keep the bracket's midpoint and angle, but cap visual elongation.
     * Panel highlights identify the exact contacts; the sprite remains a foot.
     */
    const float height =
        std::min(36.0f, 28 + std::max(0.0f, length - 12) * 0.35f);
    footprint->SetHeight(height * (1 - 0.2f * stepAnimation));
    /* Keep the step flash in the footprint's own (possibly themed) color. */
    RageColor stepColor = footprint->GetDiffuse();
    stepColor.a *= 0.5f * stepAnimation;
    footprint->SetGlow(stepColor);
  }
  /* A released foot can still overlap the new contact during a footswitch.
   * Draw the stepping foot last so the red foot cannot obscure a blue hit. */
  if (steppingFoot != nullptr) {
    MoveToTail(steppingFoot);
  }
}

class LunaDancingBot : public Luna<DancingBot> {
 public:
  static int SetSteps(T* p, lua_State* L) {
    Steps* steps = lua_isnoneornil(L, 1) ? nullptr : Luna<Steps>::check(L, 1);
    if (lua_isnoneornil(L, 2)) {
      p->SetSteps(steps);
    } else {
      p->SetSteps(steps, Enum::Check<PlayerNumber>(L, 2));
    }
    COMMON_RETURN_SELF;
  }
  static int SetSecond(T* p, lua_State* L) {
    p->SetSecond(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetMotionSeconds(T* p, lua_State* L) {
    p->SetMotionSeconds(FArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(IsSupported, IsSupported());
  DEFINE_METHOD(GetMotionSeconds, GetMotionSeconds());
  LunaDancingBot() {
    ADD_METHOD(SetSteps);
    ADD_METHOD(SetSecond);
    ADD_METHOD(SetMotionSeconds);
    ADD_METHOD(IsSupported);
    ADD_METHOD(GetMotionSeconds);
  }
};

LUA_REGISTER_DERIVED_CLASS(DancingBot, ActorFrame)
