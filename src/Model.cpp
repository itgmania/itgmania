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

static const float kFramesPerSecond = 30;
static const std::string kDefaultAnimationName = "default";

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
void Model::LoadMilkshapeAscii(const std::string& path) {
  LoadPieces(path, path, path);
}

void Model::LoadPieces(
    const std::string& meshes_path, const std::string& materials_path,
    const std::string& bones_path) {
  Clear();

  // TRICKY: Load materials before geometry so we can figure out whether the
  // materials require normals.
  LoadMaterialsFromMilkshapeAscii(materials_path);

  ASSERT(geometry_ == nullptr);
  geometry_ =
      MODELMAN->LoadMilkshapeAscii(meshes_path, this->MaterialsNeedNormals());

  // Validate material indices.
  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    const msMesh* mesh = &geometry_->meshes_[i];

    if (mesh->material_index_ >= (int)materials_.size()) {
      RageException::Throw(
          "Model \"%s\" mesh \"%s\" references material index %i, but there "
          "are only %i materials.",
          meshes_path.c_str(), mesh->name_.c_str(), mesh->material_index_,
          (int)materials_.size());
    }
  }

  if (LoadMilkshapeAsciiBones(kDefaultAnimationName, bones_path)) {
    PlayAnimation(kDefaultAnimationName);
  }

  // Setup temp vertices (if necessary)
  if (geometry_->HasAnyPerVertexBones()) {
    temp_meshes_ = geometry_->meshes_;
    temp_geometry_ = DISPLAY->CreateCompiledGeometry();
    temp_geometry_->Set(temp_meshes_);
  }
  RecalcAnimationLengthSeconds();
}

void Model::LoadFromNode(const XNode* node) {
  std::string s1, s2, s3;
  ActorUtil::GetAttrPath(node, "Meshes", s1);
  ActorUtil::GetAttrPath(node, "Materials", s2);
  ActorUtil::GetAttrPath(node, "Bones", s3);
  if (!s1.empty() || !s2.empty() || !s3.empty()) {
    ASSERT(!s1.empty() && !s2.empty() && !s3.empty());
    LoadPieces(s1, s2, s3);
  }

  Actor::LoadFromNode(node);
  RecalcAnimationLengthSeconds();
}

void Model::LoadMaterialsFromMilkshapeAscii(const std::string& path) {
  std::string sPath = path;

  FixSlashesInPlace(sPath);
  const std::string dir = Dirname(sPath);

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

    int frame;
    if (sscanf(sLine.c_str(), "Frames: %d", &frame) == 1) {
      // ignore
      // m_pModel->nTotalFrames = nFrame;
    }
    if (sscanf(sLine.c_str(), "Frame: %d", &frame) == 1) {
      // ignore
      // m_pModel->nFrame = nFrame;
    }

    // materials
    int num_materials = 0;
    if (sscanf(sLine.c_str(), "Materials: %d", &num_materials) == 1) {
      materials_.resize(num_materials);

      char name[256];

      for (int i = 0; i < num_materials; i++) {
        msMaterial& material = materials_[i];

        // name
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        if (sscanf(sLine.c_str(), "\"%255[^\"]\"", name) != 1) {
          THROW;
        }
        material.name_ = name;

        // ambient
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 ambient;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &ambient[0], &ambient[1],
                &ambient[2], &ambient[3]) != 4) {
          THROW;
        }
        material.ambient_ = ambient;

        // diffuse
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 diffuse;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &diffuse[0], &diffuse[1],
                &diffuse[2], &diffuse[3]) != 4) {
          THROW;
        }
        material.Diffuse = diffuse;

        // specular
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 specular;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &specular[0], &specular[1],
                &specular[2], &specular[3]) != 4) {
          THROW;
        }
        material.specular_ = specular;

        // emissive
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        RageVector4 emissive;
        if (sscanf(
                sLine.c_str(), "%f %f %f %f", &emissive[0], &emissive[1],
                &emissive[2], &emissive[3]) != 4) {
          THROW;
        }
        material.emissive_ = emissive;

        // shininess
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        float shininess;
        if (!StringConversion::FromString(sLine, shininess)) {
          THROW;
        }
        material.shininess_ = shininess;

        // transparency
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        float transparency;
        if (!StringConversion::FromString(sLine, transparency)) {
          THROW;
        }
        material.transparency_ = transparency;

        // diffuse texture
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        strcpy(name, "");
        sscanf(sLine.c_str(), "\"%255[^\"]\"", name);
        std::string diffuse_texture = name;

        if (diffuse_texture == "") {
          material.diffuse.LoadBlank();
        } else {
          std::string texture_path = dir + diffuse_texture;
          FixSlashesInPlace(texture_path);
          CollapsePath(texture_path);
          if (!IsAFile(texture_path)) {
            RageException::Throw(
                "\"%s\" references a texture \"%s\" that does not exist.",
                sPath.c_str(), texture_path.c_str());
          }

          material.diffuse.Load(texture_path);
        }

        // alpha texture
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }
        strcpy(name, "");
        sscanf(sLine.c_str(), "\"%255[^\"]\"", name);
        std::string alpha_texture = name;

        if (alpha_texture == "") {
          material.alpha_.LoadBlank();
        } else {
          std::string texture_path = dir + alpha_texture;
          FixSlashesInPlace(texture_path);
          CollapsePath(texture_path);
          if (!IsAFile(texture_path)) {
            RageException::Throw(
                "\"%s\" references a texture \"%s\" that does not exist.",
                sPath.c_str(), texture_path.c_str());
          }

          material.alpha_.Load(texture_path);
        }
      }
    }
  }
}

bool Model::LoadMilkshapeAsciiBones(
    const std::string& ani_name, const std::string& path) {
  map_name_to_animation_[ani_name] = msAnimation();
  msAnimation& animation = map_name_to_animation_[ani_name];

  if (animation.LoadMilkshapeAsciiBones(ani_name, path)) {
    map_name_to_animation_.erase(ani_name);
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
      const msMesh* mesh = &geometry_->meshes_[i];

      if (mesh->material_index_ != -1)  // has a material
      {
        // apply material
        msMaterial& mat = materials_[mesh->material_index_];

        RageColor emissive = mat.emissive_;
        RageColor ambient = mat.ambient_;
        RageColor diffuse = mat.Diffuse;

        emissive *= temp_state_->diffuse_[0];
        ambient *= temp_state_->diffuse_[0];
        diffuse *= temp_state_->diffuse_[0];

        DISPLAY->SetMaterial(
            emissive, ambient, diffuse, mat.specular_, mat.shininess_);

        RageVector2 tex_translate = mat.diffuse.GetTextureTranslate();
        if (tex_translate.x_ != 0 || tex_translate.y_ != 0) {
          DISPLAY->TexturePushMatrix();
          DISPLAY->TextureTranslate(tex_translate.x_, tex_translate.y_);
        }

        /* There's some common code that could be folded out here, but
         * it seems clearer to keep it separate. */
        bool use_multitexture =
            PREFSMAN->allow_multitexture_ && DISPLAY->GetNumTextureUnits() >= 2;
        if (use_multitexture) {
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

        if (tex_translate.x_ != 0 || tex_translate.y_ != 0) {
          DISPLAY->TexturePopMatrix();
        }
      } else {
        static const RageColor kEmissive(0, 0, 0, 0);
        static const RageColor kAmbient(0.2f, 0.2f, 0.2f, 1);
        static const RageColor kDiffuse(0.7f, 0.7f, 0.7f, 1);
        static const RageColor kSpecular(0.2f, 0.2f, 0.2f, 1);
        static const float kShininess = 1;
        DISPLAY->SetMaterial(
            kEmissive, kAmbient, kDiffuse, kSpecular, kShininess);
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
      const msMesh* mesh = &geometry_->meshes_[i];

      // apply material
      RageColor kEmissive = RageColor(0, 0, 0, 0);
      RageColor kAmbient = RageColor(0, 0, 0, 0);
      RageColor kDiffuse = temp_state_->glow_;
      RageColor kSpecular = RageColor(0, 0, 0, 0);
      float kShininess = 1;

      DISPLAY->SetMaterial(
          kEmissive, kAmbient, kDiffuse, kSpecular, kShininess);
      DISPLAY->ClearAllTextures();

      if (mesh->material_index_ != -1) {
        msMaterial& mat = materials_[mesh->material_index_];
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
  const msMesh* mesh = &geometry_->meshes_[i];

  // apply mesh-specific bone (if any)
  if (mesh->bone_index_ != -1) {
    DISPLAY->PushMatrix();

    const RageMatrix& mat = bones_[mesh->bone_index_].final_;
    DISPLAY->PreMultMatrix(mat);
  }

  // Draw it
  const RageCompiledGeometry* temp_geometry =
      temp_geometry_ ? temp_geometry_ : geometry_->compiled_geometry_;
  DISPLAY->DrawCompiledGeometry(temp_geometry, i, geometry_->meshes_);

  if (mesh->bone_index_ != -1) {
    DISPLAY->PopMatrix();
  }
}

void Model::SetDefaultAnimation(std::string animation, float play_rate) {
  default_animation_ = animation;
  default_animation_rate_ = play_rate;
}

void Model::PlayAnimation(const std::string& ani_name, float play_rate) {
  if (map_name_to_animation_.find(ani_name) == map_name_to_animation_.end()) {
    return;
  }

  const msAnimation* new_animation = &map_name_to_animation_[ani_name];

  cur_frame_ = 0;
  cur_animation_rate_ = play_rate;

  if (cur_animation_ == new_animation) {
    return;
  }

  cur_animation_ = new_animation;

  // setup bones
  bones_.resize(cur_animation_->bones_.size());

  for (unsigned i = 0; i < cur_animation_->bones_.size(); i++) {
    const msBone* p_bone = &cur_animation_->bones_[i];
    const RageVector3& rot = p_bone->rotation_;

    RageMatrixAngles(&bones_[i].relative_, rot);

    bones_[i].relative_.m_[3][0] = p_bone->position_[0];
    bones_[i].relative_.m_[3][1] = p_bone->position_[1];
    bones_[i].relative_.m_[3][2] = p_bone->position_[2];

    int parent_bone = cur_animation_->FindBoneByName(p_bone->parent_name_);
    if (parent_bone != -1) {
      RageMatrixMultiply(
          &bones_[i].absolute_, &bones_[parent_bone].absolute_,
          &bones_[i].relative_);
    } else {
      bones_[i].absolute_ = bones_[i].relative_;
    }
    bones_[i].final_ = bones_[i].absolute_;
  }

  // subtract out the bone's resting position
  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    msMesh* mesh = &geometry_->meshes_[i];
    std::vector<RageModelVertex>& vertices = mesh->vertices_;
    for (unsigned j = 0; j < vertices.size(); j++) {
      // int iBoneIndex = (pMesh->m_iBoneIndex!=-1) ? pMesh->m_iBoneIndex :
      // bone;
      RageVector3& pos = vertices[j].p_;
      int8_t bone = vertices[j].bone_;
      if (bone != -1) {
        pos[0] -= bones_[bone].absolute_.m_[3][0];
        pos[1] -= bones_[bone].absolute_.m_[3][1];
        pos[2] -= bones_[bone].absolute_.m_[3][2];

        RageVector3 tmp;

        RageMatrix inverse;
        RageMatrixTranspose(
            &inverse,
            &bones_[bone]
                 .absolute_);  // transpose = inverse for rotation matrices
        RageVec3TransformNormal(&tmp, &pos, &inverse);

        pos = tmp;
      }
    }
  }

  // Set up m_vpBones, just in case we're drawn without being Update()d.
  SetBones(cur_animation_, cur_frame_, bones_);
  UpdateTempGeometry();
}

void Model::SetPosition(float seconds) {
  cur_frame_ = kFramesPerSecond * seconds;
  cur_frame_ =
      std::clamp(cur_frame_, (float)0, (float)cur_animation_->total_frames_);
}

void Model::AdvanceFrame(float delta_time) {
  if (geometry_ == nullptr || geometry_->meshes_.empty() || !cur_animation_) {
    return;  // bail early
  }

  // LOG->Trace( "m_fCurFrame = %f", m_fCurFrame );

  cur_frame_ += kFramesPerSecond * delta_time * cur_animation_rate_;
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
    const msAnimation* animation, float frame, std::vector<myBone_t>& bones) {
  for (size_t i = 0; i < animation->bones_.size(); ++i) {
    const msBone* bone = &animation->bones_[i];
    if (bone->position_keys_.size() == 0 && bone->rotation_keys_.size() == 0) {
      bones[i].final_ = bones[i].absolute_;
      continue;
    }

    // search for the adjacent position keys
    const msPositionKey *last_position_key = nullptr,
                        *this_position_key = nullptr;
    for (size_t j = 0; j < bone->position_keys_.size(); ++j) {
      const msPositionKey* position_key = &bone->position_keys_[j];
      if (position_key->time_ >= frame) {
        this_position_key = position_key;
        break;
      }
      last_position_key = position_key;
    }

    RageVector3 pos;
    if (last_position_key != nullptr && this_position_key != nullptr) {
      const float s = SCALE(
          frame, last_position_key->time_, this_position_key->time_, 0, 1);
      pos = last_position_key->position_ +
            (this_position_key->position_ - last_position_key->position_) * s;
    } else if (last_position_key == nullptr) {
      pos = this_position_key->position_;
    } else if (this_position_key == nullptr) {
      pos = last_position_key->position_;
    }

    // search for the adjacent rotation keys
    const msRotationKey *last_rotation_key = nullptr,
                        *this_rotation_key = nullptr;
    for (size_t j = 0; j < bone->rotation_keys_.size(); ++j) {
      const msRotationKey* rotation_key = &bone->rotation_keys_[j];
      if (rotation_key->time_ >= frame) {
        this_rotation_key = rotation_key;
        break;
      }
      last_rotation_key = rotation_key;
    }

    RageVector4 rot;
    if (last_rotation_key != nullptr && this_rotation_key != nullptr) {
      const float s = SCALE(
          frame, last_rotation_key->time_, this_rotation_key->time_, 0, 1);
      RageQuatSlerp(
          &rot, last_rotation_key->rotation_, this_rotation_key->rotation_, s);
    } else if (last_rotation_key == nullptr) {
      rot = this_rotation_key->rotation_;
    } else if (this_rotation_key == nullptr) {
      rot = last_rotation_key->rotation_;
    }

    RageMatrix m;
    RageMatrixIdentity(&m);
    RageMatrixFromQuat(&m, rot);
    m.m_[3][0] = pos[0];
    m.m_[3][1] = pos[1];
    m.m_[3][2] = pos[2];

    RageMatrix relative_final;
    RageMatrixMultiply(&relative_final, &bones[i].relative_, &m);

    int parent_bone = animation->FindBoneByName(bone->parent_name_);
    if (parent_bone == -1) {
      bones[i].final_ = relative_final;
    } else {
      RageMatrixMultiply(
          &bones[i].final_, &bones[parent_bone].final_, &relative_final);
    }
  }
}

void Model::UpdateTempGeometry() {
  if (geometry_ == nullptr || temp_geometry_ == nullptr) {
    return;
  }

  for (unsigned i = 0; i < geometry_->meshes_.size(); ++i) {
    const msMesh& orig_mesh = geometry_->meshes_[i];
    msMesh& temp_mesh = temp_meshes_[i];
    const std::vector<RageModelVertex>& orig_vertices = orig_mesh.vertices_;
    std::vector<RageModelVertex>& temp_vertices = temp_mesh.vertices_;
    for (unsigned j = 0; j < orig_vertices.size(); j++) {
      RageVector3& temp_pos = temp_vertices[j].p_;
      RageVector3& temp_normal = temp_vertices[j].n_;
      const RageVector3& original_pos = orig_vertices[j].p_;
      const RageVector3& original_normal = orig_vertices[j].n_;
      int8_t bone = orig_vertices[j].bone_;

      if (bone == -1) {
        temp_normal = original_normal;
        temp_pos = original_pos;
      } else {
        RageVec3TransformNormal(
            &temp_normal, &original_normal, &bones_[bone].final_);
        RageVec3TransformCoord(&temp_pos, &original_pos, &bones_[bone].final_);
      }
    }
  }

  // send the new vertices to the graphics card
  temp_geometry_->Change(temp_meshes_);
}

void Model::Update(float delta) {
  Actor::Update(delta);
  AdvanceFrame(delta);

  for (unsigned i = 0; i < materials_.size(); ++i) {
    materials_[i].diffuse.Update(delta);
    materials_[i].alpha_.Update(delta);
  }
}

int Model::GetNumStates() const {
  int max_states = 0;
  for (const msMaterial& m : materials_) {
    max_states = std::max(max_states, m.diffuse.GetNumStates());
  }
  return max_states;
}

void Model::SetState(int new_state) {
  for (msMaterial& m : materials_) {
    m.diffuse.SetState(new_state);
    m.alpha_.SetState(new_state);
  }
}

void Model::RecalcAnimationLengthSeconds() {
  animation_length_seconds_ = 0;
  for (const msMaterial& m : materials_) {
    animation_length_seconds_ = std::max(
        animation_length_seconds_, m.diffuse.GetAnimationLengthSeconds());
  }
}

void Model::SetSecondsIntoAnimation(float seconds) {
  for (msMaterial& m : materials_) {
    m.diffuse.SetSecondsIntoAnimation(seconds);
    m.alpha_.SetSecondsIntoAnimation(seconds);
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
