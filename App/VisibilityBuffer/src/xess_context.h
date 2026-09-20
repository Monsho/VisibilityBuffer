#pragma once
#include <xess/xess.h>
#include <DirectXMath.h>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// App-owned SDK lifetime; all calls are made on the render thread.
class XessContext
{
public:
    ~XessContext()
    {
        if (context_)
        {
            xessDestroyContext(context_);
        }
    }

    bool Configure(ID3D12Device* device, uint32_t width, uint32_t height, int quality, bool enable);
    void BeginFrame();
    bool Execute(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color,
        ID3D12Resource* velocity, ID3D12Resource* depth, ID3D12Resource* output);

    uint32_t InputWidth() const
    {
        return input_.x;
    }
    uint32_t InputHeight() const
    {
        return input_.y;
    }

    bool Enabled() const
    {
        return enabled_;
    }
    void ResetHistory()
    {
        reset_ = true;
    }

    DirectX::XMFLOAT2 Jitter() const
    {
        return jitter_;
    }
    DirectX::XMFLOAT2 JitterDeltaUV() const
    {
        return deltaUV_;
    }

private:
    xess_context_handle_t context_ = nullptr;
    xess_2d_t input_{};
    xess_2d_t initializedOutput_{};
    xess_quality_settings_t initializedQuality_{};

    uint32_t initializedFlags_ = 0;
    bool bInitialized_ = false;
    bool bInitializationAttempted_ = false;
    bool enabled_ = false;
    bool reset_ = true;

    uint32_t frame_ = 0;
    uint32_t phaseCount_ = 8;

    DirectX::XMFLOAT2 jitter_{};
    DirectX::XMFLOAT2 previousJitterUV_{};
    DirectX::XMFLOAT2 deltaUV_{};
};
