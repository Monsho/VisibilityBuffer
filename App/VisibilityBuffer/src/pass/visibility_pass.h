#pragma once

#include "../app_pass_base.h"
#include "../scene.h"


//----
class BufferReadyPass : public AppPassBase
{
public:
	BufferReadyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~BufferReadyPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::BufferReady;
	}

	virtual std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID& ID) const override;
	virtual std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID& ID) const override;
	virtual sl12::HardwareQueue::Value GetExecuteQueue() const
	{
		return sl12::HardwareQueue::Copy;
	}
	virtual void Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID) override;

private:
	struct Data
	{
		std::map<const sl12::ResourceItemMesh*, sl12::u32> meshMap;
		std::vector<const sl12::ResourceItemMesh*> meshList;
		std::vector<sl12::u32> submeshOffsets;
		std::vector<sl12::u32> meshletOffsets;
		sl12::u32 meshCount = 0;
		sl12::u32 submeshCount = 0;
		sl12::u32 meshletCount = 0;
		sl12::u32 drawCallCount = 0;
	};
	void GatherData(Data& OutData) const;
};

//----
class VisibilityVsPass : public AppPassBase
{
public:
	VisibilityVsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~VisibilityVsPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::VisibilityVs;
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
	sl12::UniqueHandle<sl12::GraphicsPipelineState> psoOpaque_, psoOpaqueDS_, psoMasked_, psoMaskedDS_;
	UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

//----
class VisibilityMsPass : public AppPassBase
{
	enum EPipelineType
	{
		Opaque,
		OpaqueDS,
		Masked,
		MaskedDS,
		Max
	};
	
public:
	VisibilityMsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~VisibilityMsPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::VisibilityMs1st;
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
	sl12::UniqueHandle<sl12::GraphicsPipelineState> pso1st_[EPipelineType::Max], pso2nd_[EPipelineType::Max];
};

//----
class MaterialDepthPass : public AppPassBase
{
public:
	MaterialDepthPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialDepthPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialDepth;
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
class ClassifyPass : public AppPassBase
{
public:
	ClassifyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~ClassifyPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::Classify;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoClear_, psoClassify_;
};

//----
class MaterialTilePass : public AppPassBase
{
public:
	MaterialTilePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialTilePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialTile;
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
	sl12::UniqueHandle<sl12::GraphicsPipelineState> psoStandard_, psoTriplanar_;
	sl12::UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

//----
class MaterialResolvePass : public AppPassBase
{
public:
	MaterialResolvePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialResolvePass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialResolve;
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
	sl12::UniqueHandle<sl12::WorkGraphState> wgState_;
	sl12::UniqueHandle<sl12::WorkGraphContext> wgContext_;
};

//----
class MaterialComputeBinningPass : public AppPassBase
{
public:
	MaterialComputeBinningPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialComputeBinningPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialComputeBinning;
	}

	virtual void SetPassSettings(const RenderPassSetupDesc& desc)
	{
		isVRSEnable_ = desc.bUseVRS;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoInit_, psoCount_, psoCountSum_, psoPrefixSumInit_, psoPrefixSum_, psoBinning_, psoFinalize_;
	bool isVRSEnable_ = false;
};

//----
class MaterialComputeGBufferPass : public AppPassBase
{
public:
	MaterialComputeGBufferPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialComputeGBufferPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialComputeGBuffer;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoStandard_, psoTriplanar_;
	sl12::UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

//----
class MaterialTileBinningPass : public AppPassBase
{
public:
	MaterialTileBinningPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialTileBinningPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialTileBinning;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoInit_, psoBinning_;
};

//----
class MaterialTileGBufferPass : public AppPassBase
{
public:
	MaterialTileGBufferPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene);
	virtual ~MaterialTileGBufferPass();

	virtual AppPassType GetPassType() const override
	{
		return AppPassType::MaterialTileGBuffer;
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
	sl12::UniqueHandle<sl12::ComputePipelineState> psoStandard_, psoTriplanar_;
	sl12::UniqueHandle<sl12::IndirectExecuter> indirectExec_;
};

//	EOF
