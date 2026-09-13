// deferred_tonemap.ps.hlsl — P2 HDR: exposure + Reinhard white point + ACES
// Compile: fxc /T ps_3_0 /E main /Fo deferred_tonemap.ps.fxo deferred_tonemap.ps.hlsl
// c0 = (HDRExposure, HDRWhitePoint, ToneMapMode, 1) from GameData.ini:
//   exposure   : linear pre-curve multiplier
//   whitePoint : extended-Reinhard white point (W -> 1.0)
//   mode       : 0 = extended Reinhard, 1 = ACES filmic (Narkowicz fit)

struct PS_IN {
	float4 pos : POSITION;
	float2 tex0 : TEXCOORD0;
};
sampler hdrSampler : register(s0);
float4 tmParams : register(c0);
float4 main(PS_IN input) : COLOR {
	float3 hdrColor = tex2D(hdrSampler, input.tex0).rgb * tmParams.x;
	float wp2 = tmParams.y * tmParams.y;
	float3 rein = hdrColor * (1.0 + hdrColor / wp2) / (1.0 + hdrColor);
	float3 aces = saturate((hdrColor * (2.51 * hdrColor + 0.03)) / (hdrColor * (2.43 * hdrColor + 0.59) + 0.14));
	float3 ldr = lerp(rein, aces, saturate(tmParams.z));
	ldr = sqrt(abs(ldr));
	return float4(ldr, 1.0);
};
