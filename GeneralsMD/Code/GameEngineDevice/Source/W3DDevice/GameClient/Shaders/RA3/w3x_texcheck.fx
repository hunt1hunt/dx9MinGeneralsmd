// w3x_texcheck.fx - TEMP diagnostic: output raw DiffuseTexture (no lighting,
// no house-color, no gamma). If the camo pattern appears, texture sampling +
// UVs work and the problem is in the RA3 shader color processing. If the model
// stays magenta/flat, the texture is not reaching the sampler.
// Sampler/texture declaration MATCHES PBR5-10-objects-CORONA.FX exactly
// (the `string Texture` annotation + MaxAnisotropy are required for binding).

float4x4 WorldViewProj;

texture DiffuseTexture
<
    string UIName = "(BASE)DiffuseTexture";
    string ResourceMapType = "Normal";
    string TextureColorSpace = "Linear";
>;

sampler2D DiffuseTextureSampler
<string Texture = "DiffuseTexture";> = sampler_state {
    Texture = < DiffuseTexture >;
    MinFilter = 2;
    MagFilter = 2;
    MipFilter = 2;
    MaxAnisotropy = 4;
    AddressU = Wrap;
    AddressV = Wrap;
};

struct VS_IN
{
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VS_OUT
{
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VS_OUT vs_main(VS_IN v)
{
    VS_OUT o;
    o.pos = mul(v.pos, WorldViewProj);
    o.uv  = v.uv;
    return o;
}

float4 ps_main(VS_OUT i) : COLOR
{
    // After the SetTechnique-before-SetTexture fix, this should now show the
    // actual DiffuseTexture. If the camo pattern appears, texture binding works.
    return tex2D(DiffuseTextureSampler, i.uv);
}

technique TexCheck
{
    pass p0
    {
        VertexShader = compile vs_3_0 vs_main();
        PixelShader  = compile ps_3_0 ps_main();
    }
}
