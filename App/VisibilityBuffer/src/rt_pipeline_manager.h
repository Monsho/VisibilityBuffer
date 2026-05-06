#pragma once

#include "sl12/pipeline_state.h"
#include "sl12/descriptor_heap.h"
#include "sl12/unique_handle.h"

namespace sl12
{
	class Buffer;
}

struct RTPipelineEntry
{
	sl12::DxrPipelineState* pso;
	LPCWSTR rgsName;
	std::vector<LPCWSTR> msNames;
};

struct RTPipelineIndex
{
	int rgsOffset;
	int msOffset;
};

class RTPipelineManager
{
public:
	RTPipelineManager(sl12::Device* pDevice);
	~RTPipelineManager();

	int AddPipelineEntry(const RTPipelineEntry& entry);
	bool Setup(class RenderSystem* pRenderSys, class Scene* pScene);
	void BeginNewFrame();

	sl12::DxrPipelineState* GetPipelineState()
	{
		return &psoRaytracing_;
	}
	sl12::RaytracingDescriptorManager* GetDescriptorManager()
	{
		return rtDescMan_.IsValid() ? &rtDescMan_ : nullptr;
	}
	sl12::u32 GetDescriptorGeneration() const
	{
		return rtDescriptorGeneration_;
	}
	sl12::u32 GetPipelineGeneration() const
	{
		return rtPipelineGeneration_;
	}
	sl12::Buffer* GetMaterialHitGroupTable()
	{
		return &materialHGTable_;
	}
	sl12::u32 GetShaderRecordSize() const
	{
		return shaderRecordSize_;
	}
	const RTPipelineIndex& GetPipelineIndex(int index) const
	{
		assert(index < (int)pipelineIndices_.size());
		return pipelineIndices_[index];
	}

private:
	bool CreatePipeline(class RenderSystem* pRenderSys);
	bool InitializeDescriptorManager(sl12::u32 materialCount, bool forceRecreate = false);
	bool SetupMaterialHitGroupTable(class RenderSystem* pRenderSys, class Scene* pScene);

private:
	sl12::Device* pDevice_ = nullptr;

	// pipelines.
	sl12::UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_;
	sl12::UniqueHandle<sl12::RootSignature> rtLocalRS_;
	sl12::UniqueHandle<sl12::DxrPipelineState> psoRaytracing_;
	sl12::UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	sl12::UniqueHandle<sl12::Buffer> materialHGTable_;
	sl12::u32 rtMaterialCount_ = 0;
	sl12::u32 rtDescriptorGeneration_ = 0;
	sl12::u32 rtPipelineGeneration_ = 0;
	sl12::u32 materialTableDescriptorGeneration_ = ~0u;
	sl12::u32 materialTablePipelineGeneration_ = ~0u;
	sl12::u32 materialTableSceneGeneration_ = ~0u;
	sl12::u32 shaderRecordSize_ = 0;
	std::vector<RTPipelineEntry> pipelineEntries_;
	std::vector<RTPipelineIndex> pipelineIndices_;
	bool bPipelineDirty_ = true;
};

// EOF
