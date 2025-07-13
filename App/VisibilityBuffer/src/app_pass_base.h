#pragma once

#include "sl12/render_graph.h"

struct RenderPassSetupDesc;

enum class AppPassType
{
	MeshletArgCopy,
	MeshletCulling,
	ClearMiplevel,
	FeedbackMiplevel,
	DepthPre,
	GBuffer,
	VisibilityVs,
	VisibilityMs1st,
	VisibilityMs2nd,
	ShadowMap,
	ShadowExp,
	ShadowBlurX,
	ShadowBlurY,
	Lighting,
	HiZ,
	HiZafterFirstCull,
	Tonemap,
	Deinterleave,
	SSAO,
	Denoise,
	IndirectLight,
	BufferReady,
	MaterialDepth,
	Classify,
	MaterialTile,
	MaterialResolve,
	MaterialComputeBinning,
	MaterialComputeGBuffer,
	PrefixSum,
	MaterialTileBinning,
	MaterialTileGBuffer,
	Invalid
};

class AppPassBase : public sl12::IRenderPass
{
public:
	AppPassBase(sl12::Device* pDev, class RenderSystem* pRenderSys, class Scene* pScene)
		: pDevice_(pDev), pRenderSystem_(pRenderSys), pScene_(pScene)
	{}
	virtual ~AppPassBase() {}

	virtual AppPassType GetPassType() const
	{
		return AppPassType::Invalid;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc)
	{}

protected:
	sl12::Device* pDevice_;
	class RenderSystem* pRenderSystem_;
	class Scene* pScene_;
};

//	EOF
