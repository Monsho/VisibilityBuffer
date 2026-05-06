#include "rt_pipeline_manager.h"

#include "shader_types.h"
#include "scene.h"
#include "sl12/swapchain.h"
#include "sl12/buffer.h"
#include "sl12/resource_texture.h"
#include <algorithm>

namespace
{
	struct RtShaderTableLocalRecord
	{
		D3D12_GPU_DESCRIPTOR_HANDLE	cbv;
		D3D12_GPU_DESCRIPTOR_HANDLE	srv;
		D3D12_GPU_DESCRIPTOR_HANDLE	sampler;
	};

	UINT AlignShaderTableSize(UINT size, UINT align)
	{
		return ((size + align - 1) / align) * align;
	}
}

namespace RTCommon
{
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountLocal = {
		1,	// cbv
		4,	// srv
		0,	// uav
		1,	// sampler
	};
	static const sl12::RaytracingDescriptorCount kRTDescriptorCapacityGlobal = {
		9,	// cbv
		15,	// srv
		5,	// uav
		3,	// sampler
	};

	static LPCWSTR kMaterialCHS = L"MaterialCHS";
	static LPCWSTR kMaterialAHS = L"MaterialAHS";
	static LPCWSTR kMaterialOpacityHG = L"MaterialOpacityHG";
	static LPCWSTR kMaterialMaskedHG = L"MaterialMaskedHG";
	static const int kPayloadSize = 32;
	static const sl12::u32 kLocalSpaceId = 16;
}

RTPipelineManager::RTPipelineManager(sl12::Device* pDevice)
	: pDevice_(pDevice)
{}

RTPipelineManager::~RTPipelineManager()
{
	materialHGTable_.Reset();
	rtDescMan_.Reset();
	psoRaytracing_.Reset();
	psoMaterialCollection_.Reset();
}

int RTPipelineManager::AddPipelineEntry(const RTPipelineEntry& entry)
{
	pipelineEntries_.push_back(entry);
	bPipelineDirty_ = true;
	return (int)pipelineEntries_.size() - 1;
}

bool RTPipelineManager::Setup(RenderSystem* pRenderSys, Scene* pScene)
{
	if (bPipelineDirty_)
	{
		if (!CreatePipeline(pRenderSys))
		{
			return false;
		}
	}

	if (!SetupMaterialHitGroupTable(pRenderSys, pScene))
	{
		return false;
	}

	return true;
}

bool RTPipelineManager::InitializeDescriptorManager(sl12::u32 materialCount, bool forceRecreate)
{
	if (!forceRecreate && rtDescMan_.IsValid() && rtMaterialCount_ == materialCount)
	{
		return true;
	}

	rtDescMan_ = sl12::MakeUnique<sl12::RaytracingDescriptorManager>(pDevice_);
	if (!rtDescMan_->Initialize(
		pDevice_,
		sl12::Swapchain::kMaxBuffer,
		1,
		RTCommon::kRTDescriptorCapacityGlobal,
		RTCommon::kRTDescriptorCountLocal,
		materialCount))
	{
		rtDescMan_.Reset();
		rtMaterialCount_ = 0;
		return false;
	}
	rtMaterialCount_ = materialCount;
	rtDescriptorGeneration_++;
	rtDescMan_->BeginNewFrame();
	return true;
}

bool RTPipelineManager::SetupMaterialHitGroupTable(
	RenderSystem* pRenderSys,
	Scene* pScene)
{
	auto& rtTableSources = pScene->GetRTTableSources();
	auto& rtOffsetCBs = pScene->GetMeshOffsetCBs();
	const auto sceneGeneration = pScene->GetRTTableGeneration();
	const bool sceneChanged = materialTableSceneGeneration_ != sceneGeneration;

	size_t totalSubmeshCount = 0;
	for (auto&& src : rtTableSources)
	{
		totalSubmeshCount += src.pResMesh->GetSubmeshes().size();
	}

	if (!InitializeDescriptorManager(
		static_cast<sl12::u32>(totalSubmeshCount),
		sceneChanged))
	{
		sl12::ConsolePrint("Error : Failed to init raytracing descriptor.\n");
		return false;
	}

	if (!sceneChanged
		&& materialHGTable_.IsValid()
		&& materialTableDescriptorGeneration_ == rtDescriptorGeneration_
		&& materialTablePipelineGeneration_ == rtPipelineGeneration_
		&& materialTableSceneGeneration_ == sceneGeneration)
	{
		return true;
	}

	std::vector<RtShaderTableLocalRecord> materialTable;
	std::vector<bool> opaqueTable;
	auto viewDescSize = rtDescMan_->GetViewDescSize();
	auto samplerDescSize = rtDescMan_->GetSamplerDescSize();
	auto localHandleStart = rtDescMan_->IncrementLocalHandleStart();
	auto FillMeshTable = [&](const sl12::ResourceItemMesh* pMeshItem)
	{
		auto&& submeshes = pMeshItem->GetSubmeshes();
		auto CBs = rtOffsetCBs.find(pMeshItem);
		auto& CBL = CBs->second;
		for (int i = 0; i < submeshes.size(); i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = pMeshItem->GetMaterials()[submesh.materialIndex];
			auto bcSrv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			auto ormSrv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			if (material.baseColorTex.IsValid())
			{
				auto pTexBC = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
				bcSrv = &pTexBC->GetTextureView();
			}
			if (material.ormTex.IsValid())
			{
				auto pTexORM = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());
				ormSrv = &pTexORM->GetTextureView();
			}

			opaqueTable.push_back(material.blendType == sl12::ResourceMeshMaterialBlendType::Opaque);

			RtShaderTableLocalRecord table{};

			D3D12_CPU_DESCRIPTOR_HANDLE cbv[] = {
				CBL[i].GetCBV()->GetDescInfo().cpuHandle,
			};
			sl12::u32 cbvCnt = ARRAYSIZE(cbv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &localHandleStart.viewCpuHandle, &cbvCnt,
				cbvCnt, cbv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.cbv = localHandleStart.viewGpuHandle;
			localHandleStart.viewCpuHandle.ptr += viewDescSize * cbvCnt;
			localHandleStart.viewGpuHandle.ptr += viewDescSize * cbvCnt;

			D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
				pRenderSys->GetMeshManager()->GetIndexBufferSRV()->GetDescInfo().cpuHandle,
				pRenderSys->GetMeshManager()->GetVertexBufferSRV()->GetDescInfo().cpuHandle,
				bcSrv->GetDescInfo().cpuHandle,
				ormSrv->GetDescInfo().cpuHandle,
			};
			sl12::u32 srvCnt = ARRAYSIZE(srv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &localHandleStart.viewCpuHandle, &srvCnt,
				srvCnt, srv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.srv = localHandleStart.viewGpuHandle;
			localHandleStart.viewCpuHandle.ptr += viewDescSize * srvCnt;
			localHandleStart.viewGpuHandle.ptr += viewDescSize * srvCnt;

			D3D12_CPU_DESCRIPTOR_HANDLE sampler[] = {
				pRenderSys->GetLinearWrapSampler()->GetDescInfo().cpuHandle,
			};
			sl12::u32 samplerCnt = ARRAYSIZE(sampler);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &localHandleStart.samplerCpuHandle, &samplerCnt,
				samplerCnt, sampler, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			table.sampler = localHandleStart.samplerGpuHandle;
			localHandleStart.samplerCpuHandle.ptr += samplerDescSize * samplerCnt;
			localHandleStart.samplerGpuHandle.ptr += samplerDescSize * samplerCnt;

			materialTable.push_back(table);
		}
	};
	for (auto&& src : rtTableSources)
	{
		FillMeshTable(src.pResMesh);
	}

	const UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	const UINT descHandleOffset = AlignShaderTableSize(shaderIdentifierSize, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	shaderRecordSize_ = AlignShaderTableSize(descHandleOffset + sizeof(RtShaderTableLocalRecord), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

	ID3D12StateObjectProperties* prop = nullptr;
	if (FAILED(psoRaytracing_->GetPSO()->QueryInterface(IID_PPV_ARGS(&prop))))
	{
		sl12::ConsolePrint("Error : Failed to query raytracing state object properties.\n");
		return false;
	}

	void* hgIdentifiers[2] = {
		prop->GetShaderIdentifier(RTCommon::kMaterialOpacityHG),
		prop->GetShaderIdentifier(RTCommon::kMaterialMaskedHG),
	};
	std::vector<void*> hgTable;
	hgTable.reserve(opaqueTable.size());
	for (bool isOpaque : opaqueTable)
	{
		hgTable.push_back(hgIdentifiers[isOpaque ? 0 : 1]);
	}
	prop->Release();

	materialHGTable_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);
	sl12::BufferDesc desc{};
	desc.heap = sl12::BufferHeap::Dynamic;
	desc.size = shaderRecordSize_ * std::max<UINT>(1, static_cast<UINT>(materialTable.size()));
	desc.usage = sl12::ResourceUsage::ShaderResource;
	desc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
	if (!materialHGTable_->Initialize(pDevice_, desc))
	{
		return false;
	}

	auto p = static_cast<char*>(materialHGTable_->Map());
	for (int i = 0; i < static_cast<int>(materialTable.size()); ++i)
	{
		auto start = p;
		memcpy(p, hgTable[i], shaderIdentifierSize);
		p += descHandleOffset;
		memcpy(p, &materialTable[i], sizeof(RtShaderTableLocalRecord));
		p = start + shaderRecordSize_;
	}
	materialHGTable_->Unmap();

	materialTableDescriptorGeneration_ = rtDescriptorGeneration_;
	materialTablePipelineGeneration_ = rtPipelineGeneration_;
	materialTableSceneGeneration_ = sceneGeneration;
	return true;
}

void RTPipelineManager::BeginNewFrame()
{
	if (rtDescMan_.IsValid())
	{
		rtDescMan_->BeginNewFrame();
	}
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
	rtPipelineGeneration_++;
	materialTablePipelineGeneration_ = ~0u;
	return true;
}

// EOF
