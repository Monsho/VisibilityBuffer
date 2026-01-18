#include "raytracing_pass.h"
#include "render_resource_settings.h"
#include "../shader_types.h"

#include "sl12/descriptor_set.h"

#define USE_IN_CPP
#include "../../shaders/cbuffer.hlsli"


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
	return ret;
}

void BuildBvhPass::Execute(sl12::CommandList* pCmdList, sl12::TransientResourceManager* pResManager, const sl12::RenderPassID& ID)
{
	GPU_MARKER(pCmdList, 0, "BuildBvhPass");
	pScene_->UpdateBVH(pCmdList);
}


//	EOF
