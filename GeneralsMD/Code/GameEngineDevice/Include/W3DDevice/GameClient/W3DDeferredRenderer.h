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

#pragma once

#ifndef __W3DDEFERREDRENDERER_H_
#define __W3DDEFERREDRENDERER_H_

#include "always.h"
#include "d3d8compat.h"
#include "dx8wrapper.h"

class DX8Wrapper;
class TextureClass;

/**
 * W3DDeferredRenderer — G-Buffer + deferred lighting pass manager.
 *
 * Owns the 3 G-Buffer RTs and orchestrates MRT setup/teardown.
 * Initialized once at startup; resources re-created on device reset.
 *
 * Current state: skeleton only (Phase 0).
 * Phases 1-9 will flesh out:
 *   - G-Buffer RT creation / release (Phase 1)
 *   - G-Buffer writing (Phase 2)
 *   - Deferred sun/point light passes (Phase 3+)
 *   - Forward-transparent fallback (Phase 4)
 *   - Screen-space shroud (Phase 5)
 */
class W3DDeferredRenderer : public DX8_CleanupHook
{
public:

	W3DDeferredRenderer();
	~W3DDeferredRenderer();

	/// One-time init; queries caps, allocates resources.
	void init();

	/// Full teardown; releases all D3D resources.
	void shutdown();

	/// Whether the device supports MRT (3+ RTs) and deferred rendering is enabled.
	bool isAvailable() const { return m_available; }

	/// Access individual G-Buffer surfaces (0=Albedo+Metallic, 1=Normal+Roughness, 2=Emissive+Depth).
	TextureClass* getGBufferRT(int index) const { return (index >= 0 && index < 3) ? m_gbufferRT[index] : NULL; }

	/// Get the G-Buffer pass width (pixels).
	int getWidth() const { return m_gbufferWidth; }

	/// Get the G-Buffer pass height (pixels).
	int getHeight() const { return m_gbufferHeight; }

	// ---- G-Buffer pass lifecycle ----

	/// Bind the 3 G-Buffer RTs as MRT targets, clear them + depth-stencil.
	/// Returns true if successful; false if unavailable or resource creation failed.
	bool beginGBufferPass();

	/// Unbind MRT targets, restore default RT. Safe to call even if beginGBufferPass() failed.
	void endGBufferPass();

	// ---- DX8_CleanupHook interface (device reset) ----

	/// Release all D3D resources before device reset.
	virtual void ReleaseResources();

	/// Re-create all D3D resources after device reset.
	virtual void ReAcquireResources();

	/// Run the sunlight deferred lighting pass (full-screen quad).
	void sunLightPass(
		const Vector3 &sunDir,
		const Vector3 &sunColor,
		const Vector3 &ambient,
		const Vector3 &cameraPos,
		const Matrix4x4 &invViewProj
	);

	/// Render dynamic point/spot lights (additive full-screen quads).
	void renderDynamicLights(
		IDirect3DDevice8 *dev,
		const Vector3 &cameraPos,
		const Matrix4x4 &invViewProj
	);

	// ---- HDR pass lifecycle ----

	/// Bind the HDR render target (A16B16G16R16F or A8R8G8B8 fallback).
	/// All deferred lighting goes here so values can exceed 1.0.
	bool beginHDRPass();

	/// Restore the default back buffer after HDR compositing.
	void endHDRPass();

	/// Full-screen quad tone-mapping pass: HDR RT -> back buffer.
	/// Applies Reinhard tone mapping + gamma correction.
	void toneMapPass();

	// ---- Shadow map pass lifecycle ----

	/// Bind the shadow map RT, set orthographic camera from sun direction.
	/// Returns true if shadow map is available and pass started.
	bool beginShadowMapPass(const Vector3 &sunDir, const Matrix4x4 &camView);

	/// Restore default RT after shadow map rendering.
	void endShadowMapPass();

	/// Recompile the sunlight PS with shadow-map variant.
	bool compileSunLightShadowShader();

private:

	/// Create (or re-create) the G-Buffer render target textures.
	bool createGBufferResources();

	/// Release the G-Buffer render target textures.
	void releaseGBufferResources();

	bool m_initialized;			///< init() has been called.
	bool m_available;			///< MRT (3+ RTs) supported and INI enabled.
	bool m_inGBufferPass;		///< beginGBufferPass() active.
	int m_gbufferWidth;			///< G-Buffer RT width (pixels).
	int m_gbufferHeight;		///< G-Buffer RT height (pixels).
	float m_gbufferScale;		///< Dynamic resolution scale (1.0=full, 0.75=75%, 0.5=50%).

	/// G-Buffer textures (3 RTs: Albedo+Metallic, Normal+Roughness, Emissive+Depth).
	TextureClass *m_gbufferRT[3];

	/// Saved viewport for restoration after G-Buffer pass.
	D3DVIEWPORT9 m_savedViewport;

	/// Previous cleanup hook in the chain (we must not break the chain).
	DX8_CleanupHook *m_prevCleanupHook;

	// ---- Sunlight pass resources ----

	/// Create the full-screen quad vertex buffer and index buffer.
	bool createFullScreenQuad();

	/// Release the full-screen quad buffers.
	void releaseFullScreenQuad();

	/// Compile the sunlight PBR pixel shader.
	bool compileSunLightShader();

	/// Release the sunlight pixel shader.
	void releaseSunLightShader();

	/// Compile the point light PBR pixel shader.
	bool compilePointLightShader();

	/// Release the point light pixel shader.
	void releasePointLightShader();

	IDirect3DPixelShader9 *m_sunLightPS;	///< Sunlight PBR pixel shader.
	IDirect3DPixelShader9 *m_pointLightPS;	///< Point light PBR pixel shader (additive).
	IDirect3DVertexBuffer9 *m_quadVB;		///< Full-screen quad vertex buffer.
	IDirect3DIndexBuffer9 *m_quadIB;		///< Full-screen quad index buffer.

	// ---- HDR resources ----

	/// Create/release the HDR render target.
	bool createHDRResources();
	void releaseHDRResources();

	/// Create/release the tone-mapping pixel shader.
	bool compileToneMapShader();
	void releaseToneMapShader();

	TextureClass *m_hdrRT;					///< HDR compositing RT (A16B16G16R16F or A8R8G8B8 fallback).
	bool m_hdrAvailable;					///< HDR RT successfully created.
	IDirect3DPixelShader9 *m_toneMapPS;		///< Tone-mapping pixel shader (Reinhard + gamma).

	// ---- Shadow map resources ----

	/// Create/release the shadow map render target.
	bool createShadowResources();
	void releaseShadowResources();

	TextureClass *m_shadowDepthRT;			///< Shadow map depth RT (2048x2048).
	bool m_shadowMapAvailable;				///< Shadow map resources OK.
	Matrix4x4 m_shadowViewProj;				///< Sun's view-projection matrix (for shader).
	IDirect3DPixelShader9 *m_sunLightShadowPS; ///< Sunlight PS with shadow PCF 2x2.

};

/// Global deferred renderer instance; NULL if not available.
extern W3DDeferredRenderer *g_theW3DDeferredRenderer;

#endif // __W3DDEFERREDRENDERER_H_
