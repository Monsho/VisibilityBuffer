#include "indirect_light_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"


//----------------
DeinterleavePass::DeinterleavePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DeinterleaveC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::DeinterleaveC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init deinterleave pso.");
		}
	}
}

DeinterleavePass::~DeinterleavePass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> DeinterleavePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kLightAccumHistoryID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> DeinterleavePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource diDepth(kDeinterleaveDepthID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource diNormal(kDeinterleaveNormalID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource diAccum(kDeinterleaveAccumID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();

	diDepth.desc.bIsTexture = true;
	diDepth.desc.textureDesc.Initialize2D(kDeinterleaveDepthFormat, width, height, 1, 1, 0);

	diNormal.desc.bIsTexture = true;
	diNormal.desc.textureDesc.Initialize2D(kGBufferCFormat, width, height, 1, 1, 0);

	diAccum.desc.bIsTexture = true;
	diAccum.desc.textureDesc.Initialize2D(kLightAccumFormat, width, height, 1, 1, 0);

	ret.push_back(diDepth);
	ret.push_back(diNormal);
	ret.push_back(diAccum);

	return ret;
}

void DeinterleavePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "DeinterleavePass");

	auto pGBufferC = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumHistoryID);
	auto pDiDepthRes = pResManager->GetRenderGraphResource(kDeinterleaveDepthID);
	auto pDiNormalRes = pResManager->GetRenderGraphResource(kDeinterleaveNormalID);
	auto pDiAccumRes = pResManager->GetRenderGraphResource(kDeinterleaveAccumID);
	auto pGbCSRV = pResManager->CreateOrGetTextureView(pGBufferC);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pAccumSRV = pResManager->CreateOrGetTextureView(pAccumRes);
	auto pDiDepthUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pDiDepthRes);
	auto pDiNormalUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pDiNormalRes);
	auto pDiAccumUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pDiAccumRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pGbCSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, pAccumSRV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pDiDepthUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pDiNormalUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(2, pDiAccumUAV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
ScreenSpaceAOPass::ScreenSpaceAOPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoHbao_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoBitmask_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoSsgi_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoSsgiDi_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::SsaoHbaoC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::SsaoHbaoC);

		if (!psoHbao_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init hbao pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::SsaoBitmaskC);

		if (!psoBitmask_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init visibility bitmask pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::SsgiC);

		if (!psoSsgi_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssgi pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::SsgiDIC);

		if (!psoSsgiDi_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssgi deinterleave pso.");
		}
	}
}

ScreenSpaceAOPass::~ScreenSpaceAOPass()
{
	psoHbao_.Reset();
	psoBitmask_.Reset();
	psoSsgi_.Reset();
	psoSsgiDi_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> ScreenSpaceAOPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	if (type_ == 2)
	{
		if (bNeedDeinterleave_)
		{
			ret.push_back(sl12::TransientResource(kDeinterleaveDepthID, sl12::TransientState::ShaderResource));
			ret.push_back(sl12::TransientResource(kDeinterleaveNormalID, sl12::TransientState::ShaderResource));
			ret.push_back(sl12::TransientResource(kDeinterleaveAccumID, sl12::TransientState::ShaderResource));
		}
		else
		{
			ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
			ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
			ret.push_back(sl12::TransientResource(kLightAccumHistoryID, sl12::TransientState::ShaderResource));
		}
	}
	else
	{
		ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
		ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	}
	return ret;
}

std::vector<sl12::TransientResource> ScreenSpaceAOPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource ssao(kSsaoID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource ssgi(kSsgiID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();

	ssao.desc.bIsTexture = true;
	ssao.desc.textureDesc.Initialize2D(kSsaoFormat, width, height, 1, 1, 0);

	ssgi.desc.bIsTexture = true;
	ssgi.desc.textureDesc.Initialize2D(kSsgiFormat, width, height, 1, 1, 0);

	ret.push_back(ssao);
	ret.push_back(ssgi);

	return ret;
}

void ScreenSpaceAOPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "SSAO Pass");

	bool bDeinterleave = bNeedDeinterleave_ && type_ == 2;
	
	auto pNormalRes = pResManager->GetRenderGraphResource(bDeinterleave ? kDeinterleaveNormalID : kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(bDeinterleave ? kDeinterleaveDepthID : kDepthBufferID);
	auto pSsaoRes = pResManager->GetRenderGraphResource(kSsaoID);
	auto pSsgiRes = pResManager->GetRenderGraphResource(kSsgiID);
	auto pNormalSRV = pResManager->CreateOrGetTextureView(pNormalRes);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pSsaoUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pSsaoRes);
	auto pSsgiUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pSsgiRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hAmbOccCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pNormalSRV->GetDescInfo().cpuHandle);
	if (type_ == 2)
	{
		auto pAccumRes = pResManager->GetRenderGraphResource(bDeinterleave ? kDeinterleaveAccumID : kLightAccumHistoryID);
		auto pAccumSRV = pResManager->CreateOrGetTextureView(pAccumRes);
		descSet.SetCsSrv(2, pAccumSRV->GetDescInfo().cpuHandle);
	}
	descSet.SetCsUav(0, pSsaoUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pSsgiUAV->GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	// set pipeline.
	switch (type_)
	{
	case 0:
		pCmdList->GetLatestCommandList()->SetPipelineState(psoHbao_->GetPSO());
		break;
	case 1:
		pCmdList->GetLatestCommandList()->SetPipelineState(psoBitmask_->GetPSO());
		break;
	case 2:
		if (bDeinterleave)
			pCmdList->GetLatestCommandList()->SetPipelineState(psoSsgiDi_->GetPSO());
		else
			pCmdList->GetLatestCommandList()->SetPipelineState(psoSsgi_->GetPSO());
		break;
	}
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
DenoisePass::DenoisePass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	psoAO_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	psoGI_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::DenoiseC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::DenoiseC);

		if (!psoAO_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssao denoise pso.");
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::DenoiseWithGIC);

		if (!psoGI_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssgi denoise pso.");
		}
	}
}

DenoisePass::~DenoisePass()
{
	psoAO_.Reset();
	psoGI_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> DenoisePass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthHistoryID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSsaoID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kAOHistoryID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kSsgiID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kGIHistoryID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> DenoisePass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	sl12::TransientResource ao(kDenoiseAOID, sl12::TransientState::UnorderedAccess);
	sl12::TransientResource gi(kDenoiseGIID, sl12::TransientState::UnorderedAccess);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();

	ao.desc.bIsTexture = true;
	ao.desc.textureDesc.Initialize2D(kSsaoFormat, width, height, 1, 1, 0);
	ao.desc.historyFrame = 1;

	gi.desc.bIsTexture = true;
	gi.desc.textureDesc.Initialize2D(kSsgiFormat, width, height, 1, 1, 0);
	gi.desc.historyFrame = 1;

	ret.push_back(ao);
	ret.push_back(gi);

	return ret;
}

void DenoisePass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "DenoisePass");

	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pSsaoRes = pResManager->GetRenderGraphResource(kSsaoID);
	auto pSsgiRes = pResManager->GetRenderGraphResource(kSsgiID);
	auto pPrevDepthRes = pResManager->GetRenderGraphResource(kDepthHistoryID);
	auto pPrevSsaoRes = pResManager->GetRenderGraphResource(kAOHistoryID);
	auto pPrevSsgiRes = pResManager->GetRenderGraphResource(kGIHistoryID);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pSsaoSRV = pResManager->CreateOrGetTextureView(pSsaoRes);
	auto pSsgiSRV = pResManager->CreateOrGetTextureView(pSsgiRes);
	auto pPrevDepthSRV = pPrevDepthRes ? pResManager->CreateOrGetTextureView(pPrevDepthRes) : pDepthSRV;
	auto pPrevSsaoSRV = pPrevSsaoRes ? pResManager->CreateOrGetTextureView(pPrevSsaoRes) : pSsaoSRV;
	auto pPrevSsgiSRV = pPrevSsgiRes ? pResManager->CreateOrGetTextureView(pPrevSsgiRes) : pSsgiSRV;

	auto pDenoiseAORes = pResManager->GetRenderGraphResource(kDenoiseAOID);
	auto pDenoiseGIRes = pResManager->GetRenderGraphResource(kDenoiseGIID);
	auto pDenoiseAOUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pDenoiseAORes);
	auto pDenoiseGIUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pDenoiseGIRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hAmbOccCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pSsaoSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, pPrevDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pPrevSsaoSRV->GetDescInfo().cpuHandle);
	if (type_ == 2)
	{
		descSet.SetCsSrv(4, pSsgiSRV->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(5, pPrevSsgiSRV->GetDescInfo().cpuHandle);
	}
	descSet.SetCsUav(0, pDenoiseAOUAV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(1, pDenoiseGIUAV->GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	// set pipeline.
	if (type_ == 2)
	{
		pCmdList->GetLatestCommandList()->SetPipelineState(psoGI_->GetPSO());
	}
	else
	{
		pCmdList->GetLatestCommandList()->SetPipelineState(psoAO_->GetPSO());
	}
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//----------------
IndirectLightPass::IndirectLightPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(pDev);
	
	// init root signature.
	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::IndirectC));

	// init pipeline state.
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = pRenderSys->GetShader(ShaderName::IndirectC);

		if (!pso_->Initialize(pDev, desc))
		{
			sl12::ConsolePrint("Error: failed to init indirect light pso.");
		}
	}
}

IndirectLightPass::~IndirectLightPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> IndirectLightPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kGBufferAID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kGBufferBID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kGBufferCID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDenoiseAOID, sl12::TransientState::ShaderResource));
	ret.push_back(sl12::TransientResource(kDenoiseGIID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> IndirectLightPass::GetOutputResources(const sl12::RenderPassID& ID) const
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

void IndirectLightPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 1, "IndirectLightPass");

	auto pGBufferA = pResManager->GetRenderGraphResource(kGBufferAID);
	auto pGBufferB = pResManager->GetRenderGraphResource(kGBufferBID);
	auto pGBufferC = pResManager->GetRenderGraphResource(kGBufferCID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pAoRes = pResManager->GetRenderGraphResource(kDenoiseAOID);
	auto pGiRes = pResManager->GetRenderGraphResource(kDenoiseGIID);
	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pGbASRV = pResManager->CreateOrGetTextureView(pGBufferA);
	auto pGbBSRV = pResManager->CreateOrGetTextureView(pGBufferB);
	auto pGbCSRV = pResManager->CreateOrGetTextureView(pGBufferC);
	auto pDepthSRV = pResManager->CreateOrGetTextureView(pDepthRes);
	auto pAoSRV = pResManager->CreateOrGetTextureView(pAoRes);
	auto pGiSRV = pResManager->CreateOrGetTextureView(pGiRes);
	auto pAccumUAV = pResManager->CreateOrGetUnorderedAccessTextureView(pAccumRes);
	
	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, pScene_->GetTemporalCBs().hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(1, pScene_->GetTemporalCBs().hLightCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsCbv(2, pScene_->GetTemporalCBs().hDebugCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, pGbASRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(1, pGbBSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(2, pGbCSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(3, pDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(4, pAoSRV->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(5, pGiSRV->GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, pAccumUAV->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&rs_, &descSet);

	// dispatch.
	UINT x = (pScene_->GetScreenWidth() + 7) / 8;
	UINT y = (pScene_->GetScreenHeight() + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
}


//	EOF
