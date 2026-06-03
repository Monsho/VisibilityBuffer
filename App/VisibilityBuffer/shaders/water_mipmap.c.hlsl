#include "cbuffer.hlsli"

struct WaterMipmapCB
{
	uint2 sourceSize;
	uint2 pad;
};

ConstantBuffer<WaterMipmapCB> cbMipmap : register(b0);

Texture2D<float> rDepth : register(t0);
Texture2D<float4> rNormal : register(t1);

RWTexture2D<float> rwDepthMip0 : register(u0);
RWTexture2D<float> rwDepthMip1 : register(u1);
RWTexture2D<float> rwDepthMip2 : register(u2);
RWTexture2D<float> rwDepthMip3 : register(u3);
RWTexture2D<float> rwDepthMip4 : register(u4);
RWTexture2D<float4> rwNormalMip0 : register(u5);
RWTexture2D<float4> rwNormalMip1 : register(u6);
RWTexture2D<float4> rwNormalMip2 : register(u7);
RWTexture2D<float4> rwNormalMip3 : register(u8);
RWTexture2D<float4> rwNormalMip4 : register(u9);

groupshared float gsDepth[16][16];
groupshared float4 gsNormal[16][16];

float3 DecodeNormal(float4 v)
{
	return v.xyz * 2.0 - 1.0;
}

float4 EncodeNormal(float3 normal, float alpha)
{
	return float4(normal * 0.5 + 0.5, alpha);
}

float4 ReduceNormal(float4 n0, float4 n1, float4 n2, float4 n3)
{
	float3 normal = DecodeNormal(n0) + DecodeNormal(n1) + DecodeNormal(n2) + DecodeNormal(n3);
	float lenSq = dot(normal, normal);
	normal = (lenSq > 1e-6) ? normal * rsqrt(lenSq) : DecodeNormal(n0);
	float alpha = (n0.a + n1.a + n2.a + n3.a) * 0.25;
	return EncodeNormal(normal, alpha);
}

uint2 MipSize(uint mip)
{
	return max(cbMipmap.sourceSize >> mip, uint2(1, 1));
}

[numthreads(16, 16, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 did : SV_DispatchThreadID)
{
	uint2 sourceMax = cbMipmap.sourceSize - 1;
	uint2 sourcePos = min(did.xy, sourceMax);
	float depth = rDepth[sourcePos];
	float4 normal = rNormal[sourcePos];

	gsDepth[gtid.y][gtid.x] = depth;
	gsNormal[gtid.y][gtid.x] = normal;

	if (all(did.xy < MipSize(0)))
	{
		rwDepthMip0[did.xy] = depth;
		rwNormalMip0[did.xy] = normal;
	}

	GroupMemoryBarrierWithGroupSync();

	if ((gtid.x < 8) && (gtid.y < 8))
	{
		uint2 base = gtid.xy * 2;
		float d = min(min(gsDepth[base.y][base.x], gsDepth[base.y][base.x + 1]), min(gsDepth[base.y + 1][base.x], gsDepth[base.y + 1][base.x + 1]));
		float4 n = ReduceNormal(gsNormal[base.y][base.x], gsNormal[base.y][base.x + 1], gsNormal[base.y + 1][base.x], gsNormal[base.y + 1][base.x + 1]);
		gsDepth[gtid.y][gtid.x] = d;
		gsNormal[gtid.y][gtid.x] = n;

		uint2 mipPos = gid.xy * 8 + gtid.xy;
		if (all(mipPos < MipSize(1)))
		{
			rwDepthMip1[mipPos] = d;
			rwNormalMip1[mipPos] = n;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if ((gtid.x < 4) && (gtid.y < 4))
	{
		uint2 base = gtid.xy * 2;
		float d = min(min(gsDepth[base.y][base.x], gsDepth[base.y][base.x + 1]), min(gsDepth[base.y + 1][base.x], gsDepth[base.y + 1][base.x + 1]));
		float4 n = ReduceNormal(gsNormal[base.y][base.x], gsNormal[base.y][base.x + 1], gsNormal[base.y + 1][base.x], gsNormal[base.y + 1][base.x + 1]);
		gsDepth[gtid.y][gtid.x] = d;
		gsNormal[gtid.y][gtid.x] = n;

		uint2 mipPos = gid.xy * 4 + gtid.xy;
		if (all(mipPos < MipSize(2)))
		{
			rwDepthMip2[mipPos] = d;
			rwNormalMip2[mipPos] = n;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if ((gtid.x < 2) && (gtid.y < 2))
	{
		uint2 base = gtid.xy * 2;
		float d = min(min(gsDepth[base.y][base.x], gsDepth[base.y][base.x + 1]), min(gsDepth[base.y + 1][base.x], gsDepth[base.y + 1][base.x + 1]));
		float4 n = ReduceNormal(gsNormal[base.y][base.x], gsNormal[base.y][base.x + 1], gsNormal[base.y + 1][base.x], gsNormal[base.y + 1][base.x + 1]);
		gsDepth[gtid.y][gtid.x] = d;
		gsNormal[gtid.y][gtid.x] = n;

		uint2 mipPos = gid.xy * 2 + gtid.xy;
		if (all(mipPos < MipSize(3)))
		{
			rwDepthMip3[mipPos] = d;
			rwNormalMip3[mipPos] = n;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if ((gtid.x == 0) && (gtid.y == 0))
	{
		float d = min(min(gsDepth[0][0], gsDepth[0][1]), min(gsDepth[1][0], gsDepth[1][1]));
		float4 n = ReduceNormal(gsNormal[0][0], gsNormal[0][1], gsNormal[1][0], gsNormal[1][1]);

		uint2 mipPos = gid.xy;
		if (all(mipPos < MipSize(4)))
		{
			rwDepthMip4[mipPos] = d;
			rwNormalMip4[mipPos] = n;
		}
	}
}

//	EOF
