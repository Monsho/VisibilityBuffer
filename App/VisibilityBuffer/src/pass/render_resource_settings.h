#pragma once

#include "sl12/render_graph.h"

static const sl12::TransientResourceID	kGBufferAID("GBufferA");
static const sl12::TransientResourceID	kGBufferBID("GBufferB");
static const sl12::TransientResourceID	kGBufferCID("GBufferC");
static const sl12::TransientResourceID	kDepthBufferID("DepthBuffer");
static const sl12::TransientResourceID	kDepthHistoryID(kDepthBufferID, 1);
static const sl12::TransientResourceID	kShadowMapID("ShadowMap");
static const sl12::TransientResourceID	kShadowExpID("ShadowExp");
static const sl12::TransientResourceID	kShadowBlurID("ShadowBlur");
static const sl12::TransientResourceID	kMeshletIndirectArgID("MeshletIndirectArg");
static const sl12::TransientResourceID	kMiplevelFeedbackID("MiplevelFeedback");
static const sl12::TransientResourceID	kLightAccumID("LightAccum");
static const sl12::TransientResourceID	kHiZID("HiZ");
static const sl12::TransientResourceID	kSwapchainID("Swapchain");
static const sl12::TransientResourceID	kDeinterleaveDepthID("DeinterleaveDepth");
static const sl12::TransientResourceID	kDeinterleaveNormalID("DeinterleaveNormal");
static const sl12::TransientResourceID	kDeinterleaveAccumID("DeinterleaveAccum");
static const sl12::TransientResourceID	kSsaoID("SSAO");
static const sl12::TransientResourceID	kSsgiID("SSGI");
static const sl12::TransientResourceID	kDenoiseAOID("DenoiseAO");
static const sl12::TransientResourceID	kDenoiseGIID("DenoiseGI");
static const sl12::TransientResourceID	kAOHistoryID(kDenoiseAOID, 1);
static const sl12::TransientResourceID	kGIHistoryID(kDenoiseGIID, 1);

static const DXGI_FORMAT	kGBufferAFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT	kGBufferBFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT	kGBufferCFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
static const DXGI_FORMAT	kDepthFormat = DXGI_FORMAT_D32_FLOAT;
static const DXGI_FORMAT	kHiZFormat = DXGI_FORMAT_R32_FLOAT;
static const DXGI_FORMAT	kLightAccumFormat = DXGI_FORMAT_R11G11B10_FLOAT;
static const DXGI_FORMAT	kShadowMapFormat = DXGI_FORMAT_D32_FLOAT;
static const DXGI_FORMAT	kShadowExpFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const DXGI_FORMAT	kDeinterleaveDepthFormat = DXGI_FORMAT_R32_FLOAT;
static const DXGI_FORMAT	kSsaoFormat = DXGI_FORMAT_R8_UNORM;
static const DXGI_FORMAT	kSsgiFormat = DXGI_FORMAT_R11G11B10_FLOAT;

static const sl12::u32 kShadowMapSize = 2048;
static const sl12::u32 kHiZMiplevels = 6;

static const sl12::u32 kIndirectArgsBufferStride = 4 + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS); // root constant + draw indexed args.


//	EOF
