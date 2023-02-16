#include "cbuffer.hlsli"

ConstantBuffer<TileCB>			cbTile				: register(b0);

RWByteAddressBuffer				rwDrawArg			: register(u0);

[numthreads(32, 1, 1)]
void main(
	uint3 gid : SV_GroupID,
	uint3 gtid : SV_GroupThreadID,
	uint3 did : SV_DispatchThreadID)
{
	uint matIndex = did.x;
	if (matIndex < cbTile.materialMax)
	{
		uint argAddr = matIndex * 16;
		rwDrawArg.Store(argAddr + 0, 6);
		rwDrawArg.Store(argAddr + 4, 0);
		rwDrawArg.Store(argAddr + 8, 0);
		rwDrawArg.Store(argAddr + 12, 0);
	}
}