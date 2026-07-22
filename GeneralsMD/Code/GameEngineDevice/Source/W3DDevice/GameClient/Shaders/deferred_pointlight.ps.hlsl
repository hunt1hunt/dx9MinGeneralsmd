// deferred_pointlight.ps.hlsl — Point light PBR deferred (additive)
// Compile: fxc /T ps_3_0 /E main /Fo deferred_pointlight.ps.fxo deferred_pointlight.ps.hlsl

struct PS_IN { float4 pos:POSITION; float2 tex0:TEXCOORD0; };
sampler gbuf0 : register(s0);
sampler gbuf1 : register(s1);
sampler gbuf2 : register(s2);
float4 c2 : register(c2);   // cameraPos.xyz
float4 c3 : register(c3);   // invViewProj row 0
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c7 : register(c7);   // lightPos.xyz + range.w
float4 c8 : register(c8);   // lightColor.rgb
float4 c13 : register(c13); // gamma flag

float2 octDecode(float2 e) {
	float2 p = e * 2.0 - 1.0;
	float nz = 1.0 - abs(p.x) - abs(p.y);
	float t = max(-nz, 0.0);
	return p + (p >= 0 ? -t : t);
}
float4 main(PS_IN input) : COLOR {
  float4 rt0 = tex2D(gbuf0, input.tex0);
  float4 rt1 = tex2D(gbuf1, input.tex0);
  float4 rt2 = tex2D(gbuf2, input.tex0);
  float3 albedo = rt0.rgb;
  albedo *= albedo;
  float metallic = rt0.a;
  float3 N_enc = tex2D(gbuf1, input.tex0).rgb;
  float2 enc = float2(octDecode(rt1.rg));
  float nz = 1.0 - abs(enc.x) - abs(enc.y);
  float3 N = float3(enc.x, enc.y, nz);
  float t = max(-nz, 0.0);
  N.xy += (N.xy >= 0) ? -t : t;
  N = normalize(N);
  float roughness = rt1.b;
  float depth = rt2.r;
  float2 screenPos = input.tex0 * 2.0 - 1.0;
  float4 clipPos = float4(screenPos, depth, 1.0);
  float4 worldPos4 = float4(
    dot(clipPos, float4(c3.x,c4.x,c5.x,c6.x)),
    dot(clipPos, float4(c3.y,c4.y,c5.y,c6.y)),
    dot(clipPos, float4(c3.z,c4.z,c5.z,c6.z)),
    dot(clipPos, float4(c3.w,c4.w,c5.w,c6.w)));
  float3 worldPos = worldPos4.xyz / worldPos4.w;
  float3 V = normalize(c2.xyz - worldPos);
  float3 L = c7.xyz - worldPos;
  float dist = length(L); L /= max(dist, 0.001);
  float atten = 1.0 - saturate(dist / c7.w);
  atten *= atten;
  float NdotL = saturate(dot(N, L));
  float3 H = normalize(V + L);
  float NdotV = saturate(dot(N, V));
  float NdotH = saturate(dot(N, H));
  float HdotV = saturate(dot(H, V));
  float a = max(roughness*roughness, 0.001);
  float a2 = a*a;
  float denom = NdotH*NdotH*(a2-1.0)+1.0;
  float D = a2/(3.14159*denom*denom);
  float k = (roughness+1)*(roughness+1)/8.0;
  float G1 = NdotL/(NdotL*(1-k)+k);
  float G2 = NdotV/(NdotV*(1-k)+k);
  float G = G1*G2;
  float3 F0 = lerp(float3(0.04,0.04,0.04), albedo, metallic);
  float3 F = F0 + (1-F0)*pow(1-HdotV,5);
  float3 spec = D*F*G / max(4*NdotV*NdotL, 0.001);
  float3 kD = (1-F)*(1-metallic);
  float3 diffuse = albedo * kD;
  float3 direct = (diffuse + spec) * c8.rgb * NdotL * atten;
  if (c13.x >= 0.5) direct = sqrt(abs(direct));
  return float4(direct, 0.0);
};
