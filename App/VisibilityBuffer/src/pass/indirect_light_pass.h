#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


//----
class DeinterleavePass : public AppPassBase
{
public:
	DeinterleavePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~DeinterleavePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Deinterleave;
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
class ScreenSpaceAOPass : public AppPassBase
{
public:
	ScreenSpaceAOPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ScreenSpaceAOPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::SSAO;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc) override
	{
		bNeedDeinterleave_ = desc.bNeedDeinterleave;
		type_ = desc.ssaoType;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoHbao_;
	sl12::UniqueHandle<sl12::ComputePipelineState> psoBitmask_;
	sl12::UniqueHandle<sl12::ComputePipelineState> psoSsgi_;
	sl12::UniqueHandle<sl12::ComputePipelineState> psoSsgiDi_;
	bool bNeedDeinterleave_ = false;
	int type_ = 0;
};

//----
class DenoisePass : public AppPassBase
{
public:
	DenoisePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~DenoisePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Denoise;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc) override
	{
		type_ = desc.ssaoType;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoAO_;
	sl12::UniqueHandle<sl12::ComputePipelineState> psoGI_;
	int type_ = 0;
};

//----
class IndirectLightPass : public AppPassBase
{
public:
	IndirectLightPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~IndirectLightPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::IndirectLight;
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


//	EOF
