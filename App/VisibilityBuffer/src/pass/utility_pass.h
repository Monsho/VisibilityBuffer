#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


//----
class ClearMiplevelPass : public AppPassBase
{
public:
	ClearMiplevelPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ClearMiplevelPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ClearMiplevel;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
};

//----
class FeedbackMiplevelPass : public AppPassBase
{
public:
	FeedbackMiplevelPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~FeedbackMiplevelPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::FeedbackMiplevel;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
};

//----
class LightingPass : public AppPassBase
{
public:
	LightingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~LightingPass();

	inline void SetEnableShadowExp(bool b)
	{
		bEnableShadowExp_ = b;
	}
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Lighting;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
	bool bEnableShadowExp_ = false;
};

//----
class HiZPass : public AppPassBase
{
public:
	HiZPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~HiZPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::HiZ;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
};

//----
class TonemapPass : public AppPassBase
{
public:
	TonemapPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~TonemapPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Tonemap;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState> pso_;
};

//	EOF
