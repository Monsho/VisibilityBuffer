#include "shadowmap_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"


//----------------
ShadowMapPass::ShadowMapPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::ShadowVV), nullptr, nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::ShadowVV);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_FRONT;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = false;
		desc.rasterizer.isFrontCCW = true;
		desc.rasterizer.slopeScaledDepthBias = 2.0f;

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
		desc.dsvFormat = kShadowMapFormat;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init shadow depth pso.");
		}
	}
}

ShadowMapPass::~ShadowMapPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> ShadowMapPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> ShadowMapPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource depth(kShadowMapID, sl12::TransientState::DepthStencil);

	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kShadowMapFormat, kShadowMapSize, kShadowMapSize, 1, 1, 0);
	depth.desc.textureDesc.clearDepth = 0.0f;

	ret.push_back(depth);
	return ret;
}

void ShadowMapPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "ShadowDepthPass");

	auto pShadowMap = pResManager->GetRenderGraphResource(kShadowMapID);
	auto pShadowMapDSV = pResManager->CreateOrGetDepthStencilView(pShadowMap);

	// clear rt.
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pShadowMapDSV->GetDescInfo().cpuHandle;
	float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

	// set render targets.
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);

	// set viewport.
	D3D12_VIEWPORT vp;
	vp.TopLeftX = vp.TopLeftY = 0.0f;
	vp.Width = (float)kShadowMapSize;
	vp.Height = (float)kShadowMapSize;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

	// set scissor rect.
	D3D12_RECT rect;
	rect.left = rect.top = 0;
	rect.right = kShadowMapSize;
	rect.bottom = kShadowMapSize;
	pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, pScene_->GetTemporalCBs().hShadowCB.GetCBV()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// draw meshes.
	sl12::u32 meshIndex = 0;
	for (auto&& mesh : pScene_->GetSceneMeshes())
	{
		// set mesh constant.
		descSet.SetVsCbv(1, pScene_->GetTemporalCBs().hMeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

		auto meshRes = mesh->GetParentResource();

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

			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

			UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
			int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());

			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
		}

		meshIndex++;
	}
}


//----------------
ShadowExpPass::ShadowExpPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::FullscreenVV), pRenderSys->GetShader(ShaderName::ShadowP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::FullscreenVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::ShadowP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = false;
		desc.depthStencil.isDepthWriteEnable = false;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kShadowExpFormat;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init shadow exponent pso.");
		}
	}
}

ShadowExpPass::~ShadowExpPass()
{
	pso_.Reset();
	rs_.Reset();
}
	
std::vector<sl12::TransientResource> ShadowExpPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	ret.push_back(sl12::TransientResource(kShadowMapID, sl12::TransientState::ShaderResource));

	return ret;
}

std::vector<sl12::TransientResource> ShadowExpPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource depth(kShadowExpID, sl12::TransientState::RenderTarget);

	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kShadowExpFormat, kShadowMapSize, kShadowMapSize, 1, 1, 0);

	ret.push_back(depth);
	return ret;
}

void ShadowExpPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "ShadowExp");

	auto pShadowMap = pResManager->GetRenderGraphResource(kShadowMapID);
	auto pShadowExp = pResManager->GetRenderGraphResource(kShadowExpID);
	auto pShadowMapSRV = pResManager->CreateOrGetTextureView(pShadowMap);
	auto pShadowExpRTV = pResManager->CreateOrGetRenderTargetView(pShadowExp);

	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pShadowExpRTV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetPsCbv(0, pScene_->GetTemporalCBs().hShadowCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(0, pShadowMapSRV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

	// draw fullscreen.
	pCmdList->GetLatestCommandList()->DrawInstanced(3, 1, 0, 0);
}


//----------------
ShadowExpBlurPass::ShadowExpBlurPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::FullscreenVV), pRenderSys->GetShader(ShaderName::BlurP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::FullscreenVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::BlurP);

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = false;
		desc.depthStencil.isDepthWriteEnable = false;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = kShadowExpFormat;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init blur pso.");
		}
	}
}

ShadowExpBlurPass::~ShadowExpBlurPass()
{
	pso_.Reset();
	rs_.Reset();
}
	
std::vector<sl12::TransientResource> ShadowExpBlurPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	bool bXBlur = ID == "ShadowBlurXPass";

	if (bXBlur)
	{
		ret.push_back(sl12::TransientResource(kShadowExpID, sl12::TransientState::ShaderResource));
	}
	else
	{
		ret.push_back(sl12::TransientResource(kShadowBlurID, sl12::TransientState::ShaderResource));
	}

	return ret;
	
}

std::vector<sl12::TransientResource> ShadowExpBlurPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	bool bXBlur = ID == "ShadowBlurXPass";

	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource depth(bXBlur ? kShadowBlurID : kShadowExpID, sl12::TransientState::RenderTarget);

	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(kShadowExpFormat, kShadowMapSize, kShadowMapSize, 1, 1, 0);

	ret.push_back(depth);
	return ret;
}

void ShadowExpBlurPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	bool bXBlur = ID == "ShadowBlurXPass";
	GPU_MARKER(pCmdList, 0, bXBlur ? "GaussianBlurX" : "GaussianBlurY");

	auto pBlurSrc = pResManager->GetRenderGraphResource(bXBlur ? kShadowExpID : kShadowBlurID);
	auto pBlurDst = pResManager->GetRenderGraphResource(bXBlur ? kShadowBlurID : kShadowExpID);
	auto pBlurSrcSRV = pResManager->CreateOrGetTextureView(pBlurSrc);
	auto pBlurDstRTV = pResManager->CreateOrGetRenderTargetView(pBlurDst);

	// set render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pBlurDstRTV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetPsCbv(0, pScene_->GetTemporalCBs().hBlurXCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(0, pBlurSrcSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

	// draw fullscreen.
	pCmdList->GetLatestCommandList()->DrawInstanced(3, 1, 0, 0);
}

//	EOF
