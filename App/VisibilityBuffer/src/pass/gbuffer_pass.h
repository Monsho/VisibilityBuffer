#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


class MeshletArgCopyPass : public AppPassBase
{
public:
	MeshletArgCopyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MeshletArgCopyPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MeshletArgCopy;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources() const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources() const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Graphics;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager) override;
	
private:
	sl12::UniqueHandle<sl12::Buffer> indirectArgUpload_;
};

class MeshletCullingPass : public AppPassBase
{
public:
	MeshletCullingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MeshletCullingPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MeshletCulling;
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

class DepthPrePass : public AppPassBase
{
public:
	DepthPrePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~DepthPrePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::DepthPre;
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
	UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

class GBufferPass : public AppPassBase
{
public:
	GBufferPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~GBufferPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::GBuffer;
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
	sl12::UniqueHandle<sl12::GraphicsPipelineState> psoMesh_, psoTriplanar_;
	UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

//	EOF
