#pragma once

#include "sl12/pipeline_state.h"
#include "sl12/descriptor_heap.h"
#include "sl12/unique_handle.h"

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
	bool Setup(class RenderSystem* pRenderSys);
	bool InitializeDescriptorManager(
		const sl12::RaytracingDescriptorCount& globalCapacity,
		const sl12::RaytracingDescriptorCount& localCount,
		sl12::u32 materialCount);
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
	const RTPipelineIndex& GetPipelineIndex(int index) const
	{
		assert(index < (int)pipelineIndices_.size());
		return pipelineIndices_[index];
	}

private:
	bool CreatePipeline(class RenderSystem* pRenderSys);

private:
	sl12::Device* pDevice_ = nullptr;

	// pipelines.
	sl12::UniqueHandle<sl12::DxrPipelineState> psoMaterialCollection_;
	sl12::UniqueHandle<sl12::RootSignature> rtLocalRS_;
	sl12::UniqueHandle<sl12::DxrPipelineState> psoRaytracing_;
	sl12::UniqueHandle<sl12::RaytracingDescriptorManager> rtDescMan_;
	sl12::u32 rtMaterialCount_ = 0;
	sl12::u32 rtDescriptorGeneration_ = 0;
	std::vector<RTPipelineEntry> pipelineEntries_;
	std::vector<RTPipelineIndex> pipelineIndices_;
	bool bPipelineDirty_ = true;
};

// EOF
