struct MotionConvCB
{
	uint2 renderSize;
	float2 jitterDeltaUV;
};

ConstantBuffer<MotionConvCB> cbMotionConv : register(b0);

Texture2D<float2> srcMotion : register(t0);
RWTexture2D<float2> rwMotion : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	if (any(dtid.xy >= cbMotionConv.renderSize))
	{
		return;
	}
	rwMotion[dtid.xy] = (srcMotion[dtid.xy] - cbMotionConv.jitterDeltaUV) * float2(cbMotionConv.renderSize);
}
