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

// FILE: W3DShaderManager.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DShaderManager.cpp
//
// Created:   Mark Wilczynski, August 2001
//
// Desc:      Perform tests on currently selected WW3D/D3D device to determine
//			  which of our rendering features are supported.  The system allows
//			  setting up a few custom shaders that are selected based on video
//			  card features.
//
//			  To add a new shader to the system:
//			  0) Add your shader to the ShaderTypes enum
//			  1) Create shader using W3DShaderInterface
//			  2) Repeat step 1 for any alternate shaders
//			  3) Create list of alternate shaders sorted by order of preference.
//				 The first shader which passes hardware validation will be selected.
//			  4) Add list from step 3) to MasterShaderList[].
//
//-----------------------------------------------------------------------------

#include "dx8wrapper.h"
#include "assetmgr.h"
#include "Lib/BaseType.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "Common/File.h"
#include "Common/FileSystem.h"
#include <vector>
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DDeferredRenderer.h"
#include "GameClient/view.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/display.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "common/GlobalData.h"
#include "common/GameLOD.h"
#include "d3d8compat.h"
#include "dx8caps.h"
#include "common/gamelod.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// Turn this on to turn off pixel shaders. jba[4/3/2003]
#define do_not_DISABLE_PIXEL_SHADERS 1

/** Interface definition for custom shaders we define in our app.  These shaders can perform more complex
	operations than those allowed in the WW3D2 shader system.
*/
class W3DShaderInterface
{
public:
	Int getNumPasses(void) {return m_numPasses;};	///<return number of passes needed for this shader
	virtual Int set(Int pass) {return TRUE;};		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset(void) {
		ShaderClass::Invalidate();
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);};
	virtual Int init(void) = 0;			///<perform any one time initialization and validation
	virtual Int shutdown(void) { return TRUE;};			///<release resources used by shader
protected:
	Int m_numPasses;						///<number of passes to complete shader
};

//this table will contain custom versions of each shader tuned for specific video card and user options.
static W3DFilterInterface *W3DFilters[FT_MAX];
static W3DShaderInterface *W3DShaders[W3DShaderManager::ST_MAX];
static Int W3DShadersPassCount[W3DShaderManager::ST_MAX];	//number of passes for each of the above shaders
TextureClass *W3DShaderManager::m_Textures[8];
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader;
FilterTypes W3DShaderManager::m_currentFilter=FT_NULL_FILTER; ///< Last filter that was set.
Int W3DShaderManager::m_currentShaderPass;
ChipsetType W3DShaderManager::m_currentChipset;
GraphicsVenderID W3DShaderManager::m_currentVendor;
__int64 W3DShaderManager::m_driverVersion;

Bool W3DShaderManager::m_renderingToTexture = false;
IDirect3DSurface8 *W3DShaderManager::m_oldRenderSurface=NULL;	///<previous render target
IDirect3DTexture8 *W3DShaderManager::m_renderTexture=NULL;		///<texture into which rendering will be redirected.
IDirect3DSurface8 *W3DShaderManager::m_newRenderSurface=NULL;	///<new render target inside m_renderTexture
IDirect3DSurface8 *W3DShaderManager::m_oldDepthSurface=NULL;	///<previous depth buffer surface

// PBR texture pipeline static data (Phase 3+)
W3DShaderManager::PBRTextureMap *W3DShaderManager::m_pbrTextureMap = NULL;
W3DShaderManager::NormalMapMap *W3DShaderManager::m_normalMapMap = NULL;
W3DShaderManager::LegacyPBRParamsMap *W3DShaderManager::m_legacyPBRParamsMap = NULL;

// Phase 4: Extern globals for unit PBR shader handles (accessed from dx8renderer.cpp)
IDirect3DPixelShader9 *g_pbrUnitOpaqueShader = NULL;

// G-Buffer externs (defined in dx8wrapper.cpp)
extern bool g_gbufferActive;
extern IDirect3DPixelShader9 *g_gbufferPS;
extern IDirect3DVertexShader9 *g_gbufferVS;
IDirect3DPixelShader9 *g_pbrUnitAlphaShader = NULL;
IDirect3DPixelShader9 *g_pbrUnitOpaqueNTShader = NULL;  // no PBR texture variant
IDirect3DPixelShader9 *g_pbrUnitAlphaNTShader = NULL;    // alpha + no PBR texture
Bool g_pbrUnitShaderEnabled = FALSE;
Int g_pbrDebugMode = 0;
IDirect3DVertexShader9 *g_pbrUnitVS = NULL;
/*===========================================================================================*/
/*=========      Screen Shaders	=============================================================*/
/*===========================================================================================*/

class ScreenDefaultFilter : public W3DFilterInterface
{
public:
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode); ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender); ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(enum FilterModes mode){return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(enum FilterModes mode);		///<setup shader for the specified rendering pass.
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
};

ScreenDefaultFilter screenDefaultFilter;

///Default filter that just renders screen to off-screen texture and then copies it the the screen.
///Useful because we added some full-time unit effects (microwave tank smudge) to Generals MD that need access
///to the background as a texture.  This filter makes that texture always available for these effects.
W3DFilterInterface *ScreenDefaultFilterList[]=
{
	&screenDefaultFilter,
	NULL
};

Int ScreenDefaultFilter::init(void)
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return FALSE;
	}

	//Can render to texture, but we don't know if it can read and write to the same texture.
	//Since there is no D3D caps bit to tell you this, we will just hard-code some specific
	//cards that we know should work.

	Int res;

	if ((res=W3DShaderManager::getChipset()) != DC_UNKNOWN)
	{
		if ( res >=	DC_GEFORCE2)
		{	
			//Check if their driver is newer than what we tested for this vendor
/*			if (TheGameLODManager)
			{
				if (TheGameLODManager->getTestedDriverVersion(W3DShaderManager::getCurrentVendor()) < W3DShaderManager::getCurrentDriverVersion())
					return FALSE;
			}*/
		}
	}

	W3DFilters[FT_VIEW_DEFAULT]=&screenDefaultFilter;

	return TRUE;
}

Bool ScreenDefaultFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	//Right now this filter is only used for smudges, so don't bother if none are present.
	if (TheSmudgeManager)
	{	if (((W3DSmudgeManager *)TheSmudgeManager)->getSmudgeCountLastFrame() == 0)
			return FALSE;
	}
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenDefaultFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenDefaultFilter::set(enum FilterModes mode)
{
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0,NULL);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	return true;
}

void ScreenDefaultFilter::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,NULL);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.

Int ScreenBWFilter::m_fadeFrames;
Int ScreenBWFilter::m_curFadeFrame;
Real ScreenBWFilter::m_curFadeValue;
Int ScreenBWFilter::m_fadeDirection;

ScreenBWFilter screenBWFilter;
ScreenBWFilterDOT3 screenBWFilterDOT3;	//slower version for older cards without pixel shaders.

///List of different BW shader implementations in order of preference
W3DFilterInterface *ScreenBWFilterList[]=
{
	&screenBWFilter,
	&screenBWFilterDOT3,	//slower version for older cards without pixel shaders.
	NULL
};

Int ScreenBWFilter::init(void)
{
	Int res;
	HRESULT hr;

	m_dwBWPixelShader = NULL;
	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//Monochrome pixel shader.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\monochrome.pso", NULL, 0, false, (void**)&m_dwBWPixelShader);
			if (FAILED(hr))
				return FALSE;

			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;

			return TRUE;
		}
	}
	return FALSE;
}

Bool ScreenBWFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilter::set(enum FilterModes mode)
{
	HRESULT hr;

	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		hr=DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBWPixelShader);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(0, (const float*)&D3DXVECTOR4(0.3f, 0.59f, 0.11f, 1.0f), 1);

		D3DXVECTOR4	color(1.0f,1.0f,1.0f,1.0f);	//multiply color

		if (mode == FM_VIEW_BW_BLACK_AND_WHITE)
		{	//back & white mode
			color.x=1.0f;
			color.y=1.0f;
			color.z=1.0f;
		}
		if (mode == FM_VIEW_BW_RED_AND_WHITE)
		{	//red is on
			color.x = 1.0f;
			color.y = 0.0f;
			color.z = 0.0f;
			//inverse red is on
			//red is on
//			color.x = 0.0f;
//			color.y = 1.0f;
//			color.z = 1.0f;
		}
		if (mode == FM_VIEW_BW_GREEN_AND_WHITE)
		{
			color.x = 0.0f;
			color.y = 1.0f;
			color.z = 0.0f;
		}

		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(1, (const float*)&color, 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(2, (const float*)&D3DXVECTOR4(m_curFadeValue, m_curFadeValue, m_curFadeValue, 1.0f), 1);
/*		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(2,   D3DXVECTOR4(150.0f/255.0f, 150.0f/255.0f, 150.0f/255.0f, 0.0f), 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(3,   D3DXVECTOR4((765.0f/450.0f)/3, (765.0f/450.0f)/3, (765.0f/450.0f)/3, 1.0f), 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(4,   D3DXVECTOR4(0.5f, 0.5f, 0.5f, 0), 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(5,   D3DXVECTOR4((60.0f)/255.0f, (60.0f)/255.0f, (60.0f)/255.0f, 0), 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(6,   D3DXVECTOR4((157.0f)/255.0f, (157.0f)/255.0f, (157.0f)/255.0f, 0), 1);
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(7,   D3DXVECTOR4((30.0f)/255.0f, (30.0f)/255.0f, (30.0f)/255.0f, 0), 1);
*/
		return true;
	}
	return false;
}

void ScreenBWFilter::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,NULL);	//previously rendered frame inside this texture
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);	//turn off pixel shader
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilter::shutdown(void)
{
	if (m_dwBWPixelShader)
				m_dwBWPixelShader->Release();

	m_dwBWPixelShader=NULL;

	return TRUE;
}

/**Alternate version of the above filter which does not require pixel shaders - good for older cards*/
Int ScreenBWFilterDOT3::init(void)
{
	Int res;

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilterDOT3;
			return TRUE;
	}
	return FALSE;
}

Bool ScreenBWFilterDOT3::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilterDOT3::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	
	DWORD currentFade=(((Int)((1.0f-m_curFadeValue) * 255.0f))<<24) | 0x00ffffff;	//store alpha value

	v[0].color = currentFade;
	v[1].color = currentFade;
	v[2].color = currentFade;
	v[3].color = currentFade;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	//Draw B&W version first
	if (DX8Wrapper::Get_Current_Caps()->Support_Dot3())
	{	//Override W3D states with customizations for grayscale
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x80A5CA8E);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG0, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
	}
	else
	{	//doesn't have DOT3 blend mode so fake it another way.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x60606060);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	}

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	//Draw normal view blended by current fade level
	ShaderClass::Invalidate();	//reset DOT3 blend from above.
	ShaderClass shader=ShaderClass::_PresetAlphaShader;
	shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	//replace texture alpha with vertex alpha
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilterDOT3::set(enum FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		return true;
	}
	return false;
}

void ScreenBWFilterDOT3::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,NULL);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilterDOT3::shutdown(void)
{
	return TRUE;
}

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades screen between 2 different views of the scene with both being visible at once.

Int ScreenCrossFadeFilter::m_fadeFrames;
Int ScreenCrossFadeFilter::m_curFadeFrame;
Real ScreenCrossFadeFilter::m_curFadeValue;
Int ScreenCrossFadeFilter::m_fadeDirection;
TextureClass *ScreenCrossFadeFilter::m_fadePatternTexture=NULL;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;

ScreenCrossFadeFilter screenCrossFadeFilter;

///List of different BW shader implementations in order of preference
///@todo: Add a version that doesn't require pixel shader
W3DFilterInterface *ScreenCrossFadeFilterList[]=
{
	&screenCrossFadeFilter,
	NULL
};

Int ScreenCrossFadeFilter::init(void)
{
	if (!TheDisplay)
		return FALSE;	//effect is useless without a view so no point initializing for the WB, etc.

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture())
		// Have to be able to render to texture.
		return FALSE;

	//Load an alpha mask texture that will mix foreground/background views.
	m_fadePatternTexture=WW3DAssetManager::Get_Instance()->Get_Texture("exmask_g.tga");
	if (!m_fadePatternTexture)
		return FALSE;
	m_fadePatternTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	W3DFilters[FT_VIEW_CROSSFADE]=&screenCrossFadeFilter;

	return TRUE;
}

Bool ScreenCrossFadeFilter::updateFadeLevel(void)
{
	if (m_fadeDirection > 0)
	{	//turning effect on
		m_curFadeFrame++;
		Int fade = m_curFadeFrame;

		if (fade<m_fadeFrames)
		{
			m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
		}
		else
		{
			m_curFadeFrame = 0;
			m_curFadeValue = 1.0f;
			m_fadeDirection = 0;
			return false;
		}
	}
	else
	if (m_fadeDirection < 0)
	{	//turning effect off
		Int fade = m_curFadeFrame;
		if (fade<m_fadeFrames)
		{
			m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			m_curFadeFrame++;
		}
		else
		{	m_curFadeValue = 0.0f;
			TheTacticalView->setViewFilterMode(FM_NULL_MODE);
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
			m_curFadeFrame = 0;
			m_fadeDirection = 0;
			return false;
		}
	}
	return true;
}

Bool ScreenCrossFadeFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (updateFadeLevel())
	{	//if fade has not completed
		W3DShaderManager::startRenderToTexture();
		scenePassMode=SCENE_PASS_ALPHA_MASK;
		skipRender = false;
		m_skipRender=true;	//tell the postRender function not to draw into framebuffer yet.
		return true;
	}
	//fade must have completed
	return true;
}

Bool ScreenCrossFadeFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex;

	if (m_skipRender)
	{
		//don't render anything to frame buffer because we still need to draw the new scene
		//that we're fading into.  Okay to render on the next call.
		m_skipRender = false;
		doExtraRender = TRUE;
		tex =	W3DShaderManager::endRenderToTexture();
		return true;	
	}

	tex=W3DShaderManager::getRenderTexture();

	DEBUG_ASSERTCRASH(tex, ("Require last rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
		float	u1;
		float	v1;
	} v[4];

	Int xpos, ypos, width, height;
	Real radius = 0.0f;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	if (mode == FM_VIEW_CROSSFADE_CIRCLE)
	{	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1,m_fadePatternTexture->Peek_D3D_Texture());
		//Use the current fade level to scale the mask texture, for other modes the texture
		//comes pre-scaled so doesn't require uv scaling.
		radius = (1.0f-m_curFadeValue)*2.0f;
		if (radius <= 0)
			radius = 0.01f;
		radius = 0.5f/radius;
	}

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

/*	Real radius = (1.0f-m_curFadeValue);
	if (radius <= 0)
		radius = 0.01f;
	radius = 25.0f-radius*24.75f;
*/
	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[0].u1 = 0.5f+radius;	v[0].v1 = 0.5f+radius;
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[1].u1 = 0.5f+radius;	v[1].v1 = 0.5f-radius;
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[2].u1 = 0.5f-radius;	v[2].v1 = 0.5f+radius;
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[3].u1 = 0.5f-radius;	v[3].v1 = 0.5f-radius;

	DWORD diffuse = 0xffffffff;//((Int)((m_curFadeValue) * 255.0f) << 24) | 0x00ffffff;	//store alpha value in vertex diffuse

	v[0].color = diffuse;
	v[1].color = diffuse;
	v[2].color = diffuse;
	v[3].color = diffuse;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);

//		m_pDev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT); 
//		m_pDev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT); 

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenCrossFadeFilter::set(enum FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

		if (mode == FM_VIEW_CROSSFADE_CIRCLE)
		{	//cross-fading using circle mask stored in stage 1
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_MIPFILTER, D3DTEXF_NONE);
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);

		return true;
	}
	return false;
}

void ScreenCrossFadeFilter::reset(void)
{
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,NULL);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenCrossFadeFilter::shutdown(void)
{
	REF_PTR_RELEASE(m_fadePatternTexture);

	return TRUE;
}

/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.

ScreenMotionBlurFilter screenMotionBlurFilter;

Coord3D ScreenMotionBlurFilter::m_zoomToPos;
Bool ScreenMotionBlurFilter::m_zoomToValid = false;

ScreenMotionBlurFilter::ScreenMotionBlurFilter():
m_decrement(false),
m_maxCount(0),
m_lastFrame(0), 
m_skipRender(false)
{
}
///List of different motion blur implementations in order of preference
W3DFilterInterface *ScreenMotionBlurFilterList[]=
{
	&screenMotionBlurFilter,
	NULL
};

Int ScreenMotionBlurFilter::init(void)
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}
	W3DFilters[FT_VIEW_MOTION_BLUR_FILTER]=this;
	return true;
}

Bool ScreenMotionBlurFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = m_skipRender;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenMotionBlurFilter::postRender(enum FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	Bool continueEffect = true;
	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;


	if (m_additive) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ONE);
	} else { 
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Apply_Render_State_Changes();
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	Coord2D center;
	center.x = 0.5f;
	center.y = 0.5f;
	Bool pan = false;
	if (mode>=FM_VIEW_MB_PAN_ALPHA) {
		Real len = sqrt(scrollDelta.x*scrollDelta.x + scrollDelta.y*scrollDelta.y);
		//center.x += 0.5f * (scrollDelta.x/len);
		center.y -= 0.5f; // * (scrollDelta.y/len);
		m_decrement = false;
		m_maxCount = (len*200*m_panFactor/(Real)DEFAULT_PAN_FACTOR);
		if (m_maxCount<m_panFactor/2) 
			m_maxCount = m_panFactor/2;
		if (m_maxCount>m_panFactor) 
			m_maxCount=m_panFactor;
		pan = true;
		m_priorDelta = scrollDelta;
	} else if (mode == FM_VIEW_MB_END_PAN_ALPHA) {
		Real len = sqrt(m_priorDelta.x*m_priorDelta.x + m_priorDelta.y*m_priorDelta.y);
		center.x += 0.5f * (m_priorDelta.x/len);
		center.y -= 0.5f * (m_priorDelta.y/len);
		m_decrement = false;
		m_maxCount--;
		if (m_maxCount<2) {
			continueEffect = false;
		}
		pan = true;
	}


	m_skipRender = false;
	if (!pan && m_lastFrame != TheGameLogic->getFrame()) {
		if (m_decrement) {
			m_maxCount-=COUNT_STEP;
			if (m_maxCount<1) {
				m_decrement = false;
				continueEffect = false;
			}	else {
				m_skipRender = true;
			}
		} else {
			m_maxCount+=COUNT_STEP;
			if (m_maxCount>=MAX_COUNT) {
				m_decrement = true;
				if (m_doZoomTo && m_zoomToValid) {
					TheTacticalView->lookAt(&m_zoomToPos);
				} else {
					continueEffect = false;
				}
			}	else {
				m_skipRender = true;
			}
		}
	}
	Int	 i, j;
	if (!pan) {
		for (i=0; i<4; i++) {
			Real factor = 1.0f - (m_maxCount/(Real)MAX_COUNT)*0.90f;
			factor = sqrt(factor);
			v[i].u = ((v[i].u-center.x)*factor) + center.x;
			v[i].v = ((v[i].v-center.y)*factor) + center.y;
		}
	}
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);

	DX8Wrapper::Apply_Render_State_Changes();
	{
		Int limit = m_maxCount;
		if (m_maxCount>30) limit = 30;
		for (j=0; j<limit; j++) {
			for (i=0; i<4; i++) {
				Real factor = 0.99f;
				if (m_additive) factor = 0.98f;
				Int alpha = 0x15;
				if (m_additive) {
					alpha = 0x09;
					if (m_maxCount>limit) {
						alpha += (m_maxCount-limit)/5;
					}
					if (m_maxCount==MAX_COUNT) alpha += 60;
				}
				v[i].color = (alpha<<24)|0x00ffffff; // 
				if (pan) {
					v[i].u = ((v[i].u-center.x)*(factor+.006)) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				} else {
					v[i].u = ((v[i].u-center.x)*factor) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				}
			}
			pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

		}
	}
	m_lastFrame = TheGameLogic->getFrame();
	if (pan){
		m_skipRender = false;
	}
	reset();
	if (!continueEffect) {
		m_zoomToValid = false;
	}
	return continueEffect;
}

Bool ScreenMotionBlurFilter::setup(enum FilterModes mode)
{

	m_additive = false;

	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_SATURATE ||
			mode == FM_VIEW_MB_OUT_SATURATE) {
		m_additive = true;
	}

	m_doZoomTo = false;
	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_AND_OUT_ALPHA ) {
		m_doZoomTo = true;
	}
	if (mode >= FM_VIEW_MB_PAN_ALPHA)	{
		m_panFactor = (int)mode - FM_VIEW_MB_PAN_ALPHA;
		if (m_panFactor<1) m_panFactor = DEFAULT_PAN_FACTOR;
	}
	m_skipRender = false;
	if (mode != FM_VIEW_MB_END_PAN_ALPHA) 
		m_maxCount = 0;
	m_decrement = false;
	m_skipRender = false;
	switch (mode) {
		case FM_VIEW_MB_OUT_SATURATE:
		case FM_VIEW_MB_OUT_ALPHA:
			m_maxCount = MAX_COUNT;
			m_decrement = TRUE;
			break;
	}
	return true;
}

Int ScreenMotionBlurFilter::set(enum FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface motion blurred

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,NULL);
		DX8Wrapper::Set_Texture(1,NULL);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	}
	return TRUE;
}

void ScreenMotionBlurFilter::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0,NULL);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenMotionBlurFilter::shutdown(void)
{
	return TRUE;
}

/*===========================================================================================*/
/*=========      Shroud Shaders	=============================================================*/
/*===========================================================================================*/

///Shroud layer rendering shader
class ShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} shroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *ShroudShaderList[]=
{
	&shroudTextureShader,
	NULL
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int ShroudTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_SHROUD_TEXTURE]=&shroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int ShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Texture(stage, W3DShaderManager::getShaderTexture(0));	//shroud always stored in texture 0

	if (stage == 0)
	{
#if defined(_DEBUG) || defined(_INTERNAL)
	if (TheGlobalData && TheGlobalData->m_fogOfWarOn)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaSpriteShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#else
	DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#endif
	}
	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_EQUAL);

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		D3DXMATRIX inv;
		float det;

		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		D3DXMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		D3DXMatrixScaling(&scale, width, height, 1);
		*((D3DXMATRIX *)&curView) = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), *((Matrix4x4*)&curView));
	}
	m_stageOfSet=stage;
	return TRUE;
}

void ShroudTextureShader::reset(void)
{
	DX8Wrapper::Set_Texture(m_stageOfSet,NULL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXCOORDINDEX, m_stageOfSet);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);	
}

///Shroud layer rendering shader
class FlatShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} flatShroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *FlatShroudShaderList[]=
{
	&flatShroudTextureShader,
	NULL
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int FlatShroudTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_FLAT_SHROUD_TEXTURE]=&flatShroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int FlatShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	if (stage < 2)
		DX8Wrapper::Set_Texture(stage, W3DShaderManager::getShaderTexture(stage));
	else	//stages larger than 1 are not supported by W3D so set them directly
		DX8Wrapper::Set_DX8_Texture(stage, W3DShaderManager::getShaderTexture(stage)->Peek_D3D_Texture());

	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	//DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		D3DXMATRIX inv;
		float det;

		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		D3DXMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		D3DXMatrixScaling(&scale, width, height, 1);
		*((D3DXMATRIX *)&curView) = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), *((Matrix4x4*)&curView));
	}
	m_stageOfSet=stage;
	return TRUE;
}

void FlatShroudTextureShader::reset(void)
{
	if (m_stageOfSet < MAX_TEXTURE_STAGES)
		DX8Wrapper::Set_Texture(m_stageOfSet,NULL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXCOORDINDEX, m_stageOfSet);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);	
}

///Mask layer rendering shader
class MaskTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
} maskTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *MaskShaderList[]=
{
	&maskTextureShader,
	NULL
};

Int MaskTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_MASK_TEXTURE]=&maskTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_MASK_TEXTURE]=1;

	return TRUE;
}

Int MaskTextureShader::set(Int pass)
{
	Real fadeLevel=ScreenCrossFadeFilter::getCurrentFadeValue();

	//Use the current fade level to scale the mask texture
	Real radius = (1.0f-fadeLevel)*2.0f;
	if (radius <= 0)
		radius = 0.01f;
	radius = 0.5f/radius;

	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

	//For now we're always going to project the texture coming from the crossfade effect
	DX8Wrapper::Set_Texture(0, ScreenCrossFadeFilter::getCurrentMaskTexture());
	ShaderClass shader=ShaderClass::_PresetOpaqueShader;
	shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();
	Matrix4x4 curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

	D3DXMATRIX inv;
	float det;

	//Get inverse view matrix so we can transform camera space points back to world space
	D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

	D3DXMATRIX scale,offset,offsetTextureCenter;
	Coord3D centerPos;

	//Find center of projection (this should be returned from some other filter, etc. but
	//for now assume terrain location at center of screen.
	if (TheTacticalView)
	{	Int xpos,ypos;

		TheTacticalView->getOrigin(&xpos,&ypos);

		ICoord2D screenPos;
		screenPos.x=(Real)TheTacticalView->getWidth()*0.5f;
		screenPos.y=(Real)TheTacticalView->getHeight()*0.5f;
		TheTacticalView->screenToTerrain(&screenPos,&centerPos);
	}

	D3DXMatrixTranslation(&offset, -centerPos.x, -centerPos.y,0);

	D3DXMatrixTranslation(&offsetTextureCenter, 0.5f, 0.5f, 0);	//shift coordinates so center of projection falls at uv 0.5,0.5

	Real worldTexelWidth=(1.0f-fadeLevel)*25.0f;	//9 worked well for circle but weird shape requires more stretch to cover.
	Real worldTexelHeight=(1.0f-fadeLevel)*25.0f;

	///@todo: Fix this to work with non 128x128 textures.
	if (worldTexelWidth != 0 && worldTexelHeight != 0)
	{	Real widthScale = 1.0f/(worldTexelWidth*128.0f);
		Real heightScale = 1.0f/(worldTexelHeight*128.0f);
		D3DXMatrixScaling(&scale, widthScale, heightScale, 1);
		*((D3DXMATRIX *)&curView) = ((inv * offset) * scale)*offsetTextureCenter;
	}
	else
	{	D3DXMatrixScaling(&scale, 0, 0, 1);	//scaling by 0 will set uv coordinates to 0,0
		*((D3DXMATRIX *)&curView) = ((inv * offset) * scale);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, *((Matrix4x4*)&curView));

	return TRUE;
}

void MaskTextureShader::reset(void)
{
	DX8Wrapper::Set_Texture(0,NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);	
}

/*===========================================================================================*/
/*=========      Terrain Shaders	=========================================================*/
/*===========================================================================================*/

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class TerrainShader2Stage : public W3DShaderInterface
{
public:
	float m_xSlidePerSecond ;	 ///< How far the clouds move per second.
	float m_ySlidePerSecond ;	 ///< How far the clouds move per second.
	int	  m_curTick;
	float m_xOffset;
	float m_yOffset;
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	void updateNoise1 (D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise1 (i.e clouds)
	void updateNoise2 (D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise2 (i.e lightmap)
} terrainShader2Stage;

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class FlatTerrainShader2Stage : public W3DShaderInterface
{
public:
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
} flatTerrainShader2Stage;

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class FlatTerrainShaderPixelShader : public W3DShaderInterface
{
public:
	IDirect3DPixelShader9*	m_dwBasePixelShader;	///<handle to terrain D3D pixel shader
	IDirect3DPixelShader9*	m_dwBaseNoise1PixelShader;	///<handle to terrain/single noise D3D pixel shader
	IDirect3DPixelShader9*	m_dwBaseNoise2PixelShader;	///<handle to terrain/double noise D3D pixel shader
	IDirect3DPixelShader9*	m_dwBase0PixelShader;	///<handle to terrain only pixel shader
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int shutdown(void);			///<release resources used by shader
} flatTerrainShaderPixelShader;

///8 stage terrain shader which only works on certain Nvidia cards.
class TerrainShader8Stage : public W3DShaderInterface
{
	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init(void);			///<perform any one time initialization and validation
} terrainShader8Stage;

//Offsets into constant register pool used by vertex shader
#define CV_WORLDVIEWPROJ_0	0	//4 vectors for transform of world->clip space.

///Pixel shader based terrain shader - fastest method for the newest cards.
class TerrainShaderPixelShader : public W3DShaderInterface
{
public:
	IDirect3DPixelShader9*	m_dwBasePixelShader;	///<handle to terrain D3D pixel shader
	IDirect3DPixelShader9*	m_dwBaseNoise1PixelShader;	///<handle to terrain/single noise D3D pixel shader
	IDirect3DPixelShader9*	m_dwBaseNoise2PixelShader;	///<handle to terrain/double noise D3D pixel shader

	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual Int shutdown(void);			///<release resources used by shader
} terrainShaderPixelShader;

///Unit/building PBR shader (Phase 3+), compiled at runtime via D3DX
class W3DPBRShader : public W3DShaderInterface
{
public:
	IDirect3DPixelShader9*	m_dwPBRPixelShader;	    ///<unit PBR opaque pixel shader (4 lights)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShader;///<unit PBR alpha blend pixel shader
	IDirect3DPixelShader9*	m_dwPBRPixelShaderNT;   ///<unit PBR opaque, no PBR texture variant
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShaderNT;///<unit PBR alpha, no PBR texture variant
	// ps_3_0 base loop shader (Stage 5.2)
	IDirect3DPixelShader9*	m_dwPBRPixelShader_30;	    ///<unit PBR opaque ps_3_0 loop variant
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShader_30;///<unit PBR alpha ps_3_0 variant
	// Diffuse IBL variants (ps_3_0: texCUBE)
	IDirect3DPixelShader9*	m_dwPBRPixelShader_30_IBL;	    ///<unit PBR opaque + IBL (ps_3_0)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShader_30_IBL;///<unit PBR alpha + IBL (ps_3_0)
	CubeTextureClass*	m_envIrradianceMap;	    ///<irradiance CubeMap for diffuse IBL (or NULL)
	// Specular IBL variants (ps_3_0 only: texCUBElod + BRDF LUT)
	IDirect3DPixelShader9*	m_dwPBRPixelShader_30_IBLSpec;	   ///<unit PBR opaque + specular IBL (ps_3_0)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShader_30_IBLSpec;///<unit PBR alpha + specular IBL (ps_3_0)
	CubeTextureClass*	m_envPrefilteredMap;	    ///<pre-filtered CubeMap for specular IBL (s4, or NULL)
	TextureClass*		m_brdfLUT;		    ///<BRDF integration LUT 2D texture (s5, or NULL)
	// NT IBL variants (no PBR texture, with CubeMap IBL)
	IDirect3DPixelShader9*	m_dwPBRPixelShaderNT_IBL;	   ///<NT opaque + diffuse IBL (ps_2_0)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShaderNT_IBL; ///<NT alpha + diffuse IBL (ps_2_0)
	IDirect3DPixelShader9*	m_dwPBRPixelShaderNT_30_IBLSpec;	   ///<NT opaque + full IBL (ps_3_0)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShaderNT_30_IBLSpec; ///<NT alpha + full IBL (ps_3_0)
	// NT ps_3_0 no-IBL shaders (loop-based GGX without CubeMap sampling)
	IDirect3DPixelShader9*	m_dwPBRPixelShaderNT_30;	   ///<NT opaque ps_3_0 (no IBL)
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShaderNT_30; ///<NT alpha ps_3_0 (no IBL)
	// NT ps_3_0 diffuse IBL shaders (ps_3_0 loop GGX + texCUBE irradiance)
	IDirect3DPixelShader9*	m_dwPBRPixelShaderNT_30_IBL;	   ///<NT opaque ps_3_0 + diffuse IBL
	IDirect3DPixelShader9*	m_dwPBRAlphaPixelShaderNT_30_IBL; ///<NT alpha ps_3_0 + diffuse IBL
	IDirect3DVertexShader9*	m_vsPBRUnit;	///<pass-through vertex shader for PBR unit (vs_1_1)
	IDirect3DPixelShader9*	m_dwSunGlowShader;	///<sun glow overlay (RA3-style)
	Bool				m_sunGlowEnabled;	///<true after successful compile
	virtual Int set(Int pass);		///<setup shader for specified rendering pass
	virtual void reset(void);		///<restore W3D state after PBR
	virtual Int init(void);			///<compile HLSL and create shaders
	virtual Int shutdown(void);		///<release shader resources
} w3dPBRShader;

///List of PBR unit shader implementations in order of preference
W3DShaderInterface *PBRShaderList[]=
{
	&w3dPBRShader,
	NULL
};

///ps_2_0 PBR terrain shader - GGX specular highlight from sun direction
class TerrainShaderPBR : public W3DShaderInterface
{
public:
	IDirect3DPixelShader9*	m_dwPBRPixelShader;	        ///<ps_2_0 PBR pixel shader (base)
	IDirect3DPixelShader9*	m_dwPBRNoise1PixelShader;	///<ps_2_0 PBR + cloud (noise1)
	IDirect3DPixelShader9*	m_dwPBRNoise2PixelShader;	///<ps_2_0 PBR + lightmap (noise2)
	IDirect3DPixelShader9*	m_dwPBRNoise12PixelShader;	///<ps_2_0 PBR + cloud + lightmap
	IDirect3DVertexShader9* m_dwTerrainVS;			///<2026-09-09 RA3-faithful vs_3_0 terrain vertex shader (world pos + shadow UV)
	virtual Int set(Int pass);
	virtual void reset(void);
	virtual Int init(void);
	virtual Int shutdown(void);
} terrainShaderPBR;


///List of different terrain shader implementations in order of preference
W3DShaderInterface *TerrainShaderList[]=
{
	&terrainShaderPBR,
	&terrainShaderPixelShader,
	&terrainShader8Stage,
	&terrainShader2Stage,
	NULL
};

///List of different terrain shader implementations in order of preference
W3DShaderInterface *FlatTerrainShaderList[]=
{
	&flatTerrainShaderPixelShader,
	&flatTerrainShader2Stage,
	NULL
};

Int TerrainShader2Stage::init( void )
{
	//initialize settings for uv animated clouds
	m_xSlidePerSecond = -0.02f;	 
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_curTick = 0;
	m_curTick = WW3D::Get_Sync_Time();//::GetTickCount();
	m_xOffset = 0;
	m_yOffset = 0;

	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=2;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=3;

	return TRUE;
}

void TerrainShader2Stage::reset(void)
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}

void TerrainShader2Stage::updateNoise1(D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate)
{
	#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */

	D3DXMATRIX scale;

	D3DXMatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;

	D3DXMATRIX offset;

	Int delta = m_curTick;
	m_curTick = WW3D::Get_Sync_Time();//::GetTickCount();
	delta = m_curTick-delta;
	m_xOffset += m_xSlidePerSecond*delta/1000;
	m_yOffset += m_ySlidePerSecond*delta/1000;


	//m_xOffset += m_xSlidePerSecond*delta/500;
	//m_yOffset += m_ySlidePerSecond*delta/500;


	//m_yOffset = sinf( (float)m_curTick * 0.0001f );
	//m_xOffset = cosf( (float)m_curTick * 0.0001f );

	while (m_xOffset > 1) m_xOffset -= 1;
	while (m_yOffset > 1) m_yOffset -= 1;
	while (m_xOffset < -1) m_xOffset += 1;
	while (m_yOffset < -1) m_yOffset += 1;

	D3DXMatrixTranslation(&offset, m_xOffset, m_yOffset,0);
	*destMatrix *= offset;
}

void TerrainShader2Stage::updateNoise2(D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate)
{
			
	D3DXMATRIX scale;

	D3DXMatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;
}

Int TerrainShader2Stage::set(Int pass)																											  
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}

	switch (pass)
	{
		case 0:
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
			break;
		case 1:
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 1 );
			// Blend the result using the alpha. (came from diffuse mod texture)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
			// Disable stage 2.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			break;
		case 2:
			// Noise/cloud pass
			Matrix4x4 curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			//blend into frame buffer
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ZERO);

			
			D3DXMATRIX inv;
			float det;

			D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				//setup noise pass
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());

				updateNoise2(((D3DXMATRIX*)&curView),&inv);
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			} //ST_TERRAIN_BASE_NOISE12
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					updateNoise2(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, *((Matrix4x4*)&curView));
			}
			break;
	}

	return TRUE;
}

Int TerrainShader8Stage::init( void )
{	
	ChipsetType res;

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (terrainShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_TNT && res <= DC_GEFORCE2)
	{
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=1;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=2;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=2;
		W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader8Stage;
		W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=2;
		return TRUE;
	}

	return FALSE;
}

Int TerrainShader8Stage::set(Int pass)
{
	if (pass == 0)
	{
		//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
		DX8Wrapper::Apply_Render_State_Changes();

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

		if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		} else {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		}
		if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		} else {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		}
		
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_DIFFUSE | D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(2, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, 2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG2, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(3, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, 3);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG1, D3DTA_DIFFUSE | 0 | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(4, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_TEXCOORDINDEX, 4);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		DX8Wrapper::Set_DX8_Texture(5, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLOROP, D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_TEXCOORDINDEX, 5);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAOP,   D3DTOP_ADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(6, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLOROP, D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_TEXCOORDINDEX, 6);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

		DX8Wrapper::Set_DX8_Texture(7, NULL);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_TEXCOORDINDEX, 7);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	}
	else
	{	//setup cloud noise/pass
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		DX8Wrapper::Invalidate_Cached_Render_States();

		terrainShader2Stage.set(2);
	}
	return TRUE;
}

void TerrainShader8Stage::reset(void)
{
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLOROP, D3DTOP_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int TerrainShaderPixelShader::shutdown(void)
{
	if (m_dwBasePixelShader)
				m_dwBasePixelShader->Release();

	if (m_dwBaseNoise1PixelShader)
				m_dwBaseNoise1PixelShader->Release();

	if (m_dwBaseNoise2PixelShader)
				m_dwBaseNoise2PixelShader->Release();

	m_dwBasePixelShader=NULL;
	m_dwBaseNoise1PixelShader=NULL;
	m_dwBaseNoise2PixelShader=NULL;

	return TRUE;
}


// === TERRAIN PBR DIAGNOSTIC LOGGING ===
static Bool g_terrainDiagInit = FALSE;
static void TerrainDiag(const char *msg)
{
	FILE *f = fopen("E:\\terrain_diag.log", g_terrainDiagInit ? "a" : "w");
	if (f) {
		if (!g_terrainDiagInit) g_terrainDiagInit = TRUE;
		fprintf(f, "[%d] %s\n", timeGetTime(), msg);
		fclose(f);
	}
}
static void TerrainDiagI(const char *msg, int val)
{
	char buf[256];
	sprintf(buf, "%s = %d", msg, val);
	TerrainDiag(buf);
}
// === END TERRAIN DIAGNOSTIC ===

///Helper to compile a ps_2_0 shader from source string.
static HRESULT compilePBRShader(const char* source, IDirect3DPixelShader9** ppShader, const char* tag,
	const char* profile = "ps_2_a")
{
	ID3DXBuffer* compiled = NULL;
	ID3DXBuffer* errors = NULL;
	DEBUG_LOG(("CP8_TERPBR: compiling %s\n", tag));
	TerrainDiag(tag);
	HRESULT hr = D3DXCompileShader(source, (UINT)strlen(source),
		NULL, NULL, "main", profile, 0, &compiled, &errors, NULL);
	DEBUG_LOG(("CP8_TERPBR: %s D3DXCompileShader hr = %d\n", tag, (int)hr));
	TerrainDiagI(tag, (int)hr);
	// 2026-09-08: dedicated compile log (E:\pbr_compile.log, APPEND) —
	// terrain_diag.log resets ("w") on every map load and wipes the compile
	// results; without this a terrain PBR init failure silently falls back to
	// ST_TERRAIN_BASE and NO receive code ever runs.
	{
		FILE *clf = fopen("E:\\pbr_compile.log", "a");
		if (clf) {
			fprintf(clf, "[%d] %s (profile %s) compile hr=0x%08x\n",
				(int)timeGetTime(), tag, profile, (unsigned)hr);
			if (errors) {
				const char* eText = (const char*)errors->GetBufferPointer();
				if (eText) fprintf(clf, "    ERR: %s\n", eText);
			}
			fclose(clf);
		}
	}
	if (errors) {
		const char* errText = (const char*)errors->GetBufferPointer();
		if (errText) {
			DEBUG_LOG(("CP8_TERPBR: %s error: %s\n", tag, errText));
			TerrainDiag(errText);
		}
		errors->Release();
	}
	if (SUCCEEDED(hr) && compiled) {
		hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(
			(const DWORD*)compiled->GetBufferPointer(), ppShader);
		compiled->Release();
		DEBUG_LOG(("CP8_TERPBR: %s CreatePixelShader hr = %d\n", tag, (int)hr));
		TerrainDiagI(tag, (int)hr);
	}
	return hr;
}

Int TerrainShaderPBR::init( void )
{
	D3DCAPS8 caps;
	memset(&caps, 0, sizeof(caps));
	if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetDeviceCaps(&caps)) ||
		caps.PixelShaderVersion < D3DPS_VERSION(2,0))
	{
		// ps_2_0 not available, fall through to pixel shader path
		return terrainShaderPixelShader.init();
	}

	// Initialize 2Stage shader so updateNoise1/updateNoise2 helpers are available
	terrainShader2Stage.init();

	m_dwPBRPixelShader = NULL;
	m_dwPBRNoise1PixelShader = NULL;
	m_dwPBRNoise2PixelShader = NULL;
	m_dwPBRNoise12PixelShader = NULL;
	m_dwTerrainVS = NULL;
	// 2026-09-09 RA3-FAITHFUL TERRAIN VS (vs_3_0): the terrain has NO vertex
	// shader today (FVF fixed-function), which is why every coordinate feed we
	// tried (TSS stage6/7) was at the mercy of the driver's fixed-function black
	// box. This VS does exactly what RA3 Terrain.fx does in its VS:
	//   POSITION   = pos * ViewProj       (terrain verts ARE world coords)
	//   TEXCOORD6  = pos                  (WorldPosition)
	//   TEXCOORD7  = pos * ShadowUVZ      (RA3 ShadowMapTexCoord)
	// UVs/colors/normals pass through. MUST pair with ps_3_0 (D3D9 rule).
	{
		const char* vsSrc =
		"float4x4 ViewProj  : register(c0);\n"
		"float4x4 ShadowUVZ : register(c4);\n"
		"struct VSOut {\n"
		"    float4 Position  : POSITION;\n"
		"    float4 Diffuse   : COLOR0;\n"
		"    float2 UV0       : TEXCOORD0;\n"
		"    float2 UV1       : TEXCOORD1;\n"
		"    float3 WorldPos  : TEXCOORD6;\n"
		"    float4 ShadowUVZ : TEXCOORD7;\n"
		"};\n"
		"VSOut main(float3 pos : POSITION,\n"
		"           float4 diffuse : COLOR0,\n"
		"           float2 uv0 : TEXCOORD0, float2 uv1 : TEXCOORD1) {\n"
		"    VSOut o;\n"
		"    o.Position  = mul(float4(pos, 1.0), ViewProj);\n"
		"    o.Diffuse   = diffuse;\n"
		"    o.UV0       = uv0;\n"
		"    o.UV1       = uv1;\n"
		"    o.WorldPos  = pos;\n"
		"    o.ShadowUVZ = mul(float4(pos, 1.0), ShadowUVZ);\n"
		"    return o;\n"
		"}\n"
		;
		ID3DXBuffer* vsCompiled = NULL; ID3DXBuffer* vsErrors = NULL;
		HRESULT vsHr = D3DXCompileShader(vsSrc, (UINT)strlen(vsSrc), NULL, NULL, "main", "vs_3_0", 0, &vsCompiled, &vsErrors, NULL);
		{ FILE* vf = fopen("E:\\pbr_compile.log", "a"); if (vf) {
			fprintf(vf, "[%d] terrainVS (vs_3_0) compile hr=0x%08x\n", (int)timeGetTime(), (unsigned)vsHr);
			if (vsErrors) fprintf(vf, "    ERR: %s\n", (const char*)vsErrors->GetBufferPointer());
			fclose(vf); } }
		if (SUCCEEDED(vsHr) && vsCompiled) {
			DX8Wrapper::_Get_D3D_Device8()->CreateVertexShader((const DWORD*)vsCompiled->GetBufferPointer(), &m_dwTerrainVS);
			vsCompiled->Release();
		}
		if (vsErrors) vsErrors->Release();
	}


	// --- Base PBR shader: s0 + sun (GGX) ---
	{
		const char* src =
			"sampler s0 : register(s0);\n"
			"sampler s1 : register(s1);\n"
			"sampler s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float4 c2 : register(c2);\n"
			"float3 sunDirection : register(c0);\n"
			"float3 sunColor : register(c1);\n"
			"float4x4 shadowVP : register(c3);\n"
			"float4 shadowParams : register(c7);\n"	// x,y = shadow texel, z = depth bias, w = receive enable
			"float4 shadowDbg : register(c8);\n"
			"float4x4 PSInvView : register(c9);\n"	// 2026-09-08: rebuild world from camera-space TEXCOORD6	// x = PBRDebugMode: 22 = raw map depth viz, 23 = raw compare viz
			"float terrainShadow(float3 shadowUVZ)\n"
			"{\n"
			"    // 2026-09-09 RA3-FAITHFUL: shadow UV+depth arrive pre-computed per-vertex\n"
			"    // via TSS stage7 (object-space position x [sunVP*bias]) - the PS does\n"
			"    // ZERO matrix math, exactly like RA3 Terrain.fx (VS-computed TEXCOORD6).\n"
			"    float2 suv = shadowUVZ.xy;\n"
			"    float sd = shadowUVZ.z - shadowParams.z;\n"
			
			"    // 2026-09-10 v3: 9-tap spatial kernel @2 texels (averages away the\n"
			"    // diagonal texel stair-step the 1x1 bilinear could not) + the RA3\n"
			"    // CONTINUOUS transition band per tap (smooth depth edge, K=256 =\n"
			"    // ~15-unit band). Spatial kernel kills jaggies, the band softens.\n"
			"    float2 ts = float2(shadowParams.x, shadowParams.y) * 2.0;\n"
			"    float f = 0.0;\n"
			"    f += saturate((sd - tex2D(s4, suv).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    float sun = 1.0 - f * (1.0 / 9.0);\n"
			"    float lit = 0.3 + 0.7 * sun;\n"
			"    float inB = (suv.x > 0.001 && suv.x < 0.999 && suv.y > 0.001 && suv.y < 0.999) ? 1.0 : 0.0;\n"
			"    float base = lerp(1.0, lit, inB * shadowParams.w);\n"
			"    float mViz = step(0.5, shadowParams.w);\n"
			"    float m22 = step(21.5, shadowDbg.x) * (1.0 - step(22.5, shadowDbg.x)) * mViz;\n"
			"    float m23 = step(22.5, shadowDbg.x) * (1.0 - step(23.5, shadowDbg.x)) * mViz;\n"
			"    float m24 = step(23.5, shadowDbg.x) * mViz;\n"
			"    float m25 = step(24.5, shadowDbg.x) * mViz;\n"
			"    float m26 = step(25.5, shadowDbg.x) * mViz;\n"
			"    float m27 = mViz;  // 2026-09-08 HARDCODED: bypass the shadowDbg(c8) chain entirely - c7.w is log-proven =1\n"
			"    return base;  // 2026-09-09 FINAL: XCHECK numerically validated the matrix (uv/z in correct domain) - REAL shadow\n"
			"}\n"
			"float4 main(float2 tex0 : TEXCOORD0, float2 tex1 : TEXCOORD1, float4 diffuse : COLOR0, float3 worldPos : TEXCOORD6, float3 shadowUVZ : TEXCOORD7) : COLOR\n"
			"{\n"
			"    float4 base0 = tex2D(s0, tex0);\n"
			"    float4 base1 = tex2D(s1, tex1);\n"
			"    float3 terrainColor = lerp(base0.rgb, base1.rgb, diffuse.a);\n"
			"    float detail = tex2D(s4, tex0 * 8.0).r;\n"
			"    terrainColor *= (1.0 + (detail - 0.5) * 0.15);\n"
			"    float3 dp1 = ddx(worldPos);\n"
			"    float3 dp2 = ddy(worldPos);\n"
			"    float2 duv1 = ddx(tex0);\n"
			"    float2 duv2 = ddy(tex0);\n"
			"    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);\n"
			"    float3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);\n"
			"    float3 geoN = normalize(cross(B, T));\n"
			"    float2 p = tex2D(s5, tex0).rg * 2.0 - 1.0;\n"
			"    float3 nm = normalize(float3(p, 1.0 - abs(p.x) - abs(p.y)));\n"
			"    float conc = c2.x * (1.0 - diffuse.a);\n"
			"    float3 N = normalize(geoN + (nm.x * T * c2.z + nm.y * B * c2.w) * conc * geoN.z);\n"
			"    float3 L = normalize(sunDirection);\n"
			"    float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + N);\n"
			"    float NdotH = saturate(dot(N, H));\n"
			"    float VdotH = NdotH;\n"
			"    float roughness = c2.y;\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"    float D = a2 / (3.14159 * d * d);\n"
			"    float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"    float f = 1.0 - VdotH; float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"    float3 specular = D * G_L * (float3(0.04,0.04,0.04) + (1.0 - 0.04) * f5);\n"
			"    float3 result = terrainColor * (0.4 + 0.6 * NdotL);\n"
			"    result += sunColor * specular * 0.25;\n"
			"    result *= terrainShadow(shadowUVZ);\n"
			"    return float4(result, base0.a);\n"
			"}\n";
		// 2026-09-08: ps_3_0 REQUIRED for the shadow-map sample (the s7/ps_2_a
		// combination read 0 - viz mode 22 proved all-black terrain).
		if (FAILED(compilePBRShader(src, &m_dwPBRPixelShader, "terrain_pbr_nm")))
			return terrainShaderPixelShader.init();
	}

	// 2026-09-10 W3D-MESH CAST DEPTH PS: dx8renderer.cpp's shadow-pass branch
	// binds this on W3D meshes (vehicles/infantry). TSS stage7 generates
	// t7 = (u, v, z) from the view-space position x [Proj*bias] (the SAME
	// fixed-pipeline trick as the terrain receive), and this PS just outputs
	// t7.z (sun NDC depth, same domain the W3X ShadowDepth cast writes) as the
	// R-channel color. Without it W3D meshes only wrote the D24X8 and the
	// COLOR RT (what the terrain PCF samples) never saw them => no vehicle
	// ground shadows.
	{
		const char* dsrc =
			"float4 main(float4 t7 : TEXCOORD7) : COLOR\n"
			"{\n"
			"    return float4(t7.z, 0.0, 0.0, 1.0);\n"
			"}\n";
		IDirect3DPixelShader9 *depthPS = NULL;
		if (SUCCEEDED(compilePBRShader(dsrc, &depthPS, "w3d_mesh_cast_depth", "ps_2_0"))) {
			if (g_w3dShadowDepthPS) g_w3dShadowDepthPS->Release();
			g_w3dShadowDepthPS = depthPS;
		}
	}

	W3DShaders[W3DShaderManager::ST_TERRAIN_PBR] = &terrainShaderPBR;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR] = 1;

	// --- PBR + cloud (NOISE1): s0 + s2 cloud (GGX) ---
	{
		const char* src =
			"sampler s0 : register(s0);\n"
			"sampler s1 : register(s1);\n"
			"sampler s2 : register(s2);\n"
			"sampler s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float4 c2 : register(c2);\n"
			"float3 sunDirection : register(c0);\n"
			"float3 sunColor : register(c1);\n"
			"float4x4 shadowVP : register(c3);\n"
			"float4 shadowParams : register(c7);\n"	// x,y = shadow texel, z = depth bias, w = receive enable
			"float4 shadowDbg : register(c8);\n"
			"float4x4 PSInvView : register(c9);\n"	// 2026-09-08: rebuild world from camera-space TEXCOORD6	// x = PBRDebugMode: 22 = raw map depth viz, 23 = raw compare viz
			"float terrainShadow(float3 shadowUVZ)\n"
			"{\n"
			"    // 2026-09-09 RA3-FAITHFUL: shadow UV+depth arrive pre-computed per-vertex\n"
			"    // via TSS stage7 (object-space position x [sunVP*bias]) - the PS does\n"
			"    // ZERO matrix math, exactly like RA3 Terrain.fx (VS-computed TEXCOORD6).\n"
			"    float2 suv = shadowUVZ.xy;\n"
			"    float sd = shadowUVZ.z - shadowParams.z;\n"
			
			"    // 2026-09-10 v3: 9-tap spatial kernel @2 texels (averages away the\n"
			"    // diagonal texel stair-step the 1x1 bilinear could not) + the RA3\n"
			"    // CONTINUOUS transition band per tap (smooth depth edge, K=256 =\n"
			"    // ~15-unit band). Spatial kernel kills jaggies, the band softens.\n"
			"    float2 ts = float2(shadowParams.x, shadowParams.y) * 2.0;\n"
			"    float f = 0.0;\n"
			"    f += saturate((sd - tex2D(s4, suv).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    float sun = 1.0 - f * (1.0 / 9.0);\n"
			"    float lit = 0.3 + 0.7 * sun;\n"
			"    float inB = (suv.x > 0.001 && suv.x < 0.999 && suv.y > 0.001 && suv.y < 0.999) ? 1.0 : 0.0;\n"
			"    float base = lerp(1.0, lit, inB * shadowParams.w);\n"
			"    float mViz = step(0.5, shadowParams.w);\n"
			"    float m22 = step(21.5, shadowDbg.x) * (1.0 - step(22.5, shadowDbg.x)) * mViz;\n"
			"    float m23 = step(22.5, shadowDbg.x) * (1.0 - step(23.5, shadowDbg.x)) * mViz;\n"
			"    float m24 = step(23.5, shadowDbg.x) * mViz;\n"
			"    float m25 = step(24.5, shadowDbg.x) * mViz;\n"
			"    float m26 = step(25.5, shadowDbg.x) * mViz;\n"
			"    float m27 = mViz;  // 2026-09-08 HARDCODED: bypass the shadowDbg(c8) chain entirely - c7.w is log-proven =1\n"
			"    return base;  // 2026-09-09 FINAL: XCHECK numerically validated the matrix (uv/z in correct domain) - REAL shadow\n"
			"}\n"
			"float4 main(float2 tex0 : TEXCOORD0, float2 tex1 : TEXCOORD1, float2 tex2 : TEXCOORD2, float4 diffuse : COLOR0, float3 worldPos : TEXCOORD6, float3 shadowUVZ : TEXCOORD7) : COLOR\n"
			"{\n"
			"    float4 base0 = tex2D(s0, tex0);\n"
			"    float4 base1 = tex2D(s1, tex1);\n"
			"    float3 terrainColor = lerp(base0.rgb, base1.rgb, diffuse.a);\n"
			"    float detail = tex2D(s4, tex0 * 8.0).r;\n"
			"    terrainColor *= (1.0 + (detail - 0.5) * 0.15);\n"
			"    float4 cloudTex = tex2D(s2, tex2);\n"
			"    float3 dp1 = ddx(worldPos);\n"
			"    float3 dp2 = ddy(worldPos);\n"
			"    float2 duv1 = ddx(tex0);\n"
			"    float2 duv2 = ddy(tex0);\n"
			"    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);\n"
			"    float3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);\n"
			"    float3 geoN = normalize(cross(B, T));\n"
			"    float2 p = tex2D(s5, tex0).rg * 2.0 - 1.0;\n"
			"    float3 nm = normalize(float3(p, 1.0 - abs(p.x) - abs(p.y)));\n"
			"    float conc = c2.x * (1.0 - diffuse.a);\n"
			"    float3 N = normalize(geoN + (nm.x * T * c2.z + nm.y * B * c2.w) * conc * geoN.z);\n"
			"    float3 L = normalize(sunDirection);\n"
			"    float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + N);\n"
			"    float NdotH = saturate(dot(N, H));\n"
			"    float VdotH = NdotH;\n"
			"    float roughness = c2.y;\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"    float D = a2 / (3.14159 * d * d);\n"
			"    float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"    float f = 1.0 - VdotH; float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"    float3 specular = D * G_L * (float3(0.04,0.04,0.04) + (1.0 - 0.04) * f5);\n"
			"    float3 lit = terrainColor * (0.4 + 0.6 * NdotL);\n"
			"    lit += sunColor * specular * 0.25;\n"
			"    lit *= (1.0 + cloudTex.rgb * 0.3);\n"
			"    lit *= terrainShadow(shadowUVZ);\n"
			"    return float4(lit, base0.a);\n"
			"}\n";
		if (SUCCEEDED(compilePBRShader(src, &m_dwPBRNoise1PixelShader, "terrain_pbr_nm_noise1"))) {
			W3DShaders[W3DShaderManager::ST_TERRAIN_PBR_NOISE1] = &terrainShaderPBR;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE1] = 1;
		}
	}

	// --- PBR + lightmap (NOISE2): s0 + s2 lightmap (GGX) ---
	{
		const char* src =
			"sampler s0 : register(s0);\n"
			"sampler s1 : register(s1);\n"
			"sampler s2 : register(s2);\n"
			"sampler s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float4 c2 : register(c2);\n"
			"float3 sunDirection : register(c0);\n"
			"float3 sunColor : register(c1);\n"
			"float4x4 shadowVP : register(c3);\n"
			"float4 shadowParams : register(c7);\n"	// x,y = shadow texel, z = depth bias, w = receive enable
			"float4 shadowDbg : register(c8);\n"
			"float4x4 PSInvView : register(c9);\n"	// 2026-09-08: rebuild world from camera-space TEXCOORD6	// x = PBRDebugMode: 22 = raw map depth viz, 23 = raw compare viz
			"float terrainShadow(float3 shadowUVZ)\n"
			"{\n"
			"    // 2026-09-09 RA3-FAITHFUL: shadow UV+depth arrive pre-computed per-vertex\n"
			"    // via TSS stage7 (object-space position x [sunVP*bias]) - the PS does\n"
			"    // ZERO matrix math, exactly like RA3 Terrain.fx (VS-computed TEXCOORD6).\n"
			"    float2 suv = shadowUVZ.xy;\n"
			"    float sd = shadowUVZ.z - shadowParams.z;\n"
			
			"    // 2026-09-10 v3: 9-tap spatial kernel @2 texels (averages away the\n"
			"    // diagonal texel stair-step the 1x1 bilinear could not) + the RA3\n"
			"    // CONTINUOUS transition band per tap (smooth depth edge, K=256 =\n"
			"    // ~15-unit band). Spatial kernel kills jaggies, the band softens.\n"
			"    float2 ts = float2(shadowParams.x, shadowParams.y) * 2.0;\n"
			"    float f = 0.0;\n"
			"    f += saturate((sd - tex2D(s4, suv).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    float sun = 1.0 - f * (1.0 / 9.0);\n"
			"    float lit = 0.3 + 0.7 * sun;\n"
			"    float inB = (suv.x > 0.001 && suv.x < 0.999 && suv.y > 0.001 && suv.y < 0.999) ? 1.0 : 0.0;\n"
			"    float base = lerp(1.0, lit, inB * shadowParams.w);\n"
			"    float mViz = step(0.5, shadowParams.w);\n"
			"    float m22 = step(21.5, shadowDbg.x) * (1.0 - step(22.5, shadowDbg.x)) * mViz;\n"
			"    float m23 = step(22.5, shadowDbg.x) * (1.0 - step(23.5, shadowDbg.x)) * mViz;\n"
			"    float m24 = step(23.5, shadowDbg.x) * mViz;\n"
			"    float m25 = step(24.5, shadowDbg.x) * mViz;\n"
			"    float m26 = step(25.5, shadowDbg.x) * mViz;\n"
			"    float m27 = mViz;  // 2026-09-08 HARDCODED: bypass the shadowDbg(c8) chain entirely - c7.w is log-proven =1\n"
			"    return base;  // 2026-09-09 FINAL: XCHECK numerically validated the matrix (uv/z in correct domain) - REAL shadow\n"
			"}\n"
			"float4 main(float2 tex0 : TEXCOORD0, float2 tex1 : TEXCOORD1, float2 tex2 : TEXCOORD2, float4 diffuse : COLOR0, float3 worldPos : TEXCOORD6, float3 shadowUVZ : TEXCOORD7) : COLOR\n"
			"{\n"
			"    float4 base0 = tex2D(s0, tex0);\n"
			"    float4 base1 = tex2D(s1, tex1);\n"
			"    float3 terrainColor = lerp(base0.rgb, base1.rgb, diffuse.a);\n"
			"    float detail = tex2D(s4, tex0 * 8.0).r;\n"
			"    terrainColor *= (1.0 + (detail - 0.5) * 0.15);\n"
			"    float4 lightmapTex = tex2D(s2, tex2);\n"
			"    float3 dp1 = ddx(worldPos);\n"
			"    float3 dp2 = ddy(worldPos);\n"
			"    float2 duv1 = ddx(tex0);\n"
			"    float2 duv2 = ddy(tex0);\n"
			"    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);\n"
			"    float3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);\n"
			"    float3 geoN = normalize(cross(B, T));\n"
			"    float2 p = tex2D(s5, tex0).rg * 2.0 - 1.0;\n"
			"    float3 nm = normalize(float3(p, 1.0 - abs(p.x) - abs(p.y)));\n"
			"    float conc = c2.x * (1.0 - diffuse.a);\n"
			"    float3 N = normalize(geoN + (nm.x * T * c2.z + nm.y * B * c2.w) * conc * geoN.z);\n"
			"    float3 L = normalize(sunDirection);\n"
			"    float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + N);\n"
			"    float NdotH = saturate(dot(N, H));\n"
			"    float VdotH = NdotH;\n"
			"    float roughness = c2.y;\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"    float D = a2 / (3.14159 * d * d);\n"
			"    float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"    float f = 1.0 - VdotH; float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"    float3 specular = D * G_L * (float3(0.04,0.04,0.04) + (1.0 - 0.04) * f5);\n"
			"    float3 lit = terrainColor * (0.4 + 0.6 * NdotL);\n"
			"    lit += sunColor * specular * 0.25;\n"
			"    lit *= lightmapTex.rgb;\n"
			"    lit *= terrainShadow(shadowUVZ);\n"
			"    return float4(lit, base0.a);\n"
			"}\n";
		if (SUCCEEDED(compilePBRShader(src, &m_dwPBRNoise2PixelShader, "terrain_pbr_nm_noise2"))) {
			W3DShaders[W3DShaderManager::ST_TERRAIN_PBR_NOISE2] = &terrainShaderPBR;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE2] = 1;
		}
	}

	// --- PBR + cloud + lightmap (NOISE12): s0 + s2 + s3 (GGX) ---
	{
		const char* src =
			"sampler s0 : register(s0);\n"
			"sampler s1 : register(s1);\n"
			"sampler s2 : register(s2);\n"
			"sampler s3 : register(s3);\n"
			"sampler s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float4 c2 : register(c2);\n"
			"float3 sunDirection : register(c0);\n"
			"float3 sunColor : register(c1);\n"
			"float4x4 shadowVP : register(c3);\n"
			"float4 shadowParams : register(c7);\n"	// x,y = shadow texel, z = depth bias, w = receive enable
			"float4 shadowDbg : register(c8);\n"
			"float4x4 PSInvView : register(c9);\n"	// 2026-09-08: rebuild world from camera-space TEXCOORD6	// x = PBRDebugMode: 22 = raw map depth viz, 23 = raw compare viz
			"float terrainShadow(float3 shadowUVZ)\n"
			"{\n"
			"    // 2026-09-09 RA3-FAITHFUL: shadow UV+depth arrive pre-computed per-vertex\n"
			"    // via TSS stage7 (object-space position x [sunVP*bias]) - the PS does\n"
			"    // ZERO matrix math, exactly like RA3 Terrain.fx (VS-computed TEXCOORD6).\n"
			"    float2 suv = shadowUVZ.xy;\n"
			"    float sd = shadowUVZ.z - shadowParams.z;\n"
			
			"    // 2026-09-10 v3: 9-tap spatial kernel @2 texels (averages away the\n"
			"    // diagonal texel stair-step the 1x1 bilinear could not) + the RA3\n"
			"    // CONTINUOUS transition band per tap (smooth depth edge, K=256 =\n"
			"    // ~15-unit band). Spatial kernel kills jaggies, the band softens.\n"
			"    float2 ts = float2(shadowParams.x, shadowParams.y) * 2.0;\n"
			"    float f = 0.0;\n"
			"    f += saturate((sd - tex2D(s4, suv).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    float sun = 1.0 - f * (1.0 / 9.0);\n"
			"    float lit = 0.3 + 0.7 * sun;\n"
			"    float inB = (suv.x > 0.001 && suv.x < 0.999 && suv.y > 0.001 && suv.y < 0.999) ? 1.0 : 0.0;\n"
			"    float base = lerp(1.0, lit, inB * shadowParams.w);\n"
			"    float mViz = step(0.5, shadowParams.w);\n"
			"    float m22 = step(21.5, shadowDbg.x) * (1.0 - step(22.5, shadowDbg.x)) * mViz;\n"
			"    float m23 = step(22.5, shadowDbg.x) * (1.0 - step(23.5, shadowDbg.x)) * mViz;\n"
			"    float m24 = step(23.5, shadowDbg.x) * mViz;\n"
			"    float m25 = step(24.5, shadowDbg.x) * mViz;\n"
			"    float m26 = step(25.5, shadowDbg.x) * mViz;\n"
			"    float m27 = mViz;  // 2026-09-08 HARDCODED: bypass the shadowDbg(c8) chain entirely - c7.w is log-proven =1\n"
			"    return base;  // 2026-09-09 FINAL: XCHECK numerically validated the matrix (uv/z in correct domain) - REAL shadow\n"
			"}\n"
			"float4 main(float2 tex0 : TEXCOORD0, float2 tex1 : TEXCOORD1, float2 tex2 : TEXCOORD2, float2 tex3 : TEXCOORD3, float4 diffuse : COLOR0, float3 worldPos : TEXCOORD6, float3 shadowUVZ : TEXCOORD7) : COLOR\n"
			"{\n"
			"    float4 base0 = tex2D(s0, tex0);\n"
			"    float4 base1 = tex2D(s1, tex1);\n"
			"    float3 terrainColor = lerp(base0.rgb, base1.rgb, diffuse.a);\n"
			"    float detail = tex2D(s4, tex0 * 8.0).r;\n"
			"    terrainColor *= (1.0 + (detail - 0.5) * 0.15);\n"
			"    float4 cloudTex = tex2D(s2, tex2);\n"
			"    float4 lightmapTex = tex2D(s3, tex3);\n"
			"    float3 dp1 = ddx(worldPos);\n"
			"    float3 dp2 = ddy(worldPos);\n"
			"    float2 duv1 = ddx(tex0);\n"
			"    float2 duv2 = ddy(tex0);\n"
			"    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);\n"
			"    float3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);\n"
			"    float3 geoN = normalize(cross(B, T));\n"
			"    float2 p = tex2D(s5, tex0).rg * 2.0 - 1.0;\n"
			"    float3 nm = normalize(float3(p, 1.0 - abs(p.x) - abs(p.y)));\n"
			"    float conc = c2.x * (1.0 - diffuse.a);\n"
			"    float3 N = normalize(geoN + (nm.x * T * c2.z + nm.y * B * c2.w) * conc * geoN.z);\n"
			"    float3 L = normalize(sunDirection);\n"
			"    float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + N);\n"
			"    float NdotH = saturate(dot(N, H));\n"
			"    float VdotH = NdotH;\n"
			"    float roughness = c2.y;\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"    float D = a2 / (3.14159 * d * d);\n"
			"    float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"    float f = 1.0 - VdotH; float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"    float3 specular = D * G_L * (float3(0.04,0.04,0.04) + (1.0 - 0.04) * f5);\n"
			"    float3 lit = terrainColor * (0.4 + 0.6 * NdotL);\n"
			"    lit += sunColor * specular * 0.25;\n"
			"    lit *= (1.0 + cloudTex.rgb * 0.3) * lightmapTex.rgb;\n"
			"    lit *= terrainShadow(shadowUVZ);\n"
			"    return float4(lit, base0.a);\n"
			"}\n";
		if (SUCCEEDED(compilePBRShader(src, &m_dwPBRNoise12PixelShader, "terrain_pbr_nm_noise12"))) {
			W3DShaders[W3DShaderManager::ST_TERRAIN_PBR_NOISE12] = &terrainShaderPBR;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE12] = 1;
		}
	}

		// DIAG: log which PBR variants are registered
		{
			FILE *f = fopen("E:\\terrain_diag.log", "a");
			if (f) {
				fprintf(f, "[%d] PBR_INIT: base=%d noise1=%d noise2=%d noise12=%d\n",
					timeGetTime(),
					W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR],
					W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE1],
					W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE2],
					W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_PBR_NOISE12]);
				fclose(f);
			}
		}
	return TRUE;
}

Int TerrainShaderPBR::set(Int pass)
{
	W3DShaderManager::ShaderTypes curShader = W3DShaderManager::getCurrentShader();

	// Fall back to non-PBR pixel shader for unrecognized shader types
	if (curShader < W3DShaderManager::ST_TERRAIN_PBR || curShader > W3DShaderManager::ST_TERRAIN_PBR_NOISE12) {
		return terrainShaderPixelShader.set(pass);
	}

	DX8Wrapper::Apply_Render_State_Changes();

	// Base texture s0 with vertex UV set 0
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		if (TheGlobalData->m_trilinearTerrainTex) {
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAXANISOTROPY, 4);
		}
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}
	// Stage 1: detail texture blend with TEXCOORD1
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 1);
	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		if (TheGlobalData->m_trilinearTerrainTex) {
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAXANISOTROPY, 4);
		}
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}


	// Compute view + inverse view once. Stage 6 uses the inverse to rebuild
	// world position in the pixel shader (derivative TBN); stages 2/3 reuse
	// the view/inverse for the noise projection transforms.
	Matrix4x4 curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
	D3DXMATRIX inv;
	float det;
	D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

	// Stage 6: camera-space position -> world position (inverse view transform).
	// Feeds TEXCOORD6 in the pixel shader so derivative TBN recovers
	// tangent/bitangent per-pixel without any vertex tangent data.
	DX8Wrapper::Set_DX8_Texture_Stage_State(6, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(6, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
	// Raw D3D9 call: _Set_DX8_Transform asserts transform<=D3DTS_WORLD, and
	// stage 6 must always run (even the base variant) for the TBN world position.
	DX8Wrapper::_Get_D3D_Device8()->SetTransform(D3DTS_TEXTURE6, (D3DMATRIX*)&inv);
	// Bind a dummy texture so the rasterizer always processes this stage.
	if (W3DShaderManager::getShaderTexture(0)) {
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(6, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
	}

	// Setup noise texture stages for variants that need them
	if (curShader >= W3DShaderManager::ST_TERRAIN_PBR_NOISE1) {
		// Stage 2: camera-space projected noise input
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

		if (curShader == W3DShaderManager::ST_TERRAIN_PBR_NOISE1 || curShader == W3DShaderManager::ST_TERRAIN_PBR_NOISE12) {
			// Cloud texture in stage 2
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
			terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView), &inv);
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		} else {
			// Lightmap in stage 2
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView), &inv);
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		}

		if (curShader == W3DShaderManager::ST_TERRAIN_PBR_NOISE12) {
			// Both cloud and lightmap: stage 3 for lightmap
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView), &inv);
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE3, curView);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		}
	}

		// Stage 4: PBR detail texture (procedural noise, micro-detail for terrain).
		if (W3DShaderManager::getShaderTexture(4)) {
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(4,
				W3DShaderManager::getShaderTexture(4)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_TEXCOORDINDEX, 0);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		}

		// Stage 5: terrain normal map atlas (PBR bump). Shares UV set 0 with base.
		float normalWeight = 0.0f;
		if (W3DShaderManager::getShaderTexture(5)) {
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(5,
				W3DShaderManager::getShaderTexture(5)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_TEXCOORDINDEX, 0);
			DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			normalWeight = 1.0f;
		}
		// c2 = { normalWeight, terrainRoughness, bumpSignX, bumpSignY }
		//   x: detail-normal blend strength (1.0 when a normal atlas is bound, else 0.0 = geo only).
		//   y: GGX roughness. Slightly low (0.5) so the bump reads clearly; raise toward
		//      1.0 for a matte look.
		//   z,w: per-axis bump sign (z scales T/X, w scales B/Y).
		//      Root cause (2026-08-16, fixed): geoN was normalize(cross(T,B)), but the engine's
		//      terrain maps V ∝ -Y (WorldHeightMap getUVForTileIndex: corner (x,y+1) gets the
		//      SMALLER V). Reference lighting getStaticDiffuse uses l2r(+X edge) x n2f(+Y edge)
		//      for the correct up-normal; cross(T,B) = cross(+X,-Y) = -up, so the whole terrain
		//      (not just the bump) read inverted. Fixed to normalize(cross(B,T)) = up in all 4
		//      PBR variants. The atlas encoding is verified convex (nm.x>0 toward +U, nm.y>0
		//      toward +V/B). Bump signs: see s_terrainBumpSignX/Y below (currently -1,-1).
		static float s_terrainRoughness = 0.5f;
		// Bump signs. Empirical (2026-08-17, normal-viz): blended N tilted OPPOSITE to geoN on
		// slopes -> the screen-derivative tangent frame has det<0 (geoN=cross(B,T) is
		// det-insensitive but the bump nx*T+ny*B is det-sensitive), so the whole bump was
		// mirrored. Fixed by negating both axes.
		static float s_terrainBumpSignX = -1.0f;
		static float s_terrainBumpSignY = -1.0f;
		float nmWeight[4] = { normalWeight, s_terrainRoughness, s_terrainBumpSignX, s_terrainBumpSignY };
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(2, nmWeight, 1);
		// 2026-09-08 DEVICE TRUTH PROBE (throttled): read back what the device
		// ACTUALLY has on s1/s4/s5 at set() ENTRY - this reflects what the LAST
		// frame's terrain draw left bound (post material-apply, post
		// DrawPrimitive). Compare against what we bind below to see who swaps
		// the stage between our bind and the primitive.
		{
			static int s_devProbeN = 0;
			if ((s_devProbeN++ % 300) == 0) {
				IDirect3DDevice9 *d9p = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
				IDirect3DBaseTexture9 *t1 = NULL, *t4 = NULL, *t5 = NULL;
				d9p->GetTexture(1, &t1); d9p->GetTexture(4, &t4); d9p->GetTexture(5, &t5);
				FILE *pf = fopen("E:\\pbr_compile.log", "a");
				if (pf) {
					fprintf(pf, "[%d] TER-DEVENTRY: s1=%p s4=%p s5=%p (0=NULL!)\n",
						(int)timeGetTime(), (void*)t1, (void*)t4, (void*)t5);
					fclose(pf);
				}
				if (t1) t1->Release();
				if (t4) t4->Release();
				if (t5) t5->Release();
			}
		}
		// Texture shadow receive (forward pass, 2026-09-06): bind the R32F
		// shadow-map copy at s7 and upload the sun view-proj (c3-c6) plus
		// params (c7: texel.xy, depth bias, receive-enable). endShadowMapPass
		// StretchRects the copy before the forward pass draws, so the sampler
		// is fresh every frame. With shadow disabled, w=0 lerps the receive
		// to 1.0 in the PS.
			{
				static bool s_terReceiveEnable = true;	// A/B re-enabled after the viz-mask fix
				// 2026-09-08: gate on the CPU copy being READY - until the first
				// RTSTATS readback fills it, s1 holds the native detail texture
				// and sampling it as depth would read as full shadow.
				bool shadowReceive = s_terReceiveEnable && TheGlobalData && TheGlobalData->m_useShadowMap
					&& g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()
					&& g_theW3DDeferredRenderer->getShadowCpuTexture() != NULL;
				if (shadowReceive) {
				// 2026-09-08 BINARY PROBE (TEMP): bind the BASE TERRAL TEXTURE to s7
				// instead of the R32F shadow copy. viz mode 22 then SHOWS the s7
				// sample: pattern visible => the s7 BIND+SAMPLE path is fine and the
				// R32F format is what reads 0 under the D3D8-style binding; still
				// black => the binding itself never lands. Revert after the probe.
				// 2026-09-08 WRAPPER-VISIBLE BIND v2: wrap ONLY the A8R8G8B8 CPU
				// copy — the R32F wrap goes through TextureClass's format switch
				// with an UNKNOWN format and the 14:33/14:50 builds never even
				// reached this block's logging (set() died around the wrap).
				IDirect3DBaseTexture9 *shTex9 = g_theW3DDeferredRenderer->getShadowCpuTexture();
				static TextureClass *s_shWrap = NULL;
				static IDirect3DBaseTexture9 *s_shWrapSrc = NULL;
				if (shTex9 && s_shWrapSrc != shTex9) {
					{
						FILE *wf = fopen("E:\\pbr_compile.log", "a");
						if (wf) { fprintf(wf, "[%d] WRAP-TRY create TextureClass for %p\n", (int)timeGetTime(), (void*)shTex9); fclose(wf); }
					}
					if (s_shWrap) { delete s_shWrap; s_shWrap = NULL; }
					s_shWrap = NEW TextureClass((IDirect3DBaseTexture8*)shTex9);
					s_shWrapSrc = shTex9;
					{
						FILE *wf = fopen("E:\\pbr_compile.log", "a");
						if (wf) { fprintf(wf, "[%d] WRAP-OK wrap=%p\n", (int)timeGetTime(), (void*)s_shWrap); fclose(wf); }
					}
				}
				if (s_shWrap) {
					DX8Wrapper::Set_Texture(4, s_shWrap);
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, shTex9);
				} else {
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, shTex9);
				}
				DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
				DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
				DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MAGFILTER, D3DTEXF_POINT);
				Matrix4x4 svp = g_theW3DDeferredRenderer->getShadowViewProj();
				float sc3[4] = { svp[0][0], svp[0][1], svp[0][2], svp[0][3] };
				float sc4[4] = { svp[1][0], svp[1][1], svp[1][2], svp[1][3] };
				float sc5[4] = { svp[2][0], svp[2][1], svp[2][2], svp[2][3] };
				float sc6[4] = { svp[3][0], svp[3][1], svp[3][2], svp[3][3] };
				// 2026-09-10 evening: fp16 shadow RT trial - the quantum drops to
				// ~0.0005 so the bias shrinks back (0.005 = 42 units of
				// peter-panning at the 8200-unit window). PAIRED with the fp16
				// RT: if the fp16 trial rolls back to A8R8G8B8, restore 0.005.
				float sc7[4] = { 1.0f / 2048.0f, 1.0f / 2048.0f, 0.001f, 1.0f };
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(3, sc3, 1);
				// 2026-09-09 RA3-FAITHFUL TSS STAGE 7: the fixed-function 'VS' computes
				// shadow UV+depth per-vertex, exactly like RA3 Terrain.fx does in its VS:
				//   TEXCOORD7 = objectSpacePos * [sunVP * bias]
				// Terrain vertices ARE world coordinates (WORLD stays identity), so
				// object space == world space. The PS then does zero matrix math.
				{
					// 2026-09-10 V-FLIP FIX: the cast writes the RT via STANDARD D3D
					// rasterization (W3X ShadowDepth VS = raw sunVP, ndc.y=+1 lands on
					// the RT TOP row), so the receive UV must be v = 0.5 - 0.5*ndc.y.
					// _22 was +0.5 (v mirrored top/bottom): the UV domain still read
					// [0,1] - the CPU xcheck "domain correct" passed - but every texel
					// sampled the MIRRORED ground position, comparing depth from the
					// wrong place => no shadow. Reference: standard D3D shadow-map
					// ScaleBias (D3D9 docs / cross-confirmed Doubao+Kimi 2026-09-10).
					D3DXMATRIX mBias;
					mBias._11 = 0.5f; mBias._12 = 0.0f; mBias._13 = 0.0f; mBias._14 = 0.0f;
					mBias._21 = 0.0f; mBias._22 = -0.5f; mBias._23 = 0.0f; mBias._24 = 0.0f;
					mBias._31 = 0.0f; mBias._32 = 0.0f; mBias._33 = 1.0f; mBias._34 = 0.0f;
					mBias._41 = 0.5f; mBias._42 = 0.5f; mBias._43 = 0.0f; mBias._44 = 1.0f;
					D3DXMATRIX mSunVP;  // engine Matrix4x4 row-major == D3DXMATRIX memory
					memcpy(&mSunVP, &svp, sizeof(D3DXMATRIX));
					// 2026-09-09 CORRECT FORMULA (MSDN row-vector TSS): out = camPos(row) * M,
					// M = InvView * SunVP * Bias. Engine Matrix4x4 memory IS row-major D3D
					// math (fixed-pipeline VIEW/PROJ prove it); sunVP is the same convention
					// the W3X cast validated. NO transposes anywhere.
					D3DXMATRIX mShadowUVZ, mTmp;
					D3DXMatrixMultiply(&mTmp, &inv, &mSunVP);        // InvView * SunVP
					D3DXMatrixMultiply(&mShadowUVZ, &mTmp, &mBias);  // ... * Bias
					// 2026-09-09 TERRAIN VS: upload the SAME validated matrix chain as VS
					// constants c4-c7 (ShadowUVZ = inv*sunVP*bias, row-major) and c0-c3
					// (ViewProj), then bind the VS. The TSS stage7 setup below stays
					// harmless (VS bypasses fixed-function coordinate generation).
					// 2026-09-09 ENGINE-NATIVE VS CONSTANTS: W3XRenderObj.cpp's proven net
					// convention - engine Matrix4x4 memory reaches the shader VERBATIM (its
					// pre-Transpose + SetMatrix's internal transpose cancel out). We reproduce
					// that net effect directly: engine Multiply on raw engine memory, uploaded
					// as one block. No D3DX transposes anywhere.
					static const bool s_terrainVsEnabled = false;  // 2026-09-09 ROLLBACK: VS route broke the terrain - restore stable fixed-pipeline vertices
					if (s_terrainVsEnabled && m_dwTerrainVS) {
						// 2026-09-09 PROVEN-BY-MATH: the fixed pipeline renders terrain correctly
						// with the DEVICE VIEW/PROJ memory as row-vector math. HLSL mul(v,M).x
						// = dot(v,c0); uploading the device memory verbatim puts the math rows
						// in the registers - strictly correct with ZERO transposes. The earlier
						// water-cover bug was the FVF-missing NORMAL (undefined behavior),
						// NOT this matrix. Device memory, engine Multiply, no transpose.
						Matrix4x4 projM;
						DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projM);
						Matrix4x4 vp = Multiply(curView, projM);
						Matrix4x4 biasM;
						biasM.Make_Identity();
						// 2026-09-10 V-FLIP: -0.5 matches the TSS bias above (cast RT is
						// standard-rasterized; see the mBias comment for the full chain).
						biasM[0] = Vector4(0.5f, 0.0f, 0.0f, 0.0f);        // was W3XRenderObj.cpp:1262-1268 (+0.5, mirrored)
						biasM[1] = Vector4(0.0f, -0.5f, 0.0f, 0.0f);
						biasM[2] = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
						biasM[3] = Vector4(0.5f, 0.5f, 0.0f, 1.0f);
						Matrix4x4 shadowUVZm = Multiply(svp, biasM);  // VERBATIM W3XRenderObj.cpp:1268 (receive matrix)
						DX8Wrapper::_Get_D3D_Device8()->SetVertexShaderConstantF(0, (const float*)&vp, 4);
						DX8Wrapper::_Get_D3D_Device8()->SetVertexShaderConstantF(4, (const float*)&shadowUVZm, 4);
						DX8Wrapper::_Get_D3D_Device8()->SetVertexShader(m_dwTerrainVS);
						// VPCHECK: the yard point is ON SCREEN - the correct vp must map it to |x|<1,|y|<1
						{
							static int s_vpN = 0;
							if ((s_vpN++ % 600) == 0) {
								Vector4 P(2076.0f, 680.8f, 25.6f, 1.0f);
								float rx = P.X*vp[0][0] + P.Y*vp[1][0] + P.Z*vp[2][0] + P.W*vp[3][0];
								float ry = P.X*vp[0][1] + P.Y*vp[1][1] + P.Z*vp[2][1] + P.W*vp[3][1];
								float rz = P.X*vp[0][2] + P.Y*vp[1][2] + P.Z*vp[2][2] + P.W*vp[3][2];
								float rw = P.X*vp[0][3] + P.Y*vp[1][3] + P.Z*vp[2][3] + P.W*vp[3][3];
								float ux = P.X*shadowUVZm[0][0] + P.Y*shadowUVZm[1][0] + P.Z*shadowUVZm[2][0] + P.W*shadowUVZm[3][0];
								FILE* vf = fopen("E:\\pbr_compile.log", "a");
								if (vf) { fprintf(vf, "[%d] VPCHECK-ENG vp=(%.3f, %.3f, %.3f, %.2f) %s | shadowU=%.3f%s\n",
									(int)timeGetTime(), rx, ry, rz, rw,
									(fabsf(rx) < 1.0f && fabsf(ry) < 1.0f && rw > 0.0f) ? "<== NDC-OK" : "WRONG",
									ux, (ux > 0.0f && ux < 1.0f) ? " UV-OK" : ""); fclose(vf); }
							}
						}
					}

					DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
					DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
					DX8Wrapper::_Get_D3D_Device8()->SetTransform(D3DTS_TEXTURE7, &mShadowUVZ);
					// CPU CROSS-CHECK (throttled): project the known construction-yard point
					// through the same matrix so the PS's TEXCOORD7 can be validated against it.
					{
						static int s_uvzN = 0;
						if ((s_uvzN++ % 300) == 0) {
							D3DXVECTOR4 out, camPos;
							D3DXMATRIX mView;  memcpy(&mView, &curView, sizeof(D3DXMATRIX));
							D3DXVec4Transform(&camPos, &D3DXVECTOR4(2076.0f, 680.8f, 25.6f, 1.0f), &mView);
							D3DXVec4Transform(&out, &camPos, &mShadowUVZ);
							FILE *uf = fopen("E:\\pbr_compile.log", "a");
							if (uf) {
								fprintf(uf, "[%d] UVZ-XCHECK yard=(%.4f, %.4f, %.4f, %.2f)  (expect uv ~0.8 z ~0.5; V-FLIP: v = old_v mirrored about 0.5)\n",
									(int)timeGetTime(), out.x, out.y, out.z, out.w);
								fclose(uf);
							}
						}
					}
				}

				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(4, sc4, 1);
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(5, sc5, 1);
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(6, sc6, 1);
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(7, sc7, 1);
				// 2026-09-09: c9-c12 = TRANSPOSED inverse view. The engine
				// Matrix4x4 stores column-major memory; reinterpreting it as a
				// row-major D3DXMATRIX and inverting yields the TRANSPOSE of the
				// true camera inverse (c9 readback: rotation correct, but mul
				// gave rotation-only results - translation in wrong rows).
				// Uploading the transpose restores mul(float4(camPos,1), M) to
				// true world coordinates.
				{
					D3DXMATRIX invT;
					D3DXMatrixTranspose(&invT, &inv);
					float iv0[4] = { invT._11, invT._12, invT._13, invT._14 };
					float iv1[4] = { invT._21, invT._22, invT._23, invT._24 };
					float iv2[4] = { invT._31, invT._32, invT._33, invT._34 };
					float iv3[4] = { invT._41, invT._42, invT._43, invT._44 };
					DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(9, iv0, 1);
					DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(10, iv1, 1);
					DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, iv2, 1);
					DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(12, iv3, 1);
				}
				// 2026-09-08 CONSTANT READBACK (throttled): read c3/c6 straight
				// back from the DEVICE right after upload. If these differ from
				// sc3/sc6 the upload path is broken; if they match, the suv=0
				// ghost lives in the PS inputs (worldPos) or codegen.
				{
					static int s_crb = 0;
					if ((s_crb++ % 300) == 0) {
						IDirect3DDevice9 *d9c = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
						float rb3[4] = {0,0,0,0}, rb6[4] = {0,0,0,0}, rb9[4] = {0,0,0,0};
						d9c->GetPixelShaderConstantF(3, rb3, 1);
						d9c->GetPixelShaderConstantF(6, rb6, 1);
						d9c->GetPixelShaderConstantF(9, rb9, 1);
						FILE *cf = fopen("E:\\pbr_compile.log", "a");
						if (cf) {
							fprintf(cf, "[%d] CONST-READBACK c3=(%.5f,%.5f,%.5f,%.5f) c6=(%.5f,%.5f,%.5f,%.5f) c9=(%.4f,%.4f,%.4f,%.4f)\n",
								(int)timeGetTime(), rb3[0], rb3[1], rb3[2], rb3[3], rb6[0], rb6[1], rb6[2], rb6[3], rb9[0], rb9[1], rb9[2], rb9[3]);
							fclose(cf);
						}
					}
				}
				// 2026-09-06 INGAME-TERRECV (throttled): receive-end truth.
				{ static int s_trx = 0; if ((s_trx++ % 300) == 0) {
				FILE *f = fopen("E:\\pbr_compile.log", "a");
				if (f) {
					// 2026-09-08: also dump what the device holds on s1/s4 RIGHT
					// AFTER our bind (compare with TER-DEVENTRY next frame).
					IDirect3DDevice9 *d9q = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
					IDirect3DBaseTexture9 *a1 = NULL, *a4 = NULL;
					d9q->GetTexture(1, &a1); d9q->GetTexture(4, &a4);
					fprintf(f, "[%d] TER-RECV: on=1 c7=(%.5f,%.5f,%.4f,%.1f) vpR3=(%.4f,%.4f,%.4f,%.4f) postbind s1=%p s4=%p\n",
						(int)timeGetTime(), sc7[0], sc7[1], sc7[2], sc7[3], sc6[0], sc6[1], sc6[2], sc6[3],
						(void*)a1, (void*)a4);
					if (a1) a1->Release();
					if (a4) a4->Release();
					fclose(f);
					} } }
			} else {
				float sc7[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(7, sc7, 1);
				{ static int s_trx0 = 0; if ((s_trx0++ % 3000) == 0) {
					FILE *f = fopen("E:\\pbr_compile.log", "a");
					if (f) { fprintf(f, "[%d] TER-RECV: on=0 (gate false)\n", (int)timeGetTime()); fclose(f); } } }
			}
			// c8.x = PBRDebugMode - uploaded UNCONDITIONALLY. A stale c8 left by
			// other shader systems' constant uploads activated the viz masks with
			// garbage and blackened PBR geometry when receive was off.
			float sdbgT[4] = { TheGlobalData ? (float)TheGlobalData->m_pbrDebugMode : 0.0f, 0.0f, 0.0f, 0.0f };
			DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(8, sdbgT, 1);
		}


	// Select the correct pixel shader for this variant
	switch (curShader) {
		case W3DShaderManager::ST_TERRAIN_PBR:
			DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRPixelShader);
			break;
		case W3DShaderManager::ST_TERRAIN_PBR_NOISE1:
			if (m_dwPBRNoise1PixelShader)
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRNoise1PixelShader);
			else
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRPixelShader);
			break;
		case W3DShaderManager::ST_TERRAIN_PBR_NOISE2:
			if (m_dwPBRNoise2PixelShader)
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRNoise2PixelShader);
			else
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRPixelShader);
			break;
		case W3DShaderManager::ST_TERRAIN_PBR_NOISE12:
			if (m_dwPBRNoise12PixelShader)
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRNoise12PixelShader);
			else
				DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwPBRPixelShader);
			break;
	}
	return TRUE;
}

void TerrainShaderPBR::reset(void)
{
	W3DShaderManager::ShaderTypes curShader = W3DShaderManager::getCurrentShader();
	if (curShader < W3DShaderManager::ST_TERRAIN_PBR || curShader > W3DShaderManager::ST_TERRAIN_PBR_NOISE12) {
		terrainShaderPixelShader.reset();
		return;
	}
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);
	ShaderClass::Invalidate();
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(5, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(6, NULL);
	DX8Wrapper::Set_Texture(4, NULL);
	// 2026-09-09: unbind the terrain VS so other passes get fixed-function vertices
	DX8Wrapper::_Get_D3D_Device8()->SetVertexShader(NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 7);
	DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);	// 2026-09-08: shadow receive stage (s4 - DEVENTRY proved s1 swapped by material replay)
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);
	DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|4);
	DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(5, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|5);
	DX8Wrapper::Set_DX8_Texture_Stage_State(6, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(6, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|6);
}

Int TerrainShaderPBR::shutdown(void)
{
	if (m_dwPBRPixelShader) {
		m_dwPBRPixelShader->Release();
		m_dwPBRPixelShader = NULL;
	}
	if (m_dwPBRNoise1PixelShader) {
		m_dwPBRNoise1PixelShader->Release();
		m_dwPBRNoise1PixelShader = NULL;
	}
	if (m_dwPBRNoise2PixelShader) {
		m_dwPBRNoise2PixelShader->Release();
		m_dwPBRNoise2PixelShader = NULL;
	}
	if (m_dwPBRNoise12PixelShader) {
		m_dwPBRNoise12PixelShader->Release();
		m_dwPBRNoise12PixelShader = NULL;
	}
	return terrainShaderPixelShader.shutdown();
}

// W3DPBRShader ==========================================================================
Int W3DPBRShader::init( void )
{
	m_dwPBRPixelShader = NULL;
	m_dwPBRAlphaPixelShader = NULL;
	m_dwPBRPixelShaderNT = NULL;
	m_dwPBRAlphaPixelShaderNT = NULL;
	m_dwPBRPixelShader_30 = NULL;
	m_dwPBRAlphaPixelShader_30 = NULL;
	m_dwPBRPixelShader_30_IBL = NULL;
	m_dwPBRAlphaPixelShader_30_IBL = NULL;
	m_dwPBRPixelShader_30_IBLSpec = NULL;
	m_dwPBRAlphaPixelShader_30_IBLSpec = NULL;
	m_envIrradianceMap = NULL;
	m_envPrefilteredMap = NULL;
	m_brdfLUT = NULL;

	// NT IBL variants
	m_dwPBRPixelShaderNT_IBL = NULL;
	m_dwPBRAlphaPixelShaderNT_IBL = NULL;
	m_dwPBRPixelShaderNT_30_IBLSpec = NULL;
	m_dwPBRAlphaPixelShaderNT_30_IBLSpec = NULL;
	m_dwPBRPixelShaderNT_30 = NULL;
	m_dwPBRAlphaPixelShaderNT_30 = NULL;
	m_dwPBRPixelShaderNT_30_IBL = NULL;
	m_dwPBRAlphaPixelShaderNT_30_IBL = NULL;
	m_vsPBRUnit = NULL;
	m_dwSunGlowShader = NULL;
	m_sunGlowEnabled = FALSE;

	// Phase 4: 4-light GGX shader with PBR texture support
	// Register layout:
	//   s0 = albedo/diffuse, s2 = PBR (R=rough,G=metal,B=AO)
	//   c0 = sun dir, c1 = sun color, c2 = camera pos
	//   c3 = { roughnessOverride, roughness, metalnessOverride, metalness }
	//   c4 = light2 dir, c5 = light2 color
	//   c6 = light3 dir, c7 = light3 color
	//   c8 = light4 dir, c9 = light4 color
	//   c10 = ambient
	{
		const char* src =
			"sampler s0 : register(s0);\n"
			"sampler s2 : register(s2);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float4 pbrMap = tex2D(s2, tex0);\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float useOverride = step(0.5, c3.x);\n"
			"    float roughness = lerp(max(pbrMap.r, 0.04), c3.y, useOverride);\n"
			"    float metalness = lerp(saturate(pbrMap.g), c3.w, useOverride);\n"
			"    float ao = lerp(pbrMap.b, c3.z, useOverride);\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float3 result = float3(0,0,0);\n"
			"    { float3 L = normalize(c0.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c1.xyz * NdotL; }\n"
			"    { float3 L = normalize(c4.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c5.xyz * NdotL; }\n"
			"    { float3 L = normalize(c6.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c7.xyz * NdotL; }\n"
			"    { float3 L = normalize(c8.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c9.xyz * NdotL; }\n"
			"    result += diffuseColor * c10.xyz * ao;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
		if (FAILED(compilePBRShader(src, &m_dwPBRPixelShader, "pbr_unit")))
			DEBUG_LOG(("PBR: W3DPBRShader init FAILED - no PBR shader available\n"));
		else
			DEBUG_LOG(("PBR: W3DPBRShader init OK\n"));

		// Alpha variant: same HLSL, separate handle for alpha-blended meshes
		if (FAILED(compilePBRShader(src, &m_dwPBRAlphaPixelShader, "pbr_unit_alpha")))
			DEBUG_LOG(("PBR: alpha shader compile FAILED\n"));
		else
			DEBUG_LOG(("PBR: alpha shader compile OK\n"));
	}

	// NT variant: No PBR texture (s2) — uses c3.y/c3.w overrides directly
	{
		const char* srcNT =
			"sampler s0 : register(s0);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float roughness = max(c3.y, 0.04);\n"
			"    float metalness = saturate(c3.w);\n"
			"    float ao = c3.z;\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float3 result = float3(0,0,0);\n"
			"    { float3 L = normalize(c0.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c1.xyz * NdotL; }\n"
			"    { float3 L = normalize(c4.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c5.xyz * NdotL; }\n"
			"    { float3 L = normalize(c6.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c7.xyz * NdotL; }\n"
			"    { float3 L = normalize(c8.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c9.xyz * NdotL; }\n"
			"    result += diffuseColor * c10.xyz * ao;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
		if (FAILED(compilePBRShader(srcNT, &m_dwPBRPixelShaderNT, "pbr_unit_nt")))
			DEBUG_LOG(("PBR: NT shader compile FAILED\n"));
		else
			DEBUG_LOG(("PBR: NT shader compile OK\n"));

		// Alpha NT variant: same NT HLSL, separate handle
		if (FAILED(compilePBRShader(srcNT, &m_dwPBRAlphaPixelShaderNT, "pbr_unit_alphant")))
			DEBUG_LOG(("PBR: alpha NT shader compile FAILED\n"));
		else
			DEBUG_LOG(("PBR: alpha NT shader compile OK\n"));
	}

	// NT ps_3_0 base shader: loop-based GGX without PBR texture (no IBL)
	// Always compiled — not dependent on ps_2_0 result
	{
		const char* srcNT30 =
		"sampler s0 : register(s0);\n"
		"float3 c0 : register(c0);\n"
		"float3 c1 : register(c1);\n"
		"float3 c2 : register(c2);\n"
		"float4 c3 : register(c3);\n"
		"float3 c4 : register(c4);\n"
		"float3 c5 : register(c5);\n"
		"float3 c6 : register(c6);\n"
		"float3 c7 : register(c7);\n"
		"float3 c8 : register(c8);\n"
		"float3 c9 : register(c9);\n"
		"float3 c10 : register(c10);\n"
		"float4 main(float2 tex0 : TEXCOORD0,\n"
		"    float3 worldPos : TEXCOORD1,\n"
		"    float3 worldNormal : TEXCOORD4,\n"
		"    float4 diffuse : COLOR0) : COLOR\n"
		"{\n"
		"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
		"    float3 N = normalize(worldNormal);\n"
		"    float3 V = normalize(c2.xyz - worldPos);\n"
		"    float NdotV = saturate(dot(N, V));\n"
		"    float roughness = max(c3.y, 0.04);\n"
		"    float metalness = saturate(c3.w);\n"
		"    float ao = c3.z;\n"
		"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
		"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
		"    float a = roughness * roughness;\n"
		"    float a2 = a * a;\n"
		"    float k = a * 0.5;\n"
		"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
		"    float invPI = 1.0;\n" // TMP: bumped from 1/pi (0.31831) to compensate for missing environmental lighting
		"    float3 result = float3(0,0,0);\n"
		"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
		// TMP: sun light 30x to compensate for missing environmental lighting
		"    float3 lightCol[4] = { c1.xyz * 1.0, c5.xyz, c7.xyz, c9.xyz };\n"
		"    [loop] for (int i = 0; i < 4; i++) {\n"
		"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
		"        float3 L = normalize(lightDir[i]);\n"
		"        float NdotL = dot(N, L);\n"
		"        if (NdotL <= 0) continue;\n"
		"        float3 H = normalize(L + V);\n"
		"        float NdotH = saturate(dot(N, H));\n"
		"        float VdotH = saturate(dot(V, H));\n"
		"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
		"        float D = a2 / (3.14159 * d * d);\n"
		"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
		"        float G = G_V * G_L;\n"
		"        float f = 1.0 - VdotH;\n"
		"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
		"        float3 F = F0 + (1.0 - F0) * f5;\n"
		"        float3 specular = D * G * F;\n"
		"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
		"    }\n"
		"    result += diffuseColor * c10.xyz * ao;\n"
	//	"    result += diffuseColor * 5.0;\n"
		//调整环境光倍率叠加10.0倍
	//	"    result += diffuseColor * 10.0;\n"
		"    return float4(result, albedo.a);\n"
		"}\n";
		if (FAILED(compilePBRShader(srcNT30, &m_dwPBRPixelShaderNT_30, "pbr_unit_nt_ps30", "ps_3_0")))
			DEBUG_LOG(("PBR: NT ps_3_0 opaque shader compile FAILED\n"));
		else
			DEBUG_LOG(("PBR: NT ps_3_0 opaque shader compiled OK\n"));
	
		// Alpha NT ps_3_0 variant
		if (FAILED(compilePBRShader(srcNT30, &m_dwPBRAlphaPixelShaderNT_30, "pbr_unit_alpha_nt_ps30", "ps_3_0")))
			DEBUG_LOG(("PBR: NT ps_3_0 alpha shader compile FAILED\n"));
		else
			DEBUG_LOG(("PBR: NT ps_3_0 alpha shader compiled OK\n"));
	}


		// ps_3_0 base shader: loop-based GGX (Stage 5.2)
		{
			const char* src30 =
			"sampler s0 : register(s0);\n"
			"sampler s2 : register(s2);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float4 pbrMap = tex2D(s2, tex0);\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float useOverride = step(0.5, c3.x);\n"
			"    float roughness = lerp(max(pbrMap.r, 0.04), c3.y, useOverride);\n"
			"    float metalness = lerp(saturate(pbrMap.g), c3.w, useOverride);\n"
			"    float ao = lerp(pbrMap.b, c3.z, useOverride);\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
			"    float invPI = 0.31831;\n"
			"    float3 result = float3(0,0,0);\n"
			"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
			"    float3 lightCol[4] = { c1.xyz, c5.xyz, c7.xyz, c9.xyz };\n"
			"    [loop] for (int i = 0; i < 4; i++) {\n"
			"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
			"        float3 L = normalize(lightDir[i]);\n"
			"        float NdotL = dot(N, L);\n"
			"        if (NdotL <= 0) continue;\n"
			"        float3 H = normalize(L + V);\n"
			"        float NdotH = saturate(dot(N, H));\n"
			"        float VdotH = saturate(dot(V, H));\n"
			"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"        float D = a2 / (3.14159 * d * d);\n"
			"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"        float G = G_V * G_L;\n"
			"        float f = 1.0 - VdotH;\n"
			"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"        float3 F = F0 + (1.0 - F0) * f5;\n"
			"        float3 specular = D * G * F;\n"
			"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
			"    }\n"
			"    result += diffuseColor * c10.xyz * ao;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
			if (FAILED(compilePBRShader(src30, &m_dwPBRPixelShader_30, "pbr_unit_ps30", "ps_3_0")))
				DEBUG_LOG(("PBR: ps_3_0 opaque shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR: ps_3_0 opaque shader compiled OK\n"));

			// Alpha variant
			if (FAILED(compilePBRShader(src30, &m_dwPBRAlphaPixelShader_30, "pbr_unit_alpha_ps30", "ps_3_0")))
				DEBUG_LOG(("PBR: ps_3_0 alpha shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR: ps_3_0 alpha shader compiled OK\n"));
		}
		// ps_3_0 diffuse IBL shader: texCUBE irradiance + c11 debug (Stage 5.3+5.5)
		{
			// Try loading irradiance CubeMap for diffuse IBL
			try {
				m_envIrradianceMap = NEW_REF(CubeTextureClass, ("env_irradiance.dds", NULL, MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, false, false));
				DEBUG_LOG(("PBR IBL: env_irradiance.dds loaded OK\n"));
			} catch (...) {
				DEBUG_LOG(("PBR IBL: env_irradiance.dds not found\n"));
			}
			if (!m_envIrradianceMap) {
				FILE *f = fopen("E:\\terrain_diag.log", "a");
				if (f) {
					FILE *test = fopen("env_irradiance.dds", "rb");
					fprintf(f, "[%u] PBR IBL: env_irradiance.dds load FAILED (file %s on disk)\n",
						timeGetTime(), test ? "EXISTS" : "NOT FOUND");
					if (test) fclose(test);
					fclose(f);
				}
			}

			if (m_envIrradianceMap) {
				const char* src30IBL =
			"sampler s0 : register(s0);\n"
			"sampler s2 : register(s2);\n"
			"samplerCUBE s3 : register(s3);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 c11 : register(c11);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float4 pbrMap = tex2D(s2, tex0);\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float useOverride = step(0.5, c3.x);\n"
			"    float roughness = lerp(max(pbrMap.r, 0.04), c3.y, useOverride);\n"
			"    float metalness = lerp(saturate(pbrMap.g), c3.w, useOverride);\n"
			"    float ao = lerp(pbrMap.b, c3.z, useOverride);\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
			"    float invPI = 0.31831;\n"
			"    float3 result = float3(0,0,0);\n"
			"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
			"    float3 lightCol[4] = { c1.xyz, c5.xyz, c7.xyz, c9.xyz };\n"
			"    [loop] for (int i = 0; i < 4; i++) {\n"
			"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
			"        float3 L = normalize(lightDir[i]);\n"
			"        float NdotL = dot(N, L);\n"
			"        if (NdotL <= 0) continue;\n"
			"        float3 H = normalize(L + V);\n"
			"        float NdotH = saturate(dot(N, H));\n"
			"        float VdotH = saturate(dot(V, H));\n"
			"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"        float D = a2 / (3.14159 * d * d);\n"
			"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"        float G = G_V * G_L;\n"
			"        float f = 1.0 - VdotH;\n"
			"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"        float3 F = F0 + (1.0 - F0) * f5;\n"
			"        float3 specular = D * G * F;\n"
			"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
			"    }\n"
			"    // Diffuse IBL\n"
			"    float3 irradiance = texCUBE(s3, N).rgb;\n"
			"    float3 envDiffuse = diffuseColor * irradiance * ao;\n"
			"    // Debug visualization (c11.x = debug mode 0-7)\n"
			"    float dbg = c11.x;\n"
			"    if (dbg > 0.5 && dbg < 1.5) return float4(metalness.xxx, albedo.a);\n"
			"    if (dbg > 1.5 && dbg < 2.5) return float4(roughness.xxx, albedo.a);\n"
			"    if (dbg > 2.5 && dbg < 3.5) return float4(ao.xxx, albedo.a);\n"
			"    if (dbg > 3.5 && dbg < 4.5) return float4(N * 0.5 + 0.5, albedo.a);\n"
			"    result += envDiffuse;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
			if (FAILED(compilePBRShader(src30IBL, &m_dwPBRPixelShader_30_IBL, "pbr_unit_ps30_ibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: ps_3_0 diffuse IBL opaque shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: ps_3_0 diffuse IBL opaque shader compiled OK\n"));

			// Alpha variant
			if (FAILED(compilePBRShader(src30IBL, &m_dwPBRAlphaPixelShader_30_IBL, "pbr_unit_alpha_ps30_ibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: ps_3_0 diffuse IBL alpha shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: ps_3_0 diffuse IBL alpha shader compiled OK\n"));
			} else {
				m_envIrradianceMap = NULL;
			}
		}
		// ps_3_0 specular IBL shader: Split-Sum + c11 debug (Stage 5.4+5.5)
		{
			// Try loading pre-filtered environment CubeMap
			try {
				m_envPrefilteredMap = NEW_REF(CubeTextureClass, ("env_prefiltered.dds", NULL, MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, false, false));
				DEBUG_LOG(("PBR IBL: env_prefiltered.dds loaded OK\n"));
			} catch (...) {
				DEBUG_LOG(("PBR IBL: env_prefiltered.dds not found\n"));
			}

			// Try loading BRDF LUT texture
			try {
				m_brdfLUT = NEW_REF(TextureClass, ("env_brdf_lut.dds", NULL, MIP_LEVELS_1, WW3D_FORMAT_UNKNOWN, false, false));
				DEBUG_LOG(("PBR IBL: env_brdf_lut.dds loaded OK\n"));
			} catch (...) {
				DEBUG_LOG(("PBR IBL: env_brdf_lut.dds not found\n"));
			}

			if (m_envPrefilteredMap && m_brdfLUT) {
				DEBUG_LOG(("PBR IBL: compiling specular IBL ps_3_0 shader variants\n"));

				const char* src30SpecIBL =
			"sampler s0 : register(s0);\n"
			"sampler s2 : register(s2);\n"
			"samplerCUBE s3 : register(s3);\n"
			"samplerCUBE s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 c11 : register(c11);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float4 pbrMap = tex2D(s2, tex0);\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float useOverride = step(0.5, c3.x);\n"
			"    float roughness = lerp(max(pbrMap.r, 0.04), c3.y, useOverride);\n"
			"    float metalness = lerp(saturate(pbrMap.g), c3.w, useOverride);\n"
			"    float ao = lerp(pbrMap.b, c3.z, useOverride);\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
			"    float invPI = 0.31831;\n"
			"    float3 result = float3(0,0,0);\n"
			"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
			"    float3 lightCol[4] = { c1.xyz, c5.xyz, c7.xyz, c9.xyz };\n"
			"    [loop] for (int i = 0; i < 4; i++) {\n"
			"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
			"        float3 L = normalize(lightDir[i]);\n"
			"        float NdotL = dot(N, L);\n"
			"        if (NdotL <= 0) continue;\n"
			"        float3 H = normalize(L + V);\n"
			"        float NdotH = saturate(dot(N, H));\n"
			"        float VdotH = saturate(dot(V, H));\n"
			"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"        float D = a2 / (3.14159 * d * d);\n"
			"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"        float G = G_V * G_L;\n"
			"        float f = 1.0 - VdotH;\n"
			"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"        float3 F = F0 + (1.0 - F0) * f5;\n"
			"        float3 specular = D * G * F;\n"
			"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
			"    }\n"
			"    // Diffuse IBL\n"
			"    float3 irradiance = texCUBE(s3, N).rgb;\n"
			"    float kD = (1.0 - metalness);\n"
			"    float3 envDiffuse = diffuseColor * irradiance * ao * kD;\n"
			"    // Specular IBL (Split-Sum approximation)\n"
			"    float3 reflectDir = reflect(-V, N);\n"
			"    float lod = roughness * 4.0;\n"
			"    float3 prefiltered = texCUBElod(s4, float4(reflectDir, lod)).rgb;\n"
			"    float2 envBRDF = tex2Dlod(s5, float4(NdotV, roughness, 0.0, 0.0)).rg;\n"
			"    float3 envSpecular = prefiltered * (F0 * envBRDF.x + envBRDF.y);\n"
			"    // Debug visualization (c11.x = debug mode 0-7)\n"
			"    float dbg = c11.x;\n"
			"    if (dbg > 0.5 && dbg < 1.5) return float4(metalness.xxx, albedo.a);\n"
			"    if (dbg > 1.5 && dbg < 2.5) return float4(roughness.xxx, albedo.a);\n"
			"    if (dbg > 2.5 && dbg < 3.5) return float4(ao.xxx, albedo.a);\n"
			"    if (dbg > 3.5 && dbg < 4.5) return float4(N * 0.5 + 0.5, albedo.a);\n"
			"    if (dbg > 4.5 && dbg < 5.5) return float4(envDiffuse, albedo.a);\n"
			"    if (dbg > 5.5 && dbg < 6.5) return float4(envSpecular, albedo.a);\n"
			"    if (dbg > 6.5) return float4(result, albedo.a);\n"
			"    result += envDiffuse + envSpecular;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
			if (FAILED(compilePBRShader(src30SpecIBL, &m_dwPBRPixelShader_30_IBLSpec, "pbr_unit_ps30_specibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: ps_3_0 specular IBL opaque shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: ps_3_0 specular IBL opaque shader compiled OK\n"));

			// Alpha variant
			if (FAILED(compilePBRShader(src30SpecIBL, &m_dwPBRAlphaPixelShader_30_IBLSpec, "pbr_unit_alpha_ps30_specibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: ps_3_0 specular IBL alpha shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: ps_3_0 specular IBL alpha shader compiled OK\n"));
			} else {
				DEBUG_LOG(("PBR IBL: specular IBL not available (env_prefiltered.dds or env_brdf_lut.dds missing)\n"));
			}
		}

		// NT IBL variant: no PBR texture (s2) with diffuse IBL CubeMap (s3) [ps_2_0]
		{
			const char* srcNT_IBL =
			"sampler s0 : register(s0);\n"
			"samplerCUBE s3 : register(s3);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 c11 : register(c11);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    float roughness = max(c3.y, 0.04);\n"
			"    float metalness = saturate(c3.w);\n"
			"    float ao = c3.z;\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float3 result = float3(0,0,0);\n"
			"    { float3 L = normalize(c0.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c1.xyz * NdotL; }\n"
			"    { float3 L = normalize(c4.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c5.xyz * NdotL; }\n"
			"    { float3 L = normalize(c6.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c7.xyz * NdotL; }\n"
			"    { float3 L = normalize(c8.xyz); float NdotL = saturate(dot(N, L));\n"
			"    float3 H = normalize(L + V); float NdotH = saturate(dot(N, H));\n"
			"    float d = (NdotH * a2 - NdotH) * NdotH + 1.0; float D = a2 / (3.14159 * d * d);\n"
			"    result += (diffuseColor + D * F0) * c9.xyz * NdotL; }\n"
			"    // Diffuse IBL from CubeMap\n"
			"    float3 irradiance = texCUBE(s3, N).rgb;\n"
			"    result += diffuseColor * irradiance * ao * (1.0 - metalness);\n"

			"    return float4(result, albedo.a);\n"
			"}\n";
			if (FAILED(compilePBRShader(srcNT_IBL, &m_dwPBRPixelShaderNT_IBL, "pbr_unit_nt_ibl")))
				DEBUG_LOG(("PBR IBL: NT diffuse IBL opaque shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: NT diffuse IBL opaque shader compiled OK\n"));

			// Alpha NT IBL variant
			if (FAILED(compilePBRShader(srcNT_IBL, &m_dwPBRAlphaPixelShaderNT_IBL, "pbr_unit_alpha_nt_ibl")))
				DEBUG_LOG(("PBR IBL: NT diffuse IBL alpha shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: NT diffuse IBL alpha shader compiled OK\n"));
		}

			// NT ps_3_0 diffuse IBL shader: loop GGX + texCUBE irradiance, no PBR tex
			// (only compiled when env_irradiance.dds loaded)
			if (m_envIrradianceMap) {
				const char* srcNT_30_IBL =
				"sampler s0 : register(s0);\n"
				"samplerCUBE s3 : register(s3);\n"
				"float3 c0 : register(c0);\n"
				"float3 c1 : register(c1);\n"
				"float3 c2 : register(c2);\n"
				"float4 c3 : register(c3);\n"
				"float3 c4 : register(c4);\n"
				"float3 c5 : register(c5);\n"
				"float3 c6 : register(c6);\n"
				"float3 c7 : register(c7);\n"
				"float3 c8 : register(c8);\n"
				"float3 c9 : register(c9);\n"
				"float3 c10 : register(c10);\n"
				"float4 main(float2 tex0 : TEXCOORD0,\n"
				"    float3 worldPos : TEXCOORD1,\n"
				"    float3 worldNormal : TEXCOORD4,\n"
				"    float4 diffuse : COLOR0) : COLOR\n"
				"{\n"
				"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
				"    float3 N = normalize(worldNormal);\n"
				"    float3 V = normalize(c2.xyz - worldPos);\n"
				"    float NdotV = saturate(dot(N, V));\n"
				"    float roughness = max(c3.y, 0.04);\n"
				"    float metalness = saturate(c3.w);\n"
				"    float ao = c3.z;\n"
				"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
				"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
				"    float a = roughness * roughness;\n"
				"    float a2 = a * a;\n"
				"    float k = a * 0.5;\n"
				"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
				"    float invPI = 1.0;\n" // TMP: bumped from 1/pi (0.31831) to compensate for missing environmental lighting
				"    float3 result = float3(0,0,0);\n"
				"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
				// TMP: sun 30x to compensate for missing environmental lighting
				"    float3 lightCol[4] = { c1.xyz * 1.0, c5.xyz, c7.xyz, c9.xyz };\n"
				"    [loop] for (int i = 0; i < 4; i++) {\n"
				"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
				"        float3 L = normalize(lightDir[i]);\n"
				"        float NdotL = dot(N, L);\n"
				"        if (NdotL <= 0) continue;\n"
				"        float3 H = normalize(L + V);\n"
				"        float NdotH = saturate(dot(N, H));\n"
				"        float VdotH = saturate(dot(V, H));\n"
				"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
				"        float D = a2 / (3.14159 * d * d);\n"
				"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
				"        float G = G_V * G_L;\n"
				"        float f = 1.0 - VdotH;\n"
				"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
				"        float3 F = F0 + (1.0 - F0) * f5;\n"
				"        float3 specular = D * G * F;\n"
				"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
				"    }\n"
				"    // Diffuse IBL from CubeMap\n"
				"    float3 irradiance = texCUBE(s3, N).rgb;\n"
				"    result += diffuseColor * irradiance * ao * (1.0 - metalness);\n"
				"    result += diffuseColor * c10.xyz * ao;\n"
			//	"    result += diffuseColor * 5.0;\n"
				//调整环境光叠加倍率10倍
			//	"    result += diffuseColor * 10.0;\n"
				"    return float4(result, albedo.a);\n"
				"}\n";
				if (FAILED(compilePBRShader(srcNT_30_IBL, &m_dwPBRPixelShaderNT_30_IBL, "pbr_unit_nt_ps30_ibl", "ps_3_0")))
					DEBUG_LOG(("PBR IBL: NT ps_3_0 diffuse IBL opaque shader compile FAILED\n"));
				else
					DEBUG_LOG(("PBR IBL: NT ps_3_0 diffuse IBL opaque shader compiled OK\n"));
				// Alpha NT ps_3_0 IBL variant
				if (FAILED(compilePBRShader(srcNT_30_IBL, &m_dwPBRAlphaPixelShaderNT_30_IBL, "pbr_unit_alpha_nt_ps30_ibl", "ps_3_0")))
					DEBUG_LOG(("PBR IBL: NT ps_3_0 diffuse IBL alpha shader compile FAILED\n"));
				else
					DEBUG_LOG(("PBR IBL: NT ps_3_0 diffuse IBL alpha shader compiled OK\n"));
			}

		// NT ps_3_0 IBL specular variant: loop GGX + Split-Sum IBL, no PBR tex
		// (only compiled when env_prefiltered.dds and env_brdf_lut.dds loaded)
		if (m_envPrefilteredMap && m_brdfLUT) {
			const char* srcNT_30_IBLSpec =
			"sampler s0 : register(s0);\n"
			"samplerCUBE s3 : register(s3);\n"
			"samplerCUBE s4 : register(s4);\n"
			"sampler s5 : register(s5);\n"
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float3 c2 : register(c2);\n"
			"float4 c3 : register(c3);\n"
			"float3 c4 : register(c4);\n"
			"float3 c5 : register(c5);\n"
			"float3 c6 : register(c6);\n"
			"float3 c7 : register(c7);\n"
			"float3 c8 : register(c8);\n"
			"float3 c9 : register(c9);\n"
			"float3 c10 : register(c10);\n"
			"float4 c11 : register(c11);\n"
			"float4 main(float2 tex0 : TEXCOORD0,\n"
			"    float3 worldPos : TEXCOORD1,\n"
			"    float3 worldNormal : TEXCOORD4,\n"
			"    float4 diffuse : COLOR0) : COLOR\n"
			"{\n"
			"    float4 albedo = tex2D(s0, tex0);\n"
			"    albedo.rgb *= diffuse.rgb;\n"
			"    float3 N = normalize(worldNormal);\n"
			"    float3 V = normalize(c2.xyz - worldPos);\n"
			"    float NdotV = saturate(dot(N, V));\n"
			"    // No PBR texture: derive params from c3 constants\n"
			"    float roughness = max(c3.y, 0.04);\n"
			"    float metalness = saturate(c3.w);\n"
			"    float ao = c3.z;\n"
			"    float3 diffuseColor = albedo.rgb * (1.0 - metalness) * (1.0 - 0.3 * metalness);\n"
			"    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, metalness);\n"
			"    float a = roughness * roughness;\n"
			"    float a2 = a * a;\n"
			"    float k = a * 0.5;\n"
			"    float G_V = NdotV / (NdotV * (1.0 - k) + k);\n"
			"    float invPI = 1.0;\n" // TMP: bumped from 1/pi (0.31831) to compensate for missing environmental lighting
			"    float3 result = float3(0,0,0);\n"
			"    float3 lightDir[4] = { c0.xyz, c4.xyz, c6.xyz, c8.xyz };\n"
			// TMP: sun 30x to compensate for missing environmental lighting
			"    float3 lightCol[4] = { c1.xyz * 1.0, c5.xyz, c7.xyz, c9.xyz };\n"
			"    [loop] for (int i = 0; i < 4; i++) {\n"
			"        if (dot(lightDir[i], lightDir[i]) < 0.0001) continue;\n"
			"        float3 L = normalize(lightDir[i]);\n"
			"        float NdotL = dot(N, L);\n"
			"        if (NdotL <= 0) continue;\n"
			"        float3 H = normalize(L + V);\n"
			"        float NdotH = saturate(dot(N, H));\n"
			"        float VdotH = saturate(dot(V, H));\n"
			"        float d = (NdotH * a2 - NdotH) * NdotH + 1.0;\n"
			"        float D = a2 / (3.14159 * d * d);\n"
			"        float G_L = NdotL / (NdotL * (1.0 - k) + k);\n"
			"        float G = G_V * G_L;\n"
			"        float f = 1.0 - VdotH;\n"
			"        float f5 = f * f; f5 = f5 * f5; f5 = f5 * f;\n"
			"        float3 F = F0 + (1.0 - F0) * f5;\n"
			"        float3 specular = D * G * F;\n"
			"        result += (diffuseColor * (1.0 - F) * invPI * lightCol[i] * NdotL + specular * lightCol[i] / max(4.0 * NdotV, 0.001));\n"
			"    }\n"
			"    // Diffuse IBL\n"
			"    float3 irradiance = texCUBE(s3, N).rgb;\n"
			"    float kD = (1.0 - metalness);\n"
			"    float3 envDiffuse = diffuseColor * irradiance * ao * kD;\n"
			"    // Specular IBL (Split-Sum approximation)\n"
			"    float3 reflectDir = reflect(-V, N);\n"
			"    float lod = roughness * 4.0;\n"
			"    float3 prefiltered = texCUBElod(s4, float4(reflectDir, lod)).rgb;\n"
			"    float2 envBRDF = tex2Dlod(s5, float4(NdotV, roughness, 0.0, 0.0)).rg;\n"
			"    float3 envSpecular = prefiltered * (F0 * envBRDF.x + envBRDF.y);\n"
			"    // Debug visualization (c11.x = debug mode 0-7)\n"
			"    float dbg = c11.x;\n"
			"    if (dbg > 0.5 && dbg < 1.5) return float4(metalness.xxx, albedo.a);\n"
			"    if (dbg > 1.5 && dbg < 2.5) return float4(roughness.xxx, albedo.a);\n"
			"    if (dbg > 2.5 && dbg < 3.5) return float4(ao.xxx, albedo.a);\n"
			"    if (dbg > 3.5 && dbg < 4.5) return float4(N * 0.5 + 0.5, albedo.a);\n"
			"    if (dbg > 4.5 && dbg < 5.5) return float4(envDiffuse, albedo.a);\n"
			"    if (dbg > 5.5 && dbg < 6.5) return float4(envSpecular, albedo.a);\n"
			"    if (dbg > 6.5) return float4(result, albedo.a);\n"
			"    result += envDiffuse + envSpecular;\n"
			"    result += diffuseColor * c10.xyz * ao;\n"
		//	"    result += diffuseColor * 5.0;\n"
			//调整环境光倍率叠再加10倍
		//	"    result += diffuseColor * 10.0;\n"
			"    return float4(result, albedo.a);\n"
			"}\n";
			if (FAILED(compilePBRShader(srcNT_30_IBLSpec, &m_dwPBRPixelShaderNT_30_IBLSpec, "pbr_unit_nt_ps30_specibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: NT ps_3_0 specular IBL opaque shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: NT ps_3_0 specular IBL opaque shader compiled OK\n"));

			// Alpha variant
			if (FAILED(compilePBRShader(srcNT_30_IBLSpec, &m_dwPBRAlphaPixelShaderNT_30_IBLSpec, "pbr_unit_alpha_nt_ps30_specibl", "ps_3_0")))
				DEBUG_LOG(("PBR IBL: NT ps_3_0 specular IBL alpha shader compile FAILED\n"));
			else
				DEBUG_LOG(("PBR IBL: NT ps_3_0 specular IBL alpha shader compiled OK\n"));
		}
		// Register for unit shader types
		W3DShaders[W3DShaderManager::ST_PBR_UNIT_OPAQUE] = this;
	W3DShadersPassCount[W3DShaderManager::ST_PBR_UNIT_OPAQUE] = 1;
	W3DShaders[W3DShaderManager::ST_PBR_UNIT_ALPHA] = this;
	W3DShadersPassCount[W3DShaderManager::ST_PBR_UNIT_ALPHA] = 1;

	// Export shader handles for dx8renderer.cpp (cross-library).
	// Prefer specular IBL > diffuse IBL > ps_3_0 loop > ps_2_0 fallback.
	// This ensures both PBR-texture and NT (no PBR tex) paths get IBL.
	if (m_envIrradianceMap) {
		// PBR texture path (reads s2)
		if (m_envPrefilteredMap && m_brdfLUT && m_dwPBRPixelShader_30_IBLSpec) {
			g_pbrUnitOpaqueShader = m_dwPBRPixelShader_30_IBLSpec;
			g_pbrUnitAlphaShader = m_dwPBRAlphaPixelShader_30_IBLSpec;
		} else if (m_dwPBRPixelShader_30_IBL) {
			g_pbrUnitOpaqueShader = m_dwPBRPixelShader_30_IBL;
			g_pbrUnitAlphaShader = m_dwPBRAlphaPixelShader_30_IBL;
		} else {
			g_pbrUnitOpaqueShader = m_dwPBRPixelShader_30 ? m_dwPBRPixelShader_30 : m_dwPBRPixelShader;
			g_pbrUnitAlphaShader = m_dwPBRAlphaPixelShader_30 ? m_dwPBRAlphaPixelShader_30 : m_dwPBRAlphaPixelShader;
		}
		// NT path (no s2)
		if (m_envPrefilteredMap && m_brdfLUT && m_dwPBRPixelShaderNT_30_IBLSpec) {
			g_pbrUnitOpaqueNTShader = m_dwPBRPixelShaderNT_30_IBLSpec;
			g_pbrUnitAlphaNTShader = m_dwPBRAlphaPixelShaderNT_30_IBLSpec;
		} else if (m_dwPBRPixelShaderNT_30_IBL) {
			g_pbrUnitOpaqueNTShader = m_dwPBRPixelShaderNT_30_IBL;
			g_pbrUnitAlphaNTShader = m_dwPBRAlphaPixelShaderNT_30_IBL;
		} else if (m_dwPBRPixelShaderNT_IBL) {
			g_pbrUnitOpaqueNTShader = m_dwPBRPixelShaderNT_IBL;
			g_pbrUnitAlphaNTShader = m_dwPBRAlphaPixelShaderNT_IBL;
		}
	} else {
		// No CubeMap available — fall back to shaders without IBL
		g_pbrUnitOpaqueShader = m_dwPBRPixelShader_30 ? m_dwPBRPixelShader_30 : m_dwPBRPixelShader;
		g_pbrUnitAlphaShader = m_dwPBRAlphaPixelShader_30 ? m_dwPBRAlphaPixelShader_30 : m_dwPBRAlphaPixelShader;
		// NT path: prefer ps_3_0 NT (no CubeMap sampling), fall back to ps_2_0 NT
		g_pbrUnitOpaqueNTShader = m_dwPBRPixelShaderNT_30 ? m_dwPBRPixelShaderNT_30 : m_dwPBRPixelShaderNT;
		g_pbrUnitAlphaNTShader = m_dwPBRAlphaPixelShaderNT_30 ? m_dwPBRAlphaPixelShaderNT_30 : m_dwPBRAlphaPixelShaderNT;
	}
	g_pbrUnitShaderEnabled = (m_dwPBRPixelShader != NULL || m_dwPBRPixelShader_30 != NULL) ? TRUE : FALSE;
	g_pbrDebugMode = TheGlobalData ? TheGlobalData->m_pbrDebugMode : 0;

	// Compile G-Buffer vertex shader (vs_1_1) and pixel shader (ps_2_0).
	// VS outputs: oT0=texcoord, oT1=worldPos, oT4=worldNormal, oT2=clipDepth (z,w)
	// PS outputs: RT0=Albedo+Metallic, RT1=Normal+Roughness, RT2=Emissive+Depth
	if (g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isAvailable()) {
		if (!g_gbufferVS) {
			const char gbuffer_vs[] =
				"vs.1.1\n"
				"dcl_position v0\n"
				"dcl_normal v3\n"
				"dcl_texcoord0 v7\n"
				"m4x4 oPos, v0, c0\n"
				"mov oT0, v7\n"
				"m4x4 oT1, v0, c4\n"
				"m3x3 oT4, v3, c8\n"
				"dp4 oT2.x, v0, c2\n"
				"dp4 oT2.y, v0, c3\n";
			ID3DXBuffer *vsCompiled = NULL;
			ID3DXBuffer *vsErrors = NULL;
			HRESULT vsHR = D3DXAssembleShader(gbuffer_vs, (UINT)strlen(gbuffer_vs),
				NULL, NULL, 0, &vsCompiled, &vsErrors);
			if (FAILED(vsHR) || !vsCompiled) {
				WWDEBUG_SAY(("W3DShaderManager: G-Buffer VS compile failed.\n"));
				if (vsErrors) { vsErrors->Release(); }
				g_gbufferVS = NULL;
			} else {
				IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
				if (dev) {
					vsHR = dev->CreateVertexShader(
						(const DWORD*)vsCompiled->GetBufferPointer(), &g_gbufferVS);
				}
				vsCompiled->Release();
				WWDEBUG_SAY(("W3DShaderManager: G-Buffer VS compiled.\n"));
			}
		}
		if (!g_gbufferPS) {
			const char gbuffer_ps[] =
				// ---- input from vertex shader ----
				"struct PS_IN {\n"
				"\tfloat2 tex0 : TEXCOORD0;\n"
				"\tfloat3 worldPos : TEXCOORD1;\n"
				"\tfloat2 clipDepth : TEXCOORD2;\n"
				"\tfloat3 worldNormal : TEXCOORD4;\n"
				"};\n"
				// ---- MRT outputs ----
				"struct PS_OUT {\n"
				"\tfloat4 color0 : COLOR0;\n"
				"\tfloat4 color1 : COLOR1;\n"
				"\tfloat4 color2 : COLOR2;\n"
				"};\n"
								"float4 c11 : register(c11);\n"
"sampler Diffuse : register(s0);\n"
				"\n"
				// ---- octahedral normal encoding (unit sphere → 2D square) ----
				"float2 octEncode(float3 n) {\n"
				"\tn /= abs(n.x) + abs(n.y) + abs(n.z);\n"
				"\tfloat2 p = n.xy;\n"
				"\tp = (n.z >= 0) ? p : (1 - abs(p.yx)) * (2 * step(0, p) - 1);\n"
				"\treturn p * 0.5 + 0.5;\n"
				"}\n"
				"\n"
				"PS_OUT main(PS_IN input) {\n"
				"\tPS_OUT o;\n"
				"\tfloat4 albedo = tex2D(Diffuse, input.tex0);\n"
				"\tfloat3 n = normalize(input.worldNormal);\n"
				"\tfloat depth = input.clipDepth.x / input.clipDepth.y;\n"
				"\n"
				"\n"
				"\t// Octahedral encode normal → RG\n"
				"\tfloat2 encN = octEncode(n);\n"
				"\n"
				"\t// Material properties (PBR defaults; overridden by PBROverride.ini later)\n"
				"\tfloat metallic = c11.y;\n"
				"\tfloat roughness = c11.x;\n"
				"\n"
				"\t// Emissive (placeholder)\n"
				"\tfloat emissive = 0.0f;\n"
				"\tfloat specialFlag = 0.0f;\n"
				"\n"
				"\t// ---- G-Buffer channel layout ----\n"
				"\t// RT0 (A8R8G8B8): albedoSrgb.rgb + metallic.a\n"
				"\t// RT1 (A8R8G8B8): octNormal.rg + roughness.b + specialFlag.a\n"
				"\t// RT2 (A8R8G8B8): depth.r + depth*depth.g + emissive.b + specialFlag.a\n"
				"\to.color0 = float4(albedo.rgb, metallic);\n"
				"\to.color1 = float4(encN, roughness, specialFlag);\n"
				"\to.color2 = float4(depth, depth * depth, emissive, specialFlag);\n"
				"\treturn o;\n"
				"};\n";
			ID3DXBuffer *compiled = NULL;
			ID3DXBuffer *errors = NULL;
			HRESULT hr = D3DXCompileShader(gbuffer_ps, (UINT)strlen(gbuffer_ps),
				NULL, NULL, "main", "ps_3_0",
				0, &compiled, &errors, NULL);
			if (FAILED(hr) || !compiled) {
				WWDEBUG_SAY(("W3DShaderManager: G-Buffer PS compile failed.\n"));
				if (errors) {
					WWDEBUG_SAY(("  error: %s\n", (const char*)errors->GetBufferPointer()));
					errors->Release();
				}
				g_gbufferPS = NULL;
			} else {
				IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
				if (dev) {
					hr = dev->CreatePixelShader(
						(const DWORD*)compiled->GetBufferPointer(), &g_gbufferPS);
				}
				compiled->Release();
				WWDEBUG_SAY(("W3DShaderManager: G-Buffer PS compiled (%p).\n", (void*)g_gbufferPS));
			}
		}
	}
	// Compile pass-through vertex shader (vs_1_1) for PBR unit rendering.
	// D3D9On12 requires D3DXAssembleShader for assembly source text --
	// it generates proper DCL instructions (dcl_position etc.) that the
	// D3D9On12 device-creation validator accepts.  D3DXCompileShader
	// is for HLSL source only and will fail on assembly text.
	// (See W3DWater.cpp for the same pattern.)
	if (!m_vsPBRUnit) {
		// FVF->VS register: v0=POS, v3=NORMAL, v7=TEXCOORD0
		const char vs_code[] =
			"vs.1.1\n"
			"m4x4 oPos, v0, c0\n"
			"mov oT0, v7\n"
			"mov oD0, v5\n"
			"m4x4 oT1, v0, c4\n"
			"m3x3 oT4, v3, c8\n";
		ID3DXBuffer *vsCompiled = NULL;
		ID3DXBuffer *vsErrors = NULL;
		HRESULT vsHR = D3DXAssembleShader(vs_code, (UINT)strlen(vs_code),
			NULL, NULL, 0, &vsCompiled, &vsErrors);
		if (FAILED(vsHR) || !vsCompiled) {
			if (vsErrors) { vsErrors->Release(); }
		} else {
			{
			IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
			if (dev) {
				vsHR = dev->CreateVertexShader(
					(const DWORD*)vsCompiled->GetBufferPointer(), &m_vsPBRUnit);
			}
			vsCompiled->Release();
		}
			if (FAILED(vsHR)) m_vsPBRUnit = NULL;
		}
	}
	g_pbrUnitVS = m_vsPBRUnit;
	// DIAG: log VS state from DLL side
	{
		static int once = 0;
		if (!once) { once = 1;
			FILE *f = fopen("E:\\terrain_diag.log", "a");
			if (f) { fprintf(f, "[%u] PBR_VS_DLL: m_vsPBRUnit=%p g_pbrUnitVS=%p\n", timeGetTime(), (void*)m_vsPBRUnit, (void*)g_pbrUnitVS); fclose(f); }
		}
	}

	// DIAG: log IBL initialization status
	{
		FILE *f = fopen("E:\\terrain_diag.log", "a");
		if (f) {
			fprintf(f, "[%u] PBR_IBL_INIT: irradiance=%s prefiltered=%s brdfLUT=%s hasIBL=%d hasSpecIBL=%d enabled=%d\n",
				timeGetTime(),
				m_envIrradianceMap && m_envIrradianceMap->Is_Initialized() ? "OK" : "MISS",
				m_envPrefilteredMap && m_envPrefilteredMap->Is_Initialized() ? "OK" : "MISS",
				m_brdfLUT && m_brdfLUT->Is_Initialized() ? "OK" : "MISS",
				(int)(m_envIrradianceMap != NULL && m_envIrradianceMap->Is_Initialized() && m_envIrradianceMap->Peek_D3D_CubeTexture() != NULL),
				(int)(m_envIrradianceMap != NULL && m_envIrradianceMap->Is_Initialized() && m_envIrradianceMap->Peek_D3D_CubeTexture() != NULL && m_envPrefilteredMap && m_envPrefilteredMap->Is_Initialized() && m_envPrefilteredMap->Peek_D3D_CubeTexture() != NULL && m_brdfLUT && m_brdfLUT->Is_Initialized() && m_brdfLUT->Peek_D3D_Texture() != NULL && m_dwPBRPixelShader_30_IBLSpec && m_dwPBRAlphaPixelShader_30_IBLSpec),
				(int)g_pbrUnitShaderEnabled);
			fclose(f);
		}
	}

	// ---- Sun Glow overlay shader (RA3-style) - HLSL version ----
	// Assembly ps.2.0 compiled OK but CreatePixelShader rejected on D3D9On12
	// with error 0x8876086C. HLSL path works (same as all other PBR shaders).
	{
		const char* src =
			"float3 c0 : register(c0);\n"
			"float3 c1 : register(c1);\n"
			"float4 main(float2 uv : TEXCOORD0) : COLOR\n"
			"{\n"
			"    float d = dot(uv, uv);\n"
			"    float3 r0 = float3(uv * rsqrt(d), 1.0 / d);\n"
			"    float NdotL = dot(r0, c0);\n"
			"    NdotL = max(NdotL, 1.0);\n"
			"    float intensity = pow(NdotL, 32.0)\n"
			"        + pow(max(NdotL + 0.3, 0.5), 128.0) * 0.5;\n"
			"    return float4(c1 * intensity, intensity);\n"
			"}\n";
		if (FAILED(compilePBRShader(src, &m_dwSunGlowShader, "sun_glow")))
			m_dwSunGlowShader = NULL;
		m_sunGlowEnabled = (m_dwSunGlowShader != NULL) ? TRUE : FALSE;
		DEBUG_LOG(("Sun Glow shader: %s  handle=0x%08X\n",
			m_sunGlowEnabled ? "COMPILED OK" : "FAILED",
			(UINT)(UINT_PTR)m_dwSunGlowShader));
		TerrainDiagI("sun_glow_compiled", (int)m_sunGlowEnabled);
	}

	return (m_dwPBRPixelShader != NULL || m_dwPBRPixelShader_30 != NULL) ? TRUE : FALSE;
}

Int W3DPBRShader::set(Int pass)
{
	if (!m_dwPBRPixelShader && !m_dwPBRPixelShader_30) return FALSE;
	W3DShaderManager::ShaderTypes curShader = W3DShaderManager::getCurrentShader();

	// Select shader variant based on alpha mode and PBR texture availability
	TextureClass *pbrTex = W3DShaderManager::getShaderTexture(2);
	bool hasPBRTex = (pbrTex && pbrTex->Peek_D3D_Texture());
	bool hasIBL = (m_envIrradianceMap != NULL && m_envIrradianceMap->Is_Initialized() && m_envIrradianceMap->Peek_D3D_CubeTexture() != NULL);
	bool hasSpecIBL = (hasIBL && m_envPrefilteredMap && m_envPrefilteredMap->Is_Initialized() && m_envPrefilteredMap->Peek_D3D_CubeTexture() != NULL && m_brdfLUT && m_brdfLUT->Is_Initialized() && m_brdfLUT->Peek_D3D_Texture() != NULL && m_dwPBRPixelShader_30_IBLSpec && m_dwPBRAlphaPixelShader_30_IBLSpec);
	// Use ps_3_0 shaders when compiled successfully
	// (D3DCAPS8 doesn't report ps_3_0, so we detect via shader compilation result)
	bool usePS30 = (m_dwPBRPixelShader_30 != NULL);
	IDirect3DPixelShader9* pShader = NULL;

	// DIAG: log shader selection state once
	{
		static Bool diagOnce = FALSE;
		if (!diagOnce) {
			FILE *f = fopen("E:\\terrain_diag.log", "a");
			if (f) {
				fprintf(f, "[%d] PBR_SEL: ps20=%p ps30=%p use30=%d hasIBL=%d hasSpecIBL=%d curShader=%d\n",
					timeGetTime(), m_dwPBRPixelShader, m_dwPBRPixelShader_30,
					(int)usePS30, (int)hasIBL, (int)hasSpecIBL, (int)curShader);
				fclose(f);
			}
			diagOnce = TRUE;
		}
	}

	if (curShader == W3DShaderManager::ST_PBR_UNIT_ALPHA) {
		if (usePS30) {
			if (hasSpecIBL && hasPBRTex)
				pShader = m_dwPBRAlphaPixelShader_30_IBLSpec;
			else if (hasIBL && hasPBRTex && m_dwPBRAlphaPixelShader_30_IBL)
				pShader = m_dwPBRAlphaPixelShader_30_IBL;
			else if (hasPBRTex && m_dwPBRAlphaPixelShader_30)
				pShader = m_dwPBRAlphaPixelShader_30;
		}
		if (!pShader) {
			if (hasPBRTex && m_dwPBRAlphaPixelShader)
				pShader = m_dwPBRAlphaPixelShader;
			else if (!hasPBRTex) {
				if (hasSpecIBL && m_dwPBRAlphaPixelShaderNT_30_IBLSpec)
					pShader = m_dwPBRAlphaPixelShaderNT_30_IBLSpec;
				else if (hasIBL && m_dwPBRAlphaPixelShaderNT_30_IBL)
					pShader = m_dwPBRAlphaPixelShaderNT_30_IBL;
				else if (hasIBL && m_dwPBRAlphaPixelShaderNT_IBL)
					pShader = m_dwPBRAlphaPixelShaderNT_IBL;
				else if (m_dwPBRAlphaPixelShaderNT_30)
					pShader = m_dwPBRAlphaPixelShaderNT_30;
				else if (m_dwPBRAlphaPixelShaderNT)
					pShader = m_dwPBRAlphaPixelShaderNT;
			}
			else if (m_dwPBRAlphaPixelShader)
				pShader = m_dwPBRAlphaPixelShader;
		}
	}

	// Fallback: use opaque variant (ps_3_0 first if available)
	if (!pShader) {
		if (usePS30) {
			if (hasSpecIBL && hasPBRTex)
				pShader = m_dwPBRPixelShader_30_IBLSpec;
			else if (hasIBL && hasPBRTex && m_dwPBRPixelShader_30_IBL)
				pShader = m_dwPBRPixelShader_30_IBL;
			else if (hasPBRTex && m_dwPBRPixelShader_30)
				pShader = m_dwPBRPixelShader_30;
		}
		if (!pShader) {
			if (hasPBRTex && m_dwPBRPixelShader)
				pShader = m_dwPBRPixelShader;
			else if (!hasPBRTex) {
				if (hasSpecIBL && m_dwPBRPixelShaderNT_30_IBLSpec)
					pShader = m_dwPBRPixelShaderNT_30_IBLSpec;
				else if (hasIBL && m_dwPBRPixelShaderNT_30_IBL)
					pShader = m_dwPBRPixelShaderNT_30_IBL;
				else if (hasIBL && m_dwPBRPixelShaderNT_IBL)
					pShader = m_dwPBRPixelShaderNT_IBL;
				else if (m_dwPBRPixelShaderNT_30)
					pShader = m_dwPBRPixelShaderNT_30;
				else if (m_dwPBRPixelShaderNT)
					pShader = m_dwPBRPixelShaderNT;
			}
			else if (m_dwPBRPixelShader)
				pShader = m_dwPBRPixelShader;
		}
	}

	DX8Wrapper::Apply_Render_State_Changes();

	// Set up texture stage 0 (albedo) from shader texture slot
	TextureClass *albedoTex = W3DShaderManager::getShaderTexture(0);
	if (albedoTex && albedoTex->Peek_D3D_Texture()) {
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, albedoTex->Peek_D3D_Texture());
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	}

	// Set up texture stage 1 (Normal Map) from shader texture slot (Phase 5)
	{
		TextureClass *normalTex = W3DShaderManager::getShaderTexture(1);
		if (normalTex && normalTex->Peek_D3D_Texture()) {
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, normalTex->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, 0);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		}
	}

	// Set up texture stage 2 (PBR) from shader texture slot
	if (pbrTex && pbrTex->Peek_D3D_Texture()) {
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, pbrTex->Peek_D3D_Texture());
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, 0);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	}

	// Set alpha blend states for alpha pass
	if (curShader == W3DShaderManager::ST_PBR_UNIT_ALPHA) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	} else {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	}

	// Set up texture stage 3 (CubeMap) for IBL environment lighting
	if (hasIBL && m_envIrradianceMap && m_envIrradianceMap->Is_Initialized()) {
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, m_envIrradianceMap->Peek_D3D_CubeTexture());
	}
	// Set up texture stage 4 (pre-filtered CubeMap) for specular IBL
	if (hasSpecIBL && m_envPrefilteredMap && m_envPrefilteredMap->Is_Initialized()) {
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, m_envPrefilteredMap->Peek_D3D_CubeTexture());
	}
	// Set up texture stage 5 (BRDF LUT) for specular IBL
	if (hasSpecIBL && m_brdfLUT && m_brdfLUT->Is_Initialized() && m_brdfLUT->Peek_D3D_Texture()) {
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(5, m_brdfLUT->Peek_D3D_Texture());
	}
	// c11 = PBR debug visualization mode (0=off, always set to avoid stale register)
	{
	//	float dbg[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // debug mode off
	float dbg[4] = { (float)TheGlobalData->m_pbrDebugMode, 0.0f, 0.0f, 0.0f };
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(11, dbg, 1);
	}
	// Guard: if no shader was successfully compiled (e.g. after device reset failure),
	// return FALSE so the renderer skips this draw call instead of passing NULL to SetPixelShader.
	if (pShader == NULL) {
		DEBUG_LOG(("PBR: set() called but no shader available (device reset recovery pending)\n"));
		return FALSE;
	}
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(pShader);
	return TRUE;
}

void W3DPBRShader::reset(void)
{
	ShaderClass::Invalidate();
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(5, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
}

// ---- PBR exclusion list for specific mesh names ----
// Meshes registered here will skip PBR_BindVS() entirely.
// Add new entries to the initializer list; call PBR_RegisterExcludedMesh()
// at runtime for dynamic additions.
#define PBR_MAX_EXCLUDED_MESHES 32
static const char *s_pbrExcludedMeshes[PBR_MAX_EXCLUDED_MESHES] = {
	"0qsnwateryy1",		// water mirror surface -- terrain visual decal
	"bloombox_r",		// bloom/blur post-processing quad
	"bloombox_rx",		// bloom/blur variant
	"qingwaddskybox",	// skybox flat model -- terrain visual
	"qsnboxmorning",	// skybox flat model -- terrain visual
};
static int s_pbrExcludedMeshCount = 5;

extern "C" void PBR_RegisterExcludedMesh(const char *meshName)
{
	if (!meshName || s_pbrExcludedMeshCount >= PBR_MAX_EXCLUDED_MESHES) return;
	// strdup-allocated entries (indices >= s_pbrExcludedMeshCount at the
	// time of call) are owned by this array. Entries from the static
	// initializer (indices 0-4 currently) are string literals and must
	// NOT be freed.
	s_pbrExcludedMeshes[s_pbrExcludedMeshCount++] = _strdup(meshName);
}

extern "C" bool PBR_IsMeshExcluded(const char *meshName)
{
	if (!meshName) return false;
	for (int i = 0; i < s_pbrExcludedMeshCount; i++) {
		if (_stricmp(meshName, s_pbrExcludedMeshes[i]) == 0) return true;
	}
	return false;
}


Int W3DPBRShader::shutdown(void)
{
	if (m_dwPBRPixelShader) { m_dwPBRPixelShader->Release(); m_dwPBRPixelShader = NULL; }
	if (m_dwPBRAlphaPixelShader) { m_dwPBRAlphaPixelShader->Release(); m_dwPBRAlphaPixelShader = NULL; }
	if (m_dwPBRPixelShaderNT) { m_dwPBRPixelShaderNT->Release(); m_dwPBRPixelShaderNT = NULL; }
	if (m_dwPBRAlphaPixelShaderNT) { m_dwPBRAlphaPixelShaderNT->Release(); m_dwPBRAlphaPixelShaderNT = NULL; }
	if (m_dwPBRPixelShader_30) { m_dwPBRPixelShader_30->Release(); m_dwPBRPixelShader_30 = NULL; }
	if (m_dwPBRAlphaPixelShader_30) { m_dwPBRAlphaPixelShader_30->Release(); m_dwPBRAlphaPixelShader_30 = NULL; }
	if (m_dwPBRPixelShader_30_IBL) { m_dwPBRPixelShader_30_IBL->Release(); m_dwPBRPixelShader_30_IBL = NULL; }
	if (m_dwPBRAlphaPixelShader_30_IBL) { m_dwPBRAlphaPixelShader_30_IBL->Release(); m_dwPBRAlphaPixelShader_30_IBL = NULL; }
	if (m_dwPBRPixelShader_30_IBLSpec) { m_dwPBRPixelShader_30_IBLSpec->Release(); m_dwPBRPixelShader_30_IBLSpec = NULL; }
	if (m_dwPBRAlphaPixelShader_30_IBLSpec) { m_dwPBRAlphaPixelShader_30_IBLSpec->Release(); m_dwPBRAlphaPixelShader_30_IBLSpec = NULL; }
	if (m_dwPBRPixelShaderNT_IBL) { m_dwPBRPixelShaderNT_IBL->Release(); m_dwPBRPixelShaderNT_IBL = NULL; }
	if (m_dwPBRAlphaPixelShaderNT_IBL) { m_dwPBRAlphaPixelShaderNT_IBL->Release(); m_dwPBRAlphaPixelShaderNT_IBL = NULL; }
	if (m_dwPBRPixelShaderNT_30_IBLSpec) { m_dwPBRPixelShaderNT_30_IBLSpec->Release(); m_dwPBRPixelShaderNT_30_IBLSpec = NULL; }
	if (m_dwPBRAlphaPixelShaderNT_30_IBLSpec) { m_dwPBRAlphaPixelShaderNT_30_IBLSpec->Release(); m_dwPBRAlphaPixelShaderNT_30_IBLSpec = NULL; }
	if (m_dwPBRPixelShaderNT_30) { m_dwPBRPixelShaderNT_30->Release(); m_dwPBRPixelShaderNT_30 = NULL; }
	if (m_dwPBRAlphaPixelShaderNT_30) { m_dwPBRAlphaPixelShaderNT_30->Release(); m_dwPBRAlphaPixelShaderNT_30 = NULL; }
	if (m_dwPBRPixelShaderNT_30_IBL) { m_dwPBRPixelShaderNT_30_IBL->Release(); m_dwPBRPixelShaderNT_30_IBL = NULL; }
	if (m_dwPBRAlphaPixelShaderNT_30_IBL) { m_dwPBRAlphaPixelShaderNT_30_IBL->Release(); m_dwPBRAlphaPixelShaderNT_30_IBL = NULL; }
	if (m_envIrradianceMap) { REF_PTR_RELEASE(m_envIrradianceMap); }
	if (m_envPrefilteredMap) { REF_PTR_RELEASE(m_envPrefilteredMap); }
	if (m_brdfLUT) { REF_PTR_RELEASE(m_brdfLUT); }
	if (m_vsPBRUnit) { m_vsPBRUnit->Release(); m_vsPBRUnit = NULL; }
	if (m_dwSunGlowShader) {
		DEBUG_LOG(("Sun Glow shader: RELEASED\n"));
		TerrainDiag("sun_glow_released");
		m_dwSunGlowShader->Release(); m_dwSunGlowShader = NULL;
	}
	m_sunGlowEnabled = FALSE;
	if (g_gbufferVS) { g_gbufferVS->Release(); g_gbufferVS = NULL; }
		if (g_gbufferPS) { g_gbufferPS->Release(); g_gbufferPS = NULL; }

	g_pbrUnitOpaqueShader = NULL;
	g_pbrUnitAlphaShader = NULL;
	g_pbrUnitOpaqueNTShader = NULL;
	g_pbrUnitAlphaNTShader = NULL;
	g_pbrUnitShaderEnabled = FALSE;
	g_pbrUnitVS = NULL;

	// Free _strdup-allocated exclusion list entries (indices >= static count)
	for (int i = 5; i < s_pbrExcludedMeshCount; i++) {
		if (s_pbrExcludedMeshes[i]) {
			free((void*)s_pbrExcludedMeshes[i]);
			s_pbrExcludedMeshes[i] = NULL;
		}
	}
	s_pbrExcludedMeshCount = 5;	// reset to static initializer count
	return TRUE;
}

Int TerrainShaderPixelShader::init( void )
{	
	Int res;
#ifdef DISABLE_PIXEL_SHADERS
	return false;
#endif
	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (terrainShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//base version which doesn't apply any noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrain.pso", NULL, 0, false, (void**)&m_dwBasePixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 1 noise texture.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise.pso", NULL, 0, false, (void**)&m_dwBaseNoise1PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 2 noise textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\terrainnoise2.pso", NULL, 0, false, (void**)&m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=1;
			W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

Int TerrainShaderPixelShader::set(Int pass)
{	
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	//setup base pass
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}

	if (W3DShaderManager::getCurrentShader() >= W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
	{	
		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		
		if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
		{	//full shader
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(3, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBaseNoise2PixelShader);

			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

			terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);

			terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE3, curView);

			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(3,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
		}
		else
		{	//single noise texture shader
			DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBaseNoise1PixelShader);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
			{	//cloud map
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			}
			else
			{	//light map
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
				terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			}
			DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);
		}
	}
	else
	{	//just base texturing
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBasePixelShader);
	}

	return TRUE;
}

void TerrainShaderPixelShader::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2,NULL);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3,NULL);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(5, NULL);

	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);	//turn off pixel shader

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}

///Cloud layer rendering shader - used for objects similar to terrain which only need the cloud layer.
class CloudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int stage);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} cloudTextureShader;

///List of different cloud shader implementations in order of preference
W3DShaderInterface *CloudShaderList[]=
{
	&cloudTextureShader,
	NULL
};

Int CloudTextureShader::init(void)
{
	W3DShaders[W3DShaderManager::ST_CLOUD_TEXTURE]=&cloudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_CLOUD_TEXTURE]=1;

	return TRUE;
}

/**Setup a certain texture stage to project our cloud texture*/
Int CloudTextureShader::set(Int stage)
{
	Matrix4x4 curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	D3DXMATRIX inv;
	float det;

	D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

	//Get a texture matrix that applies the current cloud position
	terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv,false);	//update curView with texture matrix

	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
	DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
	DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(stage, W3DShaderManager::getShaderTexture(stage)->Peek_D3D_Texture());

	m_stageOfSet=stage;
	return TRUE;
}

void CloudTextureShader::reset(void)
{
	//Free reference to texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(m_stageOfSet, NULL);
	//Turn off texture projection
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|m_stageOfSet);

	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( m_stageOfSet, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
}

/*===========================================================================================*/
/*=========      Road Shaders	=========================================================*/
/*===========================================================================================*/
class RoadShaderPixelShader : public W3DShaderInterface
{
	IDirect3DPixelShader9*	m_dwBaseNoise2PixelShader;	///<handle to road/double noise D3D pixel shader

	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual void reset(void);		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual Int shutdown(void);			///<release resources used by shader
} roadShaderPixelShader;

class RoadShader2Stage : public W3DShaderInterface
{	friend class RoadShaderPixelShader;	//pixel shader version uses some of the same features.
	friend class RoadShaderPBR;			//PBR shadow-receive version reuses init()/set() as fallback + noise helpers.

	virtual Int set(Int pass);		///<setup shader for the specified rendering pass.
	virtual Int init(void);			///<perform any one time initialization and validation
	virtual void reset(void);
} roadShader2Stage;

// 2026-09-10 ROAD SHADOW-MAP RECEIVE (road fix, plan A):
// Roads were the ONLY ground surface left on the 2003 fixed-function
// RoadShader2Stage path (base*diffuse + cloud/lightmap TSS modulation), so a
// W3X model's texture shadow visibly stopped at the road edge while the
// terrain right next to it was darkened. This shader re-renders the SAME
// road geometry with a ps_3_0 pixel shader that replicates the fixed-function
// blend semantics (pass-0 base*diffuse, cloud modulate; the NOISE12 second
// lightmap pass becomes an in-shader lerp(1,lightmap,roadAlpha)) and
// multiplies in the SAME 9-tap PCF terrainShadow() TerrainShaderPBR uses.
// Mechanism is copied verbatim from the PROVEN terrain receive: shadow UVZ
// arrives per-vertex via TSS stage7 (TCI_CAMERASPACEPOSITION x
// InvView*SunVP*Bias, COUNT3), the shadow map is the A8R8G8B8 CPU copy on
// sampler s4, c7 = {texel, texel, depth bias, receive-enable}. Cloud and
// lightmap keep their fixed-function TCI_CAMERASPACEPOSITION stage1/stage2
// projections (the rasterizer feeds them to the PS as TEXCOORD1/2), so the
// projected cloud drift is pixel-identical to the old road look.
class RoadShaderPBR : public W3DShaderInterface
{
public:
	IDirect3DPixelShader9*	m_dwRoadPixelShader;	///<ps_3_0 road + shadow-receive pixel shader
	virtual Int set(Int pass);
	virtual void reset(void);
	virtual Int init(void);
	virtual Int shutdown(void);
} roadShaderPBR;

///List of different terrain shader implementations in order of preference
W3DShaderInterface *RoadShaderList[]=
{
	&roadShaderPBR,
	&roadShaderPixelShader,
	&roadShader2Stage,
	NULL
};

Int RoadShaderPixelShader::shutdown(void)
{
	if (m_dwBaseNoise2PixelShader)
				m_dwBaseNoise2PixelShader->Release();

	m_dwBaseNoise2PixelShader=NULL;

	return TRUE;
}

Int RoadShaderPixelShader::init( void )
{	
	Int res;

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if (roadShader2Stage.init() && (res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//version which blends 2 noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\roadnoise2.pso", NULL, 0, false, (void**)&m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			//Only set this shader for use in dual noise mode.  The 2Stage shader will take care of
			//all the other modes.
			W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

Int RoadShaderPixelShader::set(Int pass)
{
	DX8Wrapper::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);	//blend roads into terrain
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

	Matrix4x4 curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	D3DXMATRIX inv;
	float det;
	D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
	{	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}
	else
	{	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	// Two output coordinates are used.
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	
	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
	DX8Wrapper::Set_Texture(2,W3DShaderManager::getShaderTexture(2));

	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBaseNoise2PixelShader);

	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv, false);	//get texture projection matrix
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);

	terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv, false);	//get texture projection matrix
	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, curView);

	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	// Two output coordinates are used.
	DX8Wrapper::Set_DX8_Texture_Stage_State(2,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

	return TRUE;
}

void RoadShaderPixelShader::reset(void)
{

	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);	//turn off pixel shader

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int RoadShader2Stage::init( void )
{
	//no special device validation needed - anything in our min spec should handle this.
	W3DShaders[W3DShaderManager::ST_ROAD_BASE]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE1]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE1]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE2]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE2]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_BASE_NOISE12]=&roadShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_BASE_NOISE12]=2;

	return TRUE;
}

Int RoadShader2Stage::set(Int pass)																											  
{
	//First stage always contains base texture.
	DX8Wrapper::Set_Texture(0,W3DShaderManager::getShaderTexture(0));
	//Force system to apply world/view transforms.
	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);

	// Modulate the diffuse color with the texture as lighting comes from diffuse.
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);	//blend roads into terrain

	if (pass == 0)
	{	
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

		if (W3DShaderManager::getCurrentShader() >= W3DShaderManager::ST_ROAD_BASE_NOISE1)
		{	//second texture unit will contain a noise pass
			Matrix4x4 curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			D3DXMATRIX inv;
			float det;
			D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

			if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
			else
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);

			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE12)
			{	//full shader, apply noise 1 in pass 0.
				DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv, false);	//get texture projection matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
			}
			else
			{	//single noise texture shader
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_ROAD_BASE_NOISE1)
				{	//cloud map
					DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(1));
					terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv, false);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{	//light map
					DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(2));
					terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv, false);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
			}
		}
		else
		{	//just base texturing
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
		}
	}	//pass 0
	else
	{	//pass 1, apply additional noise pass
		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex)
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		else
			DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);

		DX8Wrapper::Set_Texture(1,W3DShaderManager::getShaderTexture(2));

		terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv, false);	//update curView with texture matrix
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

		//Copy alpha channel into stage 1 but mask out color channel by replacing with white.
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		//Force color channel to white by copying the alpha into RGB
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE|D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1 );

		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_BLENDCURRENTALPHA);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

		//Modulate into existing roads with clouds applied. - only apply where roads are transparent by
		//using road texture as a mask.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_ZERO);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);

		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
	}

	return TRUE;
}

void RoadShader2Stage::reset(void)
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}

Int RoadShaderPBR::init( void )
{
	D3DCAPS8 caps;
	memset(&caps, 0, sizeof(caps));
	if (FAILED(DX8Wrapper::_Get_D3D_Device8()->GetDeviceCaps(&caps)) ||
		caps.PixelShaderVersion < D3DPS_VERSION(3,0))
	{
		// ps_3_0 required for the shadow-map sample (same constraint as the
		// terrain PBR receive) - fall through to the legacy road shaders.
		return FALSE;
	}

	// 2Stage must initialize anyway: its updateNoise1/updateNoise2 helpers
	// build the cloud/lightmap projection matrices we reuse below.
	roadShader2Stage.init();

	m_dwRoadPixelShader = NULL;
	{
		// Single shader serves all four variants: c2.x = cloud on, c2.y =
		// lightmap on. TEXCOORD1/2 come from the SAME fixed-function TCI
		// projections the legacy road pass used (stage1/stage2 view-matrix),
		// TEXCOORD7 from the terrain-proven shadow projection (stage7).
		const char* src =
			"sampler s0 : register(s0);\n"		// road base (uv0)
			"sampler s1 : register(s1);\n"		// cloud (TEXCOORD1, projected)
			"sampler s2 : register(s2);\n"		// lightmap (TEXCOORD2, projected)
			"sampler s4 : register(s4);\n"		// shadow map A8R8G8B8 CPU copy
			"float4 c2 : register(c2);\n"		// x=useCloud y=useLightmap
			"float4 c7 : register(c7);\n"		// x,y = shadow texel, z = depth bias, w = receive enable
			"float4 c8 : register(c8);\n"		// x = PBRDebugMode (>=23.5 = raw shadow viz)
			"float roadShadow(float3 uvz)\n"
			"{\n"
			"    float2 suv = uvz.xy;\n"
			"    float sd = uvz.z - c7.z;\n"
			// 9-tap spatial kernel @2 texels + RA3 continuous transition band -
			// verbatim from TerrainShaderPBR::terrainShadow (2026-09-10 v3) so
			// road and terrain shadows have IDENTICAL edges and softening.
			"    float2 ts = float2(c7.x, c7.y) * 2.0;\n"
			"    float f = 0.0;\n"
			"    f += saturate((sd - tex2D(s4, suv).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, -ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, 0.0)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(-ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(0.0, ts.y)).r) * 256.0 + 1.0);\n"
			"    f += saturate((sd - tex2D(s4, suv + float2(ts.x, ts.y)).r) * 256.0 + 1.0);\n"
			"    float sun = 1.0 - f * (1.0 / 9.0);\n"
			"    float lit = 0.3 + 0.7 * sun;\n"
			"    float inB = (suv.x > 0.001 && suv.x < 0.999 && suv.y > 0.001 && suv.y < 0.999) ? 1.0 : 0.0;\n"
			"    return lerp(1.0, lit, inB * c7.w);\n"
			"}\n"
			"float4 main(float2 uv0 : TEXCOORD0, float4 diffuse : COLOR0,\n"
			"           float2 uvCloud : TEXCOORD1, float2 uvLM : TEXCOORD2,\n"
			"           float3 shadowUVZ : TEXCOORD7) : COLOR\n"
			"{\n"
			"    float4 base = tex2D(s0, uv0);\n"
			// legacy pass-0 semantics: color AND alpha modulate diffuse
			"    float3 col = base.rgb * diffuse.rgb;\n"
			"    float alpha = base.a * diffuse.a;\n"
			"    if (c2.x > 0.5) { col *= tex2D(s1, uvCloud).rgb; }\n"
			// legacy NOISE12 pass-1: dest *= lerp(1, lightmap, roadAlpha)
			"    if (c2.y > 0.5) { col *= lerp(float3(1,1,1), tex2D(s2, uvLM).rgb, alpha); }\n"
			"    float shadow = roadShadow(shadowUVZ);\n"
			// NO viz branch: terrain's final terrainShadow() ignores the
			// PBRDebugMode chain (2026-09-08 HARDCODED bypass) and a leftover
			// PBRDebugMode=27 in GameData.ini made roads render as the raw
			// shadow grayscale (white + shadows, no texture). Match terrain.
			"    return float4(col * shadow, alpha);\n"
			"}\n";
		if (FAILED(compilePBRShader(src, &m_dwRoadPixelShader, "road_pbr_shadow", "ps_3_0")))
			return FALSE;
	}
	if (m_dwRoadPixelShader == NULL)
		return FALSE;

	// Register ONLY the new ST_ROAD_PBR* slots; ST_ROAD_BASE* stay on the
	// legacy shaders so the call sites' fallback keeps working.
	W3DShaders[W3DShaderManager::ST_ROAD_PBR]=&roadShaderPBR;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE1]=&roadShaderPBR;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE1]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE2]=&roadShaderPBR;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE2]=1;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE12]=&roadShaderPBR;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE12]=1;
	m_numPasses = 1;
	return TRUE;
}

Int RoadShaderPBR::set(Int pass)
{
	W3DShaderManager::ShaderTypes curShader = W3DShaderManager::getCurrentShader();
	if (curShader < W3DShaderManager::ST_ROAD_PBR || curShader > W3DShaderManager::ST_ROAD_PBR_NOISE12) {
		return roadShader2Stage.set(pass);
	}
	if (m_dwRoadPixelShader == NULL) {
		return roadShader2Stage.set(pass);
	}

	//Base road texture on stage 0 with vertex UV set 0 (CLAMP like terrain base).
	// 2026-09-10 BLACK-ROAD FIX: bind with a RAW device call like TerrainShaderPBR
	// does - the wrapper-only Set_Texture + replay left sampler s0 EMPTY when a
	// pixel shader reads it (terrain hit the same wall: its comment records the
	// material replay swapping stages; raw SetTexture is the proven path). The
	// wrapper call stays for stage bookkeeping (matches terrain's s4 dance).
	{
		TextureClass *roadTex0 = W3DShaderManager::getShaderTexture(0);
		if (roadTex0) {
			DX8Wrapper::Set_Texture(0, roadTex0);
			DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, roadTex0->Peek_D3D_Texture());
		}
	}
	DX8Wrapper::Apply_Render_State_Changes();

	//Same depth/blend states as the legacy road pass 0 - roads still alpha-
	//blend INTO the terrain and never write z.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	Matrix4x4 curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);
	D3DXMATRIX inv;
	float det;
	D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

	float useCloud = 0.0f;
	float useLightmap = 0.0f;

	//Stage 1: cloud projection - identical TSS setup to the legacy NOISE1/
	//NOISE12 pass 0 (TCI_CAMERASPACEPOSITION x view matrix, COUNT2, WRAP).
	if (curShader == W3DShaderManager::ST_ROAD_PBR_NOISE1 || curShader == W3DShaderManager::ST_ROAD_PBR_NOISE12) {
		useCloud = 1.0f;
		// RAW bind (see BLACK-ROAD FIX above): an empty s1 multiplies the road
		// to black (cloud.rgb=0), same wrapper-replay failure mode as s0.
		{
			TextureClass *cloudTex = W3DShaderManager::getShaderTexture(1);
			if (cloudTex) {
				DX8Wrapper::Set_Texture(1, cloudTex);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, cloudTex->Peek_D3D_Texture());
			}
		}
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		Matrix4x4 viewCopy = curView;
		terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&viewCopy), &inv, false);
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, viewCopy);
	}

	//Stage 2: lightmap projection - the legacy NOISE12 second pass, done
	//in-shader now (single pass); NOISE2 uses it alone.
	if (curShader == W3DShaderManager::ST_ROAD_PBR_NOISE2 || curShader == W3DShaderManager::ST_ROAD_PBR_NOISE12) {
		useLightmap = 1.0f;
		// RAW bind (see BLACK-ROAD FIX above).
		{
			TextureClass *lmTex = W3DShaderManager::getShaderTexture(2);
			if (lmTex) {
				DX8Wrapper::Set_Texture(2, lmTex);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, lmTex->Peek_D3D_Texture());
			}
		}
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		Matrix4x4 viewCopy2 = curView;
		terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&viewCopy2), &inv, false);
		DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE2, viewCopy2);
	}

	//c2 = { cloud, lightmap, 0, 0 }
	float c2v[4] = { useCloud, useLightmap, 0.0f, 0.0f };
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(2, c2v, 1);

	//Shadow receive: bind the A8R8G8B8 CPU copy on s4 + stage7 UVZ projection,
	//byte-for-byte the TerrainShaderPBR receive (its s_shWrap dance included:
	//wrapping the raw D3D9 tex through TextureClass keeps the DX8-wrapper's
	//stage bookkeeping in sync with the raw SetTexture below).
	{
		static bool s_roadReceiveEnable = true;
		bool shadowReceive = s_roadReceiveEnable && TheGlobalData && TheGlobalData->m_useShadowMap
			&& g_theW3DDeferredRenderer && g_theW3DDeferredRenderer->isShadowMapAvailable()
			&& g_theW3DDeferredRenderer->getShadowCpuTexture() != NULL;
		float sc7[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (shadowReceive) {
			IDirect3DBaseTexture9 *shTex9 = g_theW3DDeferredRenderer->getShadowCpuTexture();
			static TextureClass *s_shWrap = NULL;
			static IDirect3DBaseTexture9 *s_shWrapSrc = NULL;
			if (shTex9 && s_shWrapSrc != shTex9) {
				if (s_shWrap) { delete s_shWrap; s_shWrap = NULL; }
				s_shWrap = NEW TextureClass((IDirect3DBaseTexture8*)shTex9);
				s_shWrapSrc = shTex9;
			}
			if (s_shWrap) {
				DX8Wrapper::Set_Texture(4, s_shWrap);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, shTex9);
			} else {
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, shTex9);
			}
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_MAGFILTER, D3DTEXF_POINT);

			Matrix4x4 svp = g_theW3DDeferredRenderer->getShadowViewProj();
			// V-FLIP bias matrix - identical to the terrain receive (_22=-0.5:
			// the cast RT is standard-rasterized, ndc.y=+1 is the TOP row).
			D3DXMATRIX mBias;
			mBias._11 = 0.5f; mBias._12 = 0.0f; mBias._13 = 0.0f; mBias._14 = 0.0f;
			mBias._21 = 0.0f; mBias._22 = -0.5f; mBias._23 = 0.0f; mBias._24 = 0.0f;
			mBias._31 = 0.0f; mBias._32 = 0.0f; mBias._33 = 1.0f; mBias._34 = 0.0f;
			mBias._41 = 0.5f; mBias._42 = 0.5f; mBias._43 = 0.0f; mBias._44 = 1.0f;
			D3DXMATRIX mSunVP;
			memcpy(&mSunVP, &svp, sizeof(D3DXMATRIX));
			D3DXMATRIX mShadowUVZ, mTmp;
			D3DXMatrixMultiply(&mTmp, &inv, &mSunVP);		// InvView * SunVP
			D3DXMatrixMultiply(&mShadowUVZ, &mTmp, &mBias);	// ... * Bias
			DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
			DX8Wrapper::_Get_D3D_Device8()->SetTransform(D3DTS_TEXTURE7, &mShadowUVZ);

			// Match the terrain receive constants: fp16-RT-era bias 0.001.
			sc7[0] = 1.0f / 2048.0f;
			sc7[1] = 1.0f / 2048.0f;
			sc7[2] = 0.001f;
			sc7[3] = 1.0f;
			{ static int s_roadRecvN = 0; if ((s_roadRecvN++ % 600) == 0) {
				FILE *f = fopen("E:\\pbr_compile.log", "a");
				if (f) { fprintf(f, "[%d] ROAD-RECV: on=1 c7=(%.5f,%.5f,%.4f,%.1f)\n",
					(int)timeGetTime(), sc7[0], sc7[1], sc7[2], sc7[3]); fclose(f); } } }
		} else {
			{ static int s_roadRecv0 = 0; if ((s_roadRecv0++ % 6000) == 0) {
				FILE *f = fopen("E:\\pbr_compile.log", "a");
				if (f) { fprintf(f, "[%d] ROAD-RECV: on=0 (gate false)\n", (int)timeGetTime()); fclose(f); } } }
		}
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(7, sc7, 1);
	}

	//c8.x = PBRDebugMode, uploaded UNCONDITIONALLY (stale-value lesson from
	//the terrain shader: a garbage c8 blacks out everything).
	float sdbg[4] = { TheGlobalData ? (float)TheGlobalData->m_pbrDebugMode : 0.0f, 0.0f, 0.0f, 0.0f };
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShaderConstantF(8, sdbg, 1);

	// DEVICE TRUTH PROBE (throttled): what s0/s1/s4 ACTUALLY hold at set()
	// exit - if the road renders black again, a NULL here names the culprit
	// stage immediately (compare against the next draw's post-replay state).
	{ static int s_roadTexN = 0; if ((s_roadTexN++ % 600) == 0) {
		IDirect3DDevice9 *d9q = static_cast<IDirect3DDevice9*>(DX8Wrapper::_Get_D3D_Device8());
		IDirect3DBaseTexture9 *r0 = NULL, *r1 = NULL, *r4 = NULL;
		d9q->GetTexture(0, &r0); d9q->GetTexture(1, &r1); d9q->GetTexture(4, &r4);
		FILE *f = fopen("E:\\pbr_compile.log", "a");
		if (f) { fprintf(f, "[%d] ROAD-TEX: s0=%p s1=%p s4=%p variant=%d (0=NULL!)\n",
			(int)timeGetTime(), (void*)r0, (void*)r1, (void*)r4, (int)(curShader - W3DShaderManager::ST_ROAD_PBR));
			fclose(f); }
		if (r0) r0->Release();
		if (r1) r1->Release();
		if (r4) r4->Release();
	} }

	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwRoadPixelShader);
	return TRUE;
}

void RoadShaderPBR::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);
	ShaderClass::Invalidate();
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::Set_Texture(4, NULL);
	//Unhook the shadow UVZ projection so later passes get plain passthrough.
	DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU | 7);
	DX8Wrapper::_Get_D3D_Device8()->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);
	DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(4, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|4);
}

Int RoadShaderPBR::shutdown(void)
{
	if (m_dwRoadPixelShader) {
		m_dwRoadPixelShader->Release();
		m_dwRoadPixelShader = NULL;
	}
	W3DShaders[W3DShaderManager::ST_ROAD_PBR]=NULL;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR]=0;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE1]=NULL;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE1]=0;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE2]=NULL;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE2]=0;
	W3DShaders[W3DShaderManager::ST_ROAD_PBR_NOISE12]=NULL;
	W3DShadersPassCount[W3DShaderManager::ST_ROAD_PBR_NOISE12]=0;
	return TRUE;
}

/** List of all custom shader lists - each list in this list contains variations of the same
	shader to allow it to work on different hardware configurations.
*/
W3DShaderInterface **MasterShaderList[]=
{
	TerrainShaderList,
	ShroudShaderList,
	FlatShroudShaderList,
	RoadShaderList,
	MaskShaderList,
	CloudShaderList,
	FlatTerrainShaderList,
	PBRShaderList,
	NULL
};

/** List of all custom filter lists - each list in this list contains variations of the same
	filter to allow it to work on different hardware configurations.
*/
W3DFilterInterface **MasterFilterList[]=
{
	ScreenDefaultFilterList,
	ScreenBWFilterList,
	ScreenMotionBlurFilterList,
	ScreenCrossFadeFilterList,
	NULL
};

// W3DShaderManager::W3DShaderManager =========================================
/** Constructor - just clears some variables */
//=============================================================================
W3DShaderManager::W3DShaderManager(void)
{
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	m_oldRenderSurface = NULL;
	m_renderTexture = NULL;
	m_newRenderSurface = NULL;
	m_oldDepthSurface = NULL;
	m_renderingToTexture = false;
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++)
	{	W3DShaders[i]=NULL;
		W3DShadersPassCount[i]=0;
	}
	for (i=0; i<FT_MAX; i++)
	{	W3DFilters[i]=NULL;
	}
	for (i=0; i<8; i++)
	{
		m_Textures[i]=NULL;
	}
	m_currentShader=(W3DShaderManager::ShaderTypes)-1;
}

// W3DShaderManagerCleanupHook ============================================
// Cleanup-hook adapter so W3DShaderManager (an all-static class) can take
// part in the device reset chain.  Re-acquire relies on the existing lazy
// init(): it only runs when the surfaces are still missing, so the
// BaseHeightMap hook's own shutdown()/init() sequence is not disturbed.
//=============================================================================
class W3DShaderManagerCleanupHook : public DX8_CleanupHook
{
public:
	virtual void ReleaseResources(void) { W3DShaderManager::releaseDeviceResources(); }
	virtual void ReAcquireResources(void)
	{
		if (!W3DShaderManager::canRenderToTexture()) {
			W3DShaderManager::init();
		}
	}
};
static W3DShaderManagerCleanupHook _shaderManagerCleanupHook;

// W3DShaderManager::init =======================================================
/** Walk through all shaders and find versions suitable for current hardware */
//=============================================================================
void W3DShaderManager::init(void)
{
	int i,j;

	// Defensive: init() may legitimately run twice after one device reset
	// (cleanup-hook adapter + BaseHeightMap re-acquire).  Drop any surfaces
	// from a previous run first so they cannot leak in D3DPOOL_DEFAULT.
	releaseDeviceResources();

	D3DSURFACE_DESC desc;
	// For now, check & see if we are gf3 or higher on the food chain.

	ChipsetType res=DC_UNKNOWN;
	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		m_currentChipset = res;	//cache the current chipset.

		//Some of our effects require an offscreen render target, so try creating it here.
		HRESULT hr=DX8Wrapper::_Get_D3D_Device8()->GetRenderTarget(0, &m_oldRenderSurface);

		// Guard: GetRenderTarget may fail after device reset if driver is not yet ready.
		// The original code dereferences m_oldRenderSurface without verifying the HRESULT,
		// causing NULL-pointer crash on Alt-Tab recovery.
		if (FAILED(hr) || m_oldRenderSurface == NULL) {
			DEBUG_LOG(("CPE_SHADERINIT: GetRenderTarget failed after device reset (hr=0x%08X)\n", (UINT)hr));
			m_oldRenderSurface = NULL;
			m_renderTexture = NULL;
			m_newRenderSurface = NULL;
			m_oldDepthSurface = NULL;
			goto skipRenderTarget;
		}
		m_oldRenderSurface->GetDesc(&desc);

		hr=DX8Wrapper::_Get_D3D_Device8()->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&m_renderTexture,NULL);

		if (hr != S_OK)
		{
			if (m_oldRenderSurface) m_oldRenderSurface->Release();
			m_oldRenderSurface = NULL;
			m_renderTexture = NULL;
		} else {
			hr = m_renderTexture->GetSurfaceLevel(0, &m_newRenderSurface);
			if (hr != S_OK)
			{
				if (m_renderTexture) m_renderTexture->Release();
				m_renderTexture = NULL;
				m_newRenderSurface = NULL;
			}	else {
				hr = DX8Wrapper::_Get_D3D_Device8()->GetDepthStencilSurface(&m_oldDepthSurface);
				if (hr != S_OK)
				{
					if (m_newRenderSurface) m_newRenderSurface->Release();
					if (m_renderTexture) m_renderTexture->Release();
					m_renderTexture = NULL;
					m_newRenderSurface = NULL;
					m_oldDepthSurface = NULL;
				}
			}
		}
	}

skipRenderTarget:

	W3DShaderInterface **shaders;

	for (i=0; MasterShaderList[i] != NULL; i++)
	{	
		shaders=MasterShaderList[i];
		for (j=0; shaders[j] != NULL; j++)
		{
			if (shaders[j]->init())
				break;	//found a working shader
		}
	}
	W3DFilterInterface **filters;

	for (i=0; MasterFilterList[i] != NULL; i++)
	{	
		filters=MasterFilterList[i];
		for (j=0; filters[j] != NULL; j++)
		{
			if (filters[j]->init())
				break;	//found a working shader
		}
	}

	DEBUG_LOG(("ShaderManager ChipsetID %d\n", res));

	// Register the device-reset adapter (duplicate registrations are ignored
	// by the registry, so repeated init() calls are safe).
	DX8Wrapper::RegisterCleanupHook(&_shaderManagerCleanupHook);
}

// W3DShaderManager::releaseDeviceResources ================================
/** Release only the bare offscreen render-target surfaces.  Idempotent:
	needed on the device-reset path (cleanup-hook registry) so the bare
	D3DPOOL_DEFAULT texture no longer blocks D3D9 Reset() in scenes without
	a heightmap (e.g. the main menu). */
//=============================================================================
void W3DShaderManager::releaseDeviceResources(void)
{
	if (m_newRenderSurface) m_newRenderSurface->Release();
	if (m_renderTexture) m_renderTexture->Release();
	if (m_oldRenderSurface) m_oldRenderSurface->Release();
	if (m_oldDepthSurface) m_oldDepthSurface->Release();
	m_renderTexture = NULL;
	m_newRenderSurface = NULL;
	m_oldDepthSurface = NULL;
	m_oldRenderSurface = NULL;
	// 2026-09-10: dgVoodoo Reset destroys shaders - drop the W3D cast depth PS
	// with the other device resources (dx8renderer.cpp must see NULL until
	// init() recompiles it, or it would bind a dead shader).
	if (g_w3dShadowDepthPS) { g_w3dShadowDepthPS->Release(); g_w3dShadowDepthPS = NULL; }
}

// W3DShaderManager::shutdown =======================================================
/** Any shaders which allocate resources will be allowed to free them */
//=============================================================================
void W3DShaderManager::shutdown(void)
{
	if (m_newRenderSurface) m_newRenderSurface->Release();
	if (m_renderTexture) m_renderTexture->Release();
	if (m_oldRenderSurface) m_oldRenderSurface->Release();
	if (m_oldDepthSurface) m_oldDepthSurface->Release();
	m_renderTexture = NULL;
	m_newRenderSurface = NULL;
	m_oldDepthSurface = NULL;
	m_oldRenderSurface = NULL;
	if (g_w3dShadowDepthPS) { g_w3dShadowDepthPS->Release(); g_w3dShadowDepthPS = NULL; }
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	//release any assets associated with a shader (vertex/pixel shaders, textures, etc.)
	for (Int i=0; i<W3DShaderManager::ST_MAX; i++) {
		if (W3DShaders[i]) {
			W3DShaders[i]->shutdown();
		}
	}

	for (i=0; i < FT_MAX; i++)
	{	
		if (W3DFilters[i])
		{
			W3DFilters[i]->shutdown();
		}
	}

}

// W3DShaderManager::getShaderPasses =======================================================
/** Return number of renderig passes required in perform the desired shader on current
	hardware.  App will need to re-render the polygons this many times to complete the
	effect.
 */
//=============================================================================
Int W3DShaderManager::getShaderPasses(ShaderTypes shader)
{
	return W3DShadersPassCount[shader];
}

// W3DShaderManager::setShader =======================================================
/** Must call this method before each rendering pass in order to perform proper D3D
	setup for each shader.
 */
//=============================================================================
Int W3DShaderManager::setShader(ShaderTypes shader, Int pass)
{
	if (shader == m_currentShader && pass == m_currentShaderPass)
		return TRUE;	//shader is already set
	m_currentShader=shader;
	m_currentShaderPass = pass;
	if (W3DShaders[shader])
		return W3DShaders[shader]->set(pass);
	return FALSE;
}

// W3DShaderManager::resetShader =======================================================
/** Must call this method after all polygons and rendering passes have been submitted.
	This method allows D3D to reset itself to a default state that doesn't conflict
	with the WW3D2 Shader system.
 */
//=============================================================================
void W3DShaderManager::resetShader(ShaderTypes shader)
{	
	if (m_currentShader == ST_INVALID)
		return;	//last shader is already reset.
	if (W3DShaders[shader])
		W3DShaders[shader]->reset();
	m_currentShader = ST_INVALID;
}
// W3DShaderManager::filterPreRender =======================================================
/** Call to view filter shaders before rendering starts.
 */
//=============================================================================
Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (W3DFilters[filter])
	{	Bool result=W3DFilters[filter]->preRender(skipRender,scenePassMode);
		if (result)
			m_currentFilter = filter;
		return result;
	}
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterPostRender(FilterTypes filter, enum FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->postRender(mode, scrollDelta,doExtraRender);

	m_currentFilter = FT_NULL_FILTER;
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
	static Bool filterSetup(FilterTypes filter, enum FilterModes mode);
Bool W3DShaderManager::filterSetup(FilterTypes filter, enum FilterModes mode)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->setup(mode);
	return FALSE;
}

/*Draws 2 triangles covering the viewport given the current render states*/
void W3DShaderManager::drawViewport(Int color)
{
	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color    
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = color;
	v[1].color = color;
	v[2].color = color;
	v[3].color = color;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
}

// PBR texture pipeline methods (Phase 3+)
//=============================================================================
void W3DShaderManager::registerPBRTexture(const char *albedoName, const char *pbrName)
{
	if (!m_pbrTextureMap) {
		m_pbrTextureMap = NEW PBRTextureMap;
	}
	AsciiString key(albedoName);
	(*m_pbrTextureMap)[key] = true;
	DEBUG_LOG(("PBR: registered texture %s -> %s\n", albedoName, pbrName));
}

Bool W3DShaderManager::hasPBRTexture(const char *albedoName)
{
	if (!m_pbrTextureMap) return false;
	AsciiString key(albedoName);
	PBRTextureMap::iterator it = m_pbrTextureMap->find(key);
	return (it != m_pbrTextureMap->end());
}

// C-linkage wrapper for cross-library access from dx8renderer.cpp
extern "C" bool PBR_HasTexture(const char *albedoName)
{
	return (W3DShaderManager::hasPBRTexture(albedoName) != 0);
}


//=============================================================================
// Normal map registration (Phase 5)
//=============================================================================
void W3DShaderManager::registerNormalMap(const char *albedoName, TextureClass *normalTex)
{
	if (!m_normalMapMap) {
		m_normalMapMap = NEW NormalMapMap;
	}
	AsciiString key(albedoName);
	(*m_normalMapMap)[key] = normalTex;
	DEBUG_LOG(("[W3X_P5] registered normal map for %s\n", albedoName));
}

TextureClass *W3DShaderManager::getNormalMapTexture(const char *albedoName)
{
	if (!m_normalMapMap) return NULL;
	AsciiString key(albedoName);
	NormalMapMap::iterator it = m_normalMapMap->find(key);
	if (it != m_normalMapMap->end()) {
		return it->second;
	}
	return NULL;
}

// C-linkage normal-map query for cross-library access from dx8renderer.cpp.
// Returns true if a _n.dds normal map was registered for this albedo texture.
extern "C" bool PBR_HasNormalMap(const char *albedoName)
{
	if (!albedoName || !albedoName[0]) return false;
	return (W3DShaderManager::getNormalMapTexture(albedoName) != NULL);
}

// C-linkage normal-map binding for cross-library access from dx8renderer.cpp.
// Binds the registered _n.dds to texture stage 1 (s1 in the PBR unit shader).
// Returns true if a normal map was bound, false otherwise.
extern "C" bool PBR_BindNormalMap(const char *albedoName)
{
	if (!albedoName || !albedoName[0]) return false;
	TextureClass *normalTex = W3DShaderManager::getNormalMapTexture(albedoName);
	if (!normalTex || !normalTex->Peek_D3D_Texture()) return false;
	IDirect3DDevice8 *pDev = DX8Wrapper::_Get_D3D_Device8();
	if (!pDev) return false;
	pDev->SetTexture(1, normalTex->Peek_D3D_Texture());
	return true;
}

// C-linkage IBL texture binding for cross-library access from dx8renderer.cpp
// Called after selecting a PBR unit shader to bind CubeMap environment textures
// to stages 3 (irradiance), 4 (prefiltered), 5 (BRDF LUT).
extern "C" void PBR_BindIBLTextures(void)
{
	if (!w3dPBRShader.m_envIrradianceMap && !w3dPBRShader.m_envPrefilteredMap)
		return;
	IDirect3DDevice8 *pDev = DX8Wrapper::_Get_D3D_Device8();
	if (!pDev) return;

	// Stage 3: diffuse irradiance CubeMap (s3 in shader)
	if (w3dPBRShader.m_envIrradianceMap && w3dPBRShader.m_envIrradianceMap->Is_Initialized()) {
		IDirect3DCubeTexture8 *cubeTex = w3dPBRShader.m_envIrradianceMap->Peek_D3D_CubeTexture();
		if (cubeTex) pDev->SetTexture(3, cubeTex);
	}
	// Stage 4: specular pre-filtered CubeMap (s4 in shader)
	if (w3dPBRShader.m_envPrefilteredMap && w3dPBRShader.m_envPrefilteredMap->Is_Initialized()) {
		IDirect3DCubeTexture8 *preCube = w3dPBRShader.m_envPrefilteredMap->Peek_D3D_CubeTexture();
		if (preCube) pDev->SetTexture(4, preCube);
	}
	// Stage 5: BRDF integration LUT (s5 in shader)
	if (w3dPBRShader.m_brdfLUT && w3dPBRShader.m_brdfLUT->Peek_D3D_Texture()) {
		pDev->SetTexture(5, w3dPBRShader.m_brdfLUT->Peek_D3D_Texture());
	}
}

extern "C" void PBR_ClearIBLTextures(void)
{
	IDirect3DDevice8 *pDev = DX8Wrapper::_Get_D3D_Device8();
	if (!pDev) return;
	pDev->SetTexture(3, NULL);
	pDev->SetTexture(4, NULL);
	pDev->SetTexture(5, NULL);
}

// Set TRUE by WaterRenderObjClass::Render() to skip PBR VS binding during water rendering.
bool g_pbrInsideWaterRender = false;

// C-linkage: get per-model PBR params from PBROverride.ini (INI-driven override).
// Called from dx8renderer.cpp Phase 3.5. Returns true if an override was found.
extern "C" void PBR_SetLegacyParam(const char *name, float roughness, float metalness)
{
	W3DShaderManager::setLegacyPBRParams(name, roughness, metalness);
}

extern "C" bool PBR_GetLegacyPBRParams(const char *meshName, W3DShaderManager::LegacyPBRParams *outParams)
{
	return W3DShaderManager::getLegacyPBRParams(meshName, outParams);
}

// =============================================================================
// PBR Override Bucket Optimization (替代全表线性遍历)
// 按首字母分桶，桶内按 key 长度降序排列，查找时仅扫描一个桶
// =============================================================================
struct PBRBucketEntry {
	AsciiString key;
	W3DShaderManager::LegacyPBRParams params;
	int keyLen;
};
static std::vector<PBRBucketEntry> s_pbrBuckets[26];

// C-linkage: sun glow shader access for W3DScene.cpp
extern "C" bool PBR_IsSunGlowEnabled(void)
{
	// DISABLED: sun glow causes significant performance degradation.屏蔽太阳辉光函数
	// Code is preserved for future re-enablement.
	return false;
}
extern "C" void PBR_RenderSunGlow(void)
{
	static Bool s_firstRender = TRUE;
	// Gate on PBR_IsSunGlowEnabled() so the existing disable flag actually
	// takes effect. The sun glow has never rendered successfully (crashed at
	// DrawPrimitiveUP — shader c0/c1 constants were never set and the device
	// still had a PBR vertex shader bound, so the XYZRHW quad fed garbage).
	// Skip it; re-enable by flipping PBR_IsSunGlowEnabled() to true.
	if (!PBR_IsSunGlowEnabled() || !w3dPBRShader.m_sunGlowEnabled || !w3dPBRShader.m_dwSunGlowShader) {
		if (s_firstRender) {
			DEBUG_LOG(("Sun Glow: skipped (disabled)\n"));
			TerrainDiag("sun_glow_skip_disabled");
		}
		return;
	}

	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) {
		if (s_firstRender) {
			DEBUG_LOG(("Sun Glow: skipped (no device)\n"));
			TerrainDiag("sun_glow_skip_nodevice");
		}
		return;
	}

	if (s_firstRender) {
		s_firstRender = FALSE;
		DEBUG_LOG(("Sun Glow: FIRST RENDER\n"));
		TerrainDiag("sun_glow_first_render");
	}

	DWORD zE, zW, aB, sB, dB;
	dev->GetRenderState(D3DRS_ZENABLE, &zE);
	dev->GetRenderState(D3DRS_ZWRITEENABLE, &zW);
	dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &aB);
	dev->GetRenderState(D3DRS_SRCBLEND, &sB);
	dev->GetRenderState(D3DRS_DESTBLEND, &dB);

	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

	struct TV { float x,y,z,w; DWORD c; float u,v; };
	TV v[4] = {
		{-1,-1,0,1,0xFFFFFFFF,0,0}, {1,-1,0,1,0xFFFFFFFF,0,0},
		{-1,1,0,1,0xFFFFFFFF,0,0}, {1,1,0,1,0xFFFFFFFF,0,0},
	};
	dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
	dev->SetPixelShader(w3dPBRShader.m_dwSunGlowShader);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TV));

	dev->SetPixelShader(NULL);
	dev->SetRenderState(D3DRS_ZENABLE, zE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, zW);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, aB);
	dev->SetRenderState(D3DRS_SRCBLEND, sB);
	dev->SetRenderState(D3DRS_DESTBLEND, dB);
}

// C-linkage: bind PBR vertex shader + VS constants from dx8renderer.cpp.
// Called AFTER world matrix is cached in DX8Wrapper::render_state.world,
// so we read render_state directly (NOT D3D device via _Get_DX8_Transform).
extern "C" void PBR_BindVS(void)
{
	if (!w3dPBRShader.m_vsPBRUnit) return;
	if (g_pbrInsideWaterRender) return;	// skip during water rendering
	IDirect3DDevice8 *dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev) return;
	// Read cached world/view via public Get_Transform (reads render_state,
	// not D3D device — render_state was just updated by Set_Transform above).
	// Get_Transform returns WW convention (Transpose of D3D), so undo that.
	Matrix4x4 worldMtx, viewMtx, projMtx;
	DX8Wrapper::Get_Transform(D3DTS_WORLD, worldMtx);
	worldMtx = worldMtx.Transpose(); // WW→D3D convention (same as _Get_DX8_Transform)
	DX8Wrapper::Get_Transform(D3DTS_VIEW, viewMtx);
	viewMtx  = viewMtx.Transpose();
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projMtx);
	Matrix4x4 wvpMtx = worldMtx * viewMtx * projMtx;
	D3DXMATRIX wvpT, worldT;
	D3DXMatrixTranspose(&wvpT, (const D3DXMATRIX*)&wvpMtx);
	D3DXMatrixTranspose(&worldT, (const D3DXMATRIX*)&worldMtx);
	// Use wrapper's Set_Vertex_Shader_Constant so the cache tracks these
	// writes — the restore path in dx8renderer.cpp zeroes via the wrapper too.
	DX8Wrapper::Set_Vertex_Shader_Constant(0, (const float*)&wvpT, 4);
	DX8Wrapper::Set_Vertex_Shader_Constant(4, (const float*)&worldT, 4);
	{
		const D3DXMATRIX *pWorld = (const D3DXMATRIX*)&worldMtx; // D3D convention (same as _Get_DX8_Transform) — correct rows for m3x3
		float normalMtx[12];
		for (int r = 0; r < 3; r++) {
			normalMtx[r*4 + 0] = pWorld->m[r][0];
			normalMtx[r*4 + 1] = pWorld->m[r][1];
			normalMtx[r*4 + 2] = pWorld->m[r][2];
			normalMtx[r*4 + 3] = 0.0f;
		}
		DX8Wrapper::Set_Vertex_Shader_Constant(8, normalMtx, 3);
	}
	DX8Wrapper::Set_Vertex_Shader(w3dPBRShader.m_vsPBRUnit);
	// DIAG: read back all VS constants to verify they are correct
	{ static int once = 0; if (!once) { once = 1;
		float rd_c0[4], rd_c1[4], rd_c2[4], rd_c8[4], rd_c10[4];
		IDirect3DVertexShader9 *rd_vs = NULL;
		IDirect3DPixelShader9 *rd_ps = NULL;
		dev->GetVertexShader(&rd_vs);
		dev->GetPixelShader(&rd_ps);
		dev->GetVertexShaderConstantF(0, rd_c0, 1);
		dev->GetPixelShaderConstantF(1, rd_c1, 1);
		dev->GetPixelShaderConstantF(2, rd_c2, 1);
		dev->GetVertexShaderConstantF(8, rd_c8, 1);
		dev->GetPixelShaderConstantF(10, rd_c10, 1);
		FILE *f = fopen("E:\\terrain_diag.log", "a");
		if (f) {
			fprintf(f, "[%u] PBR_VS_CONSTS:"
				" VS=%p PS=%p\n"
				"  c0_wvp=(%.1f,%.1f,%.1f,%.1f)\n"
				"  c1_sunCol=(%.3f,%.3f,%.3f)\n"
				"  c2_cam=(%.1f,%.1f,%.1f)\n"
				"  c8_nrm=(%.4f,%.4f,%.4f,%.4f)\n"
				"  c10_amb=(%.3f,%.3f,%.3f)\n",
				timeGetTime(),
				(void*)rd_vs, (void*)rd_ps,
				rd_c0[0],rd_c0[1],rd_c0[2],rd_c0[3],
				rd_c1[0],rd_c1[1],rd_c1[2],
				rd_c2[0],rd_c2[1],rd_c2[2],
				rd_c8[0],rd_c8[1],rd_c8[2],rd_c8[3],
				rd_c10[0],rd_c10[1],rd_c10[2]);
			fclose(f);
		}
		if (rd_vs) rd_vs->Release();
		if (rd_ps) rd_ps->Release();
	}}
	{ static int once = 0; if (!once) { once = 1;
		IDirect3DVertexShader9 *curVS = NULL;
		dev->GetVertexShader(&curVS);
		FILE *f = fopen("E:\\terrain_diag.log", "a");
		if (f) { fprintf(f, "[%u] PBR_VS_BOUND: m_vsPBRUnit=%p devVS=%p eq=%d\n",
			timeGetTime(), (void*)w3dPBRShader.m_vsPBRUnit, (void*)curVS,
			(int)(w3dPBRShader.m_vsPBRUnit == curVS)); fclose(f); }
		if (curVS) curVS->Release();
	}}
}

void W3DShaderManager::setLegacyPBRParams(const char *meshName, float roughness, float metalness)
{
	if (!m_legacyPBRParamsMap) {
		m_legacyPBRParamsMap = NEW LegacyPBRParamsMap;
	}
	AsciiString key(meshName);
	LegacyPBRParams params;
	params.roughness = roughness;
	params.metalness = metalness;
	params.pad[0] = 0.0f;
	params.pad[1] = 0.0f;
	(*m_legacyPBRParamsMap)[key] = params;

	// Also populate char bucket for fast prefix lookup
	// Insert in key-length-descending order (VC6-compatible, no lambda)
	char first = toupper(meshName[0]);
	if (first >= 'A' && first <= 'Z') {
		int idx = first - 'A';
		PBRBucketEntry entry;
		entry.key = key;
		entry.params = params;
		entry.keyLen = strlen(key.str());
		// Insert sorted: longer keys first so first match is longest
		size_t insertPos = 0;
		while (insertPos < s_pbrBuckets[idx].size() &&
			s_pbrBuckets[idx][insertPos].keyLen > entry.keyLen)
		{
			insertPos++;
		}
		s_pbrBuckets[idx].insert(s_pbrBuckets[idx].begin() + insertPos, entry);
	}
}

Bool W3DShaderManager::getLegacyPBRParams(const char *meshName, LegacyPBRParams *outParams)
{
	if (!m_legacyPBRParamsMap || !outParams || !meshName) {
		// DIAG: log when no map or no mesh (first call only)
		{ static int d = 0; if (d < 1) { d++; FILE *f = fopen("E:\\pbr_diag.log", "a");
			if (f) { fprintf(f, "[PBR_OVERRIDE] getLegacyPBRParams called but map=%p out=%p name=%s\n",
				(void*)m_legacyPBRParamsMap, (void*)outParams, meshName ? meshName : "NULL"); fclose(f); }
		}}
		return false;
	}
	// DIAG: log map size on first call
	{ static int d2 = 0; if (d2 < 1) { d2++; FILE *f = fopen("E:\\pbr_diag.log", "a");
		if (f) { fprintf(f, "[PBR_OVERRIDE] LegacyPBRParamsMap has %d entries\n", (int)m_legacyPBRParamsMap->size()); fclose(f); }
	}}

	// Prefix match: PBROverride.ini keys are container names (e.g. "AVHUMMER")
	// Must match the start of meshName (e.g. "AVHUMMER.Body01").
	// Return the BEST (longest) match.
	// OPTIMIZED: use first-char buckets instead of scanning all 8689 entries.
	int bestLen = 0;
	Bool found = false;

	char first = toupper(meshName[0]);
	if (first >= 'A' && first <= 'Z') {
		int idx = first - 'A';
		const std::vector<PBRBucketEntry> &bucket = s_pbrBuckets[idx];
		for (size_t i = 0; i < bucket.size(); i++) {
			const PBRBucketEntry &entry = bucket[i];
			if (entry.keyLen <= bestLen) continue;	// already have a longer match

			if (_strnicmp(meshName, entry.key.str(), entry.keyLen) == 0) {
				char next = meshName[entry.keyLen];
				if (next == '.' || next == '\0') {
					bestLen = entry.keyLen;
					*outParams = entry.params;
					found = true;
					// DIAG: log first 20 PBROverride matches
					{ static int diagPBR = 0; if (diagPBR < 20) { diagPBR++;
						FILE *f = fopen("E:\\pbr_diag.log", "a");
						if (f) { fprintf(f, "[PBR_OVERRIDE] match: mesh=\"%s\" key=\"%s\" rough=%.2f metal=%.2f\n",
							meshName, entry.key.str(), outParams->roughness, outParams->metalness); fclose(f); }
					}}
				}
			}
		}
	}
	return found;
}

Bool W3DShaderManager::isLegacyPBREnabled(void)
{
	if (!TheGlobalData) return false;
	return TheGlobalData->m_useLegacyPBR;
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Starts rendering to a texture.
 */
//=============================================================================
void W3DShaderManager::startRenderToTexture(void)
{	
	DEBUG_ASSERTCRASH(!m_renderingToTexture, ("Already rendering to texture - cannot nest calls."));

	if (m_renderingToTexture || m_newRenderSurface==NULL || m_oldDepthSurface==NULL) return;
	HRESULT hr = DX8Wrapper::_Get_D3D_Device8()->SetRenderTarget(0, m_newRenderSurface);
	if (hr == S_OK)
		hr = DX8Wrapper::_Get_D3D_Device8()->SetDepthStencilSurface(m_oldDepthSurface);
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr != S_OK)
		return;
	m_renderingToTexture = true;
	if (TheGlobalData->m_showSoftWaterEdge)
	{	//Soft water edges use frame buffer destination alpha so we must clear it to a known value.
		if (m_currentFilter == FT_VIEW_MOTION_BLUR_FILTER || m_currentFilter == FT_VIEW_CROSSFADE)
		{	//these filters rely on the previous frame being visible so we must be careful about clearing
			//frame buffer.  Only clear the alpha channel
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);	//only clear alpha
			ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;
			shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			DX8Wrapper::Set_Shader(shader);

			VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			DX8Wrapper::Set_Material(vmat);
			REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		
			drawViewport(0x00ffffff | (((Int)(TheWaterTransparency->m_minWaterOpacity*255.0f)) <<24));
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE);	//disable writes to alpha
		}
		else	//normal clear that overwrites everything.
			DX8Wrapper::Clear(true, false, Vector3( 0.0f, 0.0f, 0.0f ), TheWaterTransparency->m_minWaterOpacity);
	}
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Ends rendering to a texture.
 */
//=============================================================================
IDirect3DTexture8 *W3DShaderManager::endRenderToTexture(void)
{	
	DEBUG_ASSERTCRASH(m_renderingToTexture, ("Not rendering to texture."));
	if (!m_renderingToTexture) return NULL;
	HRESULT hr = DX8Wrapper::_Get_D3D_Device8()->SetRenderTarget(0, m_oldRenderSurface);
	if (hr == S_OK)
		hr = DX8Wrapper::_Get_D3D_Device8()->SetDepthStencilSurface(m_oldDepthSurface);	//restore original depth buffer
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr == S_OK)
	{
		//assume render target texure will be in stage 0.  Most hardware has "conditional" support for
		//non-power-of-2 textures so we must force some required states:
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

		m_renderingToTexture = false;
	}
	return m_renderTexture;
}

/**Returns texture containing the image that was last rendered using any of the effects requiring render target
textures.  Used mostly for cross-fading effects that need an unmodified version of the view before the effect
was applied.  NOTE: This texture does not survive device reset.. so quit effect on reset!*/
IDirect3DTexture8 *W3DShaderManager::getRenderTexture(void)
{
	return m_renderTexture;
}

enum GraphicsVenderID
{
	DC_NVIDIA_VENDOR_ID	= 0x10DE,
	DC_3DFX_VENDOR_ID	= 0x121A,
	DC_ATI_VENDOR_ID	= 0x1002
};

// W3DShaderManager::ChipsetType =======================================================
/** Returns the chipset used by the currently active rendering device.  Can be useful
	for coding around specific driver bugs.
 */
//=============================================================================
ChipsetType W3DShaderManager::getChipset( void )
{
	//check if globaldata has an override for current chipset
	if (TheGlobalData && TheGlobalData->m_chipSetType != DC_UNKNOWN)
		return (ChipsetType)TheGlobalData->m_chipSetType;

	ChipsetType chip=DC_UNKNOWN;
	IDirect3D8* d3d8Interface=DX8Wrapper::_Get_D3D8();

	if (d3d8Interface && DX8Wrapper::_Get_D3D_Device8())
	{

		D3DADAPTER_IDENTIFIER8 did;
		::ZeroMemory(&did, sizeof(D3DADAPTER_IDENTIFIER8));
	/*	HRESULT res = */ d3d8Interface->GetAdapterIdentifier(0,D3DENUM_NO_WHQL_LEVEL,&did);
		*((LARGE_INTEGER*)&m_driverVersion) = did.DriverVersion;

		if(did.VendorId == DC_NVIDIA_VENDOR_ID)
		{
			m_currentVendor = DC_NVIDIA_VENDOR_ID;

			if (did.DeviceId == 0x20)
				return DC_TNT;
   
			if (did.DeviceId >= 0x28 && did.DeviceId < 0x100)
				return DC_TNT2;

			if ( (did.DeviceId >= 0x100 && did.DeviceId <= 0x103) ||	//GeForce
				 (did.DeviceId >= 0x110 && did.DeviceId <= 0x113) ||	//GeForce2 MX
						 (did.DeviceId >= 0x150 && did.DeviceId <= 0x153) )	//GeForce2
           		return DC_GEFORCE2;

			if (did.DeviceId >= 0x200 && did.DeviceId < 0x250)
				return DC_GEFORCE3;

			if (did.DeviceId >= 0x250)
				return DC_GEFORCE4;
		}
		else
		if(did.VendorId == DC_3DFX_VENDOR_ID)
		{
			m_currentVendor = DC_3DFX_VENDOR_ID;

			if (did.DeviceId == 0x0002)
				return DC_VOODOO2;
			if (did.DeviceId == 0x0005)
				return DC_VOODOO3;
			if (did.DeviceId == 0x0008)	///@todo: Just guessing on this one - find actual Voodoo4 deviceID.
				return DC_VOODOO4;
			if (did.DeviceId == 0x0009)
				return DC_VOODOO5;
		}
		else
		if(did.VendorId == DC_ATI_VENDOR_ID)
		{
			m_currentVendor = DC_ATI_VENDOR_ID;

			if (did.DeviceId == 0x5144)
				return DC_RADEON;
			if (did.DeviceId == 0x514C)
				return DC_RADEON_8500;
			if (did.DeviceId == 0x4e44)
				return DC_RADEON_9700;
		}

		//None of the vendor specific ID's matched so use generic means to classify the card
		Int maxTextures=DX8Wrapper::Get_Current_Caps()->Get_Max_Simultaneous_Textures();
		Real pixelShaderVersion;

		char buf[256];

		//Convert version to Real
		sprintf(buf,"%d.%d",DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Major_Version(),DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Minor_Version());
		sscanf(buf,"%f",&pixelShaderVersion);

		if (maxTextures >= 4)
		{	if (pixelShaderVersion >= 1.1f)
				chip=DC_GENERIC_PIXEL_SHADER_1_1;
			if (pixelShaderVersion >= 1.4f)
				chip=DC_GENERIC_PIXEL_SHADER_1_4;
			if (maxTextures >= 8 && pixelShaderVersion >= 2.0f)
				chip=DC_GENERIC_PIXEL_SHADER_2_0;
			if (pixelShaderVersion >= 3.0f)
				chip=DC_GENERIC_PIXEL_SHADER_3_0;
		}
	}	//D3D8 interface and device exist. 
	
	return chip;
}

#include <cstdio>
#include <mmsystem.h>

static void ShaderDiag(const char *msg) {
	FILE *f = fopen("E:\\water_diag.log", "a");
	if (f) {
		fprintf(f, "[%d] SHADER: %s\n", timeGetTime(), msg);
		fclose(f);
	}
}

//=============================================================================
// WaterRenderObjClass::LoadAndCreateShader
//=============================================================================
/** Loads and creates a D3D pixel or vertex shader.*/
//=============================================================================
HRESULT W3DShaderManager::LoadAndCreateD3DShader(char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, void** ppShader)
{
	{
		char diagMsg[256];
		ChipsetType cs = getChipset();
		sprintf(diagMsg, "LoadAndCreateD3DShader ENTRY: path=%s type=%s chipset=%d DC_PS1_1=%d",
			strFilePath, ShaderType ? "VS" : "PS", (int)cs, (int)DC_GENERIC_PIXEL_SHADER_1_1);
		ShaderDiag(diagMsg);
	}
	if (getChipset() < DC_GENERIC_PIXEL_SHADER_1_1) {
		ShaderDiag("LoadAndCreateD3DShader FAIL: chipset too low");
		return E_FAIL;	//don't allow loading any shaders if hardware can't handle it.
	}
	ShaderDiag("LoadAndCreateD3DShader: chipset check passed");

	try
	{
		File *file = NULL;
		HRESULT hr;

		file = TheFileSystem->openFile(strFilePath, File::READ | File::BINARY);
		if (file == NULL)
		{
			ShaderDiag("LoadAndCreateD3DShader FAIL: file not found");
			OutputDebugString("Could not find file \n" );
			return E_FAIL;
		}
		ShaderDiag("LoadAndCreateD3DShader: file opened OK");

		FileInfo fileInfo;
		TheFileSystem->getFileInfo(AsciiString(strFilePath), &fileInfo);
		DWORD dwFileSize = fileInfo.sizeLow;
		{
			char sz[128];
			sprintf(sz, "LoadAndCreateD3DShader: fileSize=%u", (unsigned)dwFileSize);
			ShaderDiag(sz);
		}

		const DWORD* pShader = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwFileSize);
		if (!pShader)
		{
			ShaderDiag("LoadAndCreateD3DShader FAIL: alloc failed");
			OutputDebugString( "Failed to allocate memory to load shader\n " );
			return E_FAIL;
		}

		file->read((void *)pShader, dwFileSize);

		file->close();
		file = NULL;
		ShaderDiag("LoadAndCreateD3DShader: file read OK");

		if (ShaderType) {
			// Vertex shader: strip D3D8 D3DVSD section (appended at END of shader binary).
			// D3D8 embedded the vertex stream declaration after shader instructions;
			// D3D9 requires pure shader bytecode. D3DVSD ends with 0xFFFFFFFF token.
			const DWORD* pShaderCode = pShader;
			{
				INT i = (INT)(dwFileSize / sizeof(DWORD)) - 1;
				while (i >= 0 && pShader[i] != 0xFFFFFFFF)
					i--;
				if (i >= 0) {
					// Found D3DVSD_END; scan back to find where D3DVSD starts
					while (i > 0 && (pShader[i] & 0x80000000))
						i--;
					// Overwrite first D3DVSD token with END to truncate for D3D9
					((DWORD*)pShader)[i + 1] = 0x0000FFFF;
				}
			}
			{
				char sz[128];
				sprintf(sz, "LoadAndCreateD3DShader: VS codeSize=%u firstDWORD=0x%08X",
					(unsigned)dwFileSize, (unsigned)pShaderCode[0]);
				ShaderDiag(sz);
			}
			hr = DX8Wrapper::_Get_D3D_Device8()->CreateVertexShader(pShaderCode, (IDirect3DVertexShader9**)ppShader);
		} else {
			// Pixel shader
			{
				char sz[128];
				sprintf(sz, "LoadAndCreateD3DShader: PS firstDWORD=0x%08X", (unsigned)pShader[0]);
				ShaderDiag(sz);
			}
			hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(pShader, (IDirect3DPixelShader9**)ppShader);
		}

		HeapFree(GetProcessHeap(), 0, (void*)pShader);

		{
			char sz[128];
			sprintf(sz, "LoadAndCreateD3DShader: CreateShader hr=0x%08X *ppShader=%p",
				(unsigned)hr, ppShader ? *ppShader : (void*)0);
			ShaderDiag(sz);
		}
		if (FAILED(hr))
		{
			ShaderDiag("LoadAndCreateD3DShader FAIL: CreateShader failed");
			OutputDebugString( "Failed to create shader\n ");
			return E_FAIL;
		}
		ShaderDiag("LoadAndCreateD3DShader SUCCESS");
	}
	catch(...)
	{
		OutputDebugString( "Error opening file \n" );
		return E_FAIL;
	}

	return S_OK;
}

//For the MP test, we're enforcing high min-spec requirements that need to be verified.
#define MIN_INTEL_CPU_FREQ	1300
#define MIN_AMD_CPU_FREQ	1100
#define MIN_ACCEPTED_FREQUENCY	1300
#define MIN_ACCEPTED_MEMORY	(1024*1024*256)	//256 MB
#define MIN_ACCEPTED_TEXTURE_MEMORY	(1024*1024*30)	//30 MB

/**Hack to give gameengine access to this function*/
Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	return W3DShaderManager::testMinimumRequirements(videoChipType,cpuType,cpuFreq,numRAM,intBenchIndex,floatBenchIndex,memBenchIndex);
}

Bool W3DShaderManager::testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	if (videoChipType)
		*videoChipType = getChipset();

	if (cpuType)
	{
		*cpuType = XX;	//unknown

		//Check if it's an Athlon
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_AMD &&
				CPUDetectClass::Get_AMD_Processor() >= CPUDetectClass::AMD_PROCESSOR_ATHLON_025)
				*cpuType = K7;

		//Check if it's a P3
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM_III_MODEL_7)
				*cpuType = P3;
		//Check if it's a P4
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM4)
				*cpuType = P4;
	}

	if (cpuFreq)
		*cpuFreq=CPUDetectClass::Get_Processor_Speed();

	if (numRAM)
		*numRAM=CPUDetectClass::Get_Total_Physical_Memory();

	if (intBenchIndex && floatBenchIndex && memBenchIndex)
	{
		// Aliendroid1: Just simulating fake ideal results for now because this benchmarking is redundant on modern hardware
		*floatBenchIndex = 10.0f;
		*intBenchIndex = 10.0f;
		*memBenchIndex = 10.0f;
	}

	return TRUE;
}

/**Try to guess how well the video card will handle the game assuming very fast CPU*/
StaticGameLODLevel W3DShaderManager::getGPUPerformanceIndex(void)
{
	ChipsetType	chipType;
	StaticGameLODLevel detailSetting=STATIC_GAME_LOD_LOW;	//assume lowest settings for now.

	if ((chipType=getChipset()) != DC_UNKNOWN)
	{	//a known video card so we can make some assumptions
		if (chipType >=	DC_GEFORCE2)
			detailSetting=STATIC_GAME_LOD_LOW;	//these cards need multiple terrain passes.
		if (chipType >= DC_GENERIC_PIXEL_SHADER_1_1)	//these cards can do terrain in single pass.
			detailSetting=STATIC_GAME_LOD_HIGH;
	}

	return detailSetting;
}

/**We need a hardware independent method to compare different CPU's.  For lack of anything better, we'll
use time to calculate PIE using a slow random number algorithm.*/

/**Used to test function call overhead*/
void add(float *sum,float *addend)
{
	*sum = *sum + *addend;
}

/**Returns seconds needed to run the test*/
Real W3DShaderManager::GetCPUBenchTime(void)
{
	float ztot, yran, ymult, ymod, x, y, z, pi, prod;
    long int low, ixran, itot, j, iprod;

  	__int64 endTime64,freq64,startTime64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);

    ztot = 0.0;
    low = 1;
    ixran = 1907;
    yran = 5813.0;
    ymult = 1307.0;
    ymod = 5471.0;
    itot = 560000;	//total iterations. This value ends up running at ~30 fps on our P4-2.2Ghz.

    for(j=1; j<=itot; j++)
    {
		iprod = 27611 * ixran;
		ixran = iprod - 74383*(long int)(iprod/74383);
		x = (float)ixran / 74383.0;
		prod = ymult * yran;
		yran = (prod - ymod*(long int)(prod/ymod));
		y = yran / ymod;
		z = x*x + y*y;
		add(&ztot,&z);
		if ( z <= 1.0 )
		{
		  low = low + 1;
		}
	}
	pi = 4.0 * (float)low/(float)itot;

	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	return ((double)(endTime64-startTime64)/(double)(freq64));
}


// W3DShaderManager::setShroudTex =======================================================
/** Puts the shroud texture into a texture stage.
 */
//=============================================================================
Int W3DShaderManager::setShroudTex(Int stage)
{
	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
	{	 
		DX8Wrapper::Set_Texture(stage, shroud->getShroudTexture());

		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2 );
		D3DXMATRIX inv;
		float det;

		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		D3DXMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		D3DXMatrixScaling(&scale, width, height, 1);
		*((D3DXMATRIX *)&curView) = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), *((Matrix4x4*)&curView));
		return TRUE;
	}
	return FALSE;
}



Int FlatTerrainShader2Stage::init( void )
{
	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=1;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=2;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=2;
	W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=&flatTerrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=2;

	return TRUE;
}

void FlatTerrainShader2Stage::reset(void)
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}


Int FlatTerrainShader2Stage::set(Int pass)																											  
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}

	switch (pass)
	{
		case 0:

			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			if (W3DShaderManager::getShaderTexture(0)) {
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

				DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

				//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
				//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
				W3DShroud *shroud;
				if ((shroud=TheTerrainRenderObject->getShroud()) != 0)
				{	
					D3DXMATRIX inv;
					float det;

					Matrix4x4 curView;
					DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

					D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

					D3DXMATRIX scale,offset;

					//We need to make all world coordinates be relative to the heightmap data origin since that
					//is where the shroud begins.

					float xoffset = 0;
					float yoffset = 0;
					Real width=shroud->getCellWidth();
					Real height=shroud->getCellHeight();

					if (TheTerrainRenderObject->getMap())
					{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
						xoffset = -(float)shroud->getDrawOriginX() + width;
						yoffset = -(float)shroud->getDrawOriginY() + height;
					}

					D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

					width = 1.0f/(width*shroud->getTextureWidth());
					height = 1.0f/(height*shroud->getTextureHeight());
					D3DXMatrixScaling(&scale, width, height, 1);
					*((D3DXMATRIX *)&curView) = (inv * offset) * scale;
					DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0), *((Matrix4x4*)&curView));
				}
			}	else {
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG2 );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
			}
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 0 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
			break;
		case 1:
			// Noise/cloud pass
			Matrix4x4 curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			//blend into frame buffer
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ZERO);

			
			D3DXMATRIX inv;
			float det;

			D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass

				terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				//setup noise pass

				terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv);
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
				DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
			} //ST_TERRAIN_BASE_NOISE12
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, *((Matrix4x4*)&curView));
			}
			break;
	}

	return TRUE;
}






Int FlatTerrainShaderPixelShader::shutdown(void)
{
	if (m_dwBasePixelShader)
				m_dwBasePixelShader->Release();

	if (m_dwBase0PixelShader)
				m_dwBase0PixelShader->Release();

	if (m_dwBaseNoise1PixelShader)
				m_dwBaseNoise1PixelShader->Release();

	if (m_dwBaseNoise2PixelShader)
				m_dwBaseNoise2PixelShader->Release();

	m_dwBasePixelShader=NULL;
	m_dwBase0PixelShader=NULL;
	m_dwBaseNoise1PixelShader=NULL;
	m_dwBaseNoise2PixelShader=NULL;

	return TRUE;
}

Int FlatTerrainShaderPixelShader::init( void )
{	
	Int res;

#ifdef DISABLE_PIXEL_SHADERS
	return false;
#endif

	//this shader will also use the 2Stage shader for some of the passes so initialize it too.
	if ((res=W3DShaderManager::getChipset()) >= DC_GENERIC_PIXEL_SHADER_1_1)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//base version which doesn't apply any noise textures.
			HRESULT hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrain.pso", NULL, 0, false, (void**)&m_dwBasePixelShader);
			if (FAILED(hr))
				return FALSE;

			//base version which doesn't apply any shroud textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrain0.pso", NULL, 0, false, (void**)&m_dwBase0PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 1 noise texture.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrainnoise.pso", NULL, 0, false, (void**)&m_dwBaseNoise1PixelShader);
			if (FAILED(hr))
				return FALSE;

			//version which blends 2 noise textures.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\fterrainnoise2.pso", NULL, 0, false, (void**)&m_dwBaseNoise2PixelShader);
			if (FAILED(hr))
				return FALSE;

			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=&flatTerrainShaderPixelShader;
			W3DShaders[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=&flatTerrainShaderPixelShader;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2]=1;
			W3DShadersPassCount[W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12]=1;
			return TRUE;
		}
	}
	return FALSE;
}

Int FlatTerrainShaderPixelShader::set(Int pass)
{
	//setup base pass
	Int curStage = 1;
	// setup terrain [3/31/2003]

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_Texture(0, W3DShaderManager::getShaderTexture(2));
	DX8Wrapper::Set_Texture(1, W3DShaderManager::getShaderTexture(2));
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();




	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	//tell pixel shader which UV set to use for each stage
	DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);	

	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}

	curStage = 0;

	W3DShroud *shroud = TheTerrainRenderObject->getShroud();
	if (shroud) {

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

		//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
		//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
		{	
			D3DXMATRIX inv;
			float det;

			Matrix4x4 curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

			D3DXMATRIX scale,offset;

			//We need to make all world coordinates be relative to the heightmap data origin since that
			//is where the shroud begins.

			float xoffset = 0;
			float yoffset = 0;
			Real width=shroud->getCellWidth();
			Real height=shroud->getCellHeight();

			if (TheTerrainRenderObject->getMap())
			{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
				xoffset = -(float)shroud->getDrawOriginX() + width;
				yoffset = -(float)shroud->getDrawOriginY() + height;
			}

			D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

			width = 1.0f/(width*shroud->getTextureWidth());
			height = 1.0f/(height*shroud->getTextureHeight());
			D3DXMatrixScaling(&scale, width, height, 1);
			*((D3DXMATRIX *)&curView) = (inv * offset) * scale;
			DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), *((Matrix4x4*)&curView));
		}
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, shroud->getShroudTexture()->Peek_D3D_Texture());
		curStage++;
		if (curStage==1) curStage++;
	}

	Bool doNoise1 = (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE1 ||
						W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12);
	if (doNoise1) {	 // Cloud pass.
		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
		terrainShader2Stage.updateNoise1(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), *((Matrix4x4*)&curView));
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		
		curStage++;
		if (curStage==1) curStage++;
	}

	Bool doNoise2 = (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE2 ||
						W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_FLAT_TERRAIN_BASE_NOISE12);
	if (doNoise2)
	{	
		Matrix4x4 curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, (D3DXMATRIX*)&curView);

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		// Two output coordinates are used.
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);	

		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
		terrainShader2Stage.updateNoise2(((D3DXMATRIX*)&curView),&inv);	//update curView with texture matrix
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+curStage), *((Matrix4x4*)&curView));
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(curStage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		
		curStage++;
		if (curStage==1) curStage++;
	}
	if (curStage<2) {
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBase0PixelShader);
	}	else if (curStage==2) {
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBasePixelShader);
	}	else if (curStage==3) {
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBaseNoise1PixelShader);
	}else if (curStage==4) {
		DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(m_dwBaseNoise2PixelShader);
	}
	DX8Wrapper::_Get_D3D_Device8()->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(curStage, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
	return TRUE;
}

void FlatTerrainShaderPixelShader::reset(void)
{
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(2,NULL);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(3,NULL);	//release reference to any texture
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(4, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(5, NULL);

	DX8Wrapper::_Get_D3D_Device8()->SetPixelShader(NULL);	//turn off pixel shader

	DX8Wrapper::_Get_D3D_Device8()->SetTexture(0, NULL);
	DX8Wrapper::_Get_D3D_Device8()->SetTexture(1, NULL);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|2);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|3);


	DX8Wrapper::Invalidate_Cached_Render_States();
}





