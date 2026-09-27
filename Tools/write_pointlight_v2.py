import os

FX = r'''// w3x_pointlight.fx - v2.0 VIEW-SPACE volumetric point light
//
// Architecture per NordLicht's technique note: the PS never needs the camera
// position - move ALL lighting math into VIEW SPACE where the camera sits at
// the origin by definition. The VS passes through every required quantity
// (view-space pixel = the ray itself, view-space light center, the ground
// plane in view space). This depends ONLY on matrix parameters (World / View
// / ViewProjection) which are the PROVEN-bound path in this engine - the
// EyePosition engine parameter verifiably never reaches the PS (BindEngine-
// Constants logs the SetVector with valid values, the PS still reads zeros -
// a D3DX apply-layer quirk on this effect), so v2.0 eliminates it entirely.
//
// Glow = analytic point-line falloff (distance from the view ray to the light
// center, soft round, no scene depth needed) + a radial pool where the ray
// crosses the ground plane z=0. Occlusion comes free from the main z-test
// (backface volume + ZFunc GREATEREQUAL, RA3 state set).
#define DEFERRED_RENDER
#define DYNAMIC_CLOUD_REF

#include "Shaders/RA3/head1-newvs.FXH"

// ----------------------------------------------------------------------------
// Material parameters - per-mesh constants, engine binds by name (the same
// proven path w3x_lights.fx uses: Range / ColorEmissive / HDRMultiplier).
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// Vertex shader: unit cube -> Range-scaled volume; EVERYTHING the PS needs is
// precomputed here in view space (camera = origin).
// ----------------------------------------------------------------------------
struct VSout_pl
{
	float4 Position       : POSITION;
    float3 ViewPos        : TEXCOORD1;   // view-space pixel: ray = normalize(ViewPos)
    float3 LightCenterV   : TEXCOORD2;   // light center in view space
    float3 GroundN        : TEXCOORD3;   // ground plane (z_world=0) normal, view space
    float3 GroundP0       : TEXCOORD4;   // ground plane origin, view space
    float3 LightSourceColor : TEXCOORD5;
};

VSout_pl VSpointlight(float3 Position : POSITION)
{
    VSout_pl o;

    float3 objPos = Position * Range ;
    float3 center2world = World[3].xyz ;
    float4 worldPos4D = float4( float3(objPos + center2world), 1);

    float4 viewPos4 = mul(worldPos4D, View) ;
    o.ViewPos = viewPos4.xyz ;
    o.Position = mul(worldPos4D, ViewProjection) ;

    o.LightCenterV = mul(float4(center2world, 1.0), View).xyz ;

    // ground plane z_world = 0, expressed in view space
    o.GroundN  = mul(float4(0, 0, 1, 0), View).xyz ;   // world +Z direction
    o.GroundP0 = mul(float4(0, 0, 0, 1), View).xyz ;   // world origin

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

// ----------------------------------------------------------------------------
// Pixel shader: view-space analytic glow + ground-plane pool. Camera at the
// origin - no EyePosition anywhere.
// ----------------------------------------------------------------------------
float4 PSpointlight(VSout_pl i) : COLOR
{
    float3 rd = normalize(i.ViewPos) ;               // ray from camera (origin)
    float3 oc = -i.LightCenterV ;                    // ro(0) - center
    float tca = dot(oc, rd) ;
    if (tca < 0.0) return 0 ;                        // light behind the camera
    float d2 = dot(oc, oc) - tca * tca ;             // squared ray-center distance
    float R2 = Range * Range ;
    if (d2 > R2) return 0 ;                          // ray misses the light sphere
    float x2 = d2 / R2 ;
    float glow = pow(saturate(1.0 - x2), 2.0) ;      // round soft falloff
    float chord = 2.0 * sqrt(R2 - d2) / (2.0 * Range) ;
    float3 col = i.LightSourceColor * glow * (0.35 + 0.65 * chord) ;

    // ground pool: ray crosses the plane (GroundN, GroundP0)?
    float dn = dot(rd, i.GroundN) ;
    if (dn < -0.001) {
        float t = dot(i.GroundP0, i.GroundN) / dn ;
        if (t > 0.0) {
            float3 hit = rd * t ;                    // view-space hit
            float3 lv = (i.LightCenterV - hit) / Range ;
            float dsq = dot(lv, lv) ;
            col += i.LightSourceColor * pow(saturate(1.0 - dsq), 2.5) * 0.8 ;
        }
    }

    col = clamp(col, 0, HDRMultiplier.xxx) ;
    return float4(col , 1) ;
}

// ----------------------------------------------------------------------------
// Technique: RA3 volume states (backface + GREATEREQUAL far-hemisphere trick,
// additive, no ZWrite). Technique[0] = the authored TechniqueIndex of every
// FX_LIGHT mesh; [1] = the nothing-pass low detail variant.
// ----------------------------------------------------------------------------
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
    print('v2.0 written:', t, os.path.getsize(t))
