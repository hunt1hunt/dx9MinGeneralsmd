// deferred_ssao_blur.ps.hlsl — SSAO depth-aware blur (3x3)
// Compile: fxc /T ps_3_0 /E main /Fo deferred_ssao_blur.ps.fxo deferred_ssao_blur.ps.hlsl

struct PS_IN { float4 pos:POSITION; float2 tex0:TEXCOORD0; };
sampler sAO : register(s0);
sampler sDepth : register(s1);
float4 c0 : register(c0);

float4 main(PS_IN i):COLOR {
  float cd = tex2D(sDepth, i.tex0).r;
  float aoSum = 0; float wSum = 0;
  float2 ts = c0.xy;
  [unroll] for (int x = -1; x <= 1; x++) {
    [unroll] for (int y = -1; y <= 1; y++) {
      float2 uv = i.tex0 + float2(x,y) * ts;
      float d = tex2D(sDepth, uv).r;
      float w = exp(-abs(d-cd)*10) * 1.0/9.0;
      aoSum += tex2D(sAO, uv).r * w; wSum += w;
    }
  }
  return float4(aoSum/max(wSum,0.001), 0, 0, 1);
};
