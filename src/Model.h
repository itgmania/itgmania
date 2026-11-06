/* Model - A 3D model. */

#ifndef MODEL_H
#define MODEL_H

#include <map>
#include <string>
#include <vector>

#include "Actor.h"
#include "ModelTypes.h"

class RageModelGeometry;
class RageCompiledGeometry;

class Model : public Actor {
 public:
  Model();
  virtual ~Model();
  virtual Model* Copy() const override;

  void Clear();

  void LoadPieces(
      const std::string& sMeshesPath, const std::string& sMaterialsPath,
      const std::string& sBomesPath);
  void LoadMilkshapeAscii(const std::string& sFile);
  void LoadMaterialsFromMilkshapeAscii(const std::string& sPath);
  bool LoadMilkshapeAsciiBones(
      const std::string& sAniName, const std::string& sPath);

  void LoadFromNode(const XNode* pNode) override;

  void PlayAnimation(const std::string& sAniName, float fPlayRate = 1);
  void SetRate(float fRate) { cur_animation_rate_ = fRate; }
  void SetLoop(bool b) { loop_ = b; }
  void SetPosition(float fSeconds);

  virtual void Update(float fDelta) override;
  virtual bool EarlyAbortDraw() const override;
  virtual void DrawPrimitives() override;

  void DrawCelShaded();

  virtual int GetNumStates() const override;
  virtual void SetState(int iNewState) override;
  virtual float GetAnimationLengthSeconds() const override {
    return animation_length_seconds_;
  }
  virtual void RecalcAnimationLengthSeconds();
  virtual void SetSecondsIntoAnimation(float fSeconds) override;

  std::string GetDefaultAnimation() const { return default_animation_; };
  void SetDefaultAnimation(std::string sAnimation, float fPlayRate = 1);

  bool MaterialsNeedNormals() const;

  // Lua
  virtual void PushSelf(lua_State* L) override;

  Model& operator=(const Model& rhs) = delete;

 private:
  RageModelGeometry* geometry_;

  float animation_length_seconds_;
  std::vector<msMaterial> materials_;
  std::map<std::string, msAnimation> map_name_to_animation_;
  const msAnimation* cur_animation_;

  static void SetBones(
      const msAnimation* pAnimation, float fFrame,
      std::vector<myBone_t>& vpBones);
  std::vector<myBone_t> bones_;

  // If any vertex has a bone weight, then then render from m_pTempGeometry.
  // Otherwise, render directly from m_pGeometry.
  RageCompiledGeometry* temp_geometry_;
  void UpdateTempGeometry();

  /* Keep a copy of the mesh data only if m_pTempGeometry is in use.  The normal
   * and position data will be changed; the rest is static and kept only to
   * prevent making a complete copy. */
  std::vector<msMesh> temp_meshes_;

  void DrawMesh(int i) const;
  void AdvanceFrame(float fDeltaTime);

  float cur_frame_;
  std::string default_animation_;
  float default_animation_rate_;
  float cur_animation_rate_;
  bool loop_;
};

#endif

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
