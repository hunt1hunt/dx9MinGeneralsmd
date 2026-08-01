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

#ifndef __W3XMODELDRAW_H_
#define __W3XMODELDRAW_H_

#include "Common/DrawModule.h"
#include "Common/ModelState.h"
#include "Common/SparseMatchFinder.h"
#include "W3DDevice/GameClient/w3x_loader.h"

class Thing;
class RenderObjClass;
struct ID3DXEffect;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;

// W3X vertex: position + normal + tex0 + tangent + binormal + bone data
// Layout matches RA3 vertex shader input (needs TANGENT and BINORMAL)
struct W3XVertex
{
	float x, y, z;		//  0 POSITION
	float nx, ny, nz;		// 12 NORMAL
	float u, v;			// 24 TEXCOORD0
	float tx, ty, tz;		// 32 TANGENT
	float bx, by, bz;		// 44 BINORMAL
	float boneIdx;		// 56 BLENDINDICES (0 = no bone)
	float boneWeight;	// 60 BLENDWEIGHT (1.0 for rigid)
};

struct W3XConditionInfo
{
	std::vector<ModelConditionFlags>	m_conditionsYesVec;
	AsciiString							m_modelName;
	int getConditionsYesCount() const { return (int)m_conditionsYesVec.size(); }
	AsciiString getDescription() const { return m_modelName; }
	const ModelConditionFlags &getNthConditionsYes(int i) const { return m_conditionsYesVec[i]; }
};

class W3XModelDrawModuleData : public ModuleData
{
public:
	W3XModelDrawModuleData();
	~W3XModelDrawModuleData();
	mutable std::vector<W3XConditionInfo> m_conditionStates;
	AsciiString m_defaultModelName;
	mutable SparseMatchFinder<W3XConditionInfo, ModelConditionFlags> m_conditionStateMap;
	const W3XConditionInfo *findBestConditionState(const ModelConditionFlags &c) const;
	static void buildFieldParse(MultiIniFieldParse &p);
private:
	static void parseConditionState(INI *ini, void *instance, void *, const void *);
};

class W3XModelDraw : public DrawModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(W3XModelDraw, "W3XModelDraw")
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(W3XModelDraw, W3XModelDrawModuleData)

public:
	W3XModelDraw(Thing *thing, const ModuleData *moduleData);
	virtual void preloadAssets(TimeOfDay timeOfDay);
	virtual void doDrawModule(const Matrix3D *transformMtx);
	virtual void setShadowsEnabled(Bool enable) { }
	virtual void releaseShadows(void) { }
	virtual void allocateShadows(void) { }
	virtual void setFullyObscuredByShroud(Bool fullyObscured);
	virtual Bool isVisible() const;
	virtual void reactToTransformChange(const Matrix3D *, const Coord3D *, Real) { }
	virtual void reactToGeometryChange() { }

private:
	struct SubMeshBuffer
	{
		IDirect3DVertexBuffer9 *vertexBuffer;
		IDirect3DIndexBuffer9 *indexBuffer;
		int vertexCount, triangleCount;
		bool hasBones;
	};

	struct LoadedModelData
	{
		AsciiString fxShaderName;
		AsciiString hierarchyName;		// skeleton file name
		int techniqueIndex;
		int boneCount;
		std::vector<W3XShaderConstant> constants;
		std::vector<SubMeshBuffer> subMeshes;
		float *boneMatrixArray;		// boneCount * 8 floats (RA3 WorldBones: quat+offset, 2 float4 per bone)
		bool valid;
	};

	bool loadW3XModel(const char *containerName, LoadedModelData &outData);
	bool loadHierarchy(const char *sklName, LoadedModelData &data);
	void releaseModelData(LoadedModelData &data);
	void createRenderObject(LoadedModelData &data);		// build W3XRenderObjClass from loaded model
	void removeRenderObject(void);							// remove from scene + release
	void uploadBoneMatrices(ID3DXEffect *effect, const LoadedModelData &data);	// upload RA3 WorldBones to D3DXEffect

	const W3XConditionInfo *m_curState;
	LoadedModelData m_loadedModel;
	AsciiString m_loadedModelName;
	class W3XRenderObjClass *m_renderObj;					// scene render object
	bool m_fullyObscuredByShroud;
};

#endif
