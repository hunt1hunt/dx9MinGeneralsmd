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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3XEffectManager                                             *
 *                                                                                             *
 *                     $Archive::                                                             $*
 *                                                                                             *
 *                       Author:: hzc                                                          *
 *                                                                                             *
 *                    $Modtime::                                                             $*
 *                                                                                             *
 *                   $Revision::                                                             $*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   W3XEffectManager::Instance -- singleton access                                            *
 *   W3XEffectManager::FindCacheEntry -- linear search in cache array                          *
 *   W3XEffectManager::GetEffect -- load/cache D3DXEffect                                     *
 *   W3XEffectManager::ReleaseEffect -- release and possibly destroy effect                   *
 *   W3XEffectManager::OnLostDevice -- call ID3DXEffect::OnLostDevice for all cached effects  *
 *   W3XEffectManager::OnResetDevice -- call ID3DXEffect::OnResetDevice for all cached effects*
 *   W3XEffectManager::BindEngineConstants -- auto-bind engine globals                        *
 *   W3XEffectManager::BindParameter -- bind one parameter by name                            *
 *   W3XEffectManager::LogEffectInfo -- log effect metadata                                    *
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"
#include "W3DDevice/GameClient/W3XEffectManager.h"
#include "Common/GlobalData.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "matrix4.h"
#include "camera.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "GameClient/View.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/ww3d.h"
#include "ffactory.h"
#include "rawfile.h"
#include "wwdebug.h"
#include "wwmemlog.h"

// D3DX9 headers (available through global VS include search paths)
#include <d3dx9effect.h>
#include <d3dx9shader.h>

// The VC6-era D3DX9 SDK headers may not define D3DXSHADER_USE_SAS even though
// the runtime supports it (the RA3 shaders rely on SAS). Define it explicitly.
#ifndef D3DXSHADER_USE_SAS
#define D3DXSHADER_USE_SAS 0x40000000
#endif

//=============================================================================
// Statics
//=============================================================================

W3XEffectManager *W3XEffectManager::m_instance = NULL;

// Name-matching table: parameter name patterns → engine value source
// The '?' is a single-character wildcard in case-insensitive ASCII comparison
struct EngineConstantBinding
{
	const char *namePattern;	// parameter name to match (case-insensitive)
	int semanticGroup;			// which engine value to bind:
								//  0 = skip (manual binding only)
								//  1 = WorldViewProj matrix
								//  2 = World matrix
								//  3 = View matrix
								//  4 = Projection matrix
								//  5 = SunDirection (float3)
								//  6 = SunColor (float3)
								//  7 = AmbientColor (float3)
								//  8 = EyePosition (float3)
								//  9 = Time (float)
								// 10 = ViewProj matrix
								// 11 = CameraRight (float3)
								// 12 = CameraUp (float3)
};

static const EngineConstantBinding s_bindings[] =
{
	// WorldViewProj matrices (group 1)
	{ "WorldViewProj",		1 },
	{ "WorldViewProjection", 1 },
	{ "WVP",				1 },
	{ "MAXwvp",				1 },	// RA3 shader
	// World matrix (group 2)
	{ "WorldMatrix",		2 },
	{ "World",				2 },
	{ "MAXworld",			2 },	// RA3 shader
	// View matrix (group 3)
	{ "ViewMatrix",			3 },
	{ "View",				3 },
	{ "MAXview",				3 },	// RA3 shader
	// Projection matrix (group 4)
	{ "ProjectionMatrix",	4 },
	{ "Projection",			4 },
	{ "Proj",				4 },
	{ "MAXprojection",		4 },	// RA3 shader
	// ViewProj matrix (group 10)
	{ "ViewProj",			10 },
	{ "ViewProjection",		10 },	// RA3 shader (semantic ViewProjection)
	// Sun / light (groups 5-6)
	{ "SunDirection",		5 },
	{ "LightDirection",		5 },
	{ "SunColor",			6 },
	{ "LightColor",			6 },
	// Ambient (group 7)
	{ "AmbientColor",		7 },
	{ "AmbientLightColor",	7 },	// RA3 shader
	{ "Ambient",			7 },
	// Camera / eye position (group 8)
	{ "EyePosition",		8 },
	{ "CameraPos",			8 },
	{ "CameraPosition",		8 },
	// Time (group 9)
	{ "GameTime",			9 },
	{ "Time",				9 },
	// Camera frame (groups 11-12)
	{ "CameraRight",		11 },
	{ "CameraUp",			12 },
	// View inverse (group 13)
	{ "ViewInverse",		13 },
	{ "MAXviewinv",			13 },	// RA3 shader (semantic ViewInverse)
	// DirectionalLight struct array (group 14) - RA3 sun + skybox accents
	{ "DirectionalLight",	14 },
	// NoCloudMultiplier (group 15) - RA3 sky/cloud light multiplier (must be 1 for sunlight)
	{ "NoCloudMultiplier",	15 },
	// TintColor (group 16) - output multiplier; 0 would black the result
	{ "TintColor",			16 },
	// OpacityOverride (group 17) - 1 = opaque, <0.985 enables alpha blend
	{ "OpacityOverride",	17 },
	// RecolorColor (group 18) - faction color, default white
	{ "RecolorColor",		18 },
	// NumPointLights (group 19) - 0 disables point light loop
	{ "NumPointLights",		19 },
	{ NULL, 0 }	// terminator
};


//=============================================================================
// Camera matrix helpers
// Get the current view/projection matrices from the actual scene camera
// (DX8Wrapper::Get_Transform returns identity in the draw-module context)
//=============================================================================
static void GetCameraViewMatrix(Matrix4x4 &viewOut)
{
	// DIAG: report camera availability once
	static bool camDiag = false;
	if (!camDiag) {
		DEBUG_LOG(("[W3X_P3] CamDiag: TheTacticalView=%p, get3DCamera=%p\n",
			(void*)TheTacticalView,
			TheTacticalView ? (void*)static_cast<W3DView*>(TheTacticalView)->get3DCamera() : NULL));
		camDiag = true;
	}

	// Prefer the tactical view's 3D camera
	if (TheTacticalView) {
		W3DView *w3dView = static_cast<W3DView*>(TheTacticalView);
		if (w3dView && w3dView->get3DCamera()) {
			// SAGE stores column-major; transpose to D3D row-major.
			viewOut.Init(w3dView->get3DCamera()->Get_View_Matrix());
			viewOut = viewOut.Transpose();
			return;
		}
	}
	// Fallback
	DX8Wrapper::Get_Transform(D3DTS_VIEW, viewOut);
}

static void GetCameraProjectionMatrix(Matrix4x4 &projOut)
{
	if (TheTacticalView) {
		W3DView *w3dView = static_cast<W3DView*>(TheTacticalView);
		if (w3dView && w3dView->get3DCamera()) {
			// SAGE stores column-major; transpose to D3D row-major.
			w3dView->get3DCamera()->Get_D3D_Projection_Matrix(&projOut);
			projOut = projOut.Transpose();
			return;
		}
	}
	// Fallback
	DX8Wrapper::Get_Transform(D3DTS_PROJECTION, projOut);
}

//=============================================================================
// W3XEffectManager::W3XEffectManager
//=============================================================================
W3XEffectManager::W3XEffectManager(void) :
	m_cacheSize(0),
	m_prevCleanupHook(NULL)
{
	for (int i = 0; i < W3X_EFFECT_CACHE_MAX; i++) {
		m_cache[i].effect = NULL;
		m_cache[i].refCount = 0;
	}

	// Register as the device cleanup hook (chained with any existing hook) so
	// cached effects get OnLostDevice/OnResetDevice across device resets.
	m_prevCleanupHook = DX8Wrapper::GetCleanupHook();
	DX8Wrapper::SetCleanupHook(this);
	DEBUG_LOG(("[W3X_P3] W3XEffectManager registered as device cleanup hook (prev=%p)\n",
		(void*)m_prevCleanupHook));
}


//=============================================================================
// W3XEffectManager::~W3XEffectManager
//=============================================================================
W3XEffectManager::~W3XEffectManager()
{
	// Restore the previous cleanup hook if we are still registered
	if (DX8Wrapper::GetCleanupHook() == this) {
		DX8Wrapper::SetCleanupHook(m_prevCleanupHook);
	}

	// Release all cached effects
	for (int i = 0; i < m_cacheSize; i++) {
		if (m_cache[i].effect) {
			m_cache[i].effect->Release();
			m_cache[i].effect = NULL;
		}
	}
	m_cacheSize = 0;
	m_instance = NULL;
}


//=============================================================================
// W3XEffectManager::ReleaseResources
// Device lost: release effect device resources, then chain to previous hook.
//=============================================================================
void W3XEffectManager::ReleaseResources(void)
{
	OnLostDevice();
	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReleaseResources();
	}
}


//=============================================================================
// W3XEffectManager::ReAcquireResources
// Device restored: re-acquire effect device resources, then chain to previous.
//=============================================================================
void W3XEffectManager::ReAcquireResources(void)
{
	OnResetDevice();
	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReAcquireResources();
	}
}


//=============================================================================
// W3XEffectManager::Instance
//=============================================================================
W3XEffectManager *W3XEffectManager::Instance(void)
{
	if (!m_instance) {
		m_instance = NEW W3XEffectManager;
	}
	return m_instance;
}


//=============================================================================
// W3XEffectManager::FindCacheEntry
// Linear search in the cache array (max 64 entries, so this is fast enough)
//=============================================================================
int W3XEffectManager::FindCacheEntry(const char *fxPath) const
{
	for (int i = 0; i < m_cacheSize; i++) {
		if (m_cache[i].filePath.compare(fxPath) == 0) {
			return i;
		}
	}
	return -1;
}


//=============================================================================
// W3XEffectManager::GetEffect
// Load a .fx file via D3DXCreateEffect and cache it.
// Uses the engine FileClass to read the file content, then creates the
// effect from memory.
//=============================================================================
ID3DXEffect *W3XEffectManager::GetEffect(const char *fxPath)
{
	WWMEMLOG(MEM_GEOMETRY);

	if (!fxPath || !fxPath[0]) return NULL;

	// Check cache first
	int idx = FindCacheEntry(fxPath);
	if (idx >= 0) {
		m_cache[idx].refCount++;
		return m_cache[idx].effect;
	}

	// Cache full?
	if (m_cacheSize >= W3X_EFFECT_CACHE_MAX) {
		DEBUG_LOG(("[W3X_P3] GetEffect('%s') cache FULL\n", fxPath));
		return NULL;
	}

	// Read the .fx file using the engine file system
	FileClass *file = _TheFileFactory->Get_File(fxPath);
	if (!file || !file->Is_Available()) {
		DEBUG_LOG(("[W3X_P3] GetEffect('%s') file not found\n", fxPath));
		if (file) _TheFileFactory->Return_File(file);
		return NULL;
	}

	file->Open();
	int fileSize = file->Size();
	if (fileSize <= 0) {
		file->Close();
		_TheFileFactory->Return_File(file);
		return NULL;
	}

	char *fxBuffer = new char[fileSize + 1];
	int bytesRead = file->Read(fxBuffer, fileSize);
	fxBuffer[bytesRead] = 0;
	file->Close();
	_TheFileFactory->Return_File(file);

	// Get D3D device (cast D3D8 device to D3D9 for D3DXCreateEffect)
	IDirect3DDevice8 *dev8 = DX8Wrapper::_Get_D3D_Device8();
	if (!dev8) {
		delete[] fxBuffer;
		return NULL;
	}
	IDirect3DDevice9 *dev9 = static_cast<IDirect3DDevice9*>(dev8);

	// Create the effect from memory.
	// NOTE: this D3DX9 build does NOT support the SAS flag (D3DXSHADER_USE_SAS,
	// 0x40000000) — it fails with X3116 "Flags parameter is invalid". So the
	// RA3 shaders' VS_H_Array[VSchooserExpr()] dynamic VS selection cannot be
	// used; the model must use a technique that compiles the VS directly.
	ID3DXEffect *effect = NULL;
	ID3DXBuffer *errors = NULL;
	HRESULT hr = D3DXCreateEffect(dev9, fxBuffer, (UINT)bytesRead,
		NULL, NULL, 0, NULL, &effect, &errors);

	delete[] fxBuffer;

	if (FAILED(hr) || !effect) {
		const char *errMsg = "unknown error";
		if (errors) {
			errMsg = (const char*)errors->GetBufferPointer();
		}
		DEBUG_LOG(("[W3X_P3] GetEffect('%s') D3DXCreateEffect FAILED hr=0x%08X: %s\n",
			fxPath, (int)hr, errMsg));
		if (errors) errors->Release();
		return NULL;
	}
	if (errors) errors->Release();

	// Add to cache
	idx = m_cacheSize++;
	m_cache[idx].filePath = fxPath;
	m_cache[idx].effect = effect;
	m_cache[idx].refCount = 1;

	DEBUG_LOG(("[W3X_P3] GetEffect('%s') loaded OK, cache slot %d\n", fxPath, idx));

	// Log effect metadata on first load
	LogEffectInfo(fxPath, effect);

	return effect;
}


//=============================================================================
// W3XEffectManager::ReleaseEffect
//=============================================================================
void W3XEffectManager::ReleaseEffect(const char *fxPath)
{
	int idx = FindCacheEntry(fxPath);
	if (idx < 0) return;

	m_cache[idx].refCount--;
	DEBUG_LOG(("[W3X_P3] ReleaseEffect('%s') refCount=%d\n",
		fxPath, m_cache[idx].refCount));

	if (m_cache[idx].refCount <= 0) {
		// Release the effect
		if (m_cache[idx].effect) {
			m_cache[idx].effect->Release();
			m_cache[idx].effect = NULL;
		}
		// Remove from cache (shift remaining entries)
		for (int i = idx; i < m_cacheSize - 1; i++) {
			m_cache[i] = m_cache[i + 1];
		}
		m_cacheSize--;
		DEBUG_LOG(("[W3X_P3] ReleaseEffect('%s') destroyed, cache now %d entries\n",
			fxPath, m_cacheSize));
	}
}


//=============================================================================
// W3XEffectManager::OnLostDevice
//=============================================================================
void W3XEffectManager::OnLostDevice(void)
{
	DEBUG_LOG(("[W3X_P3] OnLostDevice: %d cached effects\n", m_cacheSize));
	for (int i = 0; i < m_cacheSize; i++) {
		if (m_cache[i].effect) {
			HRESULT hr = m_cache[i].effect->OnLostDevice();
			DEBUG_LOG(("[W3X_P3]   effect '%s' OnLostDevice hr=0x%08X\n",
				m_cache[i].filePath.str(), (int)hr));
		}
	}
}


//=============================================================================
// W3XEffectManager::OnResetDevice
//=============================================================================
void W3XEffectManager::OnResetDevice(void)
{
	DEBUG_LOG(("[W3X_P3] OnResetDevice: %d cached effects\n", m_cacheSize));
	for (int i = 0; i < m_cacheSize; i++) {
		if (m_cache[i].effect) {
			HRESULT hr = m_cache[i].effect->OnResetDevice();
			DEBUG_LOG(("[W3X_P3]   effect '%s' OnResetDevice hr=0x%08X\n",
				m_cache[i].filePath.str(), (int)hr));
		}
	}
}


//=============================================================================
// W3XEffectManager::BindEngineConstants
// Enumerate all effect parameters and auto-bind known engine globals.
//=============================================================================
void W3XEffectManager::BindEngineConstants(ID3DXEffect *effect,
										   const RenderInfoClass &rinfo)
{
	if (!effect) return;

	int totalParams = 0;
	int boundParams = 0;
	static Bool boundDiagLogged = FALSE;	// DIAG: log bound param names on first call

	// Enumerate all parameters
	D3DXHANDLE hParam = NULL;
	for (UINT i = 0; ; i++) {
		hParam = effect->GetParameter(NULL, i);
		if (!hParam) break;
		totalParams++;

		// Get parameter name
		D3DXPARAMETER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		if (FAILED(effect->GetParameterDesc(hParam, &desc))) continue;

		const char *paramName = desc.Name;
		if (!paramName || !paramName[0]) continue;

		// DIAG: log all parameter names on first call
		{
			static Bool paramDiagOnce = FALSE;
			if (!paramDiagOnce) {
				DEBUG_LOG(("[W3X_P3]   Param %d: '%s' (sem='%s', type=%d, class=%d)\n",
					i, paramName, desc.Semantic ? desc.Semantic : "(none)",
					(int)desc.Type, (int)desc.Class));
				if (i >= 20) paramDiagOnce = TRUE;
			}
		}

		// Try to bind by name
		if (BindParameter(effect, (void*)hParam, paramName, rinfo)) {
			boundParams++;
			if (!boundDiagLogged) {
				DEBUG_LOG(("[W3X_P3]   BOUND: '%s' (sem='%s')\n", paramName, desc.Semantic ? desc.Semantic : "(none)"));
			}
		}
	}

	// DIAG: log binding summary on first call (per effect)
	{
		static Bool diagOnce = FALSE;
		if (!diagOnce) {
			DEBUG_LOG(("[W3X_P3] BindEngineConstants: %d/%d params bound\n",
				boundParams, totalParams));
			diagOnce = TRUE;
		}
	}
	boundDiagLogged = TRUE;	// done logging bound param names for this process
}


//=============================================================================
// W3XEffectManager::BindParameter
// Match a parameter name against known engine globals and set its value.
//=============================================================================
bool W3XEffectManager::BindParameter(ID3DXEffect *effect,
									 void *voidParam,
									 const char *paramName,
									 const RenderInfoClass &rinfo)
{
	D3DXHANDLE param = (D3DXHANDLE)voidParam;

	// Try each binding by case-insensitive name comparison
	for (int b = 0; s_bindings[b].namePattern != NULL; b++) {
		if (paramName && AsciiString(paramName).compareNoCase(s_bindings[b].namePattern) == 0) {
			// Match found! Set the engine value.
			switch (s_bindings[b].semanticGroup)
			{
				case 1: // WorldViewProj
				{
					Matrix4x4 world, view, proj;
					DX8Wrapper::Get_Transform(D3DTS_WORLD, world);
					world = world.Transpose();
					GetCameraViewMatrix(view);
					GetCameraProjectionMatrix(proj);
					Matrix4x4 wv = Multiply(world, view);
					Matrix4x4 wvp = Multiply(wv, proj);
					// DIAG: dump final WVP matrix once
					static bool wvpDiag = false;
					if (!wvpDiag) {
						DEBUG_LOG(("[W3X_P3] WVP[0]=%.2f,%.2f,%.2f,%.2f WVP[1]=%.2f,%.2f,%.2f,%.2f\n",
							wvp[0].X, wvp[0].Y, wvp[0].Z, wvp[0].W,
							wvp[1].X, wvp[1].Y, wvp[1].Z, wvp[1].W));
						DEBUG_LOG(("[W3X_P3] WVP[2]=%.2f,%.2f,%.2f,%.2f WVP[3]=%.2f,%.2f,%.2f,%.2f\n",
							wvp[2].X, wvp[2].Y, wvp[2].Z, wvp[2].W,
							wvp[3].X, wvp[3].Y, wvp[3].Z, wvp[3].W));
						wvpDiag = true;
					}
					effect->SetMatrix(param, (const D3DXMATRIX*)&wvp);
					return true;
				}

				case 2: // World
				{
					Matrix4x4 world;
					DX8Wrapper::Get_Transform(D3DTS_WORLD, world);
					world = world.Transpose();
					effect->SetMatrix(param, (const D3DXMATRIX*)&world);
					return true;
				}

				case 3: // View
				{
					Matrix4x4 view;
					GetCameraViewMatrix(view);
					effect->SetMatrix(param, (const D3DXMATRIX*)&view);
					return true;
				}

				case 4: // Projection
				{
					Matrix4x4 proj;
					GetCameraProjectionMatrix(proj);
					effect->SetMatrix(param, (const D3DXMATRIX*)&proj);
					return true;
				}

				case 10: // ViewProj
				{
					Matrix4x4 view, proj;
					GetCameraViewMatrix(view);
					GetCameraProjectionMatrix(proj);
					Matrix4x4 vp = Multiply(view, proj);
					effect->SetMatrix(param, (const D3DXMATRIX*)&vp);
					return true;
				}

				case 5: // SunDirection
				{
					float dir[4] = { 0, 0, 0, 0 };
					if (TheGlobalData) {
						dir[0] = -TheGlobalData->m_terrainLightPos[0].x;
						dir[1] = -TheGlobalData->m_terrainLightPos[0].y;
						dir[2] = -TheGlobalData->m_terrainLightPos[0].z;
						float len = (float)sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
						if (len > 0.001f) {
							dir[0] /= len; dir[1] /= len; dir[2] /= len;
						}
					}
					effect->SetVector(param, (const D3DXVECTOR4*)dir);
					return true;
				}

				case 6: // SunColor
				{
					float color[4] = { 1, 1, 1, 0 };
					if (TheGlobalData) {
						color[0] = TheGlobalData->m_terrainDiffuse[0].red;
						color[1] = TheGlobalData->m_terrainDiffuse[0].green;
						color[2] = TheGlobalData->m_terrainDiffuse[0].blue;
					}
					effect->SetVector(param, (const D3DXVECTOR4*)color);
					return true;
				}

				case 7: // AmbientColor
				{
					float ambient[4] = { 0.3f, 0.3f, 0.3f, 0 };
					if (TheGlobalData) {
						ambient[0] = TheGlobalData->m_terrainAmbient[0].red;
						ambient[1] = TheGlobalData->m_terrainAmbient[0].green;
						ambient[2] = TheGlobalData->m_terrainAmbient[0].blue;
					}
					effect->SetVector(param, (const D3DXVECTOR4*)ambient);
					return true;
				}

				case 8: // EyePosition / CameraPos
				{
					Vector3 camPos = rinfo.Camera.Get_Position();
					float pos[4] = { camPos.X, camPos.Y, camPos.Z, 0 };
					effect->SetVector(param, (const D3DXVECTOR4*)pos);
					return true;
				}

				case 9: // Time / GameTime
				{
					float t = (float)timeGetTime() * 0.001f; // seconds
					effect->SetFloat(param, t);
					return true;
				}

				case 11: // CameraRight
				{
					const Matrix3D &camTM = rinfo.Camera.Get_Transform();
					Vector3 right(camTM[0].X, camTM[0].Y, camTM[0].Z);
					float vec[4] = { right.X, right.Y, right.Z, 0 };
					effect->SetVector(param, (const D3DXVECTOR4*)vec);
					return true;
				}

				case 12: // CameraUp
				{
					const Matrix3D &camTM2 = rinfo.Camera.Get_Transform();
					Vector3 up(camTM2[1].X, camTM2[1].Y, camTM2[1].Z);
					float vec[4] = { up.X, up.Y, up.Z, 0 };
					effect->SetVector(param, (const D3DXVECTOR4*)vec);
					return true;
				}

				case 13: // ViewInverse
				{
					Matrix4x4 view;
					GetCameraViewMatrix(view);
					Matrix4x4 viewInv = view.Inverse();
					effect->SetMatrix(param, (const D3DXMATRIX*)&viewInv);
					return true;
				}

				case 14: // DirectionalLight[3] {Color, Direction} - RA3 sun + skybox accents
				{
					// 3 lights x (Color.xyz + Direction.xyz) = 18 floats, tight-packed
					float dl[18];
					// [0] = sun
					{
						float sunC[3] = { 1, 1, 1 };
						float sunD[3] = { 0, 0, 1 };
						if (TheGlobalData) {
							sunC[0] = TheGlobalData->m_terrainDiffuse[0].red;
							sunC[1] = TheGlobalData->m_terrainDiffuse[0].green;
							sunC[2] = TheGlobalData->m_terrainDiffuse[0].blue;
							sunD[0] = -TheGlobalData->m_terrainLightPos[0].x;
							sunD[1] = -TheGlobalData->m_terrainLightPos[0].y;
							sunD[2] = -TheGlobalData->m_terrainLightPos[0].z;
							float len = (float)sqrt(sunD[0]*sunD[0] + sunD[1]*sunD[1] + sunD[2]*sunD[2]);
							if (len > 0.001f) { sunD[0] /= len; sunD[1] /= len; sunD[2] /= len; }
						}
						dl[0]=sunC[0]; dl[1]=sunC[1]; dl[2]=sunC[2];
						dl[3]=sunD[0]; dl[4]=sunD[1]; dl[5]=sunD[2];

						// DIAG: dump sun direction/color + ambient values once
						static Bool sunDiag = FALSE;
						if (!sunDiag) {
							if (TheGlobalData) {
								DEBUG_LOG(("[W3X_P7] SUN lightPos[0](raw)=%.2f,%.2f,%.2f\n",
									(float)TheGlobalData->m_terrainLightPos[0].x,
									(float)TheGlobalData->m_terrainLightPos[0].y,
									(float)TheGlobalData->m_terrainLightPos[0].z));
								DEBUG_LOG(("[W3X_P7] SUN diffuse=%.2f,%.2f,%.2f  dir(->light)=%.2f,%.2f,%.2f\n",
									(float)TheGlobalData->m_terrainDiffuse[0].red,
									(float)TheGlobalData->m_terrainDiffuse[0].green,
									(float)TheGlobalData->m_terrainDiffuse[0].blue,
									sunD[0], sunD[1], sunD[2]));
								DEBUG_LOG(("[W3X_P7] AMB ambient=%.2f,%.2f,%.2f\n",
									(float)TheGlobalData->m_terrainAmbient[0].red,
									(float)TheGlobalData->m_terrainAmbient[0].green,
									(float)TheGlobalData->m_terrainAmbient[0].blue));
							} else {
								DEBUG_LOG(("[W3X_P7] SUN/AMB: TheGlobalData == NULL\n"));
							}
							sunDiag = TRUE;
						}
					}
					// [1] = skybox accent 1 (hemisphere up)
					dl[6]=0.40f; dl[7]=0.50f; dl[8]=0.60f;
					dl[9]=0.0f;  dl[10]=1.0f; dl[11]=0.0f;
					// [2] = skybox accent 2
					dl[12]=0.30f; dl[13]=0.20f; dl[14]=0.10f;
					dl[15]=1.0f;  dl[16]=0.0f;  dl[17]=0.0f;
					effect->SetValue(param, dl, sizeof(dl));
					return true;
				}

				case 15: // NoCloudMultiplier (float3) - declared unmanaged, must set to 1
				{
					float ncm[4] = { 1, 1, 1, 1 };
					effect->SetVector(param, (const D3DXVECTOR4*)ncm);
					return true;
				}

				case 16: // TintColor (float4) - output multiplier; default white
				{
					float tc[4] = { 1, 1, 1, 1 };
					effect->SetVector(param, (const D3DXVECTOR4*)tc);
					return true;
				}

				case 17: // OpacityOverride (float) - 1 = opaque
				{
					effect->SetFloat(param, 1.0f);
					return true;
				}

				case 18: // RecolorColor (float4) - faction color; default white
				{
					float rc[4] = { 1, 1, 1, 1 };
					effect->SetVector(param, (const D3DXVECTOR4*)rc);
					return true;
				}

				case 19: // NumPointLights (int) - 0 disables point light loop
				{
					effect->SetInt(param, 0);
					return true;
				}
			}
			return true; // matched but might not have set (fallthrough)
		}
	}

	return false; // not matched
}


//=============================================================================
// W3XEffectManager::LogEffectInfo
// Log effect metadata for diagnostics.
//=============================================================================
void W3XEffectManager::LogEffectInfo(const char *tag, ID3DXEffect *effect)
{
	if (!effect) return;

	DEBUG_LOG(("[W3X_P3] Effect '%s'\n", tag));

	// Count techniques
	D3DXHANDLE hTechnique = NULL;
	int techCount = 0;
	for (UINT ti = 0; ; ti++) {
		hTechnique = effect->GetTechnique(ti);
		if (!hTechnique) break;
		techCount++;
	}
	DEBUG_LOG(("[W3X_P3]   Techniques: %d\n", techCount));

	// Count and log parameters
	int paramCount = 0;
	D3DXHANDLE hParam = NULL;
	for (UINT pi = 0; ; pi++) {
		hParam = effect->GetParameter(NULL, pi);
		if (!hParam) break;
		paramCount++;
	}
	DEBUG_LOG(("[W3X_P3]   Parameters: %d\n", paramCount));

	// Log details for first few parameters
	int detailCount = 0;
	hParam = NULL;
	for (UINT pj = 0; pj < 8 && paramCount > 0; pj++) {
		hParam = effect->GetParameter(NULL, pi);
		if (!hParam) break;
		D3DXPARAMETER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		if (FAILED(effect->GetParameterDesc(hParam, &desc))) continue;

		const char *typeName = "?";
		switch (desc.Class) {
			case D3DXPC_SCALAR:  typeName = "scalar"; break;
			case D3DXPC_VECTOR:  typeName = "vector"; break;
			case D3DXPC_MATRIX_ROWS: typeName = "matrix_rows"; break;
			case D3DXPC_MATRIX_COLUMNS: typeName = "matrix_cols"; break;
			case D3DXPC_OBJECT:  typeName = "object"; break;
			case D3DXPC_STRUCT:  typeName = "struct"; break;
		}

		// Check if this name matches an engine constant
		bool isEngineConst = false;
		for (int b = 0; s_bindings[b].namePattern != NULL; b++) {
			if (desc.Name && AsciiString(desc.Name).compareNoCase(s_bindings[b].namePattern) == 0) {
				isEngineConst = true;
				break;
			}
		}

		DEBUG_LOG(("[W3X_P3]     %s '%s' (class=%s, cols=%d, rows=%d, elements=%d)%s%s\n",
			typeName,
			desc.Name ? desc.Name : "(unnamed)",
			typeName,
			(desc.Columns),
			(desc.Rows),
			(desc.Elements),
			isEngineConst ? " [ENGINE]" : "",
			(desc.Elements > 1) ? " [ARRAY]" : ""));

		detailCount++;
	}

	if (paramCount > 8) {
		DEBUG_LOG(("[W3X_P3]     ... and %d more parameters\n", paramCount - 8));
	}

	// Log technique names
	for (UINT tj = 0; tj < (UINT)techCount && tj < 4; tj++) {
		D3DXHANDLE hTech = effect->GetTechnique(tj);
		if (hTech) {
			D3DXTECHNIQUE_DESC techDesc;
			ZeroMemory(&techDesc, sizeof(techDesc));
			if (SUCCEEDED(effect->GetTechniqueDesc(hTech, &techDesc))) {
				DEBUG_LOG(("[W3X_P3]   Technique %d: '%s' (%d passes)\n",
					tj, techDesc.Name ? techDesc.Name : "(unnamed)", (int)techDesc.Passes));
			}
		}
	}
}
