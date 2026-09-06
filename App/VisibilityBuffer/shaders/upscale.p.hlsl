struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

Texture2D<float3> texSource : register(t0);
SamplerState samLinear : register(s0);

float4 main(PSInput input) : SV_TARGET0
{
	return float4(texSource.SampleLevel(samLinear, input.uv, 0.0), 1.0);
}