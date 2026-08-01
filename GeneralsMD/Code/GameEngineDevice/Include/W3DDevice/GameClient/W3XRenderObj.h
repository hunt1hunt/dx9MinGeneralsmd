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
	virtual int Class_ID(void) const { return RenderObjClass::CLASSID_UNKNOWN; }
	virtual RenderObjClass *Clone(void) const { return NULL; }
	virtual void Render(RenderInfoClass &rinfo);

	// Data population
	void AddSubMesh(IDirect3DVertexBuffer9 *vb, IDirect3DIndexBuffer9 *ib, int vertexCount, int triangleCount);
	void SetFX(const char *fxName, int technique, const std::vector<W3XShaderConstant> &constants);
	void SetBones(float *bones, int boneCount);
	void SetBounds(const Vector3 &min, const Vector3 &max);
	void SetRecolorColor(unsigned int hexColor) { m_recolorHex = hexColor; }	// 0xFFRRGGBB faction color
	void Clear(void);

	// World transform for rendering (kept separately; also in RenderObjClass base)
	void SetWorldTransform(const Matrix3D &m) { m_worldTransform = m; }
	const Matrix3D &GetWorldTransform(void) const { return m_worldTransform; }

protected:
	virtual void Update_Cached_Bounding_Volumes(void) const;

private:
	struct SubMesh
	{
		IDirect3DVertexBuffer9 *vb;
		IDirect3DIndexBuffer9 *ib;
		int vertexCount;
		int triangleCount;
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
};

#endif /* W3XRENDEROBJ_H */
