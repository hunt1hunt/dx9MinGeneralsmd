/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifndef W3XRENDEROBJ_H
#define W3XRENDEROBJ_H

#include "always.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WWMath/vector3.h"
#include "WWMath/sphere.h"
#include "WWMath/aabox.h"
#include "WWMath/matrix3d.h"
#include "W3DDevice/GameClient/w3x_loader.h"
#include <vector>
#include <string.h>

struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DDevice9;
struct IDirect3DVertexDeclaration9;

// Shared W3X vertex declaration (W3XVertex layout). Returns cached decl.
IDirect3DVertexDeclaration9 *W3XGetVertexDecl(IDirect3DDevice9 *dev);

//-----------------------------------------------------------------------------
// W3XRenderObjClass: a RenderObjClass that renders W3X mesh data using
// D3DXEffect. Added to the 3D scene so doRender() calls Render() in the
// correct pass (unlike DrawModule::doDrawModule which runs in the update pass).
//-----------------------------------------------------------------------------
class W3XRenderObjClass : public RenderObjClass
{
	W3DMPO_GLUE(W3XRenderObjClass)

public:
	W3XRenderObjClass();
	virtual ~W3XRenderObjClass();

	// RenderObjClass interface
	virtual int Class_ID(void) const { return CLASSID_W3X; }
	virtual RenderObjClass *Clone(void) const { return NULL; }
	virtual void Render(RenderInfoClass &rinfo);

	// Data population
	void AddSubMesh(IDirect3DVertexBuffer9 *vb, IDirect3DIndexBuffer9 *ib, int vertexCount, int triangleCount);
	void SetSubMeshTangent(int subMeshIndex, bool hasTangents, bool hasBinormals);
	// Per-sub-mesh shader constants (textures/values) for the shared effect.
	// Without this, sub-meshes using the model-wide shader inherit the first
	// sub-mesh's textures instead of their own XML-declared ones.
	void SetSubMeshConstants(int subMeshIndex, const std::vector<W3XShaderConstant> &constants);
	// Per-sub-mesh shader override. A sub-mesh with an empty fxName uses the
	// model-wide shader (set by SetFX). Non-empty overrides per sub-mesh.
	void SetSubMeshShader(int subMeshIndex, const char *fxName, int technique,
		const std::vector<W3XShaderConstant> &constants);
	void SetFX(const char *fxName, int technique, const std::vector<W3XShaderConstant> &constants);
	void SetBones(float *bones, int boneCount);
	// Skin-bone access for the volumetric shadow system. The shadow geometry is
	// built from the raw bind-pose sub-mesh vertices; it must be skinned with the
	// same WorldBones (quat+offset, 2 float4 per bone) the renderer uses, or the
	// shadow volume sits at the bind-pose position (no self-shadow on skinned
	// parts like the turret/barrel). NULL if the model has no bones.
	const float *GetBones(void) const { return m_bones; }
	int GetBoneCount(void) const { return m_boneCount; }
	void SetBounds(const Vector3 &min, const Vector3 &max);
	void SetRecolorColor(unsigned int hexColor) { m_recolorHex = hexColor; }	// 0xFFRRGGBB faction color
	void Clear(void);

	// World transform for rendering (kept separately; also in RenderObjClass base)
	void SetWorldTransform(const Matrix3D &m) { m_worldTransform = m; }
	const Matrix3D &GetWorldTransform(void) const { return m_worldTransform; }

	// Unique class id so W3DShadowGeometryManager::Load_Geom routes a W3X
	// render object to initFromW3X (builds volumetric shadow geometry from
	// the sub-mesh vertex/index buffers).
	enum { CLASSID_W3X = 0x00010001 };

	// Sub-mesh accessors for the volumetric shadow system (reads the D3D
	// MANAGED buffers back to build shadow geometry).
	int GetSubMeshCount(void) const { return (int)m_meshes.size(); }
	IDirect3DVertexBuffer9 *GetSubMeshVB(int i) const { return (i >= 0 && i < (int)m_meshes.size()) ? m_meshes[i].vb : NULL; }
	IDirect3DIndexBuffer9 *GetSubMeshIB(int i) const { return (i >= 0 && i < (int)m_meshes.size()) ? m_meshes[i].ib : NULL; }
	int GetSubMeshVertexCount(int i) const { return (i >= 0 && i < (int)m_meshes.size()) ? m_meshes[i].vertexCount : 0; }
	int GetSubMeshTriangleCount(int i) const { return (i >= 0 && i < (int)m_meshes.size()) ? m_meshes[i].triangleCount : 0; }

	// Name (used as the shadow-geometry cache key by W3DShadowGeometryManager).
	// The base RenderObjClass::Set_Name is a no-op and Get_Name returns "UNNAMED",
	// which would make ALL W3X models share one shadow geometry. Store the real
	// model name so each W3X model gets its own cached volumetric geometry.
	virtual const char *Get_Name(void) const { return m_name; }
	virtual void Set_Name(const char *name) {
		if (name) { strncpy(m_name, name, sizeof(m_name) - 1); m_name[sizeof(m_name) - 1] = '\0'; }
	}

protected:
	virtual void Update_Cached_Bounding_Volumes(void) const;

private:
	struct SubMesh
	{
		IDirect3DVertexBuffer9 *vb;
		IDirect3DIndexBuffer9 *ib;
		int vertexCount;
		int triangleCount;
		bool hasTangents;	// native TANGENT/BINORMAL data present (bump-normal usable)
		bool hasBinormals;
		// Per-sub-mesh shader override (empty fxName -> use model-wide shader)
		AsciiString fxName;
		int technique;
		std::vector<W3XShaderConstant> constants;
	};

	std::vector<SubMesh> m_meshes;
	AsciiString m_fxName;
	int m_technique;
	std::vector<W3XShaderConstant> m_constants;
	float *m_bones;		// RA3 WorldBones (quat+offset, 2 float4 per bone)
	int m_boneCount;
	Vector3 m_bmin;
	Vector3 m_bmax;
	unsigned int m_recolorHex;	// 0xFFRRGGBB faction color (0 = none -> white)
	Matrix3D m_worldTransform;
	bool m_valid;
	char m_name[64];	// model name for shadow-geometry caching / identification
};

#endif /* W3XRENDEROBJ_H */
