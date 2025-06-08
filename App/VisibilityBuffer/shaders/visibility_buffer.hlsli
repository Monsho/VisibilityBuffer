#ifndef VISIBILITY_BUFFER_HLSLI
#define VISIBILITY_BUFFER_HLSLI

#include "cbuffer.hlsli"
#include "math.hlsli"

uint EncodeVisibility(in uint DrawCallIndex, in uint PrimID)
{
	return ((DrawCallIndex & 0xffffff) << 8) | (PrimID & 0xff); 
}

void DecodeVisibility(in uint Visibility, out uint DrawCallIndex, out uint PrimID)
{
	DrawCallIndex = (Visibility >> 8) & 0xffffff;
	PrimID = Visibility & 0xff;
}

uint3 GetVertexIndices(in ByteAddressBuffer rIndexBuffer, in MeshletData mlData, in uint triIndex)
{
	uint address = mlData.indexOffset + triIndex * 3 * 4;
	return rIndexBuffer.Load3(address);
}

float SNormToFloat(int v, float scale)
{
	float scaledV = (float)v * scale;
	return max(scaledV, -1.0);
}

float4 SNorm8ToFloat32_Vector(uint v)
{
	const float kScale = 1.0 / 127.0;
	float4 ret = float4(
		SNormToFloat(asint(v << 24) >> 24, kScale),
		SNormToFloat(asint((v << 16) & 0xff000000) >> 24, kScale),
		SNormToFloat(asint((v << 8) & 0xff000000) >> 24, kScale),
		SNormToFloat(asint(v & 0xff000000) >> 24, kScale)
		);
	return ret;
}

float3 GetVertexPosition(in ByteAddressBuffer rVertexBuffer, in SubmeshData smData, in uint index)
{
	const float kScale = 1.0 / 32767.0;
	uint address = smData.posOffset + index * 8;
	uint2 up = rVertexBuffer.Load2(address);
	float3 ret = float3(
		SNormToFloat(asint(up.x << 16) >> 16, kScale),
		SNormToFloat(asint(up.x & 0xffff0000) >> 16, kScale),
		SNormToFloat(asint(up.y << 16) >> 16, kScale)
		);
	return ret;
}

float3 GetVertexNormal(in ByteAddressBuffer rVertexBuffer, in SubmeshData smData, in uint index)
{
	uint address = smData.normalOffset + index * 4;
	uint up = rVertexBuffer.Load(address);
	return SNorm8ToFloat32_Vector(up).xyz;
}

float4 GetVertexTangent(in ByteAddressBuffer rVertexBuffer, in SubmeshData smData, in uint index)
{
	uint address = smData.tangentOffset + index * 4;
	uint up = rVertexBuffer.Load(address);
	return SNorm8ToFloat32_Vector(up);
}

float2 GetVertexTexcoord(in ByteAddressBuffer rVertexBuffer, in SubmeshData smData, in uint index)
{
	uint address = smData.uvOffset + index * 4;
	uint up = rVertexBuffer.Load(address);
	float2 ret = float2(
		f16tof32(up),
		f16tof32(up >> 16)
		);
	return ret;
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

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
PerspBaryDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
	PerspBaryDeriv ret = (PerspBaryDeriv)0;

	float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

	float2 ndc0 = pt0.xy * invW.x;
	float2 ndc1 = pt1.xy * invW.y;
	float2 ndc2 = pt2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	ret.Ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.Ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
	float ddxSum = dot(ret.Ddx, float3(1,1,1));
	float ddySum = dot(ret.Ddy, float3(1,1,1));

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
	float interpW = rcp(interpInvW);

	ret.Lambda.x = interpW * (invW[0] + deltaVec.x*ret.Ddx.x + deltaVec.y*ret.Ddy.x);
	ret.Lambda.y = interpW * (0.0f    + deltaVec.x*ret.Ddx.y + deltaVec.y*ret.Ddy.y);
	ret.Lambda.z = interpW * (0.0f    + deltaVec.x*ret.Ddx.z + deltaVec.y*ret.Ddy.z);

	ret.Ddx *= (2.0f/winSize.x);
	ret.Ddy *= (2.0f/winSize.y);
	ddxSum  *= (2.0f/winSize.x);
	ddySum  *= (2.0f/winSize.y);

	ret.Ddy *= -1.0f;
	ddySum  *= -1.0f;

	float interpW_ddx = 1.0f / (interpInvW + ddxSum);
	float interpW_ddy = 1.0f / (interpInvW + ddySum);

	ret.Ddx = interpW_ddx*(ret.Lambda*interpInvW + ret.Ddx) - ret.Lambda;
	ret.Ddy = interpW_ddy*(ret.Lambda*interpInvW + ret.Ddy) - ret.Lambda;  

	return ret;
}

float3 InterpolateWithDeriv(PerspBaryDeriv deriv, float v0, float v1, float v2)
{
	float3 mergedV = float3(v0, v1, v2);
	float3 ret;
	ret.x = dot(mergedV, deriv.Lambda);
	ret.y = dot(mergedV, deriv.Ddx);
	ret.z = dot(mergedV, deriv.Ddy);
	return ret;
}

void CalcDerivativeFloat2(PerspBaryDeriv deriv, float2 v0, float2 v1, float2 v2, out float2 v, out float2 dx, out float2 dy)
{
	float3 x = InterpolateWithDeriv(deriv, v0.x, v1.x, v2.x);
	float3 y = InterpolateWithDeriv(deriv, v0.y, v1.y, v2.y);
	v = float2(x.x, y.x);
	dx = float2(x.y, y.y);
	dy = float2(x.z, y.z);
}
void CalcDerivativeFloat3(PerspBaryDeriv deriv, float3 v0, float3 v1, float3 v2, out float3 v, out float3 dx, out float3 dy)
{
	float3 x = InterpolateWithDeriv(deriv, v0.x, v1.x, v2.x);
	float3 y = InterpolateWithDeriv(deriv, v0.y, v1.y, v2.y);
	float3 z = InterpolateWithDeriv(deriv, v0.z, v1.z, v2.z);
	v = float3(x.x, y.x, z.x);
	dx = float3(x.y, y.y, z.y);
	dy = float3(x.z, y.z, z.z);
}
void CalcDerivativeFloat4(PerspBaryDeriv deriv, float4 v0, float4 v1, float4 v2, out float4 v, out float4 dx, out float4 dy)
{
	float3 x = InterpolateWithDeriv(deriv, v0.x, v1.x, v2.x);
	float3 y = InterpolateWithDeriv(deriv, v0.y, v1.y, v2.y);
	float3 z = InterpolateWithDeriv(deriv, v0.z, v1.z, v2.z);
	float3 w = InterpolateWithDeriv(deriv, v0.w, v1.w, v2.w);
	v = float4(x.x, y.x, z.x, w.x);
	dx = float4(x.y, y.y, z.y, w.y);
	dy = float4(x.z, y.z, z.z, w.z);
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
	float3	positionDDX;
	float3	positionDDY;
	float3	normal;
	float4	tangent;
	float2	texcoord;
	float2	texcoordDDX;
	float2	texcoordDDY;
};

VertexAttr GetVertexAttrFromRay(
	in ByteAddressBuffer rVertexBuffer,	in InstanceData instance, in SubmeshData submesh,
	in uint3 vertexIndices, in float2 pixelPos,
	in float4x4 mtxProjToWorld, in float2 screenSize, in float4 eyePosition)
{
	VertexAttr attr;
	
	// get positions.
	float3 p0 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.x);
	float3 p1 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.y);
	float3 p2 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.z);
	float4x4 mtxBoxToWorld = mul(instance.mtxLocalToWorld, instance.mtxBoxTransform);
	p0 = mul(mtxBoxToWorld, float4(p0, 1)).xyz;
	p1 = mul(mtxBoxToWorld, float4(p1, 1)).xyz;
	p2 = mul(mtxBoxToWorld, float4(p2, 1)).xyz;

	// calc barycentric.
	float2 screenPos = (pixelPos + 0.5) / screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	float4 worldPos = mul(mtxProjToWorld, float4(clipSpacePos, 0, 1));
	worldPos.xyz /= worldPos.w;
	float3 rayOrig = eyePosition.xyz;
	float3 rayDir = worldPos.xyz - rayOrig;
	float3 C = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, rayDir);
	float3 Cx = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, QuadReadAcrossX(rayDir));
	float3 Cy = CalcRayIntersectBarycentric(p0, p1, p2, rayOrig, QuadReadAcrossY(rayDir));

	// get uv.
	float2 uv0 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.x);
	float2 uv1 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.y);
	float2 uv2 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.z);
	attr.texcoord = uv0 * C.x + uv1 * C.y + uv2 * C.z;
	attr.texcoordDDX = (uv0 * Cx.x + uv1 * Cx.y + uv2 * Cx.z) - attr.texcoord;
	attr.texcoordDDY = (uv0 * Cy.x + uv1 * Cy.y + uv2 * Cy.z) - attr.texcoord;

	// get other attributes.
	attr.position = p0 * C.x + p1 * C.y + p2 * C.z;
	float3 n0 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.x);
	float3 n1 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.y);
	float3 n2 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.z);
	attr.normal = n0 * C.x + n1 * C.y + n2 * C.z;
	float4 t0 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.x);
	float4 t1 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.y);
	float4 t2 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.z);
	attr.tangent = t0 * C.x + t1 * C.y + t2 * C.z;

	return attr;
}
VertexAttr GetVertexAttrPerspectiveCorrect(
	in ByteAddressBuffer rVertexBuffer, in InstanceData instance, in SubmeshData submesh,
	in uint3 vertexIndices, in float2 pixelPos,
	in float4x4 mtxWorldToProj, in float2 screenSize)
{
	VertexAttr attr;
	
	// get positions.
	float3 p0 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.x);
	float3 p1 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.y);
	float3 p2 = GetVertexPosition(rVertexBuffer, submesh, vertexIndices.z);
	float4x4 mtxBoxToWorld = mul(instance.mtxLocalToWorld, instance.mtxBoxTransform);
	p0 = mul(mtxBoxToWorld, float4(p0, 1)).xyz;
	p1 = mul(mtxBoxToWorld, float4(p1, 1)).xyz;
	p2 = mul(mtxBoxToWorld, float4(p2, 1)).xyz;

	// transform projection.
	float4 pt0 = mul(mtxWorldToProj, float4(p0, 1));
	float4 pt1 = mul(mtxWorldToProj, float4(p1, 1));
	float4 pt2 = mul(mtxWorldToProj, float4(p2, 1));

	// calc barycentric.
	float2 screenPos = (pixelPos + 0.5) / screenSize;
	float2 clipSpacePos = screenPos * float2(2, -2) + float2(-1, 1);
	PerspBaryDeriv C = CalcFullBary(pt0, pt1, pt2, clipSpacePos, screenSize);

	// get uv.
	float2 uv0 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.x);
	float2 uv1 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.y);
	float2 uv2 = GetVertexTexcoord(rVertexBuffer, submesh, vertexIndices.z);
	CalcDerivativeFloat2(C, uv0, uv1, uv2, attr.texcoord, attr.texcoordDDX, attr.texcoordDDY);

	// get other attributes.
	float3 dx3, dy3;
	float4 dx4, dy4;
	CalcDerivativeFloat3(C, p0, p1, p2, attr.position, attr.positionDDX, attr.positionDDY);

	float3 n0 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.x);
	float3 n1 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.y);
	float3 n2 = GetVertexNormal(rVertexBuffer, submesh, vertexIndices.z);
	CalcDerivativeFloat3(C, n0, n1, n2, attr.normal, dx3, dy3);

	float4 t0 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.x);
	float4 t1 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.y);
	float4 t2 = GetVertexTangent(rVertexBuffer, submesh, vertexIndices.z);
	CalcDerivativeFloat4(C, t0, t1, t2, attr.tangent, dx4, dy4);

	return attr;
}

#endif // VISIBILITY_BUFFER_HLSLI
