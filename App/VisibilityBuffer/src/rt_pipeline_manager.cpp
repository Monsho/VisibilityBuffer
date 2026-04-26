#include "rt_pipeline_manager.h"

#include "shader_types.h"
#include "scene.h"

RTPipelineManager::RTPipelineManager(sl12::Device* pDevice)
	: pDevice_(pDevice)
{}

RTPipelineManager::~RTPipelineManager()
{
	psoRaytracing_.Reset();
	psoMaterialCollection_.Reset();
}

int RTPipelineManager::AddPipelineEntry(const RTPipelineEntry& entry)
{
	pipelineEntries_.push_back(entry);
	bPipelineDirty_ = true;
	return (int)pipelineEntries_.size() - 1;
}

bool RTPipelineManager::Setup(RenderSystem* pRenderSys)
{
	if (bPipelineDirty_)
	{
		if (!CreatePipeline(pRenderSys))
		{
			return false;
		}
	}

	return true;
}

namespace RTCommon
{
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountLocal = {
		1,	// cbv
		4,	// srv
		0,	// uav
		1,	// sampler
	};

	static LPCWSTR kMaterialCHS = L"MaterialCHS";
	static LPCWSTR kMaterialAHS = L"MaterialAHS";
	static LPCWSTR kMaterialOpacityHG = L"MaterialOpacityHG";
	static LPCWSTR kMaterialMaskedHG = L"MaterialMaskedHG";
	static const int kPayloadSize = 32;
	static const sl12::u32 kLocalSpaceId = 16;
}

bool RTPipelineManager::CreatePipeline(RenderSystem* pRenderSys)
{
	if (pipelineEntries_.empty())
	{
		return false;
	}

	// material pipeline.
	if (!psoMaterialCollection_.IsValid())
	{
		// local root signature.
		rtLocalRS_ = sl12::MakeUnique<sl12::RootSignature>(pDevice_);

		if (!sl12::CreateRaytracingLocalRootSignature(pDevice_,
			RTCommon::kRTDescriptorCountLocal,
			RTCommon::kLocalSpaceId,
			&rtLocalRS_))
		{
			sl12::ConsolePrint("Error : Failed to create raytracing local root signatures.\n");
			return false;
		}

		// pso.
		psoMaterialCollection_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDevice_);

		sl12::DxrPipelineStateDesc dxrDesc;

		// export shader from library.
		auto shader = pRenderSys->GetShader(ShaderName::RTMaterialLib);
		D3D12_EXPORT_DESC libExport[] = {
			{ RTCommon::kMaterialCHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
			{ RTCommon::kMaterialAHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
		};
		dxrDesc.AddDxilLibrary(shader->GetData(), shader->GetSize(), libExport, ARRAYSIZE(libExport));

		// hit group.
		dxrDesc.AddHitGroup(RTCommon::kMaterialOpacityHG, true, nullptr, RTCommon::kMaterialCHS, nullptr);
		dxrDesc.AddHitGroup(RTCommon::kMaterialMaskedHG, true, RTCommon::kMaterialAHS, RTCommon::kMaterialCHS, nullptr);

		// payload size and intersection attr size.
		dxrDesc.AddShaderConfig(RTCommon::kPayloadSize, sizeof(float) * 2);

		// TraceRay recursive count.
		dxrDesc.AddRaytracinConfig(1);

		// local root signature.
		// if use only one root signature, do not need export association.
		dxrDesc.AddLocalRootSignature(*(&rtLocalRS_), nullptr, 0);

		// PSO生成
		if (!psoMaterialCollection_->Initialize(pDevice_, dxrDesc, D3D12_STATE_OBJECT_TYPE_COLLECTION))
		{
			sl12::ConsolePrint("Error : Failed to init material collection pso.\n");
			return false;
		}
	}

	// raytracing pipeline.
	{
		psoRaytracing_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDevice_);

		sl12::DxrPipelineStateDesc dxrDesc;
		dxrDesc.AddExistingCollection(psoMaterialCollection_->GetPSO(), nullptr, 0);
		for (auto& entry : pipelineEntries_)
		{
			dxrDesc.AddExistingCollection(entry.pso->GetPSO(), nullptr, 0);
		}

		if (!psoRaytracing_->Initialize(pDevice_, dxrDesc))
		{
			sl12::ConsolePrint("Error : Failed to init RT pso.\n");
			return false;
		}
	}

	bPipelineDirty_ = false;
	return true;
}

// EOF
