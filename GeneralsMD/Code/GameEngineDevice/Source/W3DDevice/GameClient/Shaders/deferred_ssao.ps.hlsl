// deferred_ssao.ps.hlsl — SSAO compute: 16 samples, random rotation, normal-weighted
// Compile: fxc /T ps_3_0 /E main /Fo deferred_ssao.ps.fxo deferred_ssao.ps.hlsl

struct PS_IN { float4 pos:POSITION; float2 tex0:TEXCOORD0; };
sampler sDepth : register(s0);
sampler sNormal : register(s1);
float4 c4 : register(c4);

float hash(float2 p) { return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453); }

float4 main(PS_IN i):COLOR {
  float depth = tex2D(sDepth, i.tex0).r;
  float3 N = normalize(tex2D(sNormal, i.tex0).rgb * 2 - 1);
  float radius = c4.x * 0.5 / max(depth, 0.001);
  float2 ts = float2(1.0/1920, 1.0/1080);
  float angle = hash(i.tex0) * 6.283;
  float s = sin(angle), c = cos(angle);
  float ao = 0;
  for (int j = 0; j < 4; j++) {
    float2 dir = float2(cos(j*1.571), sin(j*1.571));
    float2 rd = float2(dir.x*c - dir.y*s, dir.x*s + dir.y*c);
    for (int k = 1; k <= 4; k++) {
      float2 uv = i.tex0 + rd * (radius * k * 0.2) * ts;
      float d = tex2D(sDepth, uv).r;
      float diff = depth - d;
      float range = abs(diff) < radius * 0.5 ? 1.0 : 0.0;
      float3 sn = normalize(tex2D(sNormal, uv).rgb * 2 - 1);
      float normW = max(0, dot(N, sn));
      ao += max(0, diff / (depth + 0.001)) * range * normW;
    }
  }
  return float4(saturate(1 - ao * c4.y / 16), 0, 0, 1);
};
