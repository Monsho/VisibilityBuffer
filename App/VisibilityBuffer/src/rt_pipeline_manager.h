#pragma once

#include "sl12/pipeline_state.h"
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

	sl12::DxrPipelineState* GetPipelineState()
	{
		return &psoRaytracing_;
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
	std::vector<RTPipelineEntry> pipelineEntries_;
	std::vector<RTPipelineIndex> pipelineIndices_;
	bool bPipelineDirty_ = true;
};

// EOF
