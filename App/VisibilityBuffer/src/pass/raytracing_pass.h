#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


class BuildBvhPass : public AppPassBase
{
public:
	BuildBvhPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~BuildBvhPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::BuildBvh;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
};

//	EOF
