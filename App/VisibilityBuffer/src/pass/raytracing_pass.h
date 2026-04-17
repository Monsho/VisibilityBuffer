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

class TestRayTracingPass : public AppPassBase
{
public:
	TestRayTracingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~TestRayTracingPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::TestRayTracing;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	void CreateShaderTable();

private:
	UniqueHandle<sl12::RootSignature> rtGlobalRS_, rtLocalRS_;
	UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_, psoTestRT_;
	UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	UniqueHandle<sl12::Buffer> MaterialHGTable_, TestRGSTable_, TestMSTable_;
	UINT bvhShaderRecordSize_;
};

class ReadyRtxgiPass : public AppPassBase
{
public:
	ReadyRtxgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ReadyRtxgiPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ReadyRtxgi;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
};

class ProbeTracePass : public AppPassBase
{
public:
	ProbeTracePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ProbeTracePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ProbeTrace;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	void CreateShaderTable();

private:
	UniqueHandle<sl12::RootSignature> rtGlobalRS_, rtLocalRS_;
	UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_, psoProbeTraceRT_;
	UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	UniqueHandle<sl12::Buffer> MaterialHGTable_, ProbeTraceRGSTable_, ProbeTraceMSTable_;
	UINT bvhShaderRecordSize_;
};

class UpdateRtxgiPass : public AppPassBase
{
public:
	UpdateRtxgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~UpdateRtxgiPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::UpdateRtxgi;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;
};

class ApplyRtxgiPass : public AppPassBase
{
public:
	ApplyRtxgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ApplyRtxgiPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ApplyRtxgi;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	UniqueHandle<sl12::RootSignature> rs_;
	UniqueHandle<sl12::ComputePipelineState> psoDDGI_;
};

class DebugDdgiPass : public AppPassBase
{
public:
	DebugDdgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~DebugDdgiPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::DebugDDGI;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	UniqueHandle<sl12::RootSignature> rs_;
	UniqueHandle<sl12::GraphicsPipelineState> pso_;
};

class MonteCarloGIPass : public AppPassBase
{
public:
	MonteCarloGIPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MonteCarloGIPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MonteCarloGI;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	void CreateShaderTable();

private:
	UniqueHandle<sl12::RootSignature> rtGlobalRS_, rtLocalRS_;
	UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_, psoMonteCarloRT_;
	UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	UniqueHandle<sl12::Buffer> MaterialHGTable_, MonteCarloRGSTable_, MonteCarloMSTable_;
	UINT bvhShaderRecordSize_;
};

class InitialSamplePass : public AppPassBase
{
public:
	InitialSamplePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~InitialSamplePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::InitialSample;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc) override
	{
		bInitialFrame_ = true;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	void CreateShaderTable();

private:
	UniqueHandle<sl12::RootSignature> rtGlobalRS_, rtLocalRS_;
	UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_, psoInitialSampleRT_;
	UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	UniqueHandle<sl12::Buffer> MaterialHGTable_, InitialSampleRGSTable_, InitialSampleMSTable_;
	UINT bvhShaderRecordSize_;

	bool bInitialFrame_ = true;
};

class SpatialReusePass : public AppPassBase
{
public:
	SpatialReusePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~SpatialReusePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::SpatialReuse;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	UniqueHandle<sl12::RootSignature> rs_;
	UniqueHandle<sl12::ComputePipelineState> pso_;
};

class ReSTIRResolvePass : public AppPassBase
{
public:
	ReSTIRResolvePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ReSTIRResolvePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::ReSTIRResolve;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	UniqueHandle<sl12::RootSignature> rs_;
	UniqueHandle<sl12::ComputePipelineState> pso_;
};

class RayTracingDenoisePass : public AppPassBase
{
public:
	RayTracingDenoisePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~RayTracingDenoisePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::RayTracingDenoise;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc) override
	{
		atrousIterations_ = desc.atrousIterations;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Compute;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	UniqueHandle<sl12::RootSignature> rsPrepass_;
	UniqueHandle<sl12::ComputePipelineState> psoPrepass_;
	UniqueHandle<sl12::RootSignature> rsTemporal_;
	UniqueHandle<sl12::ComputePipelineState> psoTemporal_;
	UniqueHandle<sl12::RootSignature> rsAtrous_;
	UniqueHandle<sl12::ComputePipelineState> psoAtrous_;

	int atrousIterations_ = 2;
};

//	EOF
