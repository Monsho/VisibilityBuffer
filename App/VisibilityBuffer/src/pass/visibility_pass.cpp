#include "visibility_pass.h"
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
BufferReadyPass::BufferReadyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{}

BufferReadyPass::~BufferReadyPass()
{}

std::vector<sl12::TransientResource> BufferReadyPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

void BufferReadyPass::GatherData(Data& OutData) const
{
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto meshRes = mesh->GetParentResource();
		auto it = OutData.meshMap.find(meshRes);
		if (it == OutData.meshMap.end())
		{
			OutData.meshMap[meshRes] = OutData.meshCount;
			OutData.meshList.push_back(meshRes);
			OutData.submeshOffsets.push_back(OutData.submeshCount);
			OutData.meshCount++;
			OutData.submeshCount += (sl12::u32)meshRes->GetSubmeshes().size();

			for (auto&& submesh : meshRes->GetSubmeshes())
			{
				OutData.meshletOffsets.push_back(OutData.meshletCount);
				OutData.meshletCount += (sl12::u32)submesh.meshlets.size();
			}
		}

		for (auto&& submesh : meshRes->GetSubmeshes())
		{
			OutData.drawCallCount += (sl12::u32)submesh.meshlets.size();
		}
	}
}

std::vector<sl12::TransientResource> BufferReadyPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	Data data;
	GatherData(data);

	sl12::TransientResource ib(kInstanceBufferID, sl12::TransientState::CopyDst);
	sl12::TransientResource sb(kSubmeshBufferID, sl12::TransientState::CopyDst);
	sl12::TransientResource mb(kMeshletBufferID, sl12::TransientState::CopyDst);
	sl12::TransientResource db(kDrawCallBufferID, sl12::TransientState::CopyDst);

	ib.desc.bIsTexture = false;
	ib.desc.bufferDesc.InitializeStructured(sizeof(InstanceData), pScene_->GetSceneMeshes().size(), sl12::ResourceUsage::ShaderResource);
	sb.desc.bIsTexture = false;
	sb.desc.bufferDesc.InitializeStructured(sizeof(SubmeshData), data.submeshCount, sl12::ResourceUsage::ShaderResource);
	mb.desc.bIsTexture = false;
	mb.desc.bufferDesc.InitializeStructured(sizeof(MeshletData), data.meshletCount, sl12::ResourceUsage::ShaderResource);
	db.desc.bIsTexture = false;
	db.desc.bufferDesc.InitializeStructured(sizeof(DrawCallData), data.drawCallCount, sl12::ResourceUsage::ShaderResource);

	std::vector<sl12::TransientResource> ret;
	ret.push_back(ib);
	ret.push_back(sb);
	ret.push_back(mb);
	ret.push_back(db);
	return ret;
}

void BufferReadyPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	Data data;
	GatherData(data);

	// create copy src buffers.
	UniqueHandle<sl12::Buffer> instanceSrc = sl12::MakeUnique<sl12::Buffer>(pDevice_);
	UniqueHandle<sl12::Buffer> submeshSrc = sl12::MakeUnique<sl12::Buffer>(pDevice_);
	UniqueHandle<sl12::Buffer> meshletSrc = sl12::MakeUnique<sl12::Buffer>(pDevice_);
	UniqueHandle<sl12::Buffer> drawCallSrc = sl12::MakeUnique<sl12::Buffer>(pDevice_);
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(InstanceData), pScene_->GetSceneMeshes().size(), sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		instanceSrc->Initialize(pDevice_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(SubmeshData), data.submeshCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		submeshSrc->Initialize(pDevice_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(MeshletData), data.meshletCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		meshletSrc->Initialize(pDevice_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(DrawCallData), data.drawCallCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		drawCallSrc->Initialize(pDevice_, desc);
	}
	InstanceData* meshData = (InstanceData*)instanceSrc->Map();
	SubmeshData* submeshData = (SubmeshData*)submeshSrc->Map();
	MeshletData* meshletData = (MeshletData*)meshletSrc->Map();
	DrawCallData* drawCallData = (DrawCallData*)drawCallSrc->Map();

	// fill source.
	sl12::u32 submeshTotal = 0;
	for (auto meshRes : data.meshList)
	{
		// select pso type.
		int psoType = 0;
		if (pScene_->GetSphereMeshHandle().IsValid() && pScene_->GetSphereMeshHandle().GetItem<sl12::ResourceItemMesh>() == meshRes)
		{
			psoType = 1;
		}
		
		auto&& submeshes = meshRes->GetSubmeshes();
		for (auto&& submesh : submeshes)
		{
			sl12::u32 submeshIndexOffset = (sl12::u32)(meshRes->GetIndexHandle().offset + submesh.indexOffsetBytes);
			submeshData->materialIndex = pScene_->GetMaterialIndex(&meshRes->GetMaterials()[submesh.materialIndex]);
			submeshData->posOffset = (sl12::u32)(meshRes->GetPositionHandle().offset + submesh.positionOffsetBytes);
			submeshData->normalOffset = (sl12::u32)(meshRes->GetNormalHandle().offset + submesh.normalOffsetBytes);
			submeshData->tangentOffset = (sl12::u32)(meshRes->GetTangentHandle().offset + submesh.tangentOffsetBytes);
			submeshData->uvOffset = (sl12::u32)(meshRes->GetTexcoordHandle().offset + submesh.texcoordOffsetBytes);
			submeshData->indexOffset = submeshIndexOffset;
			submeshData++;

			sl12::u32 submeshPackedPrimOffset = (sl12::u32)(meshRes->GetMeshletPackedPrimHandle().offset + submesh.meshletPackedPrimOffsetBytes);
			sl12::u32 submeshVertexIndexOffset = (sl12::u32)(meshRes->GetMeshletVertexIndexHandle().offset + submesh.meshletVertexIndexOffsetBytes);
			for (auto&& meshlet : submesh.meshlets)
			{
				meshletData->submeshIndex = submeshTotal;
				meshletData->indexOffset = submeshIndexOffset + meshlet.indexOffset * (sl12::u32)sl12::ResourceItemMesh::GetIndexStride();
				meshletData->meshletPackedPrimCount = meshlet.primitiveCount;
				meshletData->meshletPackedPrimOffset = submeshPackedPrimOffset + meshlet.primitiveOffset * (sl12::u32)sizeof(sl12::u32);
				meshletData->meshletVertexIndexCount = meshlet.vertexIndexCount;
				meshletData->meshletVertexIndexOffset = submeshVertexIndexOffset + meshlet.vertexIndexOffset * (sl12::u32)sl12::ResourceItemMesh::GetIndexStride();
				meshletData++;
			}

			submeshTotal++;
		}
	}
	sl12::u32 instanceIndex = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto meshRes = mesh->GetParentResource();
		auto meshIndex = data.meshMap[meshRes];
		auto submeshOffset = data.submeshOffsets[meshIndex];
		
		// set mesh constant.
		DirectX::XMMATRIX l2w = DirectX::XMLoadFloat4x4(&mesh->GetMtxLocalToWorld());
		DirectX::XMMATRIX w2l = DirectX::XMMatrixInverse(nullptr, l2w);
		meshData->mtxBoxTransform = mesh->GetParentResource()->GetMtxBoxToLocal();
		meshData->mtxLocalToWorld = mesh->GetMtxLocalToWorld();
		DirectX::XMStoreFloat4x4(&meshData->mtxWorldToLocal, w2l);
		meshData++;

		auto&& submeshes = meshRes->GetSubmeshes();
		sl12::u32 submesh_count = (sl12::u32)submeshes.size();
		for (sl12::u32 i = 0; i < submesh_count; i++)
		{
			auto meshletOffset = data.meshletOffsets[submeshOffset + i];
			sl12::u32 meshlet_count = (sl12::u32)submeshes[i].meshlets.size();
			for (sl12::u32 j = 0; j < meshlet_count; j++)
			{
				drawCallData->instanceIndex = instanceIndex;
				drawCallData->meshletIndex = meshletOffset + j;
				drawCallData++;
			}
		}

		instanceIndex++;
	}
	instanceSrc->Unmap();
	submeshSrc->Unmap();
	meshletSrc->Unmap();
	drawCallSrc->Unmap();

	// copy commands.
	auto pIB = pResManager->GetRenderGraphResource(kInstanceBufferID);
	auto pSB = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMB = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDB = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(pIB->pBuffer->GetResourceDep(), 0, instanceSrc->GetResourceDep(), 0, instanceSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(pSB->pBuffer->GetResourceDep(), 0, submeshSrc->GetResourceDep(), 0, submeshSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(pMB->pBuffer->GetResourceDep(), 0, meshletSrc->GetResourceDep(), 0, meshletSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(pDB->pBuffer->GetResourceDep(), 0, drawCallSrc->GetResourceDep(), 0, drawCallSrc->GetBufferDesc().size);
}


//----------------
VisibilityVsPass::VisibilityVsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	indirectExec_ = sl12::MakeUnique<sl12::IndirectExecuter>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::VisibilityVV), pRenderSys->GetShader(ShaderName::VisibilityP), nullptr, nullptr, nullptr, 1);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::VisibilityVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::VisibilityP);

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
		desc.rtvFormats[desc.numRTVs++] = kVisibilityFormat;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init visibility pso.");
		}
	}

	// init indirect executer.
	bool bIndirectExecuterSucceeded = indirectExec_->InitializeWithConstants(pDev, sl12::IndirectType::DrawIndexed, kIndirectArgsBufferStride, &rs_);
	assert(bIndirectExecuterSucceeded);
}

VisibilityVsPass::~VisibilityVsPass()
{
	indirectExec_.Reset();
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> VisibilityVsPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kMeshletIndirectArgID, sl12::TransientState::IndirectArgument));
	
	return ret;
}

std::vector<sl12::TransientResource> VisibilityVsPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource vis(kVisBufferID, sl12::TransientState::RenderTarget);
	sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	vis.desc.bIsTexture = true;
	vis.desc.textureDesc.Initialize2D(kVisibilityFormat, width, height, 1, 1, 0);
	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kDepthFormat, width, height, 1, 1, 0);
	depth.desc.textureDesc.clearDepth = 0.0f;
	depth.desc.historyFrame = 1;

	ret.push_back(vis);
	ret.push_back(depth);
	
	return ret;
}

void VisibilityVsPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "VisibilityVsPass");

	auto pIndirectRes = pResManager->GetRenderGraphResource(kMeshletIndirectArgID);
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pVisRTV = pResManager->CreateOrGetRenderTargetView(pVisRes);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);
	
	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pVisRTV->GetDescInfo().cpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pDepthDSV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, &dsv);

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
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// draw meshes.
	sl12::u32 meshIndex = 0;
	sl12::u32 meshletTotal = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		// set mesh constant.
		descSet.SetVsCbv(1, TempCB.hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		auto meshRes = mesh->GetParentResource();

		// set vertex buffer.
		const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
			sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
		};
		pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

		// set index buffer.
		auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
		pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

		pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

		UINT meshletCnt = 0;
		for (auto&& submesh : meshRes->GetSubmeshes())
		{
			meshletCnt += (sl12::u32)submesh.meshlets.size();
		}
		pCmdList->GetLatestCommandList()->ExecuteIndirect(
			indirectExec_->GetCommandSignature(),			// command signature
			meshletCnt,										// max command count
			pIndirectRes->pBuffer->GetResourceDep(),		// argument buffer
			indirectExec_->GetStride() * meshletTotal,		// argument buffer offset
			nullptr,										// count buffer
			0);								// count buffer offset

		meshletTotal += meshletCnt;

		meshIndex++;
	}
}


//----------------
VisibilityMsPass::VisibilityMsPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso1st_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	pso2nd_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::VisibilityMesh1stA), pRenderSys->GetShader(ShaderName::VisibilityMeshM), pRenderSys->GetShader(ShaderName::VisibilityMeshP), 0);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pAS = pRenderSys->GetShader(ShaderName::VisibilityMesh1stA);
		desc.pMS = pRenderSys->GetShader(ShaderName::VisibilityMeshM);
		desc.pPS = pRenderSys->GetShader(ShaderName::VisibilityMeshP);

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

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kVisibilityFormat;
		desc.dsvFormat = kDepthFormat;
		desc.multisampleCount = 1;

		if (!pso1st_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init visibility mesh pso.");
		}

		desc.pAS = pRenderSys->GetShader(ShaderName::VisibilityMesh2ndA);
		if (!pso2nd_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init visibility mesh pso.");
		}
	}
}

VisibilityMsPass::~VisibilityMsPass()
{
	pso1st_.Reset();
	pso2nd_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> VisibilityMsPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	bool b1st = ID == kVisibilityMs1stPass;

	std::vector<sl12::TransientResource> ret;
	
	ret.push_back(sl12::TransientResource(sl12::TransientResourceID(kHiZID, b1st ? 1 : 0), sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	if (b1st)
	{}
	else
	{
		ret.push_back(sl12::TransientResource(kDrawFlagID, sl12::TransientState::ShaderResource));
	}
	
	return ret;
}

std::vector<sl12::TransientResource> VisibilityMsPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	bool b1st = ID == kVisibilityMs1stPass;

	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource vis(kVisBufferID, sl12::TransientState::RenderTarget);
	sl12::TransientResource depth(kDepthBufferID, sl12::TransientState::DepthStencil);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	
	vis.desc.bIsTexture = true;
	vis.desc.textureDesc.Initialize2D(kVisibilityFormat, width, height, 1, 1, 0);

	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kDepthFormat, width, height, 1, 1, 0);
	depth.desc.textureDesc.clearDepth = 0.0f;
	depth.desc.historyFrame = 1;

	ret.push_back(vis);
	ret.push_back(depth);

	if (b1st)
	{
		sl12::TransientResource flag(kDrawFlagID, sl12::TransientState::UnorderedAccess);
		size_t totalMeshlets = 0;
		for (auto&& mesh : pScene_->GetSceneMeshes())
		{
			auto res = mesh->GetParentResource();
			for (auto&& submesh : res->GetSubmeshes())
			{
				totalMeshlets += submesh.meshlets.size();
			}
		}
		flag.desc.bIsTexture = false;
		flag.desc.bufferDesc.InitializeByteAddress(totalMeshlets * 4, 0);
		ret.push_back(flag);
	}
	
	return ret;
}

void VisibilityMsPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	bool b1st = ID == kVisibilityMs1stPass;
	
	GPU_MARKER(pCmdList, 0, b1st ? "VisibilityMs1stPass" : "VisibilityMs2ndPass");

	auto pHiZRes = pResManager->GetRenderGraphResource(sl12::TransientResourceID(kHiZID, b1st ? 1 : 0));
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pDrawFlagRes = pResManager->GetRenderGraphResource(kDrawFlagID);

	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pVisRTV = pResManager->CreateOrGetRenderTargetView(pVisRes);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);
	
	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pVisRTV->GetDescInfo().cpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pDepthDSV->GetDescInfo().cpuHandle;
	if (b1st)
	{
		pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
	}
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, &dsv);

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

	auto pMeshMan = pRenderSystem_->GetMeshManager();
	auto&& TempCB = pScene_->GetTemporalCBs();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	// as
	descSet.SetAsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetAsCbv(1, TempCB.hFrustumCB.GetCBV()->GetDescInfo().cpuHandle);
	if (pHiZRes)
	{
		auto pHiZSRV = pResManager->CreateOrGetTextureView(pHiZRes);
		descSet.SetAsSrv(1, pHiZSRV->GetDescInfo().cpuHandle);
	}
	else
	{
		auto black = pDevice_->GetDummyTextureView(sl12::DummyTex::Black);
		descSet.SetAsSrv(1, black->GetDescInfo().cpuHandle);
	}
	if (b1st)
	{
		auto pDrawFlagUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pDrawFlagRes, 0, 0, 0, 0);
		descSet.SetAsUav(0, pDrawFlagUAV->GetDescInfo().cpuHandle);
	}
	else
	{
		auto pDrawFlagSRV = pResManager->CreateOrGetBufferView(pDrawFlagRes, 0, 0, 0);
		descSet.SetAsSrv(2, pDrawFlagSRV->GetDescInfo().cpuHandle);
	}
	// ms
	descSet.SetMsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetMsCbv(1, TempCB.hFrustumCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetMsSrv(0, pMeshMan->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetMsSrv(1, pMeshMan->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetMsSrv(2, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetMsSrv(3, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetMsSrv(4, pDrawCallSRV->GetDescInfo().cpuHandle);
	// ps
	descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	
	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState((b1st ? pso1st_ : pso2nd_)->GetPSO());

	// draw meshes.
	sl12::u32 meshIndex = 0;
	auto&& meshletCBs = pScene_->GetMeshletCBs();
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		auto meshRes = mesh->GetParentResource();

		sl12::BufferView* pMeshletBoundSrv = nullptr;
		if (pScene_->GetSuzanneMeshHandle().IsValid() && meshRes == pScene_->GetSuzanneMeshHandle().GetItem<sl12::ResourceItemMesh>())
		{
			pMeshletBoundSrv = pScene_->GetSuzanneMeshletBV();
		}
		else if (pScene_->GetSponzaMeshHandle().IsValid() && meshRes == pScene_->GetSponzaMeshHandle().GetItem<sl12::ResourceItemMesh>())
		{
			pMeshletBoundSrv = pScene_->GetSponzaMeshletBV();
		}
		else if (pScene_->GetCurtainMeshHandle().IsValid() && meshRes == pScene_->GetCurtainMeshHandle().GetItem<sl12::ResourceItemMesh>())
		{
			pMeshletBoundSrv = pScene_->GetCurtainMeshletBV();
		}
		else if (pScene_->GetSphereMeshHandle().IsValid() && meshRes == pScene_->GetSphereMeshHandle().GetItem<sl12::ResourceItemMesh>())
		{
			pMeshletBoundSrv = pScene_->GetSphereMeshletBV();
		}
		UINT meshletCnt = pMeshletBoundSrv->GetViewDesc().Buffer.NumElements;
		
		// set mesh constant.
		descSet.SetAsCbv(2, TempCB.hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetAsCbv(3, meshletCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetAsSrv(0, pMeshletBoundSrv->GetDescInfo().cpuHandle);
		descSet.SetMsCbv(2, TempCB.hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetMsCbv(3, meshletCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		pCmdList->SetMeshRootSignatureAndDescriptorSet(&rs_, &descSet);

		const UINT kLaneCount = 32;
		UINT dispatchCnt = (meshletCnt + kLaneCount - 1) / kLaneCount;
		pCmdList->GetLatestCommandList()->DispatchMesh(dispatchCnt, 1, 1);

		meshIndex++;
	}
}


//----------------
MaterialDepthPass::MaterialDepthPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::FullscreenVV), pRenderSys->GetShader(ShaderName::MatDepthP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::FullscreenVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::MatDepthP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init material depth pso.");
		}
	}
}

MaterialDepthPass::~MaterialDepthPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MaterialDepthPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> MaterialDepthPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource md(kMaterialDepthID, sl12::TransientState::DepthStencil);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	md.desc.bIsTexture = true;
	md.desc.textureDesc.Initialize2D(kMaterialDepthFormat, width, height, 1, 1, 0);
	md.desc.textureDesc.clearDepth = 0.0f;

	ret.push_back(md);
	
	return ret;
}

void MaterialDepthPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MaterialDepthPass");

	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	auto pMatDepthRes = pResManager->GetRenderGraphResource(kMaterialDepthID);
	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pMatDepthDSV = pResManager->CreateOrGetDepthStencilView(pMatDepthRes);
	
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pMatDepthDSV->GetDescInfo().cpuHandle;
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
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetPsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(1, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(2, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(3, pDrawCallSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(4, pDepthSRV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

	// draw fullscreen.
	pCmdList->GetLatestCommandList()->DrawInstanced(3, 1, 0, 0);
}


//----------------
ClassifyPass::ClassifyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoClear_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoClassify_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::ClassifyC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::ClassifyC);

		if (!psoClassify_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init classify pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::ClearArgC);

		if (!psoClear_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init clear arg pso.");
		}
	}
}

ClassifyPass::~ClassifyPass()
{
	psoClassify_.Reset();
	psoClear_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> ClassifyPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> ClassifyPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource arg(kTileArgBufferID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource index(kTileIndexBufferID, sl12::TransientState::UnorderedAccess);

	auto&& workMaterials = pScene_->GetWorkMaterials();
	UINT tileXCount = (pScene_->GetScreenWidth() + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileYCount = (pScene_->GetScreenHeight() + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileMax = tileXCount * tileYCount;

	arg.desc.bIsTexture = false;
	arg.desc.bufferDesc.InitializeByteAddress(sizeof(D3D12_DRAW_ARGUMENTS) * workMaterials.size(), 0);
	index.desc.bIsTexture = false;
	index.desc.bufferDesc.InitializeByteAddress(sizeof(sl12::u32) * tileMax * workMaterials.size(), 0);

	ret.push_back(arg);
	ret.push_back(index);
	
	return ret;
}

void ClassifyPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "ClassifyPass");

	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	auto pTileArgRes = pResManager->GetRenderGraphResource(kTileArgBufferID);
	auto pTileIndexRes = pResManager->GetRenderGraphResource(kTileIndexBufferID);
	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pTileArgUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pTileArgRes, 0, 0, 0, 0);
	auto pTileIndexUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pTileIndexRes, 0, 0, 0, 0);

	UINT x = (pScene_->GetScreenWidth() + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT y = (pScene_->GetScreenHeight() + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	sl12::u32 materialMax = (sl12::u32)pScene_->GetWorkMaterials().size();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hTileCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pTileArgUAV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(psoClear_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT t = (materialMax + 32 - 1) / 32;
	pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);
			
	// set descriptors.
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hTileCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pDrawCallSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pTileArgUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pTileIndexUAV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(psoClassify_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
MaterialTilePass::MaterialTilePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoStandard_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	psoTriplanar_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	indirectExec_ = sl12::MakeUnique<sl12::IndirectExecuter>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::MaterialTileVV), pRenderSys->GetShader(ShaderName::MaterialTileP), nullptr, nullptr, nullptr, 1);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::MaterialTileVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::MaterialTileP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = false;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_EQUAL;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kGBufferAFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferBFormat;
		desc.rtvFormats[desc.numRTVs++] = kGBufferCFormat;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.multisampleCount = 1;

		if (!psoStandard_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init material tile pso.");
		}

		desc.pPS = pRenderSys->GetShader(ShaderName::MaterialTileTriplanarP);

		if (!psoTriplanar_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init material tile triplanar pso.");
		}
	}

	// init indirect executer.
	indirectExec_->Initialize(pDev, sl12::IndirectType::Draw, 0);
}

MaterialTilePass::~MaterialTilePass()
{
	psoStandard_.Reset();
	psoTriplanar_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MaterialTilePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kTileArgBufferID, sl12::TransientState::IndirectArgument));
	ret.push_back(sl12::TransientResource(kTileIndexBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kInstanceBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> MaterialTilePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource ga(kGBufferAID, sl12::TransientState::RenderTarget);
	sl12::TransientResource gb(kGBufferBID, sl12::TransientState::RenderTarget);
	sl12::TransientResource gc(kGBufferCID, sl12::TransientState::RenderTarget);
	sl12::TransientResource depth(kMaterialDepthID, sl12::TransientState::DepthStencil);
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
	depth.desc.textureDesc.Initialize2D(kMaterialDepthFormat, width, height, 1, 1, 0);

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

void MaterialTilePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MaterialTilePass");

	auto pTileArgRes = pResManager->GetRenderGraphResource(kTileArgBufferID);
	auto pTileIndexRes = pResManager->GetRenderGraphResource(kTileIndexBufferID);
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pInstanceRes = pResManager->GetRenderGraphResource(kInstanceBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	auto pGbARes = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGbBRes = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGbCRes = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pMatDepthRes = pResManager->GetRenderGraphResource(kMaterialDepthID);
	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);

	auto pTileIndexSRV = pResManager->CreateOrGetBufferView(pTileIndexRes, 0, 0, (sl12::u32)pTileIndexRes->pBuffer->GetBufferDesc().stride);
	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pInstanceSRV = pResManager->CreateOrGetBufferView(pInstanceRes, 0, 0, (sl12::u32)pInstanceRes->pBuffer->GetBufferDesc().stride);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pGbARTV = pResManager->CreateOrGetRenderTargetView(pGbARes);
	auto pGbBRTV = pResManager->CreateOrGetRenderTargetView(pGbBRes);
	auto pGbCRTV = pResManager->CreateOrGetRenderTargetView(pGbCRes);
	auto pMatDepthDSV = pResManager->CreateOrGetDepthStencilView(pMatDepthRes);
	auto pMipUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pMipRes);
	
	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
		pGbARTV->GetDescInfo().cpuHandle,
		pGbBRTV->GetDescInfo().cpuHandle,
		pGbCRTV->GetDescInfo().cpuHandle,
	};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pMatDepthDSV->GetDescInfo().cpuHandle;
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

	// set descriptors.
	auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDetailTexHandle().GetItem<sl12::ResourceItemTextureBase>());
	auto&& tempCB = pScene_->GetTemporalCBs();
	auto&& meshMan = pRenderSystem_->GetMeshManager();
	auto&& cbvMan = pRenderSystem_->GetCbvManager();
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, tempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetVsCbv(1, tempCB.hTileCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetVsSrv(0, pTileIndexSRV->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(0, tempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(1, tempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(1, meshMan->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(2, meshMan->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(3, pInstanceSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(4, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(5, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(6, pDrawCallSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(7, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(11, detail_res->GetTextureView().GetDescInfo().cpuHandle);
	descSet.SetPsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);
	descSet.SetPsUav(0, pMipUAV->GetDescInfo().cpuHandle);

	sl12::GraphicsPipelineState* NowPSO = nullptr;

	sl12::u32 matIndex = 0;
	for (auto&& work : pScene_->GetWorkMaterials())
	{
		sl12::GraphicsPipelineState* pso = &psoStandard_;
		if (work.psoType == 1)
		{
			pso = &psoTriplanar_;
		}

		if (NowPSO != pso)
		{
			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			NowPSO = pso;
		}

		MaterialTileCB cb;
		cb.materialIndex = matIndex;
		sl12::CbvHandle hMatTileCB = cbvMan->GetTemporal(&cb, sizeof(cb));
		descSet.SetVsCbv(2, hMatTileCB.GetCBV()->GetDescInfo().cpuHandle);

		if (work.psoType == 0)
		{
			auto bc_tex_view = GetTextureView(work.pResMaterial->baseColorTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));
			auto nm_tex_view = GetTextureView(work.pResMaterial->normalTex, pDevice_->GetDummyTextureView(sl12::DummyTex::FlatNormal));
			auto orm_tex_view = GetTextureView(work.pResMaterial->ormTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));

			descSet.SetPsSrv(8, bc_tex_view->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(9, nm_tex_view->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(10, orm_tex_view->GetDescInfo().cpuHandle);
		}
		else
		{
			auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDotTexHandle().GetItem<sl12::ResourceItemTextureBase>());
			descSet.SetPsSrv(8, dot_res->GetTextureView().GetDescInfo().cpuHandle);
		}

		pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);
		pCmdList->GetLatestCommandList()->SetGraphicsRoot32BitConstant(rs_->GetRootConstantIndex(), matIndex, 0);

		pCmdList->GetLatestCommandList()->ExecuteIndirect(
			indirectExec_->GetCommandSignature(),		// command signature
			1,											// max command count
			pTileArgRes->pBuffer->GetResourceDep(),		// argument buffer
			indirectExec_->GetStride() * matIndex,		// argument buffer offset
			nullptr,									// count buffer
			0);							// count buffer offset
	
		matIndex++;
	}
}


//----------------
MaterialResolvePass::MaterialResolvePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	wgState_ = sl12::MakeUnique<sl12::WorkGraphState>(pDev);
	wgContext_ = sl12::MakeUnique<sl12::WorkGraphContext>(pDev);

	// init root signature.
	{
		sl12::RootBindlessInfo info;
		info.index_ = 0;
		info.space_ = 32;
		info.maxResources_ = 0;
		for (auto mesh : pScene->GetSceneMeshes())
		{
			info.maxResources_ += (sl12::u32)(mesh->GetParentResource()->GetMaterials().size() * 3);
		}
		info.maxResources_ += 32;	// add buffer.
		rs_->InitializeWithBindless(pDev, pRenderSys->GetShader(MaterialResolveLib), &info, 1);
	}

	// init work graph.
	{
		static LPCWSTR kProgramName = L"MaterialResolveWG";
		static LPCWSTR kEntryPoint = L"DistributeMaterialNode";

		D3D12_NODE_ID entryPoint{};
		entryPoint.Name = kEntryPoint;
		entryPoint.ArrayIndex = 0;

		sl12::WorkGraphStateDesc desc;
		desc.AddDxilLibrary(pRenderSys->GetShader(MaterialResolveLib)->GetData(), pRenderSys->GetShader(MaterialResolveLib)->GetSize(), nullptr, 0);
		desc.AddWorkGraph(kProgramName, D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES, 1, &entryPoint);
		desc.AddGlobalRootSignature(*&rs_);

		if (!wgState_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init work graph state.");
		}
		
		if (!wgContext_->Initialize(pDev, &wgState_, kProgramName))
		{
			sl12::ConsolePrint("Error: failed to init work graph context.");
		}
	}
}

MaterialResolvePass::~MaterialResolvePass()
{
	wgContext_.Reset();
	wgState_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MaterialResolvePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kInstanceBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> MaterialResolvePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource ga(kGBufferAID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource gb(kGBufferBID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource gc(kGBufferCID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource mip(kMiplevelFeedbackID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	ga.desc.bIsTexture = true;
	ga.desc.textureDesc.Initialize2D(kGBufferAFormat, width, height, 1, 1, 0);
	gb.desc.bIsTexture = true;
	gb.desc.textureDesc.Initialize2D(kGBufferBFormat, width, height, 1, 1, 0);
	gc.desc.bIsTexture = true;
	gc.desc.textureDesc.Initialize2D(kGBufferCFormat, width, height, 1, 1, 0);

	width = (width + 3) / 4;
	height = (height + 3) / 4;
	mip.desc.bIsTexture = true;
	mip.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8G8_UINT, width, height, 1, 1, 0);

	ret.push_back(ga);
	ret.push_back(gb);
	ret.push_back(gc);
	ret.push_back(mip);
	
	return ret;
}

void MaterialResolvePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	pScene_->UpdateBindlessTextures();
	
	GPU_MARKER(pCmdList, 0, "MaterialResolvePass");

	// input.
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pInstanceRes = pResManager->GetRenderGraphResource(kInstanceBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);

	// output.
	auto pGbARes = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGbBRes = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGbCRes = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);

	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pInstanceSRV = pResManager->CreateOrGetBufferView(pInstanceRes, 0, 0, (sl12::u32)pInstanceRes->pBuffer->GetBufferDesc().stride);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pGbAUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbARes);
	auto pGbBUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbBRes);
	auto pGbCUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbCRes);
	auto pMipUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pMipRes);
	
	auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDetailTexHandle().GetItem<sl12::ResourceItemTextureBase>());
	auto&& TempCB = pScene_->GetTemporalCBs();
	auto&& meshMan = pRenderSystem_->GetMeshManager();
	auto&& cbvMan = pRenderSystem_->GetCbvManager();

	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, TempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, meshMan->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, meshMan->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pInstanceSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(5, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(6, pDrawCallSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(7, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(8, pScene_->GetMaterialDataBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(9, detail_res->GetTextureView().GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pMipUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pGbAUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(2, pGbBUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(3, pGbCUAV->GetDescInfo().cpuHandle);

	// set program.
	wgContext_->SetProgram(pCmdList, D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE);

	std::vector<std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>> bindlessArrays;
	bindlessArrays.push_back(pScene_->GetBindlessTextures());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet, &bindlessArrays);

	// dispatch graph.
	struct DistributeNodeRecord
	{
		UINT GridSize[3];
	};
	DistributeNodeRecord record;
	static const int kTileSize = 8;
	record.GridSize[0] = (pScene_->GetScreenWidth() + kTileSize - 1) / kTileSize;
	record.GridSize[1] = (pScene_->GetScreenHeight() + kTileSize - 1) / kTileSize;
	record.GridSize[2] = 1;
	wgContext_->DispatchGraphCPU(pCmdList, 0, 1, sizeof(DistributeNodeRecord), &record);
}


//----------------
MaterialComputeBinningPass::MaterialComputeBinningPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoInit_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoCount_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoCountSum_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoPrefixSumInit_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoPrefixSum_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoBinning_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoFinalize_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::ClassifyC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::InitCountC);

		if (!psoInit_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init count pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::CountC);

		if (!psoCount_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to count pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::CountSumC);

		if (!psoCountSum_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to count sum pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::PrefixSumInitCS);

		if (!psoPrefixSumInit_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to init prefix sum pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::PrefixSumC);

		if (!psoPrefixSum_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to prefix sum pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::BinningC);

		if (!psoBinning_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to binning pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::FinalizeC);

		if (!psoFinalize_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to finalize pso.");
		}
	}
}

MaterialComputeBinningPass::~MaterialComputeBinningPass()
{
	psoInit_.Reset();
	psoCount_.Reset();
	psoCountSum_.Reset();
	psoPrefixSumInit_.Reset();
	psoPrefixSum_.Reset();
	psoBinning_.Reset();
	psoFinalize_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MaterialComputeBinningPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> MaterialComputeBinningPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource arg(kBinningArgBufferID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource cnt(kBinningCountBufferID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource off(kBinningOffsetBufferID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource pix(kBinningPixBufferID, sl12::TransientState::UnorderedAccess);

	auto&& workMaterials = pScene_->GetWorkMaterials();
	size_t numMaterials = workMaterials.size();
	sl12::u32 screenWidth = pScene_->GetScreenWidth();
	sl12::u32 screenHeight = pScene_->GetScreenHeight();

	arg.desc.bIsTexture = false;
	arg.desc.bufferDesc.InitializeByteAddress(sizeof(D3D12_DISPATCH_ARGUMENTS) * numMaterials, 0);
	cnt.desc.bIsTexture = false;
	cnt.desc.bufferDesc.InitializeStructured(sizeof(sl12::u32), numMaterials, 0);
	off.desc.bIsTexture = false;
	off.desc.bufferDesc.InitializeStructured(sizeof(sl12::u32), numMaterials, 0);
	pix.desc.bIsTexture = false;
	pix.desc.bufferDesc.InitializeStructured(sizeof(sl12::u32) * 2, screenWidth * screenHeight, 0);

	ret.push_back(arg);
	ret.push_back(cnt);
	ret.push_back(off);
	ret.push_back(pix);
	
	return ret;
}

void MaterialComputeBinningPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MaterialBinning");

	// inputs.
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);

	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);

	// outputs.
	auto pBinArgRes = pResManager->GetRenderGraphResource(kBinningArgBufferID);
	auto pBinCountRes = pResManager->GetRenderGraphResource(kBinningCountBufferID);
	auto pBinOffsetRes = pResManager->GetRenderGraphResource(kBinningOffsetBufferID);
	auto pBinPixRes = pResManager->GetRenderGraphResource(kBinningPixBufferID);
	
	auto pBinArgUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pBinArgRes, 0, 0, 0, 0);
	auto pBinCountUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pBinCountRes, 0, 0, sizeof(sl12::u32), 0);
	auto pBinOffsetUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pBinOffsetRes, 0, 0, sizeof(sl12::u32), 0);
	auto pBinPixUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pBinPixRes, 0, 0, sizeof(sl12::u32) * 2, 0);

	// pass only resources.
	auto&& workMaterials = pScene_->GetWorkMaterials();
	size_t numMaterials = workMaterials.size();
	size_t numBlocks = (numMaterials + 255) / 256;
	sl12::TransientResourceDesc StatusDesc;
	StatusDesc.bIsTexture = false;
	StatusDesc.bufferDesc.InitializeStructured(sizeof(sl12::u32) * 2, numBlocks, sl12::ResourceUsage::UnorderedAccess);
	
	auto pStatusB = pResManager->CreatePassOnlyResource(StatusDesc);

	auto pStatusUAV = pResManager->CreateOrGetUnorderedAccessBufferView(pStatusB, 0, 0, sizeof(sl12::u32) * 2, 0);

	sl12::u32 screenWidth = pScene_->GetScreenWidth();
	sl12::u32 screenHeight = pScene_->GetScreenHeight();

	// constant buffers.
	auto cbvMan = pRenderSystem_->GetCbvManager();
	sl12::u32 binCBData[3] = {screenWidth, screenHeight, (sl12::u32)numMaterials};
	sl12::u32 prefixCBData[2] = {(sl12::u32)numMaterials, (sl12::u32)numBlocks};
	auto binCB = cbvMan->GetTemporal(binCBData, sizeof(binCBData));
	auto prefixCB = cbvMan->GetTemporal(prefixCBData, sizeof(prefixCBData));

	// set descriptors.
	sl12::DescriptorSet binDS, prefixDS;
	binDS.Reset();
	binDS.SetCsCbv(0, binCB.GetCBV()->GetDescInfo().cpuHandle);
	binDS.SetCsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	binDS.SetCsSrv(1, pSubmeshSRV->GetDescInfo().cpuHandle);
	binDS.SetCsSrv(2, pMeshletSRV->GetDescInfo().cpuHandle);
	binDS.SetCsSrv(3, pDrawCallSRV->GetDescInfo().cpuHandle);
	binDS.SetCsSrv(4, pDepthSRV->GetDescInfo().cpuHandle);
	binDS.SetCsUav(0, pBinCountUAV->GetDescInfo().cpuHandle);
	binDS.SetCsUav(1, pBinOffsetUAV->GetDescInfo().cpuHandle);
	binDS.SetCsUav(2, pBinArgUAV->GetDescInfo().cpuHandle);
	binDS.SetCsUav(3, pBinPixUAV->GetDescInfo().cpuHandle);

	prefixDS.Reset();
	prefixDS.SetCsCbv(0, prefixCB.GetCBV()->GetDescInfo().cpuHandle);
	prefixDS.SetCsUav(0, pBinCountUAV->GetDescInfo().cpuHandle);
	prefixDS.SetCsUav(1, pBinOffsetUAV->GetDescInfo().cpuHandle);
	prefixDS.SetCsUav(2, pStatusUAV->GetDescInfo().cpuHandle);

	// count materials.
	{
		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoInit_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &binDS);

		// dispatch.
		UINT t = ((UINT)numMaterials + 32 - 1) / 32;
		pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);

		pCmdList->AddUAVBarrier(pBinCountRes->pBuffer);
		pCmdList->FlushBarriers();
		
		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoCount_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &binDS);

		// dispatch.
		static const UINT kTileX = 256;
		static const UINT kTileY = 1;
		UINT x = (screenWidth + kTileX - 1) / kTileX;
		UINT y = (screenHeight + kTileY - 1) / kTileY;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}

	// prefix sum.
	{
		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoPrefixSumInit_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &prefixDS);

		// dispatch.
		UINT t = ((UINT)numBlocks + 32 - 1) / 32;
		pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);

		pCmdList->AddUAVBarrier(pBinCountRes->pBuffer);
		pCmdList->AddUAVBarrier(pStatusB->pBuffer);
		pCmdList->FlushBarriers();

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoPrefixSum_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &prefixDS);

		// dispatch.
		pCmdList->GetLatestCommandList()->Dispatch((UINT)numBlocks, 1, 1);
	}

	// binning.
	{
		pCmdList->AddUAVBarrier(pBinOffsetRes->pBuffer);
		pCmdList->FlushBarriers();

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoBinning_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &binDS);

		// dispatch.
		static const UINT kTileX = 256;
		static const UINT kTileY = 1;
		UINT x = (screenWidth + kTileX - 1) / kTileX;
		UINT y = (screenHeight + kTileY - 1) / kTileY;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

		pCmdList->AddUAVBarrier(pBinArgRes->pBuffer);
		pCmdList->FlushBarriers();

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoFinalize_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &binDS);

		// dispatch.
		pCmdList->GetLatestCommandList()->Dispatch((UINT)numBlocks, 1, 1);
	}
}


//----------------
MaterialComputeGBufferPass::MaterialComputeGBufferPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoStandard_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoTriplanar_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	indirectExec_ = sl12::MakeUnique<sl12::IndirectExecuter>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::MatGBStandardC), 1);

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::MatGBStandardC);

		if (!psoStandard_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to gbuffer standard pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::MatGBStandardC);

		if (!psoStandard_->Initialize(pDevice_, desc))
		{
			sl12::ConsolePrint("Error: failed to gbuffer standard pso.");
		}
	}

	// init indirect executer.
	indirectExec_->Initialize(pDev, sl12::IndirectType::Dispatch, 0);
}

MaterialComputeGBufferPass::~MaterialComputeGBufferPass()
{
	psoStandard_.Reset();
	psoTriplanar_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> MaterialComputeGBufferPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kVisBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kInstanceBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSubmeshBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kMeshletBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDrawCallBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kBinningArgBufferID, sl12::TransientState::IndirectArgument));
	ret.push_back(sl12::TransientResource(kBinningCountBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kBinningOffsetBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kBinningPixBufferID, sl12::TransientState::ShaderResource));
	
	return ret;
}

std::vector<sl12::TransientResource> MaterialComputeGBufferPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource ga(kGBufferAID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource gb(kGBufferBID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource gc(kGBufferCID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource mip(kMiplevelFeedbackID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	ga.desc.bIsTexture = true;
	ga.desc.textureDesc.Initialize2D(kGBufferAFormat, width, height, 1, 1, 0);
	gb.desc.bIsTexture = true;
	gb.desc.textureDesc.Initialize2D(kGBufferBFormat, width, height, 1, 1, 0);
	gc.desc.bIsTexture = true;
	gc.desc.textureDesc.Initialize2D(kGBufferCFormat, width, height, 1, 1, 0);

	width = (width + 3) / 4;
	height = (height + 3) / 4;
	mip.desc.bIsTexture = true;
	mip.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8G8_UINT, width, height, 1, 1, 0);

	ret.push_back(ga);
	ret.push_back(gb);
	ret.push_back(gc);
	ret.push_back(mip);
	
	return ret;
}

void MaterialComputeGBufferPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "MaterialGBuffer");

	// inputs.
	auto pVisRes = pResManager->GetRenderGraphResource(kVisBufferID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pInstanceRes = pResManager->GetRenderGraphResource(kInstanceBufferID);
	auto pSubmeshRes = pResManager->GetRenderGraphResource(kSubmeshBufferID);
	auto pMeshletRes = pResManager->GetRenderGraphResource(kMeshletBufferID);
	auto pDrawCallRes = pResManager->GetRenderGraphResource(kDrawCallBufferID);
	auto pBinArgRes = pResManager->GetRenderGraphResource(kBinningArgBufferID);
	auto pBinCountRes = pResManager->GetRenderGraphResource(kBinningCountBufferID);
	auto pBinOffsetRes = pResManager->GetRenderGraphResource(kBinningOffsetBufferID);
	auto pBinPixRes = pResManager->GetRenderGraphResource(kBinningPixBufferID);

	auto pVisSRV = pResManager->CreateOrGetTextureView(pVisRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pInstanceSRV = pResManager->CreateOrGetBufferView(pInstanceRes, 0, 0, (sl12::u32)pInstanceRes->pBuffer->GetBufferDesc().stride);
	auto pSubmeshSRV = pResManager->CreateOrGetBufferView(pSubmeshRes, 0, 0, (sl12::u32)pSubmeshRes->pBuffer->GetBufferDesc().stride);
	auto pMeshletSRV = pResManager->CreateOrGetBufferView(pMeshletRes, 0, 0, (sl12::u32)pMeshletRes->pBuffer->GetBufferDesc().stride);
	auto pDrawCallSRV = pResManager->CreateOrGetBufferView(pDrawCallRes, 0, 0, (sl12::u32)pDrawCallRes->pBuffer->GetBufferDesc().stride);
	auto pBinCountSRV = pResManager->CreateOrGetBufferView(pBinCountRes, 0, 0, (sl12::u32)pBinCountRes->pBuffer->GetBufferDesc().stride);
	auto pBinOffsetSRV = pResManager->CreateOrGetBufferView(pBinOffsetRes, 0, 0, (sl12::u32)pBinOffsetRes->pBuffer->GetBufferDesc().stride);
	auto pBinPixSRV = pResManager->CreateOrGetBufferView(pBinPixRes, 0, 0, (sl12::u32)pBinPixRes->pBuffer->GetBufferDesc().stride);

	// outputs.
	auto pGbARes = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGbBRes = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGbCRes = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);

	auto pGbAUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbARes);
	auto pGbBUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbBRes);
	auto pGbCUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pGbCRes);
	auto pMipUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pMipRes);
	
	auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDetailTexHandle().GetItem<sl12::ResourceItemTextureBase>());
	auto&& TempCB = pScene_->GetTemporalCBs();
	auto&& meshMan = pRenderSystem_->GetMeshManager();
	auto&& cbvMan = pRenderSystem_->GetCbvManager();

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, TempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pVisSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, meshMan->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, meshMan->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pInstanceSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, pSubmeshSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(5, pMeshletSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(6, pDrawCallSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(7, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(8, pBinCountSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(9, pBinOffsetSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(10, pBinPixSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(14, detail_res->GetTextureView().GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearWrapSampler()->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pMipUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pGbAUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(2, pGbBUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(3, pGbCUAV->GetDescInfo().cpuHandle);

	sl12::ComputePipelineState* NowPSO = nullptr;

	sl12::u32 matIndex = 0;
	for (auto&& work : pScene_->GetWorkMaterials())
	{
		sl12::ComputePipelineState* pso = &psoStandard_;
		if (work.psoType == 1)
		{
			pso = &psoTriplanar_;
		}

		if (NowPSO != pso)
		{
			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
			NowPSO = pso;
		}

		if (work.psoType == 0)
		{
			auto bc_tex_view = GetTextureView(work.pResMaterial->baseColorTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));
			auto nm_tex_view = GetTextureView(work.pResMaterial->normalTex, pDevice_->GetDummyTextureView(sl12::DummyTex::FlatNormal));
			auto orm_tex_view = GetTextureView(work.pResMaterial->ormTex, pDevice_->GetDummyTextureView(sl12::DummyTex::Black));

			descSet.SetCsSrv(11, bc_tex_view->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(12, nm_tex_view->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(13, orm_tex_view->GetDescInfo().cpuHandle);
		}
		else
		{
			auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(pScene_->GetDotTexHandle().GetItem<sl12::ResourceItemTextureBase>());
			descSet.SetCsSrv(12, dot_res->GetTextureView().GetDescInfo().cpuHandle);
		}

		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);
		pCmdList->GetLatestCommandList()->SetComputeRoot32BitConstant(rs_->GetRootConstantIndex(), matIndex, 0);

		pCmdList->GetLatestCommandList()->ExecuteIndirect(
			indirectExec_->GetCommandSignature(),		// command signature
			1,											// max command count
			pBinArgRes->pBuffer->GetResourceDep(),		// argument buffer
			indirectExec_->GetStride() * matIndex,		// argument buffer offset
			nullptr,									// count buffer
			0);							// count buffer offset
	
		matIndex++;
	}
}


//	EOF
