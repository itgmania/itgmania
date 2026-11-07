#include "ModelManager.h"

#include <map>
#include <string>

#include "RageLog.h"
#include "RageModelGeometry.h"
#include "RageUtil.h"
#include "global.h"

ModelManager* MODELMAN =
    nullptr;  // global and accessible from anywhere in our program

ModelManager::ModelManager() {}

ModelManager::~ModelManager() {
  for (std::map<std::string, RageModelGeometry*>::iterator i =
           map_file_to_geometry_.begin();
       i != map_file_to_geometry_.end(); ++i) {
    RageModelGeometry* geom = i->second;
    if (geom->ref_count_) {
      LOG->Trace(
          "MODELMAN LEAK: '%s', RefCount = %d.", i->first.c_str(),
          geom->ref_count_);
    }
    RageUtil::SafeDelete(geom);
  }
}

RageModelGeometry* ModelManager::LoadMilkshapeAscii(
    const std::string& file, bool need_normals) {
  std::map<std::string, RageModelGeometry*>::iterator p =
      map_file_to_geometry_.find(file);
  if (p != map_file_to_geometry_.end()) {
    /* Found the geometry.  Just increase the refcount and return it. */
    RageModelGeometry* geom = p->second;
    ++geom->ref_count_;
    return geom;
  }

  RageModelGeometry* geom = new RageModelGeometry;
  geom->LoadMilkshapeAscii(file, need_normals);

  map_file_to_geometry_[file] = geom;
  return geom;
}

void ModelManager::UnloadModel(RageModelGeometry* m) {
  m->ref_count_--;
  ASSERT(m->ref_count_ >= 0);

  if (m->ref_count_) {
    return; /* Can't unload models that are still referenced. */
  }

  for (std::map<std::string, RageModelGeometry*>::iterator i =
           map_file_to_geometry_.begin();
       i != map_file_to_geometry_.end(); ++i) {
    if (i->second == m) {
      if (prefs_.delayed_unload_) {
        // leave this geometry loaded
        return;
      } else {
        map_file_to_geometry_.erase(i);  // remove map entry
        RageUtil::SafeDelete(m);         // free the texture
        return;
      }
    }
  }

  FAIL_M("Tried to delete a texture that wasn't loaded");
}

bool ModelManager::SetPrefs(const ModelManagerPrefs& prefs) {
  prefs_ = prefs;
  return false;
}

/*
 * (c) 2003-2004 Chris Danford
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
