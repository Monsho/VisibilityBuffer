#include "cbuffer.hlsli"
#include "math.hlsli"
#include "vrs.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
	float2	uv			: TEXCOORD;
};

struct PSOutput
{
	float4	color	: SV_TARGET0;
};

ConstantBuffer<DebugCB>	cbDebug		: register(b0);
Texture2D			texSource		: register(t0);
Texture2D<uint>		texVRS			: register(t1);
SamplerState		samLinear		: register(s0);

PSOutput main(PSInput In)
{
	PSOutput Out = (PSOutput)0;

	float3 color = 0;
	switch (cbDebug.displayMode)
	{
	case 2: // Roughness
		color = texSource.SampleLevel(samLinear, In.uv, 0).ggg; break;
	case 3: // Metallic
		color = texSource.SampleLevel(samLinear, In.uv, 0).bbb; break;
	case 5: // AO
		color = texSource.SampleLevel(samLinear, In.uv, 0).xxx; break;
	case 8: // VRS
		{
			int vrs = texVRS.SampleLevel(samLinear, In.uv, 0);
			if (vrs == VRS_1x2)
				color = float3(1, 0, 0);
			else if (vrs == VRS_2x1)
				color = float3(0, 1, 0);
			else if (vrs == VRS_2x2)
				color = float3(0, 0, 1);
		}
		break;
	default:
		color = texSource.SampleLevel(samLinear, In.uv, 0);
	}

	Out.color = float4(color, 1);
	return Out;
}
