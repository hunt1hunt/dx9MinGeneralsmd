// w3x_rastest.fx - ISOLATION TEST
// Uses the REAL RA3 vertex shader (VS_H_11skin, with bone skinning) but a
// SIMPLE pixel shader (raw diffuse texture, no RA3 lighting/alpha/house-color).
//
// Purpose: determine whether the RA3 PS (PS_H_ARPBR) is what makes the model
// invisible, or whether the skinned VS / bones are the problem.
//   - model visible + turret in place  -> VS + bones OK, RA3 PS is the issue
//   - model visible + turret misplaced -> bones (WorldBones values) wrong
//   - model still invisible            -> VS / technique / SAS is the issue

#define DYNAMIC_CLOUD_REF
#define ALLOW_STEALTH
#define OPACITY_OVERRIDE_OUTPUT
#define ALLOW_CLIP_TEXTURE_ALPHA
#define ALLOW_CLIP_VERTEX_ALPHA

#include "head3-vsps.FXH"   // brings head0 (params) + head2 (helpers) + VS_H_00skin/11skin

// Simple PS: raw diffuse texture
texture DiffuseTexture <string UIName="(BASE)DiffuseTexture";>;
sampler2D DiffuseTextureSampler = sampler_state {
    Texture = < DiffuseTexture >;
    MinFilter = 2;
    MagFilter = 2;
    MipFilter = 2;
    AddressU = Wrap;
    AddressV = Wrap;
};

float4 W3XSimplePS(VS_H_output i) : COLOR
{
    // TEMP: flat RED — isolates whether the skinned VS produces valid geometry
    // (if the model shows red, VS+geometry work and the invisibility is the
    // texture binding; if still invisible, the VS/geometry is the problem).
    return float4(1, 0, 0, 1);
}

// This D3DX9 build does NOT support the SAS flag, so VS_H_Array[VSchooserExpr()]
// dynamic selection cannot be used. Compile the skinned VS directly instead.
technique RasterTest
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_H_11skin();
        PixelShader  = compile ps_3_0 W3XSimplePS();
    }
}
