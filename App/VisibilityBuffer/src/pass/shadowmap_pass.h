#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


class ShadowMapPass : public AppPassBase
{
public:
	ShadowMapPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ShadowMapPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ShadowMap;
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

class ShadowExpPass : public AppPassBase
{
public:
	ShadowExpPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ShadowExpPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ShadowExp;
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

class ShadowExpBlurPass : public AppPassBase
{
public:
	ShadowExpBlurPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ShadowExpBlurPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ShadowBlurX;
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
