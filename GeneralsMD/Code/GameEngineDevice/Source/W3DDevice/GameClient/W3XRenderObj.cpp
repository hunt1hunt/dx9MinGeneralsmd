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
#include "WW3D2/coltest.h"	// CollisionMath for Cast_Ray (mouse picking)
#include "WWMath/matrix4.h"
#include "WWMath/quat.h"	// Quaternion for Get_Bone_Transform (Matrix3D::Set_Rotation)
#include "WW3D2/texture.h"
#include "WW3D2/ww3dformat.h"
#include "WW3D2/camera.h"
#include "wwdebug.h"
#include "wwmemlog.h"
#include <d3d9.h>
#include <d3dx9effect.h>
#include <cstdio>
#include <cstdarg>

//=============================================================================
// Always-on diagnostic logger (survives Release builds, where DEBUG_LOG is
// compiled out by NO_RELEASE_DEBUG_LOGGING). Writes to E:\GeneralsMD_W3XShadow.log.
// Used to verify each node of the W3X texture-shadow (RA3 shadow-map) chain:
// shadow-map pass detection (CAST) and receive-side bindings (RECEIVE).
//=============================================================================
static void W3XShadowDiag(const char *fmt, ...)
{
	static FILE *s_log = NULL;
	if (!s_log) {
		s_log = fopen("E:\\GeneralsMD_W3XShadow.log", "a");
		if (s_log) setvbuf(s_log, NULL, _IONBF, 0);
	}
	if (!s_log) return;
	fprintf(s_log, "[%09u] ", GetTickCount());
	va_list args;
	va_start(args, fmt);
	vfprintf(s_log, fmt, args);
	va_end(args);
}

// Forward decls for the RA3 WorldBones quaternion helpers (defined later in
// this file). Needed by Get_Bone_Transform which appears before their defs.
static void W3XQuatMultiply(float *out, const float *a, const float *b);
static void W3XQuatRotateVector(float *out, const float *q, const float *v);
static void W3XQuatConjugate(float *out, const float *q);
static void W3XQuatNormalize(float *out, const float *q);
static void W3XMatrix3DToQuat(const Matrix3D &m, float *q);

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
// Shared W3X SOFT-BOUND vertex declaration (W3XSoftVertex layout, 128-byte
// stride). Maps the dual POSITION/NORMAL/TANGENT/BINORMAL sets + two
// BLENDINDICES + BLENDWEIGHT to the RA3 shader's VS_H_unified_input
// (2-bone soft-skin path: WorldP = lerp(pos1*b1+off1, pos0*b0+off0, w)).
//=============================================================================
IDirect3DVertexDeclaration9 *W3XGetSoftVertexDecl(IDirect3DDevice9 *dev)
{
	static IDirect3DVertexDeclaration9 *s_decl = NULL;
	if (!s_decl && dev) {
		D3DVERTEXELEMENT9 decl[] = {
			{0,   0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0,  12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
			{0,  24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1},
			{0,  36, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 1},
			{0,  48, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
			{0,  60, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
			{0,  72, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 1},
			{0,  84, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 1},
			{0,  96, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
			{0, 112, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0},
			{0, 116, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			{0, 124, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 128, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
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
	m_valid(false),
	m_inShadowMapPass(false)
{
	for (int i = 0; i < kMaxBones; i++) {
		m_boneCtrlActive[i] = false;
		m_boneCtrlQuat[i][0] = m_boneCtrlQuat[i][1] = m_boneCtrlQuat[i][2] = 0.0f;
		m_boneCtrlQuat[i][3] = 1.0f;
		m_boneAnimQuatActive[i] = false;
		m_boneAnimQuat[i][0] = m_boneAnimQuat[i][1] = m_boneAnimQuat[i][2] = 0.0f;
		m_boneAnimQuat[i][3] = 1.0f;
		m_boneAnimTransActive[i] = false;
		m_boneAnimTrans[i][0] = m_boneAnimTrans[i][1] = m_boneAnimTrans[i][2] = 0.0f;
	}
	m_boneLocalQuat = NULL;
	m_boneLocalTrans = NULL;
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

void W3XRenderObjClass::AddSubMesh(IDirect3DVertexBuffer9 *vb, IDirect3DIndexBuffer9 *ib, int vertexCount, int triangleCount, bool softBinding)
{
	SubMesh sm;
	sm.vb = vb;
	sm.ib = ib;
	sm.vertexCount = vertexCount;
	sm.triangleCount = triangleCount;
	sm.hasTangents = false;
	sm.hasBinormals = false;
	sm.softBinding = softBinding;
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

void W3XRenderObjClass::SetSubMeshOrigShader(int subMeshIndex, const char *shader)
{
	if (subMeshIndex < 0 || subMeshIndex >= (int)m_meshes.size()) return;
	m_meshes[subMeshIndex].origShader = shader ? shader : "";
}

void W3XRenderObjClass::SetSubMeshName(int subMeshIndex, const char *name)
{
	if (subMeshIndex < 0 || subMeshIndex >= (int)m_meshes.size()) return;
	m_meshes[subMeshIndex].name = name ? name : "";
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

void W3XRenderObjClass::SetBoneNames(const std::vector<AsciiString> &names)
{
	m_boneNames = names;
}

void W3XRenderObjClass::SetBoneParents(const std::vector<int> &parents)
{
	m_boneParents = parents;
}

int W3XRenderObjClass::Get_Num_Bones(void)
{
	return m_boneCount;
}

const char *W3XRenderObjClass::Get_Bone_Name(int bone_index)
{
	if (bone_index >= 0 && bone_index < (int)m_boneNames.size())
		return m_boneNames[bone_index].str();
	return NULL;
}

int W3XRenderObjClass::Get_Bone_Index(const char *bonename)
{
	if (!bonename || !bonename[0]) return 0;
	AsciiString want(bonename);
	want.toLower();
	for (int i = 0; i < (int)m_boneNames.size(); i++) {
		if (m_boneNames[i].compareNoCase(want) == 0)
			return i;
	}
	return 0;	// W3D convention: 0 = not found / root bone
}

void W3XRenderObjClass::composeControlledBones(float *out) const
{
	// RA3 W3X composition. The animation rotation is applied to the ORIENTATION
	// only; the bone's local POSITION stays at its bind spot plus the (bind-rotated)
	// animation translation delta. Rotating bindLocalTrans by the anim quaternion
	// would swing the bone's pivot around its own centre instead of staying on the
	// parent mount — that made soldier joints stretch like rubber. Matches the RA3
	// shader contract (Skinning.fxh BoneTransformPosition = R·v + t: the translation
	// is never rotated by the bone's own rotation):
	//   localQuat  = bindLocalQuat * animQuat
	//   localTrans = bindLocalTrans + R(bindLocalQuat) * animTrans
	//   worldQuat  = parentWorldQuat * localQuat
	//   worldOffset= parentWorldOffset + R(parentWorldQuat) * localTrans
	// animQuat / animTrans are the RAW RA3 channel values (channel = bind^-1 ×
	// animLocal, so compose = bind × channel reproduces the exported pose; the
	// anim translation delta must be rotated by the bind quaternion to recover
	// animLocal.trans — identity-bind bones, i.e. all vehicles/buildings, are
	// unaffected). A turret-controlled bone (Control_Bone) replaces the animation
	// (W3D capture semantics) with the control rotation on the bind pose, so the
	// tank turret stays game-logic driven while animated bones (soldier
	// legs/spine/arms) follow the channel.
	bool hasLocalPose = (m_boneLocalQuat != NULL && m_boneLocalTrans != NULL);

	for (int bi = 0; bi < m_boneCount && bi < kMaxBones; bi++) {
		int pj = (bi < (int)m_boneParents.size()) ? m_boneParents[bi] : -1;

		float lq[4];
		float lt[3];
		bool controlled = m_boneCtrlActive[bi];
		bool animated   = m_boneAnimQuatActive[bi] || m_boneAnimTransActive[bi];

		if (controlled) {
			// W3D capture: the game-logic control rotation replaces the animation
			// on the bind pose. bindLocalQuat * ctrlQuat (the tank's bind quats are
			// all identity, so this is just ctrlQuat). Translation stays at the bind
			// spot (+ bind-rotated anim trans), matching the pre-split behavior.
			if (hasLocalPose) {
				W3XQuatMultiply(lq, &m_boneLocalQuat[bi*4], m_boneCtrlQuat[bi]);
				lt[0] = m_boneLocalTrans[bi*3+0];
				lt[1] = m_boneLocalTrans[bi*3+1];
				lt[2] = m_boneLocalTrans[bi*3+2];
				if (m_boneAnimTransActive[bi]) {
					// Same channel-frame rotation as the animated branch: rotate the
					// anim translation delta by the bind quaternion. Identity-bind
					// bones (all vehicles/buildings) are unaffected.
					float r[3];
					W3XQuatRotateVector(r, &m_boneLocalQuat[bi*4], m_boneAnimTrans[bi]);
					lt[0] += r[0];
					lt[1] += r[1];
					lt[2] += r[2];
				}
			} else {
				lq[0] = m_bones[bi*8+0]; lq[1] = m_bones[bi*8+1]; lq[2] = m_bones[bi*8+2]; lq[3] = m_bones[bi*8+3];
				lt[0] = m_bones[bi*8+4]; lt[1] = m_bones[bi*8+5]; lt[2] = m_bones[bi*8+6];
			}
		} else if (animated && hasLocalPose) {
			// SAGE composition (commit 30aabc6 + bind-rotation fix): the animation
			// rotation is applied to the ORIENTATION only; the bone's local POSITION
			// stays at its bind spot plus the bind-rotated animation translation
			// delta. Rotating bindLocalTrans by the animation quaternion would swing
			// the bone's pivot around its own centre (turret/limb spin) instead of
			// staying on the parent mount — that made soldier joints stretch.
			//   localQuat  = bindLocalQuat * animQuat
			//   localTrans = bindLocalTrans + R(bindLocalQuat) * animTrans
			float aq[4];
			if (m_boneAnimQuatActive[bi]) {
				aq[0] = m_boneAnimQuat[bi][0]; aq[1] = m_boneAnimQuat[bi][1];
				aq[2] = m_boneAnimQuat[bi][2]; aq[3] = m_boneAnimQuat[bi][3];
			} else {
				aq[0] = 0.0f; aq[1] = 0.0f; aq[2] = 0.0f; aq[3] = 1.0f;	// identity
			}
			W3XQuatMultiply(lq, &m_boneLocalQuat[bi*4], aq);
			lt[0] = m_boneLocalTrans[bi*3+0];
			lt[1] = m_boneLocalTrans[bi*3+1];
			lt[2] = m_boneLocalTrans[bi*3+2];
			if (m_boneAnimTransActive[bi]) {
				// channel = bind^-1 x animLocal, so the anim translation DELTA must be
				// rotated by the bind quaternion to recover animLocal.trans. Without
				// this, bones with a non-identity bind rotation (e.g. JUANTI
				// ENERGYRIFLE, 90 deg around Z) had the delta applied in the parent
				// frame and the weapon flew ~17 units from the hand.
				float r[3];
				W3XQuatRotateVector(r, &m_boneLocalQuat[bi*4], m_boneAnimTrans[bi]);
				lt[0] += r[0];
				lt[1] += r[1];
				lt[2] += r[2];
			}
		} else if (hasLocalPose) {
			// Bind pose (no animation, no control).
			lq[0] = m_boneLocalQuat[bi*4+0]; lq[1] = m_boneLocalQuat[bi*4+1];
			lq[2] = m_boneLocalQuat[bi*4+2]; lq[3] = m_boneLocalQuat[bi*4+3];
			lt[0] = m_boneLocalTrans[bi*3+0]; lt[1] = m_boneLocalTrans[bi*3+1];
			lt[2] = m_boneLocalTrans[bi*3+2];
		} else {
			lq[0] = m_bones[bi*8+0]; lq[1] = m_bones[bi*8+1]; lq[2] = m_bones[bi*8+2]; lq[3] = m_bones[bi*8+3];
			lt[0] = m_bones[bi*8+4]; lt[1] = m_bones[bi*8+5]; lt[2] = m_bones[bi*8+6];
		}

		// --- world = parentWorld * local ---
		if (pj >= 0 && pj < kMaxBones) {
			float wq[4];
			W3XQuatMultiply(wq, &out[pj*8], lq);
			out[bi*8+0] = wq[0]; out[bi*8+1] = wq[1]; out[bi*8+2] = wq[2]; out[bi*8+3] = wq[3];
			float rot[3];
			W3XQuatRotateVector(rot, &out[pj*8], lt);
			out[bi*8+4] = out[pj*8+4] + rot[0];
			out[bi*8+5] = out[pj*8+5] + rot[1];
			out[bi*8+6] = out[pj*8+6] + rot[2];
		} else {
			out[bi*8+0] = lq[0]; out[bi*8+1] = lq[1]; out[bi*8+2] = lq[2]; out[bi*8+3] = lq[3];
			out[bi*8+4] = lt[0]; out[bi*8+5] = lt[1]; out[bi*8+6] = lt[2];
		}
		out[bi*8+7] = m_bones[bi*8+7];	// alpha
	}
}

int W3XRenderObjClass::GetComposedBones(float *out, int maxFloats) const
{
	if (!out || maxFloats < kMaxBones * 8) return 0;
	composeControlledBones(out);
	return m_boneCount;
}

bool W3XRenderObjClass::ApplyAnimationFrame(const W3XAnimation *anim, int frame)
{
	if (!anim) return false;
	bool applied = false;
	for (size_t ci = 0; ci < anim->channels.size(); ci++) {
		const W3XAnimChannel &ch = anim->channels[ci];
		if (ch.pivot < 0 || ch.pivot >= kMaxBones) continue;
		if (!ch.quatFrames.empty()) {
			int n = (int)ch.quatFrames.size() / 4;
			int f = (n > 0) ? (frame % n) : 0;
			SetBoneAnimQuat(ch.pivot, &ch.quatFrames[f * 4]);
			applied = true;
		}
		if (!ch.transFrames.empty()) {
			int n = (int)ch.transFrames.size() / 3;
			int f = (n > 0) ? (frame % n) : 0;
			SetBoneAnimTrans(ch.pivot, &ch.transFrames[f * 3]);
			applied = true;
		}
	}
	return applied;
}

const Matrix3D &W3XRenderObjClass::Get_Bone_Transform(int boneindex)
{
	// World-space bone transform, taking the turret/barrel control (Control_Bone
	// + cascade) into account so a muzzle / launch offset on the barrel follows
	// the animated turret. Same composition as BindW3XBones:
	//   worldRot = W_quat * boneQuat, worldOffset = rot(boneOffset, W_quat) + W_trans
	m_boneTransformCache.Make_Identity();
	if (boneindex < 0 || boneindex >= m_boneCount || !m_bones) {
		m_boneTransformCache = m_worldTransform;
		return m_boneTransformCache;
	}
	// Compose bind pose + controlled rotations + cascade into a temp array.
	float comp[kMaxBones * 8];
	composeControlledBones(comp);

	float wq[4];
	W3XMatrix3DToQuat(m_worldTransform, wq);
	const float *bq = &comp[boneindex * 8 + 0];
	const float *bo = &comp[boneindex * 8 + 4];
	float worldQuat[4];
	float worldOffset[3];
	W3XQuatMultiply(worldQuat, wq, bq);			// worldRot = W_quat * boneQuat
	W3XQuatRotateVector(worldOffset, wq, bo);	// rotate offset by W_quat
	Vector3 wtrans = m_worldTransform.Get_Translation();
	worldOffset[0] += wtrans.X;
	worldOffset[1] += wtrans.Y;
	worldOffset[2] += wtrans.Z;
	Quaternion q(worldQuat[0], worldQuat[1], worldQuat[2], worldQuat[3]);
	m_boneTransformCache.Set_Rotation(q);
	m_boneTransformCache.Set_Translation(Vector3(worldOffset[0], worldOffset[1], worldOffset[2]));
	return m_boneTransformCache;
}

Matrix3D W3XRenderObjClass::Get_Bone_Transform_Model(int boneindex) const
{
	// Object-local (model-space) bone transform: the composed skeleton WITHOUT the
	// object's world transform. Mirrors the "pristine" bone W3D exposes so launch/
	// muzzle offsets that the weapon logic combines with the object transform work
	// at the correct spot (fx_laser on the launcher, not the unit origin).
	Matrix3D result;
	result.Make_Identity();
	if (boneindex < 0 || boneindex >= m_boneCount || !m_bones) return result;
	float comp[kMaxBones * 8];
	composeControlledBones(comp);
	const float *bq = &comp[boneindex * 8 + 0];
	const float *bo = &comp[boneindex * 8 + 4];
	Quaternion q(bq[0], bq[1], bq[2], bq[3]);
	result.Set_Rotation(q);
	result.Set_Translation(Vector3(bo[0], bo[1], bo[2]));
	return result;
}

Matrix3D W3XRenderObjClass::Get_Bone_Transform_Model_Anim(const W3XAnimation *anim, int boneindex, float frame) const
{
	// Model-space bone transform in the pose of the given animation at 'frame',
	// WITHOUT mutating the live animation state. The weapon launch offset must come
	// from the firing state's pose (forward muzzle), but the fire FX / launch query
	// runs while the render is still in the previous (e.g. idle) state — the live
	// bone state has the muzzle on the right side. Evaluating the firing animation
	// directly gives the forward muzzle position regardless of the render timing.
	Matrix3D result;
	result.Make_Identity();
	if (!anim || boneindex < 0 || boneindex >= m_boneCount || !m_boneLocalQuat || !m_boneLocalTrans)
		return result;

	int nf = (anim->numFrames > 1) ? anim->numFrames : 1;
	int f0 = ((int)frame % nf + nf) % nf;
	int f1 = (f0 + 1) % nf;
	float t = frame - (float)(int)frame;
	if (t < 0.0f) t = 0.0f;

	// Build per-bone animated LOCAL overrides from the animation's channels at 'frame'.
	float aq[kMaxBones][4];  bool qa[kMaxBones];
	float at[kMaxBones][3];  bool ta[kMaxBones];
	for (int i = 0; i < kMaxBones; i++) {
		aq[i][0] = aq[i][1] = aq[i][2] = 0.0f; aq[i][3] = 1.0f; qa[i] = false;
		at[i][0] = at[i][1] = at[i][2] = 0.0f; ta[i] = false;
	}
	for (size_t ci = 0; ci < anim->channels.size(); ci++) {
		const W3XAnimChannel &ch = anim->channels[ci];
		int p = ch.pivot;
		if (p < 0 || p >= kMaxBones) continue;
		if (!ch.quatFrames.empty()) {
			int n = (int)ch.quatFrames.size() / 4;
			if (n >= 1) {
				int a = f0 % n, b = f1 % n;
				const float *q0 = &ch.quatFrames[a * 4];
				const float *q1 = &ch.quatFrames[b * 4];
				// Normalize the keyframe endpoints so the slerp runs on unit
				// quaternions (RA3 data is unit; dirty extractions may not be).
				float nq0[4], nq1[4];
				W3XQuatNormalize(nq0, q0);
				W3XQuatNormalize(nq1, q1);
				Quaternion A(nq0[0], nq0[1], nq0[2], nq0[3]);
				Quaternion B(nq1[0], nq1[1], nq1[2], nq1[3]);
				Quaternion R;
				Slerp(R, A, B, t);
				// Apply the channel directly (RA3 absolute-local semantic, matches
				// updateAnimation) — NO frame-0 normalization. See updateAnimation.
				aq[p][0] = R.X; aq[p][1] = R.Y; aq[p][2] = R.Z; aq[p][3] = R.W;
				qa[p] = true;
			}
		}
		if (!ch.transFrames.empty()) {
			int n = (int)ch.transFrames.size() / 3;
			if (n >= 1) {
				int a = f0 % n, b = f1 % n;
				const float *t0 = &ch.transFrames[a * 3];
				const float *t1 = &ch.transFrames[b * 3];
				at[p][0] = (t0[0] + (t1[0] - t0[0]) * t);
				at[p][1] = (t0[1] + (t1[1] - t0[1]) * t);
				at[p][2] = (t0[2] + (t1[2] - t0[2]) * t);
				ta[p] = true;
			}
		}
	}

	// Compose: localQuat = bind × anim, localTrans = bind + R(bind)*anim;
	// world = parentWorld × local (parent chain).
	// Same math as composeControlledBones but with the evaluated anim overrides and
	// no turret Control_Bone (infantry launch bones are not turret-driven).
	float comp[kMaxBones * 8];
	for (int bi = 0; bi < m_boneCount && bi < kMaxBones; bi++) {
		int pj = (bi < (int)m_boneParents.size()) ? m_boneParents[bi] : -1;
		float lq[4], lt[3];
		if (qa[bi] || ta[bi]) {
			// Same as composeControlledBones: localQuat = bindLocalQuat * animQuat,
			// localTrans = bindLocalTrans + R(bindLocalQuat) * animTrans (RA3 export
			// channel = bind^-1 × animLocal, so the anim delta is bind-rotated).
			W3XQuatMultiply(lq, &m_boneLocalQuat[bi * 4], aq[bi]);
			lt[0] = m_boneLocalTrans[bi*3+0];
			lt[1] = m_boneLocalTrans[bi*3+1];
			lt[2] = m_boneLocalTrans[bi*3+2];
			if (ta[bi]) {
				// Rotate the anim translation delta by the bind quaternion (channel =
				// bind^-1 x animLocal), matching composeControlledBones so the launch
				// offset follows the same pose the renderer shows.
				float arot[3];
				W3XQuatRotateVector(arot, &m_boneLocalQuat[bi * 4], at[bi]);
				lt[0] += arot[0]; lt[1] += arot[1]; lt[2] += arot[2];
			}
		} else {
			lq[0] = m_boneLocalQuat[bi*4+0]; lq[1] = m_boneLocalQuat[bi*4+1]; lq[2] = m_boneLocalQuat[bi*4+2]; lq[3] = m_boneLocalQuat[bi*4+3];
			lt[0] = m_boneLocalTrans[bi*3+0]; lt[1] = m_boneLocalTrans[bi*3+1]; lt[2] = m_boneLocalTrans[bi*3+2];
		}
		if (pj >= 0 && pj < kMaxBones) {
			W3XQuatMultiply(&comp[bi*8], &comp[pj*8], lq);
			float r[3];
			W3XQuatRotateVector(r, &comp[pj*8], lt);
			comp[bi*8+4] = comp[pj*8+4] + r[0];
			comp[bi*8+5] = comp[pj*8+5] + r[1];
			comp[bi*8+6] = comp[pj*8+6] + r[2];
		} else {
			comp[bi*8+0] = lq[0]; comp[bi*8+1] = lq[1]; comp[bi*8+2] = lq[2]; comp[bi*8+3] = lq[3];
			comp[bi*8+4] = lt[0]; comp[bi*8+5] = lt[1]; comp[bi*8+6] = lt[2];
		}
		comp[bi*8+7] = 1.0f;	// alpha
	}
	const float *bq = &comp[boneindex * 8 + 0];
	const float *bo = &comp[boneindex * 8 + 4];
	Quaternion q(bq[0], bq[1], bq[2], bq[3]);
	result.Set_Rotation(q);
	result.Set_Translation(Vector3(bo[0], bo[1], bo[2]));
	return result;
}

const Matrix3D &W3XRenderObjClass::Get_Bone_Transform(const char *bonename)
{
	int idx = Get_Bone_Index(bonename);
	if (idx == 0) {
		// No match (or root bone). Return identity world so callers that
		// just translate to the bone position land on the object origin.
		m_boneTransformCache = m_worldTransform;
		return m_boneTransformCache;
	}
	return Get_Bone_Transform(idx);
}

void W3XRenderObjClass::Control_Bone(int bindex, const Matrix3D &objtm, bool /*world_space_translation*/)
{
	// Store the bone's local rotation as a quaternion override. This mirrors the
	// W3D HTree turret control: the turret/barrel bone rotates by the given
	// matrix on top of its bind pose, and the composition happens in Render().
	if (bindex < 0 || bindex >= kMaxBones) return;
	float q[4];
	W3XMatrix3DToQuat(objtm, q);
	m_boneCtrlQuat[bindex][0] = q[0];
	m_boneCtrlQuat[bindex][1] = q[1];
	m_boneCtrlQuat[bindex][2] = q[2];
	m_boneCtrlQuat[bindex][3] = q[3];
	m_boneCtrlActive[bindex] = true;
}

void W3XRenderObjClass::SetBoneAnimQuat(int bindex, const float q[4])
{
	if (bindex < 0 || bindex >= kMaxBones) return;
	// Animation channel quat goes to its OWN slot (m_boneAnimQuat), separate
	// from the game-logic turret control (m_boneCtrlQuat via Control_Bone) so an
	// animated bone and a turret-controlled bone never overwrite each other.
	m_boneAnimQuat[bindex][0] = q[0];
	m_boneAnimQuat[bindex][1] = q[1];
	m_boneAnimQuat[bindex][2] = q[2];
	m_boneAnimQuat[bindex][3] = q[3];
	m_boneAnimQuatActive[bindex] = true;
}

void W3XRenderObjClass::SetBoneAnimTrans(int bindex, const float t[3])
{
	if (bindex < 0 || bindex >= kMaxBones) return;
	m_boneAnimTrans[bindex][0] = t[0];
	m_boneAnimTrans[bindex][1] = t[1];
	m_boneAnimTrans[bindex][2] = t[2];
	m_boneAnimTransActive[bindex] = true;
}

void W3XRenderObjClass::ResetAnimationBones(void)
{
	// Clear only the animation overrides; turret Control_Bone (m_boneCtrlActive)
	// is intentionally left alone so handleClientTurretPositioning's rotation
	// survives an animation update.
	for (int i = 0; i < kMaxBones; i++) {
		m_boneAnimQuatActive[i] = false;
		m_boneAnimTransActive[i] = false;
	}
}

void W3XRenderObjClass::SetBoneLocalPose(const float *localQuat, const float *localTrans, int boneCount)
{
	if (m_boneLocalQuat) delete[] m_boneLocalQuat;
	if (m_boneLocalTrans) delete[] m_boneLocalTrans;
	m_boneLocalQuat = NULL;
	m_boneLocalTrans = NULL;
	if (localQuat && localTrans && boneCount > 0) {
		m_boneLocalQuat = new float[boneCount * 4];
		memcpy(m_boneLocalQuat, localQuat, boneCount * 4 * sizeof(float));
		m_boneLocalTrans = new float[boneCount * 3];
		memcpy(m_boneLocalTrans, localTrans, boneCount * 3 * sizeof(float));
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
	if (m_boneLocalQuat) { delete[] m_boneLocalQuat; m_boneLocalQuat = NULL; }
	if (m_boneLocalTrans) { delete[] m_boneLocalTrans; m_boneLocalTrans = NULL; }
	m_boneCount = 0;
	m_boneNames.clear();
	m_boneParents.clear();
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

static void W3XQuatConjugate(float *out, const float *q)
{
	// out = q* (inverse for unit quaternions) = (-x, -y, -z, w)
	out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

static void W3XQuatNormalize(float *out, const float *q)
{
	// out = normalize(q); falls back to identity on a zero-length quat so the
	// caller's conjugate-inverse / slerp stays well-defined on dirty data.
	float len = (float)sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
	if (len > 1e-8f) {
		out[0] = q[0]/len; out[1] = q[1]/len; out[2] = q[2]/len; out[3] = q[3]/len;
	} else {
		out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
	}
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

// Force the engine texture manager to load every texture a W3X constant set
// references, so the first in-game render doesn't hit a D3D file load.
static void PreloadW3XConstantsTextures(const std::vector<W3XShaderConstant> &constants)
{
	for (size_t ci = 0; ci < constants.size(); ci++) {
		const W3XShaderConstant &c = constants[ci];
		if (c.type != W3X_CONSTANT_TEXTURE || c.textureValue.isEmpty()) continue;
		AsciiString dds = ResolveTexturePathCached(c.textureValue.str());
		if (!dds.isEmpty())
			WW3DAssetManager::Get_Instance()->Get_Texture(dds.str(), MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, true);
	}
}

void W3XRenderObjClass::PreloadAssets(void)
{
	// Compile the model-wide + per-sub-mesh shaders (D3DXCreateEffect is the
	// biggest single-frame stall when it runs on first render).
	W3XEffectManager::Instance()->GetEffect(m_fxName.str());
	for (size_t mi = 0; mi < m_meshes.size(); mi++)
		if (!m_meshes[mi].fxName.isEmpty())
			W3XEffectManager::Instance()->GetEffect(m_meshes[mi].fxName.str());
	// Load the textures referenced by the model + per-sub-mesh constants.
	PreloadW3XConstantsTextures(m_constants);
	for (size_t si = 0; si < m_meshes.size(); si++)
		PreloadW3XConstantsTextures(m_meshes[si].constants);
}

bool W3XRenderObjClass::Cast_Ray(RayCollisionTestClass & raytest)
{
	// Mirrors AABoxRenderObjClass::Cast_Ray so mouse clicks pick W3X units. The
	// base RenderObjClass::Cast_Ray returns false -> W3X objects were unselectable
	// by click (only drag-box). Test the world-space AABB set via SetBounds.
	if ((Get_Collision_Type() & raytest.CollisionType) == 0) return false;
	if (raytest.Result->StartBad) return false;

	if (CollisionMath::Collide(raytest.Ray, Get_Bounding_Box(), raytest.Result)) {
		raytest.CollidedRenderObj = this;
		return true;
	}
	return false;
}

//=============================================================================
// BindW3XConstants: apply a list of W3XShaderConstant to an effect.
// Shared by the model-wide shader and per-sub-mesh shader overrides.
//=============================================================================
static void BindW3XConstants(ID3DXEffect *effect, const std::vector<W3XShaderConstant> &constants)
{
	for (size_t ci = 0; ci < constants.size(); ci++) {
		const W3XShaderConstant &c = constants[ci];
		// BASIC-convention "Texture_0" constant on a PBR (w3x_soviet.fx) mesh:
		// the PBR shader has no Texture_0 param, so bind the value to its
		// DiffuseTexture instead. This lets RA3 meshes that declare a single
		// Texture_0 (rotor blades Fx_blades_G2, faction decals) render with the
		// PBR shader WITHOUT editing the .w3x resource.
		const char *paramName = c.name.str();
		D3DXHANDLE h = effect->GetParameterByName(NULL, paramName);
		if (!h && c.type == W3X_CONSTANT_TEXTURE && strcmp(paramName, "Texture_0") == 0) {
			h = effect->GetParameterByName(NULL, "DiffuseTexture");
		}
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
						if (FAILED(hrt)) DEBUG_LOG(("[W3X_P5]   TEXTURE '%s': SetTexture FAILED hr=0x%08X\n", paramName, (int)hrt));
					} else {
						DEBUG_LOG(("[W3X_P5]   TEXTURE '%s': Get_Texture FAILED (tex=%p)\n", paramName, (void*)tex));
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
// ShadowMapWorldToShadow matrix, the RA3 shadow-map receive gates (HasShadow /
// ShadowMap / texel-size) and the RecolorColor faction color.
// Called for BOTH the model-wide effect and any per-sub-mesh override effect,
// since each D3DXEffect instance keeps its own parameter values.
//=============================================================================
static void BindW3XMatrices(ID3DXEffect *effect, const Matrix4x4 &world,
	const Matrix4x4 &vp, const Matrix4x4 &wvp, const Matrix4x4 &shadowW2S,
	unsigned int recolorHex, bool receiveShadow = false,
	IDirect3DBaseTexture9 *shadowTex = NULL)
{
	D3DXHANDLE hWorld = effect->GetParameterByName(NULL, "World");
	if (hWorld) effect->SetMatrix(hWorld, (const D3DXMATRIX*)&world);
	D3DXHANDLE hVP = effect->GetParameterByName(NULL, "ViewProjection");
	if (hVP) effect->SetMatrix(hVP, (const D3DXMATRIX*)&vp);
	D3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");
	if (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);

	// ShadowMapWorldToShadow — the RA3 world->shadow-space matrix. In the main
	// (receive) pass this is sunVP*bias (sun NDC -> [0,1] map UV + [0,1] depth)
	// so the skinned VS's ShadowCS feeds the PCF; in the shadow-map (cast) pass
	// it is the raw sun VP. A stale/identity matrix sends every shadow sample to
	// the wrong texel and the model reads as unshaded.
	D3DXHANDLE hShadowW2S = effect->GetParameterByName(NULL, "ShadowMapWorldToShadow");
	if (hShadowW2S) effect->SetMatrix(hShadowW2S, (const D3DXMATRIX*)&shadowW2S);

	// RA3 shadow-map receive gates. HasShadow is the master on/off for the
	// hp_invshadow_* PCF functions (they return 1 = full sunlight when off, so
	// W3X had NO texture shadow at all — HasShadow was never set). ShadowMap is
	// the sampler input (the D24X8 sun-depth texture). The texel-size constant
	// drives the PCF offsets — always set it so a stale (0,0,0,0) can't divide
	// by zero in the shader when the map is unavailable.
	{
		D3DXHANDLE hHas = effect->GetParameterByName(NULL, "HasShadow");
		if (hHas) effect->SetBool(hHas, (receiveShadow && shadowTex) ? TRUE : FALSE);
		if (receiveShadow && shadowTex) {
			D3DXHANDLE hMap = effect->GetParameterByName(NULL, "ShadowMap");
			if (hMap) effect->SetTexture(hMap, shadowTex);
		}
		D3DXHANDLE hTexel = effect->GetParameterByName(NULL, "Shadowmap_Zero_Zero_OneOverMapSize_OneOverMapSize");
		if (hTexel) {
			float smt[4] = { 0, 0, 1.0f/2048.0f, 1.0f/2048.0f };
			effect->SetVector(hTexel, (const D3DXVECTOR4*)smt);
		}
		// W3X shadow NORMAL BIAS (world units): push the receive shadow-map query
		// along the surface normal to kill the acne band that sweeps across
		// sun-facing surfaces when the camera moves perpendicular to the sun.
		// ~2 shadow-map texels (window 4000 / 2048 = 1.95 units -> ~4.0). The cast
		// renders true depth (PS_ShadowDepth ignores this param).
		D3DXHANDLE hNB = effect->GetParameterByName(NULL, "W3XNormalBias");
		if (hNB) effect->SetFloat(hNB, 4.0f);
	}

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
static void BindW3XBones(ID3DXEffect *effect, float *bones, int boneCount, const Matrix3D &worldTransform, int jointsPerVertex = -1)
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
			HRESULT hb = effect->SetFloatArray(hBones, wb, boneCount * 8);
#if defined(DEBUG_LOGGING)
			// DIAG: verify the WorldBones actually reach the shader. If bone20off is
			// ~(4.6,0.2,10.9) the bones are the animated chest pose; if (0,0,0) or
			// garbage, the upload failed -> vertices stay at bone-local (unskinned pile).
			{
				static bool s_wbDiag = false;
				if (!s_wbDiag && boneCount > 20) {
					s_wbDiag = true;
					DEBUG_LOG(("[W3X_SOFT4] SetFloatArray WorldBones hr=0x%08X n=%d | "
						"bone0q=(%.3f,%.3f,%.3f,%.3f) bone2off=(%.2f,%.2f,%.2f) "
						"bone20off=(%.2f,%.2f,%.2f)\n",
						(int)hb, boneCount * 8,
						wb[0], wb[1], wb[2], wb[3],
						wb[2*8+4], wb[2*8+5], wb[2*8+6],
						wb[20*8+4], wb[20*8+5], wb[20*8+6]));
				}
			}
#else
			(void)hb;	// keep C4189 away when DEBUG_LOG is compiled out
#endif
		} else {
			DEBUG_LOG(("[W3X_P5]   WorldBones param NOT FOUND (model has %d bones)\n", boneCount));
		}
	}

	// Set NumJointsPerVertex so the RA3 VSchooser() picks the skinned vertex
	// shader (1 = hard skin, 2 = soft skin). The model has bones, so skin it.
	// A soft-bound sub-mesh overrides to 2 (2-bone blend) right before drawing.
	{
		D3DXHANDLE hJoints = effect->GetParameterByName(NULL, "NumJointsPerVertex");
		if (hJoints) {
			int joints = (jointsPerVertex >= 0) ? jointsPerVertex : (boneCount > 0 ? 1 : 0);
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
	// setHidden (ObjectDrawInterface) toggles this; skip drawing hidden W3X
	// (e.g. units inside transports / stealthed).
	if (Is_Hidden()) return;


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
	bool inShadowPass = false;
	{
		// Detect the deferred shadow-map (CAST) pass using ONLY the deferred
		// renderer's explicit in-pass flag (set by beginShadowMapPass, cleared by
		// endShadowMapPass). The old fallback `smCWE == 0` (COLORWRITEENABLE==0)
		// could ALSO be true in the MAIN pass if any earlier stage left CWE=0 —
		// then the W3X wrongly entered the CAST branch, used the SUN camera VP to
		// rasterize the main pass (ShadowDepth technique), and every surface shadow
		// swept across the model as the camera orbited. RA3 gates on an explicit
		// HasShadow/Sas binding, never on device CWE — match that here.
		inShadowPass = (g_theW3DDeferredRenderer != NULL)
			&& g_theW3DDeferredRenderer->isInShadowMapPass();
		if (!g_theW3DDeferredRenderer) {
			return;	// no deferred renderer -> nothing to cast into
		}
		if (inShadowPass) {
			// Sun camera VP is already D3D row-major (D3DXMatrixLookAtLH/OrthoLH
			// stored verbatim by beginShadowMapPass). Do NOT transpose.
			view = g_theW3DDeferredRenderer->getShadowView();
			proj = g_theW3DDeferredRenderer->getShadowProj();
		} else {
			view.Init(rinfo.Camera.Get_View_Matrix());
			rinfo.Camera.Get_D3D_Projection_Matrix(&proj);
			view = view.Transpose();	// engine column-major -> D3D row-major
			proj = proj.Transpose();
		}
	}
	Matrix4x4 vp = Multiply(view, proj);
	Matrix4x4 wvp = Multiply(world, vp);
	// ShadowMapWorldToShadow: in the CAST pass bind the raw sun VP (the shadow
	// VS writes sun clip space). In the RECEIVE pass bind sunVP * bias, where the
	// bias maps sun NDC (x,y in [-1,1], D3D z in [0,1]) to a direct [0,1] shadow-
	// map UV + [0,1] depth — the RA3 PCF (hp_invshadow_*) samples at that UV
	// directly. The deferred SunLightShadow PS does the same 0.5x+0.5 remap by
	// hand; here it is baked into the matrix. Identity (or garbage) would send
	// every shadow sample to the wrong texel and the model reads as unshaded.
	Matrix4x4 shadowW2S;
	if (g_theW3DDeferredRenderer) {
		if (inShadowPass) {
			shadowW2S = g_theW3DDeferredRenderer->getShadowViewProj();
		} else if (g_theW3DDeferredRenderer->isShadowMapAvailable()) {
			Matrix4x4 bias;
			bias.Make_Identity();
			bias[0] = Vector4(0.5f, 0.0f, 0.0f, 0.0f);
			bias[1] = Vector4(0.0f, 0.5f, 0.0f, 0.0f);
			bias[2] = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
			bias[3] = Vector4(0.5f, 0.5f, 0.0f, 1.0f);
			shadowW2S = Multiply(g_theW3DDeferredRenderer->getShadowViewProj(), bias);
		} else {
			// Shadow map not available (INI off / no D24X8): identity so the VS's
			// ShadowCS output stays finite (a garbage W2S could feed NaN through the
			// draw). HasShadow stays 0 -> no shadow, matching the pre-fix behavior.
			shadowW2S.Make_Identity();
		}
	} else {
		shadowW2S.Make_Identity();
	}
	// Receive-side texture-shadow bindings (main forward pass only). The W3X
	// samples the shadow-map COLOR RT (m_shadowDepthRT) that the W3X CAST wrote
	// its sun-depth grayscale into via the RA3 ShadowDepth technique — this is
	// the RA3-style COLOR-RT shadow map (A8R8G8B8, PS_ShadowDepth returned clip
	// z/w into the RED channel). The W3D buildings/units keep sampling the D24X8
	// (their SunLightShadow PS path is unchanged). Only when the deferred renderer
	// + its shadow map are available; otherwise HasShadow stays 0 and the PCF
	// functions return "full sunlight".
	// 方案B (RA3-aligned stable self-shadow, 2026-08-31): the W3X surface MUST
	// receive the texture shadow (COLOR RT) — this is the RA3 model self-shadow
	// (own geometry occludes the sun + shadows from other objects). The sweeping
	// artifact was NOT the receive itself but the camera-following shadow map's
	// texel grid swimming; that is fixed by texel snapping in beginShadowMapPass.
	// So the receive is RE-ENABLED.
	bool receiveShadow = (!inShadowPass && g_theW3DDeferredRenderer
		&& g_theW3DDeferredRenderer->isShadowMapAvailable());
	IDirect3DBaseTexture9 *shadowTex = receiveShadow
		? g_theW3DDeferredRenderer->getShadowColorMapTexture() : NULL;

	// DIAG (one-shot, always-on in Release): report the texture-shadow chain
	// state at this node — pass (CAST vs RECEIVE), shadow-map availability, and
	// whether the effect exposes the RA3 shadow parameters.
	if (inShadowPass) {
		// ISOLATION BUILD: 3 bursts, >=10s apart (was a process one-shot). The
		// one-shot NEVER fired in the crashing run despite 58-object shadow
		// passes - if these lines still don't appear, W3X objects are excluded
		// from the shadow pass itself (visibility/routing), not from the cast.
		static int s_castDiagN = 0;
		static unsigned s_castDiagLast = 0;
		if (s_castDiagN < 3 && GetTickCount() - s_castDiagLast >= 10000) {
			s_castDiagN++;
			s_castDiagLast = GetTickCount();
			const Matrix4x4 &rawVP = g_theW3DDeferredRenderer->getShadowViewProj();
			W3XShadowDiag("[W3X_SHDW] CAST '%s' sunVP r0=(%.3f,%.3f,%.3f,%.3f) r3=(%.3f,%.3f,%.3f,%.3f) submeshes=%d\n",
				m_name, shadowW2S[0].X, shadowW2S[0].Y, shadowW2S[0].Z, shadowW2S[0].W,
				shadowW2S[3].X, shadowW2S[3].Y, shadowW2S[3].Z, shadowW2S[3].W,
				(int)m_meshes.size());
			// DECISIVE: compare shadowW2S (what we bind) vs getShadowViewProj()
			// (the deferred renderer's m_shadowViewProj, verified correct by [VP]).
			// If rawVP r0=(0.002,0,0,0) but shadowW2S r0=(0,-0.001,0,0), the copy
			// or the inShadowPass routing is wrong; if BOTH are (0,-0.001,0,0),
			// then m_shadowViewProj is stale at this moment (beginShadowMapPass
			// not run / different instance).
			W3XShadowDiag("[W3X_SHDW] CAST_RAW getShadowViewProj r0=(%.4f,%.4f,%.4f,%.4f) r3=(%.4f,%.4f,%.4f,%.4f)\n",
				rawVP[0].X, rawVP[0].Y, rawVP[0].Z, rawVP[0].W,
				rawVP[3].X, rawVP[3].Y, rawVP[3].Z, rawVP[3].W);
			// CAUSE-HUNT DIAG (2026-09-05): where is this object relative to the
			// sun window? world X/Y vs the map-fitted window, and the object
			// ORIGIN's sun NDC z - the rasterizer clips casters whose sun z is
			// outside [0,1] (the algebra-proven near/far clipping defect).
			{
				float cwx = world[3].X, cwy = world[3].Y, cwz = world[3].Z;
				float sndcZ = cwx * shadowW2S[2].X + cwy * shadowW2S[2].Y
					+ cwz * shadowW2S[2].Z + shadowW2S[2].W;
				W3XShadowDiag("[W3X_SHDW]   CAST-POS world=(%.1f,%.1f,%.1f) sunNdcZ=%.3f (outside [0,1] = CLIPPED)\n",
					cwx, cwy, cwz, sndcZ);
			}
		}
	} else {
		// ISOLATION BUILD: 3 bursts, >=10s apart (was a process one-shot) - shows
		// WHEN W3X objects first reach a forward/g-buffer render and with what
		// shadow bindings, alongside the periodic CAST bursts.
		static int s_recvDiagN = 0;
		static unsigned s_recvDiagLast = 0;
		if (s_recvDiagN < 3 && GetTickCount() - s_recvDiagLast >= 10000) {
			s_recvDiagN++;
			s_recvDiagLast = GetTickCount();
			D3DXHANDLE hHas = effect->GetParameterByName(NULL, "HasShadow");
			D3DXHANDLE hMap = effect->GetParameterByName(NULL, "ShadowMap");
			D3DXHANDLE hTexel = effect->GetParameterByName(NULL, "Shadowmap_Zero_Zero_OneOverMapSize_OneOverMapSize");
			W3XShadowDiag("[W3X_SHDW] RECEIVE '%s' deferred=%p avail=%d receive=%d\n",
				m_name, (void*)g_theW3DDeferredRenderer,
				(g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()) ? 1 : 0,
				receiveShadow ? 1 : 0);
			W3XShadowDiag("[W3X_SHDW]   params HasShadow=%p ShadowMap=%p SMtexel=%p shadowTex=%p\n",
				(void*)hHas, (void*)hMap, (void*)hTexel, (void*)shadowTex);
			W3XShadowDiag("[W3X_SHDW]   W2S r0=(%.3f,%.3f,%.3f,%.3f) r3=(%.3f,%.3f,%.3f,%.3f)\n",
				shadowW2S[0].X, shadowW2S[0].Y, shadowW2S[0].Z, shadowW2S[0].W,
				shadowW2S[3].X, shadowW2S[3].Y, shadowW2S[3].Z, shadowW2S[3].W);
			// DECISIVE: the receive shadowW2S must be the SAME sunVP*bias the CAST
			// wrote depth with (same frame). If receive's rawVP r3 differs from the
			// CAST_RAW r3 logged earlier, cast and receive used different sun cameras
			// -> surface shadow UV shifts as the camera orbits = the "sweep".
			if (g_theW3DDeferredRenderer) {
				const Matrix4x4 &rVP = g_theW3DDeferredRenderer->getShadowViewProj();
				W3XShadowDiag("[W3X_SHDW]   RECV_RAW getShadowViewProj r0=(%.4f,%.4f,%.4f,%.4f) r3=(%.4f,%.4f,%.4f,%.4f)\n",
					rVP[0].X, rVP[0].Y, rVP[0].Z, rVP[0].W,
					rVP[3].X, rVP[3].Y, rVP[3].Z, rVP[3].W);
			}
		}
	}

	// RA3 shaders use World (object→world) and ViewProjection (world→clip);
	// some shaders take WorldViewProj directly. Set whichever the effect
	// exposes, overriding the DX8Wrapper-based bindings (which return
	// identity for World in this render context). Pass the shadow-map receive
	// state (HasShadow / ShadowMap texture) — only bound in the main pass.
	BindW3XMatrices(effect, world, vp, wvp, shadowW2S, m_recolorHex, receiveShadow, shadowTex);

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

	// Compose per-bone turret/barrel control (Control_Bone) into a temp WorldBones
	// copy: bind pose quat replaced by the controlled rotation for captured bones.
	// The bind pose in m_bones is preserved so Control_Bone is additive per frame.
	//
	// Turret->barrel cascade: when a bone is controlled (turret yaw), its children
	// (barrel, muzzle) must rotate with it. Since m_bones stores the accumulated
	// object-local WorldBones, we re-derive each child's relative local transform
	// from the bind pose and re-compose it under the parent's NEW rotation:
	//   child_new = parent_new * (parent_base^-1 * child_base)
	// Because bone indices are parent-before-child (loadHierarchy guarantees
	// parentIndex < i), a forward 0..n pass updates parents before children.
	float ctrlBones[kMaxBones * 8];
	const float *srcBones = m_bones;
	if (m_boneCount > 0 && m_bones) {
		composeControlledBones(ctrlBones);
		srcBones = ctrlBones;
	}

	// Upload bones (WorldBones)
	BindW3XBones(effect, (float*)srcBones, m_boneCount, m_worldTransform);

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

	// Select the technique for THIS pass. In the deferred shadow-map (CAST) pass
	// the W3X must rasterize SUN-SPACE DEPTH, exactly like RA3's _CreateShadowMap
	// technique: VS_H_11skin outputs o.Position = mul(World, ViewProjection) with
	// ViewProjection = sun VP, and PS_ShadowDepth returns clip z/w as a grayscale
	// written into the shadow COLOR RT (the W3X W2S matrix is the raw sun VP). In
	// the main pass use the Default technique (index 0, full PBR). Previously the
	// code forced Default unconditionally, so the cast wrote PBR color that CWE=0
	// suppressed — the shadow COLOR RT stayed empty (white) and receive sampled
	// wrong depth -> the blocky "rectangle-tile" overlay on surfaces.
	D3DXHANDLE hTech = NULL;
	if (inShadowPass) {
		// ISOLATION BUILD (2026-09-05): cast path reverted to the 9/3-proven
		// shape - ShadowDepth lookup ONLY, set ONCE per object. The crashing run
		// correlated exactly with the first W3X shadow-map cast: that frame's
		// shadow pass exploded to 1.7s (the new per-submesh SetTechnique+
		// ValidateTechnique runs D3DX state-block validation per sub-mesh) and
		// the next Forward pass AV'd inside the device wrapper. This restore
		// removes both the 9/4 _CreateShadowMap pre-loop lookup and the
		// per-sub-mesh re-selection; the infantry fx cast technique is inert
		// until the crash is isolated. NOTE re-adding later: skip override
		// sub-meshes lacking a cast technique (the stale-technique pollution
		// bug) and call ValidateTechnique once, never per sub-mesh.
		hTech = effect->GetTechniqueByName("ShadowDepth");
		// RA3-faithful fallback (_CreateShadowMap, PBR5-10-objects-ARPBR.FX
		// naming; w3x_infantry.fx carries a 1-bone skinned cast with this name -
		// Objects.fxh arrays only 0/1-bone cast VS, min(NumJointsPerVertex,1)).
		// ONE lookup + ONE Set/Validate per OBJECT here (the 2026-09-05 crash
		// run proved a per-SUB-MESH ValidateTechnique explodes the shadow pass:
		// D3DX state-block validation is milliseconds x every sub-mesh).
		if (!hTech) hTech = effect->GetTechniqueByName("_CreateShadowMap");
		if (!hTech) return;	// no shadow-depth cast technique -> skip cast entirely
	} else {
		hTech = effect->GetTechnique(0);	// Default
	}
	if (hTech) { effect->SetTechnique(hTech); effect->ValidateTechnique(hTech); }
	effect->CommitChanges();

	// Shadow-map (CAST) pass: save the raw-device states this draw overrides and
	// restore them after the loop, so NOTHING leaks to the next object in the
	// shadow-pass loop (buildings/dozer) — the documented cause of the earlier
	// shadow-map corruption / War-Factory crash. D3DXEffect Begin/End restores the
	// technique's own render states; these are the manual states set after
	// BeginPass (CullMode, AlphaTest, ...) that bypass DX8Wrapper's state cache.
	D3DVIEWPORT9 savedVP;
	DWORD savedZen = 1, savedCull = 1, savedScissor = 0, savedStencil = 0;
	DWORD savedAlphaBlend = 0, savedAlphaTest = 0, savedAlphaRef = 0, savedAlphaFunc = 8;
	DWORD savedZWrite = 1;
	DWORD savedCWE = 0;
	IDirect3DVertexBuffer9 *savedStream = NULL;
	UINT savedStreamOffset = 0, savedStreamStride = 0;
	IDirect3DIndexBuffer9 *savedIndices = NULL;
	IDirect3DVertexDeclaration9 *savedDecl = NULL;
	if (inShadowPass && dev9) {
		dev9->GetViewport(&savedVP);
		dev9->GetRenderState(D3DRS_ZENABLE, &savedZen);
		dev9->GetRenderState(D3DRS_CULLMODE, &savedCull);
		dev9->GetRenderState(D3DRS_SCISSORTESTENABLE, &savedScissor);
		dev9->GetRenderState(D3DRS_STENCILENABLE, &savedStencil);
		dev9->GetRenderState(D3DRS_ALPHABLENDENABLE, &savedAlphaBlend);
		dev9->GetRenderState(D3DRS_ALPHATESTENABLE, &savedAlphaTest);
		dev9->GetRenderState(D3DRS_ALPHAREF, &savedAlphaRef);
		dev9->GetRenderState(D3DRS_ALPHAFUNC, &savedAlphaFunc);
		dev9->GetRenderState(D3DRS_ZWRITEENABLE, &savedZWrite);
		// The deferred shadow pass sets CWE=0 globally (so W3D objects that don't
		// know the shadow map don't paint color into the shadow COLOR RT). The W3X
		// CAST must write its depth grayscale INTO that COLOR RT (RA3 ShadowMaker
		// style), so save the CWE and re-enable RED channel for our draw; restore
		// afterwards so the W3D objects in the same pass stay color-suppressed.
		dev9->GetRenderState(D3DRS_COLORWRITEENABLE, &savedCWE);
		dev9->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);	// keep cache in sync
		// Geometry bindings are NOT in D3DXEffect's saved state block (only the
		// VS/PS/decl/FVF are), so the W3X cast's stream source / indices would
		// otherwise leak to the NEXT object in the shadow loop — the classic
		// DX8Wrapper-desync crash. Save them explicitly and restore below.
		dev9->GetStreamSource(0, &savedStream, &savedStreamOffset, &savedStreamStride);
		dev9->GetIndices(&savedIndices);
		dev9->GetVertexDeclaration(&savedDecl);
		{ static bool s_castStateDiag = false;
			if (!s_castStateDiag) {
				s_castStateDiag = true;
				W3XShadowDiag("[W3X_SHDW]   CAST states saved: zen=%u zwrite=%u cull=%u atest=%u stream=%p ib=%p decl=%p\n",
					savedZen, savedZWrite, savedCull, savedAlphaTest,
					(void*)savedStream, (void*)savedIndices, (void*)savedDecl);
			}
		}
	}

	UINT totalFailed = 0;
	for (size_t si = 0; si < m_meshes.size(); si++) {
		SubMesh &sm = m_meshes[si];
		if (!sm.vb || !sm.ib) continue;

		// Shadow-map (CAST) pass: skip the genuinely-transparent meshes, matching
		// the volumetric shadow's exclusions (GetSubMeshAlphaTest/IsMuzzleflash):
		//  - muzzleflash/rotor meshes (large flat quads) would cast a solid square
		//    into the map and swallow the fuselage shadow.
		//  - objectsgeneric lattice (wire-fence mesh) would cast a wire-grate
		//    pattern that shows up as a net shadow on other W3X models (the 发电厂/
		//    指挥中心 fence shadows). The fence renders transparent in the main
		//    pass, so it casting no shadow at all is the correct behavior.
		//  - ANY alpha-cutout lattice mesh (SKIN_G00 with AlphaTestEnable=true on
		//    发电厂/兵营/指挥中心/战争工厂 etc.): in the cast it would alpha-test,
		//    discarding the lattice gaps and writing a wire-mesh pattern into the
		//    shadow map that every W3X surface samples as the "net" overlay. Skip
		//    them entirely — the solid bodies (NOT alpha-tested) still cast. Only
		//    the fence/grille meshes carry AlphaTestEnable=true (verified in the
		//    .w3x), so this does not remove any solid-body shadow.
		if (inShadowPass) {
			const AsciiString &os = sm.origShader;
			const char *smeshName = sm.name.str();
			// A mesh is excluded from writing into the shadow map (it casts a
			// "transparent" shadow = no solid silhouette / no wire-grate pattern)
			// when it is:
			//  - muzzleflash/rotor meshes (large flat quads -> solid square).
			//  - ANY SKIN_G* grille/lattice mesh. Building grilles are authored as
			//    open wireframe geometry (SKIN_G00..G03 on APACONSTRUCTIONYARD,
			//    EUWARFACTORY, etc.) — their lattice must NOT rasterize into the
			//    shadow map or every receive-sampling surface picks up the
			//    "wire-mesh" overlay. This is the ONLY reliable marker: grilles may
			//    use EITHER objectsgeneric.fx (APACONSTRUCTIONYARD.SKIN_G00) OR
			//    buildingsallied.fx (EUWARFACTORY.SKIN_G01), and BOTH shaders are
			//    ALSO used by solid BODY meshes (humvee body/wheels use
			//    objectsgeneric.fx, factory body uses buildingsallied.fx), so a
			//    shader-name test can't distinguish them. BODY meshes (SKIN_BODY*)
			//    stay solid and keep casting.
			//  NOTE: objectsgeneric.fx must NOT be excluded by shader name alone —
			//    it is the RA3 generic-object shader used by solid vehicle bodies
			//    (EUTEVHUMVEE SKIN_BODY02 / SKIN_WHEEL), not just wire fences.
			bool isTransparent = !os.isEmpty()
				&& (strstr(os.str(), "muzzle") != NULL);
			if (!isTransparent) {
				// RA3-faithful (user 2026-09-04: RA3 does NOT name-exclude): a
				// sub-mesh whose material declares AlphaTestEnable=true is an
				// alpha-cutout lattice (SKIN_G00 grille: texture alpha ALL below
				// the 0.375 cast clip - measured on TasCC3_G.dds) and must not
				// rasterize into the shadow map. The solid meshes declare it false
				// (SKIN_BODY*) or not at all (SKIN_G01-03 posts/frames = 立柱/框线)
				// and keep casting - exactly 只投立柱+框线、网面无影.
				for (size_t ci = 0; ci < sm.constants.size(); ci++) {
					const W3XShaderConstant &cc = sm.constants[ci];
					if (cc.type == W3X_CONSTANT_BOOL
						&& strcmp(cc.name.str(), "AlphaTestEnable") == 0) {
						if (cc.boolValue) isTransparent = true;
						break;
					}
				}
			}
			// DIAG one-shot (first 12 sub-meshes, any object): dump each sub-mesh's
			// name + origShader + skip state so we can CONFIRM the SKIN_G* grilles
			// are excluded while BODY/other solid meshes still cast.
			{
				static int s_castMeshDiag = 0;
				if (s_castMeshDiag < 12) {
					s_castMeshDiag++;
					W3XShadowDiag("[W3X_SHDW] CAST_MESH obj='%s' mesh='%s' fx='%s' origShader='%s' skip=%d consts=%d\n",
						m_name, smeshName ? smeshName : "(none)",
						sm.fxName.isEmpty() ? "(shared)" : sm.fxName.str(), os.str(),
						isTransparent ? 1 : 0, (int)sm.constants.size());
				}
			}
			if (isTransparent) {
				continue;
			}
		}

		// Per-sub-mesh shader override: a sub-mesh with its own fxName uses a
		// different effect (e.g. w3x_tread.fx for scrolling treads). Bind its
		// technique + all shared render params (matrices, engine constants,
		// bones, aux samplers, constants) — each D3DXEffect keeps its own values.
		ID3DXEffect *drawEffect = effect;
		if (!sm.fxName.isEmpty() && sm.fxName.compare(m_fxName.str()) != 0) {
			drawEffect = W3XEffectManager::Instance()->GetEffect(sm.fxName.str());
			if (drawEffect) {
				// Technique first, then bind all shared params. MAIN pass uses the
				// sub-mesh's authored technique. CAST pass: the override effect
				// needs its OWN cast technique - SetTechnique ONLY (never
				// ValidateTechnique here; the per-sub-mesh D3DX state-block
				// validation is what exploded the shadow pass to 1.7s in the
				// 2026-09-05 crash run). No cast technique -> SKIP the sub-mesh:
				// drawing it with the effect's stale main-pass technique would
				// write PBR color into the RED-only shadow COLOR RT (the
				// blocky-overlay pollution).
				D3DXHANDLE hTech = NULL;
				if (inShadowPass) {
					hTech = drawEffect->GetTechniqueByName("ShadowDepth");
					if (!hTech) hTech = drawEffect->GetTechniqueByName("_CreateShadowMap");
					if (!hTech) { continue; }	// muzzle/tread overrides cast nothing
				} else {
					hTech = drawEffect->GetTechnique(sm.technique);
				}
				if (hTech) {
					drawEffect->SetTechnique(hTech);
					if (!inShadowPass) drawEffect->ValidateTechnique(hTech);	// main pass keeps the 9/3 validation
				}
				// Engine constants (sun/ambient/camera) from the real scene camera
				W3XEffectManager::Instance()->BindEngineConstants(drawEffect, rinfo);
				// Object->world->clip matrices + ShadowMapWorldToShadow + RecolorColor
				// (+ shadow-map receive state for per-sub-mesh override effects)
				BindW3XMatrices(drawEffect, world, vp, wvp, shadowW2S, m_recolorHex, receiveShadow, shadowTex);
				// This sub-mesh's shader constants (textures etc.)
				BindW3XConstants(drawEffect, sm.constants);
				// WorldBones + NumJointsPerVertex for this effect (skinned mesh).
				// Use the COMPOSED bones (srcBones = the turret-control + animation
				// composed array, or the base bind when nothing is active) so an
				// animated sub-mesh with an override shader (e.g. a tread) doesn't
				// skin at the stale bind pose.
				BindW3XBones(drawEffect, (float*)srcBones, m_boneCount, m_worldTransform);
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

		// ISOLATION BUILD (2026-09-05): the per-sub-mesh cast technique
		// re-selection (SetTechnique + ValidateTechnique per draw) is REMOVED -
		// see the cast gate comment above. The object-wide technique set before
		// the loop governs every sub-mesh, exactly like the 9/3-proven path.
		// KNOWN REGRESSION re-introduced until re-add: an override sub-mesh whose
		// effect lacks a cast technique draws with its stale main-pass technique
		// (the shadow COLOR RT pollution). Re-add with: skip such sub-meshes, and
		// ValidateTechnique once per object only.

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

		// Soft-bound sub-mesh: select the 2-bone soft-skin technique (DefaultSoft =
		// VS_H_Unified(2) in w3x_infantry.fx) and set NumJointsPerVertex=2. Non-soft
		// sub-meshes reset to the 1-bone Default technique / hard-skin joints.
		// MAIN PASS ONLY — in the shadow-map CAST pass the technique was already
		// set to ShadowDepth above (RA3-style depth-to-COLOR-RT); re-selecting
		// Default here would throw away the cast depth and write PBR color (that
		// CWE=RED then corrupts the shadow COLOR RT -> empty/white map -> the
		// "shadow sweeps / uniform shadow" artifacts).
		if (!inShadowPass)
		{
			const char *techName = sm.softBinding ? "DefaultSoft" : "Default";
			D3DXHANDLE hTech = drawEffect->GetTechniqueByName(techName);
			HRESULT vres = (hTech) ? drawEffect->ValidateTechnique(hTech) : E_FAIL;
			if (hTech) drawEffect->SetTechnique(hTech);
			D3DXHANDLE hJoints = drawEffect->GetParameterByName(NULL, "NumJointsPerVertex");
			if (hJoints) drawEffect->SetInt(hJoints, sm.softBinding ? 2 : (m_boneCount > 0 ? 1 : 0));
			drawEffect->CommitChanges();
#if defined(DEBUG_LOGGING)
			// DIAG: confirm the soft path actually engages (technique/stride/decl).
			{
				static bool s_softDiag = false;
				if (!s_softDiag && sm.softBinding) {
					s_softDiag = true;
					IDirect3DVertexDeclaration9 *sd = W3XGetSoftVertexDecl(dev9);
					DEBUG_LOG(("[W3X_SOFT] submesh[%d] soft=1 tech='%s' hTech=%p valid=0x%08X "
						"stride=%d softDecl=%p bones=%d\n",
						(int)si, techName, (void*)hTech, (int)vres,
						(int)sizeof(W3XSoftVertex), (void*)sd, m_boneCount));
					// Are the composed bones the ANIMATED pose? props1=bone20 should be
					// at chest (~4.6,0.2,10.9 in SBIDA), hips=bone2 at (~-0.1,-0.9,10.4),
					// leftfoot=bone16 at (~-1.2,-1.8,1.1). If bone20 sits near origin or
					// all zeros, the skinning data is broken -> unskinned pile.
					if (m_boneCount > 20 && srcBones) {
						const float *cb = srcBones;
						DEBUG_LOG(("[W3X_SOFT3] composed bone20=(%.2f,%.2f,%.2f) bone2=(%.2f,%.2f,%.2f) "
							"bone16=(%.2f,%.2f,%.2f)\n",
							cb[20*8+4], cb[20*8+5], cb[20*8+6],
							cb[2*8+4], cb[2*8+5], cb[2*8+6],
							cb[16*8+4], cb[16*8+5], cb[16*8+6]));
						// Skin the first launcher vertex (v551: pos0=(13.24,-0.78,1.91),
						// bone0=20). Engine-computed result must equal the sim (~chest).
						float v[3] = { 13.24f, -0.78f, 1.91f };
						float r[3];
						W3XQuatRotateVector(r, &cb[20*8], v);
						DEBUG_LOG(("[W3X_SOFT3] launcher v551 skinned(bone20)=(%.2f,%.2f,%.2f)\n",
							r[0]+cb[20*8+4], r[1]+cb[20*8+5], r[2]+cb[20*8+6]));
					}
				}
			}
#else
			(void)vres;	// keep C4189 away when DEBUG_LOG is compiled out
#endif
		}

		// Soft-bound sub-meshes use the 128-byte W3XSoftVertex format (dual
		// position/normal/tangent/binormal + two blend indices + blend weight);
		// everything else uses the 76-byte W3XVertex.
		const int vStride = sm.softBinding ? (int)sizeof(W3XSoftVertex) : 76;
		IDirect3DVertexDeclaration9 *vDecl = sm.softBinding ? W3XGetSoftVertexDecl(dev9) : decl;
		HRESULT ssRes = dev9->SetStreamSource(0, sm.vb, 0, vStride);
		dev9->SetIndices(sm.ib);
		HRESULT vdRes = vDecl ? dev9->SetVertexDeclaration(vDecl) : E_FAIL;
#if defined(DEBUG_LOGGING)
		// DIAG: did dgVoodoo accept the soft declaration? If SetVertexDeclaration
		// fails, the device keeps the previous (76-byte) declaration and reads the
		// 136-byte soft buffer wrong -> unskinned pile.
		{
			static bool s_vdDiag = false;
			if (!s_vdDiag && sm.softBinding) {
				s_vdDiag = true;
				DEBUG_LOG(("[W3X_SOFT6] SetStreamSource(soft)=0x%08X stride=%d SetVertexDeclaration=0x%08X\n",
					(int)ssRes, vStride, (int)vdRes));
			}
		}
#else
		(void)ssRes; (void)vdRes;	// keep C4189 away when DEBUG_LOG is compiled out
#endif

		UINT passes;
		HRESULT hr = drawEffect->Begin(&passes, 0);
		if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] Begin FAILED hr=0x%08X\n", (int)si, (int)hr)); totalFailed++; continue; }
		// CAST DIAG (bounded): prove the cast executes end-to-end and with which
		// technique/ps variant - a missing shadow here shows a failed stage here.
		if (inShadowPass) {
			static int s_castDrawDiag = 0;
			if (s_castDrawDiag < 10) {
				s_castDrawDiag++;
				D3DXHANDLE hATd = drawEffect->GetParameterByName(NULL, "AlphaTestEnable");
				BOOL atv = FALSE;
				if (hATd) drawEffect->GetBool(hATd, &atv);
				const char *techName = "?";
				D3DXHANDLE hCurTech = drawEffect->GetCurrentTechnique();
				D3DXTECHNIQUE_DESC tdesc;
				if (hCurTech && SUCCEEDED(drawEffect->GetTechniqueDesc(hCurTech, &tdesc)) && tdesc.Name)
					techName = tdesc.Name;
				W3XShadowDiag("[W3X_SHDW] CAST-DRAW '%s' mesh='%s' tech='%s' passes=%u atParam=%d vDecl=%p\n",
					m_name, sm.name.str() ? sm.name.str() : "?", techName, passes, atv ? 1 : 0, (void*)vDecl);
			}
		}
		for (UINT p = 0; p < passes; p++) {
			hr = drawEffect->BeginPass(p);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] BeginPass(%u) FAILED hr=0x%08X\n", (int)si, p, (int)hr)); break; }
			// CRITICAL FIX: re-bind vertex data AFTER BeginPass. D3DX9 Begin/BeginPass
			// may reset the stream source / declaration / index buffer to defaults,
			// leaving the draw with no valid vertex data -> 0 pixels.
			dev9->SetStreamSource(0, sm.vb, 0, vStride);
			dev9->SetIndices(sm.ib);
			if (vDecl) dev9->SetVertexDeclaration(vDecl);
			// W3X triangles are CW; the RA3 technique sets CullMode=2 (culls CW),
			// so disable culling AFTER BeginPass (BeginPass re-applies the pass
			// render states and would otherwise undo this).
			dev9->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			// CAST: re-enable RED channel color-write AFTER BeginPass — D3DX9
			// BeginPass re-applies the pass's render states, which would otherwise
			// restore the deferred shadow pass's CWE=0 and the ShadowDepth PS's
			// depth grayscale would never reach the shadow COLOR RT (stays white).
			// Must match RA3: the shadow map COLOR RT is written by the cast PS.
			if (inShadowPass) {
				dev9->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
				DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
			}
			// Per-mesh alpha test: meshes whose material declares
			// AlphaTestEnable=true (alpha-cutout textures - wire fences,
			// helicopter rotor blades) discard transparent pixels in the
			// rasterizer. BeginPass re-applies the pass render states, so set
			// it AFTER BeginPass (same as CullMode); the next sub-mesh's
			// BeginPass resets it from the technique. Also drive the shader's
			// `if(AlphaTestEnable) clip(extra_alpha-0.375)` (RA3 PBR cutout) by
			// re-setting the uniform AFTER BeginPass - the technique's
			// "AlphaTestEnable = 0" state assignment would otherwise reset it.
			{
				// Muzzleflash meshes (w3x_muzzle.fx: rotor blades/engine fans) never
				// alpha-test - their Additive/Multiply blend handles transparency,
				// and their MultiTextureEnable means "sample the texture twice with
				// the transposed UV" (RA3 muzzleflash), NOT an alpha-cutout. Forcing
				// the test would clip the rotor on its near-zero texture alpha, so
				// leave the technique's AlphaTestEnable=0 standing.
				// In the shadow-map CAST pass (ShadowDepth technique) the mesh must
				// write its full sun-depth silhouette into the COLOR RT — NO device
				// alpha test, matching RA3's _CreateShadowMap pass (AlphaTestEnable=0).
				// The building/vehicle BODY sub-meshes declare AlphaTestEnable=true for
				// small transparent details (glass, vents) but are essentially SOLID;
				// enabling the rasterizer alpha test in the cast would discard most of
				// their pixels and leave the shadow COLOR RT empty (the all-white dump
				// we saw -> receive samples empty depth -> shadow sweeps across the
				// model as the camera orbits). Alpha test is a MAIN-pass concern only.
				bool alphaTest = false;
				if (!inShadowPass) {
					bool isMuzzleflash = (!sm.fxName.isEmpty()
						&& strcmp(sm.fxName.str(), "Shaders\\RA3\\w3x_muzzle.fx") == 0);
					if (!isMuzzleflash) {
						for (size_t ci = 0; ci < sm.constants.size() && !alphaTest; ci++) {
							const W3XShaderConstant &cc = sm.constants[ci];
							if (cc.type == W3X_CONSTANT_BOOL
								&& strcmp(cc.name.str(), "AlphaTestEnable") == 0) {
								alphaTest = cc.boolValue;
							}
							// RA3 blade/rotor material marker: MultiTextureEnable=true
							// (the rotor meshes declare Texture_0 + MultiTextureEnable +
							// TexCoordTransform, no AlphaTestEnable). Treat it as an
							// alpha-cutout so the Fx_blades alpha gaps are clipped.
							if (cc.type == W3X_CONSTANT_BOOL
								&& strcmp(cc.name.str(), "MultiTextureEnable") == 0) {
								if (cc.boolValue) alphaTest = true;
							}
						}
					}
				}
				if (alphaTest) {
					dev9->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
					dev9->SetRenderState(D3DRS_ALPHAREF, 0x80);          // >= 0.5 alpha
					dev9->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
					D3DXHANDLE hAT = drawEffect->GetParameterByName(NULL, "AlphaTestEnable");
					if (hAT) drawEffect->SetBool(hAT, TRUE);
				} else {
					dev9->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
					D3DXHANDLE hAT = drawEffect->GetParameterByName(NULL, "AlphaTestEnable");
					if (hAT) drawEffect->SetBool(hAT, FALSE);
				}
			}
			hr = dev9->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sm.vertexCount, 0, sm.triangleCount);
			if (FAILED(hr)) { DEBUG_LOG(("[W3X_P5]   submesh[%d] DrawIndexedPrimitive FAILED hr=0x%08X (vc=%d tc=%d)\n", (int)si, (int)hr, sm.vertexCount, sm.triangleCount)); totalFailed++; }
			drawEffect->EndPass();
		}
		drawEffect->End();
	}

	// Shadow-map (CAST) pass: restore the raw-device states saved above so the
	// next object in the shadow-pass loop renders with its own state.
	if (inShadowPass && dev9) {
		dev9->SetViewport(&savedVP);
		dev9->SetRenderState(D3DRS_ZENABLE, savedZen);
		dev9->SetRenderState(D3DRS_CULLMODE, savedCull);
		dev9->SetRenderState(D3DRS_SCISSORTESTENABLE, savedScissor);
		dev9->SetRenderState(D3DRS_STENCILENABLE, savedStencil);
		dev9->SetRenderState(D3DRS_ALPHABLENDENABLE, savedAlphaBlend);
		dev9->SetRenderState(D3DRS_ALPHATESTENABLE, savedAlphaTest);
		dev9->SetRenderState(D3DRS_ALPHAREF, savedAlphaRef);
		dev9->SetRenderState(D3DRS_ALPHAFUNC, savedAlphaFunc);
		dev9->SetRenderState(D3DRS_ZWRITEENABLE, savedZWrite);
		// Restore the shadow pass's color-write suppression (CWE=0 set globally by
		// beginShadowMapPass). The W3X cast temporarily enabled RED to write depth
		// grayscale into the shadow COLOR RT; the next W3D object in the pass must
		// stay color-suppressed so it doesn't paint into that RT.
		dev9->SetRenderState(D3DRS_COLORWRITEENABLE, savedCWE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, savedCWE);
		dev9->SetStreamSource(0, savedStream, savedStreamOffset, savedStreamStride);
		dev9->SetIndices(savedIndices);
		dev9->SetVertexDeclaration(savedDecl);
		// Release the refs acquired by GetStreamSource/GetIndices/GetVertexDeclaration.
		if (savedStream) savedStream->Release();
		if (savedIndices) savedIndices->Release();
		if (savedDecl) savedDecl->Release();
	}

	// NOTE: do NOT reset device state here. The W3X render uses the raw D3D9
	// device directly (bypassing DX8Wrapper's state cache); resetting state
	// here desyncs DX8Wrapper and breaks subsequent regular renders.

	if (totalFailed > 0) {
		DEBUG_LOG(("[W3X_P5]   %u submesh(es) FAILED (of %d)\n", totalFailed, (int)m_meshes.size()));
	}
}
