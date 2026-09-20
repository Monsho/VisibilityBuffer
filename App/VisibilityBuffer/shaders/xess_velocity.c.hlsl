cbuffer ConversionCB : register(b0)
{
    uint2 renderSize;
    float2 jitterDeltaUV;
};
Texture2D<float2> sourceMotion : register(t0);
RWTexture2D<float2> outputMotion : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= renderSize)) return;
    outputMotion[id.xy] = (sourceMotion[id.xy] - jitterDeltaUV) * float2(renderSize);
}
