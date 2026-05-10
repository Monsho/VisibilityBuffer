#pragma once

#include "../app_pass_base.h"
#include "../scene.h"

class WaterLightAccumCopyPass : public AppPassBase
{
public:
	WaterLightAccumCopyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~WaterLightAccumCopyPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::WaterLightAccumCopy;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
};

class WaterPass : public AppPassBase
{
public:
	WaterPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~WaterPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Water;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState> pso_;
};

//	EOF
