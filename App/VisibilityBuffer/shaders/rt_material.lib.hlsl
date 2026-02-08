#include "common.hlsli"
#include "cbuffer.hlsli"
#include "payload.hlsli"
#include "visibility_buffer.hlsli"

// local
ConstantBuffer<SubmeshOffsetCB>	cbSubmesh		: REG_SPACE(b0, 16);
ByteAddressBuffer				Indices			: REG_SPACE(t0, 16);
ByteAddressBuffer				Vertices		: REG_SPACE(t1, 16);
Texture2D						texBaseColor	: REG_SPACE(t2, 16);
Texture2D						texORM			: REG_SPACE(t3, 16);
SamplerState					texBaseColor_s	: REG_SPACE(s0, 16);


[shader("closesthit")]
void MaterialCHS(inout MaterialPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attr : SV_IntersectionAttributes)
{
	uint3 indices = GetVertexIndices(Indices, cbSubmesh.index, PrimitiveIndex());

	float2 uvs[3] = {
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.x),
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.y),
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.z),
	};
	float2 uv = uvs[0] +
		attr.barycentrics.x * (uvs[1] - uvs[0]) +
		attr.barycentrics.y * (uvs[2] - uvs[0]);

	MaterialParam param = (MaterialParam)0;
	param.hitT = RayTCurrent();

	param.baseColor = texBaseColor.SampleLevel(texBaseColor_s, uv, 0.0);
	float4 orm = texORM.SampleLevel(texBaseColor_s, uv, 0.0);
	param.roughness = max(0.01, orm.g);
	param.metallic = orm.b;

	param.emissive = 0.0;

	float3 ns[3] = {
		GetVertexNormal(Vertices, cbSubmesh.normal, indices.x),
		GetVertexNormal(Vertices, cbSubmesh.normal, indices.y),
		GetVertexNormal(Vertices, cbSubmesh.normal, indices.z),
	};
	float3 normalLS = ns[0] +
		attr.barycentrics.x * (ns[1] - ns[0]) +
		attr.barycentrics.y * (ns[2] - ns[0]);

	// normal local to world.
	float3x4 mtxLocalToWorld = ObjectToWorld3x4();
	param.normal = normalize(mul(normalLS, (float3x3)mtxLocalToWorld));

	param.flag = 0;
	param.flag |= (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE) ? kFlagBackFaceHit : 0;

	EncodeMaterialPayload(param, payload);
}

[shader("anyhit")]
void MaterialAHS(inout MaterialPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attr : SV_IntersectionAttributes)
{
	uint3 indices = GetVertexIndices(Indices, cbSubmesh.index, PrimitiveIndex());

	float2 uvs[3] = {
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.x),
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.y),
		GetVertexTexcoord(Vertices, cbSubmesh.texcoord, indices.z),
	};
	float2 uv = uvs[0] +
		attr.barycentrics.x * (uvs[1] - uvs[0]) +
		attr.barycentrics.y * (uvs[2] - uvs[0]);

	float opacity = texBaseColor.SampleLevel(texBaseColor_s, uv, 0.0).a;
	if (opacity < 0.33)
	{
		IgnoreHit();
	}
}


// EOF
