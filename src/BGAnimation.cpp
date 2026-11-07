#include "BGAnimation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "BGAnimationLayer.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "StdString.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(BGAnimation);

BGAnimation::BGAnimation() {}

BGAnimation::~BGAnimation() { DeleteAllChildren(); }

static bool CompareLayerNames(const std::string& s1, const std::string& s2) {
  int i1, i2;
  int ret;

  ret = sscanf(s1.c_str(), "Layer%d", &i1);
  ASSERT(ret == 1);
  ret = sscanf(s2.c_str(), "Layer%d", &i2);
  ASSERT(ret == 1);
  return i1 < i2;
}

void BGAnimation::AddLayersFromAniDir(
    const std::string& s_ani_dir, const XNode* node) {
  const std::string& sAniDir = s_ani_dir;

  {
    std::vector<std::string> layer_names;
    FOREACH_CONST_Child(node, p_layer) {
      if (strncmp(p_layer->GetName().c_str(), "Layer", 5) == 0) {
        layer_names.push_back(p_layer->GetName());
      }
    }

    sort(layer_names.begin(), layer_names.end(), CompareLayerNames);

    for (const std::string& s_layer : layer_names) {
      const XNode* key = node->GetChild(s_layer);
      ASSERT(key != nullptr);

      std::string import_dir;
      if (key->GetAttrValue("Import", import_dir)) {
        bool cond;
        if (key->GetAttrValue("Condition", cond) && !cond) {
          continue;
        }

        // import a whole BGAnimation
        import_dir = sAniDir + import_dir;
        CollapsePath(import_dir);

        if (Right(import_dir, 1) != "/") {
          import_dir += "/";
        }

        ASSERT_M(IsADirectory(import_dir), import_dir + " isn't a directory");

        std::string path_to_ini = import_dir + "BGAnimation.ini";

        IniFile ini2;
        ini2.ReadFile(path_to_ini);

        AddLayersFromAniDir(import_dir, &ini2);
      } else {
        // import as a single layer
        BGAnimationLayer* bg_layer = new BGAnimationLayer;
        bg_layer->LoadFromNode(key);
        this->AddChild(bg_layer);
      }
    }
  }
}

void BGAnimation::LoadFromAniDir(const std::string& s_ani_dir) {
  DeleteAllChildren();

  if (s_ani_dir.empty()) {
    return;
  }

  std::string sAniDir = s_ani_dir;
  if (Right(sAniDir, 1) != "/") {
    sAniDir += "/";
  }

  ASSERT_M(IsADirectory(sAniDir), sAniDir + " isn't a directory");

  std::string path_to_ini = sAniDir + "BGAnimation.ini";

  if (DoesFileExist(path_to_ini)) {
    if (PREFSMAN->quirks_mode_) {
      // This is a 3.9-style BGAnimation (using .ini)
      IniFile ini;
      ini.ReadFile(path_to_ini);

      AddLayersFromAniDir(sAniDir, &ini);  // TODO: Check for circular load

      XNode* bg_animation = ini.GetChild("BGAnimation");
      XNode dummy("BGAnimation");
      if (bg_animation == nullptr) {
        bg_animation = &dummy;
      }

      LoadFromNode(bg_animation);
    } else  // We don't officially support .ini files anymore.
    {
      XNode dummy("BGAnimation");
      XNode* bg = &dummy;
      LoadFromNode(bg);
    }
  } else {
    // This is an 3.0 and before-style BGAnimation (not using .ini)

    // loading a directory of layers
    std::vector<std::string> image_paths;
    ASSERT(sAniDir != "");

    GetDirListing(sAniDir + "*.png", image_paths, false, true);
    GetDirListing(sAniDir + "*.jpg", image_paths, false, true);
    GetDirListing(sAniDir + "*.jpeg", image_paths, false, true);
    GetDirListing(sAniDir + "*.gif", image_paths, false, true);
    GetDirListing(sAniDir + "*.ogv", image_paths, false, true);
    GetDirListing(sAniDir + "*.avi", image_paths, false, true);
    GetDirListing(sAniDir + "*.mpg", image_paths, false, true);
    GetDirListing(sAniDir + "*.mpeg", image_paths, false, true);

    SortRStringArray(image_paths);

    for (unsigned i = 0; i < image_paths.size(); i++) {
      const std::string path = image_paths[i];
      if (Left(Basename(path), 1) == "_") {
        continue;  // don't directly load files starting with an underscore
      }
      BGAnimationLayer* layer = new BGAnimationLayer;
      layer->LoadFromAniLayerFile(image_paths[i]);
      AddChild(layer);
    }
  }
}

void BGAnimation::LoadFromNode(const XNode* node) {
  std::string dir;
  if (node->GetAttrValue("AniDir", dir)) {
    LoadFromAniDir(dir);
  }

  ActorFrame::LoadFromNode(node);

  /* Backwards-compatibility: if a "LengthSeconds" value is present, create a
   * dummy actor that sleeps for the given length of time. This will extend
   * GetTweenTimeLeft. */
  float length_seconds = 0;
  if (node->GetAttrValue("LengthSeconds", length_seconds)) {
    Actor* actor = new Actor;
    actor->SetName("BGAnimation dummy");
    actor->SetVisible(false);
    apActorCommands ap =
        ActorUtil::ParseActorCommands(ssprintf("sleep,%f", length_seconds));
    actor->AddCommand("On", ap);
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
