#pragma once
#include "utility_pass.h"

class XessVelocityPass : public AppPassBase
{
public:
    XessVelocityPass(sl12::Device* device, RenderSystem* system, Scene* scene);
    AppPassType GetPassType() const override { return AppPassType::XessVelocity; }
    sl12::HardwareQueue::Value GetExecuteQueue() const override { return sl12::HardwareQueue::Graphics; }
    std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID&) const override;
    std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID&) const override;
    void Execute(sl12::CommandList*, sl12::TransientResourceManager*, const sl12::RenderPassID&) override;
private:
    sl12::UniqueHandle<sl12::RootSignature> rs_;
    sl12::UniqueHandle<sl12::ComputePipelineState> pso_;
};

class XessUpscalePass : public UpscalePass
{
public:
    using UpscalePass::UpscalePass;
    AppPassType GetPassType() const override { return AppPassType::XessUpscale; }
    std::vector<sl12::TransientResource> GetInputResources(const sl12::RenderPassID&) const override;
    std::vector<sl12::TransientResource> GetOutputResources(const sl12::RenderPassID&) const override;
    void Execute(sl12::CommandList*, sl12::TransientResourceManager*, const sl12::RenderPassID&) override;
};
