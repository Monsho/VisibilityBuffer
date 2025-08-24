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
static const sl12::TransientResourceID	kLightAccumHistoryID(kLightAccumID, 1);
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
static const sl12::TransientResourceID	kVisBufferID("VisBuffer");
static const sl12::TransientResourceID	kMaterialDepthID("MaterialDepth");
static const sl12::TransientResourceID	kDrawFlagID("DrawFlag");
static const sl12::TransientResourceID	kPrevVrsID("PrevVRSBuffer");
static const sl12::TransientResourceID	kCurrVrsID("CurrVRSBuffer");

static const sl12::TransientResourceID	kInstanceBufferID("InstanceBuffer");
static const sl12::TransientResourceID	kSubmeshBufferID("SubmeshBuffer");
static const sl12::TransientResourceID	kMeshletBufferID("MeshletBuffer");
static const sl12::TransientResourceID	kDrawCallBufferID("DrawCallBuffer");
static const sl12::TransientResourceID	kTileArgBufferID("TileArgBuffer");
static const sl12::TransientResourceID	kTileIndexBufferID("TileIndexBuffer");
static const sl12::TransientResourceID	kBinningArgBufferID("BinningArgBuffer");
static const sl12::TransientResourceID	kBinningCountBufferID("BinningCountBuffer");
static const sl12::TransientResourceID	kBinningOffsetBufferID("BinningOffsetBuffer");
static const sl12::TransientResourceID	kBinningPixBufferID("BinningPixBuffer");
static const sl12::TransientResourceID	kTileBinMaterialIndexID("TileBinMaterialIndex");
static const sl12::TransientResourceID	kTileBinPixelInfoID("TileBinPixelInfo");
static const sl12::TransientResourceID	kTileBinPixelsInTileID("TileBinPixelsInTile");
static const sl12::TransientResourceID	kTileBinTileIndexID("TileBinTileIndex");
static const sl12::TransientResourceID	kTileBinArgBufferID("TileBinArgBuffer");

static const sl12::RenderPassID kMeshletArgCopyPass("MeshletArgCopyPass");
static const sl12::RenderPassID kMeshletCullingPass("MeshletCullingPass");
static const sl12::RenderPassID kClearMiplevelPass("ClearMiplevelPass");
static const sl12::RenderPassID kFeedbackMiplevelPass("FeedbackMiplevelPass");
static const sl12::RenderPassID kDepthPrePass("DepthPrePass");
static const sl12::RenderPassID kGBufferPass("GBufferPass");
static const sl12::RenderPassID kShadowMapPass("ShadowMapPass");
static const sl12::RenderPassID kShadowExpPass("ShadowExpPass");
static const sl12::RenderPassID kShadowBlurXPass("ShadowBlurXPass");
static const sl12::RenderPassID kShadowBlurYPass("ShadowBlurYPass");
static const sl12::RenderPassID kLightingPass("LightingPass");
static const sl12::RenderPassID kHiZPass("HiZPass");
static const sl12::RenderPassID kHiZafterFirstCullPass("HiZafterFirstCullPass");
static const sl12::RenderPassID kTonemapPass("TonemapPass");
static const sl12::RenderPassID kDeinterleavePass("DeinterleavePass");
static const sl12::RenderPassID kScreenSpaceAOPass("ScreenSpaceAOPass");
static const sl12::RenderPassID kDenoisePass("DenoisePass");
static const sl12::RenderPassID kIndirectLightPass("IndirectLightPass");
static const sl12::RenderPassID kBufferReadyPass("BufferReadyPass");
static const sl12::RenderPassID kVisibilityVsPass("VisibilityVsPass");
static const sl12::RenderPassID kVisibilityMs1stPass("VisibilityMs1stPass");
static const sl12::RenderPassID kVisibilityMs2ndPass("VisibilityMs2ndPass");
static const sl12::RenderPassID kMaterialDepthPass("MaterialDepthPass");
static const sl12::RenderPassID kClassifyPass("ClassifyPass");
static const sl12::RenderPassID kMaterialTilePass("MaterialTilePass");
static const sl12::RenderPassID kMaterialResolvePass("MaterialResolvePass");

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
static const DXGI_FORMAT	kVisibilityFormat = DXGI_FORMAT_R32_UINT;
static const DXGI_FORMAT	kMaterialDepthFormat = DXGI_FORMAT_D32_FLOAT;

static const sl12::u32 kShadowMapSize = 2048;
static const sl12::u32 kHiZMiplevels = 6;

static const sl12::u32 kIndirectArgsBufferStride = 4 + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS); // root constant + draw indexed args.


//	EOF
