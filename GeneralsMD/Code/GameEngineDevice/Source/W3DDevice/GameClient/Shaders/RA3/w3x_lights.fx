// w3x_lights.fx - additive emissive lamp shader for W3X building SKIN_LIGHT
// meshes (China Command Center / Barracks / War Factory + every faction's
// SKIN_LIGHT01/02 lamp quads).
//
// These meshes are authored as RA3-style light quads: defaultw3d.fx with
// ColorEmissive=(1,1,1) x EmissiveHDRMultipler(~3) and BlendMode=2 (additive,
// no depth write). The generic PBR remap (w3x_soviet.fx) ignored all of that
// and rendered them as dark opaque solids. This shader restores the authored
// look: ONE additive pass, unlit, tint = Texture_0 (TunLight02) x emissive
// constants, depth-TESTED (a lamp behind a wall is hidden) but never written.
//
// The second authored texture (FX2ndPassA, an animated overlay) has no asset
// anywhere in the game data - skipped here, can be added later.
#define DYNAMIC_CLOUD_REF

#include "Shaders/RA3/head1-newvs.FXH"

// ----------------------------------------------------------------------------
// Material parameters - exact authored constant names, bound by the engine's
// per-sub-mesh BindW3XConstants (Texture_0 = TunLight02 in every SKIN_LIGHT).
// ----------------------------------------------------------------------------
texture Texture_0
<string UIName = "Texture_0";>;

sampler2D Texture_0Sampler = sampler_state {
    Texture = <Texture_0>;
    MinFilter = 2;
    MagFilter = 2;
    MipFilter = 2;
};

float3 ColorEmissive = float3(1, 1, 1);
float EmissiveHDRMultipler = 1.0;

// ----------------------------------------------------------------------------
// Pixel shader: additive emissive = Texture_0 x ColorEmissive x EmissiveHDRMultipler.
// Shroud multiply keeps lamps consistent with every other W3X pass (the engine
// binds the shared white fallback where no real shroud applies).
// ----------------------------------------------------------------------------
float4 PS_W3X_Lights(VS_H_output i) : COLOR
{
    float4 t0 = tex2D(Texture_0Sampler, i.MainTexUV.xy);
    float3 color = t0.rgb * ColorEmissive * EmissiveHDRMultipler;
    float3 shroud = tex2D(ShroudTextureSampler, i.FogCloudUV.xy).rgb;
    color *= shroud;
    return float4(color, 1.0);
}

// ----------------------------------------------------------------------------
// Technique index 0 = the authored TechniqueIndex of every SKIN_LIGHT mesh.
// ----------------------------------------------------------------------------
technique Default
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_H_Unified(1);
        PixelShader  = compile ps_3_0 PS_W3X_Lights();
        ZEnable = 1;
        ZFunc = 4;
        ZWriteEnable = 0;
        CullMode = 1;           // D3DCULL_NONE - lamp quads are double-sided
        AlphaBlendEnable = 1;
        SrcBlend = ONE;
        DestBlend = ONE;
        AlphaTestEnable = 0;
    }
}
