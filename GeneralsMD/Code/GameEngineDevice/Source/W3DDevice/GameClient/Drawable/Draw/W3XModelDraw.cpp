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
#include "W3DDevice/GameClient/Module/W3XModelDraw.h"
#include "W3DDevice/GameClient/W3XEffectManager.h"
#include "W3DDevice/GameClient/W3XRenderObj.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3dformat.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "INI.h"
#include "Common/Xfer.h"
#include "refcount.h"
#include "wwdebug.h"
#include "wwmemlog.h"
#include <d3d9.h>
#include <d3dx9effect.h>

//=============================================================================
// W3XModelDrawModuleData
//=============================================================================

W3XModelDrawModuleData::W3XModelDrawModuleData() {}
W3XModelDrawModuleData::~W3XModelDrawModuleData() {}

const W3XConditionInfo *W3XModelDrawModuleData::findBestConditionState(const ModelConditionFlags &c) const
{
	return m_conditionStateMap.findBestInfo(m_conditionStates, c);
}

void W3XModelDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	ModuleData::buildFieldParse(p);
	static const FieldParse myFieldParse[] = {
		{ "ConditionState", W3XModelDrawModuleData::parseConditionState, NULL, 0 },
		{ "DefaultModelName", INI::parseAsciiString, NULL, offsetof(W3XModelDrawModuleData, m_defaultModelName) },
		{ 0, 0, 0, 0 }
	};
	p.add(myFieldParse);
}

void W3XModelDrawModuleData::parseConditionState(INI* ini, void *instance, void *, const void *)
{
	// Sub-field parse table for a ConditionState block
	static const FieldParse myFieldParse[] = {
		{ "Model", INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_modelName) },
		{ 0, 0, 0, 0 }
	};

	W3XModelDrawModuleData *self = (W3XModelDrawModuleData *)instance;
	W3XConditionInfo info;
	ModelConditionFlags flags;
	flags.parse(ini, NULL);
	info.m_conditionsYesVec.push_back(flags);
	ini->initFromINI(&info, myFieldParse);
	self->m_conditionStates.push_back(info);
}

//=============================================================================
// W3XModelDraw
//=============================================================================

W3XModelDraw::W3XModelDraw(Thing *thing, const ModuleData *moduleData) :
	DrawModule(thing, moduleData), m_curState(NULL), m_fullyObscuredByShroud(false),
	m_renderObj(NULL)
{
	m_loadedModel.boneMatrixArray = NULL;
	m_loadedModel.valid = false;
	DEBUG_LOG(("[W3X_P5] W3XModelDraw created\n"));
}

W3XModelDraw::~W3XModelDraw()
{
	removeRenderObject();
	releaseModelData(m_loadedModel);
}
void W3XModelDraw::crc(Xfer *xfer) { DrawModule::crc(xfer); }
void W3XModelDraw::xfer(Xfer *xfer) { XferVersion v=1, cv=v; xfer->xferVersion(&v, cv); DrawModule::xfer(xfer); }
void W3XModelDraw::loadPostProcess() { DrawModule::loadPostProcess(); }

void W3XModelDraw::preloadAssets(TimeOfDay timeOfDay)
{
	const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
	if (!md || md->m_defaultModelName.isEmpty()) return;
	LoadedModelData tmp;
	if (loadW3XModel(md->m_defaultModelName.str(), tmp)) {
		releaseModelData(m_loadedModel);
		m_loadedModel = tmp;
		m_loadedModelName = md->m_defaultModelName;
		createRenderObject(m_loadedModel);
	}
}

//-----------------------------------------------------------------------------
// Quaternion helpers for bone skinning (RA3 WorldBones format)
//-----------------------------------------------------------------------------
static void QuatMultiply(float *out, const float *a, const float *b)
{
	// out = a * b (Hamilton product)
	out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
	out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
	out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
	out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

static void QuatRotateVector(float *out, const float *q, const float *v)
{
	// out = rotate v by quaternion q
	float t[3];
	// t = 2 * cross(q.xyz, v)
	t[0] = 2.0f * (q[1]*v[2] - q[2]*v[1]);
	t[1] = 2.0f * (q[2]*v[0] - q[0]*v[2]);
	t[2] = 2.0f * (q[0]*v[1] - q[1]*v[0]);
	// out = v + q.w*t + cross(q.xyz, t)
	out[0] = v[0] + q[3]*t[0] + (q[1]*t[2] - q[2]*t[1]);
	out[1] = v[1] + q[3]*t[1] + (q[2]*t[0] - q[0]*t[2]);
	out[2] = v[2] + q[3]*t[2] + (q[0]*t[1] - q[1]*t[0]);
}

//-----------------------------------------------------------------------------
// loadHierarchy: parse .skl.w3x skeleton, compute base-pose bone transforms
// in RA3 WorldBones format (quaternion + offset, 2 float4 per bone).
//-----------------------------------------------------------------------------
bool W3XModelDraw::loadHierarchy(const char *sklName, LoadedModelData &data)
{
	if (!sklName || !sklName[0]) return false;

	// Construct .w3x filename
	char path[512];
	sprintf(path, "%s.w3x", sklName);

	std::vector<W3XBoneInfo> bones;
	if (!W3XLoader::ParseHierarchy(path, bones)) {
		DEBUG_LOG(("[W3X_P5] loadHierarchy: failed to parse '%s'\n", path));
		return false;
	}

	data.boneCount = (int)bones.size();
	DEBUG_LOG(("[W3X_P5] loadHierarchy: '%s' -> %d bones\n", path, data.boneCount));

	// Allocate WorldBones array: 2 float4 per bone (quat + offset) = 8 floats per bone
	if (data.boneMatrixArray) delete[] data.boneMatrixArray;
	data.boneMatrixArray = new float[data.boneCount * 8];
	memset(data.boneMatrixArray, 0, data.boneCount * 8 * sizeof(float));

	// Compute base-pose world transforms (quaternion + offset)
	for (int i = 0; i < data.boneCount; i++) {
		const W3XBoneInfo &b = bones[i];
		float lq[4] = { b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3] };
		float lo[3] = { b.translation[0], b.translation[1], b.translation[2] };

		// WorldBones slots for this bone
		float *wq = &data.boneMatrixArray[i * 8 + 0];	// quaternion
		float *wo = &data.boneMatrixArray[i * 8 + 4];	// offset + alpha

		if (b.parentIndex >= 0 && b.parentIndex < i) {
			// Concatenate with parent: world = parent_world * local
			float *pq = &data.boneMatrixArray[b.parentIndex * 8 + 0];
			float *po = &data.boneMatrixArray[b.parentIndex * 8 + 4];
			// world_quat = parent_quat * local_quat
			QuatMultiply(wq, pq, lq);
			// world_offset = rotate(parent_offset, local_quat) + local_offset
			float rotatedParent[3];
			QuatRotateVector(rotatedParent, lq, po);
			wo[0] = rotatedParent[0] + lo[0];
			wo[1] = rotatedParent[1] + lo[1];
			wo[2] = rotatedParent[2] + lo[2];
		} else {
			// Root bone: just local transform
			wq[0] = lq[0]; wq[1] = lq[1]; wq[2] = lq[2]; wq[3] = lq[3];
			wo[0] = lo[0]; wo[1] = lo[1]; wo[2] = lo[2];
		}
		wo[3] = 1.0f;	// alpha (bone opacity)
	}

	DEBUG_LOG(("[W3X_P5]   Bone transforms computed OK (quat+offset format)\n"));
	return true;
}

//-----------------------------------------------------------------------------
// uploadBoneMatrices: upload WorldBones array to D3DXEffect
//-----------------------------------------------------------------------------
void W3XModelDraw::uploadBoneMatrices(ID3DXEffect *effect, const LoadedModelData &data)
{
	if (!effect || !data.boneMatrixArray || data.boneCount == 0) return;

	// RA3 shaders use "WorldBones" - a float4 array of quaternion+offset pairs
	D3DXHANDLE hParam = effect->GetParameterByName(NULL, "WorldBones");
	if (!hParam) {
		DEBUG_LOG(("[W3X_P5]   No WorldBones parameter found in effect\n"));
		return;
	}

	// Upload as float4 array (2 float4 per bone)
	effect->SetFloatArray(hParam, data.boneMatrixArray, data.boneCount * 8);
	DEBUG_LOG(("[W3X_P5]   Uploaded %d bones to WorldBones\n", data.boneCount));
}

//-----------------------------------------------------------------------------
// loadW3XModel: Load container + meshes + hierarchy into GPU buffers
//-----------------------------------------------------------------------------
bool W3XModelDraw::loadW3XModel(const char *containerName, LoadedModelData &outData)
{
	WWMEMLOG(MEM_GEOMETRY);
	if (!containerName || !containerName[0]) return false;

	DEBUG_LOG(("[W3X_P5] loadW3XModel('%s')\n", containerName));

	AsciiString hierarchyName;
	std::vector<W3XSubObjectInfo> subObjects;
	char containerPath[512];
	sprintf(containerPath, "%s.w3x", containerName);
	if (!W3XLoader::ParseContainer(containerPath, hierarchyName, subObjects)) {
		DEBUG_LOG(("[W3X_P5]   Failed to parse container '%s'\n", containerPath));
		return false;
	}

	outData.subMeshes.clear();
	outData.constants.clear();
	outData.hierarchyName = hierarchyName;
	outData.boneMatrixArray = NULL;
	outData.boneCount = 0;
	outData.valid = false;

	DEBUG_LOG(("[W3X_P5]   Container parsed, hierarchy=%s, %d sub-objects\n",
		hierarchyName.str(), (int)subObjects.size()));

	// Load skeleton hierarchy (if present)
	if (!hierarchyName.isEmpty()) {
		loadHierarchy(hierarchyName.str(), outData);
	}

	// Get D3D9 device
	IDirect3DDevice8 *dev8 = DX8Wrapper::_Get_D3D_Device8();
	if (!dev8) return false;
	IDirect3DDevice9 *dev9 = static_cast<IDirect3DDevice9*>(dev8);

	const int VERTEX_STRIDE = sizeof(W3XVertex);

	// Load each sub-mesh
	for (size_t si = 0; si < subObjects.size(); si++) {
		const W3XSubObjectInfo &sub = subObjects[si];
		if (sub.isCollisionBox) continue;

		char meshPath[512];
		sprintf(meshPath, "%s.w3x", sub.renderObjectName.str());

		W3XMeshData meshData;
		if (!W3XLoader::ReadMeshData(meshPath, meshData)) continue;

		int vertCount = (int)meshData.vertices.size();
		int triCount = (int)meshData.triangles.size() / 3;
		if (vertCount == 0 || triCount == 0) continue;

		bool hasBones = ((int)meshData.boneIndices.size() >= vertCount);

		// Build vertex array
		W3XVertex *verts = new W3XVertex[vertCount];
		for (int vi = 0; vi < vertCount; vi++) {
			verts[vi].x = meshData.vertices[vi].X;
			verts[vi].y = meshData.vertices[vi].Y;
			verts[vi].z = meshData.vertices[vi].Z;
			verts[vi].nx = vi < (int)meshData.normals.size() ? meshData.normals[vi].X : 0;
			verts[vi].ny = vi < (int)meshData.normals.size() ? meshData.normals[vi].Y : 0;
			verts[vi].nz = vi < (int)meshData.normals.size() ? meshData.normals[vi].Z : 0;
			verts[vi].u = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].U : 0;
			verts[vi].v = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].V : 0;
			// DIAG: dump a few vertices from the first submesh to verify data
			if (si == 0 && vi < 4) {
				DEBUG_LOG(("[W3X_P6]   vert[%d] pos=(%.2f,%.2f,%.2f) nrm=(%.2f,%.2f,%.2f) uv=(%.3f,%.3f) bone=(%.0f,%.2f)\n",
					vi, verts[vi].x, verts[vi].y, verts[vi].z,
					verts[vi].nx, verts[vi].ny, verts[vi].nz,
					verts[vi].u, verts[vi].v,
					verts[vi].boneIdx, verts[vi].boneWeight));
			}
			verts[vi].tx = vi < (int)meshData.tangents.size() ? meshData.tangents[vi].X : 0;
			verts[vi].ty = vi < (int)meshData.tangents.size() ? meshData.tangents[vi].Y : 0;
			verts[vi].tz = vi < (int)meshData.tangents.size() ? meshData.tangents[vi].Z : 0;
			verts[vi].bx = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].X : 0;
			verts[vi].by = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Y : 0;
			verts[vi].bz = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Z : 0;
			// RA3 shader: int BoneIndex = floor(blendindices.x * 2); WorldBones
			// is laid out 2 float4 per bone ([2i]=quat, [2i+1]=offset), so the
			// raw bone index b must be stored AS-IS: floor(b*2)=2b points to the
			// bone's quat slot. Do NOT divide by 2.
			verts[vi].boneIdx = hasBones ? (float)meshData.boneIndices[vi] : 0;
			verts[vi].boneWeight = hasBones && vi < (int)meshData.boneWeights.size() ? meshData.boneWeights[vi] : 1.0f;
			verts[vi].color = 0xFFFFFFFF;	// white vertex color (RA3 shader reads VertexColor)
			verts[vi].u2 = 0; verts[vi].v2 = 0;	// TEXCOORD1 (RA3 texcoordNEW: zero)
		}

		// Build index array
		int indexCount = triCount * 3;
		unsigned short *indices = new unsigned short[indexCount];
		for (int ti = 0; ti < indexCount; ti++)
			indices[ti] = (unsigned short)meshData.triangles[ti];

		// Create vertex buffer
		IDirect3DVertexBuffer9 *vb = NULL;
		HRESULT hr = dev9->CreateVertexBuffer(vertCount * VERTEX_STRIDE, 0, 0, D3DPOOL_MANAGED, &vb, NULL);
		if (SUCCEEDED(hr) && vb) {
			void *ptr; hr = vb->Lock(0, 0, &ptr, 0);
			if (SUCCEEDED(hr)) { memcpy(ptr, verts, vertCount * VERTEX_STRIDE); vb->Unlock(); }
		}
		delete[] verts;

		// Create index buffer
		IDirect3DIndexBuffer9 *ib = NULL;
		hr = dev9->CreateIndexBuffer(indexCount * sizeof(unsigned short), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL);
		if (SUCCEEDED(hr) && ib) {
			void *ptr; hr = ib->Lock(0, 0, &ptr, 0);
			if (SUCCEEDED(hr)) { memcpy(ptr, indices, indexCount * sizeof(unsigned short)); ib->Unlock(); }
		}
		delete[] indices;

		if (!vb || !ib) {
			if (vb) vb->Release();
			if (ib) ib->Release();
			continue;
		}

		SubMeshBuffer subBuf;
		subBuf.vertexBuffer = vb;
		subBuf.indexBuffer = ib;
		subBuf.vertexCount = vertCount;
		subBuf.triangleCount = triCount;
		subBuf.hasBones = hasBones;
		outData.subMeshes.push_back(subBuf);

		DEBUG_LOG(("[W3X_P5]   Sub-mesh '%s': %d verts, %d tris, bones=%d\n",
			sub.renderObjectName.str(), vertCount, triCount, hasBones ? (int)meshData.boneIndices.size() : 0));

		// Use the first non-defaultw3d FX shader (defaultw3d.fx is a placeholder
		// that sub-parts use when they should render with the standard pipeline)
		if (!meshData.fxShaderName.isEmpty() && outData.fxShaderName.isEmpty()
			&& strcmp(meshData.fxShaderName.str(), "defaultw3d.fx") != 0) {
			// REAL RA3 PBR shader via SAS-free override (w3x_soviet.fx compiles
			// VS_H_11skin + PS_H_ARPBR directly; objectssoviet.fx itself uses
			// VS_H_Array[VSchooserExpr()] SAS dynamic selection which this D3DX9
			// build rejects with X3116).
			// (w3x_rastest.fx was the TEMP isolation test that proved the skinned
			// VS + geometry + bones work — red model appeared, turret correct.)
			outData.fxShaderName = "Shaders\\RA3\\w3x_soviet.fx";
			//outData.fxShaderName = "Shaders\\RA3\\";
			//outData.fxShaderName.concat(meshData.fxShaderName);
			outData.techniqueIndex = meshData.techniqueIndex;
			outData.constants = meshData.constants;
		}
	}

	outData.valid = (outData.subMeshes.size() > 0);
	DEBUG_LOG(("[W3X_P5]   Total: %d sub-meshes, valid=%d, fx=%s, bones=%d\n",
		(int)outData.subMeshes.size(), outData.valid, outData.fxShaderName.str(), outData.boneCount));
	return outData.valid;
}

//-----------------------------------------------------------------------------
// releaseModelData
//-----------------------------------------------------------------------------
void W3XModelDraw::releaseModelData(LoadedModelData &data)
{
	for (size_t i = 0; i < data.subMeshes.size(); i++) {
		if (data.subMeshes[i].vertexBuffer) data.subMeshes[i].vertexBuffer->Release();
		if (data.subMeshes[i].indexBuffer) data.subMeshes[i].indexBuffer->Release();
	}
	data.subMeshes.clear();
	data.constants.clear();
	data.fxShaderName.clear();
	if (data.boneMatrixArray) { delete[] data.boneMatrixArray; data.boneMatrixArray = NULL; }
	data.boneCount = 0;
	data.valid = false;
}

//-----------------------------------------------------------------------------
// Vertex declaration for W3XVertex (position + normal + tex0 + tangent + bone)
// This maps to RA3 shader semantics correctly (FVF cannot express TANGENT/BLEND*)
//-----------------------------------------------------------------------------
static IDirect3DVertexDeclaration9 *s_w3xVertexDecl = NULL;

static IDirect3DVertexDeclaration9 *GetW3XVertexDecl(IDirect3DDevice9 *dev)
{
	if (!s_w3xVertexDecl && dev) {
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
		dev->CreateVertexDeclaration(decl, &s_w3xVertexDecl);
	}
	return s_w3xVertexDecl;
}

//-----------------------------------------------------------------------------
// Texture path cache: avoid re-resolving DDS paths every frame
// (the XML lookups that fail are expensive file system calls)
//-----------------------------------------------------------------------------
struct W3XTextureCacheEntry {
	AsciiString name;
	AsciiString ddsPath;
};
static W3XTextureCacheEntry s_texCache[32];
static int s_texCacheSize = 0;

static AsciiString resolveTextureCached(const char *texName)
{
	if (!texName || !texName[0]) return AsciiString("");
	for (int i = 0; i < s_texCacheSize; i++) {
		if (s_texCache[i].name.compare(texName) == 0) return s_texCache[i].ddsPath;
	}
	AsciiString path = W3XLoader::ResolveTextureDDS(texName);
	if (s_texCacheSize < 32) {
		s_texCache[s_texCacheSize].name = texName;
		s_texCache[s_texCacheSize].ddsPath = path;
		s_texCacheSize++;
	}
	return path;
}

//-----------------------------------------------------------------------------
// renderModel: Render using D3DXEffect
void W3XModelDraw::createRenderObject(LoadedModelData &data)
{
	removeRenderObject();

	if (!data.valid || data.subMeshes.empty()) return;

	W3XRenderObjClass *robj = NEW W3XRenderObjClass;
	if (!robj) return;

	// Transfer sub-mesh buffers (ownership moves to W3XRenderObjClass;
	// NULL the source pointers so releaseModelData won't double-free)
	for (size_t i = 0; i < data.subMeshes.size(); i++) {
		SubMeshBuffer &sm = data.subMeshes[i];
		robj->AddSubMesh(sm.vertexBuffer, sm.indexBuffer, sm.vertexCount, sm.triangleCount);
		sm.vertexBuffer = NULL;
		sm.indexBuffer = NULL;
	}

	// FX shader + constants
	robj->SetFX(data.fxShaderName.str(), data.techniqueIndex, data.constants);

	// Bones
	if (data.boneCount > 0 && data.boneMatrixArray) {
		robj->SetBones(data.boneMatrixArray, data.boneCount);
	}

	// Bounds (approximate from model - use generous box)
	Vector3 bmin(-100, -100, -100), bmax(100, 100, 100);
	robj->SetBounds(bmin, bmax);

	// Add to scene
	W3DDisplay::m_3DScene->Add_Render_Object(robj);
	m_renderObj = robj;

	DEBUG_LOG(("[W3X_P5]   W3XRenderObj added to scene: %d sub-meshes, fx=%s\n",
		(int)data.subMeshes.size(), data.fxShaderName.str()));
}

//-----------------------------------------------------------------------------
// removeRenderObject: remove from scene and release.
//-----------------------------------------------------------------------------
void W3XModelDraw::removeRenderObject(void)
{
	if (m_renderObj) {
		W3DDisplay::m_3DScene->Remove_Render_Object(m_renderObj);
		REF_PTR_RELEASE(m_renderObj);
		m_renderObj = NULL;
	}
}

//-----------------------------------------------------------------------------
// doDrawModule: load/refresh the model and update the scene render object.
// NOTE: actual D3D drawing happens in W3XRenderObjClass::Render(), which the
// scene calls in the correct render pass.
//-----------------------------------------------------------------------------
void W3XModelDraw::doDrawModule(const Matrix3D *transformMtx)
{
	if (m_fullyObscuredByShroud) return;
	Drawable *draw = getDrawable();
	if (!draw) return;

	const ModelConditionFlags flags = draw->getModelConditionFlags();
	const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
	const W3XConditionInfo *state = md->findBestConditionState(flags);
	const char *targetModel = NULL;
	if (state) targetModel = state->m_modelName.str();
	else if (!md->m_defaultModelName.isEmpty()) targetModel = md->m_defaultModelName.str();
	if (!targetModel || !targetModel[0]) return;

	// Load/reload model if needed
	if (m_loadedModelName.compare(targetModel) != 0) {
		LoadedModelData newModel;
		if (loadW3XModel(targetModel, newModel)) {
			releaseModelData(m_loadedModel);
			m_loadedModel = newModel;
			if (!m_loadedModel.hierarchyName.isEmpty() && m_loadedModel.boneCount == 0) {
				loadHierarchy(m_loadedModel.hierarchyName.str(), m_loadedModel);
			}
			// Build scene render object from the freshly loaded data
			createRenderObject(m_loadedModel);
		}
		// Mark as attempted regardless of success
		m_loadedModelName = targetModel;
	}

	// Update the scene render object's transform every frame
	if (m_renderObj) {
		if (transformMtx) {
			m_renderObj->SetWorldTransform(*transformMtx);
			m_renderObj->Set_Transform(*transformMtx);
		}
		// Faction/team color for RA3 RecolorColor (same source as W3DModelDraw).
		// NOTE: uses day indicator color; could branch on TIME_OF_DAY_NIGHT.
		Object *obj = draw->getObject();
		if (obj) {
			m_renderObj->SetRecolorColor((unsigned int)obj->getIndicatorColor());
		}
	}
}

void W3XModelDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	m_fullyObscuredByShroud = (fullyObscured != FALSE);
}

Bool W3XModelDraw::isVisible() const
{
	return !m_fullyObscuredByShroud;
}
