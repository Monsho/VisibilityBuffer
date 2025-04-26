#pragma once

#include "sl12/render_graph.h"

enum class AppPassType
{
	MeshletArgCopy,
	MeshletCulling,
	ClearMiplevel,
	FeedbackMiplevel,
	DepthPre,
	GBuffer,
	ShadowMap,
	ShadowExp,
	ShadowBlurX,
	ShadowBlurY,
	Lighting,
	HiZ,
	Tonemap,
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

protected:
	sl12::Device* pDevice_;
	class RenderSystem* pRenderSystem_;
	class Scene* pScene_;
};

//	EOF
