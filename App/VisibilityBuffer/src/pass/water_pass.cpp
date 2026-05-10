#include "water_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"

#define USE_IN_CPP
#include "../../shaders/cbuffer.hlsli"

WaterLightAccumCopyPass::WaterLightAccumCopyPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{}

WaterLightAccumCopyPass::~WaterLightAccumCopyPass()
{}

std::vector<sl12::TransientResource> WaterLightAccumCopyPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kLightAccumID, sl12::TransientState::CopySrc));
	ret.push_back(sl12::TransientResource(kDepthBufferID, sl12::TransientState::CopySrc));
	return ret;
}

std::vector<sl12::TransientResource> WaterLightAccumCopyPass::GetOutputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;

	sl12::TransientResource accum(kWaterLightAccumID, sl12::TransientState::CopyDst);
	sl12::TransientResource depth(kWaterDepthID, sl12::TransientState::CopyDst);

	sl12::u32 width = pScene_->GetScreenWidth();
	sl12::u32 height = pScene_->GetScreenHeight();
	accum.desc.bIsTexture = true;
	accum.desc.textureDesc.Initialize2D(kLightAccumFormat, width, height, 1, 1, 0);
	depth.desc.bIsTexture = true;
	depth.desc.textureDesc.Initialize2D(DXGI_FORMAT_R32_FLOAT, width, height, 1, 1, 0);

	ret.push_back(accum);
	ret.push_back(depth);

	return ret;
}

void WaterLightAccumCopyPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "WaterLightAccumCopyPass");

	auto pLightAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pWaterLightAccumRes = pResManager->GetRenderGraphResource(kWaterLightAccumID);
	auto pWaterDepthRes = pResManager->GetRenderGraphResource(kWaterDepthID);

	pCmdList->GetLatestCommandList()->CopyResource(
		pWaterLightAccumRes->pTexture->GetResourceDep(),
		pLightAccumRes->pTexture->GetResourceDep());
	pCmdList->GetLatestCommandList()->CopyResource(
		pWaterDepthRes->pTexture->GetResourceDep(),
		pDepthRes->pTexture->GetResourceDep());
}


WaterPass::WaterPass(sl12::Device* pDev, RenderSystem* pRenderSys, Scene* pScene)
	: AppPassBase(pDev, pRenderSys, pScene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(pDev);
	pso_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(pDev);

	rs_->Initialize(pDev, pRenderSys->GetShader(ShaderName::WaterVV), pRenderSys->GetShader(ShaderName::WaterP), nullptr, nullptr, nullptr);

	sl12::GraphicsPipelineStateDesc desc{};
	desc.pRootSignature = &rs_;
	desc.pVS = pRenderSys->GetShader(ShaderName::WaterVV);
	desc.pPS = pRenderSys->GetShader(ShaderName::WaterP);

	desc.blend.sampleMask = UINT_MAX;
	desc.blend.rtDesc[0].isBlendEnable = true;
	desc.blend.rtDesc[0].blendOpColor = D3D12_BLEND_OP_ADD;
	desc.blend.rtDesc[0].blendOpAlpha = D3D12_BLEND_OP_ADD;
	desc.blend.rtDesc[0].srcBlendColor = D3D12_BLEND_SRC_ALPHA;
	desc.blend.rtDesc[0].dstBlendColor = D3D12_BLEND_INV_SRC_ALPHA;
	desc.blend.rtDesc[0].srcBlendAlpha = D3D12_BLEND_ONE;
	desc.blend.rtDesc[0].dstBlendAlpha = D3D12_BLEND_ZERO;
	desc.blend.rtDesc[0].writeMask = 0xf;

	desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
	desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
	desc.rasterizer.isDepthClipEnable = true;
	desc.rasterizer.isFrontCCW = true;

	desc.depthStencil.isDepthEnable = true;
	desc.depthStencil.isDepthWriteEnable = true;
	desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

	desc.inputLayout.numElements = 0;
	desc.inputLayout.pElements = nullptr;
	desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	desc.numRTVs = 0;
	desc.rtvFormats[desc.numRTVs++] = kLightAccumFormat;
	desc.dsvFormat = kDepthFormat;
	desc.multisampleCount = 1;

	if (!pso_->Initialize(pDev, desc))
	{
		sl12::ConsolePrint("Error: failed to init water pso.");
	}
}

WaterPass::~WaterPass()
{
	pso_.Reset();
	rs_.Reset();
}

std::vector<sl12::TransientResource> WaterPass::GetInputResources(const sl12::RenderPassID& ID) const
{
	std::vector<sl12::TransientResource> ret;
	ret.push_back(sl12::TransientResource(kWaterLightAccumID, sl12::TransientState::ShaderResource));
	return ret;
}

std::vector<sl12::TransientResource> WaterPass::GetOutputResources(const sl12::RenderPassID& ID) const
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

void WaterPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "WaterPass");

	auto pAccumRes = pResManager->GetRenderGraphResource(kLightAccumID);
	auto pDepthRes = pResManager->GetRenderGraphResource(kDepthBufferID);
	auto pWaterLightAccumRes = pResManager->GetRenderGraphResource(kWaterLightAccumID);
	auto pWaterDepthRes = pResManager->GetRenderGraphResource(kWaterDepthID);

	auto pAccumRTV = pResManager->CreateOrGetRenderTargetView(pAccumRes);
	auto pDepthDSV = pResManager->CreateOrGetDepthStencilView(pDepthRes);
	auto pWaterLightAccumSRV = pResManager->CreateOrGetTextureView(pWaterLightAccumRes);
	auto pWaterDepthSRV = pResManager->CreateOrGetTextureView(pWaterDepthRes);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
		pAccumRTV->GetDescInfo().cpuHandle,
	};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = pDepthDSV->GetDescInfo().cpuHandle;
	pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, false, &dsv);

	D3D12_VIEWPORT vp;
	vp.TopLeftX = vp.TopLeftY = 0.0f;
	vp.Width = (float)pScene_->GetScreenWidth();
	vp.Height = (float)pScene_->GetScreenHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

	D3D12_RECT rect;
	rect.left = rect.top = 0;
	rect.right = pScene_->GetScreenWidth();
	rect.bottom = pScene_->GetScreenHeight();
	pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

	DirectX::XMFLOAT3 sceneAabbMin, sceneAabbMax;
	pScene_->GetSceneAABB(sceneAabbMin, sceneAabbMax);

	auto&& TempCBs = pScene_->GetTemporalCBs();
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetVsCbv(0, TempCBs.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetVsCbv(1, TempCBs.hWaterCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(0, TempCBs.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsCbv(1, TempCBs.hWaterCB.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(0, pWaterLightAccumSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSrv(1, pWaterDepthSRV->GetDescInfo().cpuHandle);
	descSet.SetPsSampler(0, pRenderSystem_->GetLinearClampSampler()->GetDescInfo().cpuHandle);

	pCmdList->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rs_, &descSet);
	pCmdList->GetLatestCommandList()->DrawInstanced(6, 1, 0, 0);
}

//	EOF
