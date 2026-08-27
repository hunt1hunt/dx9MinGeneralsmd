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

// Must be defined before ANY include that pulls in WeaponSet.h (incl. the
// W3XModelDraw.h header) so TheWeaponSlotTypeNames is emitted here.
#define DEFINE_WEAPONSLOTTYPE_NAMES

#include "always.h"
#include "W3DDevice/GameClient/Module/W3XModelDraw.h"
#include "W3DDevice/GameClient/W3XEffectManager.h"
#include "W3DDevice/GameClient/W3XRenderObj.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "GameClient/Drawable.h"
#include "GameClient/View.h"	// PICK_TYPE_SELECTABLE for ray-picking collision type
#include "GameClient/Shadow.h"
#include "GameClient/FXList.h"		// FXList::doFXPos for weapon fire placement
#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"		// TheGameLogic (frame counter for animation)
#include "GameLogic/Module/AIUpdate.h"	// getTurretRotAndPitch for turret rotation
#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3dformat.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WWMath/quat.h"	// Quaternion for pristine bone transforms
#include "INI.h"
#include "Common/Xfer.h"
#include "refcount.h"
#include "wwdebug.h"
#include "wwmemlog.h"
#include <string.h>
#include <math.h>
#include <d3d9.h>
#include <d3dx9effect.h>

// RA3 (.w3x) assets live under Art/W3X/ (relative to the game root) so they
// never clutter the game root or mix with the Generals' own Art/Textures.
// .w3x is an "unknown" extension to GameFileClass, so it resolves CWD-relative
// -> Art/W3X/<file>.w3x works directly.
#define W3X_ASSET_DIR "Art/W3X/"

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

// Parse "WeaponFireFXBone = PRIMARY <name>" style entries into the per-slot
// AsciiString array at 'store' (mirrors W3DModelDraw::parseWeaponBoneName).
static void parseW3XWeaponBoneName(INI *ini, void *instance, void *store, const void *)
{
	W3XConditionInfo *self = (W3XConditionInfo *)instance;
	AsciiString *arr = (AsciiString *)store;
	WeaponSlotType wslot = (WeaponSlotType)INI::scanIndexList(ini->getNextToken(), TheWeaponSlotTypeNames);
	arr[wslot] = ini->getNextAsciiString();
	arr[wslot].toLower();
	if (arr[wslot].isNone())
		arr[wslot].clear();
	(void)self;
}


// Parse "ParticleSysBone = <bone> <system>" into the current condition state.
// Bone name is lowercased to match the loader's lowercase skeleton names
// (RA3 attaches smoke to fx_smoke01 / fx_smoke02). Mirrors W3DModelDraw.
static void parseParticleSysBone(INI* ini, void *instance, void *store, const void * /*userData*/)
{
	W3XParticleSysBoneInfo info;
	info.boneName = ini->getNextAsciiString();
	info.boneName.toLower();
	ini->parseParticleSystemTemplate(ini, instance, &(info.particleSystemTemplate), NULL);
	W3XConditionInfo *self = (W3XConditionInfo *)instance;
	self->m_particleSysBones.push_back(info);
}

// Animation playback mode names (must match W3XAnimMode order).
static const char *TheW3XAnimModeNames[] = { "LOOP", "ONCE", "ONCE_BACKWARDS", "MANUAL", NULL };

// Parse "AnimationMode = ONCE/ONCE_BACKWARDS/..." into the condition state's mode.
static void parseW3XAnimationMode(INI* ini, void *instance, void *store, const void * /*userData*/)
{
	int *mode = (int *)store;
	*mode = INI::scanIndexList(ini->getNextToken(), TheW3XAnimModeNames);
	(void)instance;
}

void W3XModelDrawModuleData::parseConditionState(INI* ini, void *instance, void *, const void *)
{
	// Sub-field parse table for a ConditionState block
	static const FieldParse myFieldParse[] = {
		{ "Model", INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_modelName) },
		{ "WeaponFireFXBone",  parseW3XWeaponBoneName, NULL, offsetof(W3XConditionInfo, m_weaponFireFXBoneName[0]) },
		{ "WeaponMuzzleFlash", parseW3XWeaponBoneName, NULL, offsetof(W3XConditionInfo, m_weaponMuzzleFlashName[0]) },
		{ "WeaponLaunchBone",  parseW3XWeaponBoneName, NULL, offsetof(W3XConditionInfo, m_weaponProjectileLaunchBoneName[0]) },
		{ "Turret",       INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_turretAngleName) },
		{ "TurretPitch",  INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_turretPitchName) },
		{ "Animation",    INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_animationName) },
		{ "IdleAnimation",INI::parseAsciiString, NULL, offsetof(W3XConditionInfo, m_idleAnimationName) },
		{ "ParticleSysBone", parseParticleSysBone, NULL, offsetof(W3XConditionInfo, m_particleSysBones) },
		{ "AnimationMode", parseW3XAnimationMode, NULL, offsetof(W3XConditionInfo, m_animationMode) },
		{ "FrameForPristineBonePositions", INI::parseInt, NULL, offsetof(W3XConditionInfo, m_frameForPristineBonePositions) },
		{ "UseWeaponTiming", INI::parseBool, NULL, offsetof(W3XConditionInfo, m_useWeaponTiming) },
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
	m_renderObj(NULL), m_shadowEnabled(TRUE), m_shadow(NULL),
	m_animFrame(0), m_animPrevFrame(-1), m_animLastFrame(-1), m_animValid(false),
	m_animMode(W3X_ANIM_LOOP), m_needRecalcBoneParticleSystems(true)
{
	m_loadedModel.boneMatrixArray = NULL;
	m_loadedModel.valid = false;
	DEBUG_LOG(("[W3X_P5] W3XModelDraw created\n"));
}

//-----------------------------------------------------------------------------
// Shadow support (mirrors W3DModelDraw): W3X objects cast the same projected
// ground shadow via TheW3DShadowManager. The three virtuals are invoked by
// Drawable::allocateShadows/releaseShadows/setShadowsEnabled (Options screen).
//-----------------------------------------------------------------------------
void W3XModelDraw::setShadowsEnabled(Bool enable)
{
	if (m_shadow) m_shadow->enableShadowRender(enable);
	m_shadowEnabled = enable;
}

void W3XModelDraw::releaseShadows(void)
{
	if (m_shadow) m_shadow->release();
	m_shadow = NULL;
}

void W3XModelDraw::allocateShadows(void)
{
	// SWITCHED to the engine's W3D soft shadow path: the deferred shadow-map could
	// not rasterize the W3X into the shadow color RT under dgVoodoo2 (0 pixels),
	// so use the engine shadow driven by the template's Shadow type
	// (SHADOW_VOLUME). The shadow geometry is skinned from the render object's
	// COMPOSED (animated) bones, so apply the current animation's frame 0 first —
	// a humanoid's bind/T-pose (arms out, weapon 11 units to the side) would
	// otherwise make the shadow ~3x the standing soldier.
	const ThingTemplate *tmplate = getDrawable() ? getDrawable()->getTemplate() : NULL;
	if (m_shadow == NULL && m_renderObj && TheW3DShadowManager
		&& tmplate && tmplate->getShadowType() != SHADOW_NONE) {
		// Apply the current animation's frame-0 pose so the baked volumetric shadow
		// silhouette is the standing pose (weapon in hand), not the spread bind.
		// doDrawModule() loaded the animation into m_curAnim before calling us.
		W3XRenderObjClass *w3x = m_renderObj;	// W3XModelDraw's render object is always a W3XRenderObjClass
		// Note: the missile defender's launcher tube is excluded from the shadow
		// geometry in W3DShadowGeometry::initFromW3X (rocket bone collapsed onto
		// the hips bone), so the idle standing pose already casts a compact
		// shadow — no per-unit pose override is needed here.
		if (w3x && m_curAnim.numFrames > 0 && !m_curAnim.channels.empty())
			w3x->ApplyAnimationFrame(&m_curAnim, 0);
		// If the applied pose is prone (hips far below the bind height — e.g. the
		// GU sniper's idle lies down), a horizontal shadow looks wrong/reversed.
		// Use the MOVING animation (the sniper's bent-upright crawl) for a normal
		// upright shadow silhouette instead. m_curAnim is left untouched so the
		// live animation keeps playing the idle.
		if (w3x) {
			int hips = w3x->Get_Bone_Index("hips");
			float bindHipsZ = 0.0f;
			if (hips > 0 && hips < w3x->GetBoneCount())
				bindHipsZ = w3x->GetBones()[hips * 8 + 6];
			float compBones[128 * 8];
			int compN = w3x->GetComposedBones(compBones, 128 * 8);
			float poseHipsZ = (compN > 0 && hips > 0 && hips < compN) ? compBones[hips * 8 + 6] : 0.0f;
			if (bindHipsZ > 1.0f && poseHipsZ < 0.6f * bindHipsZ) {
				const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
				ModelConditionFlags moveFlags;
				moveFlags.set(MODELCONDITION_MOVING);
				const W3XConditionInfo *moveState = md ? md->findBestConditionState(moveFlags) : NULL;
				if (moveState && !moveState->m_animationName.isEmpty()) {
					W3XAnimation moveAnim;
					char path[512];
					sprintf(path, W3X_ASSET_DIR "%s.w3x", moveState->m_animationName.str());
					if (W3XLoader::ParseAnimation(path, moveAnim))
						w3x->ApplyAnimationFrame(&moveAnim, 0);
				}
			}
		}
		Shadow::ShadowTypeInfo shadowInfo;
		strcpy(shadowInfo.m_ShadowName, tmplate->getShadowTextureName().str());
		shadowInfo.allowUpdates    = FALSE;
		shadowInfo.allowWorldAlign = TRUE;
		shadowInfo.m_type          = (ShadowType)tmplate->getShadowType();
		shadowInfo.m_sizeX         = tmplate->getShadowSizeX();
		shadowInfo.m_sizeY         = tmplate->getShadowSizeY();
		shadowInfo.m_offsetX       = tmplate->getShadowOffsetX();
		shadowInfo.m_offsetY       = tmplate->getShadowOffsetY();
		m_shadow = TheW3DShadowManager->addShadow(m_renderObj, &shadowInfo);
		// Restore the live animation state; the per-frame update re-applies it.
		if (w3x) w3x->ResetAnimationBones();
		if (m_shadow) {
			m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
			if (m_renderObj->Is_Hidden() || !m_shadowEnabled)
				m_shadow->enableShadowRender(FALSE);
		}
		DEBUG_LOG(("[W3X_DIAG] W3X '%s' W3D soft shadow: shadow=%p type=%d size=(%.1f,%.1f) tex='%s'\n",
			tmplate->getName().str(), (void*)m_shadow, (int)tmplate->getShadowType(),
			tmplate->getShadowSizeX(), tmplate->getShadowSizeY(),
			tmplate->getShadowTextureName().str()));
	} else {
		DEBUG_LOG(("[W3X_DIAG] W3X '%s' shadow skipped (m_shadow=%p robj=%p mgr=%p type=%d)\n",
			tmplate ? tmplate->getName().str() : "?",
			(void*)m_shadow, (void*)m_renderObj, (void*)TheW3DShadowManager,
			tmplate ? (int)tmplate->getShadowType() : -1));
	}
}

W3XModelDraw::~W3XModelDraw()
{
	stopClientParticleSystems();
	releaseShadows();
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
		// Preload everything that would otherwise load lazily on the first in-game
		// spawn/render and stall a frame: shader compile (D3DXCreateEffect), DDS
		// textures, the idle animation XML, and the volumetric shadow geometry.
		// Shaders/textures/shadow-geometry are cached globally, so the first real
		// unit of each type reuses them.
		if (m_renderObj) {
			m_renderObj->PreloadAssets();
			const W3XConditionInfo *idle = md->findBestConditionState(ModelConditionFlags());
			if (idle && !idle->m_animationName.isEmpty())
				loadAnimation(idle->m_animationName.str());
			allocateShadows();
		}
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

static void QuatNormalize(float *out, const float *q)
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

//-----------------------------------------------------------------------------
// loadHierarchy: parse .skl.w3x skeleton, compute base-pose bone transforms
// in RA3 WorldBones format (quaternion + offset, 2 float4 per bone).
//-----------------------------------------------------------------------------
bool W3XModelDraw::loadHierarchy(const char *sklName, LoadedModelData &data)
{
	if (!sklName || !sklName[0]) return false;

	// Construct .w3x filename (RA3 assets live under Art/W3X/)
	char path[512];
	sprintf(path, W3X_ASSET_DIR "%s.w3x", sklName);

	std::vector<W3XBoneInfo> bones;
	if (!W3XLoader::ParseHierarchy(path, bones)) {
		DEBUG_LOG(("[W3X_P5] loadHierarchy: failed to parse '%s'\n", path));
		return false;
	}

	data.boneCount = (int)bones.size();
	DEBUG_LOG(("[W3X_P5] loadHierarchy: '%s' -> %d bones\n", path, data.boneCount));

	// Keep the per-bone names (lowercased) so the render object can resolve
	// ini WeaponFireFXBone / WeaponMuzzleFlash / WeaponLaunchBone names to
	// skeleton pivots (W3XRenderObjClass::Get_Bone_Index etc).
	data.boneNames.clear();
	data.boneNames.reserve(data.boneCount);
	for (int bi = 0; bi < data.boneCount; bi++) {
		AsciiString n = bones[bi].name;
		n.toLower();
		data.boneNames.push_back(n);
	}

	// Keep the per-bone parent indices so a controlled bone (turret) can cascade
	// its rotation to children (barrel, muzzle) during Render.
	data.boneParents.clear();
	data.boneParents.reserve(data.boneCount);
	for (int pi = 0; pi < data.boneCount; pi++) {
		data.boneParents.push_back(bones[pi].parentIndex);
	}

	// Keep the bind LOCAL rotations + translations (Pivot Rotation/Translation).
	// composeControlledBones composes the animation offset on these, then
	// accumulates the parent chain (the SAGE composition).
	data.boneLocalQuat.clear();
	data.boneLocalQuat.reserve(data.boneCount * 4);
	data.boneLocalTrans.clear();
	data.boneLocalTrans.reserve(data.boneCount * 3);
	for (int li = 0; li < data.boneCount; li++) {
		data.boneLocalQuat.push_back(bones[li].rotation[0]);
		data.boneLocalQuat.push_back(bones[li].rotation[1]);
		data.boneLocalQuat.push_back(bones[li].rotation[2]);
		data.boneLocalQuat.push_back(bones[li].rotation[3]);
		data.boneLocalTrans.push_back(bones[li].translation[0]);
		data.boneLocalTrans.push_back(bones[li].translation[1]);
		data.boneLocalTrans.push_back(bones[li].translation[2]);
	}

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
			// world_offset = parent_offset + rotate(local_offset, parent_world_quat)
			// Standard hierarchy accumulation. The old form rotated the PARENT's
			// offset by the LOCAL quat, which scrambled humanoid skeletons that
			// carry non-identity bind rotations (RA3 infantry) -- vehicles with
			// all-identity bone rotations (tank/humvee/factory) were unaffected.
			float rotatedLocal[3];
			QuatRotateVector(rotatedLocal, pq, lo);
			wo[0] = po[0] + rotatedLocal[0];
			wo[1] = po[1] + rotatedLocal[1];
			wo[2] = po[2] + rotatedLocal[2];
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
	sprintf(containerPath, W3X_ASSET_DIR "%s.w3x", containerName);
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

	// Model-wide FX shader selection: decided from ALL non-default sub-meshes,
	// not just the first one. Mixed-convention models (soldier = BASIC Texture_0
	// / infantry.fx, vehicle = PBR DiffuseTexture / objects*.fx) must route the
	// model-wide shader to PBR so the vehicle meshes keep their textures; the
	// soldier meshes are then overridden per-sub-mesh to w3x_infantry.fx in
	// createRenderObject. Only a model whose non-default meshes are ALL BASIC
	// routes the whole model to w3x_infantry.fx.
	bool hasPBRMesh = false;
	bool hasBasicMesh = false;
	int firstPBRTech = 0;
	int firstBasicTech = 0;
	std::vector<W3XShaderConstant> firstPBRConst;
	std::vector<W3XShaderConstant> firstBasicConst;

	// Load each sub-mesh
	for (size_t si = 0; si < subObjects.size(); si++) {
		const W3XSubObjectInfo &sub = subObjects[si];
		if (sub.isCollisionBox) continue;

		char meshPath[512];
		sprintf(meshPath, W3X_ASSET_DIR "%s.w3x", sub.renderObjectName.str());

		W3XMeshData meshData;
		if (!W3XLoader::ReadMeshData(meshPath, meshData)) continue;

		int vertCount = (int)meshData.vertices.size();
		int triCount = (int)meshData.triangles.size() / 3;
		if (vertCount == 0 || triCount == 0) continue;

		bool hasBones = ((int)meshData.boneIndices.size() >= vertCount);
		// RA3 soft binding: a second vertex/normal/bone-influence set. These get a
		// dedicated 136-byte W3XSoftVertex buffer + DefaultSoft technique (2-bone),
		// giving smooth joints (the RA3 soldiers are soft-skinned; the hard path
		// made joints look like over-stretched rubber). Enabled when the mesh
		// carries a second vertex + bone-influence set. Hard-skinned meshes
		// (vehicles/buildings, no second set) keep the 1-bone Default path.
		bool softMesh = meshData.hasSoftBinding && (int)meshData.vertices2.size() >= vertCount;

		// V-flip decision (per sub-mesh, before building vertices).
		// .w3x UV V is authored V=0-at-bottom; D3D9 samples V=0-at-top, so every
		// sub-mesh needs v=1-v. Cluster-sampling TavBtMstr2 at the barrel's UV
		// islands confirmed the FLIPPED mapping lands the barrel main body on the
		// near-black metal band (33,40,33) whereas unflipped lands on warm rust
		// (112,59,41). So flip uniformly for all sub-meshes.
		const char *nm = sub.renderObjectName.str();
		const bool flipV = true;
		// The RA3 tangent slots are T-B-exchanged (binormal=+U, tangent=-V);
		// flipping V negates the V-axis, so negate the tangent for consistency.
		const float tsign = flipV ? -1.0f : 1.0f;

		// Build index array (shared by both vertex formats)
		int indexCount = triCount * 3;
		unsigned short *indices = new unsigned short[indexCount];
		for (int ti = 0; ti < indexCount; ti++)
			indices[ti] = (unsigned short)meshData.triangles[ti];

		// Create vertex buffer (soft 128-byte or hard 76-byte format)
		IDirect3DVertexBuffer9 *vb = NULL;
		HRESULT hr = E_FAIL;
		if (softMesh) {
			W3XSoftVertex *sverts = new W3XSoftVertex[vertCount];
			for (int vi = 0; vi < vertCount; vi++) {
				// POSITION0 / NORMAL0 bound to bone0 (block 0)
				sverts[vi].x0 = meshData.vertices[vi].X;
				sverts[vi].y0 = meshData.vertices[vi].Y;
				sverts[vi].z0 = meshData.vertices[vi].Z;
				sverts[vi].n0x = vi < (int)meshData.normals.size() ? meshData.normals[vi].X : 0;
				sverts[vi].n0y = vi < (int)meshData.normals.size() ? meshData.normals[vi].Y : 0;
				sverts[vi].n0z = vi < (int)meshData.normals.size() ? meshData.normals[vi].Z : 0;
				// POSITION1 / NORMAL1 bound to bone1 (block 1, soft set)
				sverts[vi].x1 = meshData.vertices2[vi].X;
				sverts[vi].y1 = meshData.vertices2[vi].Y;
				sverts[vi].z1 = meshData.vertices2[vi].Z;
				sverts[vi].n1x = vi < (int)meshData.normals2.size() ? meshData.normals2[vi].X : 0;
				sverts[vi].n1y = vi < (int)meshData.normals2.size() ? meshData.normals2[vi].Y : 0;
				sverts[vi].n1z = vi < (int)meshData.normals2.size() ? meshData.normals2[vi].Z : 0;
				// Tangent/binormal: reuse the (possibly fallback) primary set for
				// both — the soft second set carries no own tangent data.
				sverts[vi].t0x = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].X : 0;
				sverts[vi].t0y = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].Y : 0;
				sverts[vi].t0z = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].Z : 0;
				sverts[vi].b0x = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].X : 0;
				sverts[vi].b0y = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Y : 0;
				sverts[vi].b0z = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Z : 0;
				sverts[vi].t1x = sverts[vi].t0x; sverts[vi].t1y = sverts[vi].t0y; sverts[vi].t1z = sverts[vi].t0z;
				sverts[vi].b1x = sverts[vi].b0x; sverts[vi].b1y = sverts[vi].b0y; sverts[vi].b1z = sverts[vi].b0z;
				// Two bones + blend weight. blendweight.x = w1/(w0+w1) = the weight
				// toward POSITION1/BONE1, normalized in case the two blocks don't sum
				// to exactly 1. The volumetric shadow blends lerp(P0,P1,bw) and the
				// RA3 skin shader must use the SAME direction (lerp(X0,X1,bw)).
				sverts[vi].boneIdx0 = hasBones ? (float)meshData.boneIndices[vi] : (float)sub.boneIndex;
				sverts[vi].boneIdx1 = vi < (int)meshData.boneIndices2.size() ? (float)meshData.boneIndices2[vi] : sverts[vi].boneIdx0;
				float w0 = hasBones && vi < (int)meshData.boneWeights.size() ? meshData.boneWeights[vi] : 1.0f;
				float w1 = vi < (int)meshData.boneWeights2.size() ? meshData.boneWeights2[vi] : 0.0f;
				sverts[vi].blendWeight = (w0 + w1 > 1e-6f) ? (w1 / (w0 + w1)) : 0.0f;
				sverts[vi]._pad0 = 0.0f; sverts[vi]._pad1 = 0.0f;	// BLENDINDICES.z/w unused
				// Texcoords + color + texcoordNEW (same as the hard format)
				sverts[vi].u = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].U : 0;
				float rawV = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].V : 0;
				sverts[vi].v = flipV ? (1.0f - rawV) : rawV;
				sverts[vi].color = 0xFFFFFFFF;
				sverts[vi].u2 = 0; sverts[vi].v2 = 0;
			}
			hr = dev9->CreateVertexBuffer(vertCount * sizeof(W3XSoftVertex), 0, 0, D3DPOOL_MANAGED, &vb, NULL);
			if (SUCCEEDED(hr) && vb) {
				void *ptr; hr = vb->Lock(0, 0, &ptr, 0);
				if (SUCCEEDED(hr)) { memcpy(ptr, sverts, vertCount * sizeof(W3XSoftVertex)); vb->Unlock(); }
			}
			delete[] sverts;
			DEBUG_LOG(("[W3X_P5]   SOFT BINDING sub-mesh '%s': %d verts x %d bytes (2-bone)\n",
				nm, vertCount, (int)sizeof(W3XSoftVertex)));
#if defined(DEBUG_LOGGING)
			// DIAG: read the CREATED vertex buffer and dump the first soft vertices.
			// Verifies the buffer content matches the W3XSoftVertex struct (if the
			// shader still renders unskinned, this tells us whether the DATA or the
			// declaration/read is the problem).
			{
				static bool s_vbDiag = false;
				if (!s_vbDiag && vb) {
					s_vbDiag = true;
					void *ptr = NULL;
					if (SUCCEEDED(vb->Lock(0, 0, &ptr, D3DLOCK_READONLY))) {
						const char *bp = (const char *)ptr;
						for (int k = 0; k < 3 && k < vertCount; k++) {
							const float *f = (const float *)(bp + k * sizeof(W3XSoftVertex));
							DEBUG_LOG(("[W3X_SOFT5] vb vert[%d] pos0=(%.2f,%.2f,%.2f) pos1=(%.2f,%.2f,%.2f) "
								"bones=(%.0f,%.0f) blend=%.3f\n",
								k, f[0], f[1], f[2], f[6], f[7], f[8], f[24], f[25], f[28]));
						}
						vb->Unlock();
					}
				}
			}
#endif
		} else {
			// --- hard (single-set) W3XVertex, 76-byte stride ---
			W3XVertex *verts = new W3XVertex[vertCount];
			for (int vi = 0; vi < vertCount; vi++) {
				verts[vi].x = meshData.vertices[vi].X;
				verts[vi].y = meshData.vertices[vi].Y;
				verts[vi].z = meshData.vertices[vi].Z;
				verts[vi].nx = vi < (int)meshData.normals.size() ? meshData.normals[vi].X : 0;
				verts[vi].ny = vi < (int)meshData.normals.size() ? meshData.normals[vi].Y : 0;
				verts[vi].nz = vi < (int)meshData.normals.size() ? meshData.normals[vi].Z : 0;
				verts[vi].u = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].U : 0;
				float rawV = vi < (int)meshData.texcoords.size() ? meshData.texcoords[vi].V : 0;
				verts[vi].v = flipV ? (1.0f - rawV) : rawV;
				if ((si == 0 || si == 1 || si == 4 || si == 7 || strstr(nm, "WHEEL") || strstr(nm, "TREAD")) && vi < 2) {
					DEBUG_LOG(("[W3X_P5]   vert[%d] uv=(%.3f, rawV %.3f -> %.3f)%s mesh='%s'\n",
						vi, verts[vi].u, rawV, verts[vi].v, flipV ? " FLIP" : " noflip", nm));
				}
				verts[vi].tx = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].X : 0;
				verts[vi].ty = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].Y : 0;
				verts[vi].tz = vi < (int)meshData.tangents.size() ? tsign * meshData.tangents[vi].Z : 0;
				verts[vi].bx = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].X : 0;
				verts[vi].by = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Y : 0;
				verts[vi].bz = vi < (int)meshData.binormals.size() ? meshData.binormals[vi].Z : 0;
				// RA3 shader: int BoneIndex = floor(blendindices.x * 2); WorldBones
				// is laid out 2 float4 per bone ([2i]=quat, [2i+1]=offset), so the
				// raw bone index b must be stored AS-IS: floor(b*2)=2b points to the
				// bone's quat slot. Do NOT divide by 2.
				verts[vi].boneIdx = hasBones ? (float)meshData.boneIndices[vi] : (float)sub.boneIndex;
				verts[vi].boneWeight = hasBones && vi < (int)meshData.boneWeights.size() ? meshData.boneWeights[vi] : 1.0f;
				verts[vi].color = 0xFFFFFFFF;	// white vertex color (RA3 shader reads VertexColor)
				verts[vi].u2 = 0; verts[vi].v2 = 0;	// TEXCOORD1 (RA3 texcoordNEW: zero)
			}
			hr = dev9->CreateVertexBuffer(vertCount * VERTEX_STRIDE, 0, 0, D3DPOOL_MANAGED, &vb, NULL);
			if (SUCCEEDED(hr) && vb) {
				void *ptr; hr = vb->Lock(0, 0, &ptr, 0);
				if (SUCCEEDED(hr)) { memcpy(ptr, verts, vertCount * VERTEX_STRIDE); vb->Unlock(); }
			}
			delete[] verts;
		}

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
		subBuf.softBinding = softMesh;
		subBuf.name = sub.renderObjectName;
		subBuf.hasTangents = ((int)meshData.tangents.size() >= vertCount);
		subBuf.hasBinormals = ((int)meshData.binormals.size() >= vertCount);
		subBuf.origShader = meshData.fxShaderName;
		subBuf.constants = meshData.constants;
		// Carry the real local-space AABB from the .w3x <BoundingBox> node so
		// createRenderObject can build a correct model box (scene culling + the
		// projected ground shadow both depend on it).
		subBuf.boundMin[0] = meshData.boundMin[0];
		subBuf.boundMin[1] = meshData.boundMin[1];
		subBuf.boundMin[2] = meshData.boundMin[2];
		subBuf.boundMax[0] = meshData.boundMax[0];
		subBuf.boundMax[1] = meshData.boundMax[1];
		subBuf.boundMax[2] = meshData.boundMax[2];
		outData.subMeshes.push_back(subBuf);

		DEBUG_LOG(("[W3X_P5]   Sub-mesh '%s': %d verts, %d tris, bones=%d, tangents=%d, shader=%s\n",
			sub.renderObjectName.str(), vertCount, triCount, hasBones ? (int)meshData.boneIndices.size() : 0,
			(int)meshData.tangents.size(), meshData.fxShaderName.str()));

		// Classify every non-defaultw3d FX shader (defaultw3d.fx is a placeholder
		// that sub-parts use when they should render with the standard pipeline);
		// the model-wide shader is decided below from the full mesh set.
		if (!meshData.fxShaderName.isEmpty()
			&& strcmp(meshData.fxShaderName.str(), "defaultw3d.fx") != 0) {
			// REAL RA3 PBR shader via SAS-free override (w3x_soviet.fx compiles
			// VS_H_11skin + PS_H_ARPBR directly; objectssoviet.fx itself uses
			// VS_H_Array[VSchooserExpr()] SAS dynamic selection which this D3DX9
			// build rejects with X3116).
			// (w3x_rastest.fx was the TEMP isolation test that proved the skinned
			// VS + geometry + bones work — red model appeared, turret correct.)
			//
			// BASIC-convention meshes (single "Texture_0" + texture-ALPHA faction
			// mask, e.g. the infantry.fx soldiers) have no DiffuseTexture/NormalMap/
			// SpecMap constants. Binding them into the IS_OBJECT PBR path finds no
			// "Texture_0" parameter, so the DiffuseTexture sampler keeps the
			// previous model's texture (soldier wearing humvee camo). Route them to
			// the dedicated w3x_infantry.fx (real RA3 unified soldier VS from
			// head1-newvs.FXH + a BASIC pixel shader that reads the faction mask
			// from the texture alpha). PBR-convention meshes (vehicles) keep the
			// full PBR shader.
			bool isBasicConvention = false;
			for (size_t ci = 0; ci < meshData.constants.size() && !isBasicConvention; ci++) {
				if (meshData.constants[ci].type == W3X_CONSTANT_TEXTURE
					&& strcmp(meshData.constants[ci].name.str(), "Texture_0") == 0) {
					isBasicConvention = true;
				}
			}
			if (isBasicConvention) {
				if (!hasBasicMesh) {
					hasBasicMesh = true;
					firstBasicTech = meshData.techniqueIndex;
					firstBasicConst = meshData.constants;
				}
			} else {
				if (!hasPBRMesh) {
					hasPBRMesh = true;
					firstPBRTech = meshData.techniqueIndex;
					firstPBRConst = meshData.constants;
				}
			}
		}
	}

	// Decide the model-wide shader from ALL non-default sub-meshes. Any PBR
	// mesh forces the shared PBR shader (vehicles/buildings keep their
	// DiffuseTexture/NormalMap/SpecMap); soldier meshes are overridden
	// per-sub-mesh to w3x_infantry.fx in createRenderObject. A purely-BASIC
	// model (all soldiers) still routes the whole model to the infantry shader.
	if (hasPBRMesh) {
		outData.fxShaderName = "Shaders\\RA3\\w3x_soviet.fx";
		outData.techniqueIndex = firstPBRTech;
		outData.constants = firstPBRConst;
	} else if (hasBasicMesh) {
		outData.fxShaderName = "Shaders\\RA3\\w3x_infantry.fx";
		outData.techniqueIndex = firstBasicTech;
		outData.constants = firstBasicConst;
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
	data.boneNames.clear();
	data.boneParents.clear();
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
		robj->AddSubMesh(sm.vertexBuffer, sm.indexBuffer, sm.vertexCount, sm.triangleCount, sm.softBinding);
		robj->SetSubMeshTangent((int)i, sm.hasTangents, sm.hasBinormals);
		// Every sub-mesh keeps its own XML-declared textures. Without this the
		// shared-effect sub-meshes inherit the first sub-mesh's DiffuseTexture
		// (UP04 gun renders TavGattTank2 camo instead of TavBtMstr2 black metal).
		robj->SetSubMeshConstants((int)i, sm.constants);
		// Per-sub-mesh shader override: sub-meshes authored for the RA3 tread
		// shader keep their scrolling-tread effect via the SAS-free override
		// w3x_tread.fx instead of being forced onto the shared soviet shader.
		if (!sm.origShader.isEmpty()
			&& (strcmp(sm.origShader.str(), "objectsalliedtread.fx") == 0
				|| strstr(sm.origShader.str(), "tread") != NULL)) {
			robj->SetSubMeshShader((int)i, "Shaders\\RA3\\w3x_tread.fx", 0, sm.constants);
		}
		// Per-sub-mesh shader override: BASIC-convention soldier sub-meshes (their
		// .w3x FXShader is infantry.fx - a single Texture_0 + texture-alpha faction
		// mask) route to the dedicated w3x_infantry.fx so the soldier renders with
		// its own soldier texture. Without this, a mixed soldier+vehicle model
		// stays on the shared PBR shader and the soldier leaks the vehicle's
		// DiffuseTexture (soldier wearing vehicle camo) instead of its Texture_0.
		if (!sm.origShader.isEmpty()
			&& strcmp(sm.origShader.str(), "infantry.fx") == 0) {
			robj->SetSubMeshShader((int)i, "Shaders\\RA3\\w3x_infantry.fx", 0, sm.constants);
		}
		sm.vertexBuffer = NULL;
		sm.indexBuffer = NULL;
	}

	// FX shader + constants
	robj->SetFX(data.fxShaderName.str(), data.techniqueIndex, data.constants);

	// Bones
	if (data.boneCount > 0 && data.boneMatrixArray) {
		robj->SetBones(data.boneMatrixArray, data.boneCount);
	}
	// Bind LOCAL pose (rotations + translations) -> render obj so the animation
	// offset can be composed on the bind pose (weapon-in-hand / feet-planted).
	if (data.boneCount > 0 && !data.boneLocalQuat.empty() && !data.boneLocalTrans.empty()) {
		robj->SetBoneLocalPose(&data.boneLocalQuat[0], &data.boneLocalTrans[0], data.boneCount);
	}
	// Bone names -> render obj so ini WeaponFireFXBone etc can be resolved.
	robj->SetBoneNames(data.boneNames);
	// Bone parents -> render obj so turret rotation cascades to the barrel.
	robj->SetBoneParents(data.boneParents);

	// Bounds: union of the per-sub-mesh AABBs parsed from each .w3x <BoundingBox>.
	// The scene uses these for frustum culling AND the projected ground shadow
	// (W3DProjectedShadow::updateBounds projects this box to compute the shadow
	// footprint). Hardcoding a big box here made the projected shadow cover most
	// of the map, overflow the 4096-vertex shadow buffer and render nothing.
	Vector3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);
	for (size_t bi = 0; bi < data.subMeshes.size(); bi++) {
		const SubMeshBuffer &sm = data.subMeshes[bi];
		for (int k = 0; k < 3; k++) {
			if (sm.boundMin[k] < bmin[k]) bmin[k] = sm.boundMin[k];
			if (sm.boundMax[k] > bmax[k]) bmax[k] = sm.boundMax[k];
		}
	}
	// Degenerate (mesh missing <BoundingBox> / parse failed) -> fall back so
	// culling and shadows still function.
	if (bmin.X >= bmax.X || bmin.Y >= bmax.Y || bmin.Z >= bmax.Z) {
		bmin.Set(-100, -100, -100);
		bmax.Set(100, 100, 100);
		DEBUG_LOG(("[W3X_DIAG] W3X '%s' bounds MISSING -> FALLBACK box\n", m_loadedModelName.str()));
	}
	robj->SetBounds(bmin, bmax);
	DEBUG_LOG(("[W3X_DIAG] W3X '%s' bounds min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f)\n",
		m_loadedModelName.str(), bmin.X, bmin.Y, bmin.Z, bmax.X, bmax.Y, bmax.Z));

	// Add to scene
	W3DDisplay::m_3DScene->Add_Render_Object(robj);
	m_renderObj = robj;

	// Associate the drawable with the render object (mirrors W3DModelDraw:3137).
	// Without this, W3DView::pickDrawable reads Get_User_Data() == NULL and the
	// pick/hover can never resolve the W3X unit even though Cast_Ray hits it.
	if (getDrawable()) {
		robj->Set_User_Data(getDrawable()->getDrawableInfo());
	}

	// Set the render object's collision type for ray-picking (mirrors
	// W3DModelDraw). Without it the W3X collision type stays 0 and mouse clicks
	// can't select the unit (only drag-box works). Soldiers are KINDOF_SELECTABLE
	// -> PICK_TYPE_SELECTABLE.
	{
		const ThingTemplate *tmplate = getDrawable() ? getDrawable()->getTemplate() : NULL;
		if (tmplate && robj) {
			if (tmplate->isKindOf(KINDOF_SELECTABLE))
				robj->Set_Collision_Type(PICK_TYPE_SELECTABLE);
			else if (tmplate->isKindOf(KINDOF_SHRUBBERY))
				robj->Set_Collision_Type(PICK_TYPE_SHRUBBERY);
			else if (tmplate->isKindOf(KINDOF_MINE))
				robj->Set_Collision_Type(PICK_TYPE_MINES);
			else if (tmplate->isKindOf(KINDOF_FORCEATTACKABLE))
				robj->Set_Collision_Type(PICK_TYPE_FORCEATTACKABLE);
			else
				robj->Set_Collision_Type(0);
		}
	}

	DEBUG_LOG(("[W3X_P5]   W3XRenderObj added to scene: %d sub-meshes, fx=%s\n",
		(int)data.subMeshes.size(), data.fxShaderName.str()));

	// =====================================================================
	// SHADER-EFFECT DIAGNOSTICS (one-shot per model load)
	// Verifies whether each sub-mesh can actually produce PBR / bump-normal
	// output under the model-wide shared shader.
	//   - bump normals need native TANGENT+BINORMAL per vertex (else N stays
	//     unperturbed and the normal-map has no visible effect on that mesh)
	//   - a sub-mesh authored with a different FX (e.g. objectsalliedtread.fx)
	//     is overridden by the shared w3x_soviet.fx, losing its special
	//     features (e.g. tread scrolling UV animation)
	// =====================================================================
	{
		DEBUG_LOG(("[W3X_DIAG] W3X '%s' renders ALL %d sub-meshes with SHARED shader '%s' (technique %d)\n",
			m_loadedModelName.str(), (int)data.subMeshes.size(), data.fxShaderName.str(), data.techniqueIndex));
		for (size_t di = 0; di < data.subMeshes.size(); di++) {
			const SubMeshBuffer &d = data.subMeshes[di];
			const char *overrideMark = "";
			if (d.origShader.isNotEmpty() && strcmp(d.origShader.str(), "defaultw3d.fx") != 0
				&& strcmp(d.origShader.str(), data.fxShaderName.str()) != 0) {
				overrideMark = " <-- OVERRIDDEN from orig";
			}
			DEBUG_LOG(("[W3X_DIAG]   [%d] %-28s tangents=%d binormals=%d bones=%d origShader='%s'%s\n",
				(int)di, d.name.str(),
				d.hasTangents ? 1 : 0, d.hasBinormals ? 1 : 0,
				d.hasBones ? 1 : 0, d.origShader.str(), overrideMark));
			DEBUG_LOG(("[W3X_DIAG]     bump-normal: %s | PBR-shader: %s\n",
				(d.hasTangents && d.hasBinormals) ? "YES (tangent data present)" : "NO (tangent data missing -> normal map has no effect)",
				(d.origShader.isEmpty() || strcmp(d.origShader.str(), "defaultw3d.fx") == 0) ? "sub-mesh authored w/o PBR shader (defaultw3d placeholder)" : "YES (real PBR shader path)"));
		}
	}
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
	// Track the active condition state so weapon-fire / launch offset can find
	// this state's barrel bones. (This was never assigned before -> fireFX dead.)
	if (m_curState != state) {
		stopClientParticleSystems();
		m_curState = state;
		m_needRecalcBoneParticleSystems = true;
		// Switch animation playback mode and (re)load this state's animation.
		// A fresh load starts from the closed frame; ONCE_BACKWARDS (door close)
		// starts from the open frame so the door closes back down.
		bool prevAnimValid = m_animValid;
		m_animMode = state ? state->m_animationMode : W3X_ANIM_LOOP;
		if (state && !state->m_animationName.isEmpty()) {
			bool fresh = !prevAnimValid;
			if (prevAnimValid && m_curAnimName.compareNoCase(state->m_animationName.str()) != 0)
				fresh = true;
			loadAnimation(state->m_animationName.str());
			if (fresh && m_animValid)
				m_animFrame = (m_animMode == W3X_ANIM_ONCE_BACKWARDS) ? (float)(m_curAnim.numFrames - 1) : 0.0f;
		} else {
			// No animation in this state: return the skeleton to its bind pose so
			// the door renders closed (Release_Bone clears the animation override).
			m_animValid = false;
			if (m_renderObj) {
				for (int bi = 0; bi < m_renderObj->GetBoneCount(); bi++) m_renderObj->Release_Bone(bi);
			}
		}
	}
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
			// Name the render object with the model name so the volumetric shadow
			// geometry is cached per-model (not shared as "UNNAMED").
			if (m_renderObj) m_renderObj->Set_Name(targetModel);
			// Resolve this state's weapon barrel bones against the new skeleton.
			validateBarrelInfo(m_curState);
			// Resolve turret/pitch bones + load the state's animation.
			resolveTurretBones(m_curState);
			if (m_curState && !m_curState->m_animationName.isEmpty())
				loadAnimation(m_curState->m_animationName.str());
			// (Re)allocate the volumetric shadow now that the render object exists.
			// Drawable::allocateShadows() may have run before the model was built.
			if (m_shadow == NULL) allocateShadows();
		}
		// Mark as attempted regardless of success
		stopClientParticleSystems();
		m_needRecalcBoneParticleSystems = true;
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
		// Turret rotation + skeletal animation (pure client).
		handleClientTurretPositioning();
		updateAnimation();
		// Particle systems (ParticleSysBone): recreate on state/model change,
		// reposition at their bones every frame.
		recalcBonesForClientParticleSystems();
		updateBonesForClientParticleSystems();
	}
}

void W3XModelDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	m_fullyObscuredByShroud = (fullyObscured != FALSE);
	if (m_shadow) m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
}

Bool W3XModelDraw::isVisible() const
{
	return !m_fullyObscuredByShroud;
}

//-----------------------------------------------------------------------------
// ObjectDrawInterface implementations (mirror W3DModelDraw)
//-----------------------------------------------------------------------------

int W3XModelDraw::getBoneIndexByName(const AsciiString &boneName) const
{
	if (boneName.isEmpty()) return -1;
	AsciiString want(boneName);
	want.toLower();
	// Prefer the render object's bone-name lookup (has the live skeleton).
	if (m_renderObj) {
		int idx = m_renderObj->Get_Bone_Index(want.str());
		// Get_Bone_Index returns 0 for "not found"; bone 0 is roottransform, so
		// a non-empty name that resolves to 0 means no real match (or root).
		if (idx != 0) return idx;
	}
	// Fall back to the loaded model's bone name list (works before render obj
	// exists, e.g. logic-side projectile launch offset).
	for (int i = 0; i < (int)m_loadedModel.boneNames.size(); i++) {
		if (m_loadedModel.boneNames[i].compareNoCase(want) == 0)
			return i;
	}
	return -1;
}

void W3XModelDraw::validateBarrelInfo(const W3XConditionInfo *state) const
{
	if (!state) return;
	for (int wslot = 0; wslot < WEAPONSLOT_COUNT; wslot++) {
		if (state->m_barrelsValid[wslot]) continue;
		state->m_barrelsValid[wslot] = true;
		W3XWeaponBarrelInfoVec &vec = state->m_weaponBarrelInfoVec[wslot];
		vec.clear();

		const AsciiString &fxName  = state->m_weaponFireFXBoneName[wslot];
		const AsciiString &mfName  = state->m_weaponMuzzleFlashName[wslot];
		const AsciiString &lbName  = state->m_weaponProjectileLaunchBoneName[wslot];

		// If no bones declared for this slot, nothing to do.
		if (fxName.isEmpty() && mfName.isEmpty() && lbName.isEmpty())
			continue;

		// Look for barrel-suffixed bones: "%s01", "%s02", ... (like W3D).
		char buffer[128];
		for (int i = 1; i <= 8; i++) {
			W3XWeaponBarrelInfo info;
			if (!fxName.isEmpty()) {
				sprintf(buffer, "%s%02d", fxName.str(), i);
				info.m_fxBone = getBoneIndexByName(AsciiString(buffer));
			}
			if (!mfName.isEmpty()) {
				sprintf(buffer, "%s%02d", mfName.str(), i);
				info.m_muzzleFlashBone = getBoneIndexByName(AsciiString(buffer));
			}
			if (!lbName.isEmpty()) {
				sprintf(buffer, "%s%02d", lbName.str(), i);
				info.m_launchBone = getBoneIndexByName(AsciiString(buffer));
			}
			if (info.m_fxBone < 0 && info.m_muzzleFlashBone < 0 && info.m_launchBone < 0)
				break;
			vec.push_back(info);
		}
		// If no suffixed barrels matched, try the bare names once.
		if (vec.empty()) {
			W3XWeaponBarrelInfo info;
			if (!fxName.isEmpty())  info.m_fxBone  = getBoneIndexByName(fxName);
			if (!mfName.isEmpty())  info.m_muzzleFlashBone = getBoneIndexByName(mfName);
			if (!lbName.isEmpty())  info.m_launchBone = getBoneIndexByName(lbName);
			if (info.m_fxBone >= 0 || info.m_muzzleFlashBone >= 0 || info.m_launchBone >= 0)
				vec.push_back(info);
		}
		DEBUG_LOG(("[W3X_P5] validateBarrelInfo wslot=%d barrels=%d (fx=%s mf=%s lb=%s)\n",
			wslot, (int)vec.size(),
			fxName.isEmpty() ? "-" : fxName.str(),
			mfName.isEmpty() ? "-" : mfName.str(),
			lbName.isEmpty() ? "-" : lbName.str()));
	}
}

const W3XWeaponBarrelInfoVec &W3XModelDraw::resolveBarrelVec(WeaponSlotType wslot, const W3XConditionInfo *primary) const
{
	// The weapon-fire query runs in the current state (FIRING_A / BETWEEN_FIRING
	// _SHOTS_A), but units declare WeaponFireFXBone/WeaponMuzzleFlash/WeaponLaunchBone
	// only in the NONE state. If the primary state has no fire bones for this slot,
	// fall back to the first state that declares them (preferring NONE) so the
	// projectile launches from the muzzle instead of the unit origin (feet).
	static W3XWeaponBarrelInfoVec s_emptyVec;	// never populated, returned when none declare bones
	if (primary) {
		validateBarrelInfo(primary);
		if (!primary->m_weaponBarrelInfoVec[wslot].empty())
			return primary->m_weaponBarrelInfoVec[wslot];
	}
	const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
	if (md) {
		for (size_t si = 0; si < md->m_conditionStates.size(); si++) {
			const W3XConditionInfo &st = md->m_conditionStates[si];
			validateBarrelInfo(&st);
			if (!st.m_weaponBarrelInfoVec[wslot].empty())
				return st.m_weaponBarrelInfoVec[wslot];
		}
	}
	return s_emptyVec;
}

//-----------------------------------------------------------------------------
// Turret / animation (RA3 skeletal)
//-----------------------------------------------------------------------------

void W3XModelDraw::resolveTurretBones(const W3XConditionInfo *state) const
{
	if (!state) return;
	state->m_turretAngleBone = state->m_turretAngleName.isEmpty() ? -1
		: getBoneIndexByName(state->m_turretAngleName);
	state->m_turretPitchBone = state->m_turretPitchName.isEmpty() ? -1
		: getBoneIndexByName(state->m_turretPitchName);
	DEBUG_LOG(("[W3X_P5] resolveTurretBones: angle=%d pitch=%d (names '%s'/'%s')\n",
		state->m_turretAngleBone, state->m_turretPitchBone,
		state->m_turretAngleName.str(), state->m_turretPitchName.str()));
}

void W3XModelDraw::handleClientTurretPositioning()
{
	// Pure-client: rotate the turret (and optionally pitch) bone to the AI's
	// current turret/pitch angles. Must never touch GameLogic.
	if (!m_curState || !m_renderObj) return;
	const W3XConditionInfo *state = m_curState;
	Object *obj = getDrawable() ? getDrawable()->getObject() : NULL;
	if (!obj) return;
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return;

	Real turretAngle = 0, turretPitch = 0;
	if (state->m_turretAngleBone >= 0 || state->m_turretPitchBone >= 0) {
		ai->getTurretRotAndPitch(TURRET_MAIN, &turretAngle, &turretPitch);
		if (state->m_turretAngleBone >= 0) {
			Matrix3D turretXfrm(1);
			turretXfrm.Rotate_Z(turretAngle);
			m_renderObj->Control_Bone(state->m_turretAngleBone, turretXfrm);
		}
		if (state->m_turretPitchBone >= 0) {
			Matrix3D pitchXfrm(1);
			pitchXfrm.Rotate_Y(-turretPitch);
			m_renderObj->Control_Bone(state->m_turretPitchBone, pitchXfrm);
		}
	}
}

bool W3XModelDraw::loadAnimation(const char *animName)
{
	if (!animName || !animName[0]) return false;
	if (m_curAnimName.compareNoCase(animName) == 0 && m_animValid)
		return true;	// already loaded

	// RA3 animations live next to the model: Art/W3X/<name>.w3x
	char path[512];
	sprintf(path, W3X_ASSET_DIR "%s.w3x", animName);
	W3XAnimation anim;
	if (!W3XLoader::ParseAnimation(path, anim)) {
		DEBUG_LOG(("[W3X_P5] loadAnimation: failed to parse '%s'\n", path));
		return false;
	}
	m_curAnim = anim;
	m_curAnimName = animName;
	m_animFrame = 0;
	m_animPrevFrame = -1;
	m_animLastFrame = -1;	// restart timing
	m_animValid = true;
	DEBUG_LOG(("[W3X_P5] loadAnimation: '%s' frames=%d channels=%d\n",
		anim.name.str(), anim.numFrames, (int)anim.channels.size()));

#if defined(DEBUG_LOGGING)
	// ONE-SHOT per animation: dump the skeleton bone-name -> index map so the
	// [W3X_ANIM] foot/hip/muzzle probe positions can be mapped to real bones
	// (EUTEIROCKETS: 2=hips 16=leftfoot 19=rightfoot 21=fx_laser).
	static AsciiString s_lastBoneDumpAnim;
	if (m_renderObj && s_lastBoneDumpAnim.compareNoCase(animName) != 0) {
		s_lastBoneDumpAnim = animName;
		for (int bi = 0; bi < m_renderObj->GetBoneCount(); bi++) {
			const char *bn = m_renderObj->Get_Bone_Name(bi);
			DEBUG_LOG(("[W3X_ANIM]   bone[%d]=%s\n", bi, bn ? bn : "?"));
		}
	}
#endif
	return true;
}

void W3XModelDraw::updateAnimation()
{
	if (!m_animValid || !m_renderObj) return;
	if (m_curAnim.numFrames < 2) return;

	// Advance one logic/frame step; ONCE_BACKWARDS plays in reverse.
	Int frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (m_animLastFrame >= 0) {
		float d = (float)(frame - m_animLastFrame);
		if (m_animMode == W3X_ANIM_ONCE_BACKWARDS) m_animFrame -= d;
		else m_animFrame += d;
	}
	m_animLastFrame = frame;

	int totalFrames = m_curAnim.numFrames;
	int lastIdx = totalFrames - 1;

	// Playback mode: ONCE holds at the last frame, ONCE_BACKWARDS at the first,
	// LOOP wraps, MANUAL freezes in place (hold open/closed).
	if (m_animMode == W3X_ANIM_ONCE) {
		if (m_animFrame >= (float)lastIdx) m_animFrame = (float)lastIdx;
		else if (m_animFrame < 0) m_animFrame = 0.0f;
	} else if (m_animMode == W3X_ANIM_ONCE_BACKWARDS) {
		if (m_animFrame < 0) m_animFrame = 0.0f;
	} else if (m_animMode == W3X_ANIM_MANUAL) {
		// hold current frame
	} else {
		while (m_animFrame >= (float)totalFrames) m_animFrame -= (float)totalFrames;
		if (m_animFrame < 0) m_animFrame = 0.0f;
	}

	// =====================================================================
	// AUTHORITATIVE-POSE DIAGNOSTIC (every ~32 frames): report the composed
	// MODEL-space positions of the feet / hips / weapon muzzle so the two
	// interpretation models can be judged against the real skeleton. For the
	// EU AntiVehicleInfantry (EUTEIROCKETS + AUAntiVehicleInfantry_SBIDA) the
	// bind pose stands (feet Z~1.4, hips Z~10.6); the SBIDA channel is constant
	// and its RAW application folds the right leg up (rightfoot Z~9.8) while
	// frame-0 normalization keeps it planted at Z~1.4. This probe shows which
	// one the running build produces (bind + raw-fold reference in the print).
	// =====================================================================
	if ((frame & 0x1F) == 0 && m_renderObj && totalFrames > 0) {
		const char *probeNames[4] = { "leftfoot", "rightfoot", "hips", "fx_laser" };
		float px[4][3] = { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };
		bool found[4] = { false, false, false, false };
		for (int k = 0; k < 4; k++) {
			int bi = getBoneIndexByName(AsciiString(probeNames[k]));
			if (bi >= 0) {
				Matrix3D bt = m_renderObj->Get_Bone_Transform_Model(bi);
				Vector3 p = bt.Get_Translation();
				px[k][0] = p.X; px[k][1] = p.Y; px[k][2] = p.Z;
				found[k] = true;
			}
		}
		DEBUG_LOG(("[W3X_ANIM] anim='%s' frame=%.0f/%d | "
			"footL%s=(%.2f,%.2f,%.2f) footR%s=(%.2f,%.2f,%.2f) "
			"hips%s=(%.2f,%.2f,%.2f) muzzle%s=(%.2f,%.2f,%.2f) | "
			"ref: bind feetZ~1.4 planted / raw-fold rightfootZ~9.8\n",
			m_curAnimName.str(), m_animFrame, totalFrames,
			found[0] ? "" : "?", px[0][0], px[0][1], px[0][2],
			found[1] ? "" : "?", px[1][0], px[1][1], px[1][2],
			found[2] ? "" : "?", px[2][0], px[2][1], px[2][2],
			found[3] ? "" : "?", px[3][0], px[3][1], px[3][2]));
	}

	int f0 = (int)m_animFrame;
	float t = m_animFrame - (float)f0;

	// Clear the previous frame's animation overrides so a bone outside the
	// current animation's channel set returns to its bind pose (leaves turret
	// Control_Bone untouched). updateAnimation re-applies every channel below.
	m_renderObj->ResetAnimationBones();

	for (size_t ci = 0; ci < m_curAnim.channels.size(); ci++) {
		const W3XAnimChannel &ch = m_curAnim.channels[ci];

		// --- quaternion (orientation) channel ---
		if (!ch.quatFrames.empty()) {
			// 4 floats per frame; index into per-bone quat array.
			int stride = 4;
			int nFrames = (int)ch.quatFrames.size() / stride;
			if (nFrames >= 1) {
				int ff0 = f0 % nFrames;
				int ff1 = (ff0 + 1) % nFrames;
				// At the clamped end of ONCE / start of ONCE_BACKWARDS, hold the quat.
				if (m_animMode == W3X_ANIM_ONCE && f0 >= lastIdx) ff1 = ff0;
				if (m_animMode == W3X_ANIM_ONCE_BACKWARDS && f0 <= 0) ff1 = ff0;
				const float *q0 = &ch.quatFrames[ff0 * stride];
				const float *q1 = &ch.quatFrames[ff1 * stride];
				// Normalize the keyframe endpoints so the slerp runs on unit
				// quaternions (RA3 data is unit; dirty extractions may not be).
				float nq0[4], nq1[4];
				QuatNormalize(nq0, q0);
				QuatNormalize(nq1, q1);
				Quaternion a(nq0[0], nq0[1], nq0[2], nq0[3]);
				Quaternion b(nq1[0], nq1[1], nq1[2], nq1[3]);
				Quaternion r;
				Slerp(r, a, b, t);
				// The RA3 channel is the bone's ABSOLUTE local rotation (channel =
				// animLocal × bind^-1, so compose = channel × bind). Apply it directly
				// — NO frame-0 normalization. Frame-0 normalization forced clip-frame-0
				// = bind, which over-rotated clips whose frame-0 pose is not the bind
				// pose (RUNA run swung the legs to hip height = rubber joints).
				float outQ[4] = { r.X, r.Y, r.Z, r.W };
				m_renderObj->SetBoneAnimQuat(ch.pivot, outQ);
			}
		}

		// --- translation channel (X/Y/ZTranslation, 3 floats per frame) ---
		if (!ch.transFrames.empty()) {
			int tstride = 3;
			int tnFrames = (int)ch.transFrames.size() / tstride;
			if (tnFrames >= 1) {
				// RA3 channel translations are the bone's LOCAL position relative to
				// its parent. Apply the interpolated value directly (NO frame-0
				// subtract); composeControlledBones adds it to the bind local
				// translation. Matches updateAnimation's raw-channel semantic.
				int tf0 = f0 % tnFrames;
				int tf1 = (tf0 + 1) % tnFrames;
				if (m_animMode == W3X_ANIM_ONCE && f0 >= lastIdx) tf1 = tf0;
				if (m_animMode == W3X_ANIM_ONCE_BACKWARDS && f0 <= 0) tf1 = tf0;
				const float *t0 = &ch.transFrames[tf0 * tstride];
				const float *t1 = &ch.transFrames[tf1 * tstride];
				float outT[3];
				outT[0] = t0[0] + (t1[0] - t0[0]) * t;
				outT[1] = t0[1] + (t1[1] - t0[1]) * t;
				outT[2] = t0[2] + (t1[2] - t0[2]) * t;
				m_renderObj->SetBoneAnimTrans(ch.pivot, outT);
			}
		}
	}
}

Bool W3XModelDraw::clientOnly_getRenderObjInfo(Coord3D *pos, Real *boundingSphereRadius, Matrix3D *transform) const
{
	if (!m_renderObj || !pos || !boundingSphereRadius || !transform) return false;
	Vector3 objPos = m_renderObj->Get_Position();
	pos->x = objPos.X; pos->y = objPos.Y; pos->z = objPos.Z;
	*transform = m_renderObj->Get_Transform();
	*boundingSphereRadius = m_renderObj->Get_Bounding_Sphere().Radius;
	return true;
}

Bool W3XModelDraw::clientOnly_getRenderObjBoundBox(OBBoxClass *boundbox) const
{
	// Not needed for W3X rendering; return false so callers fall back.
	return false;
}

Bool W3XModelDraw::clientOnly_getRenderObjBoneTransform(const AsciiString &boneName, Matrix3D *set_tm) const
{
	if (!m_renderObj || !set_tm) return false;
	int idx = getBoneIndexByName(boneName);
	if (idx < 0) return false;
	*set_tm = m_renderObj->Get_Bone_Transform(idx);
	return true;
}

Int W3XModelDraw::getPristineBonePositionsForConditionState(
	const ModelConditionFlags &condition, const char *boneNamePrefix, Int startIndex,
	Coord3D *positions, Matrix3D *transforms, Int maxBones) const
{
	// Return the model-space bone transforms for the matching condition state.
	// Positions/transforms here are "pristine" (model space at origin) - the
	// caller concatenates the object transform.
	const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
	const W3XConditionInfo *state = md ? md->findBestConditionState(condition) : NULL;
	if (!state || !positions) return 0;

	Int count = 0;
	for (Int i = startIndex; i < startIndex + maxBones && i < (Int)m_loadedModel.boneNames.size(); i++) {
		// Match bone name prefix (suffix digits allowed, like W3D).
		if (boneNamePrefix && boneNamePrefix[0]) {
			if (strncmp(m_loadedModel.boneNames[i].str(), boneNamePrefix, strlen(boneNamePrefix)) != 0)
				continue;
		}
		const float *bq = &m_loadedModel.boneMatrixArray[i * 8 + 0];
		const float *bo = &m_loadedModel.boneMatrixArray[i * 8 + 4];
		if (transforms) {
			// Model-space bone transform: use the base-pose (quat+offset) only,
			// no object world transform (pristine = at origin).
			Matrix3D mtx;
			mtx.Make_Identity();
			Quaternion q(bq[0], bq[1], bq[2], bq[3]);
			mtx.Set_Rotation(q);
			mtx.Set_Translation(Vector3(bo[0], bo[1], bo[2]));
			transforms[count] = mtx;
		}
		if (positions) {
			positions[count].x = bo[0];
			positions[count].y = bo[1];
			positions[count].z = bo[2];
		}
		count++;
	}
	return count;
}

Int W3XModelDraw::getCurrentBonePositions(
	const char *boneNamePrefix, Int startIndex,
	Coord3D *positions, Matrix3D *transforms, Int maxBones) const
{
	// No skeletal animation yet -> current == pristine. Reuse the pristine path
	// but without a condition (uses whatever model is currently loaded).
	if (!positions) return 0;
	Int count = 0;
	for (Int i = startIndex; i < startIndex + maxBones && i < (Int)m_loadedModel.boneNames.size(); i++) {
		const AsciiString &boneName = m_loadedModel.boneNames[i];
		if (boneNamePrefix && boneNamePrefix[0]) {
			if (strncmp(boneName.str(), boneNamePrefix, strlen(boneNamePrefix)) != 0)
				continue;
		}
		const float *bq = &m_loadedModel.boneMatrixArray[i * 8 + 0];
		const float *bo = &m_loadedModel.boneMatrixArray[i * 8 + 4];
		if (transforms) {
			Matrix3D mtx;
			mtx.Make_Identity();
			Quaternion q(bq[0], bq[1], bq[2], bq[3]);
			mtx.Set_Rotation(q);
			mtx.Set_Translation(Vector3(bo[0], bo[1], bo[2]));
			transforms[count] = mtx;
		}
		positions[count].x = bo[0];
		positions[count].y = bo[1];
		positions[count].z = bo[2];
		count++;
	}
	return count;
}

Bool W3XModelDraw::getCurrentWorldspaceClientBonePositions(const char *boneName, Matrix3D &transform) const
{
	if (!m_renderObj) return false;
	int idx = getBoneIndexByName(AsciiString(boneName));
	if (idx < 0) return false;
	transform = m_renderObj->Get_Bone_Transform(idx);
	return true;
}

Bool W3XModelDraw::getProjectileLaunchOffset(
	const ModelConditionFlags &condition, WeaponSlotType wslot, Int specificBarrelToUse,
	Matrix3D *launchPos, WhichTurretType tur, Coord3D *turretRotPos, Coord3D *turretPitchPos) const
{
	// CRITICAL: once getObjectDrawInterface() returns this, the logic's weapon
	// code routes launch offsets through here. Returning false triggers a
	// DEBUG_CRASH (Weapon.cpp), so always return true when a state exists and
	// fall back to the unit-center matrix when no launch bone is defined.
	const W3XModelDrawModuleData *md = (const W3XModelDrawModuleData *)getModuleData();
	const W3XConditionInfo *state = md ? md->findBestConditionState(condition) : NULL;
	if (!state) {
		DEBUG_LOG(("[W3X_FIRE] getProjectileLaunchOffset NO STATE wslot=%d -> false (unit-center)\n", wslot));
		return false;
	}
	validateBarrelInfo(state);
	const W3XWeaponBarrelInfoVec &vec = resolveBarrelVec(wslot, state);
	DEBUG_LOG(("[W3X_FIRE] getProjectileLaunchOffset ENTRY anim='%s' wslot=%d barrels=%d\n",
		state->m_animationName.str(), wslot, (int)vec.size()));
	if (launchPos) {
		if (!vec.empty()) {
			int bi = (specificBarrelToUse < 0 || specificBarrelToUse >= (Int)vec.size()) ? 0 : specificBarrelToUse;
			// Use the CURRENT animated model-space transform of the launch bone
			// (WeaponProjectileLaunchBone, else WeaponFireFXBone). The pristine
			// m_projectileOffsetMtx was never populated for W3X models, so the
			// projectile launched from the unit origin (feet); this follows the
			// animated bone instead (fx_laser sits on the launcher, a child of the
			// weapon bone).
			int lb = vec[bi].m_launchBone;
			if (lb < 0) lb = vec[bi].m_fxBone;
			if (lb >= 0 && m_renderObj) {
				Matrix3D lm;
				lm.Make_Identity();
				// When the render is ALREADY in the resolved state (e.g. a tank
				// playing its firing animation with the turret aimed), use the LIVE
				// pose: it carries the current animation + turret Control_Bone so the
				// muzzle follows the aimed turret.
				if (m_curState == state) {
					lm = m_renderObj->Get_Bone_Transform_Model(lb);
				} else {
					// Render is still in another (previous) state at the instant of
					// fire — for the missile soldier the launch query runs while the
					// render is in the idle state, whose tube is held ACROSS the
					// chest, so the live muzzle sits on the RIGHT side. Evaluate the
					// resolved state's animation (SATEA: tube aiming forward, hands
					// front/back) so the projectile starts at the FORWARD muzzle.
					if (state && !state->m_animationName.isEmpty()) {
						char path[512];
						sprintf(path, W3X_ASSET_DIR "%s.w3x", state->m_animationName.str());
						W3XAnimation fireAnim;
						if (W3XLoader::ParseAnimation(path, fireAnim)) {
							// RA3 FrameForPristineBonePositions: the launch uses the
							// state's pristine frame (e.g. 4 for the Javelin firing
							// state, where the launcher is properly aimed), not frame 0.
							float pFrame = (float)state->m_frameForPristineBonePositions;
							lm = m_renderObj->Get_Bone_Transform_Model_Anim(&fireAnim, lb, pFrame);
						} else {
							lm = m_renderObj->Get_Bone_Transform_Model(lb);
						}
					} else {
						lm = m_renderObj->Get_Bone_Transform_Model(lb);
					}
				}
				*launchPos = lm;
				Vector3 lt = launchPos->Get_Translation();
				DEBUG_LOG(("[W3X_FIRE] getProjectileLaunchOffset launchBone=%d model=(%.2f,%.2f,%.2f)%s\n",
					lb, lt.X, lt.Y, lt.Z, (m_curState == state) ? " [live]" : " [state-anim]"));
			} else {
				DEBUG_LOG(("[W3X_FIRE] getProjectileLaunchOffset NO launch bone (lb=%d), identity\n", lb));
				launchPos->Make_Identity();	// no launch bone -> unit center
			}
		} else {
			DEBUG_LOG(("[W3X_FIRE] getProjectileLaunchOffset anim='%s' wslot=%d BARRELS EMPTY -> identity (fire bones not in this state?)\n",
				state->m_animationName.str(), wslot));
			launchPos->Make_Identity();
		}
	}
	if (turretRotPos) turretRotPos->zero();
	if (turretPitchPos) turretPitchPos->zero();
	return true;
}

Bool W3XModelDraw::handleWeaponFireFX(
	WeaponSlotType wslot, Int specificBarrelToUse, const FXList *fxl,
	Real weaponSpeed, const Coord3D *victimPos, Real damageRadius)
{
	DEBUG_LOG(("[W3X_FIRE] handleWeaponFireFX ENTRY wslot=%d curState=%s\n",
		wslot, m_curState ? m_curState->m_animationName.str() : "(none)"));
	if (!m_curState || !m_renderObj) return false;
	validateBarrelInfo(m_curState);
	const W3XWeaponBarrelInfoVec &vec = resolveBarrelVec(wslot, m_curState);
	if (vec.empty()) {
		DEBUG_LOG(("[W3X_FIRE] handleWeaponFireFX curState='%s' BARRELS EMPTY -> false\n",
			m_curState->m_animationName.str()));
		return false;
	}
	int bi = (specificBarrelToUse < 0 || specificBarrelToUse >= (Int)vec.size()) ? 0 : specificBarrelToUse;
	const W3XWeaponBarrelInfo &info = vec[bi];
	if (fxl && info.m_fxBone >= 0) {
		if (!m_renderObj->Is_Hidden()) {
			Matrix3D mtx = m_renderObj->Get_Bone_Transform(info.m_fxBone);
			Coord3D pos;
			pos.x = mtx.Get_X_Translation();
			pos.y = mtx.Get_Y_Translation();
			pos.z = mtx.Get_Z_Translation();
			DEBUG_LOG(("[W3X_FIRE] handleWeaponFireFX bone=%d '%s' world=(%.2f,%.2f,%.2f)\n",
				info.m_fxBone, vec[bi].m_fxBone >= 0 ? "fx" : "-", pos.x, pos.y, pos.z));
			FXList::doFXPos(fxl, &pos, &mtx, weaponSpeed, victimPos, damageRadius);
			return true;
		}
	}
	return false;	// caller falls back to unit position
}

Int W3XModelDraw::getBarrelCount(WeaponSlotType wslot) const
{
	if (!m_curState) return 0;
	validateBarrelInfo(m_curState);
	return (Int)resolveBarrelVec(wslot, m_curState).size();
}

void W3XModelDraw::setHidden(Bool h)
{
	if (m_renderObj) m_renderObj->Set_Hidden(h != FALSE);
}

void W3XModelDraw::replaceIndicatorColor(Color color)
{
	if (m_renderObj) m_renderObj->SetRecolorColor((unsigned int)color);
}


//-----------------------------------------------------------------------------
// Particle systems (ParticleSysBone attachment)
//-----------------------------------------------------------------------------

// Recreate the particle systems for the active condition state at their bones.
// Runs only when m_needRecalcBoneParticleSystems is set (state/model changed).
void W3XModelDraw::recalcBonesForClientParticleSystems()
{
	if (!m_needRecalcBoneParticleSystems) return;
	m_needRecalcBoneParticleSystems = false;

	const Drawable *drawable = getDrawable();
	if (!drawable || !m_curState || drawable->testDrawableStatus(DRAWABLE_STATUS_NO_STATE_PARTICLES)) return;

	for (W3XParticleSysBoneInfoVector::const_iterator it = m_curState->m_particleSysBones.begin();
		it != m_curState->m_particleSysBones.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(it->particleSystemTemplate);
		if (!sys) continue;

		Coord3D pos; pos.zero();
		Real rotation = 0.0f;

		int boneIndex = m_renderObj ? m_renderObj->Get_Bone_Index(it->boneName.str()) : 0;
		if (boneIndex != 0) {
			// Get the bone transform in model space (zero the world transform first).
			Matrix3D originalTransform = m_renderObj->Get_Transform();
			Matrix3D tmp(true);
			tmp.Scale(drawable->getScale());
			m_renderObj->Set_Transform(tmp);
			const Matrix3D boneTransform = m_renderObj->Get_Bone_Transform(boneIndex);
			Vector3 vpos = boneTransform.Get_Translation();
			rotation = boneTransform.Get_Z_Rotation();
			m_renderObj->Set_Transform(originalTransform);
			pos.x = vpos.X; pos.y = vpos.Y; pos.z = vpos.Z;
		}

		sys->setPosition(&pos);
		sys->rotateLocalTransformZ(rotation);
		sys->attachToDrawable(drawable);
		sys->setSaveable(FALSE);
		if (drawable->isDrawableEffectivelyHidden() || m_fullyObscuredByShroud) sys->stop();

		W3XParticleSysTracker tracker;
		tracker.id = sys->getSystemID();
		tracker.boneIndex = boneIndex;
		m_particleSystemIDs.push_back(tracker);
	}
}

// Per-frame: reposition the attached particle systems at their (world) bones.
// Called by AnimatedParticleSysBoneClientUpdate / the draw update.
Bool W3XModelDraw::updateBonesForClientParticleSystems()
{
	const Drawable *drawable = getDrawable();
	if (!drawable || !m_curState || !m_renderObj) return false;

	for (std::vector<W3XParticleSysTracker>::const_iterator it = m_particleSystemIDs.begin();
		it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem(it->id);
		int boneIndex = it->boneIndex;
		if (!sys || boneIndex == 0) continue;

		const Matrix3D boneTransform = m_renderObj->Get_Bone_Transform(boneIndex);
		Vector3 vpos = boneTransform.Get_Translation();
		Coord3D pos; pos.x = vpos.X; pos.y = vpos.Y; pos.z = vpos.Z;
		sys->setPosition(&pos);
		sys->rotateLocalTransformZ(boneTransform.Get_Z_Rotation());
		sys->setLocalTransform(&boneTransform);
		sys->setSkipParentXfrm(true);
	}
	return true;
}

// Kill every particle system created for this draw module.
void W3XModelDraw::stopClientParticleSystems()
{
	for (std::vector<W3XParticleSysTracker>::const_iterator it = m_particleSystemIDs.begin();
		it != m_particleSystemIDs.end(); ++it)
	{
		ParticleSystem *sys = TheParticleSystemManager->findParticleSystem(it->id);
		if (sys) sys->stop();
	}
	m_particleSystemIDs.clear();
}
