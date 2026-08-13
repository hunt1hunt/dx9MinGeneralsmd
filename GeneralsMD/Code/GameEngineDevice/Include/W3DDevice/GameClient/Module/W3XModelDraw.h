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

#include "Common/DrawModule.h"		// ObjectDrawInterface, WeaponSlotType (via GameType.h)
#include "Common/ModelState.h"
#include "Common/SparseMatchFinder.h"
#include "Common/GameCommon.h"		// WhichTurretType (used in getProjectileLaunchOffset decl)
#include "WWMath/matrix3d.h"		// Matrix3D by-value in W3XWeaponBarrelInfo
#include "W3DDevice/GameClient/w3x_loader.h"

class Thing;
class RenderObjClass;
class Shadow;
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
	unsigned int color;	// 64 COLOR (0xAARRGGBB; white for RA3 VertexColor)
	float u2, v2;		// 68 TEXCOORD1 (RA3 texcoordNEW; zero = no damage UV)
};

// Weapon barrel/muzzle/launch bone info for one weapon slot, resolved at model
// load time from the ini bone names. Mirrors W3DModelDraw's WeaponBarrelInfo.
struct W3XWeaponBarrelInfo
{
	int			m_fxBone;			// -1 = none
	int			m_muzzleFlashBone;	// -1 = none
	int			m_launchBone;		// -1 = none
	Matrix3D	m_projectileOffsetMtx;	// pristine (model-space) launch transform
	W3XWeaponBarrelInfo() : m_fxBone(-1), m_muzzleFlashBone(-1), m_launchBone(-1)
	{ m_projectileOffsetMtx.Make_Identity(); }
};
typedef std::vector<W3XWeaponBarrelInfo> W3XWeaponBarrelInfoVec;

struct W3XConditionInfo
{
	std::vector<ModelConditionFlags>	m_conditionsYesVec;
	AsciiString							m_modelName;
	// Weapon bone names parsed from ini (WeaponFireFXBone / WeaponMuzzleFlash /
	// WeaponLaunchBone), one per weapon slot.
	AsciiString							m_weaponFireFXBoneName[WEAPONSLOT_COUNT];
	AsciiString							m_weaponMuzzleFlashName[WEAPONSLOT_COUNT];
	AsciiString							m_weaponProjectileLaunchBoneName[WEAPONSLOT_COUNT];
	// Resolved barrel info (bone indices) + validity per slot. mutable so the
	// const ObjectDrawInterface queries can lazily populate on first use.
	mutable W3XWeaponBarrelInfoVec		m_weaponBarrelInfoVec[WEAPONSLOT_COUNT];
	mutable bool						m_barrelsValid[WEAPONSLOT_COUNT];
	// Turret (yaw) and pitch bones (like W3D Turret / TurretPitch).
	AsciiString							m_turretAngleName;
	AsciiString							m_turretPitchName;
	// Animation names (like W3D Animation / IdleAnimation).
	AsciiString							m_animationName;
	AsciiString							m_idleAnimationName;
	W3XConditionInfo()
	{
		for (int i = 0; i < WEAPONSLOT_COUNT; i++) m_barrelsValid[i] = false;
		m_turretAngleBone = m_turretPitchBone = -1;
	}
	// Resolved turret/pitch bone indices (set at model load).
	mutable int							m_turretAngleBone;
	mutable int							m_turretPitchBone;
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

class W3XModelDraw : public DrawModule, public ObjectDrawInterface
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(W3XModelDraw, "W3XModelDraw")
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(W3XModelDraw, W3XModelDrawModuleData)

public:
	W3XModelDraw(Thing *thing, const ModuleData *moduleData);
	virtual void preloadAssets(TimeOfDay timeOfDay);
	virtual void doDrawModule(const Matrix3D *transformMtx);
	virtual void setShadowsEnabled(Bool enable);
	virtual void releaseShadows(void);
	virtual void allocateShadows(void);
	virtual void setFullyObscuredByShroud(Bool fullyObscured);
	virtual Bool isVisible() const;
	virtual void reactToTransformChange(const Matrix3D *, const Coord3D *, Real) { }
	virtual void reactToGeometryChange() { }

	// ObjectDrawInterface (so Drawable::handleWeaponFireFX / weapon launch reach
	// the W3X draw module). Mirrors W3DModelDraw.
	virtual ObjectDrawInterface *getObjectDrawInterface() { return this; }
	virtual const ObjectDrawInterface *getObjectDrawInterface() const { return this; }
	virtual Bool clientOnly_getRenderObjInfo(Coord3D *pos, Real *boundingSphereRadius, Matrix3D *transform) const;
	virtual Bool clientOnly_getRenderObjBoundBox(OBBoxClass *boundbox) const;
	virtual Bool clientOnly_getRenderObjBoneTransform(const AsciiString &boneName, Matrix3D *set_tm) const;
	virtual Int getPristineBonePositionsForConditionState(const ModelConditionFlags &condition, const char *boneNamePrefix, Int startIndex, Coord3D *positions, Matrix3D *transforms, Int maxBones) const;
	virtual Int getCurrentBonePositions(const char *boneNamePrefix, Int startIndex, Coord3D *positions, Matrix3D *transforms, Int maxBones) const;
	virtual Bool getCurrentWorldspaceClientBonePositions(const char *boneName, Matrix3D &transform) const;
	virtual Bool getProjectileLaunchOffset(const ModelConditionFlags &condition, WeaponSlotType wslot, Int specificBarrelToUse, Matrix3D *launchPos, WhichTurretType tur, Coord3D *turretRotPos, Coord3D *turretPitchPos) const;
	virtual void updateProjectileClipStatus(UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot) { }
	virtual void updateDrawModuleSupplyStatus(Int maxSupply, Int currentSupply) { }
	virtual void notifyDrawModuleDependencyCleared() { }
	virtual void setHidden(Bool h);
	virtual void replaceModelConditionState(const ModelConditionFlags &a) { }
	virtual void replaceIndicatorColor(Color color);
	virtual Bool handleWeaponFireFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList *fxl, Real weaponSpeed, const Coord3D *victimPos, Real damageRadius);
	virtual Int getBarrelCount(WeaponSlotType wslot) const;
	virtual void setSelectable(Bool selectable) { }
	virtual void setAnimationLoopDuration(UnsignedInt numFrames) { }
	virtual void setAnimationCompletionTime(UnsignedInt numFrames) { }
	virtual Bool updateBonesForClientParticleSystems() { return false; }
	virtual void setAnimationFrame(int frame) { }
	virtual void setPauseAnimation(Bool pauseAnim) { }
	virtual void updateSubObjects() { }
	virtual void showSubObject(const AsciiString &name, Bool show) { }

private:
	// Resolve a single bone name -> skeleton index (0 = not found / root).
	int getBoneIndexByName(const AsciiString &boneName) const;
	// Populate this state's weapon barrel info vectors from its ini bone names.
	void validateBarrelInfo(const W3XConditionInfo *state) const;

private:
	struct SubMeshBuffer
	{
		IDirect3DVertexBuffer9 *vertexBuffer;
		IDirect3DIndexBuffer9 *indexBuffer;
		int vertexCount, triangleCount;
		bool hasBones;
		AsciiString name;			// sub-mesh name (for diagnostics)
		bool hasTangents;			// true when .w3x provided native tangent data
		bool hasBinormals;			// true when .w3x provided native binormal data
		AsciiString origShader;		// FX shader the sub-mesh was authored with
		std::vector<W3XShaderConstant> constants;	// this sub-mesh's shader constants
		float boundMin[3];			// local-space AABB from .w3x <BoundingBox> (culling + projected shadow)
		float boundMax[3];
	};

	struct LoadedModelData
	{
		AsciiString fxShaderName;
		AsciiString hierarchyName;		// skeleton file name
		int techniqueIndex;
		int boneCount;
		std::vector<AsciiString> boneNames;	// per-bone name (index-aligned with boneMatrixArray)
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

	// --- Turret / animation (RA3 skeletal) ---
	// Resolve a ConditionState's turret/pitch bones against the loaded skeleton.
	void resolveTurretBones(const W3XConditionInfo *state) const;
	// Per-frame: read the AI turret/pitch angles and Control_Bone the turret bone.
	void handleClientTurretPositioning();
	// Load an animation file (W3DAnimation) into m_curAnim (no-op if same).
	bool loadAnimation(const char *animName);
	// Advance m_curAnim by one frame and apply its keyframes to the render obj.
	void updateAnimation();

	const W3XConditionInfo *m_curState;
	LoadedModelData m_loadedModel;
	AsciiString m_loadedModelName;
	class W3XRenderObjClass *m_renderObj;					// scene render object
	bool m_fullyObscuredByShroud;
	Bool m_shadowEnabled;			// cached shadow-enable state (Options screen)
	Shadow *m_shadow;				// projected ground shadow for this object

	// Animation state
	W3XAnimation m_curAnim;			// loaded keyframe data (name/hierarchy/channels)
	AsciiString m_curAnimName;		// name of the loaded animation (to avoid reload)
	float m_animFrame;				// current frame (float for interpolation)
	int m_animPrevFrame;			// previous integer frame (to detect step)
	int m_animLastFrame;			// last TheGameLogic frame (per-instance, not static)
	bool m_animValid;
};

#endif
