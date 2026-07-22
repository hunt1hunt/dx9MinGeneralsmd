// deferred_sunshadow.ps.hlsl — Sunlight PBR + shadow map PCF 2x2
// Compile: fxc /T ps_3_0 /E main /Fo deferred_sunshadow.ps.fxo deferred_sunshadow.ps.hlsl

struct PS_IN {
	float4 pos : POSITION;
	float2 tex0 : TEXCOORD0;
};
sampler gbuf0 : register(s0);
sampler gbuf1 : register(s1);
sampler gbuf2 : register(s2);
sampler sShadow : register(s4);
samplerCUBE sIblDiff : register(s5);
samplerCUBE sIblSpec : register(s6);
sampler2D sBrdfLUT : register(s7);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4x4 shadowVP : register(c9);
float4 c13 : register(c13);

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
	float4 worldPos = float4(
		dot(clipPos, float4(c3.x,c4.x,c5.x,c6.x)),
		dot(clipPos, float4(c3.y,c4.y,c5.y,c6.y)),
		dot(clipPos, float4(c3.z,c4.z,c5.z,c6.z)),
		dot(clipPos, float4(c3.w,c4.w,c5.w,c6.w)));
	worldPos.xyz /= worldPos.w;
	float3 V = normalize(c2.xyz - worldPos.xyz);
	float3 L = -c0.xyz;
	float3 H = normalize(V + L);
	// Shadow map PCF 2x2
	float4 shadProj = mul(float4(worldPos.xyz, 1), shadowVP);
	float2 shadUV = shadProj.xy / shadProj.w;
	shadUV = shadUV * 0.5 + 0.5;
	float shadDepth = shadProj.z / shadProj.w;
	float bias = 0.002;
	float2 texelSize = 1.0 / 2048;
	float shadow = 0.0;
	for (int x = -1; x <= 1; x += 2) {
		for (int y = -1; y <= 1; y += 2) {
			float d = tex2D(sShadow, shadUV + float2(x,y)*texelSize).r;
			shadow += (shadDepth - bias) > d ? 1.0 : 0.0;
		}
	}
	shadow *= 0.25;
	float NdotL = saturate(dot(N, L));
	float NdotV = saturate(dot(N, V));
	float NdotH = saturate(dot(N, H));
	float HdotV = saturate(dot(H, V));
	float alpha = max(roughness * roughness, 0.001);
	float a2 = alpha * alpha;
	float denom = NdotH*NdotH*(a2-1.0)+1.0;
	float D = a2 / (3.14159*denom*denom);
	float k = (roughness+1)*(roughness+1)/8.0;
	float G1 = NdotL/(NdotL*(1-k)+k);
	float G2 = NdotV/(NdotV*(1-k)+k);
	float G = G1*G2;
	float3 F0 = lerp(float3(0.04,0.04,0.04), albedo, metallic);
	float3 F = F0 + (1-F0)*pow(1-HdotV,5);
	float3 spec = D*F*G/max(4*NdotV*NdotL,0.001);
	float3 kD = (1-F)*(1-metallic);
	float3 direct = (albedo*kD + spec) * c1.rgb * NdotL * shadow;
	float3 R = reflect(-V, N);
	float3 diffuseIBL = texCUBE(sIblDiff, N).rgb * c2.w * 0.25;
	float3 specularIBL = texCUBElod(sIblSpec, float4(R, roughness * 7.0)).rgb * 0.3;
	float2 brdf = tex2D(sBrdfLUT, float2(NdotV, roughness)).rg;
	float3 F0_ibl = lerp(float3(0.04,0.04,0.04), albedo, metallic);
	float3 specIBL = specularIBL * (F0_ibl * brdf.x + brdf.y);
	float3 ambient = diffuseIBL * albedo;
	float3 final = ambient + direct + specIBL + emissive;
	if (c13.x >= 0.5) final = sqrt(abs(final));
	return float4(final, 1.0);
};
