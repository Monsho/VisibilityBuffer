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

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
	
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

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
	
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

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
	
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

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
	
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

//----
class GenerateVrsPass : public AppPassBase
{
public:
	GenerateVrsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~GenerateVrsPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::GenerateVRS;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc)
	{
		intensityThreshold_ = desc.vrsIntensityThreshold;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
	float intensityThreshold_ = 1.0f;
};

//----
class ReprojectVrsPass : public AppPassBase
{
public:
	ReprojectVrsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ReprojectVrsPass();
	
	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ReprojectVRS;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc)
	{
		depthThreshold_ = desc.vrsDepthThreshold;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
	float depthThreshold_ = 1.0f;
};

//----
class PrefixSumTestPass : public AppPassBase
{
public:
	PrefixSumTestPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~PrefixSumTestPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::PrefixSumTest;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoInit_, psoMain_;
};

//----
class DebugPass : public AppPassBase
{
public:
	DebugPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~DebugPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::PrefixSumTest;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc)
	{
		debugMode_ = desc.debugMode;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
	
private:
	int debugMode_ = 0;
	sl12::UniqueHandle<sl12::RootSignature> rs_;
	sl12::UniqueHandle<sl12::GraphicsPipelineState> pso_;
};

//	EOF
