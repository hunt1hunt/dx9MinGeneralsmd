// w3x_tread.fx - RA3 tread/vehicle-wheel PBR shader, SAS-free override
// Same macros as objectsalliedtread.fx, but the technique compiles VS_H_11skin
// + PS_H_ARPBR directly instead of the SAS dynamic array (VS_H_Array[...])
// which this D3DX9 build rejects (X3116 flags invalid).
//
// KEY DIFFERENCE from w3x_soviet.fx: SUPPORT_TREAD_SCROLLING is defined, so
// the VS advances MainTexUV.x by the per-vertex color alpha (o.VertexColor.a)
// every frame -> tread texture scrolls. The engine must feed a scrolling
// alpha value in the vertex color (0..1 loop) for the effect to be visible.

#define FORBID_FACTION_COLOR
#define ALLOW_STEALTH
#define FORBID_SHADOW_ALPHATEST
#define OPACITY_OVERRIDE_OUTPUT
#define ALLOW_CLIP_TEXTURE_ALPHA
#define SUPPORT_TREAD_SCROLLING

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
