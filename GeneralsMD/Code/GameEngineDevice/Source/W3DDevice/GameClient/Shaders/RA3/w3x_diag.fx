// W3X Diagnostic shader - renders the model with a flat red color
// Purpose: isolate whether invisibility is due to RA3 shader complexity
// or fundamental vertex/matrix/render-pipeline issues.
float4x4 WorldViewProj;

struct VS_IN
{
    float4 pos : POSITION;
};

struct VS_OUT
{
    float4 pos : POSITION;
};

VS_OUT main(VS_IN input)
{
    VS_OUT output;
    output.pos = mul(input.pos, WorldViewProj);
    return output;
}

float4 PS(VS_OUT input) : COLOR
{
    return float4(1.0, 0.2, 0.2, 1.0);  // flat red
}

technique Default
{
    pass P0
    {
        VertexShader = compile vs_2_0 main();
        PixelShader = compile ps_2_0 PS();
        CullMode = 1;          // no culling
        ZEnable = 1;
        ZWriteEnable = 1;
        AlphaBlendEnable = 0;
    }
}
