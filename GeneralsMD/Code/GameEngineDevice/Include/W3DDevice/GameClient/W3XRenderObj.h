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
	// Bone-name interface so the Generals model/weapon code (W3DModelDraw's
	// findSingleBone etc) can resolve WeaponFireFXBone/WeaponMuzzleFlash/
	// WeaponLaunchBone ini names to skeleton pivots. Bone names are loaded from
	// the .skl hierarchy and stored in m_boneNames (index-aligned with m_bones).
	void SetBoneNames(const std::vector<AsciiString> &names);
	// Parent index per bone (matches W3DHierarchy Pivot Parent). Needed so a
	// controlled bone (turret) can cascade its rotation to children (barrel,
	// muzzle) during Render — otherwise the barrel stays at the bind-pose
	// orientation and doesn't follow the turret.
	void SetBoneParents(const std::vector<int> &parents);
	virtual int Get_Num_Bones(void);
	virtual const char *Get_Bone_Name(int bone_index);
	virtual int Get_Bone_Index(const char *bonename);
	virtual const Matrix3D &Get_Bone_Transform(int boneindex);
	virtual const Matrix3D &Get_Bone_Transform(const char *bonename);
	// Bone control for turret/barrel (game-logic driven rotation). The given
	// transform is the bone's LOCAL rotation (e.g. Rotate_Z(turretYaw)); it is
	// composed into the object-local WorldBones during Render. Mirrors
	// RenderObjClass::Control_Bone so W3D turret logic can drive a W3X model.
	virtual void Capture_Bone(int bindex) { if (bindex >= 0 && bindex < kMaxBones) m_boneCtrlActive[bindex] = true; }
	virtual void Release_Bone(int bindex) { if (bindex >= 0 && bindex < kMaxBones) m_boneCtrlActive[bindex] = false; }
	virtual bool Is_Bone_Captured(int bindex) const { return (bindex >= 0 && bindex < kMaxBones) ? m_boneCtrlActive[bindex] : false; }
	virtual void Control_Bone(int bindex, const Matrix3D &objtm, bool world_space_translation = false);
	// Animation override: set a bone's object-local rotation quaternion directly
	// (used by the W3X animation system for keyframe-driven bones like the
	// barrel). Takes precedence over Control_Bone for the same bone.
	void SetBoneAnimQuat(int bindex, const float q[4]);
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

	enum { kMaxBones = 64 };	// must match BindW3XBones' 64-bone array
	// Compose the bind pose + Control_Bone rotations + turret->barrel cascade
	// into 'out' (kMaxBones*8 floats, quat+offset per bone). Shared by Render
	// (which uploads it) and Get_Bone_Transform (muzzle/launch offset must follow
	// the animated turret). 'out' may alias m_bones when nothing is controlled.
	void composeControlledBones(float *out) const;
	std::vector<SubMesh> m_meshes;
	AsciiString m_fxName;
	int m_technique;
	std::vector<W3XShaderConstant> m_constants;
	float *m_bones;		// RA3 WorldBones (quat+offset, 2 float4 per bone)
	int m_boneCount;
	std::vector<AsciiString> m_boneNames;	// per-bone name (index-aligned with m_bones)
	std::vector<int> m_boneParents;			// per-bone parent index (-1 = root), for turret->barrel cascade
	Matrix3D m_boneTransformCache;			// Get_Bone_Transform returns a const ref
	bool m_boneCtrlActive[kMaxBones];		// per-bone turret-control flag
	float m_boneCtrlQuat[kMaxBones][4];		// per-bone control rotation (quat)
	Vector3 m_bmin;
	Vector3 m_bmax;
	unsigned int m_recolorHex;	// 0xFFRRGGBB faction color (0 = none -> white)
	Matrix3D m_worldTransform;
	bool m_valid;
	char m_name[64];	// model name for shadow-geometry caching / identification
};

#endif /* W3XRENDEROBJ_H */
