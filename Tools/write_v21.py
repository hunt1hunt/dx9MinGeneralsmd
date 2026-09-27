import os

BS = chr(92)
# ---------- 1) engine: bind ViewProjInverse in BindW3XMatrices ----------
p = r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\W3XRenderObj.cpp'
d = open(p, 'rb').read()
anchor = b'\tD3DXHANDLE hWVP = effect->GetParameterByName(NULL, "WorldViewProj");\n\tif (hWVP) effect->SetMatrix(hWVP, (const D3DXMATRIX*)&wvp);\n'
# the anchor now has injected blocks after it; find it and inject after the FIRST occurrence region
assert d.count(anchor) == 1, d.count(anchor)
inject = anchor + (
'\t// 2026-09-12 v2.1: matrices are the ONLY parameter class PROVEN to land on\n'
'\t// every W3X effect (World/VP). The camera position is recoverable from the\n'
'\t// INVERSE view-projection (row 3 translation) - bind it as a matrix so the\n'
'\t// point-light PS never needs a vector parameter at all.\n'
'\tD3DXHANDLE hIVP = effect->GetParameterByName(NULL, "ViewProjInverse");\n'
'\tif (hIVP) {\n'
'\t\tD3DXMATRIX mVP;\n'
'\t\tmemcpy(&mVP, &vp, sizeof(D3DXMATRIX));\n'
'\t\tD3DXMATRIX mIVP;\n'
'\t\tfloat detIVP;\n'
'\t\tD3DXMatrixInverse(&mIVP, &detIVP, &mVP);\n'
'\t\teffect->SetMatrix(hIVP, &mIVP);\n'
'\t}\n').encode()
d = d.replace(anchor, inject)
open(p, 'wb').write(d)
print('InvVP bind installed')

# ---------- 2) FX v2.1: world-space impostor, camera from InvVP matrix row ----------
FX = r'''// w3x_pointlight.fx - v2.1 INVERSE-VP volumetric point light
//
// FINAL ARCHITECTURE LESSON: on this engine, engine-bound VECTOR/FLOAT
// parameters (EyePosition/Time/View...) verifiably never reach this effect's
// shaders (handles valid, SetVector logged with real values, PS reads zeros),
// while MATRIX parameters bound in BindW3XMatrices (World/ViewProjection)
// always land. So v2.1 uses ONLY matrices + per-mesh constants: the camera
// world position is extracted from ViewProjInverse's translation row in the
// PS - no vector parameter anywhere.
//
// Glow = analytic point-line falloff (distance view-ray to light center,
// round and soft) + radial pool where the ray crosses ground z=0. Occlusion
// comes free from the main z-test (backface + GREATEREQUAL, RA3 states).
#define DEFERRED_RENDER
#define DYNAMIC_CLOUD_REF

#include "Shaders/RA3/head1-newvs.FXH"

float Range
<
    string UIName = "Range";
    float UIMax = 1024; float UIMin = 32; float UIStep = 1;
> = 100 ;

float3 ColorEmissive
<
	string UIName = "ColorEmissive";
    string UIWidget = "Color";
> = float3(0.5, 0.5, 0.5);

float HDRMultiplier
<
	string UIName = "HDRMultiplier";
    float UIMax = 16; float UIMin = 0; float UIStep = 0.1;
> = 3;

bool UseRecolorColors <string UIName = "UseRecolorColors";> = 0;
bool HouseColorPulse  <string UIName = "HouseColorPulse";> = 0;

// Bound by BindW3XMatrices (the proven matrix path) - inverse of ViewProjection.
float4x4 ViewProjInverse ;

struct VSout_pl
{
	float4 Position       : POSITION;
    float3 WorldPos       : TEXCOORD1;
    float3 LightCenter    : TEXCOORD2;
    float3 LightSourceColor : TEXCOORD3;
};

VSout_pl VSpointlight(float3 Position : POSITION)
{
    VSout_pl o;

    float3 objPos = Position * Range ;
    float3 center2world = World[3].xyz ;
    float4 worldPos4D = float4( float3(objPos + center2world), 1);

    o.WorldPos = worldPos4D.xyz ;
    o.Position = mul(worldPos4D, ViewProjection) ;
    o.LightCenter = center2world ;

    float3 realHC = (HasRecolorColors)? RecolorColor : 0 ;
    realHC *= dot(ColorEmissive , 0.5) ;
    float pulse = 0 ;
    if(HouseColorPulse){ pulse = abs(frac(Time /1) *2 -1) ;}
    float3 lightColor = 1 ;
    lightColor = (UseRecolorColors)? realHC : ColorEmissive ;
    lightColor +=  realHC * pulse ;
    lightColor *= OpacityOverride ;
    o.LightSourceColor = lightColor ;

    return o;
}

float4 PSpointlight(VSout_pl i) : COLOR
{
    // camera world position from the inverse view-projection translation row
    float3 camPos = float3(ViewProjInverse[3][0], ViewProjInverse[3][1], ViewProjInverse[3][2]) ;
    if (dot(camPos, camPos) < 1.0) return float4(0.0, 0.0, 0.12, 1) ;   // DEBUG: matrix dead -> blue

    float3 ro = camPos ;
    float3 rd = normalize(i.WorldPos - ro) ;
    float3 oc = ro - i.LightCenter ;
    float tca = dot(oc, rd) ;
    if (tca < 0.0) return 0 ;
    float d2 = dot(oc, oc) - tca * tca ;
    float R2 = Range * Range ;
    if (d2 > R2) return 0 ;
    float x2 = d2 / R2 ;
    float glow = pow(saturate(1.0 - x2), 2.0) ;
    float chord = 2.0 * sqrt(R2 - d2) / (2.0 * Range) ;
    float3 col = i.LightSourceColor * glow * (0.35 + 0.65 * chord) ;

    if (rd.z < -0.001) {
        float tz = -ro.z / rd.z ;
        float3 hit = ro + rd * tz ;
        float3 lv = (i.LightCenter - hit) / Range ;
        float dsq = dot(lv, lv) ;
        col += i.LightSourceColor * pow(saturate(1.0 - dsq), 2.5) * 0.8 ;
    }

    col = clamp(col, 0, HDRMultiplier.xxx) ;
    return float4(col , 1) ;
}

technique Default
{    pass p0
    {
    VertexShader = compile vs_3_0 VSpointlight();
    PixelShader  = compile ps_3_0 PSpointlight();
    ZEnable = 1;
    ZFunc = D3DCMP_GREATEREQUAL ;
    ZWriteEnable = 0;
    CullMode = D3DCULL_CCW ;
	AlphaTestEnable = 0;
	AlphaBlendEnable = 1;
	SrcBlend = ONE ;
	DestBlend = ONE ;
    }
}

technique Default_M
{    pass p0
    {
    VertexShader = compile vs_3_0 VSpointlight();
    PixelShader  = compile ps_3_0 PSpointlight();
    ZEnable = 0;
    ZWriteEnable = 0;
    CullMode = D3DCULL_CCW ;
	AlphaTestEnable = 0;
	AlphaBlendEnable = 0;
    }
}
'''
for t in [r'D:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3\w3x_pointlight.fx',
          r'E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Shaders\RA3\w3x_pointlight.fx']:
    open(t, 'w', encoding='utf-8', newline='\r\n').write(FX)
    print('v2.1 written:', t, os.path.getsize(t))
