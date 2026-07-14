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
#include "WW3D2/DX8Caps.h"
#include "Common/GlobalData.h"

// Terrain render object for shroud texture access
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DShroud.h"
extern BaseHeightMapRenderObjClass *TheTerrainRenderObject;

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
	m_gbufferHeight(0),
	m_gbufferScale(1.0f),
	m_prevCleanupHook(NULL),
	m_sunLightPS(NULL),
	m_pointLightPS(NULL),
	m_quadVB(NULL),
	m_quadIB(NULL)
{
	m_gbufferRT[0] = NULL;
	m_gbufferRT[1] = NULL;
	m_gbufferRT[2] = NULL;
	memset(&m_savedViewport, 0, sizeof(m_savedViewport));
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

	// Apply dynamic resolution scaling.
	m_gbufferWidth = (int)(m_gbufferWidth * m_gbufferScale);
	m_gbufferHeight = (int)(m_gbufferHeight * m_gbufferScale);
	if (m_gbufferWidth < 1) m_gbufferWidth = 1;
	if (m_gbufferHeight < 1) m_gbufferHeight = 1;

	//
	// Create the three G-Buffer render target textures.
	//
	if (!createGBufferResources()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to create G-Buffer RTs. Disabled.\n"));
		m_available = false;
		return;
	}

	//
	// Create full-screen quad resources for the lighting pass.
	//
	if (!createFullScreenQuad()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to create full-screen quad.\n"));
		releaseGBufferResources();
		m_available = false;
		return;
	}

	//
	// Compile the sunlight PBR pixel shader.
	//
	if (!compileSunLightShader()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to compile sunlight PS.\n"));
		releaseFullScreenQuad();
		releaseGBufferResources();
		m_available = false;
		return;
	}
	if (!compilePointLightShader()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to compile point light PS.\n"));
		releasePointLightShader();
		releaseSunLightShader();
		releaseFullScreenQuad();
		releaseGBufferResources();
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

	releaseSunLightShader();
	releaseFullScreenQuad();
	releaseGBufferResources();
	m_available = false;
	m_initialized = false;
}

// ============================================================================
// W3DDeferredRenderer::createGBufferResources
// ============================================================================
bool W3DDeferredRenderer::createGBufferResources()
{
	for (int i = 0; i < 3; i++) {
		m_gbufferRT[i] = DX8Wrapper::Create_Render_Target(
			m_gbufferWidth,
			m_gbufferHeight,
			WW3D_FORMAT_A8R8G8B8,
			true);  // allowNonPOT

		if (m_gbufferRT[i] == NULL) {
			WWDEBUG_SAY(("W3DDeferredRenderer: Create_Render_Target(%d) failed!\n", i));
			for (int j = 0; j < i; j++) {
				REF_PTR_RELEASE(m_gbufferRT[j]);
			}
			return false;
		}
	}

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

	if (m_gbufferRT[0] == NULL) {
		if (!createGBufferResources()) {
			return false;
		}
	}

	// Save the current viewport.
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	m_savedViewport = vp;

	// Bind all three G-Buffer RTs as MRT targets.
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

	// Clear all three RTs to black and clear depth-stencil.
	DX8Wrapper::Clear(true, true, Vector3(0, 0, 0), 0, 1.0f, 0);

	// Set viewport to match G-Buffer size.
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

	// Restore default render target (DX8Wrapper clears MRT slots automatically).
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);

	// Restore original viewport.
	DX8CALL(SetViewport(&m_savedViewport));
}

// ============================================================================
// W3DDeferredRenderer::sunLightPass
// ============================================================================
void W3DDeferredRenderer::sunLightPass(
	const Vector3 &sunDir,
	const Vector3 &sunColor,
	const Vector3 &ambient,
	const Vector3 &cameraPos,
	const Matrix4x4 &invViewProj)
{
	if (!m_available) {
		return;
	}
	if (!m_sunLightPS || !m_quadVB || !m_quadIB) {
		return;
	}

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;

	// Bind G-Buffer textures as pixel shader inputs.
	// s0 = Albedo+Metallic (RT0), s1 = Normal+Roughness (RT1), s2 = Emissive+Depth (RT2)
	IDirect3DBaseTexture8 *tex0 = m_gbufferRT[0]->Peek_D3D_Base_Texture();
	IDirect3DBaseTexture8 *tex1 = m_gbufferRT[1]->Peek_D3D_Base_Texture();
	IDirect3DBaseTexture8 *tex2 = m_gbufferRT[2]->Peek_D3D_Base_Texture();

	dev->SetTexture(0, tex0);
	dev->SetTexture(1, tex1);
	dev->SetTexture(2, tex2);

	// Bind shroud texture as s3 (if available)
	if (TheTerrainRenderObject && TheTerrainRenderObject->getShroud()) {
		TextureClass *shroudTex = TheTerrainRenderObject->getShroud()->getShroudTexture();
		if (shroudTex) {
			IDirect3DBaseTexture8 *shroudBase = shroudTex->Peek_D3D_Base_Texture();
			dev->SetTexture(3, shroudBase);
		}
	}

	// Set sunlight PBR shader constants:
	// c0 = sunDir (normalized, w=0 for directional light)
	// c1 = sunColor (rgb, a=0)
	// c2 = cameraPos.xyz + ambient.r
	// c3 = invViewProj row 0
	// c4 = invViewProj row 1
	// c5 = invViewProj row 2
	// c6 = invViewProj row 3

	float c0[4] = { sunDir.X, sunDir.Y, sunDir.Z, 0.0f };
	float c1[4] = { sunColor.X, sunColor.Y, sunColor.Z, 0.0f };
	float c2[4] = { cameraPos.X, cameraPos.Y, cameraPos.Z, (ambient.X + ambient.Y + ambient.Z) * 0.333f };

	dev->SetPixelShaderConstantF(0, c0, 1);
	dev->SetPixelShaderConstantF(1, c1, 1);
	dev->SetPixelShaderConstantF(2, c2, 1);

	// InvViewProj matrix (3 rows = 3 float4 constants)
	Matrix4x4 inv = invViewProj;
	float c3[4] = { inv[0][0], inv[0][1], inv[0][2], inv[0][3] };
	float c4[4] = { inv[1][0], inv[1][1], inv[1][2], inv[1][3] };
	float c5[4] = { inv[2][0], inv[2][1], inv[2][2], inv[2][3] };
	float c6[4] = { inv[3][0], inv[3][1], inv[3][2], inv[3][3] };

	dev->SetPixelShaderConstantF(3, c3, 1);
	dev->SetPixelShaderConstantF(4, c4, 1);
	dev->SetPixelShaderConstantF(5, c5, 1);
	dev->SetPixelShaderConstantF(6, c6, 1);

	// Set pixel shader.
	dev->SetPixelShader(m_sunLightPS);

	// Disable depth writes, enable alpha blending (for additive lights later).
	// For the sun light, just disable depth test (full-screen quad).
	DWORD oldZEnable, oldZWrite;
	dev->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

	// Set FVF for pre-transformed quad vertices (XYZRHW + TEX1).
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

	// Set streams.
	dev->SetStreamSource(0, m_quadVB, 0, sizeof(float) * 6);
	dev->SetIndices(m_quadIB);

	// Draw two triangles (full-screen quad).
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);

	// Restore render states.
	dev->SetRenderState(D3DRS_ZENABLE, oldZEnable);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);

	// Unbind textures.
	dev->SetTexture(0, NULL);
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
	dev->SetTexture(3, NULL);
}


void W3DDeferredRenderer::renderDynamicLights(
	IDirect3DDevice8 *dev,
	const Vector3 &cameraPos,
	const Matrix4x4 &invViewProj)
{
	if (!m_available || !m_pointLightPS || !m_quadVB) return;
	if (!dev || !m_gbufferRT[0]) return;

	// Bind G-Buffer textures for sampling.
	IDirect3DBaseTexture8 *tex0 = m_gbufferRT[0]->Peek_D3D_Base_Texture();
	IDirect3DBaseTexture8 *tex1 = m_gbufferRT[1]->Peek_D3D_Base_Texture();
	IDirect3DBaseTexture8 *tex2 = m_gbufferRT[2]->Peek_D3D_Base_Texture();
	dev->SetTexture(0, tex0);
	dev->SetTexture(1, tex1);
	dev->SetTexture(2, tex2);

	// Set invViewProj constants (c3-c6, same layout as sunlight PS).
	Matrix4x4 inv = invViewProj;
	float c3[4] = { inv[0][0], inv[0][1], inv[0][2], inv[0][3] };
	float c4[4] = { inv[1][0], inv[1][1], inv[1][2], inv[1][3] };
	float c5[4] = { inv[2][0], inv[2][1], inv[2][2], inv[2][3] };
	float c6[4] = { inv[3][0], inv[3][1], inv[3][2], inv[3][3] };
	float camConst[4] = { cameraPos.X, cameraPos.Y, cameraPos.Z, 0 };

	dev->SetPixelShaderConstantF(2, camConst, 1);
	dev->SetPixelShaderConstantF(3, c3, 1);
	dev->SetPixelShaderConstantF(4, c4, 1);
	dev->SetPixelShaderConstantF(5, c5, 1);
	dev->SetPixelShaderConstantF(6, c6, 1);

	dev->SetPixelShader(m_pointLightPS);

	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	dev->SetStreamSource(0, m_quadVB, 0, sizeof(float) * 6);
	dev->SetIndices(m_quadIB);

	// Save and set additive blend states.
	DWORD oldZen, oldZw, oldAlpha, oldSrc, oldDst;
	dev->GetRenderState(D3DRS_ZENABLE, &oldZen);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZw);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
	dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
	dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);

	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

	// Collect and render dynamic lights.
	int lightCount = 0;
	const int MAX_DYNAMIC_LIGHTS = 8;
	RefRenderObjListClass *lightList = NULL;
	if (W3DDisplay::m_3DScene) {
		lightList = W3DDisplay::m_3DScene->getDynamicLights();
	}

	if (lightList) {
		RefRenderObjListIterator it(lightList);
		for (it.First(); !it.Is_Done(); it.Next()) {
			if (lightCount >= MAX_DYNAMIC_LIGHTS) break;
			W3DDynamicLight *pDyna = (W3DDynamicLight*)it.Peek_Obj();
			if (!pDyna || !pDyna->isEnabled()) continue;

			Vector3 lightPos = pDyna->Get_Position();
			Vector3 diffColor;
			pDyna->Get_Diffuse(&diffColor);
			float intensity = pDyna->Get_Intensity();
			float range = pDyna->Get_Attenuation_Range();

			diffColor.X *= intensity; diffColor.Y *= intensity; diffColor.Z *= intensity;

			// Distance cull.
			Vector3 toCam = lightPos - cameraPos;
			if (toCam.Length() > range * 2.0f + 100.0f) continue;

			float lightPosConst[4] = { lightPos.X, lightPos.Y, lightPos.Z, range };
			float lightColorConst[4] = { diffColor.X, diffColor.Y, diffColor.Z, 0 };
			dev->SetPixelShaderConstantF(7, lightPosConst, 1);
			dev->SetPixelShaderConstantF(8, lightColorConst, 1);
			dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
			lightCount++;
		}
	}

	// Restore states.
	dev->SetRenderState(D3DRS_ZENABLE, oldZen);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZw);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
	dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
	dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
	dev->SetTexture(0, NULL);
	dev->SetTexture(1, NULL);
	dev->SetTexture(2, NULL);
}


// ============================================================================
// W3DDeferredRenderer::createFullScreenQuad
// ============================================================================
bool W3DDeferredRenderer::createFullScreenQuad()
{
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return false;

	// 4 vertices: pre-transformed (XYZRHW) with texcoords.
	// Screen coords: (-1,-1) to (1,1) in normalized space, rhw=1.
	// Texture coords: (0,0) to (1,1).
	struct QuadVertex { float x, y, z, rhw; float u, v; };
	QuadVertex verts[4] = {
		{ -1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 0.0f },  // top-left
		{  1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f },  // top-right
		{ -1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 1.0f },  // bottom-left
		{  1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f },  // bottom-right
	};

	HRESULT hr = dev->CreateVertexBuffer(
		sizeof(verts), D3DUSAGE_WRITEONLY,
		D3DFVF_XYZRHW | D3DFVF_TEX1, D3DPOOL_DEFAULT, &m_quadVB, NULL);
	if (FAILED(hr) || !m_quadVB) {
		WWDEBUG_SAY(("W3DDeferredRenderer: Quad VB creation failed.\n"));
		return false;
	}

	// Fill VB.
	void *vbPtr = NULL;
	m_quadVB->Lock(0, sizeof(verts), &vbPtr, 0);
	if (vbPtr) {
		memcpy(vbPtr, verts, sizeof(verts));
		m_quadVB->Unlock();
	}

	// Index buffer (2 triangles: 0-1-2, 2-1-3).
	unsigned short indices[6] = { 0, 1, 2, 2, 1, 3 };
	hr = dev->CreateIndexBuffer(
		sizeof(indices), D3DUSAGE_WRITEONLY,
		D3DFMT_INDEX16, D3DPOOL_DEFAULT, &m_quadIB, NULL);
	if (FAILED(hr) || !m_quadIB) {
		WWDEBUG_SAY(("W3DDeferredRenderer: Quad IB creation failed.\n"));
		m_quadVB->Release(); m_quadVB = NULL;
		return false;
	}

	void *ibPtr = NULL;
	m_quadIB->Lock(0, sizeof(indices), &ibPtr, 0);
	if (ibPtr) {
		memcpy(ibPtr, indices, sizeof(indices));
		m_quadIB->Unlock();
	}

	return true;
}

// ============================================================================
// W3DDeferredRenderer::releaseFullScreenQuad
// ============================================================================
void W3DDeferredRenderer::releaseFullScreenQuad()
{
	if (m_quadVB) { m_quadVB->Release(); m_quadVB = NULL; }
	if (m_quadIB) { m_quadIB->Release(); m_quadIB = NULL; }
}

// ============================================================================
// W3DDeferredRenderer::compileSunLightShader
// ============================================================================
bool W3DDeferredRenderer::compileSunLightShader()
{
	// Sunlight PBR pixel shader (ps_2_0).
	// Input G-Buffer textures:
	//   s0 = Albedo+Metallic, s1 = Normal+Roughness, s2 = Emissive+Depth
	// Constants:
	//   c0 = sunDir (xyz, w=0), c1 = sunColor (rgb)
	//   c2 = cameraPos.xyz + ambient.r
	//   c3-c6 = invViewProj matrix rows
	//
	// Algorithm:
	//   1. Sample G-Buffer textures
	//   2. Decode normal from RT1.rgb (undo *0.5+0.5)
	//   3. Reconstruct world position from depth + screen UV + invViewProj
	//   4. Compute PBR Cook-Torrance BRDF with GGX
	//   5. Output final color
	const char ps_source[] =
		"struct PS_IN {\n"
		"	float4 pos : POSITION;\n"
		"	float2 tex0 : TEXCOORD0;\n"
		"};\n"
		"sampler gbuf0 : register(s0);\n"
		"sampler gbuf1 : register(s1);\n"
		"sampler gbuf2 : register(s2);\n"
		"sampler sShroud : register(s3);\n"
		"float4 main(PS_IN input) : COLOR {\n"
		"	float4 rt0 = tex2D(gbuf0, input.tex0);\n"
		"	float4 rt1 = tex2D(gbuf1, input.tex0);\n"
		"	float4 rt2 = tex2D(gbuf2, input.tex0);\n"
		"	float3 albedo = rt0.rgb;\n"
		"	float metallic = rt0.a;\n"
		"	float3 normalRaw = rt1.rgb;\n"
		"	float roughness = rt1.a;\n"
		"	float depth = rt2.a;\n"
		"	float3 emissive = rt2.rgb;\n"
		"	float3 N = normalize(normalRaw * 2.0 - 1.0 + 1e-6);\n"
		"	float2 screenPos = input.tex0 * 2.0 - 1.0;\n"
		"	float4 clipPos = float4(screenPos, depth, 1.0);\n"
		"	float4 worldPos4 = float4(\n"
		"		dot(clipPos, float4(c3.x, c4.x, c5.x, c6.x)),\n"
		"		dot(clipPos, float4(c3.y, c4.y, c5.y, c6.y)),\n"
		"		dot(clipPos, float4(c3.z, c4.z, c5.z, c6.z)),\n"
		"		dot(clipPos, float4(c3.w, c4.w, c5.w, c6.w))\n"
		"	);\n"
		"	float3 worldPos = worldPos4.xyz / worldPos4.w;\n"
		"	float3 V = normalize(c2.xyz - worldPos);\n"
		"	float3 L = -c0.xyz;\n"
		"	float3 H = normalize(V + L);\n"
		"	float NdotL = saturate(dot(N, L));\n"
		"	float NdotV = saturate(dot(N, V));\n"
		"	float NdotH = saturate(dot(N, H));\n"
		"	float HdotV = saturate(dot(H, V));\n"
		"	float alpha = max(roughness * roughness, 0.001);\n"
		"	float alpha2 = alpha * alpha;\n"
		"	float NdotH2 = NdotH * NdotH;\n"
		"	float denom = NdotH2 * (alpha2 - 1.0) + 1.0;\n"
		"	float D = alpha2 / (3.14159 * denom * denom);\n"
		"	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;\n"
		"	float G1 = NdotL / (NdotL * (1.0 - k) + k);\n"
		"	float G2 = NdotV / (NdotV * (1.0 - k) + k);\n"
		"	float G = G1 * G2;\n"
		"	float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);\n"
		"	float3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);\n"
		"	float3 spec = D * F * G / max(4.0 * NdotV * NdotL, 0.001);\n"
		"	float3 kD = (1.0 - F) * (1.0 - metallic);\n"
		"	float3 diffuse = albedo * kD;\n"
		"	float3 ambientLight = c2.www * albedo;\n"
		"	float3 direct = (diffuse + spec) * c1.rgb * NdotL;\n"
		"	float3 finalColor = ambientLight + direct + emissive;\n"
		"	// Apply shroud visibility from s3\n"
		"	float shroudVis = tex2D(sShroud, input.tex0).a;\n"
		"	finalColor = lerp(float3(0,0,0), finalColor, shroudVis);\n"
		"	// Gamma correction (linear to sRGB)\n"
		"	finalColor = pow(abs(finalColor), 1.0 / 2.2);\n"
		"	return float4(finalColor, 1.0);\n"
		"};\n";

	ID3DXBuffer *compiled = NULL;
	ID3DXBuffer *errors = NULL;
	HRESULT hr = D3DXCompileShader(ps_source, (UINT)strlen(ps_source),
		NULL, NULL, "main", "ps_2_0",
		0, &compiled, &errors, NULL);

	if (FAILED(hr) || !compiled) {
		WWDEBUG_SAY(("W3DDeferredRenderer: SunLight PS compile failed.\n"));
		if (errors) {
			WWDEBUG_SAY(("  error: %s\n", (const char*)errors->GetBufferPointer()));
			errors->Release();
		}
		m_sunLightPS = NULL;
		return false;
	}

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev) {
		hr = dev->CreatePixelShader(
			(const DWORD*)compiled->GetBufferPointer(), &m_sunLightPS);
	}
	compiled->Release();

	if (FAILED(hr) || !m_sunLightPS) {
		WWDEBUG_SAY(("W3DDeferredRenderer: SunLight PS CreatePixelShader failed.\n"));
		m_sunLightPS = NULL;
		return false;
	}

	WWDEBUG_SAY(("W3DDeferredRenderer: SunLight PS compiled successfully.\n"));
	return true;
}

bool W3DDeferredRenderer::compilePointLightShader()
{
	const char ps_source[] =
		"struct PS_IN {\n"
		"  float4 pos : POSITION;\n"
		"  float2 tex0 : TEXCOORD0;\n"
		"};\n"
		"sampler gbuf0 : register(s0);\n"
		"sampler gbuf1 : register(s1);\n"
		"sampler gbuf2 : register(s2);\n"
		"float4 main(PS_IN input) : COLOR {\n"
		"  float4 rt0 = tex2D(gbuf0, input.tex0);\n"
		"  float4 rt1 = tex2D(gbuf1, input.tex0);\n"
		"  float4 rt2 = tex2D(gbuf2, input.tex0);\n"
		"  float3 albedo = rt0.rgb;\n"
		"  float metallic = rt0.a;\n"
		"  float3 N = normalize(rt1.rgb * 2.0 - 1.0 + 1e-6);\n"
		"  float roughness = rt1.a;\n"
		"  float depth = rt2.a;\n"
		"  float2 screenPos = input.tex0 * 2.0 - 1.0;\n"
		"  float4 clipPos = float4(screenPos, depth, 1.0);\n"
		"  float4 worldPos4 = float4(\n"
		"    dot(clipPos, float4(c3.x, c4.x, c5.x, c6.x)),\n"
		"    dot(clipPos, float4(c3.y, c4.y, c5.y, c6.y)),\n"
		"    dot(clipPos, float4(c3.z, c4.z, c5.z, c6.z)),\n"
		"    dot(clipPos, float4(c3.w, c4.w, c5.w, c6.w))\n"
		"  );\n"
		"  float3 worldPos = worldPos4.xyz / worldPos4.w;\n"
		"  float3 V = normalize(c2.xyz - worldPos);\n"
		"  float3 L = c7.xyz - worldPos;\n"
		"  float dist = length(L);\n"
		"  L = L / dist;\n"
		"  float atten = 1.0 - saturate(dist / c7.w);\n"
		"  atten *= atten;\n"
		"  if (atten < 0.001) discard;\n"
		"  float3 H = normalize(V + L);\n"
		"  float NdotL = saturate(dot(N, L));\n"
		"  float NdotV = saturate(dot(N, V));\n"
		"  float NdotH = saturate(dot(N, H));\n"
		"  float HdotV = saturate(dot(H, V));\n"
		"  float alpha = max(roughness * roughness, 0.001);\n"
		"  float alpha2 = alpha * alpha;\n"
		"  float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;\n"
		"  float D = alpha2 / (3.14159 * denom * denom);\n"
		"  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;\n"
		"  float G1 = NdotL / (NdotL * (1.0 - k) + k);\n"
		"  float G2 = NdotV / (NdotV * (1.0 - k) + k);\n"
		"  float G = G1 * G2;\n"
		"  float3 F0 = lerp(float3(0.04,0.04,0.04), albedo, metallic);\n"
		"  float3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);\n"
		"  float3 spec = D * F * G / max(4.0 * NdotV * NdotL, 0.001);\n"
		"  float3 kD = (1.0 - F) * (1.0 - metallic);\n"
		"  float3 diffuse = albedo * kD;\n"
		"  float3 direct = (diffuse + spec) * c8.rgb * NdotL * atten;\n"
		"  // Gamma correction\n"
		"  direct = pow(abs(direct), 1.0 / 2.2);\n"
		"  return float4(direct, 0.0);\n"
		"};\n";

	ID3DXBuffer *compiled = NULL;
	ID3DXBuffer *errors = NULL;
	HRESULT hr = D3DXCompileShader(ps_source, (UINT)strlen(ps_source),
		NULL, NULL, "main", "ps_2_0", 0, &compiled, &errors, NULL);
	if (FAILED(hr) || !compiled) {
		WWDEBUG_SAY(("W3DDeferredRenderer: PointLight PS compile failed.\n"));
		if (errors) {
			WWDEBUG_SAY(("  error: %s\n", (const char*)errors->GetBufferPointer()));
			errors->Release();
		}
		m_pointLightPS = NULL;
		return false;
	}
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev) {
		hr = dev->CreatePixelShader(
			(const DWORD*)compiled->GetBufferPointer(), &m_pointLightPS);
	}
	compiled->Release();
	if (FAILED(hr) || !m_pointLightPS) {
		WWDEBUG_SAY(("W3DDeferredRenderer: PointLight PS CreatePixelShader failed.\n"));
		m_pointLightPS = NULL;
		return false;
	}
	WWDEBUG_SAY(("W3DDeferredRenderer: PointLight PS compiled.\n"));
	return true;
}


// ============================================================================
// W3DDeferredRenderer::releaseSunLightShader
// ============================================================================
void W3DDeferredRenderer::releaseSunLightShader()
{
	if (m_sunLightPS) {
		m_sunLightPS->Release();
		m_sunLightPS = NULL;
	}
}

void W3DDeferredRenderer::releasePointLightShader()
{
	if (m_pointLightPS) {
		m_pointLightPS->Release();
		m_pointLightPS = NULL;
	}
}

// ============================================================================
// W3DDeferredRenderer::ReleaseResources  (DX8_CleanupHook)
// ============================================================================
void W3DDeferredRenderer::ReleaseResources()
{
	WWDEBUG_SAY(("W3DDeferredRenderer: releasing resources (device reset).\n"));

	if (m_inGBufferPass) {
		endGBufferPass();
	}

	releaseSunLightShader();
	releaseFullScreenQuad();
	releaseGBufferResources();

	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReleaseResources();
	}
}

// ============================================================================
// W3DDeferredRenderer::ReAcquireResources  (DX8_CleanupHook)
// ============================================================================
void W3DDeferredRenderer::ReAcquireResources()
{
	if (m_prevCleanupHook) {
		m_prevCleanupHook->ReAcquireResources();
	}

	WWDEBUG_SAY(("W3DDeferredRenderer: re-acquiring resources.\n"));

	if (!createGBufferResources()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to re-create G-Buffer RTs.\n"));
		m_available = false;
		return;
	}

	if (!createFullScreenQuad()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to re-create quad.\n"));
		m_available = false;
		return;
	}

	if (!compileSunLightShader()) {
		WWDEBUG_SAY(("W3DDeferredRenderer: failed to re-compile sunlight PS.\n"));
		m_available = false;
		return;
	}
}

// ============================================================================
// W3DDeferredRenderer member function that doesn't exist in .h yet - add it
// ============================================================================
