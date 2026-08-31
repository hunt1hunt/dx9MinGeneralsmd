// w3x_soviet.fx - REAL RA3 PBR shader, SAS-free technique override
// Same macros as objectssoviet.fx, but the technique compiles VS_H_11skin +
// PS_H_ARPBR directly instead of the SAS dynamic array (VS_H_Array[VSchooserExpr()])
// which this D3DX9 build cannot parse (X3116 flags invalid).
//
// REPLACE_DEFAULT_TECHNIQUE: PBR5-10-objects-ARPBR.FX skips its Default /
// Default_M / _CreateShadowMap techniques (they use SAS expression assignment).
// The override technique below restores the same render state manually.

#define DYNAMIC_CLOUD_REF
#define ALLOW_STEALTH
#define OPACITY_OVERRIDE_OUTPUT
#define ALLOW_CLIP_TEXTURE_ALPHA
#define ALLOW_CLIP_VERTEX_ALPHA

#define REPLACE_DEFAULT_TECHNIQUE

#include "Shaders/RA3/PBR5-10-objects-ARPBR.FX"

technique Default
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_H_11skin();
        PixelShader  = compile ps_3_0 PS_H_ARPBR();
        ZEnable = 1;
        ZFunc = 4;
        ZWriteEnable = 1;
        CullMode = 2;
        AlphaFunc = 7;
        AlphaRef = 95;
        AlphaBlendEnable = (OpacityOverride < 0.985);
        AlphaTestEnable = 0;
    }
}

// ----------------------------------------------------------------------------
// Shadow-map depth pass. The W3X shadow-map COLOR RT stores the sun-space depth
// as color (D16 depth textures are not reliably sampleable under dgVoodoo2, so
// depth is rendered into the color RT instead of relying on the depth texture).
// ----------------------------------------------------------------------------
// RA3-style shadow-map cast PS, matching PS_ShadowMaker_*: RA3 selects between
// PS_ShadowMaker_NoAlphaTest (plain depth, no clip) and PS_ShadowMaker_ARPBR
// (clip by diffuse alpha) via SMPSchooserExpr() == AlphaTestEnable. Match that:
// only when the mesh declares AlphaTestEnable=true (wire fences / grille
// lattices) do we sample the diffuse alpha and clip transparent pixels, so a
// LATTICE casts a lattice shadow (gaps don't write depth) while solid bodies
// (AlphaTestEnable=false -> extra_alpha stays 1) keep their full silhouette.
// Without this gate, sampling DiffuseEasySampler on a mesh whose DiffuseTexture
// is unbound returned 0 -> clip(-0.375) discarded EVERY pixel -> shadow COLOR
// RT stayed empty (the all-white dump).
float4 PS_ShadowDepth(float4 MainTexUV : TEXCOORD0, float4 shadowCS : TEXCOORD1) : COLOR
{
    float extra_alpha = 1;
    // RA3 SMPSchooserExpr(): clip only for alpha-test meshes (fence/grille).
    if (AlphaTestEnable) {
        extra_alpha *= tex2D(DiffuseEasySampler, MainTexUV.xy).w;
        clip(extra_alpha - 0.375);
    }

    // shadowCS = mul(worldPos, ShadowMapWorldToShadow); orthographic so w == 1
    // and ndcZ is already the [0,1] depth value.
    float ndcZ = shadowCS.z / max(shadowCS.w, 1e-6f);
    return float4(ndcZ, ndcZ, ndcZ, 1.0f);
}

technique ShadowDepth
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_H_11skin();
        PixelShader  = compile ps_3_0 PS_ShadowDepth();
        ZEnable = 1;
        ZFunc = 4;          // D3DCMP_LESS
        ZWriteEnable = 1;
        CullMode = 1;       // D3DCULL_NONE
        AlphaBlendEnable = 0;
        AlphaTestEnable = 0;
    }
}
