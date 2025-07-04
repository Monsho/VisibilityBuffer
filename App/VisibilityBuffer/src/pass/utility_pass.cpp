﻿#include "utility_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"


//----------------
ClearMiplevelPass::ClearMiplevelPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::ClearMipC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::ClearMipC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init clear mips pso.");
		}
	}
}

ClearMiplevelPass::~ClearMiplevelPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> ClearMiplevelPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

std::vector<sl12::TransientResource> ClearMiplevelPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource mip(kMiplevelFeedbackID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = (pScene_->GetScreenWidth() + 3) / 4;
	sl12::u32 height = (pScene_->GetScreenHeight() + 3) / 4;
	mip.desc.bIsTexture = true;
	mip.desc.textureDesc.Initialize2D(DXGI_FORMAT_R8G8_UINT, width, height, 1, 1, sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess);

	ret.push_back(mip);
	return ret;
}

void ClearMiplevelPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "ClearMiplevelPass");

	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);
	auto pMipUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pMipRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsUav(0, pMipUAV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT w = (pScene_->GetScreenWidth() + 3) / 4;
	UINT h = (pScene_->GetScreenHeight() + 3) / 4;
	UINT x = (w + 7) / 8;
	UINT y = (w + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

	// feedback clear.
	auto miplevelBuffer = pScene_->GetMiplevelBuffer();
	auto miplevelCopySrc = pScene_->GetMiplevelCopySrc();
	pCmdList->TransitionBarrier(miplevelBuffer, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->GetLatestCommandList()->CopyResource(miplevelBuffer->GetResourceDep(), miplevelCopySrc->GetResourceDep());
}


//----------------
FeedbackMiplevelPass::FeedbackMiplevelPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::FeedbackMipC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::FeedbackMipC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init feedback mips pso.");
		}
	}
}

FeedbackMiplevelPass::~FeedbackMiplevelPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> FeedbackMiplevelPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kMiplevelFeedbackID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> FeedbackMiplevelPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	return ret;
}

void FeedbackMiplevelPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "FeedbackMiplevelPass");

	auto pMipRes = pResManager->GetRenderGraphResource(kMiplevelFeedbackID);
	auto pMipSRV = pResManager->CreateOrGetTextureView(pMipRes);
	
	// output barrier.
	pCmdList->TransitionBarrier(pScene_->GetMiplevelBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsSrv(0, pMipSRV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pScene_->GetMiplevelUAV()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT w = (pScene_->GetScreenWidth() + 3) / 4;
	UINT h = (pScene_->GetScreenHeight() + 3) / 4;
	UINT x = (w + 7) / 8;
	UINT y = (w + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
LightingPass::LightingPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::LightingC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::LightingC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init lighting pso.");
		}
	}
}

LightingPass::~LightingPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> LightingPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kGBufferAID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kGBufferBID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
	if (!bEnableShadowExp_)
	{
		ret.push_back(sl12::TransientResource(kShadowMapID, sl12::TransientState::ShaderResource));
	}
	else
	{
		ret.push_back(sl12::TransientResource(kShadowExpID, sl12::TransientState::ShaderResource));
	}
	return ret;
}

std::vector<sl12::TransientResource> LightingPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource accum(kLightAccumID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	accum.desc.bIsTexture = true;
	accum.desc.textureDesc.Initialize2D(kLightAccumFormat, width, height, 1, 1, 0);
	accum.desc.historyFrame = 1;

	ret.push_back(accum);

	return ret;
}

void LightingPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "LightingPass");

	auto pGBufferA = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGBufferB = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGBufferC = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pShadowRes = pResManager->GetRenderGraphResource(bEnableShadowExp_ ? kShadowExpID : kShadowMapID);
	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pGbASRV = pResManager->CreateOrGetTextureView(pGBufferA);
	auto pGbBSRV = pResManager->CreateOrGetTextureView(pGBufferB);
	auto pGbCSRV = pResManager->CreateOrGetTextureView(pGBufferC);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pShadowSRV = pResManager->CreateOrGetTextureView(pShadowRes);
	auto pAccumUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pAccumRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hLightCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(2, pScene_->GetTemporalCBs().hShadowCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pGbASRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pGbBSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, pGbCSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, pShadowSRV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pAccumUAV->GetDescInfo().cpuHandle);
	auto sampler = bEnableShadowExp_ ? pRenderSystem_->GetLinearClampSampler() : pRenderSystem_->GetShadowSampler();
	descSet.SetCsSampler(0, sampler->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
HiZPass::HiZPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DepthReductionC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::DepthReductionC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init HiZ pso.");
		}
	}
}

HiZPass::~HiZPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> HiZPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> HiZPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource hiz(kHiZID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth() / 2;
	sl12::u32 height = pScene_->GetScreenHeight() / 2;
	hiz.desc.bIsTexture = true;
	hiz.desc.textureDesc.Initialize2D(kHiZFormat, width, height, kHiZMiplevels, 1, 0);
	hiz.desc.historyFrame = 1;

	ret.push_back(hiz);

	return ret;
}

void HiZPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "HiZPass");

	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pHiZRes = pResManager->GetRenderGraphResource(kHiZID);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	
	auto srv = pDepthSRV->GetDescInfo().cpuHandle;
	auto width = pScene_->GetScreenWidth() >> 2;
	auto height = pScene_->GetScreenHeight() >> 2;
	sl12::u32 i = 0;
	while (true)
	{
		auto pUAV0 = pResManager->CreateOrGetUnorderedAccessTextureView(pHiZRes, i);
		auto pUAV1 = pResManager->CreateOrGetUnorderedAccessTextureView(pHiZRes, i + 1);
		
		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsSrv(0, srv);
		descSet.SetCsUav(0, pUAV0->GetDescInfo().cpuHandle);
		descSet.SetCsUav(1, pUAV1->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

		// dispatch.
		UINT x = (width + 7) / 8 + 1;
		UINT y = (height + 7) / 8 + 1;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

		i += 2;
		if (i >= kHiZMiplevels)
			break;

		auto pSRV = pResManager->CreateOrGetTextureView(pHiZRes, i - 1);
		srv = pSRV->GetDescInfo().cpuHandle;
		width >>= 2;
		height >>= 2;

		pCmdList->AddUAVBarrier(pHiZRes->pTexture);
		pCmdList->FlushBarriers();
	}
}


//----------------
TonemapPass::TonemapPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::FullscreenVV), pRenderSys->GetShader(ShaderName::TonemapP), nullptr, nullptr, nullptr);

	// init pipeline state.
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pVS = pRenderSys->GetShader(ShaderName::FullscreenVV);
		desc.pPS = pRenderSys->GetShader(ShaderName::TonemapP);

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
		desc.rtvFormats[desc.numRTVs++] = pDev->GetSwapchain().GetTexture(0)->GetResourceDesc().Format;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init tonemap pso.");
		}
	}
}

TonemapPass::~TonemapPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> TonemapPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kLightAccumID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> TonemapPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kSwapchainID, sl12::TransientState::RenderTarget));
	return ret;
}

void TonemapPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "TonemapPass");

	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pSwapRes = pResManager->GetRenderGraphResource(kSwapchainID);
	auto pAccumSRV = pResManager->CreateOrGetTextureView(pAccumRes);
	auto pSwapRTV = pResManager->CreateOrGetRenderTargetView(pSwapRes);
	
	// set render targets.
	auto&& rtv = pSwapRTV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	
	// set viewport.
	D3D12_VIEWPORT vp;
	vp.TopLeftX = vp.TopLeftY = 0.0f;
	vp.Width = (float)width;
	vp.Height = (float)height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

	// set scissor rect.
	D3D12_RECT rect;
	rect.left = rect.top = 0;
	rect.right = width;
	rect.bottom = height;
	pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetPsSrv(0, pAccumSRV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);

	// draw fullscreen.
	pCmdList->GetLatestCommandList()->DrawInstanced(3, 1, 0, 0);
}


//	EOF
