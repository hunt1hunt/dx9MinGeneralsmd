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
#include <math.h>
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
			{0, 64, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 68, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
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
	m_recolorHex(0),
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
// Quaternion helpers (RA3 WorldBones skinning) — world-transform the bones
//=============================================================================
static void W3XQuatMultiply(float *out, const float *a, const float *b)
{
	// out = a * b (Hamilton product)
	out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
	out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
	out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
	out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

static void W3XQuatRotateVector(float *out, const float *q, const float *v)
{
	// out = rotate v by quaternion q
	float t[3];
	t[0] = 2.0f * (q[1]*v[2] - q[2]*v[1]);
	t[1] = 2.0f * (q[2]*v[0] - q[0]*v[2]);
	t[2] = 2.0f * (q[0]*v[1] - q[1]*v[0]);
	out[0] = v[0] + q[3]*t[0] + (q[1]*t[2] - q[2]*t[1]);
	out[1] = v[1] + q[3]*t[1] + (q[2]*t[0] - q[0]*t[2]);
	out[2] = v[2] + q[3]*t[2] + (q[0]*t[1] - q[1]*t[0]);
}

// Extract the rotation part of a Matrix3D as a quaternion (x,y,z,w)
static void W3XMatrix3DToQuat(const Matrix3D &m, float *q)
{
	float m00 = m[0].X, m01 = m[0].Y, m02 = m[0].Z;
	float m10 = m[1].X, m11 = m[1].Y, m12 = m[1].Z;
	float m20 = m[2].X, m21 = m[2].Y, m22 = m[2].Z;
	float tr = m00 + m11 + m22;
	if (tr > 0.0f) {
		float s = (float)sqrt(tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (m21 - m12) / s;
		q[1] = (m02 - m20) / s;
		q[2] = (m10 - m01) / s;
	} else if (m00 > m11 && m00 > m22) {
		float s = (float)sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		q[3] = (m21 - m12) / s;
		q[0] = 0.25f * s;
		q[1] = (m01 + m10) / s;
		q[2] = (m02 + m20) / s;
	} else if (m11 > m22) {
		float s = (float)sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		q[3] = (m02 - m20) / s;
		q[0] = (m01 + m10) / s;
		q[1] = 0.25f * s;
		q[2] = (m12 + m21) / s;
	} else {
		float s = (float)sqrt(1.0f + m22 - m00 - m11) * 2.0f;
		q[3] = (m10 - m01) / s;
		q[0] = (m02 + m20) / s;
		q[1] = (m12 + m21) / s;
		q[2] = 0.25f * s;
	}
}


//=============================================================================
// Texture path cache: ResolveTextureDDS does file reads; cache the resolved
// path by texture name so the per-frame render path doesn't hit the file
// system (and spam the log) for every texture every frame.
//=============================================================================
struct W3XRenderTexPathEntry
{
	AsciiString name;
	AsciiString ddsPath;
};
static W3XRenderTexPathEntry s_texPathCache[32];
static int s_texPathCacheSize = 0;

static AsciiString ResolveTexturePathCached(const char *texName)
{
	if (!texName || !texName[0]) return AsciiString("");
	for (int i = 0; i < s_texPathCacheSize; i++) {
		if (s_texPathCache[i].name.compare(texName) == 0) return s_texPathCache[i].ddsPath;
	}
	AsciiString path = W3XLoader::ResolveTextureDDS(texName);
	if (s_texPathCacheSize < 32) {
		s_texPathCache[s_texPathCacheSize].name = texName;
		s_texPathCache[s_texPathCacheSize].ddsPath = path;
		s_texPathCacheSize++;
	}
	return path;
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

	// Select technique FIRST (D3DX9 pattern): SetTechnique before setting any
	// parameters, otherwise it can reset textures/matrices set beforehand.
	{
		D3DXHANDLE hTech = effect->GetTechnique(m_technique);
		D3DXTECHNIQUE_DESC techDesc;
		memset(&techDesc, 0, sizeof(techDesc));
		if (hTech && SUCCEEDED(effect->GetTechniqueDesc(hTech, &techDesc))) {
			DEBUG_LOG(("[W3X_P6]   Technique %d: '%s' (%u passes)\n", m_technique, techDesc.Name, techDesc.Passes));
		}
		if (hTech) {
			effect->SetTechnique(hTech);
			// Validate the technique against the device (SM3.0 caps, etc.)
			HRESULT hrVal = effect->ValidateTechnique(hTech);
			DEBUG_LOG(("[W3X_P6]   ValidateTechnique('%s') hr=0x%08X\n",
				techDesc.Name ? techDesc.Name : "?", (int)hrVal));
		} else {
			DEBUG_LOG(("[W3X_P6]   Technique %d NOT FOUND\n", m_technique));
		}
	}

	// Bind engine constants using the real scene camera (rinfo.Camera)
	W3XEffectManager::Instance()->BindEngineConstants(effect, rinfo);

	// Compute object→world→clip matrices from the object's world transform
	// and the scene camera (SAGE column-major → D3D row-major).
	Matrix4x4 world;
	world.Init(m_worldTransform);
	world = world.Transpose();
	Matrix4x4 view;
	view.Init(rinfo.Camera.Get_View_Matrix());
	view = view.Transpose();
	Matrix4x4 proj;
	rinfo.Camera.Get_D3D_Projection_Matrix(&proj);
	proj = proj.Transpose();
	Matrix4x4 vp = Multiply(view, proj);
	Matrix4x4 wvp = Multiply(world, vp);

	// RA3 shaders use World (object→world) and ViewProjection (world→clip);
	// some shaders take WorldViewProj directly. Set whichever the effect
	// exposes, overriding the DX8Wrapper-based bindings (which return
	// identity for World in this render context).
	D3DXHANDLE hWorld = effect->GetParameterByName(NULL, "World");
	if (hWorld) effect->SetMatrix(hWorld, (const D3DXMATRIX*)&world);
	D3DXHANDLE hVP = effect->GetParameterByName(NULL, "ViewProjection");
	if (hVP) effect->SetMatrix(hVP, (const D3DXMATRIX*)&vp);
	D3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");
	if (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);

	// ShadowMapWorldToShadow — bind identity so the skinned VS's ShadowCS
	// output isn't garbage/NaN (which could cause the draw to be rejected).
	D3DXHANDLE hShadowW2S = effect->GetParameterByName(NULL, "ShadowMapWorldToShadow");
	if (hShadowW2S) {
		Matrix4x4 ident;
		ident.Make_Identity();
		effect->SetMatrix(hShadowW2S, (const D3DXMATRIX*)&ident);
	}

	// Faction color (RA3 RecolorColor) from the owning player's team color.
	D3DXHANDLE hRecolor = effect->GetParameterByName(NULL, "RecolorColor");
	if (hRecolor) {
		unsigned int rc = m_recolorHex ? m_recolorHex : 0xFFFFFFFF;	// 0xFFRRGGBB, fall back to white
		float rcv[4] = {
			(float)((rc >> 16) & 0xFF) / 255.0f,
			(float)((rc >> 8)  & 0xFF) / 255.0f,
			(float)( rc        & 0xFF) / 255.0f,
			1.0f
		};
		effect->SetVector(hRecolor, (const D3DXVECTOR4*)rcv);
	}

	// DIAG: dump matrices once to verify camera/view correctness
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

	// Set per-instance constants (DIAGNOSTIC: log texture resolution/load)
	static bool w3xConstDiag = false;
	for (size_t ci = 0; ci < m_constants.size(); ci++) {
		const W3XShaderConstant &c = m_constants[ci];
		D3DXHANDLE h = effect->GetParameterByName(NULL, c.name.str());
		if (!h) {
			if (w3xConstDiag) DEBUG_LOG(("[W3X_P6]   const[%d] '%s': NOT FOUND in effect\n", (int)ci, c.name.str()));
			continue;
		}
		switch (c.type) {
			case W3X_CONSTANT_FLOAT: effect->SetFloat(h, c.floatValue); break;
			case W3X_CONSTANT_BOOL:  effect->SetBool(h, c.boolValue); break;
			case W3X_CONSTANT_INT:   effect->SetInt(h, c.intValue); break;
			case W3X_CONSTANT_VECTOR: {
				D3DXVECTOR4 v(c.vecValue[0], c.vecValue[1], c.vecValue[2], c.vecValue[3]);
				effect->SetVector(h, &v); break;
			}
			case W3X_CONSTANT_TEXTURE: {
				AsciiString dds = ResolveTexturePathCached(c.textureValue.str());
				DEBUG_LOG(("[W3X_P6]   TEXTURE[%d] '%s' = '%s' -> dds='%s'\n",
					(int)ci, c.name.str(), c.textureValue.str(), dds.str()));
				if (!dds.isEmpty()) {
					TextureClass *tex = WW3DAssetManager::Get_Instance()->Get_Texture(dds.str(), MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, true);
					if (tex && tex->Peek_D3D_Texture()) {
						IDirect3DTexture9 *d3dt = static_cast<IDirect3DTexture9*>(tex->Peek_D3D_Texture());
						HRESULT hrt = effect->SetTexture(h, d3dt);
						DEBUG_LOG(("[W3X_P6]     -> SetTexture hr=0x%08X\n", (int)hrt));
						// DIAG: log the actual D3D texture dimensions/format to
						// confirm it's the real 1024x1024 DXT5, not a placeholder.
						{
							D3DSURFACE_DESC sd;
							memset(&sd, 0, sizeof(sd));
							if (d3dt && SUCCEEDED(d3dt->GetLevelDesc(0, &sd))) {
								DEBUG_LOG(("[W3X_P6]     -> tex %dx%d fmt=%d (D3DFMT_A8R8G8B8=%d DXT1=%d DXT5=%d)\n",
									(int)sd.Width, (int)sd.Height, (int)sd.Format,
									(int)D3DFMT_A8R8G8B8, (int)D3DFMT_DXT1, (int)D3DFMT_DXT5));
							} else {
								DEBUG_LOG(("[W3X_P6]     -> GetLevelDesc FAILED\n"));
							}
						}
					} else {
						DEBUG_LOG(("[W3X_P6]     -> Get_Texture FAILED (tex=%p)\n", (void*)tex));
					}
				} else {
					DEBUG_LOG(("[W3X_P6]     -> ResolveTextureDDS EMPTY\n"));
				}
				break;
			}
			default: break;
		}
	}
	w3xConstDiag = true;

	// Upload bones (WorldBones)
	if (m_boneCount > 0 && m_bones) {
		D3DXHANDLE hBones = effect->GetParameterByName(NULL, "WorldBones");
		if (hBones) {
			// The loaded bones are in OBJECT-LOCAL space. The RA3 skinned VS
			// (VS_H_11skin) transforms the vertex by the bone and then by
			// ViewProjection (world->clip) with NO World matrix — so WorldBones
			// must be in WORLD space. Compose the object's world transform.
			float wq[4];
			W3XMatrix3DToQuat(m_worldTransform, wq);
			Vector3 wtrans = m_worldTransform.Get_Translation();
			float wb[64 * 8];	// enough for up to 64 bones
			for (int bi = 0; bi < m_boneCount && bi < 64; bi++) {
				const float *bq = &m_bones[bi*8 + 0];
				const float *bo = &m_bones[bi*8 + 4];
				float *wo = &wb[bi*8];
				W3XQuatMultiply(&wo[0], wq, bq);				// worldRot = W_rot * boneRot
				W3XQuatRotateVector(&wo[4], wq, bo);			// rotate offset by W_rot
				wo[4] += wtrans.X; wo[5] += wtrans.Y; wo[6] += wtrans.Z;	// + W_trans
				wo[7] = bo[3];									// alpha
			}
			HRESULT hrb = effect->SetFloatArray(hBones, wb, m_boneCount * 8);
			DEBUG_LOG(("[W3X_P6]   WorldBones: %d bones, SetFloatArray hr=0x%08X\n", m_boneCount, (int)hrb));
		} else {
			DEBUG_LOG(("[W3X_P6]   WorldBones param NOT FOUND (model has %d bones)\n", m_boneCount));
		}
		// DIAG: dump first few bones' quat+offset to verify the WorldBones values
		static bool w3xBoneDiag = false;
		if (!w3xBoneDiag && m_boneCount > 0) {
			for (int bi = 0; bi < m_boneCount && bi < 3; bi++) {
				DEBUG_LOG(("[W3X_P6]   BONE[%d] quat=(%.3f,%.3f,%.3f,%.3f) off=(%.3f,%.3f,%.3f,%.3f)\n",
					bi,
					m_bones[bi*8+0], m_bones[bi*8+1], m_bones[bi*8+2], m_bones[bi*8+3],
					m_bones[bi*8+4], m_bones[bi*8+5], m_bones[bi*8+6], m_bones[bi*8+7]));
			}
			w3xBoneDiag = true;
		}
	}

	// Set NumJointsPerVertex so the RA3 VSchooser() picks the skinned vertex
	// shader (1 = hard skin). The model has bones, so skin it.
	{
		D3DXHANDLE hJoints = effect->GetParameterByName(NULL, "NumJointsPerVertex");
		if (hJoints) {
			int joints = m_boneCount > 0 ? 1 : 0;
			HRESULT hrj = effect->SetInt(hJoints, joints);
			DEBUG_LOG(("[W3X_P6]   NumJointsPerVertex=%d hr=0x%08X (bones=%d)\n", joints, (int)hrj, m_boneCount));
		}
	}

	IDirect3DDevice9 *dev9 = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
	if (!dev9) return;

	// Bind RA3 auxiliary samplers that the pixel shader multiplies into the
	// output. ShroudTexture (PBR5-10-objects-ARPBR.FX:393) turns the whole
	// model black when unbound (samples as black); CloudTexture/ShadowMap too.
	{
		static IDirect3DTexture9 *s_whiteTex = NULL;
		if (!s_whiteTex) {
			D3DXCreateTexture(dev9, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_whiteTex);
			if (s_whiteTex) {
				D3DLOCKED_RECT lr;
				if (SUCCEEDED(s_whiteTex->LockRect(0, &lr, NULL, 0))) {
					if (lr.pBits) *((DWORD*)lr.pBits) = 0xFFFFFFFF;	// solid white
					s_whiteTex->UnlockRect(0);
				}
			}
		}
		if (s_whiteTex) {
			const char *auxTex[] = { "ShroudTexture", "CloudTexture", "ShadowMap" };
			for (int ai = 0; ai < 3; ai++) {
				D3DXHANDLE hAux = effect->GetParameterByName(NULL, auxTex[ai]);
				if (hAux) {
					effect->SetTexture(hAux, s_whiteTex);
					DEBUG_LOG(("[W3X_P6]   aux sampler '%s' -> white\n", auxTex[ai]));
				}
			}
		}
	}

	// Vertex declaration
	IDirect3DVertexDeclaration9 *decl = W3XGetVertexDecl(dev9);
	DEBUG_LOG(("[W3X_P6]   Vertex decl=%p, %d submeshes\n", (void*)decl, (int)m_meshes.size()));

	// Commit all pending parameter changes (textures, matrices, constants) to
	// the device. D3DX9 requires CommitChanges after SetX before drawing.
	HRESULT hrCommit = effect->CommitChanges();
	DEBUG_LOG(("[W3X_P6]   CommitChanges hr=0x%08X\n", (int)hrCommit));

	UINT totalDrawn = 0, totalFailed = 0;
	for (size_t si = 0; si < m_meshes.size(); si++) {
		SubMesh &sm = m_meshes[si];
		if (!sm.vb || !sm.ib) { DEBUG_LOG(("[W3X_P6]   submesh[%d] SKIPPED (vb=%p ib=%p)\n", (int)si, (void*)sm.vb, (void*)sm.ib)); continue; }

		dev9->SetStreamSource(0, sm.vb, 0, 76);	// W3XVertex stride (76 with COLOR + TEXCOORD1)
		dev9->SetIndices(sm.ib);
		if (decl) dev9->SetVertexDeclaration(decl);

		UINT passes;
		HRESULT hr = effect->Begin(&passes, 0);
		if (FAILED(hr)) { DEBUG_LOG(("[W3X_P6]   submesh[%d] Begin FAILED hr=0x%08X\n", (int)si, (int)hr)); totalFailed++; continue; }
		// DIAG: check the actual bound vertex/pixel shader after Begin
		{
			IDirect3DVertexShader9 *vs = NULL;
			IDirect3DPixelShader9 *ps = NULL;
			dev9->GetVertexShader(&vs);
			dev9->GetPixelShader(&ps);
			DEBUG_LOG(("[W3X_P6]   submesh[%d] bound VS=%p PS=%p\n", (int)si, (void*)vs, (void*)ps));
			if (vs) vs->Release();
			if (ps) ps->Release();
		}
		for (UINT p = 0; p < passes; p++) {
			hr = effect->BeginPass(p);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P6]   submesh[%d] BeginPass(%u) FAILED hr=0x%08X\n", (int)si, p, (int)hr)); break; }
			// W3X triangles are CW; the RA3 technique sets CullMode=2 (culls CW),
			// so disable culling AFTER BeginPass (BeginPass re-applies the pass
			// render states and would otherwise undo this).
			dev9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			hr = dev9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sm.vertexCount, 0, sm.triangleCount);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P6]   submesh[%d] DrawIndexedPrimitive FAILED hr=0x%08X (vc=%d tc=%d)\n", (int)si, (int)hr, sm.vertexCount, sm.triangleCount)); totalFailed++; }
			else totalDrawn++;
			effect->EndPass();
		}
		effect->End();
	}

	// NOTE: do NOT reset device state here. The W3X render uses the raw D3D9
	// device directly (bypassing DX8Wrapper's state cache); resetting state
	// here desyncs DX8Wrapper and breaks subsequent regular renders.

	DEBUG_LOG(("[W3X_P6]   Draw summary: %u submeshes drawn OK, %u failed (of %d)\n", totalDrawn, totalFailed, (int)m_meshes.size()));
}
