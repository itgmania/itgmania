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
    bool bAllVertsUseSameBone = true;

    char iBoneIndex = mesh.vertices_[0].bone_;

    for (unsigned j = 1; j < mesh.vertices_.size(); j++) {
      if (mesh.vertices_[j].bone_ != iBoneIndex) {
        bAllVertsUseSameBone = false;
        break;
      }
    }

    if (bAllVertsUseSameBone) {
      mesh.bone_index_ = iBoneIndex;

      // clear all vertex/bone associations;
      for (unsigned j = 0; j < mesh.vertices_.size(); j++) {
        mesh.vertices_[j].bone_ = -1;
      }
    }
  }
}

void RageModelGeometry::MergeMeshes(int iFromIndex, int iToIndex) {
  msMesh& meshFrom = meshes_[iFromIndex];
  msMesh& meshTo = meshes_[iToIndex];

  int iShiftTriangleVertexIndicesBy = meshTo.vertices_.size();
  int iStartShiftingAtTriangleIndex = meshTo.triangles_.size();

  meshTo.vertices_.insert(
      meshTo.vertices_.end(), meshFrom.vertices_.begin(),
      meshFrom.vertices_.end());
  meshTo.triangles_.insert(
      meshTo.triangles_.end(), meshFrom.triangles_.begin(),
      meshFrom.triangles_.end());

  for (unsigned i = iStartShiftingAtTriangleIndex; i < meshTo.triangles_.size();
       i++) {
    for (int j = 0; j < 3; j++) {
      uint16_t& iIndex = meshTo.triangles_[i].vertex_indices_[j];
      iIndex = uint16_t(iIndex + iShiftTriangleVertexIndicesBy);
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
    const std::string& _sPath, bool bNeedsNormals) {
  std::string sPath = _sPath;
  FixSlashesInPlace(sPath);
  const std::string sDir = Dirname(sPath);

  RageFile f;
  if (!f.Open(sPath)) {
    RageException::Throw(
        "RageModelGeometry::LoadMilkshapeAscii Could not open \"%s\": %s",
        sPath.c_str(), f.GetError().c_str());
  }

  std::string sLine;
  int iLineNum = 0;
  char szName[MS_MAX_NAME];
  int nFlags, nIndex;

  RageVec3ClearBounds(mins_, maxs_);

  while (f.GetLine(sLine) > 0) {
    iLineNum++;

    if (!strncmp(sLine.c_str(), "//", 2)) {
      continue;
    }

    int nFrame;
    if (sscanf(sLine.c_str(), "Frames: %d", &nFrame) == 1) {
      // ignore
      // m_pRageModelGeometry->nTotalFrames = nFrame;
    }
    if (sscanf(sLine.c_str(), "Frame: %d", &nFrame) == 1) {
      // ignore
      // m_pRageModelGeometry->nFrame = nFrame;
    }

    int nNumMeshes = 0;
    if (sscanf(sLine.c_str(), "Meshes: %d", &nNumMeshes) == 1) {
      ASSERT(meshes_.empty());
      meshes_.resize(nNumMeshes);

      for (int i = 0; i < nNumMeshes; i++) {
        msMesh& mesh = meshes_[i];
        std::vector<RageModelVertex>& Vertices = mesh.vertices_;
        std::vector<msTriangle>& Triangles = mesh.triangles_;

        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        // mesh: name, flags, material index
        if (sscanf(
                sLine.c_str(), "\"%31[^\"]\" %d %d", szName, &nFlags,
                &nIndex) != 3) {
          THROW;
        }

        mesh.name_ = szName;
        // mesh.nFlags = nFlags;
        mesh.material_index_ = (uint8_t)nIndex;

        mesh.bone_index_ = -1;

        //
        // vertices
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int nNumVertices = 0;
        if (sscanf(sLine.c_str(), "%d", &nNumVertices) != 1) {
          THROW;
        }

        Vertices.resize(nNumVertices);

        for (int j = 0; j < nNumVertices; j++) {
          RageModelVertex& v = Vertices[j];

          if (f.GetLine(sLine) <= 0) {
            THROW;
          }

          if (sscanf(
                  sLine.c_str(), "%d %f %f %f %f %f %d", &nFlags, &v.p_[0],
                  &v.p_[1], &v.p_[2], &v.t_[0], &v.t_[1], &nIndex) != 7) {
            THROW;
          }

          // vertex.nFlags = nFlags;
          if (nFlags & 1) {
            v.texture_matrix_scale_.x_ = 0;
          }
          if (nFlags & 2) {
            v.texture_matrix_scale_.y_ = 0;
          }
          if (nFlags & 4) {
            v.t_[0] = v.p_[0] / v.t_[0];
            v.t_[1] = v.p_[1] / v.t_[1];
          }
          v.bone_ = (uint8_t)nIndex;
          RageVec3AddToBounds(v.p_, mins_, maxs_);
        }

        //
        // normals
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int nNumNormals = 0;
        if (sscanf(sLine.c_str(), "%d", &nNumNormals) != 1) {
          THROW;
        }

        std::vector<RageVector3> Normals;
        Normals.resize(nNumNormals);
        for (int j = 0; j < nNumNormals; j++) {
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
          Normals[j] = Normal;
        }

        //
        // triangles
        //
        if (f.GetLine(sLine) <= 0) {
          THROW;
        }

        int nNumTriangles = 0;
        if (sscanf(sLine.c_str(), "%d", &nNumTriangles) != 1) {
          THROW;
        }

        Triangles.resize(nNumTriangles);

        for (int j = 0; j < nNumTriangles; j++) {
          if (f.GetLine(sLine) <= 0) {
            THROW;
          }

          uint16_t nIndices[3];
          uint16_t nNormalIndices[3];
          if (sscanf(
                  sLine.c_str(), "%d %hu %hu %hu %hu %hu %hu %d", &nFlags,
                  &nIndices[0], &nIndices[1], &nIndices[2], &nNormalIndices[0],
                  &nNormalIndices[1], &nNormalIndices[2], &nIndex) != 8) {
            THROW;
          }

          // deflate the normals into vertices
          for (int k = 0; k < 3; k++) {
            ASSERT_M(
                nIndices[k] < Vertices.size(),
                ssprintf(
                    "mesh \"%s\" tri #%i accesses vertex %i, but we only have "
                    "%i",
                    szName, j, nIndices[k], int(Vertices.size())));
            ASSERT_M(
                nNormalIndices[k] < Normals.size(),
                ssprintf(
                    "mesh \"%s\" tri #%i accesses normal %i, but we only have "
                    "%i",
                    szName, j, nNormalIndices[k], int(Normals.size())));
            RageModelVertex& vertex = Vertices[nIndices[k]];
            RageVector3& normal = Normals[nNormalIndices[k]];
            vertex.n_ = normal;
            // mesh.Vertices[nIndices[k]].n = Normals[ nNormalIndices[k] ];
          }

          msTriangle& Triangle = Triangles[j];
          // Triangle.nFlags = nFlags;
          memcpy(
              &Triangle.vertex_indices_, nIndices,
              sizeof(Triangle.vertex_indices_));
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
