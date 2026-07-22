// deferred_sunlight.ps.hlsl — Sunlight PBR deferred lighting
// Compile: fxc /T ps_3_0 /E main /Fo deferred_sunlight.ps.fxo deferred_sunlight.ps.hlsl

struct PS_IN {
	float4 pos : POSITION;
	float2 tex0 : TEXCOORD0;
};
sampler gbuf0 : register(s0);
sampler gbuf1 : register(s1);
sampler gbuf2 : register(s2);
sampler sShroud : register(s3);
samplerCUBE sIblDiff : register(s5);
samplerCUBE sIblSpec : register(s6);
sampler2D sBrdfLUT : register(s7);

float4 c0 : register(c0);  // sunDir
float4 c1 : register(c1);  // sunColor
float4 c2 : register(c2);  // cameraPos.xyz + ambient.w
float4 c3 : register(c3);  // invViewProj row 0
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c13 : register(c13); // gamma flag
float4 c14 : register(c14); // PBRDebugMode (0=off, 1-7=debug modes)

float3 octDecode(float2 e) {
	float2 p = e * 2.0 - 1.0;
	float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
	float t = max(-n.z, 0.0);
	n.xy += (n.xy >= 0) ? -t : t;
	return normalize(n);
}

float4 main(PS_IN input) : COLOR {
	float4 rt0 = tex2D(gbuf0, input.tex0);
	float4 rt1 = tex2D(gbuf1, input.tex0);
	float4 rt2 = tex2D(gbuf2, input.tex0);
	float3 albedo = rt0.rgb;
	albedo *= albedo;
	float metallic = rt0.a;
	float3 N = octDecode(rt1.rg);
	float roughness = rt1.b;
	float depth = rt2.r;
	float3 emissive = rt2.b;
	float2 screenPos = input.tex0 * 2.0 - 1.0;
	float4 clipPos = float4(screenPos, depth, 1.0);
	float4 worldPos4 = float4(
		dot(clipPos, float4(c3.x, c4.x, c5.x, c6.x)),
		dot(clipPos, float4(c3.y, c4.y, c5.y, c6.y)),
		dot(clipPos, float4(c3.z, c4.z, c5.z, c6.z)),
		dot(clipPos, float4(c3.w, c4.w, c5.w, c6.w))
	);
	float3 worldPos = worldPos4.xyz / worldPos4.w;
	float3 V = normalize(c2.xyz - worldPos);
	float3 L = -c0.xyz;
	float3 H = normalize(V + L);
	float NdotL = saturate(dot(N, L));
	float NdotV = saturate(dot(N, V));
	float NdotH = saturate(dot(N, H));
	float HdotV = saturate(dot(H, V));
	float alpha = max(roughness * roughness, 0.001);
	float alpha2 = alpha * alpha;
	float NdotH2 = NdotH * NdotH;
	float denom = NdotH2 * (alpha2 - 1.0) + 1.0;
	float D = alpha2 / (3.14159 * denom * denom);
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	float G1 = NdotL / (NdotL * (1.0 - k) + k);
	float G2 = NdotV / (NdotV * (1.0 - k) + k);
	float G = G1 * G2;
	float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	float3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
	float3 spec = D * F * G / max(4.0 * NdotV * NdotL, 0.001);
	float3 kD = (1.0 - F) * (1.0 - metallic);
	float3 diffuse = albedo * kD;
	float3 R = reflect(-V, N);
	float3 diffuseIBL = texCUBE(sIblDiff, N).rgb * c2.w * 0.5;
	float3 specularIBL = texCUBElod(sIblSpec, float4(R, roughness * 7.0)).rgb * 0.3;
	float2 brdf = tex2D(sBrdfLUT, float2(NdotV, roughness)).rg;
	float3 F0_ibl = lerp(float3(0.04,0.04,0.04), albedo, metallic);
	float3 specIBL = specularIBL * (F0_ibl * brdf.x + brdf.y);
	float3 ambientLight = diffuseIBL * albedo;
	float3 direct = (diffuse + spec) * c1.rgb * NdotL;
	float3 finalColor = ambientLight + direct + specIBL + emissive;
	float shroudVis = tex2D(sShroud, input.tex0).a;
	finalColor = lerp(float3(0,0,0), finalColor, shroudVis);
	// Debug visualization (PBRDebugMode in c14.x)
	if (c14.x > 6.5) finalColor = direct + emissive;
	else if (c14.x > 5.5) finalColor = specIBL;
	else if (c14.x > 4.5) finalColor = diffuseIBL * albedo;
	else if (c14.x > 3.5) finalColor = N * 0.5 + 0.5;
	else if (c14.x > 2.5) finalColor = ambientLight;
	else if (c14.x > 1.5) finalColor = float3(roughness,roughness,roughness);
	else if (c14.x > 0.5) finalColor = float3(metallic,metallic,metallic);
	if (c13.x >= 0.5) finalColor = sqrt(abs(finalColor));
	return float4(finalColor, 1.0);
};
