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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "always.h"
#include "W3DDevice/GameClient/W3DDeferredRenderer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3d.h"
#include "Common/GlobalData.h"

// Global singleton pointer
W3DDeferredRenderer *g_theW3DDeferredRenderer = NULL;

// ============================================================================
// W3DDeferredRenderer::W3DDeferredRenderer
// ============================================================================
W3DDeferredRenderer::W3DDeferredRenderer()
	:
	m_initialized(false),
	m_available(false),
	m_inGBufferPass(false),
	m_gbufferWidth(0),
	m_gbufferHeight(0)
{
	m_gbufferRT[0] = NULL;
	m_gbufferRT[1] = NULL;
	m_gbufferRT[2] = NULL;
	memset(&m_savedViewport, 0, sizeof(m_savedViewport));
	m_prevCleanupHook = NULL;
}

// ============================================================================
// W3DDeferredRenderer::~W3DDeferredRenderer
// ============================================================================
W3DDeferredRenderer::~W3DDeferredRenderer()
{
	shutdown();
}

// ============================================================================
// W3DDeferredRenderer::init
// ============================================================================
void W3DDeferredRenderer::init()
{
	if (m_initialized) {
		return;
	}
	m_initialized = true;

	//
	// Check hardware support: need 3+ simultaneous render targets.
	//
	const DX8Caps *caps = DX8Wrapper::Get_Current_Caps();
	if (caps == NULL) {
		WWDEBUG_SAY(("W3DDeferredRenderer: no caps available, deferred rendering disabled.\n"));
		m_available = false;
		return;
	}

	int numRTs = caps->Get_Num_Simultaneous_RTs();
	if (numRTs < 3) {
		WWDEBUG_SAY(("W3DDeferredRenderer: GPU supports %d RTs (need 3+). Disabled.\n", numRTs));
		m_available = false;
		return;
	}

	//
	// Check INI switch: UseDeferredRendering must be enabled.
	//
	if (TheGlobalData && !TheGlobalData->m_useDeferredRendering) {
		WWDEBUG_SAY(("W3DDeferredRenderer: UseDeferredRendering=0 in INI. Disabled.\n"));
		m_available = false;
		return;
	}

	//
	// Determine G-Buffer size from the current display resolution.
	//
	m_gbufferWidth = TheGlobalData ? TheGlobalData->m_xResolution : 640;
	m_gbufferHeight = TheGlobalData ? TheGlobalData->m_yResolution : 480;
	if (m_gbufferWidth < 1 || m_gbufferHeight < 1) {
		m_gbufferWidth = 640;
		m_gbufferHeight = 480;
	}

	//
	// Create the three G-Buffer render target textures.
	//
	if (!createGBufferResources()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to create G-Buffer RTs. Disabled.\n"));
		m_available = false;
		return;
	}

	//
	// Register as a cleanup hook (chain with any existing hook).
	//
	m_prevCleanupHook = DX8Wrapper::GetCleanupHook();
	DX8Wrapper::SetCleanupHook(this);

	m_available = true;
	WWDEBUG_SAY(("W3DDeferredRenderer: initialized (%dx%d, MRT=%d).\n",
		m_gbufferWidth, m_gbufferHeight, numRTs));
}

// ============================================================================
// W3DDeferredRenderer::shutdown
// ============================================================================
void W3DDeferredRenderer::shutdown()
{
	if (m_inGBufferPass) {
		endGBufferPass();
	}

	// Restore previous cleanup hook in the chain.
	DX8Wrapper::SetCleanupHook(m_prevCleanupHook);
	m_prevCleanupHook = NULL;

	releaseGBufferResources();
	m_available = false;
	m_initialized = false;
}

// ============================================================================
// W3DDeferredRenderer::createGBufferResources
// ============================================================================
bool W3DDeferredRenderer::createGBufferResources()
{
	//
	// Create 3 A8R8G8B8 render targets at the display resolution.
	// allowNonPOT=true lets them match the display size exactly
	// (no wasteful POT padding).
	//
	for (int i = 0; i < 3; i++) {
		m_gbufferRT[i] = DX8Wrapper::Create_Render_Target(
			m_gbufferWidth,
			m_gbufferHeight,
			WW3D_FORMAT_A8R8G8B8,
			true);  // allowNonPOT

		if (m_gbufferRT[i] == NULL) {
			WWDEBUG_SAY(("W3DDeferredRenderer: Create_Render_Target(%d) failed!\n", i));
			// Release previously created ones
			for (int j = 0; j < i; j++) {
				REF_PTR_RELEASE(m_gbufferRT[j]);
			}
			return false;
		}
	}

	WWDEBUG_SAY(("W3DDeferredRenderer: 3 G-Buffer RTs created (%dx%d).\n",
		m_gbufferWidth, m_gbufferHeight));
	return true;
}

// ============================================================================
// W3DDeferredRenderer::releaseGBufferResources
// ============================================================================
void W3DDeferredRenderer::releaseGBufferResources()
{
	for (int i = 0; i < 3; i++) {
		REF_PTR_RELEASE(m_gbufferRT[i]);
	}
}

// ============================================================================
// W3DDeferredRenderer::beginGBufferPass
// ============================================================================
bool W3DDeferredRenderer::beginGBufferPass()
{
	if (!m_available) {
		return false;
	}

	//
	// Re-create resources if they were released (device reset).
	//
	if (m_gbufferRT[0] == NULL) {
		if (!createGBufferResources()) {
			return false;
		}
	}

	//
	// Save the current viewport so we can restore it after.
	//
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	m_savedViewport = vp;

	//
	// Bind all three G-Buffer RTs as MRT targets.
	// RT0: Albedo+Metallic, RT1: Normal+Roughness, RT2: Emissive+Depth
	//
	IDirect3DSurface8 *surf0 = m_gbufferRT[0]->Get_D3D_Surface_Level();
	IDirect3DSurface8 *surf1 = m_gbufferRT[1]->Get_D3D_Surface_Level();
	IDirect3DSurface8 *surf2 = m_gbufferRT[2]->Get_D3D_Surface_Level();

	WWASSERT(surf0 != NULL && surf1 != NULL && surf2 != NULL);

	DX8Wrapper::Set_Render_Target(0, surf0);
	DX8Wrapper::Set_Render_Target(1, surf1);
	DX8Wrapper::Set_Render_Target(2, surf2);

	surf0->Release();
	surf1->Release();
	surf2->Release();

	//
	// Clear all three RTs to black (0 alpha for metallic/roughness/depth channels)
	// and clear depth-stencil to far plane.
	//
	DX8Wrapper::Clear(true, true, Vector3(0, 0, 0), 0, 1.0f, 0);

	//
	// Set viewport to match the G-Buffer RT size.
	//
	D3DVIEWPORT9 gbVp;
	gbVp.X = 0;
	gbVp.Y = 0;
	gbVp.Width = m_gbufferWidth;
	gbVp.Height = m_gbufferHeight;
	gbVp.MinZ = 0.0f;
	gbVp.MaxZ = 1.0f;
	DX8CALL(SetViewport(&gbVp));

	m_inGBufferPass = true;
	return true;
}

// ============================================================================
// W3DDeferredRenderer::endGBufferPass
// ============================================================================
void W3DDeferredRenderer::endGBufferPass()
{
	if (!m_inGBufferPass) {
		return;
	}
	m_inGBufferPass = false;

	//
	// Restore the default render target.
	// DX8Wrapper's Set_Render_Target(NULL) automatically clears
	// MRT slots RT1..RT3 (thanks to P0 changes).
	//
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);

	//
	// Restore the original viewport.
	//
	DX8CALL(SetViewport(&m_savedViewport));
}

// ============================================================================
// W3DDeferredRenderer::ReleaseResources  (DX8_CleanupHook)
// ============================================================================
void W3DDeferredRenderer::ReleaseResources()
{
	WWDEBUG_SAY(("W3DDeferredRenderer: releasing G-Buffer resources (device reset).\n"));

	if (m_inGBufferPass) {
		endGBufferPass();
	}

	releaseGBufferResources();

	// Chain to the previous cleanup hook.
	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReleaseResources();
	}
}

// ============================================================================
// W3DDeferredRenderer::ReAcquireResources  (DX8_CleanupHook)
// ============================================================================
void W3DDeferredRenderer::ReAcquireResources()
{
	// Chain to the previous cleanup hook first (restore their state before ours).
	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReAcquireResources();
	}

	WWDEBUG_SAY(("W3DDeferredRenderer: re-acquiring G-Buffer resources.\n"));

	if (!createGBufferResources()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to re-create G-Buffer RTs after reset.\n"));
		m_available = false;
	}
}
