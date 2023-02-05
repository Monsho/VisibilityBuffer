#include "cbuffer.hlsli"
#include "math.hlsli"

struct PSInput
{
	float4	position	: SV_POSITION;
};

ConstantBuffer<SceneCB>			cbScene			: register(b0);

Texture2D<uint>					texVis			: register(t0);
ByteAddressBuffer				rVertexBuffer	: register(t1);
ByteAddressBuffer				rIndexBuffer	: register(t2);
StructuredBuffer<InstanceData>	rInstanceData	: register(t3);
StructuredBuffer<SubmeshData>	rSubmeshData	: register(t4);
StructuredBuffer<DrawCallData>	rDrawCallData	: register(t5);
Texture2D<float>				texDepth		: register(t6);
Texture2D						texColor		: register(t7);
Texture2D						texNormal		: register(t8);
Texture2D						texORM			: register(t9);

SamplerState		samLinearWrap	: register(s0);

uint3 GetVertexIndices(in SubmeshData smData, in uint triIndex)
{
	uint address = smData.indexOffset + triIndex * 3 * 4;
	return rIndexBuffer.Load3(address);
}

float3 GetVertexPosition(in SubmeshData smData, in uint index)
{
	uint address = smData.posOffset + index * 12;
	uint3 up = rVertexBuffer.Load3(address);
	return asfloat(up);
}

float3 GetVertexNormal(in SubmeshData smData, in uint index)
{
	uint address = smData.normalOffset + index * 12;
	uint3 up = rVertexBuffer.Load3(address);
	return asfloat(up);
}

float4 GetVertexTangent(in SubmeshData smData, in uint index)
{
	uint address = smData.tangentOffset + index * 16;
	uint4 up = rVertexBuffer.Load4(address);
	return asfloat(up);
}

float2 GetVertexTexcoord(in SubmeshData smData, in uint index)
{
	uint address = smData.uvOffset + index * 8;
	uint2 up = rVertexBuffer.Load2(address);
	return asfloat(up);
}

// perspective correct attribute interpolation using partial derivatives.
// https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf
struct PerspBaryDeriv
{
	float3 Lambda;
	float3 Ddx;
	float3 Ddy;
	float invPersp;
};

PerspBaryDeriv CalcPerspectiveBarycentric(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc)
{
	PerspBaryDeriv ret;
	float3 invW =  rcp(float3(pt0.w, pt1.w, pt2.w));
	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float D = determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1));
	float invDet = rcp(D);

	float3 lx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y);
	ret.Ddx = lx * invDet * invW;
	float3 ly = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x);
	ret.Ddy = ly * invDet * invW;

	float2 Dpos = pixelNdc - ndc0;
	ret.Lambda.x = invW.x + Dpos.x * ret.Ddx.x + Dpos.y * ret.Ddy.x;
	ret.Lambda.y = 0.0    + Dpos.x * ret.Ddx.y + Dpos.y * ret.Ddy.y;
	ret.Lambda.z = 0.0    + Dpos.x * ret.Ddx.z + Dpos.y * ret.Ddy.z;

	ret.invPersp = rcp(dot(ret.Lambda, (1.0).xxx));

	return ret;
}

float3 CalcDerivativeBarycentric(PerspBaryDeriv deriv, float4 pt0, float2 pixelNdc)
{
	float invW = rcp(pt0.w);
	float2 ndc0 = pt0.xy * invW;

	float2 Dpos = pixelNdc - ndc0;
	float3 ret;
	ret.x = invW + Dpos.x * deriv.Ddx.x + Dpos.y * deriv.Ddy.x;
	ret.y = 0.0  + Dpos.x * deriv.Ddx.y + Dpos.y * deriv.Ddy.y;
	ret.z = 0.0  + Dpos.x * deriv.Ddx.z + Dpos.y * deriv.Ddy.z;
	return ret;
}

// calc barycentric with ray intersection.
float3 CalcRayIntersectBarycentric(float3 p0, float3 p1, float3 p2, float3 rayOrig, float3 rayDir)
{
	float3 v10 = p1 - p0;
	float3 v20 = p2 - p0;
	float3 pv = cross(rayDir, v20);
	float invD = 1.0 / dot(v10, pv);
	float3 tv = rayOrig - p0;
	float u = dot(tv, pv) * invD;
	float3 qv = cross(tv, v10);
	float v = dot(rayDir, qv) * invD;
	float w = 1.0 - v - u;
	return float3(w, u, v);
}

// get vertex attributes.
struct VertexAttr
{
	float3	position;
	float3	normal;
	float4	tangent;
	float2	texcoord;
	float2	texcoordDDX;
	float2	texcoordDDY;
};
VertexAttr GetVertexAttrFromRay(InstanceData instance, SubmeshData submesh, uint3 vertexIndices, float2 pixelPos)
{
	VertexAttr attr;
	
	// get positions.
	float3 p0 = GetVertexPosition(submesh, vertexIndices.x);
	float3 p1 = GetVertexPosition(submesh, vertexIndices.y);
	float3 p2 = GetVertexPosition(submesh, vertexIndices.z);
	p0 = mul(instance.mtxLocalToWorld, float4(p0, 1)).xyz;
	p1 = mul(instance.mtxLocalToWorld, float4(p1, 1)).xyz;
	p2 = mul(instance.mtxLocalToWorld, float4(p2, 1)).xyz;

	// calc barycentric.
	float2 screenPos = (pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(cbScene.mtxProjToWorld, float4(clipSpacePos, 0, 1));
	worldPos.xyz /= worldPos.w;
	float3 rayOrig = cbScene.eyePosition.xyz;
	float3 rayDir = worldPos.xyz - rayOrig;
	float3 C = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, rayDir);
	float3 Cx = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, QuadReadAcrossX(rayDir));
	float3 Cy = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, QuadReadAcrossY(rayDir));

	// get uv.
	float2 uv0 = GetVertexTexcoord(submesh, vertexIndices.x);
	float2 uv1 = GetVertexTexcoord(submesh, vertexIndices.y);
	float2 uv2 = GetVertexTexcoord(submesh, vertexIndices.z);
	attr.texcoord = uv0 * C.x + uv1 * C.y + uv2 * C.z;
	attr.texcoordDDX = (uv0 * Cx.x + uv1 * Cx.y + uv2 * Cx.z) - attr.texcoord;
	attr.texcoordDDY = (uv0 * Cy.x + uv1 * Cy.y + uv2 * Cy.z) - attr.texcoord;

	// get other attributes.
	attr.position = p0 * C.x + p1 * C.y + p2 * C.z;
	float3 n0 = GetVertexNormal(submesh, vertexIndices.x);
	float3 n1 = GetVertexNormal(submesh, vertexIndices.y);
	float3 n2 = GetVertexNormal(submesh, vertexIndices.z);
	attr.normal = n0 * C.x + n1 * C.y + n2 * C.z;
	float4 t0 = GetVertexTangent(submesh, vertexIndices.x);
	float4 t1 = GetVertexTangent(submesh, vertexIndices.y);
	float4 t2 = GetVertexTangent(submesh, vertexIndices.z);
	attr.tangent = t0 * C.x + t1 * C.y + t2 * C.z;

	return attr;
}
VertexAttr GetVertexAttrPerspectiveCorrect(InstanceData instance, SubmeshData submesh, uint3 vertexIndices, float2 pixelPos)
{
	VertexAttr attr;
	
	// get positions.
	float3 p0 = GetVertexPosition(submesh, vertexIndices.x);
	float3 p1 = GetVertexPosition(submesh, vertexIndices.y);
	float3 p2 = GetVertexPosition(submesh, vertexIndices.z);
	p0 = mul(instance.mtxLocalToWorld, float4(p0, 1)).xyz;
	p1 = mul(instance.mtxLocalToWorld, float4(p1, 1)).xyz;
	p2 = mul(instance.mtxLocalToWorld, float4(p2, 1)).xyz;
	float4 pt0 = mul(cbScene.mtxWorldToProj, float4(p0, 1));
	float4 pt1 = mul(cbScene.mtxWorldToProj, float4(p1, 1));
	float4 pt2 = mul(cbScene.mtxWorldToProj, float4(p2, 1));
	float3 invW = 1.0 / float3(pt0.w, pt1.w, pt2.w);

	// calc barycentric.
	float2 screenPos = (pixelPos + 0.5) / cbScene.screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	PerspBaryDeriv C = CalcPerspectiveBarycentric(pt0, pt1, pt2, clipSpacePos);

	// get uv.
	float2 uv0 = GetVertexTexcoord(submesh, vertexIndices.x);
	float2 uv1 = GetVertexTexcoord(submesh, vertexIndices.y);
	float2 uv2 = GetVertexTexcoord(submesh, vertexIndices.z);
	attr.texcoord = uv0 * C.Lambda.x + uv1 * C.Lambda.y + uv2 * C.Lambda.z;

	float2 sx = (pixelPos + float2(1.5, 0.5)) / cbScene.screenSize;
	float2 px = sx * float2(2, -2) + float2(-1, 1);
	float3 Cx = CalcDerivativeBarycentric(C, pt0, px);
	float2 sy = (pixelPos + float2(0.5, 1.5)) / cbScene.screenSize;
	float2 py = sy * float2(2, -2) + float2(-1, 1);
	float3 Cy = CalcDerivativeBarycentric(C, pt0, py);
	attr.texcoordDDX = uv0 * Cx.x + uv1 * Cx.y + uv2 * Cx.z;
	attr.texcoordDDY = uv0 * Cy.x + uv1 * Cy.y + uv2 * Cy.z;
	attr.texcoord *= C.invPersp;
	attr.texcoordDDX *= C.invPersp;
	attr.texcoordDDY *= C.invPersp;
	attr.texcoordDDX -= attr.texcoord;
	attr.texcoordDDY -= attr.texcoord;

	// get other attributes.
	attr.position = (p0 * C.Lambda.x + p1 * C.Lambda.y + p2 * C.Lambda.z) * C.invPersp;
	float3 n0 = GetVertexNormal(submesh, vertexIndices.x);
	float3 n1 = GetVertexNormal(submesh, vertexIndices.y);
	float3 n2 = GetVertexNormal(submesh, vertexIndices.z);
	attr.normal = (n0 * C.Lambda.x + n1 * C.Lambda.y + n2 * C.Lambda.z) * C.invPersp;
	float4 t0 = GetVertexTangent(submesh, vertexIndices.x);
	float4 t1 = GetVertexTangent(submesh, vertexIndices.y);
	float4 t2 = GetVertexTangent(submesh, vertexIndices.z);
	attr.tangent = (t0 * C.Lambda.x + t1 * C.Lambda.y + t2 * C.Lambda.z) * C.invPersp;

	return attr;
}

[earlydepthstencil]
float4 main(PSInput In) : SV_Target
{
	// get visibility.
	uint2 pos = (uint2)In.position.xy;
	uint vis = texVis[pos];
	uint drawCallIndex = (vis >> 16) & 0xffff;
	uint triIndex = vis & 0xffff;

	// get triangle indices.
	DrawCallData dcData = rDrawCallData[drawCallIndex];
	InstanceData inData = rInstanceData[dcData.instanceIndex];
	SubmeshData smData = rSubmeshData[dcData.submeshIndex];
	uint3 vertexIndices = GetVertexIndices(smData, triIndex);

	// barycentric type.
	const uint kBaryCalcType = 1;

	VertexAttr attr;
	if (kBaryCalcType == 0)
	{
		attr = GetVertexAttrFromRay(inData, smData, vertexIndices, In.position.xy);
	}
	else if(kBaryCalcType == 1)
	{
		attr = GetVertexAttrPerspectiveCorrect(inData, smData, vertexIndices, In.position.xy);
	}

	// sample texture.
	float3 bc = texColor.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 orm = texORM.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).rgb;
	float3 normalInTS = texNormal.SampleGrad(samLinearWrap, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY).xyz * 2 - 1;

	float3 normalV = normalize(mul((float3x3)inData.mtxLocalToWorld, attr.normal));
	float4 tangentV = float4(normalize(mul((float3x3)inData.mtxLocalToWorld, attr.tangent.xyz)), attr.tangent.w);
	
	float3 T, B, N;
	GetTangentSpace(normalV, tangentV, T, B, N);
	normalInTS *= float3(1, -sign(tangentV.w), 1);
	float3 normalInWS = ConvertVectorTangetToWorld(normalInTS, T, B, N);

	float NoL = saturate(normalInWS.y);
	float3 diffuse = lerp(bc.rgb, 0, orm.b);
	return float4(diffuse * NoL, 1);
}
