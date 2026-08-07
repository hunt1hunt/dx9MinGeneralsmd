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
			}
			// Per-submesh tangent availability -> bump-normal viability
			for (size_t si = 0; si < m_meshes.size(); si++) {
				const SubMesh &sm = m_meshes[si];
				DEBUG_LOG(("[W3X_DIAG]   submesh[%d] verts=%d tris=%d tangent=%d binormal=%d -> bump-normal %s\n",
					(int)si, sm.vertexCount, sm.triangleCount,
					sm.hasTangents ? 1 : 0, sm.hasBinormals ? 1 : 0,
					(sm.hasTangents && sm.hasBinormals) ? "ACTIVE" : "MISSING (normal map inert on this mesh)"));
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
	bool isShadowMapPass = false;	// true while rendering into the shadow-map pass
	Matrix4x4 view;
	Matrix4x4 proj;
	{
		DWORD smCWE = 0;
		IDirect3DDevice9 *smDev = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
		if (smDev) smDev->GetRenderState(D3DRS_COLORWRITEENABLE, &smCWE);
		if (smCWE == 0) {
			isShadowMapPass = true;
			// SKIP the deferred shadow map entirely: the W3X now uses the W3D
			// volumetric soft shadow (SHADOW_VOLUME), NOT the deferred shadow map.
			// Rendering into the shadow pass here forced device states (DS=NULL,
			// ZEnable=0, viewport 2048, scissor/stencil/alpha off) that LEAKED to
			// the next objects (buildings/dozer), corrupting their shadows and
			// crashing on some (War Factory). Return early so the shadow pass is a
			// no-op for the W3X.
			return;
			// Shadow-map pass: read the sun camera matrices directly from the
			// renderer (stored by beginShadowMapPass). The DX8Wrapper transform
			// stack is unreliable here — Get_Transform(D3DTS_VIEW) returned a zero
			// matrix (other renderables clobber render_state.view before this draw),
			// which collapsed every vertex to the origin and emptied the shadow map.
			if (g_theW3DDeferredRenderer) {
				// getShadowView/getShadowProj return D3DXMATRIX (ROW-major) as a
				// Matrix4x4, and the RA3 shaders use the row-vector convention — so
				// use them DIRECTLY, do NOT transpose. (The engine's own column-major
				// camera matrices below DO need the transpose.) Transposing these
				// produced a garbage sun VP whose scale was dominated by the view
				// translation (e.g. shadowW2S[0] ~ 281 = 0.5 * tx), pushing the
				// shadow-pass geometry off-screen and leaving the shadow-map color RT
				// empty (SHADOWDBG R-min = 1.0).
				view = g_theW3DDeferredRenderer->getShadowView();	// D3DX row-major: no transpose
				proj = g_theW3DDeferredRenderer->getShadowProj();	// D3DX row-major: no transpose
			} else {
				DX8Wrapper::Get_Transform(D3DTS_VIEW, view);
				DX8Wrapper::Get_Transform(D3DTS_PROJECTION, proj);
				view = view.Transpose();	// engine transforms -> D3D row-major
				proj = proj.Transpose();
			}
			static bool s_smPassDiag = false;
			if (!s_smPassDiag) {
				s_smPassDiag = true;
				DEBUG_LOG(("[W3X_DIAG] W3X rendering into SHADOW MAP pass (smCWE==0)\n"));
				DEBUG_LOG(("[W3X_DIAG] sunV[0]=%.3f,%.3f,%.3f,%.3f [3]=%.3f,%.3f,%.3f,%.3f\n",
					view[0].X, view[0].Y, view[0].Z, view[0].W,
					view[3].X, view[3].Y, view[3].Z, view[3].W));
				DEBUG_LOG(("[W3X_DIAG] sunP[0]=%.3f,%.3f,%.3f,%.3f [3]=%.3f,%.3f,%.3f,%.3f\n",
					proj[0].X, proj[0].Y, proj[0].Z, proj[0].W,
					proj[3].X, proj[3].Y, proj[3].Z, proj[3].W));
			}
		} else {
			view.Init(rinfo.Camera.Get_View_Matrix());
			rinfo.Camera.Get_D3D_Projection_Matrix(&proj);
			view = view.Transpose();	// engine column-major -> D3D row-major
			proj = proj.Transpose();
		}
	}
	// NOTE: the shadow matrices are D3DX row-major (used directly above); the main
	// camera matrices were transposed in the else branch. No global transpose here.
	Matrix4x4 vp = Multiply(view, proj);
	Matrix4x4 wvp = Multiply(world, vp);
	// DIAG (one-shot): is the model inside the 1000x1000 shadow ortho frustum?
	// Project the object's world position through the sun VP; if NDC is outside
	// [-1,1]x[-1,1]x[0,1] the model never draws into the shadow map -> empty.
	{
		static bool s_frustumDiag = false;
		if (isShadowMapPass && !s_frustumDiag) {
			s_frustumDiag = true;
			Vector3 wp = m_worldTransform.Get_Translation();
			float cx = wp.X*vp[0].X + wp.Y*vp[1].X + wp.Z*vp[2].X + vp[3].X;
			float cy = wp.X*vp[0].Y + wp.Y*vp[1].Y + wp.Z*vp[2].Y + vp[3].Y;
			float cz = wp.X*vp[0].Z + wp.Y*vp[1].Z + wp.Z*vp[2].Z + vp[3].Z;
			float cw = wp.X*vp[0].W + wp.Y*vp[1].W + wp.Z*vp[2].W + vp[3].W;
			float iw = (cw != 0.0f) ? 1.0f / cw : 1.0f;
			float nx = cx * iw, ny = cy * iw, nz = cz * iw;
			bool in = (nx >= -1.0f && nx <= 1.0f && ny >= -1.0f && ny <= 1.0f && nz >= 0.0f && nz <= 1.0f);
			DEBUG_LOG(("[W3X_DIAG] shadowFrustum worldPos=(%.1f,%.1f,%.1f) NDC=(%.3f,%.3f,%.3f) inFrustum=%s\n",
				wp.X, wp.Y, wp.Z, nx, ny, nz, in ? "YES" : "NO"));
		}
	}
	// ShadowMapWorldToShadow = sun viewProj * NDC->UV bias.
	// IMPORTANT: the shadow-map depth pass renders W3X with ViewProjection =
	// V_sun^T * P_sun^T (same transpose the main camera uses). The sampling
	// matrix MUST be built from the SAME transposed sun VP, otherwise world->shadow
	// UV won't line up with where depth was written -> no visible shadow.
	Matrix4x4 shadowW2S;
	shadowW2S.Make_Identity();
	if (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()) {
		Matrix4x4 sv = g_theW3DDeferredRenderer->getShadowView();	// D3DX row-major sun view
		Matrix4x4 sp = g_theW3DDeferredRenderer->getShadowProj();	// D3DX row-major sun proj
		// sv/sp are D3DX row-major; the RA3 shader is row-vector, so the world->shadow
		// UV matrix is simply V * P * bias (NO transpose). Transposing here produced
		// values dominated by the view translation (e.g. row0.x ~ 281 = 0.5 * tx),
		// which maps every world point to UV way outside [0,1] -> no shadow.
		Matrix4x4 svp = Multiply(sv, sp);	// V * P (row-major)
		Matrix4x4 bias;
		bias.Make_Identity();
		bias[0].X = 0.5f;
		bias[1].Y = -0.5f;	// D3D: NDC y=+1 (top) -> texture v=0, so negate Y
		bias[3].X = 0.5f;
		bias[3].Y = 0.5f;
		shadowW2S = Multiply(svp, bias);
		// DIAG (one-shot, only after a real shadow pass so the matrix is initialized)
		static bool s_w2sDiag = false;
		const Matrix4x4 &rawSVP = g_theW3DDeferredRenderer->getShadowViewProj();
		if (!s_w2sDiag && (fabsf(rawSVP[0].X) > 1e-6f || fabsf(rawSVP[0].Y) > 1e-6f || fabsf(rawSVP[0].Z) > 1e-6f)) {
			s_w2sDiag = true;
			DEBUG_LOG(("[W3X_DIAG] shadowW2S full matrix (V P bias, row-major):\n"));
			DEBUG_LOG(("[W3X_DIAG]   [0] %.3f %.3f %.3f %.3f\n",
				shadowW2S[0].X, shadowW2S[0].Y, shadowW2S[0].Z, shadowW2S[0].W));
			DEBUG_LOG(("[W3X_DIAG]   [1] %.3f %.3f %.3f %.3f\n",
				shadowW2S[1].X, shadowW2S[1].Y, shadowW2S[1].Z, shadowW2S[1].W));
			DEBUG_LOG(("[W3X_DIAG]   [2] %.3f %.3f %.3f %.3f\n",
				shadowW2S[2].X, shadowW2S[2].Y, shadowW2S[2].Z, shadowW2S[2].W));
			DEBUG_LOG(("[W3X_DIAG]   [3] %.3f %.3f %.3f %.3f\n",
				shadowW2S[3].X, shadowW2S[3].Y, shadowW2S[3].Z, shadowW2S[3].W));
		}

		// =====================================================================
		// COMPREHENSIVE one-shot shadow-pass diagnostic: answers, in ONE run,
		// whether the model is in the sun frustum and where it lands in the map.
		// =====================================================================
		{
			static bool s_shadowFullDiag = false;
			if (isShadowMapPass && !s_shadowFullDiag) {
				s_shadowFullDiag = true;
				Vector3 wp = m_worldTransform.Get_Translation();
				// (a) NDC via sun VP -> frustum membership
				float cx = wp.X*vp[0].X + wp.Y*vp[1].X + wp.Z*vp[2].X + vp[3].X;
				float cy = wp.X*vp[0].Y + wp.Y*vp[1].Y + wp.Z*vp[2].Y + vp[3].Y;
				float cz = wp.X*vp[0].Z + wp.Y*vp[1].Z + wp.Z*vp[2].Z + vp[3].Z;
				float cw = wp.X*vp[0].W + wp.Y*vp[1].W + wp.Z*vp[2].W + vp[3].W;
				float iw = (cw != 0.0f) ? 1.0f / cw : 1.0f;
				float nx = cx*iw, ny = cy*iw, nz = cz*iw;
				bool inF = (nx >= -1.0f && nx <= 1.0f && ny >= -1.0f && ny <= 1.0f && nz >= 0.0f && nz <= 1.0f);
				// (b) shadow UV+depth via shadowW2S -> would it land inside the map?
				float sx = wp.X*shadowW2S[0].X + wp.Y*shadowW2S[1].X + wp.Z*shadowW2S[2].X + shadowW2S[3].X;
				float sy = wp.X*shadowW2S[0].Y + wp.Y*shadowW2S[1].Y + wp.Z*shadowW2S[2].Y + shadowW2S[3].Y;
				float sz = wp.X*shadowW2S[0].Z + wp.Y*shadowW2S[1].Z + wp.Z*shadowW2S[2].Z + shadowW2S[3].Z;
				float sw = wp.X*shadowW2S[0].W + wp.Y*shadowW2S[1].W + wp.Z*shadowW2S[2].W + shadowW2S[3].W;
				float su = sx / (sw != 0.0f ? sw : 1.0f);
				float sv = sy / (sw != 0.0f ? sw : 1.0f);
				float sd = sz / (sw != 0.0f ? sw : 1.0f);
				bool inMap = (su >= 0.0f && su <= 1.0f && sv >= 0.0f && sv <= 1.0f);
				DEBUG_LOG(("[W3X_DIAG] shadowFull worldPos=(%.1f,%.1f,%.1f) NDC=(%.3f,%.3f,%.3f) inFrustum=%s"
					" shadowUV=(%.3f,%.3f) depth=%.3f inMap=%s\n",
					wp.X, wp.Y, wp.Z, nx, ny, nz, inF ? "YES" : "NO",
					su, sv, sd, inMap ? "YES" : "NO"));
			}
		}
	}

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
	// CloudTexture / ShadowMap). A white 1x1 avoids black/NaN output when the
	// real maps aren't bound. Function-scope static so both the model-wide
	// effect and per-sub-mesh overrides can reference it.
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
	// In the shadow-map pass the ShadowDepth technique doesn't sample any
	// textures; binding the color RT we're rendering into as a sampler is also
	// invalid, so skip all texture/parameter binding here.
	if (s_whiteTex && !isShadowMapPass) {
		IDirect3DTexture9 *realShadow = (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable())
			? g_theW3DDeferredRenderer->getShadowMapTexture() : NULL;
		D3DXHANDLE hShadowMap = effect->GetParameterByName(NULL, "ShadowMap");
		if (hShadowMap) effect->SetTexture(hShadowMap, realShadow ? realShadow : s_whiteTex);
		D3DXHANDLE hHasShadow = effect->GetParameterByName(NULL, "HasShadow");
		if (hHasShadow) effect->SetBool(hHasShadow, realShadow ? TRUE : FALSE);
		// RA3 shadow fn needs 1/mapSize for texel offsets; unbound it is 0 -> SMUV/0 = NaN.
		D3DXHANDLE hSM = NULL;
		if (realShadow) {
			hSM = effect->GetParameterByName(NULL, "Shadowmap_Zero_Zero_OneOverMapSize_OneOverMapSize");
			if (hSM) { float sm[4] = { 0, 0, 1.0f/2048.0f, 1.0f/2048.0f }; effect->SetVector(hSM, (const D3DXVECTOR4*)sm); }
		}
		const char *auxTex[] = { "ShroudTexture", "CloudTexture" };
		for (int ai = 0; ai < 2; ai++) {
			D3DXHANDLE hAux = effect->GetParameterByName(NULL, auxTex[ai]);
			if (hAux) effect->SetTexture(hAux, s_whiteTex);
		}
		// DIAG (one-shot): shadow-map bind state
		static bool s_shadowDiagOnce = false;
		if (!s_shadowDiagOnce) {
			s_shadowDiagOnce = true;
			DEBUG_LOG(("[W3X_DIAG] shadow bind: avail=%d realShadow=%p hShadowMap=%p hHasShadow=%p hSM=%p\n",
				(g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()) ? 1 : 0,
				(void*)realShadow, (void*)hShadowMap, (void*)hHasShadow, (void*)hSM));
		}
	}

	// Vertex declaration
	IDirect3DVertexDeclaration9 *decl = W3XGetVertexDecl(dev9);

	// Commit all pending parameter changes (textures, matrices, constants) to
	// the device. D3DX9 requires CommitChanges after SetX before drawing.
	effect->CommitChanges();

	// Shadow pass: write the sun-space depth to the color shadow-map RT via the
	// main effect's ShadowDepth technique (D16 depth textures aren't reliably
	// sampleable under dgVoodoo2), enabling color writes just for this pass.
	// Main pass: explicitly restore the Default technique (it may have been left
	// on ShadowDepth by the shadow-pass render).
	if (isShadowMapPass) {
		// w3x_soviet.fx technique order: 0=Default, 1=ShadowDepth. The effect
		// interface here only exposes GetTechnique(UINT), so select by index.
		D3DXHANDLE hTech = effect->GetTechnique(1);
		if (hTech) { effect->SetTechnique(hTech); effect->ValidateTechnique(hTech); }
		else { DEBUG_LOG(("[W3X_DIAG] ShadowDepth technique NOT FOUND (w3x_soviet.fx not updated?)\n")); }
		if (dev9) {
			dev9->SetRenderState(D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);
		}
	} else {
		D3DXHANDLE hTech = effect->GetTechnique(0);	// Default
		if (hTech) { effect->SetTechnique(hTech); effect->ValidateTechnique(hTech); }
	}

	// D3DX9 pattern: SetTechnique FIRST, then CommitChanges so the newly selected
	// technique's pass states/parameter block are actually committed. The shadow
	// pass switches to ShadowDepth here; without a re-commit after the switch the
	// technique's states can be left stale, so the shadow pass may render the lit
	// Default pass into the shadow-map color RT (colors, not depth) -> the main
	// pass samples "depth" that is actually lighting -> comparison always "lit" ->
	// no visible shadow.
	effect->CommitChanges();

	// DIAG (one-shot): which technique is ACTIVE and the color-write state when
	// the shadow pass draws — confirms ShadowDepth (not Default) is used and that
	// the PS can actually write depth into the color RT.
	{
		static bool s_techDiag = false;
		if (!s_techDiag && isShadowMapPass) {
			s_techDiag = true;
			D3DXHANDLE hCur = effect->GetCurrentTechnique();
			D3DXTECHNIQUE_DESC td;
			AsciiString techName = "(unknown)";
			if (hCur && SUCCEEDED(effect->GetTechniqueDesc(hCur, &td))) techName = td.Name;
			DWORD cwe = 0xFFFFFFFF;
			if (dev9) dev9->GetRenderState(D3DRS_COLORWRITEENABLE, &cwe);
			DEBUG_LOG(("[W3X_DIAG] shadowTech active='%s' isShadowPass=%d COLORWRITEENABLE=0x%08X\n",
				techName.str(), isShadowMapPass ? 1 : 0, (unsigned)cwe));
		}
	}

	UINT totalFailed = 0;
	for (size_t si = 0; si < m_meshes.size(); si++) {
		SubMesh &sm = m_meshes[si];
		if (!sm.vb || !sm.ib) continue;

		// Per-sub-mesh shader override: a sub-mesh with its own fxName uses a
		// different effect (e.g. w3x_tread.fx for scrolling treads). Bind its
		// technique + all shared render params (matrices, engine constants,
		// bones, aux samplers, constants) — each D3DXEffect keeps its own values.
		ID3DXEffect *drawEffect = effect;
		if (!isShadowMapPass && !sm.fxName.isEmpty() && sm.fxName.compare(m_fxName.str()) != 0) {
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
				// Bind the auxiliary samplers for this effect too
				{
					IDirect3DTexture9 *realShadow = (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable())
						? g_theW3DDeferredRenderer->getShadowMapTexture() : NULL;
					D3DXHANDLE hShadowMap = drawEffect->GetParameterByName(NULL, "ShadowMap");
					if (hShadowMap) drawEffect->SetTexture(hShadowMap, realShadow ? realShadow : s_whiteTex);
					D3DXHANDLE hHasShadow = drawEffect->GetParameterByName(NULL, "HasShadow");
					if (hHasShadow) drawEffect->SetBool(hHasShadow, realShadow ? TRUE : FALSE);
					if (realShadow) {
						D3DXHANDLE hSM = drawEffect->GetParameterByName(NULL, "Shadowmap_Zero_Zero_OneOverMapSize_OneOverMapSize");
						if (hSM) { float sm[4] = { 0, 0, 1.0f/2048.0f, 1.0f/2048.0f }; drawEffect->SetVector(hSM, (const D3DXVECTOR4*)sm); }
					}
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
		// (Shadow pass: ShadowDepth technique samples no textures.)
		if (!isShadowMapPass && drawEffect == effect) {
			BindW3XConstants(drawEffect, sm.constants);
		}

		// DIAG (one-shot per sub-mesh): which DiffuseTexture does this sub-mesh
		// actually bind — its own constant or the inherited model-wide one? This
		// verifies the per-sub-mesh rebind above (and flags sub-meshes that still
		// inherit, e.g. SP01 whose defaultw3d constants don't name DiffuseTexture).
		{
			static bool s_texDiag[64] = { false };
			if (!isShadowMapPass && si < 64 && !s_texDiag[si]) {
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
			}
		}

		dev9->SetStreamSource(0, sm.vb, 0, 76);	// W3XVertex stride (76 with COLOR + TEXCOORD1)
		dev9->SetIndices(sm.ib);
		if (decl) dev9->SetVertexDeclaration(decl);

		UINT passes;
		HRESULT hr = drawEffect->Begin(&passes, 0);
		if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] Begin FAILED hr=0x%08X\n", (int)si, (int)hr)); totalFailed++; continue; }
		// DIAG (one-shot): how many passes does the shadow technique report? 0 =
		// technique failed validation -> nothing would be drawn into the shadow map.
		{
			static bool s_shadowPassDiag = false;
			if (isShadowMapPass && !s_shadowPassDiag) {
				s_shadowPassDiag = true;
				DEBUG_LOG(("[W3X_DIAG] shadowBegin submesh[%d] passes=%u\n", (int)si, passes));
			}
		}
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
			// SHADOW PASS: diagnose the FULL device state right before the draw —
			// render target, depth stencil, Z test, color write — because ANY of
			// these being wrong leaves the shadow-map color RT empty even though the
			// geometry is in the frustum (SHADOWDBG R-min=1.0). Force CWE + Z states
			// to fix the common cases, and log what was actually set (one-shot).
			if (isShadowMapPass) {
				static bool s_shadowStateDiag = false;
				// Render target: must be the shadow color RT (m_shadowDepthRT)
				IDirect3DSurface9 *curRT = NULL;
				dev9->GetRenderTarget(0, &curRT);
				// Depth stencil: must be the shadow D16
				IDirect3DSurface9 *curDS = NULL;
				dev9->GetDepthStencilSurface(&curDS);
				// Color write + Z states
				DWORD cwe = 0xFFFFFFFF, zEnable = 0, zFunc = 0;
				dev9->GetRenderState(D3DRS_COLORWRITEENABLE, &cwe);
				dev9->GetRenderState(D3DRS_ZENABLE, &zEnable);
				dev9->GetRenderState(D3DRS_ZFUNC, &zFunc);
				// CRITICAL MISSED LINKS: stencil + alpha tests. A prior object in the
				// shadow pass may have left STENCILENABLE or ALPHATESTENABLE on, which
				// rejects the W3X's fragments (stencil bits / alpha clip) -> 0 pixels
				// even with every other state correct.
				DWORD stencilEnable = 0, alphaTestEnable = 0;
				dev9->GetRenderState(D3DRS_STENCILENABLE, &stencilEnable);
				dev9->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTestEnable);
				// The shadow color RT surface for comparison (is the draw going to it?)
				// Must be the RENDER TARGET surface, not the sampler copy.
				IDirect3DSurface9 *shadowRTSurf = NULL;
				if (g_theW3DDeferredRenderer) {
					shadowRTSurf = g_theW3DDeferredRenderer->getShadowRTSurface();
				}
				if (!s_shadowStateDiag) {
					s_shadowStateDiag = true;
					// CRITICAL MISSED LINK: viewport + scissor. The shadow RT is 1024x1024
					// with a 1024x1024 viewport set by beginShadowMapPass — but if a prior
					// object or the D3DXEffect Begin left a WRONG viewport or an ACTIVE
					// scissor, the geometry gets clipped to 0 pixels even though every other
					// state (RT/CWE/Z/DS/VP) is correct. This has NEVER been checked.
					D3DVIEWPORT9 vpSt;
					memset(&vpSt, 0, sizeof(vpSt));
					dev9->GetViewport(&vpSt);
					RECT scRect;
					memset(&scRect, 0, sizeof(scRect));
					dev9->GetScissorRect(&scRect);
					DEBUG_LOG(("[W3X_DIAG] shadowViewport x=%d y=%d w=%d h=%d scissor=(%d,%d)-(%d,%d)\n",
						(int)vpSt.X, (int)vpSt.Y, (int)vpSt.Width, (int)vpSt.Height,
						(int)scRect.left, (int)scRect.top, (int)scRect.right, (int)scRect.bottom));
					DEBUG_LOG(("[W3X_DIAG] shadowDraw RT=%p shadowRTSurf=%p RT_MATCH=%s CWE=0x%08X ZEnable=%d ZFunc=%d DS=%p Stencil=%d AlphaTest=%d\n",
						(void*)curRT, (void*)shadowRTSurf,
						(curRT == shadowRTSurf) ? "YES" : "NO",
						(unsigned)cwe, (int)zEnable, (int)zFunc, (void*)curDS,
						(int)stencilEnable, (int)alphaTestEnable));
					// pixels=0 means o.Position is degenerate. Compare the effect's ACTUAL
					// ViewProjection against the local sun VP, and dump the first bone —
					// a mismatch (main-camera VP leaked in) or NaN bones is the root cause.
					D3DXMATRIX vpEff;
					bool vpGot = false;
					D3DXHANDLE hVP = effect->GetParameterByName(NULL, "ViewProjection");
					if (hVP && SUCCEEDED(effect->GetMatrix(hVP, &vpEff))) vpGot = true;
					DEBUG_LOG(("[W3X_DIAG] shadowVP local[0]=%.4f,%.4f,%.4f,%.4f effect[0]=%.4f,%.4f,%.4f,%.4f got=%d\n",
						vp[0].X, vp[0].Y, vp[0].Z, vp[0].W,
						vpGot ? vpEff._11 : 0, vpGot ? vpEff._12 : 0, vpGot ? vpEff._13 : 0, vpGot ? vpEff._14 : 0,
						vpGot ? 1 : 0));
					DEBUG_LOG(("[W3X_DIAG] shadowBones count=%d bone0=(%.3f,%.3f,%.3f,%.3f) bone1=(%.3f,%.3f,%.3f,%.3f)\n",
						m_boneCount,
						m_bones ? m_bones[0] : 0, m_bones ? m_bones[1] : 0, m_bones ? m_bones[2] : 0, m_bones ? m_bones[3] : 0,
						m_bones ? m_bones[4] : 0, m_bones ? m_bones[5] : 0, m_bones ? m_bones[6] : 0, m_bones ? m_bones[7] : 0));
					// DECISIVE: dump the UPLOADED WorldBones (what the VS skins with). If the
					// uploaded root bone's offset is (0,0,0) instead of the world translation
					// (2145,625,..), the vertices stay in MODEL space -> they project OUTSIDE
					// the sun frustum -> 0 pixels -> empty shadow map.
					D3DXHANDLE hWB = effect->GetParameterByName(NULL, "WorldBones");
					if (hWB) {
						float wb[8] = {0};
						if (SUCCEEDED(effect->GetValue(hWB, wb, 8 * sizeof(float)))) {
							DEBUG_LOG(("[W3X_DIAG] shadowWorldBones bone0 quat=(%.3f,%.3f,%.3f,%.3f) off=(%.1f,%.1f,%.1f)\n",
								wb[0], wb[1], wb[2], wb[3], wb[4], wb[5], wb[6]));
						}
					}
				}
				if (shadowRTSurf) shadowRTSurf->Release();
				// Force the states the depth-only shadow pass needs.
				if (cwe != (DWORD)(D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|
					D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA)) {
					dev9->SetRenderState(D3DRS_COLORWRITEENABLE,
						D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|
						D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);
				}
				// CRITICAL FIX: force the 2048x2048 viewport + disable any scissor. If a
				// prior object or the effect's Begin left a wrong viewport (e.g. 0x0 or the
				// 1920x1080 main viewport) or an ACTIVE scissor, the shadow geometry gets
				// clipped to 0 pixels even with all other states correct — the missed link.
				{
					D3DVIEWPORT9 vpShadow = { 0, 0, 2048, 2048, 0.0f, 1.0f };
					dev9->SetViewport(&vpShadow);
					dev9->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
					// CRITICAL FIX: force stencil + alpha tests OFF — a prior object may
					// have left them enabled, rejecting the shadow fragments (0 pixels).
					dev9->SetRenderState(D3DRS_STENCILENABLE, FALSE);
					dev9->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
				}
				// TEST/FIX: pixels=0 despite correct VP/bones/states => the depth test
				// rejects the humvee's fragments (a building's closer depth was written
				// to the D16 first, but buildings write D16-only, not COLOR RT -> the
				// COLOR RT stays empty). Disable the depth test so the W3X ALWAYS writes
				// its sun depth to the shadow color RT. If SHADOWDBG now shows content,
				// the depth-test rejection was the root cause.
				dev9->SetRenderState(D3DRS_ZENABLE, 0);
				// CRITICAL FIX (shader-level alpha clip): PS_H_ARPBR clips fragments when
				// the AlphaTestEnable UNIFORM is TRUE (if(AlphaTestEnable) clip(alpha-0.375)).
				// Forcing the DEVICE state D3DRS_ALPHATESTENABLE does NOT stop this shader
				// clip. If the Default technique leaked into the shadow pass, this clip
				// rejects every fragment -> 0 pixels. Force the uniform FALSE + commit.
				{
					// CRITICAL: re-select the ShadowDepth technique + force AlphaTestEnable
					// uniform FALSE + commit, immediately before the draw. If Begin is not
					// applying the set technique (Default leaks -> PS_H_ARPBR alpha clip),
					// re-selecting here + CommitChanges forces it.
					D3DXHANDLE hTech = effect->GetTechnique(1);	// ShadowDepth (index 1)
					if (hTech) { effect->SetTechnique(hTech); effect->ValidateTechnique(hTech); }
					D3DXHANDLE hAlpha = effect->GetParameterByName(NULL, "AlphaTestEnable");
					if (hAlpha) effect->SetBool(hAlpha, FALSE);
					// CRITICAL: ensure ShadowMapWorldToShadow (used by VS to compute
					// ShadowCS) is bound for the ShadowDepth technique too.
					if (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()) {
						D3DXHANDLE hW2S = effect->GetParameterByName(NULL, "ShadowMapWorldToShadow");
						if (hW2S) effect->SetMatrix(hW2S, (const D3DXMATRIX*)&shadowW2S);
					}
					effect->CommitChanges();
				}
				// CRITICAL FIX (DS compatibility): with ZEnable=0 the depth stencil is not
				// used for testing; a D16 + A16B16G16R16F RT combo may be invalid under
				// dgVoodoo2 and silently fail the draw. Detach the DS (NULL) for the draw.
				dev9->SetDepthStencilSurface(NULL);
				//if (zEnable != 1) dev9->SetRenderState(D3DRS_ZENABLE, 1);
				//if (zFunc != 4)  dev9->SetRenderState(D3DRS_ZFUNC, 4);	// D3DCMP_LESS
				// FINAL FIX: force the draw to target the shadow color RT. Even if a
				// previously-rendered object changed the device render target mid-pass,
				// this guarantees the depth pass writes into the shadow map.
				if (g_theW3DDeferredRenderer) {
					IDirect3DSurface9 *shadowRT = g_theW3DDeferredRenderer->getShadowRTSurface();
					if (shadowRT) {
						if (curRT != shadowRT) dev9->SetRenderTarget(0, shadowRT);
						shadowRT->Release();
					}
				}
				if (curRT) curRT->Release();
				if (curDS) curDS->Release();
			}
			// OCCLUSION QUERY (decisive): count pixels the shadow draw actually writes.
			// All draw states are verified correct, yet the shadow-map color RT reads
			// 1.0 — this query splits "the draw rasterized nothing (pixels=0, vertex/VS
			// issue)" from "the draw rasterized (pixels>0) but the RT content isn't
			// readable under dgVoodoo2 (RT->SRV/resolve issue)".
			static IDirect3DQuery9 *s_occQuery = NULL;
			if (isShadowMapPass && !s_occQuery && dev9) dev9->CreateQuery(D3DQUERYTYPE_OCCLUSION, &s_occQuery);
			if (isShadowMapPass && s_occQuery) s_occQuery->Issue(D3DISSUE_BEGIN);
			hr = dev9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sm.vertexCount, 0, sm.triangleCount);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] DrawIndexedPrimitive FAILED hr=0x%08X (vc=%d tc=%d)\n", (int)si, (int)hr, sm.vertexCount, sm.triangleCount)); totalFailed++; }
			if (isShadowMapPass && s_occQuery) {
				s_occQuery->Issue(D3DISSUE_END);
				DWORD pix = 0;
				if (SUCCEEDED(s_occQuery->GetData(&pix, sizeof(pix), D3DGETDATA_FLUSH))) {
					static bool s_occDiag = false;
					if (!s_occDiag) {
						s_occDiag = true;
						DEBUG_LOG(("[W3X_DIAG] shadowOcclusion submesh[%d] pixels=%u (0 = nothing rasterized)\n", (int)si, pix));
					}
				}
			}
			drawEffect->EndPass();
		}
		drawEffect->End();
	}

	// Restore the shadow pass's color-write disable that beginShadowMapPass set.
	// The W3X render bypasses DX8Wrapper's state cache, so restore the device to
	// the value the wrapper still believes (COLORWRITEENABLE=0) to keep the cache
	// in sync and subsequent W3D objects in depth-only mode.
	if (isShadowMapPass && dev9) {
		dev9->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
	}

	// NOTE: do NOT reset device state here. The W3X render uses the raw D3D9
	// device directly (bypassing DX8Wrapper's state cache); resetting state
	// here desyncs DX8Wrapper and breaks subsequent regular renders.

	if (totalFailed > 0) {
		DEBUG_LOG(("[W3X_P5]   %u submesh(es) FAILED (of %d)\n", totalFailed, (int)m_meshes.size()));
	}
}
