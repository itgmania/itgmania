#include "global.h"

#include "BGAnimation.h"

#include "ActorUtil.h"
#include "BGAnimationLayer.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageUtil.h"

#include <vector>


REGISTER_ACTOR_CLASS(BGAnimation);

BGAnimation::BGAnimation() {}

BGAnimation::~BGAnimation() { DeleteAllChildren(); }

static bool CompareLayerNames(const RString& s1, const RString& s2) {
  int i1, i2;
  int ret;

  ret = sscanf(s1, "Layer%d", &i1);
  ASSERT(ret == 1);
  ret = sscanf(s2, "Layer%d", &i2);
  ASSERT(ret == 1);
  return i1 < i2;
}

void BGAnimation::AddLayersFromAniDir(
    const RString& ani_dir, const XNode* node) {
  std::vector<RString> layer_names;
  FOREACH_CONST_Child(node, layer) {
    if (strncmp(layer->GetName(), "Layer", 5) == 0) {
      layer_names.push_back(layer->GetName());
    }
  }

  std::sort(layer_names.begin(), layer_names.end(), CompareLayerNames);

  for (const RString& layer : layer_names) {
    const XNode* key = node->GetChild(layer);
    ASSERT(key != nullptr);

    RString import_dir;
    if (key->GetAttrValue("Import", import_dir)) {
      bool bCond;
      if (key->GetAttrValue("Condition", bCond) && !bCond) {
        continue;
      }

      // Import a whole BGAnimation
      import_dir = ani_dir + import_dir;
      CollapsePath(import_dir);

      if (import_dir.Right(1) != "/") {
        import_dir += "/";
      }

      ASSERT_M(IsADirectory(import_dir), import_dir + " isn't a directory");

      RString ini_path = import_dir + "BGAnimation.ini";

      IniFile ini2;
      ini2.ReadFile(ini_path);

      AddLayersFromAniDir(import_dir, &ini2);
    } else {
      // Import as a single layer
      BGAnimationLayer* bg_layer = new BGAnimationLayer;
      bg_layer->LoadFromNode(key);
      this->AddChild(bg_layer);
    }
  }
}

void BGAnimation::LoadFromAniDir(const RString& _ani_dir) {
  DeleteAllChildren();

  if (_ani_dir.empty()) {
    return;
  }

  RString ani_dir = _ani_dir;
  if (ani_dir.Right(1) != "/") {
    ani_dir += "/";
  }

  ASSERT_M(IsADirectory(ani_dir), ani_dir + " isn't a directory");

  RString ini_path = ani_dir + "BGAnimation.ini";

  if (DoesFileExist(ini_path)) {
    if (PREFSMAN->m_bQuirksMode) {
      // This is a 3.9-style BGAnimation (using .ini)
      IniFile ini;
      ini.ReadFile(ini_path);

      // TODO: Check for circular load
      AddLayersFromAniDir(ani_dir, &ini);

      XNode* bg_animation = ini.GetChild("BGAnimation");
      XNode dummy("BGAnimation");
      if (bg_animation == nullptr) {
        bg_animation = &dummy;
      }

      LoadFromNode(bg_animation);
    } else  // We don't officially support .ini files anymore.
    {
      XNode dummy("BGAnimation");
      XNode* bg_animation = &dummy;
      LoadFromNode(bg_animation);
    }
  } else {
    // This is an 3.0 and before-style BGAnimation (not using .ini)

    // Loading a directory of layers
    std::vector<RString> image_paths;
    ASSERT(ani_dir != "");

    GetDirListing(ani_dir + "*.png", image_paths, false, true);
    GetDirListing(ani_dir + "*.jpg", image_paths, false, true);
    GetDirListing(ani_dir + "*.jpeg", image_paths, false, true);
    GetDirListing(ani_dir + "*.gif", image_paths, false, true);
    GetDirListing(ani_dir + "*.ogv", image_paths, false, true);
    GetDirListing(ani_dir + "*.avi", image_paths, false, true);
    GetDirListing(ani_dir + "*.mpg", image_paths, false, true);
    GetDirListing(ani_dir + "*.mpeg", image_paths, false, true);

    SortRStringArray(image_paths);

    for (unsigned i = 0; i < image_paths.size(); ++i) {
      const RString path = image_paths[i];
      if (Basename(path).Left(1) == "_") {
        // Don't directly load files starting with an underscore
        continue;
      }
      BGAnimationLayer* layer = new BGAnimationLayer;
      layer->LoadFromAniLayerFile(image_paths[i]);
      AddChild(layer);
    }
  }
}

void BGAnimation::LoadFromNode(const XNode* node) {
  RString dir;
  if (node->GetAttrValue("AniDir", dir)) {
    LoadFromAniDir(dir);
  }

  ActorFrame::LoadFromNode(node);

  // Backwards-compatibility: if a "LengthSeconds" value is present, create a
  // dummy actor that sleeps for the given length of time. This will extend
  // GetTweenTimeLeft.
  float length_seconds = 0;
  if (node->GetAttrValue("LengthSeconds", length_seconds)) {
    Actor* actor = new Actor;
    actor->SetName("BGAnimation dummy");
    actor->SetVisible(false);
    apActorCommands actor_commands =
        ActorUtil::ParseActorCommands(ssprintf("sleep,%f", length_seconds));
    actor->AddCommand("On", actor_commands);
    AddChild(actor);
  }
}

/*
 * (c) 2001-2004 Ben Nordstrom, Chris Danford
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
