// deferred_tonemap.ps.hlsl — Reinhard tone mapping + gamma
// Compile: fxc /T ps_3_0 /E main /Fo deferred_tonemap.ps.fxo deferred_tonemap.ps.hlsl

struct PS_IN {
	float4 pos : POSITION;
	float2 tex0 : TEXCOORD0;
};
sampler hdrSampler : register(s0);
float4 main(PS_IN input) : COLOR {
	float3 hdrColor = tex2D(hdrSampler, input.tex0).rgb;
	float3 ldr = hdrColor / (hdrColor + 1.0);
	ldr = sqrt(abs(ldr));
	return float4(ldr, 1.0);
};
