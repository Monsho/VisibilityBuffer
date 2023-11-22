#ifndef CBUFFER_HLSLI
#define CBUFFER_HLSLI

#ifdef USE_IN_CPP
#	define		float4x4		DirectX::XMFLOAT4X4
#	define		float4			DirectX::XMFLOAT4
#	define		float3			DirectX::XMFLOAT3
#	define		float2			DirectX::XMFLOAT2
#	define		uint			UINT
#	define		uint2			DirectX::XMUINT2
#	define		uint3			DirectX::XMUINT3
#	define		uint4			DirectX::XMUINT4
#endif

struct SceneCB
{
	float4x4	mtxWorldToProj;
	float4x4	mtxWorldToView;
	float4x4	mtxViewToProj;
	float4x4	mtxProjToWorld;
	float4x4	mtxViewToWorld;
	float4x4	mtxProjToView;
	float4x4	mtxProjToPrevProj;
	float4x4	mtxPrevViewToProj;
	float4		eyePosition;
	float2		screenSize;
	float2		invScreenSize;
	float2		nearFar;
	uint2		feedbackIndex;
};

struct LightCB
{
	float3		ambientSky;
	int			pad0;
	float3		ambientGround;
	float		ambientIntensity;
	float3		directionalVec;
	int			pad1;
	float3		directionalColor;
	int			pad2;
};

struct ShadowCB
{
	float4x4	mtxWorldToProj;
	float2		exponent;
	float		constBias;
};

struct MeshCB
{
	float4x4	mtxLocalToWorld;
	float4x4	mtxPrevLocalToWorld;
};

struct VisibilityCB
{
	uint		drawCallIndex;
};

struct MaterialTileCB
{
	uint		materialIndex;
};

struct TileCB
{
	uint	numX;
	uint	numY;
	uint	tileMax;
	uint	materialMax;
};

struct BlurCB
{
	float4	kernel0;
	float4	kernel1;
	float2	offset;
};

struct DetailCB
{
	float2	detailTile;
	float	detailIntensity;
	uint	detailType;
	float	triplanarTile;
	uint	triplanarType;
};

struct AmbOccCB
{
	float	intensity;
	float	giIntensity;
	float	worldSpaceRadius;
	float	ndcPixelSize;
	uint	maxPixelRadius;
	uint	sliceCount;
	uint	stepCount;
	uint	temporalIndex;
	float	tangentBias;
	float	thickness;
	float	viewBias;
	uint	baseVecType;
	float	denoiseRadius;
	float	denoiseBaseWeight;
	float	denoiseDepthSigma;
};

struct FrustumCB
{
	float4	frustumPlanes[6];
};

struct MeshletCullCB
{
	uint	argStartAddress;
	uint	meshletStartIndex;
	uint	meshletCount;
};

struct DebugCB
{
	uint	displayMode;
};

struct InstanceData
{
	float4x4	mtxLocalToWorld;
	float4x4	mtxWorldToLocal;
};

struct SubmeshData
{
	uint	materialIndex;
	uint	posOffset;
	uint	normalOffset;
	uint	tangentOffset;
	uint	uvOffset;
	uint	indexOffset;
};

struct MeshletData
{
	uint	submeshIndex;
	uint	indexOffset;
	uint	meshletPackedPrimCount;
	uint	meshletPackedPrimOffset;
	uint	meshletVertexIndexCount;
	uint	meshletVertexIndexOffset;
};

struct DrawCallData
{
	uint	instanceIndex;
	uint	meshletIndex;
};

#define CLASSIFY_TILE_WIDTH (64)
#define CLASSIFY_THREAD_WIDTH (16)
#define CLASSIFY_MATERIAL_MAX (256)
#define CLASSIFY_DEPTH_RANGE (CLASSIFY_MATERIAL_MAX * 32)

#define SHADOW_TYPE 0

#endif // CBUFFER_HLSLI
//  EOF
