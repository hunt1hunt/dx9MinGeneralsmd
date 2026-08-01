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

#include "always.h"
#include "W3DDevice/GameClient/W3XRenderObj.h"
#include "W3DDevice/GameClient/W3XEffectManager.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/dx8wrapper.h"
#include "WWMath/matrix4.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3dformat.h"
#include "WW3D2/camera.h"
#include "wwdebug.h"
#include "wwmemlog.h"
#include <d3d9.h>
#include <d3dx9effect.h>

//=============================================================================
// Shared W3X vertex declaration (W3XVertex layout, 64-byte stride)
//=============================================================================
IDirect3DVertexDeclaration9 *W3XGetVertexDecl(IDirect3DDevice9 *dev)
{
	static IDirect3DVertexDeclaration9 *s_decl = NULL;
	if (!s_decl && dev) {
		D3DVERTEXELEMENT9 decl[] = {
			{0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
			{0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			{0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
			{0, 44, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
			{0, 56, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
			{0, 60, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0},
			D3DDECL_END()
		};
		dev->CreateVertexDeclaration(decl, &s_decl);
	}
	return s_decl;
}

//=============================================================================
// W3XRenderObjClass
//=============================================================================

W3XRenderObjClass::W3XRenderObjClass() :
	RenderObjClass(),
	m_technique(0),
	m_bones(NULL),
	m_boneCount(0),
	m_valid(false)
{
	m_bmin.Set(0, 0, 0);
	m_bmax.Set(0, 0, 0);
	m_worldTransform.Make_Identity();
}

W3XRenderObjClass::~W3XRenderObjClass()
{
	if (m_bones) {
		delete[] m_bones;
		m_bones = NULL;
	}
	// Release sub-mesh GPU buffers (ownership transferred from W3XModelDraw)
	for (size_t i = 0; i < m_meshes.size(); i++) {
		if (m_meshes[i].vb) { m_meshes[i].vb->Release(); m_meshes[i].vb = NULL; }
		if (m_meshes[i].ib) { m_meshes[i].ib->Release(); m_meshes[i].ib = NULL; }
	}
	m_meshes.clear();
	m_constants.clear();
}

void W3XRenderObjClass::AddSubMesh(IDirect3DVertexBuffer9 *vb, IDirect3DIndexBuffer9 *ib, int vertexCount, int triangleCount)
{
	SubMesh sm;
	sm.vb = vb;
	sm.ib = ib;
	sm.vertexCount = vertexCount;
	sm.triangleCount = triangleCount;
	m_meshes.push_back(sm);
	m_valid = true;
}

void W3XRenderObjClass::SetFX(const char *fxName, int technique, const std::vector<W3XShaderConstant> &constants)
{
	m_fxName = fxName ? fxName : "";
	m_technique = technique;
	m_constants = constants;
}

void W3XRenderObjClass::SetBones(float *bones, int boneCount)
{
	if (m_bones) delete[] m_bones;
	m_boneCount = boneCount;
	if (bones && boneCount > 0) {
		m_bones = new float[boneCount * 8];
		memcpy(m_bones, bones, boneCount * 8 * sizeof(float));
	} else {
		m_bones = NULL;
	}
}

void W3XRenderObjClass::SetBounds(const Vector3 &min, const Vector3 &max)
{
	m_bmin = min;
	m_bmax = max;
	Invalidate_Cached_Bounding_Volumes();
}

void W3XRenderObjClass::Clear(void)
{
	if (m_bones) { delete[] m_bones; m_bones = NULL; }
	m_boneCount = 0;
	m_meshes.clear();
	m_constants.clear();
	m_fxName.clear();
	m_valid = false;
}

//=============================================================================
// Update_Cached_Bounding_Volumes: set cached sphere/box from W3X bounds
//=============================================================================
void W3XRenderObjClass::Update_Cached_Bounding_Volumes(void) const
{
	Vector3 center = (m_bmin + m_bmax) * 0.5f;
	Vector3 extent = (m_bmax - m_bmin) * 0.5f;
	float radius = extent.Length();
	if (radius < 0.01f) radius = 0.01f;
	// Transform to world space (matching MeshClass) so the scene's frustum
	// culling tests the sphere at the object's actual position. Without this
	// the sphere stays at the origin and the object is always culled.
	Get_Transform().mulVector3(center);
	CachedBoundingBox.Init(center, extent);
	CachedBoundingSphere.Init(center, radius);
	Validate_Cached_Bounding_Volumes();
}

//=============================================================================
// Render: draw all sub-meshes using D3DXEffect.
// This is called by the scene during the correct render pass.
//=============================================================================
void W3XRenderObjClass::Render(RenderInfoClass &rinfo)
{
	if (!m_valid || m_meshes.empty()) return;

	ID3DXEffect *effect = W3XEffectManager::Instance()->GetEffect(m_fxName.str());
	if (!effect) { DEBUG_LOG(("[W3X_P5]   No effect '%s'\n", m_fxName.str())); return; }

	// Bind engine constants using the real scene camera (rinfo.Camera)
	W3XEffectManager::Instance()->BindEngineConstants(effect, rinfo);

	// Set WorldViewProj from our world transform + scene camera
	D3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");
	if (hWVP) {
		// World (row-major from Matrix3D)
		Matrix4x4 world;
		world.Init(m_worldTransform);
		world = world.Transpose();
		// View from scene camera (SAGE column-major -> transpose to row-major)
		Matrix4x4 view;
		view.Init(rinfo.Camera.Get_View_Matrix());
		view = view.Transpose();
		// Projection (D3D compatible, transpose to row-major)
		Matrix4x4 proj;
		rinfo.Camera.Get_D3D_Projection_Matrix(&proj);
		proj = proj.Transpose();
		Matrix4x4 wv = Multiply(world, view);
		Matrix4x4 wvp = Multiply(wv, proj);
		effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);

		// DIAG: dump WVP matrices once to verify camera/view correctness
		{
			static bool w3xWvpDiag = false;
			if (!w3xWvpDiag) {
				DEBUG_LOG(("[W3X_P5] DIAG world[0]=%.2f,%.2f,%.2f,%.2f\n",
					world[0].X, world[0].Y, world[0].Z, world[0].W));
				DEBUG_LOG(("[W3X_P5] DIAG view[0]=%.2f,%.2f,%.2f,%.2f view[3]=%.2f,%.2f,%.2f,%.2f\n",
					view[0].X, view[0].Y, view[0].Z, view[0].W,
					view[3].X, view[3].Y, view[3].Z, view[3].W));
				DEBUG_LOG(("[W3X_P5] DIAG proj[0]=%.2f,%.2f,%.2f,%.2f proj[3]=%.2f,%.2f,%.2f,%.2f\n",
					proj[0].X, proj[0].Y, proj[0].Z, proj[0].W,
					proj[3].X, proj[3].Y, proj[3].Z, proj[3].W));
				w3xWvpDiag = true;
			}
		}
	}

	// Set per-instance constants
	for (size_t ci = 0; ci < m_constants.size(); ci++) {
		const W3XShaderConstant &c = m_constants[ci];
		D3DXHANDLE h = effect->GetParameterByName(NULL, c.name.str());
		if (!h) continue;
		switch (c.type) {
			case W3X_CONSTANT_FLOAT: effect->SetFloat(h, c.floatValue); break;
			case W3X_CONSTANT_BOOL:  effect->SetBool(h, c.boolValue); break;
			case W3X_CONSTANT_INT:   effect->SetInt(h, c.intValue); break;
			case W3X_CONSTANT_VECTOR: {
				D3DXVECTOR4 v(c.vecValue[0], c.vecValue[1], c.vecValue[2], c.vecValue[3]);
				effect->SetVector(h, &v); break;
			}
			case W3X_CONSTANT_TEXTURE: {
				AsciiString dds = W3XLoader::ResolveTextureDDS(c.textureValue.str());
				if (!dds.isEmpty()) {
					TextureClass *tex = WW3DAssetManager::Get_Instance()->Get_Texture(dds.str(), MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, true);
					if (tex && tex->Peek_D3D_Texture()) {
						effect->SetTexture(h, static_cast<IDirect3DTexture9*>(tex->Peek_D3D_Texture()));
					}
				}
				break;
			}
			default: break;
		}
	}

	// Upload bones (WorldBones)
	if (m_boneCount > 0 && m_bones) {
		D3DXHANDLE hBones = effect->GetParameterByName(NULL, "WorldBones");
		if (hBones) {
			effect->SetFloatArray(hBones, m_bones, m_boneCount * 8);
		}
	}

	IDirect3DDevice9 *dev9 = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
	if (!dev9) return;

	// Select technique
	D3DXHANDLE hTech = effect->GetTechnique(m_technique);
	if (hTech) effect->SetTechnique(hTech);

	// Vertex declaration
	IDirect3DVertexDeclaration9 *decl = W3XGetVertexDecl(dev9);

	for (size_t si = 0; si < m_meshes.size(); si++) {
		SubMesh &sm = m_meshes[si];
		if (!sm.vb || !sm.ib) continue;

		dev9->SetStreamSource(0, sm.vb, 0, 64);	// W3XVertex stride
		dev9->SetIndices(sm.ib);
		if (decl) dev9->SetVertexDeclaration(decl);

		UINT passes;
		HRESULT hr = effect->Begin(&passes, 0);
		if (FAILED(hr)) continue;
		// W3X triangles are CW; disable culling
		dev9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		for (UINT p = 0; p < passes; p++) {
			hr = effect->BeginPass(p);
			if (FAILED(hr)) break;
			dev9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sm.vertexCount, 0, sm.triangleCount);
			effect->EndPass();
		}
		effect->End();
	}
}
