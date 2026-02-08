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

class RaytracingGIPass : public AppPassBase
{
public:
	RaytracingGIPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~RaytracingGIPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::RaytracingGI;
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

//	EOF
