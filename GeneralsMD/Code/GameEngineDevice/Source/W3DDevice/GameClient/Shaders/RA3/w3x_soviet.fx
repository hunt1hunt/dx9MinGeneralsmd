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
float4 PS_ShadowDepth(float4 shadowCS : TEXCOORD1) : COLOR
{
    // shadowCS = mul(worldPos, ShadowMapWorldToShadow); the sun camera is
    // orthographic so w == 1 and ndcZ is already the [0,1] depth value.
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
