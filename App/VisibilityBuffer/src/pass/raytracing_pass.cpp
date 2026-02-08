#include "raytracing_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"

#define USE_IN_CPP
#include "../../shaders/cbuffer.hlsli"
#include "sl12/resource_texture.h"

namespace
{
	static const sl12::TransientResourceID	kBuildBvhDummy("BuildBvhDummy");
	static const sl12::TransientResourceID	kReadyRtxgiDummy("ReadyRtxgiDummy");
	static const sl12::TransientResourceID	kProbeTraceDummy("ProbeTraceDummy");
}

//----------------
BuildBvhPass::BuildBvhPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
}

BuildBvhPass::~BuildBvhPass()
{
}

std::vector<sl12::TransientResource> BuildBvhPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> BuildBvhPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource dummy(kBuildBvhDummy, sl12::TransientState::UnorderedAccess);

	dummy.desc.bIsTexture = true;
	dummy.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8_UNORM, 1, 1, 1, 1, 0);

	ret.push_back(dummy);
	return ret;
}

void BuildBvhPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "BuildBvhPass");

	// build bvh.
	pScene_->UpdateBVH(pCmdList);
}


namespace TestPass
{
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountGlobal = {
		2,	// cbv
		0,	// srv
		1,	// uav
		0,	// sampler
	};
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountLocal = {
		1,	// cbv
		4,	// srv
		0,	// uav
		1,	// sampler
	};

	static LPCWSTR kMaterialCHS = L"MaterialCHS";
	static LPCWSTR kMaterialAHS = L"MaterialAHS";
	static LPCWSTR kTestRGS = L"TestRGS";
	static LPCWSTR kTestMS = L"TestMS";
	static LPCWSTR kMaterialOpacityHG = L"MaterialOpacityHG";
	static LPCWSTR kMaterialMaskedHG = L"MaterialMaskedHG";
	static const int kPayloadSize = 32;
	static const sl12::u32 kLocalSpaceId = 16;

}

//----------------
TestRayTracingPass::TestRayTracingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	// global and local root signature.
	rtGlobalRS_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	rtLocalRS_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	if (!sl12::CreateRaytracingRootSignature(pDev,
		1,		// AS count
		TestPass::kRTDescriptorCountGlobal,
		TestPass::kRTDescriptorCountLocal,
		TestPass::kLocalSpaceId,
		&rtGlobalRS_, &rtLocalRS_))
	{
		sl12::ConsolePrint("Error : Failed to create raytracint root signatures.\n");
		assert(false);
	}

	psoMaterialCollection_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDev);
	psoTestRT_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDev);
	{
		sl12::DxrPipelineStateDesc dxrDesc;

		// export shader from library.
		auto shader = pRenderSys->GetShader(ShaderName::RTMaterialLib);
		D3D12_EXPORT_DESC libExport[] = {
			{ TestPass::kMaterialCHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
			{ TestPass::kMaterialAHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
		};
		dxrDesc.AddDxilLibrary(shader->GetData(), shader->GetSize(), libExport, ARRAYSIZE(libExport));

		// hit group.
		dxrDesc.AddHitGroup(TestPass::kMaterialOpacityHG, true, nullptr, TestPass::kMaterialCHS, nullptr);
		dxrDesc.AddHitGroup(TestPass::kMaterialMaskedHG, true, TestPass::kMaterialAHS, TestPass::kMaterialCHS, nullptr);

		// payload size and intersection attr size.
		dxrDesc.AddShaderConfig(TestPass::kPayloadSize, sizeof(float) * 2);

		// global root signature.
		dxrDesc.AddGlobalRootSignature(*(&rtGlobalRS_));

		// TraceRay recursive count.
		dxrDesc.AddRaytracinConfig(1);

		// local root signature.
		// if use only one root signature, do not need export association.
		dxrDesc.AddLocalRootSignature(*(&rtLocalRS_), nullptr, 0);

		// PSO生成
		if (!psoMaterialCollection_->Initialize(pDev, dxrDesc, D3D12_STATE_OBJECT_TYPE_COLLECTION))
		{
			sl12::ConsolePrint("Error : Failed to init material collection pso.\n");
			assert(false);
		}
	}
	{
		sl12::DxrPipelineStateDesc dxrDesc;

		// export shader from library.
		auto shader = pRenderSys->GetShader(ShaderName::RTTestLib);
		D3D12_EXPORT_DESC libExport[] = {
			{ TestPass::kTestRGS,	nullptr, D3D12_EXPORT_FLAG_NONE },
			{ TestPass::kTestMS,	nullptr, D3D12_EXPORT_FLAG_NONE },
		};
		dxrDesc.AddDxilLibrary(shader->GetData(), shader->GetSize(), libExport, ARRAYSIZE(libExport));

		// payload size and intersection attr size.
		dxrDesc.AddShaderConfig(TestPass::kPayloadSize, sizeof(float) * 2);

		// global root signature.
		dxrDesc.AddGlobalRootSignature(*(&rtGlobalRS_));

		// TraceRay recursive count.
		dxrDesc.AddRaytracinConfig(1);

		// collection pso.
		dxrDesc.AddExistingCollection(psoMaterialCollection_->GetPSO(), nullptr, 0);

		// PSO生成
		if (!psoTestRT_->Initialize(pDev, dxrDesc))
		{
			sl12::ConsolePrint("Error : Failed to init rt test pso.\n");
			assert(false);
		}
	}
}

TestRayTracingPass::~TestRayTracingPass()
{
	psoTestRT_.Reset();
	psoMaterialCollection_.Reset();
	rtGlobalRS_.Reset();
	rtLocalRS_.Reset();
}

std::vector<sl12::TransientResource> TestRayTracingPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> TestRayTracingPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource test(kTestRTResultID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	test.desc.bIsTexture = true;
	test.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 0);

	ret.push_back(test);
	return ret;
}

void TestRayTracingPass::CreateShaderTable()
{
	auto& rtTableSources = pScene_->GetRTTableSources();
	auto& rtOffsetCBs = pScene_->GetMeshOffsetCBs();

	// count submeshes.
	size_t totalSubmeshCount = 0;
	for (auto&& src : rtTableSources)
	{
		totalSubmeshCount += src.pResMesh->GetSubmeshes().size();
	}

	// initialize descriptor manager.
	rtDescMan_ = sl12::MakeUnique<sl12::RaytracingDescriptorManager>(pDevice_);
	if (!rtDescMan_->Initialize(pDevice_,
		1,		// Render Count
		1,		// AS Count
		TestPass::kRTDescriptorCountGlobal,
		TestPass::kRTDescriptorCountLocal,
		(sl12::u32)totalSubmeshCount))
	{
		sl12::ConsolePrint("Error : Failed to init raytracing descriptor.\n");
		assert(false);
	}

	// create local shader resource table.
	struct LocalTable
	{
		D3D12_GPU_DESCRIPTOR_HANDLE	cbv;
		D3D12_GPU_DESCRIPTOR_HANDLE	srv;
		D3D12_GPU_DESCRIPTOR_HANDLE	sampler;
	};
	std::vector<LocalTable> material_table;
	std::vector<bool> opaque_table;
	auto view_desc_size = rtDescMan_->GetViewDescSize();
	auto sampler_desc_size = rtDescMan_->GetSamplerDescSize();
	auto local_handle_start = rtDescMan_->IncrementLocalHandleStart();
	auto FillMeshTable = [&](const sl12::ResourceItemMesh* pMeshItem)
	{
		auto&& submeshes = pMeshItem->GetSubmeshes();
		auto CBs = rtOffsetCBs.find(pMeshItem);
		auto& CBL = CBs->second;
		for (int i = 0; i < submeshes.size(); i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = pMeshItem->GetMaterials()[submesh.materialIndex];
			auto bc_srv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			auto orm_srv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			if (material.baseColorTex.IsValid())
			{
				auto pTexBC = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
				bc_srv = &pTexBC->GetTextureView();
			}
			if (material.ormTex.IsValid())
			{
				auto pTexORM = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());
				orm_srv = &pTexORM->GetTextureView();
			}

			opaque_table.push_back(material.blendType == sl12::ResourceMeshMaterialBlendType::Opaque);

			LocalTable table;

			// CBV
			D3D12_CPU_DESCRIPTOR_HANDLE cbv[] = {
				CBL[i].GetCBV()->GetDescInfo().cpuHandle,
			};
			sl12::u32 cbv_cnt = ARRAYSIZE(cbv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.viewCpuHandle, &cbv_cnt,
				cbv_cnt, cbv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.cbv = local_handle_start.viewGpuHandle;
			local_handle_start.viewCpuHandle.ptr += view_desc_size * cbv_cnt;
			local_handle_start.viewGpuHandle.ptr += view_desc_size * cbv_cnt;

			// SRV
			D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
				pRenderSystem_->GetMeshManager()->GetIndexBufferSRV()->GetDescInfo().cpuHandle,
				pRenderSystem_->GetMeshManager()->GetVertexBufferSRV()->GetDescInfo().cpuHandle,
				bc_srv->GetDescInfo().cpuHandle,
				orm_srv->GetDescInfo().cpuHandle,
			};
			sl12::u32 srv_cnt = ARRAYSIZE(srv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.viewCpuHandle, &srv_cnt,
				srv_cnt, srv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.srv = local_handle_start.viewGpuHandle;
			local_handle_start.viewCpuHandle.ptr += view_desc_size * srv_cnt;
			local_handle_start.viewGpuHandle.ptr += view_desc_size * srv_cnt;

			// Samplerは1つ
			D3D12_CPU_DESCRIPTOR_HANDLE sampler[] = {
				pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle,
			};
			sl12::u32 sampler_cnt = ARRAYSIZE(sampler);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.samplerCpuHandle, &sampler_cnt,
				sampler_cnt, sampler, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			table.sampler = local_handle_start.samplerGpuHandle;
			local_handle_start.samplerCpuHandle.ptr += sampler_desc_size * sampler_cnt;
			local_handle_start.samplerGpuHandle.ptr += sampler_desc_size * sampler_cnt;

			material_table.push_back(table);
		}
	};
	for (auto&& s : rtTableSources)
	{
		FillMeshTable(s.pResMesh);
	}

	// create shader table.
	auto Align = [](UINT size, UINT align)
	{
		return ((size + align - 1) / align) * align;
	};
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	UINT descHandleOffset = Align(shaderIdentifierSize, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	UINT shaderRecordSize = Align(descHandleOffset + sizeof(LocalTable), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	bvhShaderRecordSize_ = shaderRecordSize;

	auto GenShaderTable = [&](
		void* const * shaderIds,
		int tableCountPerMaterial,
		sl12::UniqueHandle<sl12::Buffer>& buffer,
		int materialCount)
	{
		buffer = sl12::MakeUnique<sl12::Buffer>(pDevice_);

		materialCount = (materialCount < 0) ? (int)material_table.size() : materialCount;
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.size = shaderRecordSize * tableCountPerMaterial * materialCount;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		if (!buffer->Initialize(pDevice_, desc))
		{
			return false;
		}

		auto p = (char*)buffer->Map();
		for (int i = 0; i < materialCount; ++i)
		{
			for (int id = 0; id < tableCountPerMaterial; ++id)
			{
				auto start = p;

				memcpy(p, shaderIds[i * tableCountPerMaterial + id], shaderIdentifierSize);
				p += descHandleOffset;

				memcpy(p, &material_table[i], sizeof(LocalTable));

				p = start + shaderRecordSize;
			}
		}
		buffer->Unmap();

		return true;
	};
	// material shader table.
	{
		void* hg_identifier[2];
		{
			ID3D12StateObjectProperties* prop;
			psoTestRT_->GetPSO()->QueryInterface(IID_PPV_ARGS(&prop));
			hg_identifier[0] = prop->GetShaderIdentifier(TestPass::kMaterialOpacityHG);
			hg_identifier[1] = prop->GetShaderIdentifier(TestPass::kMaterialMaskedHG);
			prop->Release();
		}
		std::vector<void*> hg_table;
		for (auto v : opaque_table)
		{
			hg_table.push_back(v ? hg_identifier[0] : hg_identifier[1]);
		}
		if (!GenShaderTable(hg_table.data(), 1, MaterialHGTable_, -1))
		{
			assert(false);
		}
	}
	// for rgs and ms.
	{
		void* rgs_identifier;
		void* ms_identifier;
		{
			ID3D12StateObjectProperties* prop;
			psoTestRT_->GetPSO()->QueryInterface(IID_PPV_ARGS(&prop));
			rgs_identifier = prop->GetShaderIdentifier(TestPass::kTestRGS);
			ms_identifier = prop->GetShaderIdentifier(TestPass::kTestMS);
			prop->Release();
		}
		if (!GenShaderTable(&rgs_identifier, 1, TestRGSTable_, 1))
		{
			assert(false);
		}
		if (!GenShaderTable(&ms_identifier, 1, TestMSTable_, 1))
		{
			assert(false);
		}
	}
}

void TestRayTracingPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "TestRayTracingPass");

	if (pScene_->IsRTTableDirty())
	{
		CreateShaderTable();
	}

	auto pResultMap = pResManager->GetRenderGraphResource(kTestRTResultID);
	auto pResultUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pResultMap);

	auto&& TempCB = pScene_->GetTemporalCBs();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, TempCB.hLightCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pResultUAV->GetDescInfo().cpuHandle);

	D3D12_GPU_VIRTUAL_ADDRESS as_address[] = {
		pScene_->GetBvhScene()->GetGPUAddress()
	};
	pCmdList->SetRaytracingGlobalRootSignatureAndDescriptorSet(&rtGlobalRS_, &descSet, &rtDescMan_, as_address, ARRAYSIZE(as_address));

	// execute raytracing.
	sl12::DispatchRaysDesc desc{};
	desc.pso = &psoTestRT_;
	desc.hitGroupTable = &MaterialHGTable_;
	desc.missTable = &TestMSTable_;
	desc.rayGenTable = &TestRGSTable_;
	desc.hitGroupRecordSize = bvhShaderRecordSize_;
	desc.missRecordSize = bvhShaderRecordSize_;
	desc.width = pResultMap->pTexture->GetTextureDesc().width;
	desc.height = pResultMap->pTexture->GetTextureDesc().height;
	desc.depth = 1;
	pCmdList->DispatchRays(desc);
}


//----------------
ReadyRtxgiPass::ReadyRtxgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
}

ReadyRtxgiPass::~ReadyRtxgiPass()
{
}

std::vector<sl12::TransientResource> ReadyRtxgiPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> ReadyRtxgiPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource rtxgi(kReadyRtxgiDummy, sl12::TransientState::UnorderedAccess);

	rtxgi.desc.bIsTexture = true;
	rtxgi.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8_UNORM, 1, 1, 1, 1, 0);

	ret.push_back(rtxgi);
	return ret;
}

void ReadyRtxgiPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "ReadyRtxgiPass");

	auto rtxgi = pScene_->GetRtxgiComponent();

	// update constants stb.
	rtxgi->UploadConstants(pCmdList, static_cast<sl12::u32>(pScene_->GetFrameIndex()));

	// clear probes, if needed.
	pScene_->ClearProbes(pCmdList);
}


namespace ProbeTrace
{
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountGlobal = {
		2,	// cbv
		5,	// srv
		1,	// uav
		1,	// sampler
	};
	static const sl12::RaytracingDescriptorCount kRTDescriptorCountLocal = {
		1,	// cbv
		4,	// srv
		0,	// uav
		1,	// sampler
	};

	static LPCWSTR kMaterialCHS = L"MaterialCHS";
	static LPCWSTR kMaterialAHS = L"MaterialAHS";
	static LPCWSTR kProbeTraceRGS = L"ProbeTraceRGS";
	static LPCWSTR kProbeTraceMS = L"ProbeTraceMS";
	static LPCWSTR kMaterialOpacityHG = L"MaterialOpacityHG";
	static LPCWSTR kMaterialMaskedHG = L"MaterialMaskedHG";
	static const int kPayloadSize = 32;
	static const sl12::u32 kLocalSpaceId = 16;

}

//----------------
ProbeTracePass::ProbeTracePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	// global and local root signature.
	rtGlobalRS_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	rtLocalRS_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	if (!sl12::CreateRaytracingRootSignature(pDev,
		1,		// AS count
		ProbeTrace::kRTDescriptorCountGlobal,
		ProbeTrace::kRTDescriptorCountLocal,
		ProbeTrace::kLocalSpaceId,
		&rtGlobalRS_, &rtLocalRS_))
	{
		sl12::ConsolePrint("Error : Failed to create raytracint root signatures.\n");
		assert(false);
	}

	psoMaterialCollection_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDev);
	psoProbeTraceRT_ = sl12::MakeUnique<sl12::DxrPipelineState>(pDev);
	{
		sl12::DxrPipelineStateDesc dxrDesc;

		// export shader from library.
		auto shader = pRenderSys->GetShader(ShaderName::RTMaterialLib);
		D3D12_EXPORT_DESC libExport[] = {
			{ ProbeTrace::kMaterialCHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
			{ ProbeTrace::kMaterialAHS,	nullptr, D3D12_EXPORT_FLAG_NONE },
		};
		dxrDesc.AddDxilLibrary(shader->GetData(), shader->GetSize(), libExport, ARRAYSIZE(libExport));

		// hit group.
		dxrDesc.AddHitGroup(ProbeTrace::kMaterialOpacityHG, true, nullptr, ProbeTrace::kMaterialCHS, nullptr);
		dxrDesc.AddHitGroup(ProbeTrace::kMaterialMaskedHG, true, ProbeTrace::kMaterialAHS, ProbeTrace::kMaterialCHS, nullptr);

		// payload size and intersection attr size.
		dxrDesc.AddShaderConfig(ProbeTrace::kPayloadSize, sizeof(float) * 2);

		// global root signature.
		dxrDesc.AddGlobalRootSignature(*(&rtGlobalRS_));

		// TraceRay recursive count.
		dxrDesc.AddRaytracinConfig(1);

		// local root signature.
		// if use only one root signature, do not need export association.
		dxrDesc.AddLocalRootSignature(*(&rtLocalRS_), nullptr, 0);

		// PSO生成
		if (!psoMaterialCollection_->Initialize(pDev, dxrDesc, D3D12_STATE_OBJECT_TYPE_COLLECTION))
		{
			sl12::ConsolePrint("Error : Failed to init material collection pso.\n");
			assert(false);
		}
	}
	{
		sl12::DxrPipelineStateDesc dxrDesc;

		// export shader from library.
		auto shader = pRenderSys->GetShader(ShaderName::RTProbeTraceLib);
		D3D12_EXPORT_DESC libExport[] = {
			{ ProbeTrace::kProbeTraceRGS,	nullptr, D3D12_EXPORT_FLAG_NONE },
			{ ProbeTrace::kProbeTraceMS,	nullptr, D3D12_EXPORT_FLAG_NONE },
		};
		dxrDesc.AddDxilLibrary(shader->GetData(), shader->GetSize(), libExport, ARRAYSIZE(libExport));

		// payload size and intersection attr size.
		dxrDesc.AddShaderConfig(ProbeTrace::kPayloadSize, sizeof(float) * 2);

		// global root signature.
		dxrDesc.AddGlobalRootSignature(*(&rtGlobalRS_));

		// TraceRay recursive count.
		dxrDesc.AddRaytracinConfig(1);

		// collection pso.
		dxrDesc.AddExistingCollection(psoMaterialCollection_->GetPSO(), nullptr, 0);

		// PSO生成
		if (!psoProbeTraceRT_->Initialize(pDev, dxrDesc))
		{
			sl12::ConsolePrint("Error : Failed to init rt probe trace pso.\n");
			assert(false);
		}
	}
}

ProbeTracePass::~ProbeTracePass()
{
	psoProbeTraceRT_.Reset();
	psoMaterialCollection_.Reset();
	rtGlobalRS_.Reset();
	rtLocalRS_.Reset();
}

std::vector<sl12::TransientResource> ProbeTracePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kBuildBvhDummy, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kReadyRtxgiDummy, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> ProbeTracePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource dummy(kProbeTraceDummy, sl12::TransientState::UnorderedAccess);

	dummy.desc.bIsTexture = true;
	dummy.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8_UNORM, 1, 1, 1, 1, 0);

	ret.push_back(dummy);
	return ret;
}

void ProbeTracePass::CreateShaderTable()
{
	auto& rtTableSources = pScene_->GetRTTableSources();
	auto& rtOffsetCBs = pScene_->GetMeshOffsetCBs();

	// count submeshes.
	size_t totalSubmeshCount = 0;
	for (auto&& src : rtTableSources)
	{
		totalSubmeshCount += src.pResMesh->GetSubmeshes().size();
	}

	// initialize descriptor manager.
	rtDescMan_ = sl12::MakeUnique<sl12::RaytracingDescriptorManager>(pDevice_);
	if (!rtDescMan_->Initialize(pDevice_,
		1,		// Render Count
		1,		// AS Count
		ProbeTrace::kRTDescriptorCountGlobal,
		ProbeTrace::kRTDescriptorCountLocal,
		(sl12::u32)totalSubmeshCount))
	{
		sl12::ConsolePrint("Error : Failed to init raytracing descriptor.\n");
		assert(false);
	}

	// create local shader resource table.
	struct LocalTable
	{
		D3D12_GPU_DESCRIPTOR_HANDLE	cbv;
		D3D12_GPU_DESCRIPTOR_HANDLE	srv;
		D3D12_GPU_DESCRIPTOR_HANDLE	sampler;
	};
	std::vector<LocalTable> material_table;
	std::vector<bool> opaque_table;
	auto view_desc_size = rtDescMan_->GetViewDescSize();
	auto sampler_desc_size = rtDescMan_->GetSamplerDescSize();
	auto local_handle_start = rtDescMan_->IncrementLocalHandleStart();
	auto FillMeshTable = [&](const sl12::ResourceItemMesh* pMeshItem)
	{
		auto&& submeshes = pMeshItem->GetSubmeshes();
		auto CBs = rtOffsetCBs.find(pMeshItem);
		auto& CBL = CBs->second;
		for (int i = 0; i < submeshes.size(); i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = pMeshItem->GetMaterials()[submesh.materialIndex];
			auto bc_srv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			auto orm_srv = pDevice_->GetDummyTextureView(sl12::DummyTex::White);
			if (material.baseColorTex.IsValid())
			{
				auto pTexBC = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
				bc_srv = &pTexBC->GetTextureView();
			}
			if (material.ormTex.IsValid())
			{
				auto pTexORM = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());
				orm_srv = &pTexORM->GetTextureView();
			}

			opaque_table.push_back(material.blendType == sl12::ResourceMeshMaterialBlendType::Opaque);

			LocalTable table;

			// CBV
			D3D12_CPU_DESCRIPTOR_HANDLE cbv[] = {
				CBL[i].GetCBV()->GetDescInfo().cpuHandle,
			};
			sl12::u32 cbv_cnt = ARRAYSIZE(cbv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.viewCpuHandle, &cbv_cnt,
				cbv_cnt, cbv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.cbv = local_handle_start.viewGpuHandle;
			local_handle_start.viewCpuHandle.ptr += view_desc_size * cbv_cnt;
			local_handle_start.viewGpuHandle.ptr += view_desc_size * cbv_cnt;

			// SRV
			D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
				pRenderSystem_->GetMeshManager()->GetIndexBufferSRV()->GetDescInfo().cpuHandle,
				pRenderSystem_->GetMeshManager()->GetVertexBufferSRV()->GetDescInfo().cpuHandle,
				bc_srv->GetDescInfo().cpuHandle,
				orm_srv->GetDescInfo().cpuHandle,
			};
			sl12::u32 srv_cnt = ARRAYSIZE(srv);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.viewCpuHandle, &srv_cnt,
				srv_cnt, srv, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			table.srv = local_handle_start.viewGpuHandle;
			local_handle_start.viewCpuHandle.ptr += view_desc_size * srv_cnt;
			local_handle_start.viewGpuHandle.ptr += view_desc_size * srv_cnt;

			// Samplerは1つ
			D3D12_CPU_DESCRIPTOR_HANDLE sampler[] = {
				pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle,
			};
			sl12::u32 sampler_cnt = ARRAYSIZE(sampler);
			pDevice_->GetDeviceDep()->CopyDescriptors(
				1, &local_handle_start.samplerCpuHandle, &sampler_cnt,
				sampler_cnt, sampler, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			table.sampler = local_handle_start.samplerGpuHandle;
			local_handle_start.samplerCpuHandle.ptr += sampler_desc_size * sampler_cnt;
			local_handle_start.samplerGpuHandle.ptr += sampler_desc_size * sampler_cnt;

			material_table.push_back(table);
		}
	};
	for (auto&& s : rtTableSources)
	{
		FillMeshTable(s.pResMesh);
	}

	// create shader table.
	auto Align = [](UINT size, UINT align)
	{
		return ((size + align - 1) / align) * align;
	};
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	UINT descHandleOffset = Align(shaderIdentifierSize, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	UINT shaderRecordSize = Align(descHandleOffset + sizeof(LocalTable), D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
	bvhShaderRecordSize_ = shaderRecordSize;

	auto GenShaderTable = [&](
		void* const * shaderIds,
		int tableCountPerMaterial,
		sl12::UniqueHandle<sl12::Buffer>& buffer,
		int materialCount)
	{
		buffer = sl12::MakeUnique<sl12::Buffer>(pDevice_);

		materialCount = (materialCount < 0) ? (int)material_table.size() : materialCount;
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.size = shaderRecordSize * tableCountPerMaterial * materialCount;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		if (!buffer->Initialize(pDevice_, desc))
		{
			return false;
		}

		auto p = (char*)buffer->Map();
		for (int i = 0; i < materialCount; ++i)
		{
			for (int id = 0; id < tableCountPerMaterial; ++id)
			{
				auto start = p;

				memcpy(p, shaderIds[i * tableCountPerMaterial + id], shaderIdentifierSize);
				p += descHandleOffset;

				memcpy(p, &material_table[i], sizeof(LocalTable));

				p = start + shaderRecordSize;
			}
		}
		buffer->Unmap();

		return true;
	};
	// material shader table.
	{
		void* hg_identifier[2];
		{
			ID3D12StateObjectProperties* prop;
			psoProbeTraceRT_->GetPSO()->QueryInterface(IID_PPV_ARGS(&prop));
			hg_identifier[0] = prop->GetShaderIdentifier(ProbeTrace::kMaterialOpacityHG);
			hg_identifier[1] = prop->GetShaderIdentifier(ProbeTrace::kMaterialMaskedHG);
			prop->Release();
		}
		std::vector<void*> hg_table;
		for (auto v : opaque_table)
		{
			hg_table.push_back(v ? hg_identifier[0] : hg_identifier[1]);
		}
		if (!GenShaderTable(hg_table.data(), 1, MaterialHGTable_, -1))
		{
			assert(false);
		}
	}
	// for rgs and ms.
	{
		void* rgs_identifier;
		void* ms_identifier;
		{
			ID3D12StateObjectProperties* prop;
			psoProbeTraceRT_->GetPSO()->QueryInterface(IID_PPV_ARGS(&prop));
			rgs_identifier = prop->GetShaderIdentifier(ProbeTrace::kProbeTraceRGS);
			ms_identifier = prop->GetShaderIdentifier(ProbeTrace::kProbeTraceMS);
			prop->Release();
		}
		if (!GenShaderTable(&rgs_identifier, 1, ProbeTraceRGSTable_, 1))
		{
			assert(false);
		}
		if (!GenShaderTable(&ms_identifier, 1, ProbeTraceMSTable_, 1))
		{
			assert(false);
		}
	}
}

void ProbeTracePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "ProbeTracePass");

	if (pScene_->IsRTTableDirty())
	{
		CreateShaderTable();
	}

	auto&& TempCB = pScene_->GetTemporalCBs();
	auto rtxgi = pScene_->GetRtxgiComponent();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, TempCB.hLightCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, rtxgi->GetConstantSTBView()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, rtxgi->GetIrradianceSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, rtxgi->GetDistanceSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, rtxgi->GetProbeDataSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(5, pScene_->GetIrradianceMapSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, rtxgi->GetRayDataUAV()->GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);

	D3D12_GPU_VIRTUAL_ADDRESS as_address[] = {
		pScene_->GetBvhScene()->GetGPUAddress()
	};
	pCmdList->SetRaytracingGlobalRootSignatureAndDescriptorSet(&rtGlobalRS_, &descSet, &rtDescMan_, as_address, ARRAYSIZE(as_address));

	// execute raytracing.
	sl12::DispatchRaysDesc desc{};
	desc.pso = &psoProbeTraceRT_;
	desc.hitGroupTable = &MaterialHGTable_;
	desc.missTable = &ProbeTraceMSTable_;
	desc.rayGenTable = &ProbeTraceRGSTable_;
	desc.hitGroupRecordSize = bvhShaderRecordSize_;
	desc.missRecordSize = bvhShaderRecordSize_;
	rtxgi->GetDDGIVolume()->GetRayDispatchDimensions(desc.width, desc.height, desc.depth);
	pCmdList->DispatchRays(desc);
}


//----------------
UpdateRtxgiPass::UpdateRtxgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
}

UpdateRtxgiPass::~UpdateRtxgiPass()
{
}

std::vector<sl12::TransientResource> UpdateRtxgiPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kProbeTraceDummy, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> UpdateRtxgiPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource rtxgi(kRTDummyResultID, sl12::TransientState::UnorderedAccess);

	rtxgi.desc.bIsTexture = true;
	rtxgi.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8_UNORM, 1, 1, 1, 1, 0);

	ret.push_back(rtxgi);
	return ret;
}

void UpdateRtxgiPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "UpdateRtxgiPass");

	auto rtxgi = pScene_->GetRtxgiComponent();

	rtxgi->UpdateProbes(pCmdList);
	rtxgi->RelocateProbes(pCmdList);
	rtxgi->ClassifyProbes(pCmdList);
}


//----------------
RaytracingGIPass::RaytracingGIPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoDDGI_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::ApplyDDGI));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::ApplyDDGI);

		if (!psoDDGI_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init apply DDGI pso.");
		}
	}
}

RaytracingGIPass::~RaytracingGIPass()
{
	psoDDGI_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> RaytracingGIPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kRTDummyResultID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> RaytracingGIPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource gi(kDenoiseGIID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();

	gi.desc.bIsTexture = true;
	gi.desc.textureDesc.Initialize2D(kSsgiFormat, width, height, 1, 1, 0);
	gi.desc.historyFrame = 1;

	ret.push_back(gi);

	return ret;
}

void RaytracingGIPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "RaytracingGIPass");

	auto pGBufferC = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pGiRes = pResManager->GetRenderGraphResource(kDenoiseGIID);
	auto pGbCSRV = pResManager->CreateOrGetTextureView(pGBufferC);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pGiUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGiRes);

	auto&& TempCB = pScene_->GetTemporalCBs();
	auto rtxgi = pScene_->GetRtxgiComponent();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, TempCB.hLightCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(2, TempCB.hAmbOccCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pGbCSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, rtxgi->GetConstantSTBView()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, rtxgi->GetIrradianceSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, rtxgi->GetDistanceSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(5, rtxgi->GetProbeDataSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pGiUAV->GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(psoDDGI_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
DebugDdgiPass::DebugDdgiPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DDGIDebugVV), pRenderSys->GetShader(ShaderName::DDGIDebugP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::DDGIDebugVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::DDGIDebugP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, sl12::ResourceItemMesh::GetNormalFormat(),   1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kLightAccumFormat;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh xlu pso.");
		}
	}
}

DebugDdgiPass::~DebugDdgiPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> DebugDdgiPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	return ret;
}

std::vector<sl12::TransientResource> DebugDdgiPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource accum(kLightAccumID, sl12::TransientState::RenderTarget);
	sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	accum.desc.bIsTexture = true;
	accum.desc.textureDesc.Initialize2D(kLightAccumFormat, width, height, 1, 1, 0);
	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kDepthFormat, width, height, 1, 1, 0);
	depth.desc.historyFrame = 1;

	ret.push_back(accum);
	ret.push_back(depth);

	return ret;
}

void DebugDdgiPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "DebugDdgiPass");

	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pAccumRTV = pResManager->CreateOrGetRenderTargetView(pAccumRes);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);

	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
		pAccumRTV->GetDescInfo().cpuHandle,
	};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pDepthDSV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, false, &dsv);

	// set viewport.
	D3D12_VIEWPORT vp;
	vp.TopLeftX = vp.TopLeftY = 0.0f;
	vp.Width = (float)pScene_->GetScreenWidth();
	vp.Height = (float)pScene_->GetScreenHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

	// set scissor rect.
	D3D12_RECT rect;
	rect.left = rect.top = 0;
	rect.right = pScene_->GetScreenWidth();
	rect.bottom = pScene_->GetScreenHeight();
	pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

	auto&& TempCB = pScene_->GetTemporalCBs();
	auto rtxgi = pScene_->GetRtxgiComponent();

	auto sphereRes = pScene_->GetDebugSphereMeshHandle().GetItem<sl12::ResourceItemMesh>();
	DirectX::XMMATRIX mtx = DirectX::XMMatrixScaling(10.0f, 10.0f, 10.0f);
	MeshCB cbMesh;
	cbMesh.mtxBoxTransform = sphereRes->GetMtxBoxToLocal();
	DirectX::XMStoreFloat4x4(&cbMesh.mtxLocalToWorld, mtx);
	cbMesh.mtxPrevLocalToWorld = cbMesh.mtxLocalToWorld;
	auto hMeshCB = pRenderSystem_->GetCbvManager()->GetTemporal(&cbMesh, sizeof(cbMesh));

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetVsCbv(1, hMeshCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetVsSrv(0, rtxgi->GetConstantSTBView()->GetDescInfo().cpuHandle);
	descSet.SetVsSrv(1, rtxgi->GetProbeDataSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(0, rtxgi->GetConstantSTBView()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(1, rtxgi->GetIrradianceSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(2, rtxgi->GetDistanceSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(3, rtxgi->GetProbeDataSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	// draw mesh.
	int numProbes = rtxgi->GetNumProbes();
	{
		// set vertex buffer.
		const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
			sl12::MeshManager::CreateVertexView(sphereRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
			sl12::MeshManager::CreateVertexView(sphereRes->GetNormalHandle(), 0, 0, sl12::ResourceItemMesh::GetNormalStride()),
		};
		pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

		// set index buffer.
		auto ibv = sl12::MeshManager::CreateIndexView(sphereRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
		pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto&& submeshes = sphereRes->GetSubmeshes();
		for (auto&& submesh : submeshes)
		{
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

			UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
			int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());
			for (auto&& meshlet : submesh.meshlets)
			{
				pCmdList->GetLatestCommandList()->DrawIndexedInstanced(
					meshlet.indexCount, numProbes, StartIndexLocation + meshlet.indexOffset, BaseVertexLocation, 0);
			}
		}
	}
}


//	EOF
