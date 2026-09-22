#include "xess_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"
#include "sl12/command_list.h"
#include "sl12/descriptor_set.h"
#include "sl12/string_util.h"

XessVelocityPass::XessVelocityPass(sl12::Device* device, RenderSystem* system, Scene* scene)
	: AppPassBase(device, system, scene)
{
	rs_ = sl12::MakeUnique<sl12::RootSignature>(device);
	pso_ = sl12::MakeUnique<sl12::ComputePipelineState>(device);
	rs_->Initialize(device, system->GetShader(ShaderName::XessVelocityC));

	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rs_;
		desc.pCS = system->GetShader(ShaderName::XessVelocityC);
		if (!pso_->Initialize(device, desc))
		{
			sl12::ConsolePrint("Failed to create XeSS velocity PSO.");
		}
	}
}

std::vector<sl12::TransientResource> XessVelocityPass::GetInputResources(const sl12::RenderPassID&) const
{
	return {sl12::TransientResource(kMotionVectorID, sl12::TransientState::ShaderResource)};
}
std::vector<sl12::TransientResource> XessVelocityPass::GetOutputResources(const sl12::RenderPassID&) const
{
	auto& info = pScene_->GetSceneRenderInfo();

	sl12::TransientResource output(kXessVelocityID, sl12::TransientState::UnorderedAccess);
	output.desc.textureDesc.Initialize2D(kMotionVectorFormat, info.GetRenderWidth(), info.GetRenderHeight(), 1, 1, 0);

	return {output};
}
void XessVelocityPass::Execute(sl12::CommandList* cmd, sl12::TransientResourceManager* resources, const sl12::RenderPassID&)
{
	auto source = resources->GetRenderGraphResource(kMotionVectorID);
	auto output = resources->GetRenderGraphResource(kXessVelocityID);
	auto srv = resources->CreateOrGetTextureView(source);
	auto uav = resources->CreateOrGetUnorderedAccessTextureView(output);

	sl12::DescriptorSet set;
	set.Reset();
	set.SetCsCbv(0, pScene_->GetTemporalCBs().hMotionCB.GetCBV()->GetDescInfo().cpuHandle);
	set.SetCsSrv(0, srv->GetDescInfo().cpuHandle);
	set.SetCsUav(0, uav->GetDescInfo().cpuHandle);

	auto& info = pScene_->GetSceneRenderInfo();
	cmd->GetLatestCommandList()->SetPipelineState(pso_->GetPSO());
	cmd->SetComputeRootSignatureAndDescriptorSet(&rs_, &set);
	cmd->GetLatestCommandList()->Dispatch((info.GetRenderWidth() + 7) / 8, (info.GetRenderHeight() + 7) / 8, 1);
}

std::vector<sl12::TransientResource> XessUpscalePass::GetInputResources(const sl12::RenderPassID&) const
{
	return {sl12::TransientResource(kLightAccumID, sl12::TransientState::ShaderResource),
		sl12::TransientResource(kXessVelocityID, sl12::TransientState::ShaderResource),
		sl12::TransientResource(kDepthBufferID, sl12::TransientState::ShaderResource)};
}
std::vector<sl12::TransientResource> XessUpscalePass::GetOutputResources(const sl12::RenderPassID&) const
{
	auto& info = pScene_->GetSceneRenderInfo();
	sl12::TransientResource output(kUpscaledLightAccumID, sl12::TransientState::UnorderedAccess);
	// RTV capability permits the Bilinear fallback if SDK execution fails.
	output.desc.textureDesc.Initialize2D(kLightAccumFormat, info.GetDisplayWidth(), info.GetDisplayHeight(),
		1, 1, sl12::ResourceUsage::RenderTarget | sl12::ResourceUsage::UnorderedAccess);
	return {output};
}
void XessUpscalePass::Execute(sl12::CommandList* cmd, sl12::TransientResourceManager* resources, const sl12::RenderPassID& id)
{
	auto color = resources->GetRenderGraphResource(kLightAccumID)->pTexture;
	auto velocity = resources->GetRenderGraphResource(kXessVelocityID)->pTexture;
	auto depth = resources->GetRenderGraphResource(kDepthBufferID)->pTexture;
	auto output = resources->GetRenderGraphResource(kUpscaledLightAccumID)->pTexture;

	for (auto input : {color, velocity, depth})
	{
		cmd->AddTransitionBarrier(input, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	cmd->FlushBarriers();

	bool success = pScene_->GetXess().Execute(cmd->GetLatestCommandList(), color->GetResourceDep(),
		velocity->GetResourceDep(), depth->GetResourceDep(), output->GetResourceDep());

	// 安全のためRootSigunatureとDescriptorHeapを無効化
	auto* native = cmd->GetLatestCommandList();
	native->SetComputeRootSignature(nullptr);
	native->SetGraphicsRootSignature(nullptr);
	cmd->SetDescriptorHeapDirty();

	for (auto input : {color, velocity, depth})
	{
		cmd->AddTransitionBarrier(input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}
	if (!success)
	{
		cmd->AddTransitionBarrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}
	cmd->FlushBarriers();

	if (!success) {
		UpscalePass::Execute(cmd, resources, id);
		cmd->AddTransitionBarrier(output, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->FlushBarriers();
	}
}
