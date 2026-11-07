#include "RageModelGeometry.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ModelTypes.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "global.h"

#define MS_MAX_NAME 32

RageModelGeometry::RageModelGeometry() {
  ref_count_ = 1;
  compiled_geometry_ = DISPLAY->CreateCompiledGeometry();
}

RageModelGeometry::~RageModelGeometry() {
  DISPLAY->DeleteCompiledGeometry(compiled_geometry_);
}

void RageModelGeometry::OptimizeBones() {
  for (unsigned i = 0; i < meshes_.size(); i++) {
    msMesh& mesh = meshes_[i];

    if (mesh.vertices_.empty()) {
      continue;  // nothing to optimize
    }

    // check to see if all vertices have the same bone index
    bool all_verts_use_same_bone = true;

    char bone_index = mesh.vertices_[0].bone_;

    for (unsigned j = 1; j < mesh.vertices_.size(); j++) {
      if (mesh.vertices_[j].bone_ != bone_index) {
        all_verts_use_same_bone = false;
        break;
      }
    }

    if (all_verts_use_same_bone) {
      mesh.bone_index_ = bone_index;

      // clear all vertex/bone associations;
      for (unsigned j = 0; j < mesh.vertices_.size(); j++) {
        mesh.vertices_[j].bone_ = -1;
      }
    }
  }
}

void RageModelGeometry::MergeMeshes(int from_index, int to_index) {
  msMesh& mesh_from = meshes_[from_index];
  msMesh& mesh_to = meshes_[to_index];

  int shift_triangle_vertex_indices_by = mesh_to.vertices_.size();
  int start_shifting_at_triangle_index = mesh_to.triangles_.size();

  mesh_to.vertices_.insert(
      mesh_to.vertices_.end(), mesh_from.vertices_.begin(),
      mesh_from.vertices_.end());
  mesh_to.triangles_.insert(
      mesh_to.triangles_.end(), mesh_from.triangles_.begin(),
      mesh_from.triangles_.end());

  for (unsigned i = start_shifting_at_triangle_index;
       i < mesh_to.triangles_.size(); i++) {
    for (int j = 0; j < 3; j++) {
      uint16_t& index = mesh_to.triangles_[i].vertex_indices_[j];
      index = uint16_t(index + shift_triangle_vertex_indices_by);
    }
  }
}

bool RageModelGeometry::HasAnyPerVertexBones() const {
  for (unsigned i = 0; i < meshes_.size(); ++i) {
    const msMesh& mesh = meshes_[i];
    for (unsigned j = 0; j < mesh.vertices_.size(); ++j) {
      if (mesh.vertices_[j].bone_ != -1) {
        return true;
      }
    }
  }

  return false;
}

#define THROW                                                               \
  RageException::Throw(                                                     \
      "Parse error in \"%s\" at line %d: \"%s\".", sPath.c_str(), iLineNum, \
      sLine.c_str())

void RageModelGeometry::LoadMilkshapeAscii(
    const std::string& path, bool needs_normals) {
  std::string sPath = path;
  FixSlashesInPlace(sPath);
  const std::string dir = Dirname(sPath);

  RageFile f;
  if (!f.Open(sPath)) {
    RageException::Throw(
        "RageModelGeometry::LoadMilkshapeAscii Could not open \"%s\": %s",
        sPath.c_str(), f.GetError().c_str());
  }

  std::string sLine;
  int iLineNum = 0;
  char name[MS_MAX_NAME];
  int flags, index;

  RageVec3ClearBounds(mins_, maxs_);

  while (f.GetLine(sLine) > 0) {
    iLineNum++;

    if (!strncmp(sLine.c_str(), "//", 2)) {
      continue;
    }

    int frame;
    if (sscanf(sLine.c_str(), "Frames: %d", &frame) == 1) {
      // ignore
      // m_pRageModelGeometry->nTotalFrames = nFrame;
    }
    if (sscanf(sLine.c_str(), "Frame: %d", &frame) == 1) {
      // ignore
      // m_pRageModelGeometry->nFrame = nFrame;
    }

    int num_meshes = 0;
    if (sscanf(sLine.c_str(), "Meshes: %d", &num_meshes) == 1) {
      ASSERT(meshes_.empty());
      meshes_.resize(num_meshes);

      for (int i = 0; i < num_meshes; i++) {
        msMesh& mesh = meshes_[i];
        std::vector<RageModelVertex>& vertices = mesh.vertices_;
        std::vector<msTriangle>& triangles = mesh.triangles_;

        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        // mesh: name, flags, material index
        if (sscanf(sLine.c_str(), "\"%31[^\"]\" %d %d", name, &flags, &index) !=
            3) {
          THROW;
        }

        mesh.name_ = name;
        // mesh.nFlags = nFlags;
        mesh.material_index_ = (uint8_t)index;

        mesh.bone_index_ = -1;

        //
        // vertices
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int num_vertices = 0;
        if (sscanf(sLine.c_str(), "%d", &num_vertices) != 1) {
          THROW;
        }

        vertices.resize(num_vertices);

        for (int j = 0; j < num_vertices; j++) {
          RageModelVertex& v = vertices[j];

          if (f.GetLine(sLine) <= 0) {
            THROW;
          }

          if (sscanf(
                  sLine.c_str(), "%d %f %f %f %f %f %d", &flags, &v.p_[0],
                  &v.p_[1], &v.p_[2], &v.t_[0], &v.t_[1], &index) != 7) {
            THROW;
          }

          // vertex.nFlags = nFlags;
          if (flags & 1) {
            v.texture_matrix_scale_.x_ = 0;
          }
          if (flags & 2) {
            v.texture_matrix_scale_.y_ = 0;
          }
          if (flags & 4) {
            v.t_[0] = v.p_[0] / v.t_[0];
            v.t_[1] = v.p_[1] / v.t_[1];
          }
          v.bone_ = (uint8_t)index;
          RageVec3AddToBounds(v.p_, mins_, maxs_);
        }

        //
        // normals
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int num_normals = 0;
        if (sscanf(sLine.c_str(), "%d", &num_normals) != 1) {
          THROW;
        }

        std::vector<RageVector3> normals;
        normals.resize(num_normals);
        for (int j = 0; j < num_normals; j++) {
          if (f.GetLine(sLine) <= 0) {
            THROW;
          }

          RageVector3 Normal;
          if (sscanf(
                  sLine.c_str(), "%f %f %f", &Normal[0], &Normal[1],
                  &Normal[2]) != 3) {
            THROW;
          }

          RageVec3Normalize((RageVector3*)&Normal, (RageVector3*)&Normal);
          normals[j] = Normal;
        }

        //
        // triangles
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int num_triangles = 0;
        if (sscanf(sLine.c_str(), "%d", &num_triangles) != 1) {
          THROW;
        }

        triangles.resize(num_triangles);

        for (int j = 0; j < num_triangles; j++) {
          if (f.GetLine(sLine) <= 0) {
            THROW;
          }

          uint16_t indices[3];
          uint16_t normal_indices[3];
          if (sscanf(
                  sLine.c_str(), "%d %hu %hu %hu %hu %hu %hu %d", &flags,
                  &indices[0], &indices[1], &indices[2], &normal_indices[0],
                  &normal_indices[1], &normal_indices[2], &index) != 8) {
            THROW;
          }

          // deflate the normals into vertices
          for (int k = 0; k < 3; k++) {
            ASSERT_M(
                indices[k] < vertices.size(),
                ssprintf(
                    "mesh \"%s\" tri #%i accesses vertex %i, but we only have "
                    "%i",
                    name, j, indices[k], int(vertices.size())));
            ASSERT_M(
                normal_indices[k] < normals.size(),
                ssprintf(
                    "mesh \"%s\" tri #%i accesses normal %i, but we only have "
                    "%i",
                    name, j, normal_indices[k], int(normals.size())));
            RageModelVertex& vertex = vertices[indices[k]];
            RageVector3& normal = normals[normal_indices[k]];
            vertex.n_ = normal;
            // mesh.Vertices[nIndices[k]].n = Normals[ nNormalIndices[k] ];
          }

          msTriangle& triangle = triangles[j];
          // Triangle.nFlags = nFlags;
          memcpy(
              &triangle.vertex_indices_, indices,
              sizeof(triangle.vertex_indices_));
          // Triangle.nSmoothingGroup = nIndex;
        }
      }
    }
  }

  OptimizeBones();

  if (DISPLAY->SupportsPerVertexMatrixScale()) {
    if (meshes_.size() == 2 && meshes_[0].name_ == meshes_[1].name_) {
      MergeMeshes(1, 0);
    }
  }

  // send the finalized vertices to the graphics card
  compiled_geometry_->Set(meshes_);
}

/*
 * Copyright (c) 2001-2002 Chris Danford
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
