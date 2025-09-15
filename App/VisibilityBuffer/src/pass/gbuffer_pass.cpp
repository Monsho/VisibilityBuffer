#include "gbuffer_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"

#define USE_IN_CPP
#include "../../shaders/cbuffer.hlsli"


namespace
{
	sl12::TextureView* GetTextureView(sl12::ResourceHandle resTexHandle, sl12::TextureView* pDummyView)
	{
		auto resTex = resTexHandle.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			return &const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView();
		}
		return pDummyView;
	}
}

//----------------
MeshletArgCopyPass::MeshletArgCopyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
}

MeshletArgCopyPass::~MeshletArgCopyPass()
{
}

std::vector<sl12::TransientResource> MeshletArgCopyPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> MeshletArgCopyPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	auto cbvMan = pRenderSystem_->GetCbvManager();
	auto&& meshletCBs = pScene_->GetMeshletCBs();
	sl12::u32 numMeshlets = 0;
	meshletCBs.clear();
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
		sl12::u32 cnt = 0;
		for (auto&& submesh : submeshes)
		{
			cnt += (sl12::u32)submesh.meshlets.size();
		}

		MeshletCullCB cb;
		cb.meshletStartIndex = numMeshlets;
		cb.meshletCount = cnt;
		cb.argStartAddress = numMeshlets * kIndirectArgsBufferStride;

		auto handle = cbvMan->GetTemporal(&cb, sizeof(cb));
		meshletCBs.push_back(std::move(handle));

		numMeshlets += cnt;
	}

	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource arg(kMeshletIndirectArgID, sl12::TransientState::CopyDst);

	arg.desc.bIsTexture = false;
	arg.desc.bufferDesc.heap = sl12::BufferHeap::Default;
	arg.desc.bufferDesc.size = kIndirectArgsBufferStride * numMeshlets + 4/* overflow support. */;
	arg.desc.bufferDesc.stride = kIndirectArgsBufferStride;
	arg.desc.bufferDesc.usage = sl12::ResourceUsage::UnorderedAccess;

	ret.push_back(arg);
	return ret;
}

void MeshletArgCopyPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MeshletArgCopyPass");

	// create indirect arg copy source buffer.
	if (!indirectArgUpload_.IsValid())
	{
		auto cbvMan = pRenderSystem_->GetCbvManager();
		auto&& meshletCBs = pScene_->GetMeshletCBs();

		indirectArgUpload_.Reset();
		meshletCBs.clear();

		sl12::u32 numMeshlets = 0;
		for (auto&& mesh : pScene_->GetSceneMeshes())
		{
			auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
			sl12::u32 cnt = 0;
			for (auto&& submesh : submeshes)
			{
				cnt += (sl12::u32)submesh.meshlets.size();
			}

			MeshletCullCB cb;
			cb.meshletStartIndex = numMeshlets;
			cb.meshletCount = cnt;
			cb.argStartAddress = numMeshlets * kIndirectArgsBufferStride;

			auto handle = cbvMan->GetResident(sizeof(cb));
			cbvMan->RequestResidentCopy(handle, &cb, sizeof(cb));
			meshletCBs.push_back(std::move(handle));

			numMeshlets += cnt;
		}
		cbvMan->ExecuteCopy(pCmdList);

		// create buffers.
		indirectArgUpload_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);

		sl12::BufferDesc desc{};
		desc.stride = kIndirectArgsBufferStride;
		desc.size = desc.stride * numMeshlets + 4/* overflow support. */;
		desc.usage = sl12::ResourceUsage::Unknown;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;
		indirectArgUpload_->Initialize(pDevice_, desc);

		// build upload buffer.
		sl12::u8* pUpload = static_cast<sl12::u8*>(indirectArgUpload_->Map());
		for (auto&& mesh : pScene_->GetSceneMeshes())
		{
			auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
			for (auto&& submesh : submeshes)
			{
				UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
				int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());
				
				for (auto&& meshlet : submesh.meshlets)
				{
					// root const.
					memset(pUpload, 0, sizeof(sl12::u32));
					pUpload += sizeof(sl12::u32);

					// draw arg.
					D3D12_DRAW_INDEXED_ARGUMENTS arg{};
					arg.IndexCountPerInstance = meshlet.indexCount;
					arg.InstanceCount = 1;
					arg.StartIndexLocation = StartIndexLocation + meshlet.indexOffset;
					arg.BaseVertexLocation = BaseVertexLocation;
					arg.StartInstanceLocation = 0;
					memcpy(pUpload, &arg, sizeof(arg));
					pUpload += sizeof(arg);
				}
			}
		}
		indirectArgUpload_->Unmap();
	}

	auto pArgRes = pResManager->GetRenderGraphResource(kMeshletIndirectArgID);

	// copy indirect arg buffer.
	pCmdList->GetLatestCommandList()->CopyResource(pArgRes->pBuffer->GetResourceDep(), indirectArgUpload_->GetResourceDep());
}


//----------------
MeshletCullingPass::MeshletCullingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::MeshletCullC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::MeshletCullC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init meshlet cull pso.");
		}
	}
}

MeshletCullingPass::~MeshletCullingPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MeshletCullingPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> MeshletCullingPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	auto cbvMan = pRenderSystem_->GetCbvManager();
	auto&& meshletCBs = pScene_->GetMeshletCBs();
	sl12::u32 numMeshlets = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
		sl12::u32 cnt = 0;
		for (auto&& submesh : submeshes)
		{
			cnt += (sl12::u32)submesh.meshlets.size();
		}

		MeshletCullCB cb;
		cb.meshletStartIndex = numMeshlets;
		cb.meshletCount = cnt;
		cb.argStartAddress = numMeshlets * kIndirectArgsBufferStride;

		auto handle = cbvMan->GetResident(sizeof(cb));
		cbvMan->RequestResidentCopy(handle, &cb, sizeof(cb));
		meshletCBs.push_back(std::move(handle));

		numMeshlets += cnt;
	}

	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource arg(kMeshletIndirectArgID, sl12::TransientState::UnorderedAccess);

	arg.desc.bIsTexture = false;
	arg.desc.bufferDesc.heap = sl12::BufferHeap::Default;
	arg.desc.bufferDesc.size = kIndirectArgsBufferStride * numMeshlets + 4/* overflow support. */;
	arg.desc.bufferDesc.stride = kIndirectArgsBufferStride;
	arg.desc.bufferDesc.usage = sl12::ResourceUsage::UnorderedAccess;

	ret.push_back(arg);
	return ret;
}

void MeshletCullingPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MeshletCullingPass");

	auto pArgRes = pResManager->GetRenderGraphResource(kMeshletIndirectArgID);
	auto pArgUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pArgRes, 0, 0, 0, 0);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hFrustumCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pArgUAV->GetDescInfo().cpuHandle);

	sl12::u32 meshIndex = 0;
	auto&& meshletCBs = pScene_->GetMeshletCBs();
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		// set mesh constant.
		descSet.SetCsCbv(2, pScene_->GetTemporalCBs().hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(3, meshletCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		sl12::u32 meshletCnt = 0;
		sl12::BufferView* meshletBV = pScene_->GetMeshletBoundsSRV(mesh->GetParentResource()->GetHandle());
		descSet.SetCsSrv(0, meshletBV->GetDescInfo().cpuHandle);
		meshletCnt = (sl12::u32)(meshletBV->GetViewDesc().Buffer.NumElements);

		pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

		UINT t = (meshletCnt + 32 - 1) / 32;
		pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);
		
		meshIndex++;
	}
}


//----------------
DepthPrePass::DepthPrePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rsOpaque_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	rsMasked_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoOpaque_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoOpaqueDS_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoMasked_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoMaskedDS_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	
	// init root signature.
	rsOpaque_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DepthOpaqueVV), nullptr, nullptr, nullptr, nullptr);
	rsMasked_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DepthMaskedVV), pRenderSys->GetShader(ShaderName::DepthMaskedP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsOpaque_;
		desc.pVS = pRenderSys->GetShader(ShaderName::DepthOpaqueVV);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!psoOpaque_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init depth opaque pso.");
		}

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;

		if (!psoOpaqueDS_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init depth opaque doublesided pso.");
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsMasked_;
		desc.pVS = pRenderSys->GetShader(ShaderName::DepthMaskedVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::DepthMaskedP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, sl12::ResourceItemMesh::GetTexcoordFormat(), 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!psoMasked_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init depth masked pso.");
		}

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		
		if (!psoMaskedDS_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init depth masked doublesided pso.");
		}
	}

	// init indirect executer.
	indirectExec_ = sl12::MakeUnique<sl12::IndirectExecuter>(pDev);
	bool bIndirectExecuterSucceeded = indirectExec_->Initialize(pDev, sl12::IndirectType::DrawIndexed, kIndirectArgsBufferStride);
	assert(bIndirectExecuterSucceeded);
}

DepthPrePass::~DepthPrePass()
{
	indirectExec_.Reset();
	psoOpaque_.Reset();
	psoOpaqueDS_.Reset();
	psoMasked_.Reset();
	psoMaskedDS_.Reset();
	rsOpaque_.Reset();
	rsMasked_.Reset();
}

std::vector<sl12::TransientResource> DepthPrePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kMeshletIndirectArgID, sl12::TransientState::IndirectArgument));
	
	return ret;
}

std::vector<sl12::TransientResource> DepthPrePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kDepthFormat, width, height, 1, 1, 0);
	depth.desc.textureDesc.clearDepth = 0.0f;
	depth.desc.historyFrame = 1;

	ret.push_back(depth);
	return ret;
}

void DepthPrePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "DepthPrePass");

	auto pIndirectRes = pResManager->GetRenderGraphResource(kMeshletIndirectArgID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);
	
	// clear depth.
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pDepthDSV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

	// set render targets.
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);

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

	// set descriptors.
	auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDetailTexHandle().GetItem<sl12::ResourceItemTextureBase>());
	sl12::DescriptorSet dsOpaque, dsMasked;
	dsOpaque.Reset();
	dsOpaque.SetVsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	dsMasked.Reset();
	dsMasked.SetVsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	dsMasked.SetPsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);

	sl12::GraphicsPipelineState* NowPSO = nullptr;
	sl12::RootSignature* NowRS = nullptr;
	
	// draw meshes.
	sl12::u32 meshIndex = 0;
	sl12::u32 meshletTotal = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto meshRes = mesh->GetParentResource();
		
		// set mesh constant.
		dsOpaque.SetVsCbv(1, pScene_->GetTemporalCBs().hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
		dsMasked.SetVsCbv(1, pScene_->GetTemporalCBs().hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		// set vertex buffer.
		const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
			sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
		};
		pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

		// set index buffer.
		auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
		pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

		auto&& submeshes = meshRes->GetSubmeshes();
		auto submesh_count = submeshes.size();
		for (int i = 0; i < submesh_count; i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
			sl12::u32 meshletCnt = (sl12::u32)submesh.meshlets.size();

			// select pso.
			sl12::GraphicsPipelineState* pso = nullptr;
			sl12::RootSignature* NowRS = nullptr;
			sl12::DescriptorSet* NowDS = nullptr;
			bool isSetTex = false;
			switch (material.blendType)
			{
			case sl12::ResourceMeshMaterialBlendType::Masked:
				if (material.cullMode == sl12::ResourceMeshMaterialCullMode::Back)
				{
					pso = &psoMasked_;
				}
				else
				{
					pso = &psoMaskedDS_;
				}
				NowRS = &rsMasked_;
				NowDS = &dsMasked;
				isSetTex = true;
				break;
			case sl12::ResourceMeshMaterialBlendType::Translucent:
			default:
				if (material.cullMode == sl12::ResourceMeshMaterialCullMode::Back)
				{
					pso = &psoOpaque_;
				}
				else
				{
					pso = &psoOpaqueDS_;
				}
				NowRS = &rsOpaque_;
				NowDS = &dsOpaque;
				break;
			}

			if (NowPSO != pso)
			{
				// set pipeline.
				pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
				pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				NowPSO = pso;
			}
			
			if (isSetTex)
			{
				auto bc_tex_view = GetTextureView(material.baseColorTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));
				NowDS->SetPsSrv(0, bc_tex_view->GetDescInfo().cpuHandle);
			}

			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(NowRS, NowDS);

			pCmdList->GetLatestCommandList()->ExecuteIndirect(
				indirectExec_->GetCommandSignature(),			// command signature
				meshletCnt,										// max command count
				pIndirectRes->pBuffer->GetResourceDep(),		// argument buffer
				indirectExec_->GetStride() * meshletTotal + 4,	// argument buffer offset
				nullptr,										// count buffer
				0);								// count buffer offset

			meshletTotal += meshletCnt;
		}

		meshIndex++;
	}
}


//----------------
GBufferPass::GBufferPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoMeshOpaque_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoMeshOpaqueDS_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoMeshMasked_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoMeshMaskedDS_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoTriplanar_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	indirectExec_ = sl12::MakeUnique<sl12::IndirectExecuter>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::MeshVV), pRenderSys->GetShader(ShaderName::MeshOpaqueP), nullptr, nullptr, nullptr, 1);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::MeshVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::MeshOpaqueP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, sl12::ResourceItemMesh::GetNormalFormat(),   1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TANGENT",  0, sl12::ResourceItemMesh::GetTangentFormat(),  2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, sl12::ResourceItemMesh::GetTexcoordFormat(), 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kGBufferAFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferBFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferCFormat;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!psoMeshOpaque_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh opaque pso.");
		}

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		
		if (!psoMeshOpaqueDS_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh opaque doublesided pso.");
		}

		desc.pPS = pRenderSys->GetShader(ShaderName::MeshMaskedP);
		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;

		if (!psoMeshMasked_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh masked pso.");
		}

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;

		if (!psoMeshMaskedDS_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh masked doublesided pso.");
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::TriplanarVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::TriplanarP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, sl12::ResourceItemMesh::GetPositionFormat(), 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, sl12::ResourceItemMesh::GetNormalFormat(),   1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TANGENT",  0, sl12::ResourceItemMesh::GetTangentFormat(),  2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, sl12::ResourceItemMesh::GetTexcoordFormat(), 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kGBufferAFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferBFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferCFormat;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!psoTriplanar_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init triplanar pso.");
		}
	}

	// init indirect executer.
	bool bIndirectExecuterSucceeded = indirectExec_->Initialize(pDev, sl12::IndirectType::DrawIndexed, kIndirectArgsBufferStride);
	assert(bIndirectExecuterSucceeded);
}

GBufferPass::~GBufferPass()
{
	indirectExec_.Reset();
	psoMeshOpaque_.Reset();
	psoMeshOpaqueDS_.Reset();
	psoMeshMasked_.Reset();
	psoMeshMaskedDS_.Reset();
	psoTriplanar_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> GBufferPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kMeshletIndirectArgID, sl12::TransientState::IndirectArgument));
	
	return ret;
}

std::vector<sl12::TransientResource> GBufferPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource ga(kGBufferAID, sl12::TransientState::RenderTarget);
	sl12::TransientResource gb(kGBufferBID, sl12::TransientState::RenderTarget);
	sl12::TransientResource gc(kGBufferCID, sl12::TransientState::RenderTarget);
	sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);
	sl12::TransientResource mip(kMiplevelFeedbackID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	ga.desc.bIsTexture = true;
	ga.desc.textureDesc.Initialize2D(kGBufferAFormat, width, height, 1, 1, 0);
	gb.desc.bIsTexture = true;
	gb.desc.textureDesc.Initialize2D(kGBufferBFormat, width, height, 1, 1, 0);
	gc.desc.bIsTexture = true;
	gc.desc.textureDesc.Initialize2D(kGBufferCFormat, width, height, 1, 1, 0);
	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kDepthFormat, width, height, 1, 1, 0);
	depth.desc.historyFrame = 1;

	width = (width + 3) / 4;
	height = (height + 3) / 4;
	mip.desc.bIsTexture = true;
	mip.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8G8_UINT, width, height, 1, 1, 0);

	ret.push_back(ga);
	ret.push_back(gb);
	ret.push_back(gc);
	ret.push_back(depth);
	ret.push_back(mip);
	
	return ret;
}

void GBufferPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "GBufferPass");

	auto pIndirectRes = pResManager->GetRenderGraphResource(kMeshletIndirectArgID);
	auto pGbARes = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGbBRes = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGbCRes = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);
	auto pGbARTV = pResManager->CreateOrGetRenderTargetView(pGbARes);
	auto pGbBRTV = pResManager->CreateOrGetRenderTargetView(pGbBRes);
	auto pGbCRTV = pResManager->CreateOrGetRenderTargetView(pGbCRes);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);
	auto pMipUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pMipRes);
	
	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
		pGbARTV->GetDescInfo().cpuHandle,
		pGbBRTV->GetDescInfo().cpuHandle,
		pGbCRTV->GetDescInfo().cpuHandle,
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
	
	// set descriptors.
	auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDetailTexHandle().GetItem<sl12::ResourceItemTextureBase>());
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(1, TempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
	// if (detailType_ != 3)
	if (true)
	{
		descSet.SetPsSrv(3, detail_res->GetTextureView().GetDescInfo().cpuHandle);
	}
	else
	{
		// descSet.SetPsSrv(3, detailDerivSrv_->GetDescInfo().cpuHandle);
	}
	descSet.SetPsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);
	descSet.SetPsUav(0, pMipUAV->GetDescInfo().cpuHandle);

	sl12::GraphicsPipelineState* NowPSO = nullptr;

	// draw meshes.
	sl12::u32 meshIndex = 0;
	sl12::u32 meshletTotal = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		// select pso.
		auto meshRes = mesh->GetParentResource();
		
		// set mesh constant.
		descSet.SetVsCbv(1, TempCB.hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		// set vertex buffer.
		const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
			sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
			sl12::MeshManager::CreateVertexView(meshRes->GetNormalHandle(), 0, 0, sl12::ResourceItemMesh::GetNormalStride()),
			sl12::MeshManager::CreateVertexView(meshRes->GetTangentHandle(), 0, 0, sl12::ResourceItemMesh::GetTangentStride()),
			sl12::MeshManager::CreateVertexView(meshRes->GetTexcoordHandle(), 0, 0, sl12::ResourceItemMesh::GetTexcoordStride()),
		};
		pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

		// set index buffer.
		auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
		pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

		auto&& submeshes = meshRes->GetSubmeshes();
		auto submesh_count = submeshes.size();
		for (int i = 0; i < submesh_count; i++)
		{
			auto&& submesh = submeshes[i];
			auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
			sl12::u32 meshletCnt = (sl12::u32)submesh.meshlets.size();

			// select pso.
			sl12::GraphicsPipelineState* pso = nullptr;
			if (pScene_->GetSphereMeshHandle().IsValid() && meshRes == pScene_->GetSphereMeshHandle().GetItem<sl12::ResourceItemMesh>())
			{
				pso = &psoTriplanar_;
			}
			else
			{
				switch (material.blendType)
				{
				case sl12::ResourceMeshMaterialBlendType::Masked:
					if (material.cullMode == sl12::ResourceMeshMaterialCullMode::Back)
					{
						pso = &psoMeshMasked_;
					}
					else
					{
						pso = &psoMeshMaskedDS_;
					}
					break;
				case sl12::ResourceMeshMaterialBlendType::Translucent:
				default:
					if (material.cullMode == sl12::ResourceMeshMaterialCullMode::Back)
					{
						pso = &psoMeshOpaque_;
					}
					else
					{
						pso = &psoMeshOpaqueDS_;
					}
					break;
				}
			}

			if (NowPSO != pso)
			{
				// set pipeline.
				pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
				pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				NowPSO = pso;
			}

			if (pso != &psoTriplanar_)
			{
				auto bc_tex_view = GetTextureView(material.baseColorTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));
				auto nm_tex_view = GetTextureView(material.normalTex, pDevice_->GetDummyTextureView(sl12::DummyTex::FlatNormal));
				auto orm_tex_view = GetTextureView(material.ormTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));

				descSet.SetPsSrv(0, bc_tex_view->GetDescInfo().cpuHandle);
				descSet.SetPsSrv(1, nm_tex_view->GetDescInfo().cpuHandle);
				descSet.SetPsSrv(2, orm_tex_view->GetDescInfo().cpuHandle);
			}
			else
			{
				auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDotTexHandle().GetItem<sl12::ResourceItemTextureBase>());
				descSet.SetPsSrv(0, dot_res->GetTextureView().GetDescInfo().cpuHandle);
			}

			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);
			pCmdList->GetLatestCommandList()->SetGraphicsRoot32BitConstant(rs_->GetRootConstantIndex(), (UINT)pScene_->GetMaterialIndex(&meshRes->GetMaterials()[submesh.materialIndex]), 0);

			pCmdList->GetLatestCommandList()->ExecuteIndirect(
				indirectExec_->GetCommandSignature(),			// command signature
				meshletCnt,										// max command count
				pIndirectRes->pBuffer->GetResourceDep(),		// argument buffer
				indirectExec_->GetStride() * meshletTotal + 4,	// argument buffer offset
				nullptr,										// count buffer
				0);								// count buffer offset

			meshletTotal += meshletCnt;
		}

		meshIndex++;
	}
}

//	EOF
