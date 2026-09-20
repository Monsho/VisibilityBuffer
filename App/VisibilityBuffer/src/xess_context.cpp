#include "xess_context.h"
#define NOMINMAX
#include <xess/xess_d3d12.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	bool Check(xess_result_t result, const char* operation)
	{
		if (result != XESS_RESULT_SUCCESS)
		{
			std::printf("XeSS %s: %d\n", operation, static_cast<int>(result));
		}
		return result >= XESS_RESULT_SUCCESS;
	}

	float Halton(uint32_t index, uint32_t base)
	{
		float value = 0, weight = 1;
		while (index)
		{
			weight /= base;
			value += weight * (index % base);
			index /= base;
		}
		return value - 0.5f;
	}
}

bool XessContext::Configure(ID3D12Device* device, uint32_t width, uint32_t height, int quality, bool enable)
{
	// Caller waits for all submitted work before reinitializing.
	enabled_ = false;
	reset_ = true;
	frame_ = 0;

	// Keep the last frame's normalized jitter across quality/resolution changes.
	input_ = {width, height};
	if (!context_ && !Check(xessD3D12CreateContext(device, &context_), "create"))
	{
		return false;
	}

	xess_2d_t output{width, height}, minimum{}, maximum{};
	const auto preset = static_cast<xess_quality_settings_t>(106 - std::clamp(quality, 0, 6));
	if (!Check(xessGetOptimalInputResolution(context_, &output, preset, &input_, &minimum, &maximum), "resolution"))
	{
		input_ = output;
		return false;
	}
	if (!input_.x || !input_.y)
	{
		input_ = output;
		return false;
	}

	const float ratio = (std::max)(static_cast<float>(width) / static_cast<float>(input_.x), static_cast<float>(height) / static_cast<float>(input_.y));
	phaseCount_ = static_cast<unsigned>(std::ceil(8.0f * ratio * ratio));
	if (!enable)
	{
		return true;
	}

	const uint32_t flags = XESS_INIT_FLAG_INVERTED_DEPTH;
	if (bInitialized_
		&& initializedOutput_.x == width && initializedOutput_.y == height
		&& initializedQuality_ == preset && initializedFlags_ == flags)
	{
		// Resume the existing SDK instance; resetHistory is set above.
		enabled_ = true;
		return true;
	}
	if (bInitializationAttempted_)
	{
		// Never reinitialize an SDK instance, including one whose init failed.
		// Caller must finish GPU work and serialize SDK calls before destruction.
		if (!Check(xessDestroyContext(context_), "destroy"))
		{
			return false;
		}
		context_ = nullptr;
		bInitialized_ = false;
		bInitializationAttempted_ = false;
		if (!Check(xessD3D12CreateContext(device, &context_), "create"))
		{
			return false;
		}
	}

	xess_d3d12_init_params_t init{};
	init.outputResolution = output;
	init.qualitySetting = preset;
	init.initFlags = flags;
	bInitializationAttempted_ = true;
	enabled_ = Check(xessD3D12Init(context_, &init), "init");
	bInitialized_ = enabled_;
	if (bInitialized_)
	{
		initializedOutput_ = output;
		initializedQuality_ = preset;
		initializedFlags_ = flags;
	}

	return enabled_;
}

void XessContext::BeginFrame()
{
	jitter_ = {};
	if (enabled_)
	{
		uint32_t index = frame_++ % phaseCount_ + 1;
		jitter_ = {Halton(index, 2), Halton(index, 3)};
	}

	// Projection translation is +jitter in screen pixels. Remove its temporal delta.
	deltaUV_ = {previousJitterUV_.x - jitter_.x / input_.x,
				previousJitterUV_.y - jitter_.y / input_.y};
	previousJitterUV_ = {jitter_.x / input_.x, jitter_.y / input_.y};
}

bool XessContext::Execute(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color,
	ID3D12Resource* velocity, ID3D12Resource* depth, ID3D12Resource* output)
{
	xess_d3d12_execute_params_t params{};
	params.pColorTexture = color;
	params.pVelocityTexture = velocity;
	params.pDepthTexture = depth;
	params.pOutputTexture = output;
	// Same convention as the SDK sample: geometry displacement, screen Y down.
	params.jitterOffsetX = jitter_.x;
	params.jitterOffsetY = jitter_.y;
	params.exposureScale = 1.0f;
	params.resetHistory = reset_ ? 1 : 0;
	params.inputWidth = input_.x;
	params.inputHeight = input_.y;

	bool success = Check(xessD3D12Execute(context_, cmd, &params), "execute");
	reset_ = !success;
	if (!success)
	{
		enabled_ = false;
	}

	return success;
}
