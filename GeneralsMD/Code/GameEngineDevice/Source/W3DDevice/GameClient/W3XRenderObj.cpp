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
#include "W3DDevice/GameClient/W3DDeferredRenderer.h"
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
	strcpy(m_name, "UNNAMED");	// base RenderObjClass returns this literal; store our own copy
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
	sm.hasTangents = false;
	sm.hasBinormals = false;
	m_meshes.push_back(sm);
	m_valid = true;
}

void W3XRenderObjClass::SetSubMeshTangent(int subMeshIndex, bool hasTangents, bool hasBinormals)
{
	if (subMeshIndex < 0 || subMeshIndex >= (int)m_meshes.size()) return;
	m_meshes[subMeshIndex].hasTangents = hasTangents;
	m_meshes[subMeshIndex].hasBinormals = hasBinormals;
}

void W3XRenderObjClass::SetSubMeshConstants(int subMeshIndex,
	const std::vector<W3XShaderConstant> &constants)
{
	if (subMeshIndex < 0 || subMeshIndex >= (int)m_meshes.size()) return;
	m_meshes[subMeshIndex].constants = constants;
}

void W3XRenderObjClass::SetSubMeshShader(int subMeshIndex, const char *fxName, int technique,
	const std::vector<W3XShaderConstant> &constants)
{
	if (subMeshIndex < 0 || subMeshIndex >= (int)m_meshes.size()) return;
	SubMesh &sm = m_meshes[subMeshIndex];
	sm.fxName = fxName ? fxName : "";
	sm.technique = technique;
	sm.constants = constants;
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
// BindW3XConstants: apply a list of W3XShaderConstant to an effect.
// Shared by the model-wide shader and per-sub-mesh shader overrides.
//=============================================================================
static void BindW3XConstants(ID3DXEffect *effect, const std::vector<W3XShaderConstant> &constants)
{
	for (size_t ci = 0; ci < constants.size(); ci++) {
		const W3XShaderConstant &c = constants[ci];
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
				AsciiString dds = ResolveTexturePathCached(c.textureValue.str());
				if (!dds.isEmpty()) {
					TextureClass *tex = WW3DAssetManager::Get_Instance()->Get_Texture(dds.str(), MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, true);
					if (tex && tex->Peek_D3D_Texture()) {
						IDirect3DTexture9 *d3dt = static_cast<IDirect3DTexture9*>(tex->Peek_D3D_Texture());
						HRESULT hrt = effect->SetTexture(h, d3dt);
						if (FAILED(hrt)) DEBUG_LOG(("[W3X_P5]   TEXTURE '%s': SetTexture FAILED hr=0x%08X\n", c.name.str(), (int)hrt));
					} else {
						DEBUG_LOG(("[W3X_P5]   TEXTURE '%s': Get_Texture FAILED (tex=%p)\n", c.name.str(), (void*)tex));
					}
				}
				break;
			}
			default: break;
		}
	}
}

//=============================================================================
// Texture name lookup helper: find a sub-mesh's OWN texture constant value
// (e.g. "DiffuseTexture" -> "TavBtMstr2"). Returns NULL if the sub-mesh's
// constants don't name that parameter (then the effect inherits the previous
// binding — that's exactly what the per-sub-mesh diagnostic wants to reveal).
//=============================================================================
static const char *W3XConstantTextureName(const std::vector<W3XShaderConstant> &constants, const char *paramName)
{
	for (size_t i = 0; i < constants.size(); i++) {
		if (constants[i].type == W3X_CONSTANT_TEXTURE
			&& constants[i].name.compare(paramName) == 0) {
			return constants[i].textureValue.str();
		}
	}
	return NULL;
}

//=============================================================================
// BindW3XMatrices: bind the shared object->world->clip matrices plus the
// ShadowMapWorldToShadow identity and the RA3 RecolorColor faction color.
// Called for BOTH the model-wide effect and any per-sub-mesh override effect,
// since each D3DXEffect instance keeps its own parameter values.
//=============================================================================
static void BindW3XMatrices(ID3DXEffect *effect, const Matrix4x4 &world,
	const Matrix4x4 &vp, const Matrix4x4 &wvp, const Matrix4x4 &shadowW2S, unsigned int recolorHex)
{
	D3DXHANDLE hWorld = effect->GetParameterByName(NULL, "World");
	if (hWorld) effect->SetMatrix(hWorld, (const D3DXMATRIX*)&world);
	D3DXHANDLE hVP = effect->GetParameterByName(NULL, "ViewProjection");
	if (hVP) effect->SetMatrix(hVP, (const D3DXMATRIX*)&vp);
	D3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");
	if (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);

	// ShadowMapWorldToShadow — bind identity so the skinned VS's ShadowCS
	// output isn't garbage/NaN (which could cause the draw to be rejected).
	D3DXHANDLE hShadowW2S = effect->GetParameterByName(NULL, "ShadowMapWorldToShadow");
	if (hShadowW2S) effect->SetMatrix(hShadowW2S, (const D3DXMATRIX*)&shadowW2S);

	// Faction color (RA3 RecolorColor) from the owning player's team color.
	D3DXHANDLE hRecolor = effect->GetParameterByName(NULL, "RecolorColor");
	if (hRecolor) {
		unsigned int rc = recolorHex ? recolorHex : 0xFFFFFFFF;	// 0xFFRRGGBB, fall back to white
		float rcv[4] = {
			(float)((rc >> 16) & 0xFF) / 255.0f,
			(float)((rc >> 8)  & 0xFF) / 255.0f,
			(float)( rc        & 0xFF) / 255.0f,
			1.0f
		};
		effect->SetVector(hRecolor, (const D3DXVECTOR4*)rcv);
	}
}

//=============================================================================
// BindW3XBones: upload the RA3 WorldBones array (object-local -> world space)
// plus NumJointsPerVertex to the given effect. Shared by the model-wide effect
// and per-sub-mesh override effects.
//=============================================================================
static void BindW3XBones(ID3DXEffect *effect, float *bones, int boneCount, const Matrix3D &worldTransform)
{
	if (boneCount > 0 && bones) {
		D3DXHANDLE hBones = effect->GetParameterByName(NULL, "WorldBones");
		if (hBones) {
			// The loaded bones are in OBJECT-LOCAL space. The RA3 skinned VS
			// (VS_H_11skin) transforms the vertex by the bone and then by
			// ViewProjection (world->clip) with NO World matrix — so WorldBones
			// must be in WORLD space. Compose the object's world transform.
			float wq[4];
			W3XMatrix3DToQuat(worldTransform, wq);
			Vector3 wtrans = worldTransform.Get_Translation();
			float wb[64 * 8];	// enough for up to 64 bones
			for (int bi = 0; bi < boneCount && bi < 64; bi++) {
				const float *bq = &bones[bi*8 + 0];
				const float *bo = &bones[bi*8 + 4];
				float *wo = &wb[bi*8];
				W3XQuatMultiply(&wo[0], wq, bq);				// worldRot = W_rot * boneRot
				W3XQuatRotateVector(&wo[4], wq, bo);			// rotate offset by W_rot
				wo[4] += wtrans.X; wo[5] += wtrans.Y; wo[6] += wtrans.Z;	// + W_trans
				wo[7] = bo[3];									// alpha
			}
			effect->SetFloatArray(hBones, wb, boneCount * 8);
		} else {
			DEBUG_LOG(("[W3X_P5]   WorldBones param NOT FOUND (model has %d bones)\n", boneCount));
		}
	}

	// Set NumJointsPerVertex so the RA3 VSchooser() picks the skinned vertex
	// shader (1 = hard skin). The model has bones, so skin it.
	{
		D3DXHANDLE hJoints = effect->GetParameterByName(NULL, "NumJointsPerVertex");
		if (hJoints) {
			int joints = boneCount > 0 ? 1 : 0;
			effect->SetInt(hJoints, joints);
		}
	}
}


//=============================================================================
// Render: draw all sub-meshes using D3DXEffect.
// This is called by the scene during the correct render pass.
//=============================================================================
void W3XRenderObjClass::Render(RenderInfoClass &rinfo)
{
	if (!m_valid || m_meshes.empty()) return;

	// PASS DIAGNOSTIC (one-shot, first 8 calls): confirms the W3X is invoked
	// from which scene passes. g_gbufferActive=1 => the deferred G-Buffer pass,
	// g_gbufferActive=0 => the Forward pass (and any others). CWE=0 => the
	// shadow-map pass (skipped below). This verifies the "renders twice per
	// frame (G-Buffer + Forward), both writing the same main depth buffer"
	// root cause of the wheel-covered-by-track depth artefact.
	{
		static int s_passDiag = 0;
		if (s_passDiag < 8) {
			s_passDiag++;
			DWORD cwe = 0, zen = 0, zwr = 0, zfn = 0;
			IDirect3DDevice9 *pd9 = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
			if (pd9) {
				pd9->GetRenderState(D3DRS_COLORWRITEENABLE, &cwe);
				pd9->GetRenderState(D3DRS_ZENABLE, &zen);
				pd9->GetRenderState(D3DRS_ZWRITEENABLE, &zwr);
				pd9->GetRenderState(D3DRS_ZFUNC, &zfn);
			}
			DEBUG_LOG(("[W3X_PASS] Render #%d gbuf=%d CWE=0x%08X ZEN=%u ZWR=%u ZFN=%u\n",
				s_passDiag, g_gbufferActive ? 1 : 0, (unsigned)cwe, zen, zwr, zfn));
		}
	}

	// SKIP the deferred G-Buffer pass. The W3X is a forward PBR renderer; drawing
	// it in the G-Buffer pass writes its PBR color into RT0 (albedo) while RT1
	// (normal) and RT2 (depth) stay black, AND writes its depth into the main
	// depth buffer. The Forward pass then depth-tests (LESS) against that SAME
	// self-written depth — equal depth fails strict LESS, so every sub-mesh is
	// either rejected or z-fights (the track, drawn last, wins over the wheel).
	// Rendering ONLY in the Forward pass gives the W3X a clean depth to test and
	// write, restoring correct wheel/track occlusion. The shadow-map pass is
	// already skipped via the COLORWRITEENABLE==0 check below.
	if (g_gbufferActive) return;

	// GEOMETRY PROBE (one-shot): report each sub-mesh's SKINNED MODEL-SPACE AABB
	// (each vertex transformed through its bone's quat+offset — same math the
	// RA3 skinned VS applies). This separates "the track genuinely wraps OUTSIDE
	// the wheels (covering the wheel's outer face is geometric, not a depth bug)"
	// from "the wheels stick out beyond the track (any covering is a depth/pass
	// failure)". Compare sub-mesh[5]=WHEEL vs sub-mesh[6]=TREAD (this model):
	// the OUTER-face direction is the model's Y axis. track|Y|max > wheel|Y|max
	// => track wider, covering expected; wheel|Y|max > track|Y|max => wheel
	// sticks out and must be visible. W3XVertex layout: x,y,z@0 (floats 0-2),
	// ... boneIdx@56 (float index 14); stride 76 bytes = 19 floats.
	if (m_bones && m_boneCount > 0)
	{
		static bool s_geomProbe = false;
		if (!s_geomProbe) {
			s_geomProbe = true;
			int maxBone = m_boneCount < 64 ? m_boneCount : 64;
			for (size_t si = 0; si < m_meshes.size(); si++) {
				SubMesh &sm = m_meshes[si];
				if (!sm.vb || sm.vertexCount <= 0) continue;
				void *ptr = NULL;
				if (FAILED(sm.vb->Lock(0, 0, &ptr, D3DLOCK_READONLY))) continue;
				const float *f = (const float *)ptr;
				const int stride = 19;	// 76-byte W3XVertex
				float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
				for (int vi = 0; vi < sm.vertexCount; vi++) {
					const float *v = f + vi * stride;
					int bi = (int)v[14];	// boneIdx
					if (bi < 0 || bi >= maxBone) bi = 0;
					float wp[3];
					W3XQuatRotateVector(wp, &m_bones[bi * 8], v);
					wp[0] += m_bones[bi * 8 + 4];
					wp[1] += m_bones[bi * 8 + 5];
					wp[2] += m_bones[bi * 8 + 6];
					for (int a = 0; a < 3; a++) {
						if (wp[a] < mn[a]) mn[a] = wp[a];
						if (wp[a] > mx[a]) mx[a] = wp[a];
					}
				}
				sm.vb->Unlock();
				DEBUG_LOG(("[W3X_GEOM] submesh[%d] fx='%s' modelAABB X=%.2f..%.2f Y=%.2f..%.2f Z=%.2f..%.2f size=%.2fx%.2fx%.2f\n",
					(int)si, sm.fxName.isEmpty() ? "shared" : sm.fxName.str(),
					mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
					mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]));
			}
		}
	}

	ID3DXEffect *effect = W3XEffectManager::Instance()->GetEffect(m_fxName.str());
	if (!effect) { DEBUG_LOG(("[W3X_P5]   No effect '%s'\n", m_fxName.str())); return; }

	// =====================================================================
	// RUNTIME SHADER-EFFECT DIAGNOSTICS (one-shot per instance)
	// Confirms which shader/technique is actually bound at draw time and
	// whether each sub-mesh can produce bump-normal output (needs tangent).
	// =====================================================================
	{
		static bool w3xRtDiag = false;
		if (!w3xRtDiag) {
			w3xRtDiag = true;
			DEBUG_LOG(("[W3X_DIAG] RUNTIME '%s' effect=%p technique=%d submeshes=%d\n",
				m_fxName.str(), (void*)effect, m_technique, (int)m_meshes.size()));
			// Does the effect expose the RA3 normal-map sampler?
			{
				D3DXHANDLE hDiff = effect->GetParameterByName(NULL, "DiffuseTexture");
				D3DXHANDLE hNrm = effect->GetParameterByName(NULL, "NormalMap");
				D3DXHANDLE hSpc = effect->GetParameterByName(NULL, "SpecMap");
				DEBUG_LOG(("[W3X_DIAG]   samplers: DiffuseTexture=%p NormalMap=%p SpecMap=%p\n",
					(void*)hDiff, (void*)hNrm, (void*)hSpc));
				(void)hDiff; (void)hNrm; (void)hSpc;	// keep C4189 quiet when DEBUG_LOG is compiled out
			}
			// Per-submesh tangent availability -> bump-normal viability
			for (size_t si = 0; si < m_meshes.size(); si++) {
				const SubMesh &sm = m_meshes[si];
				DEBUG_LOG(("[W3X_DIAG]   submesh[%d] verts=%d tris=%d tangent=%d binormal=%d -> bump-normal %s\n",
					(int)si, sm.vertexCount, sm.triangleCount,
					sm.hasTangents ? 1 : 0, sm.hasBinormals ? 1 : 0,
					(sm.hasTangents && sm.hasBinormals) ? "ACTIVE" : "MISSING (normal map inert on this mesh)"));
				(void)sm;	// keep C4189 quiet when DEBUG_LOG is compiled out
			}
		}
	}

	// Select technique FIRST (D3DX9 pattern): SetTechnique before setting any
	// parameters, otherwise it can reset textures/matrices set beforehand.
	{
		D3DXHANDLE hTech = effect->GetTechnique(m_technique);
		if (hTech) {
			effect->SetTechnique(hTech);
			// Validate the technique against the device (SM3.0 caps, etc.)
			effect->ValidateTechnique(hTech);
		} else {
			DEBUG_LOG(("[W3X_P5]   Technique %d NOT FOUND\n", m_technique));
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
	Matrix4x4 proj;
	{
		DWORD smCWE = 0;
		IDirect3DDevice9 *smDev = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
		if (smDev) smDev->GetRenderState(D3DRS_COLORWRITEENABLE, &smCWE);
		if (smCWE == 0) {
			// Deferred shadow-map pass: the W3X uses the W3D volumetric soft shadow
			// (SHADOW_VOLUME), NOT the deferred shadow map. Rendering into the shadow
			// pass here forced device states (DS=NULL, ZEnable=0, viewport 2048,
			// scissor/stencil/alpha off) that LEAKED to the next objects
			// (buildings/dozer), corrupting their shadows and crashing on some
			// (War Factory). Skip the pass entirely.
			return;
		}
		view.Init(rinfo.Camera.Get_View_Matrix());
		rinfo.Camera.Get_D3D_Projection_Matrix(&proj);
		view = view.Transpose();	// engine column-major -> D3D row-major
		proj = proj.Transpose();
	}
	Matrix4x4 vp = Multiply(view, proj);
	Matrix4x4 wvp = Multiply(world, vp);
	// The RA3 skinned VS writes ShadowCS from ShadowMapWorldToShadow. The W3X no
	// longer samples a deferred shadow map (it casts the W3D volumetric soft shadow
	// instead), so bind identity — a non-degenerate matrix keeps the VS output
	// finite (a garbage/NaN ShadowCS could reject the draw).
	Matrix4x4 shadowW2S;
	shadowW2S.Make_Identity();

	// RA3 shaders use World (object→world) and ViewProjection (world→clip);
	// some shaders take WorldViewProj directly. Set whichever the effect
	// exposes, overriding the DX8Wrapper-based bindings (which return
	// identity for World in this render context).
	BindW3XMatrices(effect, world, vp, wvp, shadowW2S, m_recolorHex);

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

	// Set per-instance constants
	BindW3XConstants(effect, m_constants);

	// Upload bones (WorldBones)
	BindW3XBones(effect, m_bones, m_boneCount, m_worldTransform);

	IDirect3DDevice9 *dev9 = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
	if (!dev9) return;

	// Shared solid-white texture for RA3 auxiliary samplers (ShroudTexture /
	// CloudTexture). A white 1x1 avoids black/NaN output when the real maps
	// aren't bound. Function-scope static so both the model-wide effect and
	// per-sub-mesh overrides can reference it.
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
	// Bind the RA3 auxiliary samplers (ShroudTexture / CloudTexture) to a shared
	// solid-white 1x1 so an unbound sampler never turns the model black.
	if (s_whiteTex) {
		const char *auxTex[] = { "ShroudTexture", "CloudTexture" };
		for (int ai = 0; ai < 2; ai++) {
			D3DXHANDLE hAux = effect->GetParameterByName(NULL, auxTex[ai]);
			if (hAux) effect->SetTexture(hAux, s_whiteTex);
		}
	}

	// Vertex declaration
	IDirect3DVertexDeclaration9 *decl = W3XGetVertexDecl(dev9);

	// Commit all pending parameter changes (textures, matrices, constants) to
	// the device. D3DX9 requires CommitChanges after SetX before drawing.
	effect->CommitChanges();

	// Main pass: ensure the Default technique (0) is active. The RA3 ShadowDepth
	// technique is no longer used — the W3X casts a volumetric soft shadow, not a
	// deferred shadow-map depth.
	D3DXHANDLE hTech = effect->GetTechnique(0);	// Default
	if (hTech) { effect->SetTechnique(hTech); effect->ValidateTechnique(hTech); }
	effect->CommitChanges();

	UINT totalFailed = 0;
	for (size_t si = 0; si < m_meshes.size(); si++) {
		SubMesh &sm = m_meshes[si];
		if (!sm.vb || !sm.ib) continue;

		// Per-sub-mesh shader override: a sub-mesh with its own fxName uses a
		// different effect (e.g. w3x_tread.fx for scrolling treads). Bind its
		// technique + all shared render params (matrices, engine constants,
		// bones, aux samplers, constants) — each D3DXEffect keeps its own values.
		ID3DXEffect *drawEffect = effect;
		if (!sm.fxName.isEmpty() && sm.fxName.compare(m_fxName.str()) != 0) {
			drawEffect = W3XEffectManager::Instance()->GetEffect(sm.fxName.str());
			if (drawEffect) {
				// Technique first, then bind all shared params.
				D3DXHANDLE hTech = drawEffect->GetTechnique(sm.technique);
				if (hTech) {
					drawEffect->SetTechnique(hTech);
					drawEffect->ValidateTechnique(hTech);
				}
				// Engine constants (sun/ambient/camera) from the real scene camera
				W3XEffectManager::Instance()->BindEngineConstants(drawEffect, rinfo);
				// Object->world->clip matrices + ShadowMapWorldToShadow + RecolorColor
				BindW3XMatrices(drawEffect, world, vp, wvp, shadowW2S, m_recolorHex);
				// This sub-mesh's shader constants (textures etc.)
				BindW3XConstants(drawEffect, sm.constants);
				// WorldBones + NumJointsPerVertex for this effect (skinned mesh)
				BindW3XBones(drawEffect, m_bones, m_boneCount, m_worldTransform);
				// Bind the auxiliary samplers for this effect too (Shroud/Cloud ->
				// shared white fallback). The RA3 ShadowMap sampler is NOT bound:
				// the W3X casts a volumetric soft shadow, not a deferred shadow-map.
				{
					const char *auxTex[] = { "ShroudTexture", "CloudTexture" };
					for (int ai = 0; ai < 2; ai++) {
						D3DXHANDLE hAux = drawEffect->GetParameterByName(NULL, auxTex[ai]);
						if (hAux) drawEffect->SetTexture(hAux, s_whiteTex);
					}
				}
			} else {
				DEBUG_LOG(("[W3X_P5]   submesh[%d] override shader '%s' NOT FOUND\n", (int)si, sm.fxName.str()));
			}
		}
		if (!drawEffect) { totalFailed++; continue; }

		// Per-sub-mesh texture bindings for the SHARED effect: the model-wide
		// bind (first PBR sub-mesh's DiffuseTexture/NormalMap/SpecMap, usually
		// TavGattTank2) leaks to sub-meshes with their OWN textures (UP04 gun ->
		// TavBtMstr2, SP01 -> TunLight02), making those parts render with the
		// body texture -> misaligned camo. Rebind each sub-mesh's constants.
		if (drawEffect == effect) {
			BindW3XConstants(drawEffect, sm.constants);
		}

		// DIAG (one-shot per sub-mesh): which DiffuseTexture does this sub-mesh
		// actually bind — its own constant or the inherited model-wide one? This
		// verifies the per-sub-mesh rebind above (and flags sub-meshes that still
		// inherit, e.g. SP01 whose defaultw3d constants don't name DiffuseTexture).
		{
			static bool s_texDiag[64] = { false };
			if (si < 64 && !s_texDiag[si]) {
				s_texDiag[si] = true;
				const char *ownD = W3XConstantTextureName(sm.constants, "DiffuseTexture");
				const char *ownN = W3XConstantTextureName(sm.constants, "NormalMap");
				const char *ownS = W3XConstantTextureName(sm.constants, "SpecMap");
				IDirect3DBaseTexture9 *bound = NULL;
				D3DXHANDLE hDiff = drawEffect->GetParameterByName(NULL, "DiffuseTexture");
				if (hDiff) drawEffect->GetTexture(hDiff, &bound);
				DEBUG_LOG(("[W3X_DIAG] submesh[%d] fx='%s' ownD='%s' ownN='%s' ownS='%s' diffParam=%s bound=%p%s\n",
					(int)si, sm.fxName.str(),
					ownD ? ownD : "(inherit)", ownN ? ownN : "(inherit)", ownS ? ownS : "(inherit)",
					hDiff ? "YES" : "NO", (void*)bound,
					(ownD && !bound) ? "  <-- OWN TEXTURE NOT BOUND" : ""));
				(void)ownD; (void)ownN; (void)ownS; (void)bound;	// keep C4189 quiet when DEBUG_LOG is compiled out
			}
		}

		dev9->SetStreamSource(0, sm.vb, 0, 76);	// W3XVertex stride (76 with COLOR + TEXCOORD1)
		dev9->SetIndices(sm.ib);
		if (decl) dev9->SetVertexDeclaration(decl);

		UINT passes;
		HRESULT hr = drawEffect->Begin(&passes, 0);
		if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] Begin FAILED hr=0x%08X\n", (int)si, (int)hr)); totalFailed++; continue; }
		for (UINT p = 0; p < passes; p++) {
			hr = drawEffect->BeginPass(p);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] BeginPass(%u) FAILED hr=0x%08X\n", (int)si, p, (int)hr)); break; }
			// CRITICAL FIX: re-bind vertex data AFTER BeginPass. D3DX9 Begin/BeginPass
			// may reset the stream source / declaration / index buffer to defaults,
			// leaving the draw with no valid vertex data -> 0 pixels.
			dev9->SetStreamSource(0, sm.vb, 0, 76);
			dev9->SetIndices(sm.ib);
			if (decl) dev9->SetVertexDeclaration(decl);
			// W3X triangles are CW; the RA3 technique sets CullMode=2 (culls CW),
			// so disable culling AFTER BeginPass (BeginPass re-applies the pass
			// render states and would otherwise undo this).
			dev9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			// DRAW-STATE DIAG (one-shot, first draw): the device states the pass
			// ACTUALLY applied at the draw moment. ZEN=1 (test on) ZWR=1 (write on)
			// ZFN=4 (LESS) CULL=1 (NONE) is the expected good state. ZEN=0/ZWR=0
			// here means the effect's technique pass states are not reaching the
			// device, so every sub-mesh submits in order with no depth separation —
			// the track (drawn last) then wins over the wheel regardless of depth.
			{
				// DIAG: RT/DS on the first few draws. The W3X is rendered from multiple
				// scene passes (aux reflection/env pass at 256x256, then the main
				// Forward pass at the back-buffer size). Capturing several draws shows
				// WHICH RT/DS each pass uses — the main-pass depth is what the
				// volumetric shadow volume depth-tests against (self-shadow needs the
				// hull depth in the same DS the shadow pass reads).
				static int s_drawDiagCount = 0;
				if (s_drawDiagCount < 6) {
					s_drawDiagCount++;
					DWORD zen = 0, zwr = 0, zfn = 0, cul = 0;
					dev9->GetRenderState(D3DRS_ZENABLE, &zen);
					dev9->GetRenderState(D3DRS_ZWRITEENABLE, &zwr);
					dev9->GetRenderState(D3DRS_ZFUNC, &zfn);
					dev9->GetRenderState(D3DRS_CULLMODE, &cul);
					IDirect3DSurface9 *drawDS = NULL;
					IDirect3DSurface9 *drawRT = NULL;
					D3DSURFACE_DESC dsd, rtd;
					bool hasDS = SUCCEEDED(dev9->GetDepthStencilSurface(&drawDS)) && drawDS;
					bool hasRT = SUCCEEDED(dev9->GetRenderTarget(0, &drawRT)) && drawRT;
					if (hasDS) { drawDS->GetDesc(&dsd); }
					if (hasRT) { drawRT->GetDesc(&rtd); }
					DEBUG_LOG(("[W3X_DRAW#%d] DS=%p %ux%u fmt=0x%X | RT0=%p %ux%u fmt=0x%X | gbuf=%d fx='%s' ZEN=%u ZWR=%u ZFN=%u CULL=%u\n",
						s_drawDiagCount,
						(void*)drawDS, hasDS ? dsd.Width : 0, hasDS ? dsd.Height : 0, hasDS ? dsd.Format : 0,
						(void*)drawRT, hasRT ? rtd.Width : 0, hasRT ? rtd.Height : 0, hasRT ? rtd.Format : 0,
						g_gbufferActive ? 1 : 0,
						(drawEffect == effect) ? m_fxName.str() : sm.fxName.str(),
						(unsigned)zen, (unsigned)zwr, (unsigned)zfn, (unsigned)cul));
					if (drawDS) drawDS->Release();
					if (drawRT) drawRT->Release();
				}
			}
			hr = dev9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sm.vertexCount, 0, sm.triangleCount);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] DrawIndexedPrimitive FAILED hr=0x%08X (vc=%d tc=%d)\n", (int)si, (int)hr, sm.vertexCount, sm.triangleCount)); totalFailed++; }
			drawEffect->EndPass();
		}
		drawEffect->End();
	}

	// NOTE: do NOT reset device state here. The W3X render uses the raw D3D9
	// device directly (bypassing DX8Wrapper's state cache); resetting state
	// here desyncs DX8Wrapper and breaks subsequent regular renders.

	if (totalFailed > 0) {
		DEBUG_LOG(("[W3X_P5]   %u submesh(es) FAILED (of %d)\n", totalFailed, (int)m_meshes.size()));
	}
}
