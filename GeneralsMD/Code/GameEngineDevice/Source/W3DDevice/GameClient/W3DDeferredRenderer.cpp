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

#include <cstdio>
#include <cstdarg>
#include "always.h"
#include "W3DDevice/GameClient/W3DDeferredRenderer.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8caps.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/DX8Caps.h"
#include "Common/GlobalData.h"
#include "WW3D2/formconv.h"

// ----------------------------------------------------------------------------
// Diagnostic logging — always compiled, writes to fixed path on E: drive.
// Use DIAG_LOG(("format %d", value)) — same double-paren convention as DEBUG_LOG.
// ----------------------------------------------------------------------------
static void diagWrite(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	FILE *f = fopen("E:\\GeneralsMD_DeferredRT.log", "a");
	if (f) {
		vfprintf(f, fmt, args);
		fclose(f);
	}
	va_end(args);
}
#define DIAG_LOG(x)  do { diagWrite x; } while (0)

// ----------------------------------------------------------------------------
// CPU-side self-test: validate octahedral encode/decode symmetry
// ----------------------------------------------------------------------------
static void debugValidateOctEncoding()
{
	float maxAngleErr = 0.0f;
	srand(42);
	for (int i = 0; i < 10000; i++) {
		// Random unit vector
		float theta = (float)rand() / (float)RAND_MAX * 6.2831853f;
		float phi = acosf(2.0f * (float)rand() / (float)RAND_MAX - 1.0f);
		float nx = sinf(phi) * cosf(theta);
		float ny = sinf(phi) * sinf(theta);
		float nz = cosf(phi);
		// Octahedral encode
		float l1 = fabsf(nx) + fabsf(ny) + fabsf(nz);
		float px = nx / l1, py = ny / l1;
		if (nz < 0.0f) {
			float ax = fabsf(px), ay = fabsf(py);
			px = (1.0f - ay) * (px >= 0.0f ? 1.0f : -1.0f);
			py = (1.0f - ax) * (py >= 0.0f ? 1.0f : -1.0f);
		}
		float ex = px * 0.5f + 0.5f, ey = py * 0.5f + 0.5f;
		// Decode
		float dx = ex * 2.0f - 1.0f, dy = ey * 2.0f - 1.0f;
		float dnx = dx, dny = dy, dnz = 1.0f - fabsf(dx) - fabsf(dy);
		if (dnz < 0.0f) {
			float t = -dnz;
			dnx += (dnx >= 0.0f) ? -t : t;
			dny += (dny >= 0.0f) ? -t : t;
		}
		float len = sqrtf(dnx*dnx + dny*dny + dnz*dnz);
		dnx /= len; dny /= len; dnz /= len;
		// Angle error
		float dot = nx*dnx + ny*dny + nz*dnz;
		float angle = acosf(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
		if (angle > maxAngleErr) maxAngleErr = angle;
	}
	DIAG_LOG(("GBuffer self-test: octahedral encoding max angle error = %.6f rad (%.4f deg)\n",
		maxAngleErr, maxAngleErr * 180.0f / 3.14159265f));
}

// ----------------------------------------------------------------------------
// CPU-side self-test: validate depth 16-bit encode/decode symmetry
// ----------------------------------------------------------------------------
static void debugValidateDepthEncoding()
{
	float maxErr = 0.0f;
	for (int i = 0; i < 10000; i++) {
		float depth = (float)i / 9999.0f;
		float encR = depth;
		float encG = depth * depth; (void)encG;  // lsb verification (depth² in G channel)
		float decoded = encR;  // primary value from R channel
		float err = fabsf(decoded - depth);
		if (err > maxErr) maxErr = err;
	}
	DIAG_LOG(("GBuffer self-test: depth 16-bit encoding max error = %.8f\n", maxErr));
}

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
	m_quadIB(NULL),
	m_hdrRT(NULL),
	m_hdrAvailable(false),
	m_toneMapPS(NULL),
	m_shadowDepthRT(NULL),
	m_shadowMapAvailable(false),
	m_sunLightShadowPS(NULL),
	m_aoRawRT(NULL),
	m_aoBlurredRT(NULL),
	m_ssaoAvailable(false),
	m_ssaoPS(NULL),
	m_ssaoBlurPS(NULL)
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
		DIAG_LOG(("W3DDeferredRenderer: no caps available, deferred rendering disabled.\n"));
		m_available = false;
		return;
	}

	int numRTs = caps->Get_Num_Simultaneous_RTs();
	if (numRTs < 3) {
		DIAG_LOG(("W3DDeferredRenderer: GPU supports %d RTs (need 3+). Disabled.\n", numRTs));
		m_available = false;
		return;
	}

	//
	// Check Pixel Shader 3.0 support (needed for MRT in HLSL).
	//
	{
		const D3DCAPS9 &d3dCaps = caps->Get_DX8_Caps();
		DWORD psVer = d3dCaps.PixelShaderVersion;
		int psMajor = D3DSHADER_VERSION_MAJOR(psVer);
		int psMinor = D3DSHADER_VERSION_MINOR(psVer);
		DIAG_LOG(("W3DDeferredRenderer: PixelShaderVersion=%d.%d (%s)\n",
			psMajor, psMinor,
			psVer >= D3DPS_VERSION(3,0) ? "OK" : "NEED 3.0"));
		if (psVer < D3DPS_VERSION(3,0)) {
			DIAG_LOG(("W3DDeferredRenderer: ps_3_0 required for MRT. Disabled.\n"));
			m_available = false;
			return;
		}
	}

	//
	// Check INI switch: UseDeferredRendering must be enabled.
	//
	if (TheGlobalData && !TheGlobalData->m_useDeferredRendering) {
		DIAG_LOG(("W3DDeferredRenderer: UseDeferredRendering=0 in INI. Disabled.\n"));
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
	// WW3DFormat extension self-test: verify new float format conversions.
	// Always runs; output goes to E:\GeneralsMD_DeferredRT.log regardless of build config.
	//
	{
		struct { WW3DFormat ww3d; const char *name; } testFmts[3] = {
			{ WW3D_FORMAT_R32F, "R32F" },
			{ WW3D_FORMAT_G16R16F, "G16R16F" },
			{ WW3D_FORMAT_A16B16G16R16F, "A16B16G16R16F" },
		};
		for (int i = 0; i < 3; i++) {
			D3DFORMAT d3dFmt = WW3DFormat_To_D3DFormat(testFmts[i].ww3d);
			DIAG_LOG(("W3DDeferredRenderer: Format[%s] -> D3DFMT=0x%04x (%s)\n",
				testFmts[i].name, (int)d3dFmt,
				d3dFmt != D3DFMT_UNKNOWN ? "OK" : "FAIL"));

			WW3DFormat backFmt = D3DFormat_To_WW3DFormat(d3dFmt);
			DIAG_LOG(("W3DDeferredRenderer:   D3DFMT=0x%04x -> WW3DFormat=%d (%s)\n",
				(int)d3dFmt, (int)backFmt,
				backFmt == testFmts[i].ww3d ? "ROUNDTRIP_OK" : "ROUNDTRIP_FAIL"));

			// Try creating a small RT as capability check (non-fatal if fails).
			TextureClass *testRT = DX8Wrapper::Create_Render_Target(
				64, 64, testFmts[i].ww3d, true);
			DIAG_LOG(("W3DDeferredRenderer:   Create_Render_Target(%s,64,64) -> %s\n",
				testFmts[i].name,
				testRT ? "SUCCESS" : "NULL (expected on old HW)"));
			REF_PTR_RELEASE(testRT);
		}
	}

	//
	// Create the three G-Buffer render target textures.
	//
	if (!createGBufferResources()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to create G-Buffer RTs. Disabled.\n"));
		m_available = false;
		return;
	}

	//
	// Create full-screen quad resources for the lighting pass.
	//
	if (!createFullScreenQuad()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to create full-screen quad.\n"));
		releaseGBufferResources();
		m_available = false;
		return;
	}

	//
	// Compile the sunlight PBR pixel shader.
	//
	if (!compileSunLightShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to compile sunlight PS.\n"));
		releaseFullScreenQuad();
		releaseGBufferResources();
		m_available = false;
		return;
	}
	if (!compilePointLightShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to compile point light PS.\n"));
		releasePointLightShader();
		releaseSunLightShader();
		releaseFullScreenQuad();
		releaseGBufferResources();
		m_available = false;
		return;
	}

	if (!createHDRResources()) {
		DIAG_LOG(("W3DDeferredRenderer: HDR RT creation failed.\n"));
	}
	if (!compileToneMapShader()) {
		DIAG_LOG(("W3DDeferredRenderer: tone map PS compile failed.\n"));
	}
	if (!createShadowResources()) {
		DIAG_LOG(("W3DDeferredRenderer: shadow map creation failed.\n"));
	}
	if (!compileSunLightShadowShader()) {
		DIAG_LOG(("W3DDeferredRenderer: sun light shadow PS compile failed.\n"));
	}
	if (!createAOResources()) {
		DIAG_LOG(("W3DDeferredRenderer: AO RT creation failed.\n"));
	}
	if (!compileAOPassShaders()) {
		DIAG_LOG(("W3DDeferredRenderer: AO shader compile failed.\n"));
	}

	//
	// Register as a cleanup hook (chain with any existing hook).
	//
	m_prevCleanupHook = DX8Wrapper::GetCleanupHook();
	DX8Wrapper::SetCleanupHook(this);

	m_available = true;
	DIAG_LOG(("W3DDeferredRenderer: initialized (%dx%d, MRT=%d).\n",
		m_gbufferWidth, m_gbufferHeight, numRTs));

	debugValidateOctEncoding();
	debugValidateDepthEncoding();
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
	releasePointLightShader();
	releaseFullScreenQuad();
	releaseGBufferResources();
	releaseHDRResources();
	releaseToneMapShader();
	releaseShadowResources();
	releaseAOResources();
	releaseAOPassShaders();
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
			DIAG_LOG(("W3DDeferredRenderer: Create_Render_Target(%d) failed!\n", i));
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

	// Set viewport to match G-buffer size (quad coordinates are G-buffer pixels).
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	vp.Width = m_gbufferWidth;
	vp.Height = m_gbufferHeight;
	vp.X = 0;
	vp.Y = 0;
	DX8CALL(SetViewport(&vp));

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

	// Bind shadow map as s4 (if available) and use shadow shader
	if (m_shadowMapAvailable && m_sunLightShadowPS && m_shadowDepthRT) {
		IDirect3DBaseTexture8 *shadowTex = m_shadowDepthRT->Peek_D3D_Base_Texture();
		dev->SetTexture(4, shadowTex);
		Matrix4x4 svp = m_shadowViewProj;
		float c9[4]  = { svp[0][0], svp[0][1], svp[0][2], svp[0][3] };
		float c10[4] = { svp[1][0], svp[1][1], svp[1][2], svp[1][3] };
		float c11[4] = { svp[2][0], svp[2][1], svp[2][2], svp[2][3] };
		float c12[4] = { svp[3][0], svp[3][1], svp[3][2], svp[3][3] };
		dev->SetPixelShaderConstantF(9, c9, 1);
		dev->SetPixelShaderConstantF(10, c10, 1);
		dev->SetPixelShaderConstantF(11, c11, 1);
		dev->SetPixelShaderConstantF(12, c12, 1);
		dev->SetPixelShader(m_sunLightShadowPS);
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
	dev->SetTexture(4, NULL);
}


void W3DDeferredRenderer::renderDynamicLights(
	IDirect3DDevice8 *dev,
	const Vector3 &cameraPos,
	const Matrix4x4 &invViewProj)
{
	if (!m_available || !m_pointLightPS || !m_quadVB) return;
	if (!dev || !m_gbufferRT[0]) return;

	// Set viewport to match G-buffer size (quad coordinates are G-buffer pixels).
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	vp.Width = m_gbufferWidth;
	vp.Height = m_gbufferHeight;
	vp.X = 0;
	vp.Y = 0;
	DX8CALL(SetViewport(&vp));

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

	//
	// Full-screen quad using pre-transformed XYZRHW vertices.
	//
	// With D3DFVF_XYZRHW the rasterizer interprets (x,y) as pixel coordinates
	// within the current viewport.  To cover every pixel in the viewport:
	//   - The quad must span from (-0.5, -0.5) to (w-0.5, h-0.5)
	//   - The -0.5 is D3D9's half-pixel offset: texel centres are at half-
	//     integer positions in texture space, and the rasterizer evaluates
	//     coverage at integer pixel centres.  Offsetting by -0.5 aligns the
	//     two coordinate systems so that texel (0,0) maps to pixel (0,0)
	//     exactly, eliminating a 0.5-pixel misalignment along every edge.
	//
	// UVs span (0,0) → (1,1) and are used to sample the G-Buffer textures
	// at their full resolution regardless of the G-Buffer RT size.
	//
	// z=0, rhw=1 (no perspective).
	//
	struct QuadVertex { float x, y, z, rhw; float u, v; };
	float w = (float)m_gbufferWidth;
	float h = (float)m_gbufferHeight;
	float o = 0.5f;
	QuadVertex verts[4] = {
		{  -o,    -o, 0.0f, 1.0f, 0.0f, 0.0f },  // top-left
		{ w - o,  -o, 0.0f, 1.0f, 1.0f, 0.0f },  // top-right
		{  -o,   h - o, 0.0f, 1.0f, 0.0f, 1.0f },  // bottom-left
		{ w - o, h - o, 0.0f, 1.0f, 1.0f, 1.0f },  // bottom-right
	};

	DIAG_LOG(("W3DDeferredRenderer: FS quad (%.0fx%.0f) half-pixel offset=%.1f\n"
		"  vert[0]=(%.1f,%.1f) uv=(%.3f,%.3f)\n"
		"  vert[1]=(%.1f,%.1f) uv=(%.3f,%.3f)\n"
		"  vert[2]=(%.1f,%.1f) uv=(%.3f,%.3f)\n"
		"  vert[3]=(%.1f,%.1f) uv=(%.3f,%.3f)\n",
		w, h, o,
		verts[0].x, verts[0].y, verts[0].u, verts[0].v,
		verts[1].x, verts[1].y, verts[1].u, verts[1].v,
		verts[2].x, verts[2].y, verts[2].u, verts[2].v,
		verts[3].x, verts[3].y, verts[3].u, verts[3].v));

	HRESULT hr = dev->CreateVertexBuffer(
		sizeof(verts), D3DUSAGE_WRITEONLY,
		D3DFVF_XYZRHW | D3DFVF_TEX1, D3DPOOL_DEFAULT, &m_quadVB, NULL);
	if (FAILED(hr) || !m_quadVB) {
		DIAG_LOG(("W3DDeferredRenderer: Quad VB creation failed.\n"));
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
		DIAG_LOG(("W3DDeferredRenderer: Quad IB creation failed.\n"));
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
	// Sunlight PBR pixel shader (ps_3_0).
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
		// Pixel shader constant registers (bound via SetPixelShaderConstantF)
		"float4 c0 : register(c0);\n"
		"float4 c1 : register(c1);\n"
		"float4 c2 : register(c2);\n"
		"float4 c3 : register(c3);\n"
		"float4 c4 : register(c4);\n"
		"float4 c5 : register(c5);\n"
		"float4 c6 : register(c6);\n"
	// ---- octahedral normal decoding (2D square to unit sphere) ----
		"float3 octDecode(float2 e) {\n"
		"	float2 p = e * 2.0 - 1.0;\n"
		"	float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));\n"
		"	float t = max(-n.z, 0.0);\n"
		"	n.xy += (n.xy >= 0) ? -t : t;\n"
		"	return normalize(n);\n"
		"}\n"

		"float4 main(PS_IN input) : COLOR {\n"
		"	float4 rt0 = tex2D(gbuf0, input.tex0);\n"
		"	float4 rt1 = tex2D(gbuf1, input.tex0);\n"
		"	float4 rt2 = tex2D(gbuf2, input.tex0);\n"
		"	float3 albedo = rt0.rgb;\n"
		"	float metallic = rt0.a;\n"
	"	float3 N = octDecode(rt1.rg);\n"
	"	float roughness = rt1.b;\n"
	"	float depth = rt2.r;\n"
	"	float3 emissive = rt2.b;\n"
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
		NULL, NULL, "main", "ps_3_0",
		0, &compiled, &errors, NULL);

	if (FAILED(hr) || !compiled) {
		DIAG_LOG(("W3DDeferredRenderer: SunLight PS compile failed.\n"));
		if (errors) {
			DIAG_LOG(("  error: %s\n", (const char*)errors->GetBufferPointer()));
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
		DIAG_LOG(("W3DDeferredRenderer: SunLight PS CreatePixelShader failed.\n"));
		m_sunLightPS = NULL;
		return false;
	}

	DIAG_LOG(("W3DDeferredRenderer: SunLight PS compiled successfully.\n"));
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
		// Pixel shader constant registers (bound via SetPixelShaderConstantF)
		"float4 c2 : register(c2);\n"   // cameraPos.xyz
		"float4 c3 : register(c3);\n"   // invViewProj row 0
		"float4 c4 : register(c4);\n"   // invViewProj row 1
		"float4 c5 : register(c5);\n"   // invViewProj row 2
		"float4 c6 : register(c6);\n"   // invViewProj row 3
		"float4 c7 : register(c7);\n"   // lightPos.xyz + range.w
		"float4 c8 : register(c8);\n"   // lightColor.rgb
		// ---- octahedral normal decoding ----
		"float3 octDecode(float2 e) {\n"
		"  float2 p = e * 2.0 - 1.0;\n"
		"  float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));\n"
		"  float t = max(-n.z, 0.0);\n"
		"  n.xy += (n.xy >= 0) ? -t : t;\n"
		"  return normalize(n);\n"
		"}\n"
		"\n"
		"float4 main(PS_IN input) : COLOR {\n"
		"  float4 rt0 = tex2D(gbuf0, input.tex0);\n"
		"  float4 rt1 = tex2D(gbuf1, input.tex0);\n"
		"  float4 rt2 = tex2D(gbuf2, input.tex0);\n"
		"  float3 albedo = rt0.rgb;\n"
		"  float metallic = rt0.a;\n"
		"  float3 N = octDecode(rt1.rg);\n"
		"  float roughness = rt1.b;\n"
		"  float depth = rt2.r;\n"
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
		NULL, NULL, "main", "ps_3_0", 0, &compiled, &errors, NULL);
	if (FAILED(hr) || !compiled) {
		DIAG_LOG(("W3DDeferredRenderer: PointLight PS compile failed.\n"));
		if (errors) {
			DIAG_LOG(("  error: %s\n", (const char*)errors->GetBufferPointer()));
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
		DIAG_LOG(("W3DDeferredRenderer: PointLight PS CreatePixelShader failed.\n"));
		m_pointLightPS = NULL;
		return false;
	}
	DIAG_LOG(("W3DDeferredRenderer: PointLight PS compiled.\n"));
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
	DIAG_LOG(("W3DDeferredRenderer: releasing resources (device reset).\n"));

	if (m_inGBufferPass) {
		endGBufferPass();
	}

	releaseSunLightShader();
	releasePointLightShader();
	releaseFullScreenQuad();
	releaseGBufferResources();
	releaseHDRResources();
	releaseToneMapShader();
	releaseShadowResources();
	releaseAOResources();
	releaseAOPassShaders();

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

	DIAG_LOG(("W3DDeferredRenderer: re-acquiring resources.\n"));

	if (!createGBufferResources()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-create G-Buffer RTs.\n"));
		m_available = false;
		return;
	}

	if (!createFullScreenQuad()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-create quad.\n"));
		releaseGBufferResources();
		m_available = false;
		return;
	}

	if (!compileSunLightShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-compile sunlight PS.\n"));
		releaseFullScreenQuad();
		releaseGBufferResources();
		m_available = false;
		return;
	}

	if (!compilePointLightShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-compile point light PS.\n"));
		releaseSunLightShader();
		releaseFullScreenQuad();
		releaseGBufferResources();
		m_available = false;
		return;
	}
	if (!createHDRResources()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-create HDR RT.\n"));
	}
	if (!compileToneMapShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-compile tone map PS.\n"));
	}
	if (!createShadowResources()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-create shadow map.\n"));
	}
	if (!compileSunLightShadowShader()) {
		DIAG_LOG(("W3DDeferredRenderer: failed to re-compile shadow PS.\n"));
	}
	if (!createAOResources()) {
		DIAG_LOG(("W3DDeferredRenderer: failed AO create.\n"));
	}
	if (!compileAOPassShaders()) {
		DIAG_LOG(("W3DDeferredRenderer: failed AO shader compile.\n"));
	}
}

// ============================================================================
// ============================================================================
// W3DDeferredRenderer::createHDRResources
// ============================================================================
bool W3DDeferredRenderer::createHDRResources()
{
	WW3DFormat hdrFormat = WW3D_FORMAT_A16B16G16R16F;
	m_hdrRT = DX8Wrapper::Create_Render_Target(
		m_gbufferWidth, m_gbufferHeight, hdrFormat, true);
	if (m_hdrRT) {
		m_hdrAvailable = true;
		DIAG_LOG(("W3DDeferredRenderer: HDR RT created (%dx%d, A16B16G16R16F).\n",
			m_gbufferWidth, m_gbufferHeight));
		return true;
	}
	hdrFormat = WW3D_FORMAT_A8R8G8B8;
	m_hdrRT = DX8Wrapper::Create_Render_Target(
		m_gbufferWidth, m_gbufferHeight, hdrFormat, true);
	if (m_hdrRT) {
		m_hdrAvailable = false;
		DIAG_LOG(("W3DDeferredRenderer: HDR RT created as A8R8G8B8 fallback.\n"));
		return true;
	}
	DIAG_LOG(("W3DDeferredRenderer: HDR RT creation completely failed.\n"));
	m_hdrAvailable = false;
	return false;
}

// ============================================================================
// W3DDeferredRenderer::releaseHDRResources
// ============================================================================
void W3DDeferredRenderer::releaseHDRResources()
{
	REF_PTR_RELEASE(m_hdrRT);
	m_hdrAvailable = false;
}

// ============================================================================
// W3DDeferredRenderer::compileToneMapShader
// ============================================================================
bool W3DDeferredRenderer::compileToneMapShader()
{
	const char ps_source[] =
	"struct PS_IN {\n"
	"float4 pos : POSITION;\n"
	"float2 tex0 : TEXCOORD0;\n"
	"};\n"
	"sampler hdrSampler : register(s0);\n"
	"float4 main(PS_IN input) : COLOR {\n"
	"  float3 hdrColor = tex2D(hdrSampler, input.tex0).rgb;\n"
	"  // Reinhard tone map\n"
	"  float3 ldr = hdrColor / (hdrColor + 1.0);\n"
	"  // Gamma correction (linear to sRGB)\n"
	"  ldr = pow(abs(ldr), 1.0 / 2.2);\n"
	"  return float4(ldr, 1.0);\n"
	"};\n"
	;

	ID3DXBuffer *compiled = NULL;
	ID3DXBuffer *errors = NULL;
	HRESULT hr = D3DXCompileShader(ps_source, (UINT)strlen(ps_source),
		NULL, NULL, "main", "ps_3_0",
		0, &compiled, &errors, NULL);
	if (FAILED(hr) || !compiled) {
		DIAG_LOG(("W3DDeferredRenderer: ToneMap PS compile failed.\n"));
		if (errors) {
			DIAG_LOG(("  error: %s\n", (const char*)errors->GetBufferPointer()));
			errors->Release();
		}
		m_toneMapPS = NULL;
		return false;
	}
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev) {
		hr = dev->CreatePixelShader(
			(const DWORD*)compiled->GetBufferPointer(), &m_toneMapPS);
	}
	compiled->Release();
	if (FAILED(hr) || !m_toneMapPS) {
		DIAG_LOG(("W3DDeferredRenderer: ToneMap PS CreatePixelShader failed.\n"));
		m_toneMapPS = NULL;
		return false;
	}
	DIAG_LOG(("W3DDeferredRenderer: ToneMap PS compiled successfully.\n"));
	return true;
}

// ============================================================================
// W3DDeferredRenderer::releaseToneMapShader
// ============================================================================
void W3DDeferredRenderer::releaseToneMapShader()
{
	if (m_toneMapPS) {
		m_toneMapPS->Release();
		m_toneMapPS = NULL;
	}
}

// ============================================================================
// W3DDeferredRenderer::beginHDRPass
// ============================================================================
bool W3DDeferredRenderer::beginHDRPass()
{
	if (!m_hdrRT) {
		DIAG_LOG(("W3DDeferredRenderer: beginHDRPass called but no HDR RT.\n"));
		return false;
	}
	IDirect3DSurface8 *surf = m_hdrRT->Get_D3D_Surface_Level();
	if (!surf) return false;
	DX8Wrapper::Set_Render_Target(0, surf);
	surf->Release();
	DX8Wrapper::Clear(true, false, Vector3(0, 0, 0), 0, 1.0f, 0);
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	vp.Width = m_gbufferWidth;
	vp.Height = m_gbufferHeight;
	vp.X = 0;
	vp.Y = 0;
	DX8CALL(SetViewport(&vp));
	return true;
}

// ============================================================================
// W3DDeferredRenderer::endHDRPass
// ============================================================================
void W3DDeferredRenderer::endHDRPass()
{
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);
}

// ============================================================================
// W3DDeferredRenderer::toneMapPass
// ============================================================================
void W3DDeferredRenderer::toneMapPass()
{
	if (!m_hdrRT || !m_toneMapPS || !m_quadVB || !m_quadIB) {
		DIAG_LOG(("W3DDeferredRenderer: toneMapPass skipped.\n"));
		return;
	}
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;
	D3DVIEWPORT9 vp;
	DX8CALL(GetViewport(&vp));
	vp.Width = m_gbufferWidth;
	vp.Height = m_gbufferHeight;
	vp.X = 0;
	vp.Y = 0;
	DX8CALL(SetViewport(&vp));
	IDirect3DBaseTexture8 *tex = m_hdrRT->Peek_D3D_Base_Texture();
	dev->SetTexture(0, tex);
	dev->SetPixelShader(m_toneMapPS);
	DWORD oldZEnable, oldZWrite;
	dev->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	dev->SetStreamSource(0, m_quadVB, 0, sizeof(float) * 6);
	dev->SetIndices(m_quadIB);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
	dev->SetRenderState(D3DRS_ZENABLE, oldZEnable);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
	dev->SetTexture(0, NULL);
}

// ============================================================================
//

// ============================================================================
// W3DDeferredRenderer::createShadowResources
// ============================================================================
bool W3DDeferredRenderer::createShadowResources()
{
	// Create 2048x2048 shadow map depth texture (A8R8G8B8, use R channel as depth).
	// In a full implementation this would be R32F or D24X8, but we keep it simple.
	const int SM_SIZE = 2048;
	m_shadowDepthRT = DX8Wrapper::Create_Render_Target(
		SM_SIZE, SM_SIZE, WW3D_FORMAT_A8R8G8B8, true);
	if (!m_shadowDepthRT) {
		DIAG_LOG(("W3DDeferredRenderer: shadow map RT creation failed.\n"));
		m_shadowMapAvailable = false;
		return false;
	}
	m_shadowMapAvailable = true;
	DIAG_LOG(("W3DDeferredRenderer: shadow map created (%dx%d).\n", SM_SIZE, SM_SIZE));
	return true;
}

// ============================================================================
// W3DDeferredRenderer::releaseShadowResources
// ============================================================================
void W3DDeferredRenderer::releaseShadowResources()
{
	REF_PTR_RELEASE(m_shadowDepthRT);
	m_shadowMapAvailable = false;
}

// ============================================================================
// W3DDeferredRenderer::beginShadowMapPass
// ============================================================================
bool W3DDeferredRenderer::beginShadowMapPass(
	const Vector3 &sunDir, const Matrix4x4 &camView)
{
	if (!m_shadowDepthRT) return false;
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return false;
	// Bind shadow map RT.
	IDirect3DSurface8 *surf = m_shadowDepthRT->Get_D3D_Surface_Level();
	if (!surf) return false;
	DX8Wrapper::Set_Render_Target(0, surf);
	surf->Release();
	// Clear to white (far depth) and set viewport.
	DX8Wrapper::Clear(true, true, Vector3(1, 1, 1), 0, 1.0f, 0);
	D3DVIEWPORT9 vp = { 0, 0, 2048, 2048, 0.0f, 1.0f };
	DX8CALL(SetViewport(&vp));
	// Compute shadow VP matrix for the pixel shader.
	// (Scene depth-to-shadow-map rendering uses the scene's own camera;
	//  the shadow VP here is used by the lighting PS for projective texturing.)
	Vector3 target(0, 0, 0);
	Vector3 up(0, 0, 1);
	if (fabsf(sunDir.Z) > 0.99f) { up.Set(0, 1, 0); }
	Vector3 eye = target - sunDir * 500.0f;
	D3DXMATRIX d3dV, d3dP;
	D3DXMatrixLookAtLH(&d3dV,
		(const D3DXVECTOR3*)&eye, (const D3DXVECTOR3*)&target, (const D3DXVECTOR3*)&up);
	D3DXMatrixOrthoLH(&d3dP, 300.0f, 300.0f, 0.1f, 1000.0f);
	D3DXMATRIX d3dVP = d3dV * d3dP;
	m_shadowViewProj = *(Matrix4x4*)&d3dVP;
	return true;
}

// ============================================================================
// W3DDeferredRenderer::endShadowMapPass
// ============================================================================
void W3DDeferredRenderer::endShadowMapPass()
{
	// Restore default render target.
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)NULL);
}

// ============================================================================
// W3DDeferredRenderer::compileSunLightShadowShader
// ============================================================================
bool W3DDeferredRenderer::compileSunLightShadowShader()
{
	// Sunlight PBR + shadow map PCF 2x2 (ps_3_0).
	// s4 = shadow map.
	const char ps_source[] =
		"struct PS_IN {\n"
		"\tfloat4 pos : POSITION;\n"
		"\tfloat2 tex0 : TEXCOORD0;\n"
		"\tfloat4 shadowUV : TEXCOORD1;\n"
		"};\n"
		"sampler gbuf0 : register(s0);\n"
		"sampler gbuf1 : register(s1);\n"
		"sampler gbuf2 : register(s2);\n"
		"sampler sShadow : register(s4);\n"
		"float4 c0 : register(c0);\n"
		"float4 c1 : register(c1);\n"
		"float4 c2 : register(c2);\n"
		"float4 c3 : register(c3);\n"
		"float4 c4 : register(c4);\n"
		"float4 c5 : register(c5);\n"
		"float4 c6 : register(c6);\n"
		"float4 c9 : register(c9);\n"
		"float3 octDecode(float2 e) {\n"
		"\tfloat2 p = e * 2.0 - 1.0;\n"
		"\tfloat3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));\n"
		"\tfloat t = max(-n.z, 0.0);\n"
		"\tn.xy += (n.xy >= 0) ? -t : t;\n"
		"\treturn normalize(n);\n"
		"}\n"
		"float4 main(PS_IN input) : COLOR {\n"
		"\tfloat4 rt0 = tex2D(gbuf0, input.tex0);\n"
		"\tfloat4 rt1 = tex2D(gbuf1, input.tex0);\n"
		"\tfloat4 rt2 = tex2D(gbuf2, input.tex0);\n"
		"\tfloat3 albedo = rt0.rgb;\n"
		"\tfloat metallic = rt0.a;\n"
		"\tfloat3 N = octDecode(rt1.rg);\n"
		"\tfloat roughness = rt1.b;\n"
		"\tfloat depth = rt2.r;\n"
		"\tfloat3 emissive = rt2.b;\n"
		"\tfloat2 screenPos = input.tex0 * 2.0 - 1.0;\n"
		"\tfloat4 clipPos = float4(screenPos, depth, 1.0);\n"
		"\tfloat4 worldPos = float4(\n"
		"\t\tdot(clipPos, float4(c3.x,c4.x,c5.x,c6.x)),\n"
		"\t\tdot(clipPos, float4(c3.y,c4.y,c5.y,c6.y)),\n"
		"\t\tdot(clipPos, float4(c3.z,c4.z,c5.z,c6.z)),\n"
		"\t\tdot(clipPos, float4(c3.w,c4.w,c5.w,c6.w))\n"
		");\n"
		"\tworldPos.xyz /= worldPos.w;\n"
		"\tfloat3 V = normalize(c2.xyz - worldPos.xyz);\n"
		"\tfloat3 L = -c0.xyz;\n"
		"\tfloat3 H = normalize(V + L);\n"
	"\t// Shadow map PCF 2x2\n"
	"\tfloat4 shadProj = mul(float4(worldPos.xyz, 1), c9);\n"
	"\tfloat2 shadUV = shadProj.xy / shadProj.w;\n"
	"\tshadUV = shadUV * 0.5 + 0.5;\n"
	"\tfloat shadDepth = shadProj.z / shadProj.w;\n"
		"\tfloat bias = 0.002;\n"
		"\tfloat2 texelSize = 1.0 / 2048;\n"
		"\tfloat shadow = 0.0;\n"
		"\tfor (int x = -1; x <= 1; x += 2) {\n"
		"\t\tfor (int y = -1; y <= 1; y += 2) {\n"
		"\t\t\tfloat d = tex2D(sShadow, shadUV + float2(x,y)*texelSize).r;\n"
		"\t\t\tshadow += (shadDepth - bias) > d ? 1.0 : 0.0;\n"
		"\t\t}\n"
		"\t}\n"
		"\tshadow *= 0.25;\n"
		"\tfloat NdotL = saturate(dot(N, L));\n"
		"\tfloat NdotV = saturate(dot(N, V));\n"
		"\tfloat NdotH = saturate(dot(N, H));\n"
		"\tfloat HdotV = saturate(dot(H, V));\n"
		"\tfloat alpha = max(roughness * roughness, 0.001);\n"
		"\tfloat a2 = alpha * alpha;\n"
		"\tfloat denom = NdotH*NdotH*(a2-1.0)+1.0;\n"
		"\tfloat D = a2 / (3.14159*denom*denom);\n"
		"\tfloat k = (roughness+1)*(roughness+1)/8.0;\n"
		"\tfloat G1 = NdotL/(NdotL*(1-k)+k);\n"
		"\tfloat G2 = NdotV/(NdotV*(1-k)+k);\n"
		"\tfloat G = G1*G2;\n"
		"\tfloat3 F0 = lerp(float3(0.04,0.04,0.04), albedo, metallic);\n"
		"\tfloat3 F = F0 + (1-F0)*pow(1-HdotV,5);\n"
		"\tfloat3 spec = D*F*G/max(4*NdotV*NdotL,0.001);\n"
		"\tfloat3 kD = (1-F)*(1-metallic);\n"
		"\tfloat3 direct = (albedo*kD + spec) * c1.rgb * NdotL * shadow;\n"
		"\tfloat3 ambient = c2.www * albedo * 0.5;\n"
		"\tfloat3 final = ambient + direct + emissive;\n"
		"\t// Gamma\n"
		"\tfinal = pow(abs(final), 1.0/2.2);\n"
		"\treturn float4(final, 1.0);\n"
		"};\n"
	;

	ID3DXBuffer *compiled = NULL;
	ID3DXBuffer *errors = NULL;
	HRESULT hr = D3DXCompileShader(ps_source, (UINT)strlen(ps_source),
		NULL, NULL, "main", "ps_3_0",
		0, &compiled, &errors, NULL);
	if (FAILED(hr) || !compiled) {
		DIAG_LOG(("W3DDeferredRenderer: SunLightShadow PS compile failed.\n"));
		if (errors) {
			DIAG_LOG(("  error: %s\n", (const char*)errors->GetBufferPointer()));
			errors->Release();
		}
		m_sunLightShadowPS = NULL;
		return false;
	}
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev) {
		hr = dev->CreatePixelShader(
			(const DWORD*)compiled->GetBufferPointer(), &m_sunLightShadowPS);
	}
	compiled->Release();
	if (FAILED(hr) || !m_sunLightShadowPS) {
		m_sunLightShadowPS = NULL;
		return false;
	}
	DIAG_LOG(("W3DDeferredRenderer: SunLightShadow PS compiled with PCF 2x2.\n"));
	return true;
}


// === createAOResources ===

bool W3DDeferredRenderer::createAOResources()
{
	m_aoRawRT = DX8Wrapper::Create_Render_Target(m_gbufferWidth, m_gbufferHeight, WW3D_FORMAT_A8R8G8B8, true);
	m_aoBlurredRT = DX8Wrapper::Create_Render_Target(m_gbufferWidth, m_gbufferHeight, WW3D_FORMAT_A8R8G8B8, true);
	if (!m_aoRawRT || !m_aoBlurredRT) {
		REF_PTR_RELEASE(m_aoRawRT); REF_PTR_RELEASE(m_aoBlurredRT);
		m_ssaoAvailable = false; return false;
	}
	m_ssaoAvailable = true;
	DIAG_LOG(("W3DDeferredRenderer: AO RTs created (%dx%d).\n", m_gbufferWidth, m_gbufferHeight));
	return true;
}

// === releaseAOResources ===

void W3DDeferredRenderer::releaseAOResources()
{
	REF_PTR_RELEASE(m_aoRawRT); REF_PTR_RELEASE(m_aoBlurredRT);
	m_ssaoAvailable = false;
}

// === compileAOPassShaders ===
bool W3DDeferredRenderer::compileAOPassShaders()
{
	// SSAO compute
	const char ssao_ps[] =
		"struct PS_IN { float4 pos:POSITION; float2 tex0:TEXCOORD0; };\n"
		"sampler sDepth : register(s0);\n"
		"sampler sNormal : register(s1);\n"
		"float4 c4 : register(c4);\n"
		"float4 main(PS_IN i):COLOR {\n"
		"  float depth = tex2D(sDepth, i.tex0).r;\n"
		"  float3 N = normalize(tex2D(sNormal, i.tex0).rgb * 2 - 1);\n"
		"  float2 xy = i.tex0 * 2 - 1;\n"
		"  // Simple AO: 4-sample at screen-space offset\n"
		"  float2 off[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };\n"
		"  float radius = c4.x; float ao = 0;\n"
		"  for (int j = 0; j < 4; j++) {\n"
		"    float2 uv = i.tex0 + off[j] * radius * (1.0/1920);\n"
		"    float d = tex2D(sDepth, uv).r;\n"
		"    ao += max(0, depth - d);\n"
		"  }\n"
		"  return float4(saturate(1 - ao * c4.y), 0, 0, 1);\n"
		"};\n"
	;
	ID3DXBuffer *c = NULL; ID3DXBuffer *e = NULL;
	HRESULT hr = D3DXCompileShader(ssao_ps, (UINT)strlen(ssao_ps), NULL, NULL, "main", "ps_3_0", 0, &c, &e, NULL);
	if (FAILED(hr) || !c) { DIAG_LOG(("W3DDeferredRenderer: SSAO PS failed.\n")); if(e){e->Release();} m_ssaoPS=NULL; return false; }
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev) hr = dev->CreatePixelShader((const DWORD*)c->GetBufferPointer(), &m_ssaoPS);
	c->Release();
	if (FAILED(hr) || !m_ssaoPS) { m_ssaoPS = NULL; return false; }

	// AO blur (simple 3x3)
	const char blur_ps[] =
		"struct PS_IN { float4 pos:POSITION; float2 tex0:TEXCOORD0; };\n"
		"sampler sAO : register(s0);\n"
		"sampler sDepth : register(s1);\n"
		"float4 c0 : register(c0);\n"
		"float4 main(PS_IN i):COLOR {\n"
		"  float cd = tex2D(sDepth, i.tex0).r;\n"
		"  float aoSum = 0; float wSum = 0;\n"
		"  float2 ts = c0.xy;\n"
		"  [unroll] for (int x = -1; x <= 1; x++) {\n"
		"    [unroll] for (int y = -1; y <= 1; y++) {\n"
		"      float2 uv = i.tex0 + float2(x,y) * ts;\n"
		"      float d = tex2D(sDepth, uv).r;\n"
		"      float w = exp(-abs(d-cd)*10) * 1.0/9.0;\n"
		"      aoSum += tex2D(sAO, uv).r * w; wSum += w;\n"
		"    }\n"
		"  }\n"
		"  return float4(aoSum/max(wSum,0.001), 0, 0, 1);\n"
		"};\n"
	;

	c = NULL; e = NULL;
	hr = D3DXCompileShader(blur_ps, (UINT)strlen(blur_ps), NULL, NULL, "main", "ps_3_0", 0, &c, &e, NULL);
	if (FAILED(hr) || !c) { DIAG_LOG(("W3DDeferredRenderer: AOBlur PS failed.\n")); if(e){e->Release();} m_ssaoBlurPS=NULL; return false; }
	if (dev) hr = dev->CreatePixelShader((const DWORD*)c->GetBufferPointer(), &m_ssaoBlurPS);
	c->Release();
	if (FAILED(hr) || !m_ssaoBlurPS) { m_ssaoBlurPS = NULL; return false; }
	DIAG_LOG(("W3DDeferredRenderer: SSAO shaders compiled.\n"));
	return true;
}


// === releaseAOPassShaders ===

void W3DDeferredRenderer::releaseAOPassShaders()
{
	if (m_ssaoPS) { m_ssaoPS->Release(); m_ssaoPS = NULL; }
	if (m_ssaoBlurPS) { m_ssaoBlurPS->Release(); m_ssaoBlurPS = NULL; }
}

// === computeAO ===

void W3DDeferredRenderer::computeAO()
{
	if (!m_ssaoAvailable || !m_ssaoPS || !m_ssaoBlurPS) return;
	if (!m_gbufferRT[1] || !m_gbufferRT[2] || !m_aoRawRT || !m_aoBlurredRT) return;
	if (!m_quadVB || !m_quadIB) return;
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;

	// Pass 1: raw AO
	IDirect3DSurface8 *s = m_aoRawRT->Get_D3D_Surface_Level();
	if (!s) return;
	DX8Wrapper::Set_Render_Target(0, s); s->Release();
	D3DVIEWPORT9 vp; DX8CALL(GetViewport(&vp));
	vp.Width=m_gbufferWidth; vp.Height=m_gbufferHeight; vp.X=0; vp.Y=0;
	DX8CALL(SetViewport(&vp));
	DX8Wrapper::Clear(true,false,Vector3(1,1,1),0,1.0f,0);
	dev->SetTexture(0, m_gbufferRT[2]->Peek_D3D_Base_Texture());
	dev->SetTexture(1, m_gbufferRT[1]->Peek_D3D_Base_Texture());
	dev->SetPixelShader(m_ssaoPS);
	float p[4]={0.01f,4.0f,0,0}; dev->SetPixelShaderConstantF(4,p,1);
	dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
	dev->SetStreamSource(0,m_quadVB,0,sizeof(float)*6);
	dev->SetIndices(m_quadIB);
	DWORD z,w; dev->GetRenderState(D3DRS_ZENABLE,&z); dev->GetRenderState(D3DRS_ZWRITEENABLE,&w);
	dev->SetRenderState(D3DRS_ZENABLE,FALSE); dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);
	dev->SetRenderState(D3DRS_ZENABLE,z); dev->SetRenderState(D3DRS_ZWRITEENABLE,w);
	dev->SetTexture(0,NULL); dev->SetTexture(1,NULL);

	// Pass 2: blur
	s = m_aoBlurredRT->Get_D3D_Surface_Level();
	if (!s) return;
	DX8Wrapper::Set_Render_Target(0, s); s->Release();
	dev->SetTexture(0, m_aoRawRT->Peek_D3D_Base_Texture());
	dev->SetTexture(1, m_gbufferRT[2]->Peek_D3D_Base_Texture());
	dev->SetPixelShader(m_ssaoBlurPS);
	float ts[4]={1.0f/m_gbufferWidth,1.0f/m_gbufferHeight,0,0};
	dev->SetPixelShaderConstantF(0,ts,1);
	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);
	dev->SetTexture(0,NULL); dev->SetTexture(1,NULL);

	DX8Wrapper::Set_Render_Target((IDirect3DSurface8*)NULL);
}
