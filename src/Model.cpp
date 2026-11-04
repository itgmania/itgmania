#include "Model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "ModelManager.h"
#include "ModelTypes.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageMath.h"
#include "RageTexture.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "StdString.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(Model);

static const float FRAMES_PER_SECOND = 30;
static const std::string DEFAULT_ANIMATION_NAME = "default";

Model::Model() {
  texture_wrapping_ = true;
  SetUseZBuffer(true);
  SetCullMode(CULL_BACK);
  geometry_ = nullptr;
  cur_animation_ = nullptr;
  default_animation_rate_ = 1;
  cur_animation_rate_ = 1;
  loop_ = true;
  temp_geometry_ = nullptr;
}

Model::~Model() { Clear(); }

void Model::Clear() {
  if (geometry_) {
    MODELMAN->UnloadModel(geometry_);
    geometry_ = nullptr;
  }
  bones_.clear();
  materials_.clear();
  map_name_to_animation_.clear();
  cur_animation_ = nullptr;
  RecalcAnimationLengthSeconds();

  if (temp_geometry_) {
    DISPLAY->DeleteCompiledGeometry(temp_geometry_);
  }
}

#define THROW                                                               \
  RageException::Throw(                                                     \
      "Parse error in \"%s\" at line %d: \"%s\".", sPath.c_str(), iLineNum, \
      sLine.c_str())

// TODO: Move MS3D loading into its own class. - Colby
void Model::LoadMilkshapeAscii(const std::string& sPath) {
  LoadPieces(sPath, sPath, sPath);
}

void Model::LoadPieces(
    const std::string& sMeshesPath, const std::string& sMaterialsPath,
    const std::string& sBonesPath) {
  Clear();

  // TRICKY: Load materials before geometry so we can figure out whether the
  // materials require normals.
  LoadMaterialsFromMilkshapeAscii(sMaterialsPath);

  ASSERT(geometry_ == nullptr);
  geometry_ =
      MODELMAN->LoadMilkshapeAscii(sMeshesPath, this->MaterialsNeedNormals());

  // Validate material indices.
  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    const msMesh* pMesh = &geometry_->meshes_[i];

    if (pMesh->material_index_ >= (int)materials_.size()) {
      RageException::Throw(
          "Model \"%s\" mesh \"%s\" references material index %i, but there "
          "are only %i materials.",
          sMeshesPath.c_str(), pMesh->name_.c_str(), pMesh->material_index_,
          (int)materials_.size());
    }
  }

  if (LoadMilkshapeAsciiBones(DEFAULT_ANIMATION_NAME, sBonesPath)) {
    PlayAnimation(DEFAULT_ANIMATION_NAME);
  }

  // Setup temp vertices (if necessary)
  if (geometry_->HasAnyPerVertexBones()) {
    temp_meshes_ = geometry_->meshes_;
    temp_geometry_ = DISPLAY->CreateCompiledGeometry();
    temp_geometry_->Set(temp_meshes_);
  }
  RecalcAnimationLengthSeconds();
}

void Model::LoadFromNode(const XNode* pNode) {
  std::string s1, s2, s3;
  ActorUtil::GetAttrPath(pNode, "Meshes", s1);
  ActorUtil::GetAttrPath(pNode, "Materials", s2);
  ActorUtil::GetAttrPath(pNode, "Bones", s3);
  if (!s1.empty() || !s2.empty() || !s3.empty()) {
    ASSERT(!s1.empty() && !s2.empty() && !s3.empty());
    LoadPieces(s1, s2, s3);
  }

  Actor::LoadFromNode(pNode);
  RecalcAnimationLengthSeconds();
}

void Model::LoadMaterialsFromMilkshapeAscii(const std::string& _sPath) {
  std::string sPath = _sPath;

  FixSlashesInPlace(sPath);
  const std::string sDir = Dirname(sPath);

  RageFile f;
  if (!f.Open(sPath)) {
    RageException::Throw(
        "Model::LoadMilkshapeAscii Could not open \"%s\": %s", sPath.c_str(),
        f.GetError().c_str());
  }

  std::string sLine;
  int iLineNum = 0;

  while (f.GetLine(sLine) > 0) {
    iLineNum++;

    if (!strncmp(sLine.c_str(), "//", 2)) {
      continue;
    }

    int nFrame;
    if (sscanf(sLine.c_str(), "Frames: %d", &nFrame) == 1) {
      // ignore
      // m_pModel->nTotalFrames = nFrame;
    }
    if (sscanf(sLine.c_str(), "Frame: %d", &nFrame) == 1) {
      // ignore
      // m_pModel->nFrame = nFrame;
    }

    // materials
    int nNumMaterials = 0;
    if (sscanf(sLine.c_str(), "Materials: %d", &nNumMaterials) == 1) {
      materials_.resize(nNumMaterials);

      char szName[256];

      for (int i = 0; i < nNumMaterials; i++) {
        msMaterial& Material = materials_[i];

        // name
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        if (sscanf(sLine.c_str(), "\"%255[^\"]\"", szName) != 1) {
          THROW;
        }
        Material.name_ = szName;

        // ambient
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 Ambient;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &Ambient[0], &Ambient[1],
                &Ambient[2], &Ambient[3]) != 4) {
          THROW;
        }
        Material.ambient_ = Ambient;

        // diffuse
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 Diffuse;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &Diffuse[0], &Diffuse[1],
                &Diffuse[2], &Diffuse[3]) != 4) {
          THROW;
        }
        Material.Diffuse = Diffuse;

        // specular
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 Specular;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &Specular[0], &Specular[1],
                &Specular[2], &Specular[3]) != 4) {
          THROW;
        }
        Material.specular_ = Specular;

        // emissive
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 Emissive;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &Emissive[0], &Emissive[1],
                &Emissive[2], &Emissive[3]) != 4) {
          THROW;
        }
        Material.emissive_ = Emissive;

        // shininess
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        float fShininess;
        if (!StringConversion::FromString(sLine, fShininess)) {
          THROW;
        }
        Material.shininess_ = fShininess;

        // transparency
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        float fTransparency;
        if (!StringConversion::FromString(sLine, fTransparency)) {
          THROW;
        }
        Material.transparency_ = fTransparency;

        // diffuse texture
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        strcpy(szName, "");
        sscanf(sLine.c_str(), "\"%255[^\"]\"", szName);
        std::string sDiffuseTexture = szName;

        if (sDiffuseTexture == "") {
          Material.diffuse.LoadBlank();
        } else {
          std::string sTexturePath = sDir + sDiffuseTexture;
          FixSlashesInPlace(sTexturePath);
          CollapsePath(sTexturePath);
          if (!IsAFile(sTexturePath)) {
            RageException::Throw(
                "\"%s\" references a texture \"%s\" that does not exist.",
                sPath.c_str(), sTexturePath.c_str());
          }

          Material.diffuse.Load(sTexturePath);
        }

        // alpha texture
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        strcpy(szName, "");
        sscanf(sLine.c_str(), "\"%255[^\"]\"", szName);
        std::string sAlphaTexture = szName;

        if (sAlphaTexture == "") {
          Material.alpha_.LoadBlank();
        } else {
          std::string sTexturePath = sDir + sAlphaTexture;
          FixSlashesInPlace(sTexturePath);
          CollapsePath(sTexturePath);
          if (!IsAFile(sTexturePath)) {
            RageException::Throw(
                "\"%s\" references a texture \"%s\" that does not exist.",
                sPath.c_str(), sTexturePath.c_str());
          }

          Material.alpha_.Load(sTexturePath);
        }
      }
    }
  }
}

bool Model::LoadMilkshapeAsciiBones(
    const std::string& sAniName, const std::string& sPath) {
  map_name_to_animation_[sAniName] = msAnimation();
  msAnimation& Animation = map_name_to_animation_[sAniName];

  if (Animation.LoadMilkshapeAsciiBones(sAniName, sPath)) {
    map_name_to_animation_.erase(sAniName);
    return false;
  }

  return true;
}

bool Model::EarlyAbortDraw() const {
  return geometry_ == nullptr || geometry_->meshes_.empty();
}

void Model::DrawCelShaded() {
  // First pass: shell. We only want the backfaces for this.
  DISPLAY->SetCelShaded(1);
  DISPLAY->SetCullMode(CULL_FRONT);
  this->SetZWrite(
      false);  // XXX: Why on earth isn't the culling working? -Colby
  this->Draw();

  // Second pass: cel shading
  DISPLAY->SetCelShaded(2);
  DISPLAY->SetCullMode(CULL_BACK);
  this->SetZWrite(true);
  this->Draw();

  DISPLAY->SetCelShaded(0);
}

void Model::DrawPrimitives() {
  Actor::SetGlobalRenderStates();  // set Actor-specified render states

  // Don't if we're fully transparent
  if (temp_state_->diffuse_[0].a_ < 0.001f && temp_state_->glow_.a_ < 0.001f) {
    return;
  }

  DISPLAY->Scale(1, -1, 1);  // flip Y so positive is up

  //////////////////////
  // render the diffuse pass
  //////////////////////
  if (temp_state_->diffuse_[0].a_ > 0) {
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);

    for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
      const msMesh* pMesh = &geometry_->meshes_[i];

      if (pMesh->material_index_ != -1)  // has a material
      {
        // apply material
        msMaterial& mat = materials_[pMesh->material_index_];

        RageColor Emissive = mat.emissive_;
        RageColor Ambient = mat.ambient_;
        RageColor Diffuse = mat.Diffuse;

        Emissive *= temp_state_->diffuse_[0];
        Ambient *= temp_state_->diffuse_[0];
        Diffuse *= temp_state_->diffuse_[0];

        DISPLAY->SetMaterial(
            Emissive, Ambient, Diffuse, mat.specular_, mat.shininess_);

        RageVector2 vTexTranslate = mat.diffuse.GetTextureTranslate();
        if (vTexTranslate.x_ != 0 || vTexTranslate.y_ != 0) {
          DISPLAY->TexturePushMatrix();
          DISPLAY->TextureTranslate(vTexTranslate.x_, vTexTranslate.y_);
        }

        /* There's some common code that could be folded out here, but
         * it seems clearer to keep it separate. */
        bool bUseMultitexture =
            PREFSMAN->allow_multitexture_ && DISPLAY->GetNumTextureUnits() >= 2;
        if (bUseMultitexture) {
          // render the diffuse texture with texture unit 1
          DISPLAY->SetTexture(
              TextureUnit_1,
              mat.diffuse.GetCurrentTexture()
                  ? mat.diffuse.GetCurrentTexture()->GetTexHandle()
                  : 0);
          Actor::SetTextureRenderStates();  // set Actor-specified render states
          DISPLAY->SetSphereEnvironmentMapping(
              TextureUnit_1, mat.diffuse.sphere_mapped_);

          // render the additive texture with texture unit 2
          if (mat.alpha_.GetCurrentTexture()) {
            DISPLAY->SetTexture(
                TextureUnit_2,
                mat.alpha_.GetCurrentTexture()
                    ? mat.alpha_.GetCurrentTexture()->GetTexHandle()
                    : 0);
            Actor::SetTextureRenderStates();  // set Actor-specified render
                                              // states
            DISPLAY->SetSphereEnvironmentMapping(
                TextureUnit_2, mat.alpha_.sphere_mapped_);
            DISPLAY->SetTextureMode(TextureUnit_2, TextureMode_Add);
            DISPLAY->SetTextureFiltering(TextureUnit_2, true);
          } else {
            DISPLAY->SetTexture(TextureUnit_2, 0);

            // set current texture back to 0 or else texture
            // transform applied above  isn't used. Why?!?
            DISPLAY->SetTexture(
                TextureUnit_1,
                mat.diffuse.GetCurrentTexture()
                    ? mat.diffuse.GetCurrentTexture()->GetTexHandle()
                    : 0);
          }

          // go
          DrawMesh(i);

          // Turn off environment mapping on tex unit 0.
          DISPLAY->SetSphereEnvironmentMapping(TextureUnit_1, false);
        } else {
          // render the diffuse texture
          DISPLAY->SetTexture(
              TextureUnit_1,
              mat.diffuse.GetCurrentTexture()
                  ? mat.diffuse.GetCurrentTexture()->GetTexHandle()
                  : 0);
          Actor::SetTextureRenderStates();  // set Actor-specified render states
          DISPLAY->SetSphereEnvironmentMapping(
              TextureUnit_1, mat.diffuse.sphere_mapped_);
          DrawMesh(i);

          // render the additive texture
          if (mat.alpha_.GetCurrentTexture()) {
            DISPLAY->SetTexture(
                TextureUnit_1,
                mat.alpha_.GetCurrentTexture()
                    ? mat.alpha_.GetCurrentTexture()->GetTexHandle()
                    : 0);
            Actor::SetTextureRenderStates();  // set Actor-specified render
                                              // states

            DISPLAY->SetSphereEnvironmentMapping(
                TextureUnit_1, mat.alpha_.sphere_mapped_);
            // UGLY: This overrides the Actor's BlendMode.
            DISPLAY->SetBlendMode(BLEND_ADD);
            DISPLAY->SetTextureFiltering(TextureUnit_1, true);
            DrawMesh(i);
          }
        }

        if (vTexTranslate.x_ != 0 || vTexTranslate.y_ != 0) {
          DISPLAY->TexturePopMatrix();
        }
      } else {
        static const RageColor emissive(0, 0, 0, 0);
        static const RageColor ambient(0.2f, 0.2f, 0.2f, 1);
        static const RageColor diffuse(0.7f, 0.7f, 0.7f, 1);
        static const RageColor specular(0.2f, 0.2f, 0.2f, 1);
        static const float shininess = 1;
        DISPLAY->SetMaterial(emissive, ambient, diffuse, specular, shininess);
        DISPLAY->ClearAllTextures();
        DISPLAY->SetSphereEnvironmentMapping(TextureUnit_1, false);
        DrawMesh(i);
      }

      DISPLAY->SetSphereEnvironmentMapping(TextureUnit_1, false);
      DISPLAY->SetBlendMode(BLEND_NORMAL);
    }
  }

  // render the glow pass
  if (temp_state_->glow_.a_ > 0.0001f) {
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Glow);

    for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
      const msMesh* pMesh = &geometry_->meshes_[i];

      // apply material
      RageColor emissive = RageColor(0, 0, 0, 0);
      RageColor ambient = RageColor(0, 0, 0, 0);
      RageColor diffuse = temp_state_->glow_;
      RageColor specular = RageColor(0, 0, 0, 0);
      float shininess = 1;

      DISPLAY->SetMaterial(emissive, ambient, diffuse, specular, shininess);
      DISPLAY->ClearAllTextures();

      if (pMesh->material_index_ != -1) {
        msMaterial& mat = materials_[pMesh->material_index_];
        DISPLAY->SetTexture(
            TextureUnit_1, mat.diffuse.GetCurrentTexture()
                               ? mat.diffuse.GetCurrentTexture()->GetTexHandle()
                               : 0);
        Actor::SetTextureRenderStates();  // set Actor-specified render states
      } else {
        // hey why is this otherwise empty else block here? -aj
      }

      DrawMesh(i);
    }
  }
}

void Model::DrawMesh(int i) const {
  const msMesh* pMesh = &geometry_->meshes_[i];

  // apply mesh-specific bone (if any)
  if (pMesh->bone_index_ != -1) {
    DISPLAY->PushMatrix();

    const RageMatrix& mat = bones_[pMesh->bone_index_].final_;
    DISPLAY->PreMultMatrix(mat);
  }

  // Draw it
  const RageCompiledGeometry* TempGeometry =
      temp_geometry_ ? temp_geometry_ : geometry_->compiled_geometry_;
  DISPLAY->DrawCompiledGeometry(TempGeometry, i, geometry_->meshes_);

  if (pMesh->bone_index_ != -1) {
    DISPLAY->PopMatrix();
  }
}

void Model::SetDefaultAnimation(std::string sAnimation, float fPlayRate) {
  default_animation_ = sAnimation;
  default_animation_rate_ = fPlayRate;
}

void Model::PlayAnimation(const std::string& sAniName, float fPlayRate) {
  if (map_name_to_animation_.find(sAniName) == map_name_to_animation_.end()) {
    return;
  }

  const msAnimation* pNewAnimation = &map_name_to_animation_[sAniName];

  cur_frame_ = 0;
  cur_animation_rate_ = fPlayRate;

  if (cur_animation_ == pNewAnimation) {
    return;
  }

  cur_animation_ = pNewAnimation;

  // setup bones
  bones_.resize(cur_animation_->bones_.size());

  for (unsigned i = 0; i < cur_animation_->bones_.size(); i++) {
    const msBone* pBone = &cur_animation_->bones_[i];
    const RageVector3& vRot = pBone->rotation_;

    RageMatrixAngles(&bones_[i].relative_, vRot);

    bones_[i].relative_.m_[3][0] = pBone->position_[0];
    bones_[i].relative_.m_[3][1] = pBone->position_[1];
    bones_[i].relative_.m_[3][2] = pBone->position_[2];

    int nParentBone = cur_animation_->FindBoneByName(pBone->parent_name_);
    if (nParentBone != -1) {
      RageMatrixMultiply(
          &bones_[i].absolute_, &bones_[nParentBone].absolute_,
          &bones_[i].relative_);
    } else {
      bones_[i].absolute_ = bones_[i].relative_;
    }
    bones_[i].final_ = bones_[i].absolute_;
  }

  // subtract out the bone's resting position
  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    msMesh* pMesh = &geometry_->meshes_[i];
    std::vector<RageModelVertex>& Vertices = pMesh->vertices_;
    for (unsigned j = 0; j < Vertices.size(); j++) {
      // int iBoneIndex = (pMesh->m_iBoneIndex!=-1) ? pMesh->m_iBoneIndex :
      // bone;
      RageVector3& pos = Vertices[j].p_;
      int8_t bone = Vertices[j].bone_;
      if (bone != -1) {
        pos[0] -= bones_[bone].absolute_.m_[3][0];
        pos[1] -= bones_[bone].absolute_.m_[3][1];
        pos[2] -= bones_[bone].absolute_.m_[3][2];

        RageVector3 vTmp;

        RageMatrix inverse;
        RageMatrixTranspose(
            &inverse,
            &bones_[bone]
                 .absolute_);  // transpose = inverse for rotation matrices
        RageVec3TransformNormal(&vTmp, &pos, &inverse);

        pos = vTmp;
      }
    }
  }

  // Set up m_vpBones, just in case we're drawn without being Update()d.
  SetBones(cur_animation_, cur_frame_, bones_);
  UpdateTempGeometry();
}

void Model::SetPosition(float fSeconds) {
  cur_frame_ = FRAMES_PER_SECOND * fSeconds;
  cur_frame_ =
      std::clamp(cur_frame_, (float)0, (float)cur_animation_->total_frames_);
}

void Model::AdvanceFrame(float fDeltaTime) {
  if (geometry_ == nullptr || geometry_->meshes_.empty() || !cur_animation_) {
    return;  // bail early
  }

  // LOG->Trace( "m_fCurFrame = %f", m_fCurFrame );

  cur_frame_ += FRAMES_PER_SECOND * fDeltaTime * cur_animation_rate_;
  if (cur_frame_ < 0 || cur_frame_ >= cur_animation_->total_frames_) {
    if (default_animation_ != "") {
      this->PlayAnimation(default_animation_, default_animation_rate_);
      /* XXX: add to m_fCurFrame the wrapover from the previous
       * m_fCurFrame-m_pCurAnimation->nTotalFrames, so it doesn't skip */
    } else if (loop_) {
      wrap(cur_frame_, (float)cur_animation_->total_frames_);
    } else {
      cur_frame_ = std::clamp(
          cur_frame_, (float)0, (float)cur_animation_->total_frames_);
    }
  }

  SetBones(cur_animation_, cur_frame_, bones_);
  UpdateTempGeometry();
}

void Model::SetBones(
    const msAnimation* pAnimation, float fFrame,
    std::vector<myBone_t>& vpBones) {
  for (size_t i = 0; i < pAnimation->bones_.size(); ++i) {
    const msBone* pBone = &pAnimation->bones_[i];
    if (pBone->position_keys_.size() == 0 &&
        pBone->rotation_keys_.size() == 0) {
      vpBones[i].final_ = vpBones[i].absolute_;
      continue;
    }

    // search for the adjacent position keys
    const msPositionKey *pLastPositionKey = nullptr,
                        *pThisPositionKey = nullptr;
    for (size_t j = 0; j < pBone->position_keys_.size(); ++j) {
      const msPositionKey* pPositionKey = &pBone->position_keys_[j];
      if (pPositionKey->time_ >= fFrame) {
        pThisPositionKey = pPositionKey;
        break;
      }
      pLastPositionKey = pPositionKey;
    }

    RageVector3 vPos;
    if (pLastPositionKey != nullptr && pThisPositionKey != nullptr) {
      const float s =
          SCALE(fFrame, pLastPositionKey->time_, pThisPositionKey->time_, 0, 1);
      vPos = pLastPositionKey->position_ +
             (pThisPositionKey->position_ - pLastPositionKey->position_) * s;
    } else if (pLastPositionKey == nullptr) {
      vPos = pThisPositionKey->position_;
    } else if (pThisPositionKey == nullptr) {
      vPos = pLastPositionKey->position_;
    }

    // search for the adjacent rotation keys
    const msRotationKey *pLastRotationKey = nullptr,
                        *pThisRotationKey = nullptr;
    for (size_t j = 0; j < pBone->rotation_keys_.size(); ++j) {
      const msRotationKey* pRotationKey = &pBone->rotation_keys_[j];
      if (pRotationKey->time_ >= fFrame) {
        pThisRotationKey = pRotationKey;
        break;
      }
      pLastRotationKey = pRotationKey;
    }

    RageVector4 vRot;
    if (pLastRotationKey != nullptr && pThisRotationKey != nullptr) {
      const float s =
          SCALE(fFrame, pLastRotationKey->time_, pThisRotationKey->time_, 0, 1);
      RageQuatSlerp(
          &vRot, pLastRotationKey->rotation_, pThisRotationKey->rotation_, s);
    } else if (pLastRotationKey == nullptr) {
      vRot = pThisRotationKey->rotation_;
    } else if (pThisRotationKey == nullptr) {
      vRot = pLastRotationKey->rotation_;
    }

    RageMatrix m;
    RageMatrixIdentity(&m);
    RageMatrixFromQuat(&m, vRot);
    m.m_[3][0] = vPos[0];
    m.m_[3][1] = vPos[1];
    m.m_[3][2] = vPos[2];

    RageMatrix RelativeFinal;
    RageMatrixMultiply(&RelativeFinal, &vpBones[i].relative_, &m);

    int iParentBone = pAnimation->FindBoneByName(pBone->parent_name_);
    if (iParentBone == -1) {
      vpBones[i].final_ = RelativeFinal;
    } else {
      RageMatrixMultiply(
          &vpBones[i].final_, &vpBones[iParentBone].final_, &RelativeFinal);
    }
  }
}

void Model::UpdateTempGeometry() {
  if (geometry_ == nullptr || temp_geometry_ == nullptr) {
    return;
  }

  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    const msMesh& origMesh = geometry_->meshes_[i];
    msMesh& tempMesh = temp_meshes_[i];
    const std::vector<RageModelVertex>& origVertices = origMesh.vertices_;
    std::vector<RageModelVertex>& tempVertices = tempMesh.vertices_;
    for (unsigned j = 0; j < origVertices.size(); j++) {
      RageVector3& tempPos = tempVertices[j].p_;
      RageVector3& tempNormal = tempVertices[j].n_;
      const RageVector3& originalPos = origVertices[j].p_;
      const RageVector3& originalNormal = origVertices[j].n_;
      int8_t bone = origVertices[j].bone_;

      if (bone == -1) {
        tempNormal = originalNormal;
        tempPos = originalPos;
      } else {
        RageVec3TransformNormal(
            &tempNormal, &originalNormal, &bones_[bone].final_);
        RageVec3TransformCoord(&tempPos, &originalPos, &bones_[bone].final_);
      }
    }
  }

  // send the new vertices to the graphics card
  temp_geometry_->Change(temp_meshes_);
}

void Model::Update(float fDelta) {
  Actor::Update(fDelta);
  AdvanceFrame(fDelta);

  for (unsigned i = 0; i < materials_.size(); ++i) {
    materials_[i].diffuse.Update(fDelta);
    materials_[i].alpha_.Update(fDelta);
  }
}

int Model::GetNumStates() const {
  int iMaxStates = 0;
  for (const msMaterial& m : materials_) {
    iMaxStates = std::max(iMaxStates, m.diffuse.GetNumStates());
  }
  return iMaxStates;
}

void Model::SetState(int iNewState) {
  for (msMaterial& m : materials_) {
    m.diffuse.SetState(iNewState);
    m.alpha_.SetState(iNewState);
  }
}

void Model::RecalcAnimationLengthSeconds() {
  animation_length_seconds_ = 0;
  for (const msMaterial& m : materials_) {
    animation_length_seconds_ = std::max(
        animation_length_seconds_, m.diffuse.GetAnimationLengthSeconds());
  }
}

void Model::SetSecondsIntoAnimation(float fSeconds) {
  for (msMaterial& m : materials_) {
    m.diffuse.SetSecondsIntoAnimation(fSeconds);
    m.alpha_.SetSecondsIntoAnimation(fSeconds);
  }
}

bool Model::MaterialsNeedNormals() const {
  return std::any_of(
      materials_.begin(), materials_.end(),
      [](const msMaterial& m) { return m.NeedsNormals(); });
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Model. */
class LunaModel : public Luna<Model> {
 public:
  static int position(T* p, lua_State* L) {
    p->SetPosition(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int playanimation(T* p, lua_State* L) {
    p->PlayAnimation(SArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int SetDefaultAnimation(T* p, lua_State* L) {
    p->SetDefaultAnimation(SArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int GetDefaultAnimation(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDefaultAnimation().c_str());
    return 1;
  }
  static int loop(T* p, lua_State* L) {
    p->SetLoop(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int rate(T* p, lua_State* L) {
    p->SetRate(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetNumStates(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumStates());
    return 1;
  }

  LunaModel() {
    ADD_METHOD(position);
    ADD_METHOD(playanimation);
    ADD_METHOD(SetDefaultAnimation);
    ADD_METHOD(GetDefaultAnimation);
    ADD_METHOD(loop);
    ADD_METHOD(rate);
    // sm-ssc adds:
    ADD_METHOD(GetNumStates);
    //  LoadMilkshapeAsciiBones?
  }
};

LUA_REGISTER_DERIVED_CLASS(Model, Actor)
// lua end

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
